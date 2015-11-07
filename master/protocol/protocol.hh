#ifndef __MASTER_PROTOCOL_PROTOCOL_HH__
#define __MASTER_PROTOCOL_PROTOCOL_HH__

#include "../../common/config/server_addr.hh"
#include "../../common/ds/array_map.hh"
#include "../../common/ds/bitmask_array.hh"
#include "../../common/ds/latency.hh"
#include "../../common/ds/sockaddr_in.hh"
#include "../../common/protocol/protocol.hh"

class MasterProtocol : public Protocol {
public:
	volatile bool *status; // Indicate which slave in the stripe is accessing the internal buffer

	MasterProtocol() : Protocol( ROLE_MASTER ) {}
	bool init( size_t size, uint32_t parityChunkCount );
	void free();

	/* Coordinator */
	// Register
	char *reqRegisterCoordinator( size_t &size, uint32_t id, uint32_t addr, uint16_t port );
	// Loading statistics
	char *reqPushLoadStats( size_t &size, uint32_t id, ArrayMap< struct sockaddr_in, Latency > *slaveGetLatency, ArrayMap< struct sockaddr_in, Latency > *slaveSetLatency );
	bool parseLoadingStats( const LoadStatsHeader& loadStatsHeader, ArrayMap< struct sockaddr_in, Latency > &slaveGetLatency, ArrayMap< struct sockaddr_in, Latency > &slaveSetLatency, std::set<struct sockaddr_in> &overloadedSlaveSet, char* buffer, uint32_t size );
	// Degraded operation
	char *reqDegradedLock( size_t &size, uint32_t id, uint32_t srcListId, uint32_t srcChunkId, uint32_t dstListId, uint32_t dstChunkId, char *key, uint8_t keySize );

	/* Slave */
	// Register
	char *reqRegisterSlave( size_t &size, uint32_t id, uint32_t addr, uint16_t port );
	// SET
	char *reqSet( size_t &size, uint32_t id, char *key, uint8_t keySize, char *value, uint32_t valueSize, char *buf = 0 );
	// Remapping SET
	char *reqRemappingSetLock( size_t &size, uint32_t id, uint32_t listId, uint32_t chunkId, char *key, uint8_t keySize );
	char *reqRemappingSet( size_t &size, uint32_t id, uint32_t listId, uint32_t chunkId, bool needsForwarding, char *key, uint8_t keySize, char *value, uint32_t valueSize, char *buf = 0 );
	// GET
	char *reqGet( size_t &size, uint32_t id, char *key, uint8_t keySize );
	char *reqDegradedGet( size_t &size, uint32_t id, uint32_t listId, uint32_t stripeId, uint32_t chunkId, bool isSealed, char *key, uint8_t keySize );
	// UPDATE
	char *reqUpdate( size_t &size, uint32_t id, char *key, uint8_t keySize, char *valueUpdate, uint32_t valueUpdateOffset, uint32_t valueUpdateSize );
	char *reqDegradedUpdate( size_t &size, uint32_t id, uint32_t listId, uint32_t stripeId, uint32_t chunkId, bool isSealed, char *key, uint8_t keySize, char *valueUpdate, uint32_t valueUpdateOffset, uint32_t valueUpdateSize );
	// DELETE
	char *reqDelete( size_t &size, uint32_t id, char *key, uint8_t keySize );
	char *reqDegradedDelete( size_t &size, uint32_t id, uint32_t listId, uint32_t stripeId, uint32_t chunkId, bool isSealed, char *key, uint8_t keySize );

	/* Application */
	// Register
	char *resRegisterApplication( size_t &size, uint32_t id, bool success );
	// SET
	char *resSet( size_t &size, uint32_t id, bool success, uint8_t keySize, char *key );
	// GET
	char *resGet( size_t &size, uint32_t id, bool success, uint8_t keySize, char *key, uint32_t valueSize = 0, char *value = 0 );
	// UPDATE
	char *resUpdate( size_t &size, uint32_t id, bool success, uint8_t keySize, char *key, uint32_t valueUpdateOffset, uint32_t valueUpdateSize );
	// DELETE
	char *resDelete( size_t &size, uint32_t id, bool success, uint8_t keySize, char *key );
};

#endif
