#include <ctime>
#include <ctype.h>
#include <utility>
#include "worker.hh"
#include "../main/master.hh"
#include "../remap/basic_remap_scheme.hh"

uint32_t MasterWorker::dataChunkCount;
uint32_t MasterWorker::parityChunkCount;
uint32_t MasterWorker::updateInterval;
bool MasterWorker::disableRemappingSet;
bool MasterWorker::degradedTargetIsFixed;
IDGenerator *MasterWorker::idGenerator;
Pending *MasterWorker::pending;
MasterEventQueue *MasterWorker::eventQueue;
StripeList<SlaveSocket> *MasterWorker::stripeList;
PacketPool *MasterWorker::packetPool;
ArrayMap<int, SlaveSocket> *MasterWorker::slaveSockets;
MasterRemapMsgHandler *MasterWorker::remapMsgHandler;

void MasterWorker::dispatch( MixedEvent event ) {
	switch( event.type ) {
		case EVENT_TYPE_APPLICATION:
			this->dispatch( event.event.application );
			break;
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
			break;
	}
}

void MasterWorker::dispatch( MasterEvent event ) {}

SlaveSocket *MasterWorker::getSlaves( char *data, uint8_t size, uint32_t &listId, uint32_t &chunkId ) {
	SlaveSocket *ret;
	Key key;
	key.set( size, data );
	listId = MasterWorker::stripeList->get(
		data, ( size_t ) size,
		this->dataSlaveSockets,
		this->paritySlaveSockets,
		&chunkId, true
	);
	ret = this->dataSlaveSockets[ chunkId ];
	return ret->ready() ? ret : 0;
}

bool MasterWorker::getSlaves(
	uint8_t opcode, char *data, uint8_t size,
	uint32_t *&original, uint32_t *&remapped, uint32_t &remappedCount,
	SlaveSocket *&originalDataSlaveSocket, bool &useCoordinatedFlow
) {
	bool ret = true;

	useCoordinatedFlow = false;

	// Determine original data slave
	uint32_t originalListId, originalChunkId;
	originalListId = MasterWorker::stripeList->get(
		data, ( size_t ) size,
		this->dataSlaveSockets,
		this->paritySlaveSockets,
		&originalChunkId, true
	);
	originalDataSlaveSocket = this->dataSlaveSockets[ originalChunkId ];

	Master *master = Master::getInstance();
	switch( opcode ) {
		case PROTO_OPCODE_SET:
			// already checked in MasterWorker::handleSetRequest()
			useCoordinatedFlow = true;
			break;
		case PROTO_OPCODE_GET:
		// 	if ( master->isDegraded( originalDataSlaveSocket ) )
		// 		useCoordinatedFlow = true;
		// 	break;
		case PROTO_OPCODE_UPDATE:
		case PROTO_OPCODE_DELETE:
			for ( uint32_t i = 0; i < 1 + MasterWorker::parityChunkCount; i++ ) {
				if ( master->isDegraded(
					( i == 0 ) ? originalDataSlaveSocket : this->paritySlaveSockets[ i - 1 ] )
				) {
					useCoordinatedFlow = true;
					break;
				}
			}
			break;
	}

	if ( ! useCoordinatedFlow ) {
		remappedCount = 0;
		original = 0;
		remapped = 0;
		return ret;
	}

	original = this->original;
	remapped = this->remapped;

	original[ 0 ] = originalListId;
	original[ 1 ] = originalChunkId;
	for ( uint32_t i = 0; i < MasterWorker::parityChunkCount; i++ ) {
		original[ ( i + 1 ) * 2 ] = originalListId;
		original[ ( i + 1 ) * 2 + 1 ] = MasterWorker::dataChunkCount + i;
	}

	// Determine remapped data slave
	BasicRemappingScheme::getRemapTarget(
		this->original, this->remapped, remappedCount,
		MasterWorker::dataChunkCount, MasterWorker::parityChunkCount,
		this->dataSlaveSockets, this->paritySlaveSockets
	);

	if ( remappedCount ) {
		uint32_t *_original = new uint32_t[ remappedCount * 2 ];
		uint32_t *_remapped = new uint32_t[ remappedCount * 2 ];
		for ( uint32_t i = 0; i < remappedCount * 2; i++ ) {
			_original[ i ] = original[ i ];
			_remapped[ i ] = remapped[ i ];
		}
		original = _original;
		remapped = _remapped;
	} else {
		original = 0;
		remapped = 0;
	}

	return ret;
}

SlaveSocket *MasterWorker::getSlaves( uint32_t listId, uint32_t chunkId ) {
	SlaveSocket *ret;
	MasterWorker::stripeList->get( listId, this->paritySlaveSockets, this->dataSlaveSockets );
	ret = chunkId < MasterWorker::dataChunkCount ? this->dataSlaveSockets[ chunkId ] : this->paritySlaveSockets[ chunkId - MasterWorker::dataChunkCount ];
	return ret->ready() ? ret : 0;
}

/*
SlaveSocket *MasterWorker::getSlaves( char *data, uint8_t size, uint32_t &listId, uint32_t &chunkId, uint32_t &newChunkId, bool &useDegradedMode, SlaveSocket *&original ) {
	useDegradedMode = false;
	original = this->getSlaves( data, size, listId, chunkId );
	newChunkId = chunkId;
	if ( Master::getInstance()->isDegraded( original ) ) {
		// Perform degraded operation
		useDegradedMode = true;
		if ( MasterWorker::degradedTargetIsFixed ) {
			if ( ! BasicRemappingScheme::isOverloaded( original ) || ! Master::getInstance()->remapMsgHandler.allowRemapping( original->getAddr() ) )
				return original; // not overloaded

			// Pick a new server from the same stripe list to handle the request
			for ( uint32_t jump = 0, chunkCount = MasterWorker::dataChunkCount + MasterWorker::parityChunkCount; jump < chunkCount; jump++ ) {
				SlaveSocket *target = MasterWorker::stripeList->get( listId, chunkId, jump, &newChunkId );
				if ( chunkId == newChunkId )
					continue;
				if ( target && target->ready() )
					return target;
			}
			__ERROR__( "MasterWorker", "getSlaves", "No slaves are available for degraded operations." );
			return 0;
		} else {
			BasicRemappingScheme::getDegradedOpTarget(
				listId, chunkId, newChunkId,
				MasterWorker::dataChunkCount,
				MasterWorker::parityChunkCount,
				this->dataSlaveSockets,
				this->paritySlaveSockets
			);
			if ( newChunkId < MasterWorker::dataChunkCount )
				return this->dataSlaveSockets[ newChunkId ];
			return this->paritySlaveSockets[ newChunkId - MasterWorker::dataChunkCount ];
		}
	}
	return original;
}

SlaveSocket *MasterWorker::getSlaves( char *data, uint8_t size, uint32_t &listId, uint32_t &dataChunkId, uint32_t &newDataChunkId, uint32_t &parityChunkId, uint32_t &newParityChunkId, bool &useDegradedMode ) {
	SlaveSocket *socket;
	bool found = false;

	useDegradedMode = false;
	this->getSlaves( data, size, listId, dataChunkId );
	newDataChunkId = dataChunkId;
	parityChunkId = newParityChunkId = 0; // baseline: no need to redirect parity servers

	for ( uint32_t i = 0; i < MasterWorker::parityChunkCount + 1; i++ ) {
		// Already redirect a data or parity slave
		if ( useDegradedMode )
			break;

		socket = i == 0 ? this->dataSlaveSockets[ dataChunkId ] : this->paritySlaveSockets[ i - 1 ];
		if ( Master::getInstance()->isDegraded( socket ) ) {
			// Perform degraded operation
			useDegradedMode = true;

			uint32_t &chunkId = i == 0 ? dataChunkId : parityChunkId;
			uint32_t &newChunkId = i == 0 ? newDataChunkId : newParityChunkId;

			if ( i != 0 )
				chunkId = ( MasterWorker::dataChunkCount + i - 1 ); // set parity chunk ID

			if ( MasterWorker::degradedTargetIsFixed ) {
				if ( ! BasicRemappingScheme::isOverloaded( socket ) || ! Master::getInstance()->remapMsgHandler.allowRemapping( socket->getAddr() ) ) {
					useDegradedMode = false;
					continue;
				}

				// Pick a new server from the same stripe list to handle the request
				for ( uint32_t jump = 0, chunkCount = MasterWorker::dataChunkCount + MasterWorker::parityChunkCount; jump < chunkCount; jump++ ) {
					SlaveSocket *target = MasterWorker::stripeList->get( listId, chunkId, jump, &newChunkId );
					if ( chunkId == newChunkId )
						continue;
					if ( target && target->ready() ) {
						found = true;
						break;
					}
				}

				if ( ! found )
					__ERROR__( "MasterWorker", "getSlaves", "No slaves are available for degraded operations." );
				break;
			} else {
				BasicRemappingScheme::getDegradedOpTarget(
					listId, chunkId, newChunkId,
					MasterWorker::dataChunkCount,
					MasterWorker::parityChunkCount,
					this->dataSlaveSockets,
					this->paritySlaveSockets
				);
			}
		}
	}

	socket = this->dataSlaveSockets[ newDataChunkId ];
	return socket->ready() ? socket : 0;
}
*/

void MasterWorker::removePending( SlaveSocket *slave, bool needsAck ) {

	struct sockaddr_in saddr = slave->getAddr();
	char buf[ INET_ADDRSTRLEN ];
	inet_ntop( AF_INET, &saddr.sin_addr.s_addr, buf, INET_ADDRSTRLEN );
	// remove pending ack
	std::vector<AcknowledgementInfo> ackInfoList;
	// remove parity backup ack
	MasterWorker::pending->eraseAck( PT_ACK_REMOVE_PARITY, slave->instanceId, &ackInfoList );
	for ( AcknowledgementInfo &it : ackInfoList ) {
		if ( it.lock ) LOCK( it.lock );
		if ( it.counter ) *it.counter -= 1;
		if ( it.lock ) UNLOCK( it.lock );
	}
	// revert parity ack
	ackInfoList.clear();
	MasterWorker::pending->eraseAck( PT_ACK_REVERT_PARITY, slave->instanceId, &ackInfoList );
	for ( AcknowledgementInfo &it : ackInfoList ) {
		if ( it.lock ) LOCK( it.lock );
		if ( it.counter ) *it.counter -= 1;
		if ( it.lock ) UNLOCK( it.lock );
	}

	if ( needsAck )
		Master::getInstance()->remapMsgHandler.ackTransit();
}

void MasterWorker::replayRequestPrepare( SlaveSocket *slave ) {
	uint16_t instanceId = slave->instanceId;
	Pending *pending = MasterWorker::pending;
	if ( pending->replay.requestsLock.count( instanceId ) == 0 ) {
		pending->replay.requestsLock[ instanceId ] = LOCK_T();
		LOCK_INIT( &pending->replay.requestsLock.at( instanceId ) );
	}
	if ( pending->replay.requests.count( instanceId ) == 0 ) {
		pending->replay.requests[ instanceId ] = std::map<uint32_t, RequestInfo>();
	}
	LOCK_T *lock = &pending->replay.requestsLock.at( instanceId );
	std::map<uint32_t, RequestInfo> *map = &pending->replay.requests.at( instanceId );
	LOCK_T *pendingLock;
	RequestInfo requestInfo;
	PendingIdentifier pid;

#define SEARCH_MAP_FOR_REQUEST( _OPCODE_, _SLAVE_VALUE_TYPE_, _APPLICATION_VALUE_TYPE_, _APPLICATION_VALUE_VAR_, _PENDING_TYPE_, _PENDING_SET_NAME_ ) \
	do { \
		_APPLICATION_VALUE_TYPE_ _APPLICATION_VALUE_VAR_; \
		bool needsDup = false; \
		LOCK( pendingLock ); \
		for ( \
			std::unordered_multimap<PendingIdentifier, _SLAVE_VALUE_TYPE_>::iterator it = pending->slaves._PENDING_SET_NAME_.begin(), safeIt = it; \
			it != pending->slaves._PENDING_SET_NAME_.end(); it = safeIt \
		) { \
			/* hold a save ptr for safe erase */ \
			safeIt++; \
			/* skip requests other than those associated with target slave */ \
			if ( it->first.ptr != slave ) \
				continue; \
			/* skip request if backup is not available */ \
			if ( ! pending->erase##_APPLICATION_VALUE_TYPE_( PT_APPLICATION_##_PENDING_TYPE_, it->first.parentInstanceId, it->first.parentRequestId, ( void* ) 0, &pid, &_APPLICATION_VALUE_VAR_, true, true, true, it->second.data ) ) { \
				__ERROR__( "MasterWorker", "replayRequestPrepare", "Cannot find the SET request backup for ID = (%u, %u).", it->first.parentInstanceId, it->first.parentRequestId ); \
				continue; \
			} \
			/* cancel the request reply to application by setting application socket to 0 */ \
			if ( MasterWorker::pending->count( PT_SLAVE_##_PENDING_TYPE_, it->first.instanceId, it->first.requestId, false, false /* already locked (pendingLock) */ ) > 1 ) { \
				__DEBUG__( YELLOW, "MasterWorker", "replayRequestPrepare", "Reinsert ID = (%u,%u)\n", ( ( SlaveSocket * ) it->first.ptr )->getSocket(), it->first.parentInstanceId, it->first.parentRequestId ); \
				if ( ! MasterWorker::pending->insert##_APPLICATION_VALUE_TYPE_( PT_APPLICATION_##_PENDING_TYPE_, pid.instanceId, pid.requestId, pid.ptr, _APPLICATION_VALUE_VAR_ ) ) \
					__ERROR__( "MasterWorker", "replayRequestPrepare", "Failed to reinsert the %s request backup for ID = (%u, %u).", #_OPCODE_, pid.instanceId, pid.requestId ); \
				else \
					needsDup = true; \
			} \
			__DEBUG__( YELLOW, "MasterWorker", "replayRequestPrepare", "Add %s request ID = (%u,%u) with timestamp %u to replay.", #_OPCODE_, pid.instanceId, pid.requestId, pid.timestamp ); \
			/* mark the first timestamp to start the replay */ \
			if ( pending->replay.requestsStartTime.count( instanceId ) == 0 ) { \
				pending->replay.requestsStartTime[ instanceId ] = pid.timestamp; \
			} \
			/* insert the request into pending set for replay */ \
			requestInfo.set( pid.ptr, pid.instanceId, pid.requestId, PROTO_OPCODE_##_OPCODE_, _APPLICATION_VALUE_VAR_, !needsDup ); \
			map->insert( std::pair<uint32_t, RequestInfo>( pid.timestamp, requestInfo ) ); \
			/* remove the pending ack */ \
			pending->slaves._PENDING_SET_NAME_.erase( it ); \
		} \
		UNLOCK( pendingLock ); \
	} while( 0 )

	LOCK( lock );
	// SET
	pendingLock = &pending->slaves.setLock;
	SEARCH_MAP_FOR_REQUEST( SET, Key, KeyValue, keyValue, SET, set );
	// GET
	pendingLock = &pending->slaves.getLock;
	SEARCH_MAP_FOR_REQUEST( GET, Key, Key, key, GET, get );
	// UPDATE
	pendingLock = &pending->slaves.updateLock;
	SEARCH_MAP_FOR_REQUEST( UPDATE, KeyValueUpdate, KeyValueUpdate, keyValueUpdate, UPDATE, update );
	// DELETE
	pendingLock = &pending->slaves.delLock;
	SEARCH_MAP_FOR_REQUEST( DELETE, Key, Key, key, DEL, del );

	UNLOCK( lock );

#undef SEARCH_MAP_FOR_REQUEST
}

void MasterWorker::replayRequest( SlaveSocket *slave ) {
	uint16_t instanceId = slave->instanceId;

	if (
		MasterWorker::pending->replay.requestsLock.count( instanceId ) == 0 ||
		MasterWorker::pending->replay.requests.count( instanceId ) == 0
	) {
		__ERROR__( "MasterWorker", "replayRequest", "Cannot replay request for slave with id = %u.", instanceId );
		return;
	}
	LOCK_T *lock = &MasterWorker::pending->replay.requestsLock.at( instanceId );

	std::map<uint32_t, RequestInfo> *map = &MasterWorker::pending->replay.requests.at( instanceId );
	std::map<uint32_t, RequestInfo>::iterator lit, rit;
	ApplicationEvent event;

	LOCK( lock );

	if ( ! MasterWorker::pending->replay.requestsStartTime.count( instanceId ) || map->empty() ) {
		UNLOCK( lock );
		return;
	}

	// from the first timestamp, to the first timestamp
	lit = map->find( pending->replay.requestsStartTime[ instanceId ] );
	rit = lit;
	// replay requests
	do {
		switch ( lit->second.opcode ) {
			case PROTO_OPCODE_SET:
				event.replaySetRequest(
					( ApplicationSocket * ) lit->second.application,
					lit->second.instanceId, lit->second.requestId,
					lit->second.keyValue
				);
				break;
			case PROTO_OPCODE_GET:
				event.replayGetRequest(
					( ApplicationSocket * ) lit->second.application,
					lit->second.instanceId, lit->second.requestId,
					lit->second.key
				);
				break;
			case PROTO_OPCODE_UPDATE:
				event.replayUpdateRequest(
					( ApplicationSocket * ) lit->second.application,
					lit->second.instanceId, lit->second.requestId,
					lit->second.keyValueUpdate
				);
				break;
			case PROTO_OPCODE_DELETE:
				event.replayDeleteRequest(
					( ApplicationSocket * ) lit->second.application,
					lit->second.instanceId, lit->second.requestId,
					lit->second.key
				);
				break;
			default:
				__ERROR__( "MasterWorker", "replayRequest", "Unknown request OPCODE = 0x%0x\n", lit->second.opcode );
				continue;
		}
		MasterWorker::eventQueue->insert( event );
		lit++;
		if ( lit == map->end() )
			lit = map->begin();
	} while( lit != rit && lit != map->end() );

	map->clear();
	pending->replay.requestsStartTime.erase( instanceId );

	UNLOCK( lock );
}

void MasterWorker::free() {
	this->protocol.free();
	delete[] original;
	delete[] remapped;
	delete[] this->dataSlaveSockets;
	delete[] this->paritySlaveSockets;
}

void *MasterWorker::run( void *argv ) {
	MasterWorker *worker = ( MasterWorker * ) argv;
	WorkerRole role = worker->getRole();
	MasterEventQueue *eventQueue = MasterWorker::eventQueue;

#define MASTER_WORKER_EVENT_LOOP(_EVENT_TYPE_, _EVENT_QUEUE_) \
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
			// MASTER_WORKER_EVENT_LOOP(
			// 	MixedEvent,
			// 	eventQueue->mixed
			// );
		{
			MixedEvent event;
			bool ret;
			while( worker->getIsRunning() | ( ret = eventQueue->extractMixed( event ) ) ) {
				if ( ret )
					worker->dispatch( event );
			}
		}
			break;
		case WORKER_ROLE_APPLICATION:
			MASTER_WORKER_EVENT_LOOP(
				ApplicationEvent,
				eventQueue->separated.application
			);
			break;
		case WORKER_ROLE_COORDINATOR:
			MASTER_WORKER_EVENT_LOOP(
				CoordinatorEvent,
				eventQueue->separated.coordinator
			);
			break;
		case WORKER_ROLE_MASTER:
			MASTER_WORKER_EVENT_LOOP(
				MasterEvent,
				eventQueue->separated.master
			);
			break;
		case WORKER_ROLE_SLAVE:
			MASTER_WORKER_EVENT_LOOP(
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

bool MasterWorker::init() {
	Master *master = Master::getInstance();

	MasterWorker::idGenerator = &master->idGenerator;
	MasterWorker::dataChunkCount = master->config.global.coding.params.getDataChunkCount();
	MasterWorker::parityChunkCount = master->config.global.coding.params.getParityChunkCount();
	MasterWorker::updateInterval = master->config.master.loadingStats.updateInterval;
	MasterWorker::disableRemappingSet = master->config.master.remap.disableRemappingSet;
	MasterWorker::degradedTargetIsFixed = master->config.master.degraded.isFixed;
	MasterWorker::pending = &master->pending;
	MasterWorker::eventQueue = &master->eventQueue;
	MasterWorker::stripeList = master->stripeList;
	MasterWorker::slaveSockets = &master->sockets.slaves;
	MasterWorker::packetPool = &master->packetPool;
	MasterWorker::remapMsgHandler = &master->remapMsgHandler;
	return true;
}

bool MasterWorker::init( GlobalConfig &config, WorkerRole role, uint32_t workerId ) {
	this->protocol.init(
		Protocol::getSuggestedBufferSize(
			config.size.key,
			config.size.chunk
		)
	);
	this->original = new uint32_t[ ( MasterWorker::dataChunkCount + MasterWorker::parityChunkCount ) * 2 ];
	this->remapped = new uint32_t[ ( MasterWorker::dataChunkCount + MasterWorker::parityChunkCount ) * 2 ];
	this->dataSlaveSockets = new SlaveSocket*[ MasterWorker::dataChunkCount ];
	this->paritySlaveSockets = new SlaveSocket*[ MasterWorker::parityChunkCount ];
	this->role = role;
	this->workerId = workerId;
	return role != WORKER_ROLE_UNDEFINED;
}

bool MasterWorker::start() {
	this->isRunning = true;
	if ( pthread_create( &this->tid, NULL, MasterWorker::run, ( void * ) this ) != 0 ) {
		__ERROR__( "MasterWorker", "start", "Cannot start worker thread." );
		return false;
	}
	return true;
}

void MasterWorker::stop() {
	this->isRunning = false;
}

void MasterWorker::print( FILE *f ) {
	char role[ 16 ];
	switch( this->role ) {
		case WORKER_ROLE_MIXED:
			strcpy( role, "Mixed" );
			break;
		case WORKER_ROLE_APPLICATION:
			strcpy( role, "Application" );
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
