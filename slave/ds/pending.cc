#include "pending.hh"
#include "../../common/util/debug.hh"

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, RemappingRecordKey> *&map ) {
	switch( type ) {
		case PT_MASTER_REMAPPING_SET:
			lock = &this->masters.remappingSetLock;
			map = &this->masters.remappingSet;
			break;
		case PT_SLAVE_PEER_REMAPPING_SET:
			lock = &this->slavePeers.remappingSetLock;
			map = &this->slavePeers.remappingSet;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, Key> *&map ) {
	switch( type ) {
		case PT_MASTER_GET:
			lock = &this->masters.getLock;
			map = &this->masters.get;
			break;
		case PT_MASTER_DEL:
			lock = &this->masters.delLock;
			map = &this->masters.del;
			break;
		case PT_SLAVE_PEER_GET:
			lock = &this->slavePeers.getLock;
			map = &this->slavePeers.get;
			break;
		case PT_SLAVE_PEER_DEL:
			lock = &this->slavePeers.delLock;
			map = &this->slavePeers.del;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, KeyValueUpdate> *&map ) {
	switch( type ) {
		case PT_MASTER_UPDATE:
			lock = &this->masters.updateLock;
			map = &this->masters.update;
			break;
		case PT_SLAVE_PEER_UPDATE:
			lock = &this->slavePeers.updateLock;
			map = &this->slavePeers.update;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, DegradedOp> *&map ) {
	switch( type ) {
		case PT_SLAVE_PEER_DEGRADED_OPS:
			lock = &this->slavePeers.degradedOpsLock;
			map = &this->slavePeers.degradedOps;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, ChunkRequest> *&map ) {
	switch( type ) {
		case PT_SLAVE_PEER_GET_CHUNK:
			lock = &this->slavePeers.getChunkLock;
			map = &this->slavePeers.getChunk;
			break;
		case PT_SLAVE_PEER_SET_CHUNK:
			lock = &this->slavePeers.setChunkLock;
			map = &this->slavePeers.setChunk;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

bool Pending::get( PendingType type, LOCK_T *&lock, std::unordered_multimap<PendingIdentifier, ChunkUpdate> *&map ) {
	switch( type ) {
		case PT_SLAVE_PEER_UPDATE_CHUNK:
			lock = &this->slavePeers.updateChunkLock;
			map = &this->slavePeers.updateChunk;
			break;
		case PT_SLAVE_PEER_DEL_CHUNK:
			lock = &this->slavePeers.delChunkLock;
			map = &this->slavePeers.deleteChunk;
			break;
		default:
			lock = 0;
			map = 0;
			return false;
	}
	return true;
}

#define DEFINE_PENDING_MASTER_INSERT_METHOD( METHOD_NAME, VALUE_TYPE, VALUE_VAR ) \
	bool Pending::METHOD_NAME( PendingType type, uint32_t id, void *ptr, VALUE_TYPE &VALUE_VAR, bool needsLock, bool needsUnlock ) { \
		PendingIdentifier pid( id, id, ptr ); \
		std::pair<PendingIdentifier, VALUE_TYPE> p( pid, VALUE_VAR ); \
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE>::iterator ret; \
 		\
		LOCK_T *lock; \
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE> *map; \
		if ( ! this->get( type, lock, map ) ) \
			return false; \
 		\
		if ( needsLock ) LOCK( lock ); \
		ret = map->insert( p ); \
		if ( needsUnlock ) UNLOCK( lock ); \
 		\
		return true; /* ret.second; */ \
	}

#define DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( METHOD_NAME, VALUE_TYPE, VALUE_VAR ) \
	bool Pending::METHOD_NAME( PendingType type, uint32_t id, uint32_t parentId, void *ptr, VALUE_TYPE &VALUE_VAR, bool needsLock, bool needsUnlock ) { \
		PendingIdentifier pid( id, parentId, ptr ); \
		std::pair<PendingIdentifier, VALUE_TYPE> p( pid, VALUE_VAR ); \
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE>::iterator ret; \
 		\
		LOCK_T *lock; \
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE> *map; \
		if ( ! this->get( type, lock, map ) ) \
			return false; \
 		\
		if ( needsLock ) LOCK( lock ); \
		ret = map->insert( p ); \
		if ( needsUnlock ) UNLOCK( lock ); \
 		\
		return true; /* ret.second; */ \
	}

#define DEFINE_PENDING_ERASE_METHOD( METHOD_NAME, VALUE_TYPE, VALUE_PTR_VAR ) \
	bool Pending::METHOD_NAME( PendingType type, uint32_t id, void *ptr, PendingIdentifier *pidPtr, VALUE_TYPE *VALUE_PTR_VAR, bool needsLock, bool needsUnlock ) { \
		PendingIdentifier pid( id, 0, ptr ); \
		LOCK_T *lock; \
		bool ret; \
		\
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE> *map; \
		std::unordered_multimap<PendingIdentifier, VALUE_TYPE>::iterator it; \
		if ( ! this->get( type, lock, map ) ) \
			return false; \
		\
		if ( needsLock ) LOCK( lock ); \
		if ( ptr ) { \
			it = map->find( pid ); \
			ret = ( it != map->end() ); \
		} else { \
			it = map->find( pid ); \
			ret = ( it != map->end() && it->first.id == id ); \
		} \
		if ( ret ) { \
			if ( pidPtr ) *pidPtr = it->first; \
			if ( VALUE_PTR_VAR ) *VALUE_PTR_VAR = it->second; \
			map->erase( it ); \
		} \
		if ( needsUnlock ) UNLOCK( lock ); \
		\
		return ret; \
	}

DEFINE_PENDING_MASTER_INSERT_METHOD( insertRemappingRecordKey, RemappingRecordKey, remappingRecordKey )
DEFINE_PENDING_MASTER_INSERT_METHOD( insertKey, Key, key )
DEFINE_PENDING_MASTER_INSERT_METHOD( insertKeyValueUpdate, KeyValueUpdate, keyValueUpdate )

DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertRemappingRecordKey, RemappingRecordKey, remappingRecordKey )
DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertKey, Key, key )
DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertKeyValueUpdate, KeyValueUpdate, keyValueUpdate )
DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertDegradedOp, DegradedOp, degradedOp )
DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertChunkRequest, ChunkRequest, chunkRequest )
DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD( insertChunkUpdate, ChunkUpdate, chunkUpdate )

DEFINE_PENDING_ERASE_METHOD( eraseRemappingRecordKey, RemappingRecordKey, remappingRecordKeyPtr )
DEFINE_PENDING_ERASE_METHOD( eraseKey, Key, keyPtr )
DEFINE_PENDING_ERASE_METHOD( eraseKeyValueUpdate, KeyValueUpdate, keyValueUpdatePtr )
DEFINE_PENDING_ERASE_METHOD( eraseDegradedOp, DegradedOp, degradedOpPtr )
DEFINE_PENDING_ERASE_METHOD( eraseChunkRequest, ChunkRequest, chunkRequestPtr )
DEFINE_PENDING_ERASE_METHOD( eraseChunkUpdate, ChunkUpdate, chunkUpdatePtr )

#undef DEFINE_PENDING_MASTER_INSERT_METHOD
#undef DEFINE_PENDING_SLAVE_PEER_INSERT_METHOD
#undef DEFINE_PENDING_ERASE_METHOD

void Pending::insertReleaseDegradedLock( uint32_t id, CoordinatorSocket *socket, uint32_t count ) {
	PendingIdentifier pid( id, id, socket );
	std::unordered_map<PendingIdentifier, PendingDegradedLock>::iterator it;

	LOCK( &this->coordinators.releaseDegradedLockLock );
	it = this->coordinators.releaseDegradedLock.find( pid );
	if ( it == this->coordinators.releaseDegradedLock.end() ) {
		PendingDegradedLock d;
		d.count = count;
		d.total = count;

		this->coordinators.releaseDegradedLock[ pid ] = d;
	} else {
		it->second.count += count;
		it->second.total += count;
	}
	UNLOCK( &this->coordinators.releaseDegradedLockLock );
}

bool Pending::insertReconstruction( uint32_t id, CoordinatorSocket *socket, uint32_t listId, uint32_t chunkId, std::unordered_set<uint32_t> &stripeIds ) {
	PendingIdentifier pid( id, id, socket );
	PendingReconstruction r;
	r.set( listId, chunkId, stripeIds );
	std::unordered_map<PendingIdentifier, PendingReconstruction>::iterator it;
	bool ret;

	LOCK( &this->coordinators.reconstructionLock );
	it = this->coordinators.reconstruction.find( pid );
	if ( it == this->coordinators.reconstruction.end() ) {
		std::pair<PendingIdentifier, PendingReconstruction> p( pid, r );
		std::pair<std::unordered_map<PendingIdentifier, PendingReconstruction>::iterator, bool> r;
		r = this->coordinators.reconstruction.insert( p );
		ret = r.second;
	} else {
		PendingReconstruction &reconstruction = it->second;
		if ( reconstruction.listId == listId && reconstruction.chunkId == chunkId ) {
			reconstruction.merge( stripeIds );
			ret = true;
		} else {
			ret = false;
		}
	}
	UNLOCK( &this->coordinators.reconstructionLock );

	return ret;
}

bool Pending::insertRecovery( uint32_t id, CoordinatorSocket *socket, uint32_t addr, uint16_t port, uint32_t count, uint32_t *buf ) {
	PendingIdentifier pid( id, id, socket );
	PendingRecovery r( addr, port );

	std::unordered_map<PendingIdentifier, PendingRecovery>::iterator it;
	bool ret;

	LOCK( &this->coordinators.recoveryLock );
	it = this->coordinators.recovery.find( pid );
	if ( it == this->coordinators.recovery.end() ) {
		r.insert( count, buf );

		std::pair<PendingIdentifier, PendingRecovery> p( pid, r );
		std::pair<std::unordered_map<PendingIdentifier, PendingRecovery>::iterator, bool> r;
		r = this->coordinators.recovery.insert( p );
		ret = r.second;
	} else {
		it->second.insert( count, buf );
		ret = true;
	}
	UNLOCK( &this->coordinators.recoveryLock );

	return ret;
}

bool Pending::eraseReleaseDegradedLock( uint32_t id, uint32_t count, uint32_t &remaining, uint32_t &total, PendingIdentifier *pidPtr ) {
	PendingIdentifier pid( id, id, 0 );
	std::unordered_map<PendingIdentifier, PendingDegradedLock>::iterator it;

	LOCK( &this->coordinators.releaseDegradedLockLock );
	it = this->coordinators.releaseDegradedLock.find( pid );
	if ( it == this->coordinators.releaseDegradedLock.end() ) {
		UNLOCK( &this->coordinators.releaseDegradedLockLock );
		return false;
	}
	if ( pidPtr ) *pidPtr = it->first;
	it->second.count -= count;
	remaining = it->second.count;
	total = it->second.total;
	UNLOCK( &this->coordinators.releaseDegradedLockLock );

	return true;
}

bool Pending::eraseReconstruction( uint32_t id, CoordinatorSocket *&socket, uint32_t listId, uint32_t stripeId, uint32_t chunkId, uint32_t &remaining, uint32_t &total, PendingIdentifier *pidPtr ) {
	PendingIdentifier pid( id, id, 0 );
	std::unordered_map<PendingIdentifier, PendingReconstruction>::iterator it;
	bool ret;

	LOCK( &this->coordinators.reconstructionLock );
	it = this->coordinators.reconstruction.find( pid );
	if ( it == this->coordinators.reconstruction.end() ) {
		UNLOCK( &this->coordinators.reconstructionLock );
		return false;
	}
	// Check whether the list ID and chunk ID match
	if ( listId == it->second.listId && chunkId == it->second.chunkId ) {
		size_t count;
		if ( pidPtr ) *pidPtr = it->first;
		socket = ( CoordinatorSocket * ) it->first.ptr;
		count = it->second.stripeIds.erase( stripeId );
		ret = count > 0;
		remaining = it->second.stripeIds.size();
		total = it->second.total;

		if ( remaining == 0 )
			this->coordinators.reconstruction.erase( it );
	} else {
		socket = 0;
		ret = false;
	}
	UNLOCK( &this->coordinators.reconstructionLock );

	return ret;
}

bool Pending::eraseRecovery( uint32_t listId, uint32_t stripeId, uint32_t chunkId, uint32_t &id, CoordinatorSocket *&socket, uint32_t &addr, uint16_t &port, uint32_t &remaining, uint32_t &total ) {
	std::unordered_map<PendingIdentifier, PendingRecovery>::iterator it;
	Metadata metadata;
	bool ret = false;

	metadata.set( listId, stripeId, chunkId );

	LOCK( &this->coordinators.recoveryLock );
	for ( it = this->coordinators.recovery.begin(); it != this->coordinators.recovery.end(); it++ ) {
		const PendingIdentifier &pid = it->first;
		PendingRecovery &recovery = it->second;

		if ( recovery.chunks.erase( metadata ) > 0 ) {
			id = pid.id;
			socket = ( CoordinatorSocket * ) pid.ptr;
			addr = recovery.addr;
			port = recovery.port;
			remaining = recovery.chunks.size();
			total = recovery.total;

			if ( remaining == 0 )
				this->coordinators.recovery.erase( it );

			ret = true;
			break;
		}
	}
	UNLOCK( &this->coordinators.recoveryLock );

	return ret;
}

bool Pending::findReconstruction( uint32_t id, uint32_t stripeId, uint32_t &listId, uint32_t &chunkId ) {
	PendingIdentifier pid( id, id, 0 );
	std::unordered_map<PendingIdentifier, PendingReconstruction>::iterator it;
	size_t count;

	LOCK( &this->coordinators.reconstructionLock );
	it = this->coordinators.reconstruction.find( pid );
	if ( it == this->coordinators.reconstruction.end() ) {
		UNLOCK( &this->coordinators.reconstructionLock );
		return 0;
	}
	listId = it->second.listId;
	chunkId = it->second.chunkId;
	count = it->second.stripeIds.count( stripeId );
	UNLOCK( &this->coordinators.reconstructionLock );

	return count > 0;
}

bool Pending::findChunkRequest( PendingType type, uint32_t id, void *ptr, std::unordered_multimap<PendingIdentifier, ChunkRequest>::iterator &it, bool needsLock, bool needsUnlock ) {
	PendingIdentifier pid( id, 0, ptr );
	LOCK_T *lock;
	bool ret;

	std::unordered_multimap<PendingIdentifier, ChunkRequest> *map;
	if ( ! this->get( type, lock, map ) )
		return false;

	if ( needsLock ) LOCK( lock );
	if ( ptr ) {
		it = map->find( pid );
		ret = ( it != map->end() );
	} else {
		it = map->find( pid );
		ret = ( it != map->end() && it->first.id == id );
	}
	if ( needsUnlock ) UNLOCK( lock );

	return ret;
}

uint32_t Pending::count( PendingType type, uint32_t id, bool needsLock, bool needsUnlock ) {
	PendingIdentifier pid( id, 0, 0 );
	LOCK_T *lock;
	uint32_t ret = 0;
	if ( type == PT_SLAVE_PEER_REMAPPING_SET ) {
		std::unordered_multimap<PendingIdentifier, RemappingRecordKey> *map;
		std::unordered_multimap<PendingIdentifier, RemappingRecordKey>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->count( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else if ( type == PT_SLAVE_PEER_DEGRADED_OPS ) {
		std::unordered_multimap<PendingIdentifier, DegradedOp> *map;
		std::unordered_multimap<PendingIdentifier, DegradedOp>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->count( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else if ( type == PT_SLAVE_PEER_UPDATE ) {
		std::unordered_multimap<PendingIdentifier, KeyValueUpdate> *map;
		std::unordered_multimap<PendingIdentifier, KeyValueUpdate>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->count( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else if ( type == PT_SLAVE_PEER_DEL ) {
		std::unordered_multimap<PendingIdentifier, Key> *map;
		std::unordered_multimap<PendingIdentifier, Key>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->count( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else if ( type == PT_SLAVE_PEER_GET_CHUNK || type == PT_SLAVE_PEER_SET_CHUNK ) {
		std::unordered_multimap<PendingIdentifier, ChunkRequest> *map;
		std::unordered_multimap<PendingIdentifier, ChunkRequest>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->count( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else if ( type == PT_SLAVE_PEER_UPDATE_CHUNK || type == PT_SLAVE_PEER_DEL_CHUNK ) {
		std::unordered_multimap<PendingIdentifier, ChunkUpdate> *map;
		std::unordered_multimap<PendingIdentifier, ChunkUpdate>::iterator it;

		if ( ! this->get( type, lock, map ) ) return 0;

		if ( needsLock ) LOCK( lock );
		ret = map->count( pid );
		// it = map->lower_bound( pid );
		// for ( ret = 0; it != map->end() && it->first.id == id; ret++, it++ );
		if ( needsUnlock ) UNLOCK( lock );
	} else {
		__ERROR__( "Pending", "count", "The count function is not implemented for this type." );
		return 0;
	}

	return ret;
}
