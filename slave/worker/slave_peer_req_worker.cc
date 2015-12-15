#include "worker.hh"
#include "../main/slave.hh"

bool SlaveWorker::handleSlavePeerRegisterRequest( SlavePeerSocket *socket, char *buf, size_t size ) {
	struct AddressHeader header;
	if ( ! this->protocol.parseAddressHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleSlavePeerRegisterRequest", "Invalid address header." );
		return false;
	}

	// Find the slave peer socket in the array map
	int index = -1;
	for ( int i = 0, len = slavePeers->values.size(); i < len; i++ ) {
		if ( slavePeers->values[ i ]->equal( header.addr, header.port ) ) {
			index = i;
			break;
		}
	}
	if ( index == -1 ) {
		__ERROR__( "SlaveWorker", "handleSlavePeerRegisterRequest", "The slave is not in the list. Ignoring this slave..." );
		return false;
	}

	SlavePeerEvent event;
	event.resRegister( slavePeers->values[ index ], true );
	SlaveWorker::eventQueue->insert( event );
	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool SlaveWorker::handleSetRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct KeyValueHeader header;
	if ( ! this->protocol.parseKeyValueHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleSetRequest (SlavePeer)", "Invalid SET request (size = %lu).", size );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleSetRequest (SlavePeer) ",
		"[SET] Key: %.*s (key size = %u); Value: (value size = %u)",
		( int ) header.keySize, header.key, header.keySize, header.valueSize
	);
	// same flow as set from masters
	MasterEvent masterEvent;
	bool success = this->handleSetRequest( masterEvent, buf, size, false );

	uint32_t listId, chunkId;
	listId = SlaveWorker::stripeList->get( header.key, header.keySize, 0, 0, &chunkId );

	Key key;
	key.set( header.keySize, header.key );
	event.resSet( event.socket, event.id, listId, chunkId, key, success );
	this->dispatch( event );

	return success;
}

bool SlaveWorker::handleGetRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ListStripeKeyHeader header;
	bool ret;
	if ( ! this->protocol.parseListStripeKeyHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleGetRequest", "Invalid UNSEALED_GET request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleGetRequest",
		"[UNSEALED_GET] List ID: %u, chunk ID: %u; key: %.*s.",
		header.listId, header.chunkId, header.keySize, header.key
	);

	Key key;
	KeyValue keyValue;

	ret = SlaveWorker::chunkBuffer->at( header.listId )->findValueByKey(
		header.key, header.keySize, &keyValue, &key
	);

	if ( ret )
		event.resGet( event.socket, event.id, keyValue );
	else
		event.resGet( event.socket, event.id, key );
	this->dispatch( event );
	return ret;
}

bool SlaveWorker::handleUpdateRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkKeyValueUpdateHeader header;
	if ( ! this->protocol.parseChunkKeyValueUpdateHeader( header, true, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleUpdateRequest", "Invalid UPDATE request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleUpdateRequest",
		"[UPDATE] Key: %.*s (key size = %u); Value: (update size = %u, offset = %u); list ID = %u, stripe ID = %u, chunk Id = %u, chunk update offset = %u.",
		( int ) header.keySize, header.key, header.keySize,
		header.valueUpdateSize, header.valueUpdateOffset,
		header.listId, header.stripeId, header.chunkId,
		header.chunkUpdateOffset
	);


	Key key;
	bool ret = SlaveWorker::chunkBuffer->at( header.listId )->updateKeyValue(
		header.key, header.keySize,
		header.valueUpdateOffset, header.valueUpdateSize, header.valueUpdate
	);
	if ( ! ret ) {
		// Use the chunkUpdateOffset
		SlaveWorker::chunkBuffer->at( header.listId )->update(
			header.stripeId, header.chunkId,
			header.chunkUpdateOffset, header.valueUpdateSize, header.valueUpdate,
			this->chunks, this->dataChunk, this->parityChunk
		);
		ret = true;
	}

	key.set( header.keySize, header.key );
	event.resUpdate(
		event.socket, event.id,
		header.listId, header.stripeId, header.chunkId, key,
		header.valueUpdateOffset,
		header.valueUpdateSize,
		header.chunkUpdateOffset,
		ret
	);
	this->dispatch( event );

	return ret;
}

bool SlaveWorker::handleDeleteRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkKeyHeader header;
	if ( ! this->protocol.parseChunkKeyHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleDeleteRequest", "Invalid DELETE request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleDeleteRequest",
		"[DELETE] Key: %.*s (key size = %u); list ID = %u, stripe ID = %u, chunk Id = %u.",
		( int ) header.keySize, header.key, header.keySize,
		header.listId, header.stripeId, header.chunkId
	);

	Key key;
	bool ret = SlaveWorker::chunkBuffer->at( header.listId )->deleteKey( header.key, header.keySize );

	key.set( header.keySize, header.key );
	event.resDelete(
		event.socket, event.id,
		header.listId, header.stripeId, header.chunkId,
		key, ret
	);
	this->dispatch( event );

	return ret;
}

////////////////////////////////////////////////////////////////////////////////

bool SlaveWorker::handleGetChunkRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkHeader header;
	bool ret;
	if ( ! this->protocol.parseChunkHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleGetChunkRequest", "Invalid GET_CHUNK request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleGetChunkRequest",
		"[GET_CHUNK] List ID: %u; stripe ID: %u; chunk ID: %u.",
		header.listId, header.stripeId, header.chunkId
	);

	Metadata metadata;
	Chunk *chunk = map->findChunkById( header.listId, header.stripeId, header.chunkId, &metadata );
	ret = chunk;

	// Check whether the chunk is sealed or not
	MixedChunkBuffer *chunkBuffer = SlaveWorker::chunkBuffer->at( header.listId );
	int chunkBufferIndex = chunkBuffer->lockChunk( chunk, true );
	bool isSealed = ( chunkBufferIndex == -1 );

	// if ( ! chunk ) {
	// 	__ERROR__( "SlaveWorker", "handleGetChunkRequest", "The chunk (%u, %u, %u) does not exist.", header.listId, header.stripeId, header.chunkId );
	// }

	if ( chunk && chunk->status == CHUNK_STATUS_NEEDS_LOAD_FROM_DISK ) {
		// printf( "Loading chunk: (%u, %u, %u) from disk.\n", header.listId, header.stripeId, header.chunkId );
		this->storage->read(
			chunk,
			chunk->metadata.listId,
			chunk->metadata.stripeId,
			chunk->metadata.chunkId,
			chunk->isParity
		);
	}

	event.resGetChunk( event.socket, event.id, metadata, ret, isSealed ? chunk : 0 );
	this->dispatch( event );

	chunkBuffer->unlock( chunkBufferIndex );

	return ret;
}

bool SlaveWorker::handleSetChunkRequest( SlavePeerEvent event, bool isSealed, char *buf, size_t size ) {
	union {
		struct ChunkDataHeader chunkData;
		struct ChunkKeyValueHeader chunkKeyValue;
	} header;
	Metadata metadata;
	char *ptr = 0;

	if ( isSealed ) {
		if ( ! this->protocol.parseChunkDataHeader( header.chunkData, buf, size ) ) {
			__ERROR__( "SlaveWorker", "handleSetChunkRequest", "Invalid SET_CHUNK request." );
			return false;
		}
		metadata.set( header.chunkData.listId, header.chunkData.stripeId, header.chunkData.chunkId );
		__DEBUG__(
			BLUE, "SlaveWorker", "handleSetChunkRequest",
			"[SET_CHUNK] List ID: %u; stripe ID: %u; chunk ID: %u; chunk size = %u.",
			header.chunkData.listId, header.chunkData.stripeId, header.chunkData.chunkId, header.chunkData.size
		);
	} else {
		if ( ! this->protocol.parseChunkKeyValueHeader( header.chunkKeyValue, ptr, buf, size ) ) {
			__ERROR__( "SlaveWorker", "handleSetChunkRequest", "Invalid SET_CHUNK request." );
			return false;
		}
		metadata.set( header.chunkKeyValue.listId, header.chunkKeyValue.stripeId, header.chunkKeyValue.chunkId );
		__DEBUG__(
			BLUE, "SlaveWorker", "handleSetChunkRequest",
			"[SET_CHUNK] List ID: %u; stripe ID: %u; chunk ID: %u; deleted = %u; count = %u.",
			header.chunkKeyValue.listId, header.chunkKeyValue.stripeId, header.chunkKeyValue.chunkId,
			header.chunkKeyValue.deleted, header.chunkKeyValue.count
		);
	}

	Key key;
	KeyValue keyValue;
	KeyMetadata keyMetadata;
	bool ret;
	uint32_t offset, chunkSize, valueSize, objSize;
	char *valueStr;
	Chunk *chunk;
	LOCK_T *keysLock, *cacheLock;
	std::unordered_map<Key, KeyMetadata> *keys;
	std::unordered_map<Metadata, Chunk *> *cache;
	bool notifyCoordinator = false;
	CoordinatorEvent coordinatorEvent;

	SlaveWorker::map->getKeysMap( keys, keysLock );
	SlaveWorker::map->getCacheMap( cache, cacheLock );

	LOCK( keysLock );
	LOCK( cacheLock );

	chunk = SlaveWorker::map->findChunkById(
		metadata.listId, metadata.stripeId, metadata.chunkId, 0,
		false, // needsLock
		false // needsUnlock
	);
	// Lock the data chunk buffer
	MixedChunkBuffer *chunkBuffer = SlaveWorker::chunkBuffer->at( metadata.listId );
	int chunkBufferIndex = chunkBuffer->lockChunk( chunk, false );

	ret = chunk;
	if ( ! chunk ) {
		// Allocate memory for this chunk
		chunk = SlaveWorker::chunkPool->malloc();
		chunk->clear();
		chunk->metadata.listId = metadata.listId;
		chunk->metadata.stripeId = metadata.stripeId;
		chunk->metadata.chunkId = metadata.chunkId;
		chunk->isParity = metadata.chunkId >= SlaveWorker::dataChunkCount;
		SlaveWorker::map->setChunk(
			metadata.listId, metadata.stripeId, metadata.chunkId,
			chunk, chunk->isParity,
			false, false
		);

		// Remove the chunk from pending chunk set
		uint32_t requestId, addr, remaining, total;
		uint16_t port;
		CoordinatorSocket *coordinatorSocket;
		if ( SlaveWorker::pending->eraseRecovery( metadata.listId, metadata.stripeId, metadata.chunkId, requestId, coordinatorSocket, addr, port, remaining, total ) ) {
			// printf( "Received (%u, %u, %u). Number of remaining pending chunks = %u / %u.\n", metadata.listId, metadata.stripeId, metadata.chunkId, remaining, total );

			if ( remaining == 0 ) {
				notifyCoordinator = true;
				coordinatorEvent.resPromoteBackupSlave( coordinatorSocket, requestId, addr, port, total );
			}
		} else {
			// __ERROR__( "SlaveWorker", "handleSetChunkRequest", "Cannot find the chunk (%u, %u, %u) from pending chunk set.", metadata.listId, metadata.stripeId, metadata.chunkId );
		}
	}

	if ( metadata.chunkId < SlaveWorker::dataChunkCount ) {
		if ( isSealed ) {
			uint32_t originalChunkSize;
			// Delete all keys in the chunk from the map
			offset = 0;
			originalChunkSize = chunkSize = chunk->getSize();
			while ( offset < chunkSize ) {
				keyValue = chunk->getKeyValue( offset );
				keyValue.deserialize( key.data, key.size, valueStr, valueSize );

				key.set( key.size, key.data );
				SlaveWorker::map->deleteKey(
					key, 0, keyMetadata,
					false, // needsLock
					false, // needsUnlock
					false  // needsUpdateOpMetadata
				);

				objSize = KEY_VALUE_METADATA_SIZE + key.size + valueSize;
				offset += objSize;
			}

			// Replace chunk contents
			chunk->loadFromSetChunkRequest(
				header.chunkData.data,
				header.chunkData.size,
				header.chunkData.offset,
				header.chunkData.chunkId >= SlaveWorker::dataChunkCount
			);

			// Add all keys in the new chunk to the map
			offset = 0;
			chunkSize = header.chunkData.size;
			while( offset < chunkSize ) {
				keyValue = chunk->getKeyValue( offset );
				keyValue.deserialize( key.data, key.size, valueStr, valueSize );
				objSize = KEY_VALUE_METADATA_SIZE + key.size + valueSize;

				key.set( key.size, key.data );

				keyMetadata.set( metadata.listId, metadata.stripeId, metadata.chunkId );
				keyMetadata.offset = offset;
				keyMetadata.length = objSize;

				SlaveWorker::map->insertKey(
					key, 0, keyMetadata,
					false, // needsLock
					false, // needsUnlock
					false  // needsUpdateOpMetadata
				);

				offset += objSize;
			}
			chunk->updateData();

			// Re-insert into data chunk buffer
			assert( chunkBufferIndex == -1 );
			if ( ! chunkBuffer->reInsert( this, chunk, originalChunkSize - chunkSize, false, false ) ) {
				// The chunk is compacted before. Need to seal the chunk first
				// Seal from chunk->lastDelPos
				if ( chunk->lastDelPos > 0 && chunk->lastDelPos < chunk->getSize() ) {
					printf( "chunk->lastDelPos = %u; chunk->getSize() = %u\n", chunk->lastDelPos, chunk->getSize() );
					// Only issue seal chunk request when new key-value pairs are received
					this->issueSealChunkRequest( chunk, chunk->lastDelPos );
				}
			}
		} else {
			struct KeyHeader keyHeader;
			struct KeyValueHeader keyValueHeader;
			uint32_t offset = ptr - buf;

			// Handle removed keys in DEGRADED_DELETE
			for ( uint32_t i = 0; i < header.chunkKeyValue.deleted; i++ ) {
				if ( ! this->protocol.parseKeyHeader( keyHeader, buf, size, offset ) ) {
					__ERROR__( "SlaveWorker", "handleSetChunkRequest", "Invalid key in deleted key list." );
					break;
				}
				key.set( keyHeader.keySize, keyHeader.key );

				// Update key map and chunk
				if ( SlaveWorker::map->deleteKey( key, PROTO_OPCODE_DELETE, keyMetadata, false, false, false ) ) {
					chunk->deleteKeyValue( keys, keyMetadata );
				} else {
					__ERROR__( "SlaveWorker", "handleSetChunkRequest", "The deleted key does not exist." );
				}

				offset += PROTO_KEY_SIZE + keyHeader.keySize;
			}

			// Update key-value pairs
			for ( uint32_t i = 0; i < header.chunkKeyValue.count; i++ ) {
				if ( ! this->protocol.parseKeyValueHeader( keyValueHeader, buf, size, offset ) ) {
					__ERROR__( "SlaveWorker", "handleSetChunkRequest", "Invalid key-value pair in updated value list." );
					break;
				}

				// Update the key-value pair
				if ( map->findValueByKey( keyValueHeader.key, keyValueHeader.keySize, &keyValue, 0, 0, 0, 0, false, false ) ) {
					keyValue.deserialize( key.data, key.size, valueStr, valueSize );
					assert( valueSize == keyValueHeader.valueSize );
					memcpy( valueStr, keyValueHeader.value, valueSize );
				} else {
					__ERROR__( "SlaveWorker", "handleSetChunkRequest", "The updated key-value pair does not exist." );
				}

				offset += PROTO_KEY_VALUE_SIZE + keyValueHeader.keySize + keyValueHeader.valueSize;
			}
		}
	} else {
		// Replace chunk contents for parity chunks
		chunk->loadFromSetChunkRequest(
			header.chunkData.data,
			header.chunkData.size,
			header.chunkData.offset,
			header.chunkData.chunkId >= SlaveWorker::dataChunkCount
		);
	}

	UNLOCK( cacheLock );
	UNLOCK( keysLock );
	if ( chunkBufferIndex != -1 )
		chunkBuffer->updateAndUnlockChunk( chunkBufferIndex );

	if ( notifyCoordinator ) {
		SlaveWorker::eventQueue->insert( coordinatorEvent );
	}

	if ( isSealed || header.chunkKeyValue.isCompleted ) {
		event.resSetChunk( event.socket, event.id, metadata, ret );
		this->dispatch( event );
	}

	return ret;
}


bool SlaveWorker::handleUpdateChunkRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkUpdateHeader header;
	bool ret;
	if ( ! this->protocol.parseChunkUpdateHeader( header, true, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleUpdateChunkRequest", "Invalid UPDATE_CHUNK request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleUpdateChunkRequest",
		"[UPDATE_CHUNK] List ID: %u; stripe ID: %u; chunk ID: %u; offset: %u; length: %u; updating chunk ID: %u",
		header.listId, header.stripeId, header.chunkId,
		header.offset, header.length,
		header.updatingChunkId
	);

	SlaveWorker::chunkBuffer->at( header.listId )->update(
		header.stripeId, header.chunkId,
		header.offset, header.length, header.delta,
		this->chunks, this->dataChunk, this->parityChunk
	);
	ret = true;

	Metadata metadata;
	metadata.set( header.listId, header.stripeId, header.chunkId );

	event.resUpdateChunk(
		event.socket, event.id, metadata,
		header.offset, header.length,
		header.updatingChunkId, ret
	);
	this->dispatch( event );

	return ret;
}

bool SlaveWorker::handleDeleteChunkRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkUpdateHeader header;
	bool ret;
	if ( ! this->protocol.parseChunkUpdateHeader( header, true, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleDeleteChunkRequest", "Invalid DELETE_CHUNK request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleDeleteChunkRequest",
		"[DELETE_CHUNK] List ID: %u, stripe ID: %u, chunk ID: %u; offset: %u; length: %u; updating chunk ID: %u.",
		header.listId, header.stripeId, header.chunkId,
		header.offset, header.length, header.updatingChunkId
	);

	SlaveWorker::chunkBuffer->at( header.listId )->update(
		header.stripeId, header.chunkId,
		header.offset, header.length, header.delta,
		this->chunks, this->dataChunk, this->parityChunk,
		true /* isDelete */
	);
	ret = true;

	Metadata metadata;
	metadata.set( header.listId, header.stripeId, header.chunkId );

	event.resDeleteChunk(
		event.socket, event.id, metadata,
		header.offset, header.length,
		header.updatingChunkId, ret
	);

	this->dispatch( event );

	return ret;
}

////////////////////////////////////////////////////////////////////////////////

bool SlaveWorker::handleSealChunkRequest( SlavePeerEvent event, char *buf, size_t size ) {
	struct ChunkSealHeader header;
	bool ret;
	if ( ! this->protocol.parseChunkSealHeader( header, buf, size ) ) {
		__ERROR__( "SlaveWorker", "handleSealChunkRequest", "Invalid SEAL_CHUNK request." );
		return false;
	}
	__DEBUG__(
		BLUE, "SlaveWorker", "handleSealChunkRequest",
		"[SEAL_CHUNK] List ID: %u, stripe ID: %u, chunk ID: %u; count = %u.",
		header.listId, header.stripeId, header.chunkId, header.count
	);

	ret = SlaveWorker::chunkBuffer->at( header.listId )->seal(
		header.stripeId, header.chunkId, header.count,
		buf + PROTO_CHUNK_SEAL_SIZE,
		size - PROTO_CHUNK_SEAL_SIZE,
		this->chunks, this->dataChunk, this->parityChunk
	);

	if ( ! ret )
		__ERROR__( "SlaveWorker", "handleSealChunkRequest", "Cannot update parity chunk." );

	return true;
}

bool SlaveWorker::issueSealChunkRequest( Chunk *chunk, uint32_t startPos ) {
	// printf(
	// 	"issueSealChunkRequest(): (%u, %u, %u)\n",
	// 	chunk->metadata.listId,
	// 	chunk->metadata.stripeId,
	// 	chunk->metadata.chunkId
	// );
	// The chunk is locked when this function is called
	// Only issue seal chunk request when new key-value pairs are received
	if ( SlaveWorker::parityChunkCount && startPos < chunk->getSize() ) {
		size_t size;
		uint32_t requestId = SlaveWorker::idGenerator->nextVal( this->workerId );
		Packet *packet = SlaveWorker::packetPool->malloc();
		packet->setReferenceCount( SlaveWorker::parityChunkCount );

		// Find parity slaves
		this->getSlaves( chunk->metadata.listId );

		// Prepare seal chunk request
		this->protocol.reqSealChunk( size, requestId, chunk, startPos, packet->data );
		packet->size = ( uint32_t ) size;

		if ( size == PROTO_HEADER_SIZE + PROTO_CHUNK_SEAL_SIZE ) {
			packet->setReferenceCount( 1 );
			SlaveWorker::packetPool->free( packet );
			return true;
		}

		for ( uint32_t i = 0; i < SlaveWorker::parityChunkCount; i++ ) {
			SlavePeerEvent slavePeerEvent;
			slavePeerEvent.send( this->paritySlaveSockets[ i ], packet );

#ifdef SLAVE_WORKER_SEND_REPLICAS_PARALLEL
			if ( i == SlaveWorker::parityChunkCount - 1 )
				this->dispatch( slavePeerEvent );
			else
				SlaveWorker::eventQueue->prioritizedInsert( slavePeerEvent );
#else
			this->dispatch( slavePeerEvent );
#endif
		}
	}
	return true;
}