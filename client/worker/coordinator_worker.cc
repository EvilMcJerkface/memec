#include "worker.hh"
#include "../main/client.hh"

void ClientWorker::dispatch( CoordinatorEvent event ) {
	bool connected, isSend;
	uint16_t instanceId = Client::instanceId;
	uint32_t requestId;
	ssize_t ret;
	struct {
		size_t size;
		char *data;
	} buffer;

	if ( event.type != COORDINATOR_EVENT_TYPE_PENDING )
		requestId = ClientWorker::idGenerator->nextVal( this->workerId );

	buffer.data = this->protocol.buffer.send;

	switch( event.type ) {
		case COORDINATOR_EVENT_TYPE_REGISTER_REQUEST:
			buffer.size = this->protocol.generateAddressHeader(
				PROTO_MAGIC_REQUEST, PROTO_MAGIC_TO_COORDINATOR,
				PROTO_OPCODE_REGISTER,
				PROTO_UNINITIALIZED_INSTANCE, requestId,
				event.message.address.addr,
				event.message.address.port
			);
			isSend = true;
			break;
		case COORDINATOR_EVENT_TYPE_PUSH_LOAD_STATS:
			// TODO lock the latency when constructing msg ??
			buffer.data = this->protocol.reqPushLoadStats(
				buffer.size,
				instanceId,
				requestId,
				event.message.loading.serverGetLatency,
				event.message.loading.serverSetLatency
			);
			isSend = true;
			break;
		case COORDINATOR_EVENT_TYPE_PENDING:
			isSend = false;
			break;
		default:
			return;
	}

	if ( isSend ) {
		ret = event.socket->send( buffer.data, buffer.size, connected );
		if ( ret != ( ssize_t ) buffer.size )
			__ERROR__( "ClientWorker", "dispatch", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
	} else {
		ProtocolHeader header;
		WORKER_RECEIVE_FROM_EVENT_SOCKET();
		ArrayMap<struct sockaddr_in, Latency> getLatency, setLatency;
		struct LoadStatsHeader loadStatsHeader;
		Client *client = Client::getInstance();

		while ( buffer.size > 0 ) {
			WORKER_RECEIVE_WHOLE_MESSAGE_FROM_EVENT_SOCKET( "ClientWorker" );

			buffer.data += PROTO_HEADER_SIZE;
			buffer.size -= PROTO_HEADER_SIZE;
			// Validate message
			if ( header.from != PROTO_MAGIC_FROM_COORDINATOR ) {
				__ERROR__( "ClientWorker", "dispatch", "Invalid message source from coordinator." );
			} else {
				bool success;
				switch( header.magic ) {
					case PROTO_MAGIC_RESPONSE_SUCCESS:
						success = true;
						break;
					case PROTO_MAGIC_RESPONSE_FAILURE:
					default:
						success = false;
						break;
				}

				event.instanceId = header.instanceId;
				event.requestId = header.requestId;
				switch( header.opcode ) {
					case PROTO_OPCODE_REGISTER:
						switch( header.magic ) {
							case PROTO_MAGIC_RESPONSE_SUCCESS:
								event.socket->registered = true;
								Client::instanceId = header.instanceId;
								break;
							case PROTO_MAGIC_RESPONSE_FAILURE:
								__ERROR__( "ClientWorker", "dispatch", "Failed to register with coordinator." );
								break;
							case PROTO_MAGIC_LOADING_STATS:
								this->protocol.parseLoadStatsHeader( loadStatsHeader, buffer.data, buffer.size );
								buffer.data += PROTO_LOAD_STATS_SIZE;
								buffer.size -= PROTO_LOAD_STATS_SIZE;

								// parse the loading stats and merge with existing stats
								LOCK( &client->overloadedServer.lock );
								client->overloadedServer.serverSet.clear();
								this->protocol.parseLoadingStats( loadStatsHeader, getLatency, setLatency, client->overloadedServer.serverSet, buffer.data, buffer.size );
								UNLOCK( &client->overloadedServer.lock );
								client->mergeServerCumulativeLoading( &getLatency, &setLatency );

								buffer.data -= PROTO_LOAD_STATS_SIZE;
								buffer.size += PROTO_LOAD_STATS_SIZE;

								getLatency.needsDelete = false;
								setLatency.needsDelete = false;
								getLatency.clear();
								setLatency.clear();

								break;
							default:
								__ERROR__( "ClientWorker", "dispatch", "Invalid magic code from coordinator." );
								break;
						}
						break;
					case PROTO_OPCODE_DEGRADED_LOCK:
						this->handleDegradedLockResponse( event, success, buffer.data, buffer.size );
						break;
					case PROTO_OPCODE_REMAPPING_LOCK:
						switch( header.magic ) {
							case PROTO_MAGIC_RESPONSE_SUCCESS:
								success = true;
								break;
							case PROTO_MAGIC_RESPONSE_FAILURE:
								success = false;
								break;
							default:
								__ERROR__( "ClientWorker", "dispatch", "Invalid magic code from coordinator." );
								goto quit_1;
						}
						this->handleDegradedSetLockResponse( event, success, buffer.data, buffer.size );
						break;
					case PROTO_OPCODE_SERVER_RECONSTRUCTED:
						this->handleServerReconstructedMsg( event, buffer.data, header.length );
						break;
					default:
						__ERROR__( "ClientWorker", "dispatch", "Invalid opcode from coordinator." );
						break;
				}
			}
quit_1:
			buffer.data += header.length;
			buffer.size -= header.length;
		}

		if ( connected ) event.socket->done();
	}
	if ( ! connected )
		__ERROR__( "ClientWorker", "dispatch", "The coordinator is disconnected." );
}
