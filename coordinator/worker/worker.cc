#include "worker.hh"
#include "../ds/log.hh"
#include "../main/coordinator.hh"
#include "../../common/util/debug.hh"
#include "../../common/ds/sockaddr_in.hh"

#define WORKER_COLOR	YELLOW

uint32_t CoordinatorWorker::dataChunkCount;
uint32_t CoordinatorWorker::parityChunkCount;
uint32_t CoordinatorWorker::chunkCount;
IDGenerator *CoordinatorWorker::idGenerator;
CoordinatorEventQueue *CoordinatorWorker::eventQueue;
RemappingRecordMap *CoordinatorWorker::remappingRecords;
StripeList<SlaveSocket> *CoordinatorWorker::stripeList;
Pending *CoordinatorWorker::pending;

void CoordinatorWorker::dispatch( MixedEvent event ) {
	switch( event.type ) {
		case EVENT_TYPE_COORDINATOR:
			this->dispatch( event.event.coordinator );
			break;
		case EVENT_TYPE_MASTER:
			this->dispatch( event.event.master );
			break;
		case EVENT_TYPE_SLAVE:
			this->dispatch( event.event.slave );
			break;
		default:
			return;
	}
}

void CoordinatorWorker::dispatch( CoordinatorEvent event ) {
	Coordinator *coordinator = Coordinator::getInstance();
	struct {
		size_t size;
		char *data;
	} buffer;

	switch( event.type ) {
		case COORDINATOR_EVENT_TYPE_SYNC_REMAPPING_RECORDS:
		{
			Packet *packet;
			std::vector<Packet*> packets;
			uint32_t counter, id;
			bool empty = coordinator->pendingRemappingRecords.toSend.empty();
			MasterEvent masterEvent;

			// generate packets of remapping records
			LOCK( &coordinator->pendingRemappingRecords.toSendLock );
			// stop if there is no remapping record to sync
			if ( empty ) {
				UNLOCK( &coordinator->pendingRemappingRecords.toSendLock );
				event.message.remap.counter->clear();
				*event.message.remap.done = true;
				break;
			}
			do {
				id = coordinator->idGenerator.nextVal( this->workerId );
				coordinator->pending.addRemappingRecords( id, event.message.remap.counter, event.message.remap.done );

				packet = coordinator->packetPool.malloc();
				buffer.data = packet->data;
				this->protocol.reqSyncRemappingRecord(
					buffer.size, id,
					coordinator->pendingRemappingRecords.toSend, 0 /* no need to lock again */,
					empty, buffer.data
				);
				packet->size = buffer.size;
				packets.push_back( packet );
				// remove records sent
				std::unordered_map<Key, RemappingRecord>::iterator it, safePtr;
				for ( it = coordinator->pendingRemappingRecords.toSend.begin(); it != coordinator->pendingRemappingRecords.toSend.end(); it = safePtr ) {
					safePtr = it;
					safePtr++;
					if ( it->second.sent ) {
						coordinator->pendingRemappingRecords.toSend.erase( it );
						delete it->first.data;
					}
				}
				empty = coordinator->pendingRemappingRecords.toSend.empty();
			} while ( ! empty );
			UNLOCK( &coordinator->pendingRemappingRecords.toSendLock );

			counter = packets.size();

			// prepare to send the packets to all masters
			LOCK( &coordinator->sockets.masters.lock );
			// reference count
			for ( uint32_t pcnt = 0; pcnt < packets.size(); pcnt++ ) {
				packets[ pcnt ]->setReferenceCount( coordinator->sockets.masters.size() );
			}
			for ( uint32_t i = 0; i < coordinator->sockets.masters.size(); i++ ) {
				event.message.remap.counter->insert(
					std::pair<struct sockaddr_in, uint32_t> (
					coordinator->sockets.masters[ i ]->getAddr(), counter
					)
				);
				// event for each master
				masterEvent.syncRemappingRecords( coordinator->sockets.masters[ i ], new std::vector<Packet*>( packets ) );
				coordinator->eventQueue.insert( masterEvent );
			}
			UNLOCK( &coordinator->sockets.masters.lock );
		}
			break;
		default:
			break;
	}

}

void CoordinatorWorker::dispatch( MasterEvent event ) {
	bool connected = false, isSend, success = false;
	ssize_t ret;
	struct {
		size_t size;
		char *data;
	} buffer;
	Coordinator *coordinator = Coordinator::getInstance();
	Packet *packet = NULL;

	switch( event.type ) {
		case MASTER_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS:
			buffer.data = this->protocol.resRegisterMaster( buffer.size, event.id, true );
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_REGISTER_RESPONSE_FAILURE:
			buffer.data = this->protocol.resRegisterMaster( buffer.size, event.id, false );
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_PUSH_LOADING_STATS:
			buffer.data = this->protocol.reqPushLoadStats(
				buffer.size, 0, // id
				event.message.slaveLoading.slaveGetLatency,
				event.message.slaveLoading.slaveSetLatency,
				event.message.slaveLoading.overloadedSlaveSet
			);
			// release the ArrayMaps
			event.message.slaveLoading.slaveGetLatency->clear();
			event.message.slaveLoading.slaveSetLatency->clear();
			delete event.message.slaveLoading.slaveGetLatency;
			delete event.message.slaveLoading.slaveSetLatency;
			delete event.message.slaveLoading.overloadedSlaveSet;
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_FORWARD_REMAPPING_RECORDS:
			buffer.size = event.message.forward.prevSize;
			buffer.data = this->protocol.forwardRemappingRecords ( buffer.size, 0, event.message.forward.data );
			delete [] event.message.forward.data;
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_REMAPPING_SET_LOCK_RESPONSE_SUCCESS:
			success = true;
		case MASTER_EVENT_TYPE_REMAPPING_SET_LOCK_RESPONSE_FAILURE:
			buffer.data = this->protocol.resRemappingSetLock(
				buffer.size,
				event.id,
				success,
				event.message.remap.listId,
				event.message.remap.chunkId,
				event.message.remap.isRemapped,
				event.message.remap.key.size,
				event.message.remap.key.data,
				event.message.remap.sockfd
			);
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_SWITCH_PHASE:
			isSend = false;
			if ( event.message.remap.slaves == NULL || ! Coordinator::getInstance()->remapMsgHandler )
				break;
			// just trigger / stop the remap phase, no message need to be handled
			if ( event.message.remap.toRemap ) {
				coordinator->remapMsgHandler->startRemap( event.message.remap.slaves ); // Phase 1 --> 2
			} else {
				coordinator->remapMsgHandler->stopRemap( event.message.remap.slaves );
			}
			// free the vector of slaves
			delete event.message.remap.slaves;
			break;
		case MASTER_EVENT_TYPE_SYNC_REMAPPING_RECORDS:
			// TODO directly send packets out
		{
			std::vector<Packet*> *packets = event.message.remap.syncPackets;

			packet = packets->back();
			buffer.data = packet->data;
			buffer.size = packet->size;

			packets->pop_back();

			// check if this is the last packet to send
			if ( packets->empty() )
				delete packets;
			else
				coordinator->eventQueue.insert( event );
		}
			isSend = true;
			break;
		// Degraded operation
		case MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_IS_LOCKED:
		case MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_WAS_LOCKED:
			buffer.data = this->protocol.resDegradedLock(
				buffer.size,
				event.id,
				event.type == MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_IS_LOCKED /* success */,
				event.message.degradedLock.isSealed,
				event.message.degradedLock.key.size,
				event.message.degradedLock.key.data,
				event.message.degradedLock.listId,
				event.message.degradedLock.stripeId,
				event.message.degradedLock.srcDataChunkId,
				event.message.degradedLock.dstDataChunkId,
				event.message.degradedLock.srcParityChunkId,
				event.message.degradedLock.dstParityChunkId
			);
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_NOT_LOCKED:
		case MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_NOT_FOUND:
			buffer.data = this->protocol.resDegradedLock(
				buffer.size,
				event.id,
				event.type == MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_NOT_LOCKED /* exist */,
				event.message.degradedLock.key.size,
				event.message.degradedLock.key.data,
				event.message.degradedLock.listId,
				event.message.degradedLock.srcDataChunkId,
				event.message.degradedLock.srcParityChunkId
			);
			isSend = true;
			break;
		case MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_REMAPPED:
			buffer.data = this->protocol.resDegradedLock(
				buffer.size,
				event.id,
				event.message.degradedLock.key.size,
				event.message.degradedLock.key.data,
				event.message.degradedLock.listId,
				event.message.degradedLock.srcDataChunkId,
				event.message.degradedLock.dstDataChunkId,
				event.message.degradedLock.srcParityChunkId,
				event.message.degradedLock.dstParityChunkId
			);
			isSend = true;
			break;
		// Pending
		case MASTER_EVENT_TYPE_PENDING:
			isSend = false;
			break;
		default:
			return;
	}

	if ( isSend ) {
		ret = event.socket->send( buffer.data, buffer.size, connected );
		if ( ret != ( ssize_t ) buffer.size )
			__ERROR__( "CoordinatorWorker", "dispatch", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
		if ( event.type == MASTER_EVENT_TYPE_SYNC_REMAPPING_RECORDS && packet )
			coordinator->packetPool.free( packet );
	} else if ( event.type == MASTER_EVENT_TYPE_SWITCH_PHASE ) {
		// just to avoid error message
		connected = true;
	} else {
		ProtocolHeader header;
		WORKER_RECEIVE_FROM_EVENT_SOCKET();

		struct LoadStatsHeader loadStatsHeader;
		ArrayMap< struct sockaddr_in, Latency > getLatency, setLatency, *latencyPool = NULL;
		Coordinator *coordinator = Coordinator::getInstance();
		struct sockaddr_in masterAddr;

		while( buffer.size > 0 ) {
			WORKER_RECEIVE_WHOLE_MESSAGE_FROM_EVENT_SOCKET( "CoordinatorWorker" );

			buffer.data += PROTO_HEADER_SIZE;
			buffer.size -= PROTO_HEADER_SIZE;

			// Validate message
			if ( header.from != PROTO_MAGIC_FROM_MASTER ) {
				__ERROR__( "CoordinatorWorker", "dispatch", "Invalid message source from master." );
			}

			int index = 0;

			if ( header.magic == PROTO_MAGIC_LOADING_STATS ) {
				this->protocol.parseLoadStatsHeader( loadStatsHeader, buffer.data, buffer.size );
				buffer.data += PROTO_LOAD_STATS_SIZE;
				buffer.size -= PROTO_LOAD_STATS_SIZE;
				if ( ! this->protocol.parseLoadingStats( loadStatsHeader, getLatency, setLatency, buffer.data, buffer.size ) )
					__ERROR__( "CoordinatorWorker", "dispatch", "Invalid amount of data received from master." );
				//fprintf( stderr, "get stats GET %d SET %d\n", loadStatsHeader.slaveGetCount, loadStatsHeader.slaveSetCount );
				// set the latest loading stats
				//fprintf( stderr, "fd %d IP %u:%hu\n", event.socket->getSocket(), ntohl( event.socket->getAddr().sin_addr.s_addr ), ntohs( event.socket->getAddr().sin_port ) );

#define SET_SLAVE_LATENCY_FOR_MASTER( _MASTER_ADDR_, _SRC_, _DST_ ) \
	for ( uint32_t i = 0; i < _SRC_.size(); i++ ) { \
		coordinator->slaveLoading._DST_.get( _SRC_.keys[ i ], &index ); \
		if ( index == -1 ) { \
			coordinator->slaveLoading._DST_.set( _SRC_.keys[ i ], new ArrayMap<struct sockaddr_in, Latency> () ); \
			index = coordinator->slaveLoading._DST_.size() - 1; \
			coordinator->slaveLoading._DST_.values[ index ]->set( _MASTER_ADDR_, _SRC_.values[ i ] ); \
		} else { \
			latencyPool = coordinator->slaveLoading._DST_.values[ index ]; \
			latencyPool->get( _MASTER_ADDR_, &index ); \
			if ( index == -1 ) { \
				latencyPool->set( _MASTER_ADDR_, _SRC_.values[ i ] ); \
			} else { \
				delete latencyPool->values[ index ]; \
				latencyPool->values[ index ] = _SRC_.values[ i ]; \
			} \
		} \
	} \

				masterAddr = event.socket->getAddr();
				LOCK ( &coordinator->slaveLoading.lock );
				SET_SLAVE_LATENCY_FOR_MASTER( masterAddr, getLatency, latestGet );
				SET_SLAVE_LATENCY_FOR_MASTER( masterAddr, setLatency, latestSet );
				UNLOCK ( &coordinator->slaveLoading.lock );

				getLatency.needsDelete = false;
				setLatency.needsDelete = false;
				getLatency.clear();
				setLatency.clear();

				buffer.data -= PROTO_LOAD_STATS_SIZE;
				buffer.size += PROTO_LOAD_STATS_SIZE;
			} else if ( header.magic == PROTO_MAGIC_REQUEST ) {
				event.id = header.id;
				switch( header.opcode ) {
					case PROTO_OPCODE_REMAPPING_LOCK:
						this->handleRemappingSetLockRequest( event, buffer.data, buffer.size );
						break;
					case PROTO_OPCODE_DEGRADED_LOCK:
						this->handleDegradedLockRequest( event, buffer.data, buffer.size );
						break;
					default:
						goto quit_1;
				}
			} else if ( header.magic == PROTO_MAGIC_REMAPPING ) {
				switch( header.opcode ) {
					case PROTO_OPCODE_SYNC:
					{
						coordinator->pending.decrementRemappingRecords( header.id, event.socket->getAddr(), true, false );
						coordinator->pending.checkAndRemoveRemappingRecords( header.id, 0, false, true );
					}
						break;
					default:
						__ERROR__( "CoordinatorWorker", "dispatch", "Invalid opcode from master." );
						goto quit_1;
				}
			} else {
				__ERROR__( "CoordinatorWorker", "dispatch", "Invalid magic code from master." );
				goto quit_1;
			}

#undef SET_SLAVE_LATENCY_FOR_MASTER
quit_1:
			buffer.data += header.length;
			buffer.size -= header.length;
		}

		if ( connected ) event.socket->done();

	}

	if ( ! connected )
		__ERROR__( "CoordinatorWorker", "dispatch", "The master is disconnected." );
}

void CoordinatorWorker::dispatch( SlaveEvent event ) {
	bool connected, isSend;
	ssize_t ret;
	struct {
		size_t size;
		char *data;
	} buffer;
	uint32_t requestId;

	switch( event.type ) {
		case SLAVE_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS:
			buffer.data = this->protocol.resRegisterSlave( buffer.size, event.id, true );
			isSend = true;
			break;
		case SLAVE_EVENT_TYPE_REGISTER_RESPONSE_FAILURE:
			buffer.data = this->protocol.resRegisterSlave( buffer.size, event.id, false );
			isSend = true;
			break;
		case SLAVE_EVENT_TYPE_REQUEST_SEAL_CHUNKS:
			requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );
			buffer.data = this->protocol.reqSealChunks( buffer.size, requestId );
			isSend = true;
			break;
		case SLAVE_EVENT_TYPE_REQUEST_FLUSH_CHUNKS:
			requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );
			buffer.data = this->protocol.reqFlushChunks( buffer.size, requestId );
			isSend = true;
			break;
		case SLAVE_EVENT_TYPE_REQUEST_SYNC_META:
			requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );
			buffer.data = this->protocol.reqSyncMeta( buffer.size, requestId );
			// add sync meta request to pending set
			Coordinator::getInstance()->pending.addSyncMetaReq( requestId, event.message.sync );
			isSend = true;
			break;
		case SLAVE_EVENT_TYPE_REQUEST_RELEASE_DEGRADED_LOCK:
			this->handleReleaseDegradedLockRequest( event.socket, event.message.degraded.done );
			isSend = false;
			break;
		case SLAVE_EVENT_TYPE_PENDING:
			isSend = false;
			break;
		case SLAVE_EVENT_TYPE_ANNOUNCE_SLAVE_CONNECTED:
		case SLAVE_EVENT_TYPE_ANNOUNCE_SLAVE_RECONSTRUCTED:
			isSend = false;
			break;
		case SLAVE_EVENT_TYPE_DISCONNECT:
			isSend = false;
			break;
		default:
			return;
	}

	if ( event.type == SLAVE_EVENT_TYPE_ANNOUNCE_SLAVE_CONNECTED ) {
		ArrayMap<int, SlaveSocket> &slaves = Coordinator::getInstance()->sockets.slaves;
		uint32_t requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );

		buffer.data = this->protocol.announceSlaveConnected( buffer.size, requestId, event.socket );

		LOCK( &slaves.lock );
		for ( uint32_t i = 0; i < slaves.size(); i++ ) {
			SlaveSocket *slave = slaves.values[ i ];
			if ( event.socket->equal( slave ) || ! slave->ready() )
				continue; // No need to tell the new socket

			ret = slave->send( buffer.data, buffer.size, connected );
			if ( ret != ( ssize_t ) buffer.size )
				__ERROR__( "CoordinatorWorker", "dispatch", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
		}
		// notify the remap message handler of the new slave
		struct sockaddr_in slaveAddr = event.socket->getAddr();
		if ( Coordinator::getInstance()->remapMsgHandler )
			Coordinator::getInstance()->remapMsgHandler->addAliveSlave( slaveAddr );
		UNLOCK( &slaves.lock );
	} else if ( event.type == SLAVE_EVENT_TYPE_ANNOUNCE_SLAVE_RECONSTRUCTED ) {
		ArrayMap<int, SlaveSocket> &slaves = Coordinator::getInstance()->sockets.slaves;
		uint32_t requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );

		buffer.data = this->protocol.announceSlaveReconstructed( buffer.size, requestId, event.message.reconstructed.src, event.message.reconstructed.dst );

		LOCK( &slaves.lock );
		for ( uint32_t i = 0; i < slaves.size(); i++ ) {
			SlaveSocket *slave = slaves.values[ i ];
			if ( slave->equal( event.message.reconstructed.dst ) || ! slave->ready() )
				continue; // No need to tell the backup server

			ret = slave->send( buffer.data, buffer.size, connected );
			if ( ret != ( ssize_t ) buffer.size )
				__ERROR__( "CoordinatorWorker", "dispatch", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
		}
		// notify the remap message handler of the new slave
		struct sockaddr_in slaveAddr = event.message.reconstructed.dst->getAddr();
		if ( Coordinator::getInstance()->remapMsgHandler )
			Coordinator::getInstance()->remapMsgHandler->addAliveSlave( slaveAddr );
		UNLOCK( &slaves.lock );
	} else if ( event.type == SLAVE_EVENT_TYPE_DISCONNECT ) {
		this->handleReconstructionRequest( event.socket );
		// notify the remap message handler of a "removed" slave
		if ( Coordinator::getInstance()->remapMsgHandler )
			Coordinator::getInstance()->remapMsgHandler->removeAliveSlave( event.socket->getAddr() );
	} else if ( isSend ) {
		ret = event.socket->send( buffer.data, buffer.size, connected );
		if ( ret != ( ssize_t ) buffer.size )
			__ERROR__( "CoordinatorWorker", "dispatch", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
		if ( ! connected )
			__ERROR__( "CoordinatorWorker", "dispatch", "The slave is disconnected." );
	} else {
		// Parse requests from slaves
		ProtocolHeader header;
		WORKER_RECEIVE_FROM_EVENT_SOCKET();
		ArrayMap<int, MasterSocket> &masters = Coordinator::getInstance()->sockets.masters;
		while( buffer.size > 0 ) {
			WORKER_RECEIVE_WHOLE_MESSAGE_FROM_EVENT_SOCKET( "CoordinatorWorker" );

			// avvoid declaring variables after jump statements
			size_t bytes, offset, count = 0;
			buffer.data += PROTO_HEADER_SIZE;
			buffer.size -= PROTO_HEADER_SIZE;
			// Validate message
			if ( header.from != PROTO_MAGIC_FROM_SLAVE ) {
				__ERROR__( "CoordinatorWorker", "dispatch", "Invalid message source from slave." );
				goto quit_1;
			}

			event.id = header.id;
			switch( header.opcode ) {
				case PROTO_OPCODE_RELEASE_DEGRADED_LOCKS:
					switch( header.magic ) {
						case PROTO_MAGIC_RESPONSE_SUCCESS:
							this->handleReleaseDegradedLockResponse( event, buffer.data, header.length );
							break;
						default:
							__ERROR__( "CoordinatorWorker", "dispatch", "Invalid magic code from slave." );
					}
					break;
				case PROTO_OPCODE_RECONSTRUCTION:
					switch( header.magic ) {
						case PROTO_MAGIC_RESPONSE_SUCCESS:
							this->handleReconstructionResponse( event, buffer.data, header.length );
							break;
						default:
							__ERROR__( "CoordinatorWorker", "dispatch", "Invalid magic code from slave." );
					}
					break;
				case PROTO_OPCODE_BACKUP_SLAVE_PROMOTED:
					switch( header.magic ) {
						case PROTO_MAGIC_RESPONSE_SUCCESS:
							this->handlePromoteBackupSlaveResponse( event, buffer.data, header.length );
							break;
						default:
							__ERROR__( "CoordinatorWorker", "dispatch", "Invalid magic code from slave." );
					}
					break;
				case PROTO_OPCODE_SYNC:
					switch( header.magic ) {
						case PROTO_MAGIC_HEARTBEAT:
							this->processHeartbeat( event, buffer.data, header.length );
							break;
						case PROTO_MAGIC_REMAPPING:
						{
							struct RemappingRecordHeader remappingRecordHeader;
							struct SlaveSyncRemapHeader slaveSyncRemapHeader;
							if ( ! this->protocol.parseRemappingRecordHeader( remappingRecordHeader, buffer.data, buffer.size ) ) {
								__ERROR__( "CoordinatorWorker", "dispatch", "Invalid remapping record protocol header." );
								goto quit_1;
							}
							// start parsing the remapping records
							// TODO buffer.size >> total size of remapping records?
							offset = PROTO_REMAPPING_RECORD_SIZE;
							RemappingRecordMap *map = CoordinatorWorker::remappingRecords;
							for ( count = 0; offset < ( size_t ) buffer.size && count < remappingRecordHeader.remap; offset += bytes ) {
								if ( ! this->protocol.parseSlaveSyncRemapHeader( slaveSyncRemapHeader, bytes, buffer.data, buffer.size - offset, offset ) )
									break;
								count++;

								Key key;
								key.set( slaveSyncRemapHeader.keySize, slaveSyncRemapHeader.key );

								RemappingRecord remappingRecord;
								remappingRecord.set( slaveSyncRemapHeader.listId, slaveSyncRemapHeader.chunkId, 0 );

								if ( slaveSyncRemapHeader.opcode == 0 ) { // remove record
									map->erase( key, remappingRecord );
								} else if ( slaveSyncRemapHeader.opcode == 1 ) { // add record
									map->insert( key, remappingRecord );
								}
							}
							//map->print();
							//fprintf ( stderr, "Remapping Records no.=%lu (%u) upto=%lu size=%lu\n", count, remappingRecordHeader.remap, offset, buffer.size );

							// forward the copies of message to masters
							MasterEvent masterEvent;
							masterEvent.type = MASTER_EVENT_TYPE_FORWARD_REMAPPING_RECORDS;
							masterEvent.message.forward.prevSize = buffer.size;
							for ( uint32_t i = 0; i < masters.size() ; i++ ) {
								masterEvent.socket = masters.values[ i ];
								masterEvent.message.forward.data = new char[ buffer.size ];
								memcpy( masterEvent.message.forward.data, buffer.data, buffer.size );
								CoordinatorWorker::eventQueue->insert( masterEvent );
							}
						}
							break;
						default:
							__ERROR__( "CoordinatorWorker", "dispatch", "Invalid magic code from slave." );
							break;
					}
					break;
				default:
					__ERROR__( "CoordinatorWorker", "dispatch", "Invalid opcode from slave." );
					goto quit_1;
			}
quit_1:
			buffer.data += header.length;
			buffer.size -= header.length;
		}
		if ( connected )
			event.socket->done();
		else
			__ERROR__( "CoordinatorWorker", "dispatch", "The slave is disconnected." );
	}
}

void CoordinatorWorker::free() {
	this->protocol.free();
}

void *CoordinatorWorker::run( void *argv ) {
	CoordinatorWorker *worker = ( CoordinatorWorker * ) argv;
	WorkerRole role = worker->getRole();
	CoordinatorEventQueue *eventQueue = CoordinatorWorker::eventQueue;

#define COORDINATOR_WORKER_EVENT_LOOP(_EVENT_TYPE_, _EVENT_QUEUE_) \
	do { \
		_EVENT_TYPE_ event; \
		bool ret; \
		while( worker->getIsRunning() | ( ret = _EVENT_QUEUE_->extract( event ) ) ) { \
			if ( ret ) \
				worker->dispatch( event ); \
		} \
	} while( 0 )

	switch ( role ) {
		case WORKER_ROLE_MIXED:
			COORDINATOR_WORKER_EVENT_LOOP(
				MixedEvent,
				eventQueue->mixed
			);
			break;
		case WORKER_ROLE_COORDINATOR:
			COORDINATOR_WORKER_EVENT_LOOP(
				CoordinatorEvent,
				eventQueue->separated.coordinator
			);
			break;
		case WORKER_ROLE_MASTER:
			COORDINATOR_WORKER_EVENT_LOOP(
				MasterEvent,
				eventQueue->separated.master
			);
			break;
		case WORKER_ROLE_SLAVE:
			COORDINATOR_WORKER_EVENT_LOOP(
				SlaveEvent,
				eventQueue->separated.slave
			);
			break;
		default:
			break;
	}

	worker->free();
	pthread_exit( 0 );
	return 0;
}

bool CoordinatorWorker::processHeartbeat( SlaveEvent event, char *buf, size_t size ) {
	uint32_t count, requestId = event.id;
	size_t processed, offset, failed = 0;
	struct HeartbeatHeader heartbeat;
	union {
		struct MetadataHeader metadata;
		struct KeyOpMetadataHeader op;
	} header;

	offset = 0;
	if ( ! this->protocol.parseHeartbeatHeader( heartbeat, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "dispatch", "Invalid heartbeat protocol header." );
		return false;
	}

	offset += PROTO_HEARTBEAT_SIZE;

	LOCK( &event.socket->map.chunksLock );
	for ( count = 0; count < heartbeat.sealed; count++ ) {
		if ( this->protocol.parseMetadataHeader( header.metadata, processed, buf, size, offset ) ) {
			event.socket->map.insertChunk(
				header.metadata.listId,
				header.metadata.stripeId,
				header.metadata.chunkId,
				false, false
			);
		} else {
			failed++;
		}
		offset += processed;
	}
	UNLOCK( &event.socket->map.chunksLock );

	LOCK( &event.socket->map.keysLock );
	for ( count = 0; count < heartbeat.keys; count++ ) {
		if ( this->protocol.parseKeyOpMetadataHeader( header.op, processed, buf, size, offset ) ) {
			SlaveSocket *s = event.socket;
			if ( header.op.opcode == PROTO_OPCODE_DELETE ) { // Handle keys from degraded DELETE
				s = CoordinatorWorker::stripeList->get( header.op.listId, header.op.chunkId );
			}
			s->map.insertKey(
				header.op.key,
				header.op.keySize,
				header.op.listId,
				header.op.stripeId,
				header.op.chunkId,
				header.op.opcode,
				false, false
			);
		} else {
			failed++;
		}
		offset += processed;
	}
	UNLOCK( &event.socket->map.keysLock );

	if ( failed ) {
		__ERROR__( "CoordinatorWorker", "processHeartbeat", "Number of failed objects = %lu", failed );
	// } else {
	// 	__ERROR__( "CoordinatorWorker", "processHeartbeat", "(sealed, keys, remap) = (%u, %u, %u)", heartbeat.sealed, heartbeat.keys, heartbeat.remap );
	}

	// check if this is the last packet for a sync operation
	// remove pending meta sync requests
	if ( requestId && heartbeat.isLast && ! failed ) {
		bool *sync = Coordinator::getInstance()->pending.removeSyncMetaReq( requestId );
		if ( sync )
			*sync = true;
	}

	return failed == 0;
}

bool CoordinatorWorker::handleReconstructionRequest( SlaveSocket *socket ) {
	int index = CoordinatorWorker::stripeList->search( socket );
	if ( index == -1 ) {
		__ERROR__( "CoordinatorWorker", "handleReconstructionRequest", "The disconnected server does not exist in the consistent hash ring.\n" );
		return false;
	}

	struct timespec startTime = start_timer();

	/////////////////////////////////////////////////////////////////////
	// Choose a backup slave socket for reconstructing the failed node //
	/////////////////////////////////////////////////////////////////////
	ArrayMap<int, SlaveSocket> &slaves = Coordinator::getInstance()->sockets.slaves;
	ArrayMap<int, SlaveSocket> &backupSlaves = Coordinator::getInstance()->sockets.backupSlaves;
	int fd;
	SlaveSocket *backupSlaveSocket;

	///////////////////////////////////////////////////////
	// Choose a backup slave to replace the failed slave //
	///////////////////////////////////////////////////////
	if ( backupSlaves.size() == 0 ) {
		__ERROR__( "CoordinatorWorker", "handleReconstructionRequest", "No backup node is available!" );
		return false;
	}
	backupSlaveSocket = backupSlaves[ 0 ];
	backupSlaves.removeAt( 0 );

	////////////////////////////
	// Update SlaveSocket map //
	////////////////////////////
	fd = backupSlaveSocket->getSocket();
	backupSlaveSocket->failed = socket;
	slaves.set( index, fd, backupSlaveSocket );

	////////////////////////////
	// Announce to the slaves //
	////////////////////////////
	SlaveEvent slaveEvent;
	slaveEvent.announceSlaveReconstructed( socket, backupSlaveSocket );
	CoordinatorWorker::eventQueue->insert( slaveEvent );

	////////////////////////////////////////////////////////////////////////////

	uint32_t numLostChunks = 0, listId, stripeId, chunkId, requestId = 0;
	std::set<Metadata> unsealedChunks;
	bool connected, isCompleted, isAllCompleted;
	ssize_t ret;
	struct {
		size_t size;
		char *data;
	} buffer;

	std::unordered_set<Metadata>::iterator chunksIt;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> stripeIds;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>>::iterator stripeIdsIt;
	std::unordered_set<uint32_t>::iterator stripeIdSetIt;
	std::unordered_map<uint32_t, SlaveSocket **> sockets;

	std::vector<StripeListIndex> lists = CoordinatorWorker::stripeList->list( ( uint32_t ) index );

	ArrayMap<int, SlaveSocket> &map = Coordinator::getInstance()->sockets.slaves;

	//////////////////////////////////////////////////
	// Get the SlaveSockets of the surviving slaves //
	//////////////////////////////////////////////////
	LOCK( &map.lock );
	for ( uint32_t i = 0, size = lists.size(); i < size; i++ ) {
		listId = lists[ i ].listId;
		if ( sockets.find( listId ) == sockets.end() ) {
			SlaveSocket **s = new SlaveSocket*[ CoordinatorWorker::chunkCount ];
			CoordinatorWorker::stripeList->get(
				listId, s + CoordinatorWorker::dataChunkCount, s
			);
			sockets[ listId ] = s;
		}
	}
	UNLOCK( &map.lock );

	LOCK( &socket->map.chunksLock );
	LOCK( &socket->map.keysLock );

	//////////////////////////////
	// Promote the backup slave //
	//////////////////////////////
	requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );
	chunksIt = socket->map.chunks.begin();
	do {
		buffer.data = this->protocol.promoteBackupSlave(
			buffer.size,
			requestId,
			socket,
			socket->map.chunks,
			chunksIt,
			isCompleted
		);

		ret = backupSlaveSocket->send( buffer.data, buffer.size, connected );
		if ( ret != ( ssize_t ) buffer.size ) {
			__ERROR__( "CoordinatorWorker", "handleReconstructionRequest", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
			break;
		}
	} while ( ! isCompleted );

	// Insert into pending recovery map
	ServerAddr srcAddr = socket->getServerAddr();
	if ( ! CoordinatorWorker::pending->insertRecovery(
		requestId,
		srcAddr.addr,
		srcAddr.port,
		socket->map.chunks.size(),
		startTime,
		backupSlaveSocket
	) ) {
		__ERROR__( "CoordinatorWorker", "handleReconstructionRequest", "Cannot insert into pending recovery map." );
	}

	/////////////////////////////////////////////////////////////////
	// Distribute the reconstruction tasks to the surviving slaves //
	/////////////////////////////////////////////////////////////////
	for ( chunksIt = socket->map.chunks.begin(); chunksIt != socket->map.chunks.end(); chunksIt++ ) {
		listId = chunksIt->listId;
		stripeId = chunksIt->stripeId;

		stripeIdsIt = stripeIds.find( listId );
		if ( stripeIdsIt == stripeIds.end() ) {
			std::unordered_set<uint32_t> ids;
			ids.insert( stripeId );
			stripeIds[ listId ] = ids;
		} else {
			stripeIdsIt->second.insert( stripeId );
		}
		numLostChunks++;
	}
	assert( numLostChunks == socket->map.chunks.size() );
	// Distribute the reconstruction task among the slaves in the same stripe list
	for ( uint32_t i = 0, size = lists.size(); i < size; i++ ) {
		uint32_t numSurvivingSlaves = 0;
		uint32_t numStripePerSlave;

		listId = lists[ i ].listId;
		chunkId = lists[ i ].chunkId;

		if ( chunkId >= CoordinatorWorker::dataChunkCount ) {
			LOCK( &Map::stripesLock );
			// Update stripeIds for parity slave
			stripeIdsIt = stripeIds.find( listId );
			if ( stripeIdsIt == stripeIds.end() ) {
				std::unordered_set<uint32_t> ids;
				for ( uint32_t j = 0; j < Map::stripes[ listId ]; j++ )
					ids.insert( j );
				stripeIds[ listId ] = ids;
			} else {
				for ( uint32_t j = 0; j < Map::stripes[ listId ]; j++ )
					stripeIdsIt->second.insert( j );
			}
			UNLOCK( &Map::stripesLock );
		}

		for ( uint32_t j = 0; j < CoordinatorWorker::chunkCount; j++ ) {
			SlaveSocket *s = sockets[ listId ][ j ];
			if ( s->ready() && s != backupSlaveSocket )
				numSurvivingSlaves++;
		}

		numStripePerSlave = stripeIds[ listId ].size() / numSurvivingSlaves;
		if ( stripeIds[ listId ].size() % numSurvivingSlaves > 0 )
			numStripePerSlave++;

		// Insert into pending map
		pthread_mutex_t *lock;
		pthread_cond_t *cond;

		requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );

		CoordinatorWorker::pending->insertReconstruction(
			requestId,
			listId, chunkId, stripeIds[ listId ],
			lock, cond
		);

		// Distribute the task
		stripeIdSetIt = stripeIds[ listId ].begin();
		do {
			isAllCompleted = true;
			for ( uint32_t j = 0; j < CoordinatorWorker::chunkCount; j++ ) {
				if ( stripeIdSetIt == stripeIds[ listId ].end() )
					break;

				SlaveSocket *s = sockets[ listId ][ j ];
				if ( s->ready() && s != backupSlaveSocket ) {
					buffer.data = this->protocol.reqReconstruction(
						buffer.size,
						requestId,
						listId,
						chunkId,
						stripeIds[ listId ],
						stripeIdSetIt,
						numStripePerSlave,
						isCompleted
					);

					ret = s->send( buffer.data, buffer.size, connected );
					if ( ret != ( ssize_t ) buffer.size )
						__ERROR__( "CoordinatorWorker", "handleReconstructionRequest", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );

					pthread_mutex_lock( lock );
					pthread_cond_wait( cond, lock );
					pthread_mutex_unlock( lock );

					isAllCompleted &= isCompleted;
				}
			}
		} while ( ! isAllCompleted );

		__INFO__( YELLOW, "CoordinatorWorker", "handleReconstructionRequest", "[%u] (%u, %u): Number of surviving slaves: %u; number of stripes per slave: %u; total number of stripes: %lu", requestId, listId, chunkId, numSurvivingSlaves, numStripePerSlave, stripeIds[ listId ].size() );
	}

	////////////////////////////
	// Handle unsealed chunks //
	////////////////////////////
	// TODO

	UNLOCK( &socket->map.keysLock );
	UNLOCK( &socket->map.chunksLock );

	printf( "Number of chunks that need to be recovered: %u\n", numLostChunks );

	return true;
}

bool CoordinatorWorker::handleReconstructionResponse( SlaveEvent event, char *buf, size_t size ) {
	struct ReconstructionHeader header;
	if ( ! this->protocol.parseReconstructionHeader( header, false /* isRequest */, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "handleReconstructionResponse", "Invalid RECONSTRUCTION response (size = %lu).", size );
		return false;
	}

	uint32_t remaining;
	if ( ! CoordinatorWorker::pending->eraseReconstruction( event.id, header.listId, header.chunkId, header.numStripes, remaining ) ) {
		__ERROR__( "CoordinatorWorker", "handleReconstructionResponse", "The response does not match with the request!" );
		return false;
	}

	__DEBUG__(
		BLUE, "CoordinatorWorker", "handleReconstructionResponse",
		"[RECONSTRUCTION] Request ID: %u; list ID: %u, chunk Id: %u, number of stripes: %u (%s)",
		event.id, header.listId, header.chunkId, header.numStripes,
		remaining == 0 ? "Done" : "In progress"
	);

	return true;
}

bool CoordinatorWorker::handleReleaseDegradedLockRequest( SlaveSocket *socket, bool *done ) {
	std::unordered_map<Metadata, Metadata>::iterator dlsIt;
	Map &map = socket->map;
	Metadata dst;
	SlaveSocket *dstSocket;
	bool isCompleted, connected;
	uint32_t requestId;
	ssize_t ret;
	struct {
		size_t size;
		char *data;
	} buffer;
	// (dstListId, dstChunkId) |-> (srcListId, srcStripeId, srcChunkId)
	std::unordered_map<Metadata, std::vector<Metadata>> chunks;
	std::unordered_map<Metadata, std::vector<Metadata>>::iterator chunksIt;

	LOCK( &map.degradedLocksLock );
	for ( dlsIt = map.degradedLocks.begin(); dlsIt != map.degradedLocks.end(); dlsIt++ ) {
		const Metadata &src = dlsIt->first, &dst = dlsIt->second;
		chunksIt = chunks.find( dst );
		if ( chunksIt != chunks.end() ) {
			std::vector<Metadata> &srcs = chunksIt->second;
			srcs.push_back( src );
		} else {
			std::vector<Metadata> srcs;
			srcs.push_back( src );
			chunks[ dst ] = srcs;
		}
	}
	map.degradedLocks.swap( map.releasingDegradedLocks );
	UNLOCK( &map.degradedLocksLock );

	if ( chunks.size() == 0 ) {
		// No chunks needed to be sync.
		*done = true;
		return true;
	}

	// Update pending map
	requestId = CoordinatorWorker::idGenerator->nextVal( this->workerId );

	for ( chunksIt = chunks.begin(); chunksIt != chunks.end(); chunksIt++ ) {
		std::vector<Metadata> &srcs = chunksIt->second;
		dst = chunksIt->first;
		dstSocket = CoordinatorWorker::stripeList->get( dst.listId, dst.chunkId );

		CoordinatorWorker::pending->addReleaseDegradedLock( requestId, srcs.size(), done );

		isCompleted = true;
		do {

			buffer.data = this->protocol.reqReleaseDegradedLock(
				buffer.size, requestId, srcs, isCompleted
			);
			ret = dstSocket->send( buffer.data, buffer.size, connected );
			if ( ret != ( ssize_t ) buffer.size )
				__ERROR__( "CoordinatorWorker", "handleReleaseDegradedLockRequest", "The number of bytes sent (%ld bytes) is not equal to the message size (%lu bytes).", ret, buffer.size );
		} while ( ! isCompleted );

		// printf( "dst: (%u, %u) |-> %lu\n", chunksIt->first.listId, chunksIt->first.chunkId, srcs.size() );
	}

	return true;
}

bool CoordinatorWorker::handleReleaseDegradedLockResponse( SlaveEvent event, char *buf, size_t size ) {
	struct DegradedReleaseResHeader header;
	if ( ! this->protocol.parseDegradedReleaseResHeader( header, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "handleReleaseDegradedLockResponse", "Invalid RELEASE_DEGRADED_LOCK request (size = %lu).", size );
		return false;
	}
	__DEBUG__(
		BLUE, "CoordinatorWorker", "handleReleaseDegradedLockResponse",
		"[RELEASE_DEGRADED_LOCK] Request ID: %u; Count: %u",
		event.id, header.count
	);

	bool *done = CoordinatorWorker::pending->removeReleaseDegradedLock( event.id, header.count );
	if ( done )
		*done = true;
	return true;
}

bool CoordinatorWorker::handlePromoteBackupSlaveResponse( SlaveEvent event, char *buf, size_t size ) {
	struct PromoteBackupSlaveHeader header;
	if ( ! this->protocol.parsePromoteBackupSlaveHeader( header, false /* isRequest */, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "handlePromoteBackupSlaveResponse", "Invalid PROMOTE_BACKUP_SLAVE response (size = %lu).", size );
		return false;
	}
	__DEBUG__(
		BLUE, "CoordinatorWorker", "handlePromoteBackupSlaveResponse",
		"[PROMOTE_BACKUP_SLAVE] Request ID: %u; Count: %u (%u:%u)",
		event.id, header.count, header.addr, header.port
	);

	uint32_t remaining, total;
	double elapsedTime;

	if ( ! CoordinatorWorker::pending->eraseRecovery( event.id, header.addr, header.port, header.count, event.socket, remaining, total, elapsedTime ) ) {
		__ERROR__( "SlaveWorker", "handlePromoteBackupSlaveResponse", "Cannot find a pending RECOVERY request that matches the response. This message will be discarded. (ID: %u)", event.id );
		return false;
	}

	if ( remaining == 0 ) {
		__INFO__( CYAN, "CoordinatorWorker", "handlePromoteBackupSlaveResponse", "Recovery is completed. Number of chunks reconstructed = %u; elapsed time = %lf s.\n", total, elapsedTime );

		Log log;
		log.setRecovery(
			header.addr,
			header.port,
			header.count,
			elapsedTime
		);
		Coordinator::getInstance()->appendLog( log );

		// system( "ssh testbed-node10 'screen -S experiment -p 0 -X stuff \"$(printf '\r')\"'" );
	}

	return true;
}

bool CoordinatorWorker::handleDegradedLockRequest( MasterEvent event, char *buf, size_t size ) {
	struct DegradedLockReqHeader header;
	if ( ! this->protocol.parseDegradedLockReqHeader( header, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "handleDegradedLockRequest", "Invalid DEGRADED_LOCK request (size = %lu).", size );
		return false;
	}
	__DEBUG__(
		BLUE, "CoordinatorWorker", "handleDegradedLockRequest",
		"[DEGRADED_LOCK] Key: %.*s (key size = %u); List ID: %u; Data: (%u --> %u); Parity: (%u --> %u).",
		( int ) header.keySize, header.key, header.keySize,
		header.listId,
		header.srcDataChunkId, header.dstDataChunkId,
		header.srcParityChunkId, header.dstParityChunkId
	);

	// Metadata metadata;
	RemappingRecord remappingRecord;
	Key key;
	key.set( header.keySize, header.key );

	if ( CoordinatorWorker::remappingRecords->find( key, &remappingRecord ) ) {
		// Remapped
		if ( remappingRecord.listId != header.listId || remappingRecord.chunkId != header.srcDataChunkId ) {
			// Reject the degraded operation if the data chunk ID does not match
			event.resDegradedLock(
				event.socket, event.id, key,
				header.listId,
				header.srcDataChunkId, remappingRecord.chunkId,
				header.srcParityChunkId, header.srcParityChunkId
			);
			this->dispatch( event );
			return false;
		}
	}

	// Find the SlaveSocket which stores the stripe with listId and srcDataChunkId
	SlaveSocket *socket = CoordinatorWorker::stripeList->get( header.listId, header.srcDataChunkId );
	Map *map = &socket->map;
	Metadata srcMetadata /* set via findMetadataByKey() */, dstMetadata;
	bool ret = true;

	if ( ! map->findMetadataByKey( header.key, header.keySize, srcMetadata ) ) {
		// Key not found
		event.resDegradedLock(
			event.socket, event.id,
			key, false,
			header.listId, header.srcDataChunkId, header.srcParityChunkId
		);
		ret = false;
	} else if ( header.srcDataChunkId != header.dstDataChunkId && map->findDegradedLock( srcMetadata.listId, srcMetadata.stripeId, header.srcDataChunkId, dstMetadata ) ) {
		// The chunk is already locked
		event.resDegradedLock(
			event.socket, event.id, key,
			dstMetadata.listId == header.listId && dstMetadata.chunkId == header.dstDataChunkId, // the degraded lock is attained
			map->isSealed( srcMetadata ), // the chunk is sealed
			srcMetadata.listId, srcMetadata.stripeId,
			srcMetadata.chunkId, dstMetadata.chunkId,
			header.srcParityChunkId, header.srcParityChunkId // Ignore parity server redirection
		);
	} else if ( header.srcParityChunkId != header.dstParityChunkId && map->findDegradedLock( srcMetadata.listId, srcMetadata.stripeId, header.srcParityChunkId, dstMetadata ) ) {
		// The chunk is already locked
		event.resDegradedLock(
			event.socket, event.id, key,
			dstMetadata.listId == header.listId && dstMetadata.chunkId == header.dstParityChunkId, // the degraded lock is attained
			map->isSealed( srcMetadata ), // the chunk is sealed
			srcMetadata.listId, srcMetadata.stripeId,
			srcMetadata.chunkId, srcMetadata.chunkId, // Ignore data server redirection
			header.srcParityChunkId, dstMetadata.chunkId
		);
	} else if ( header.srcDataChunkId == header.dstDataChunkId && header.srcParityChunkId == header.dstParityChunkId ) {
		// No need to lock
		event.resDegradedLock(
			event.socket, event.id,
			key, true,
			srcMetadata.listId,
			header.srcDataChunkId,
			header.srcParityChunkId
		);
	} else {
		// Check whether any chunk in the same stripe has been locked
		bool exist = false;
		Metadata tmpDst;
		for ( uint32_t chunkId = 0; chunkId < CoordinatorWorker::chunkCount; chunkId++ ) {
			if ( chunkId != srcMetadata.chunkId && map->findDegradedLock( srcMetadata.listId, srcMetadata.stripeId, chunkId, tmpDst ) ) {
				printf(
					"Chunk (%u, %u, %u) in the same stripe has been locked (destination chunk ID: %u). Rejecting degraded lock request: (data: %u --> %u; parity: %u --> %u).\n",
					srcMetadata.listId, srcMetadata.stripeId,
					chunkId, tmpDst.chunkId,
					header.srcDataChunkId, header.dstDataChunkId,
					header.srcParityChunkId, header.dstParityChunkId
				);
				exist = true;
				break;
			}
		}

		if ( exist ) {
			// Reject the lock request
			event.resDegradedLock(
				event.socket, event.id,
				key, true,
				srcMetadata.listId,
				header.srcDataChunkId,
				header.srcParityChunkId
			);
		} else {
			if ( header.srcDataChunkId != header.dstDataChunkId ) {
				dstMetadata.set( header.listId, 0, header.dstDataChunkId );
				ret = map->insertDegradedLock( srcMetadata, dstMetadata );
			} else {
				Metadata tmp;
				tmp.set( srcMetadata.listId, srcMetadata.stripeId, header.srcParityChunkId );

				dstMetadata.set( header.listId, 0, header.dstParityChunkId );
				ret = map->insertDegradedLock( tmp, dstMetadata );
			}

			event.resDegradedLock(
				event.socket, event.id, key,
				true,                         // the degraded lock is attained
				map->isSealed( srcMetadata ), // the chunk is sealed
				srcMetadata.listId, srcMetadata.stripeId,
				srcMetadata.chunkId, header.dstDataChunkId,
				header.srcParityChunkId, header.dstParityChunkId
			);
		}
	}
	this->dispatch( event );
	return ret;
}

bool CoordinatorWorker::handleRemappingSetLockRequest( MasterEvent event, char *buf, size_t size ) {
	struct RemappingLockHeader header;
	if ( ! this->protocol.parseRemappingLockHeader( header, buf, size ) ) {
		__ERROR__( "CoordinatorWorker", "handleRemappingSetLockRequest", "Invalid REMAPPING_SET_LOCK request (size = %lu).", size );
		return false;
	}
	__DEBUG__(
		BLUE, "CoordinatorWorker", "handleRemappingSetLockRequest",
		"[REMAPPING_SET_LOCK] Key: %.*s (key size = %u); remapped list ID: %u, remapped chunk ID: %u",
		( int ) header.keySize, header.key, header.keySize, header.listId, header.chunkId
	);

	Key key;
	key.set( header.keySize, header.key );

	// Find the SlaveSocket which stores the stripe with srcListId and srcChunkId
	SlaveSocket *socket = CoordinatorWorker::stripeList->get( header.listId, header.chunkId );
	Map *map = &socket->map;

	// if already exists, does not allow remap; otherwise insert the remapping record
	RemappingRecord remappingRecord( header.listId, header.chunkId );
	if ( map->insertKey(
		header.key, header.keySize, header.listId, 0,
		header.chunkId, PROTO_OPCODE_REMAPPING_LOCK,
		true, true)
	) {
		if ( header.isRemapped ) {
			if ( CoordinatorWorker::remappingRecords->insert( key, remappingRecord ) ) {
				key.dup();
				LOCK( &Coordinator::getInstance()->pendingRemappingRecords.toSendLock );
				Coordinator::getInstance()->pendingRemappingRecords.toSend[ key ] = remappingRecord;
				UNLOCK( &Coordinator::getInstance()->pendingRemappingRecords.toSendLock );
				event.resRemappingSetLock( event.socket, event.id, header.isRemapped, key, remappingRecord, true, header.sockfd );
			} else {
				event.resRemappingSetLock( event.socket, event.id, header.isRemapped, key, remappingRecord, false, header.sockfd );
			}
		} else {
			event.resRemappingSetLock( event.socket, event.id, header.isRemapped, key, remappingRecord, true, header.sockfd );
		}
	} else {
		event.resRemappingSetLock( event.socket, event.id, header.isRemapped, key, remappingRecord, false, header.sockfd );
	}
	this->dispatch( event );

	return true;
}

bool CoordinatorWorker::init() {
	Coordinator *coordinator = Coordinator::getInstance();

	CoordinatorWorker::dataChunkCount =
	coordinator->config.global.coding.params.getDataChunkCount();
	CoordinatorWorker::parityChunkCount = coordinator->config.global.coding.params.getParityChunkCount();
	CoordinatorWorker::chunkCount = CoordinatorWorker::dataChunkCount + CoordinatorWorker::parityChunkCount;
	CoordinatorWorker::idGenerator = &coordinator->idGenerator;
	CoordinatorWorker::eventQueue = &coordinator->eventQueue;
	CoordinatorWorker::remappingRecords = &coordinator->remappingRecords;
	CoordinatorWorker::stripeList = coordinator->stripeList;
	CoordinatorWorker::pending = &coordinator->pending;

	return true;
}

bool CoordinatorWorker::init( GlobalConfig &config, WorkerRole role, uint32_t workerId ) {
	this->protocol.init(
		Protocol::getSuggestedBufferSize(
			config.size.key,
			config.size.chunk
		)
	);
	this->role = role;
	this->workerId = workerId;
	return role != WORKER_ROLE_UNDEFINED;
}

bool CoordinatorWorker::start() {
	this->isRunning = true;
	if ( pthread_create( &this->tid, NULL, CoordinatorWorker::run, ( void * ) this ) != 0 ) {
		__ERROR__( "CoordinatorWorker", "start", "Cannot start worker thread." );
		return false;
	}
	return true;
}

void CoordinatorWorker::stop() {
	this->isRunning = false;
}

void CoordinatorWorker::print( FILE *f ) {
	char role[ 16 ];
	switch( this->role ) {
		case WORKER_ROLE_MIXED:
			strcpy( role, "Mixed" );
			break;
		case WORKER_ROLE_COORDINATOR:
			strcpy( role, "Coordinator" );
			break;
		case WORKER_ROLE_MASTER:
			strcpy( role, "Master" );
			break;
		case WORKER_ROLE_SLAVE:
			strcpy( role, "Slave" );
			break;
		default:
			return;
	}
	fprintf( f, "%11s worker (Thread ID = %lu): %srunning\n", role, this->tid, this->isRunning ? "" : "not " );
}
