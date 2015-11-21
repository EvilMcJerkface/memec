#ifndef __COORDINATOR_EVENT_MASTER_EVENT_HH__
#define __COORDINATOR_EVENT_MASTER_EVENT_HH__

#include <climits>
#include <set>
#include "../socket/master_socket.hh"
#include "../../common/ds/key.hh"
#include "../../common/ds/latency.hh"
#include "../../common/ds/key.hh"
#include "../../common/ds/metadata.hh"
#include "../../common/ds/packet_pool.hh"
#include "../../common/event/event.hh"

enum MasterEventType {
	MASTER_EVENT_TYPE_UNDEFINED,
	// REGISTER
	MASTER_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS,
	MASTER_EVENT_TYPE_REGISTER_RESPONSE_FAILURE,
	// LOADING_STAT
	MASTER_EVENT_TYPE_PUSH_LOADING_STATS,
	// REMAPPING_PHASE_SWITCH
	MASTER_EVENT_TYPE_SWITCH_PHASE,
	// REMAPPING_RECORDS
	MASTER_EVENT_TYPE_FORWARD_REMAPPING_RECORDS,
	MASTER_EVENT_TYPE_SYNC_REMAPPING_RECORDS,
	// Degraded operation
	MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_IS_LOCKED,
	MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_WAS_LOCKED,
	MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_NOT_LOCKED,
	MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_REMAPPED,
	MASTER_EVENT_TYPE_DEGRADED_LOCK_RESPONSE_NOT_FOUND,
	// REMAPPING_SET_LOCK
	MASTER_EVENT_TYPE_REMAPPING_SET_LOCK_RESPONSE_SUCCESS,
	MASTER_EVENT_TYPE_REMAPPING_SET_LOCK_RESPONSE_FAILURE,
	// PENDING
	MASTER_EVENT_TYPE_PENDING
};

class MasterEvent : public Event {
public:
	MasterEventType type;
	uint32_t id;
	MasterSocket *socket;
	union {
		struct {
			ArrayMap<struct sockaddr_in, Latency> *slaveGetLatency;
			ArrayMap<struct sockaddr_in, Latency> *slaveSetLatency;
			std::set<struct sockaddr_in> *overloadedSlaveSet;
		} slaveLoading;
		struct {
			bool toRemap;
			std::vector<struct sockaddr_in> *slaves;
			Key key;
			uint32_t listId;
			uint32_t chunkId;
			uint32_t isRemapped;
			uint32_t sockfd;
			std::vector<Packet*> *syncPackets;
		} remap;
		struct {
			size_t prevSize;
			char *data;
		} forward;
		struct {
			Key key;
			uint32_t srcListId, srcStripeId, srcChunkId;
			uint32_t dstListId, dstChunkId;
			bool isSealed;
		} degradedLock;
	} message;

	void resRegister( MasterSocket *socket, uint32_t id, bool success = true );

	void reqPushLoadStats (
		MasterSocket *socket,
		ArrayMap<struct sockaddr_in, Latency> *slaveGetLatency,
		ArrayMap<struct sockaddr_in, Latency> *slaveSetLatency,
		std::set<struct sockaddr_in> *slaveSet
	);
	void switchPhase( bool toRemap, std::set<struct sockaddr_in> slaves );
	// Degraded lock
	void resDegradedLock(
		MasterSocket *socket, uint32_t id, Key &key, bool isLocked, bool isSealed,
		uint32_t srcListId, uint32_t srcStripeId, uint32_t srcChunkId,
		uint32_t dstListId, uint32_t dstChunkId
	);
	void resDegradedLock(
		MasterSocket *socket, uint32_t id, Key &key, bool exist,
		uint32_t listId, uint32_t chunkId
	);
	void resDegradedLock(
		MasterSocket *socket, uint32_t id, Key &key,
		uint32_t srcListId, uint32_t srcChunkId,
		uint32_t dstListId, uint32_t dstChunkId
	);
	// REMAPPING_SET_LOCK
	void resRemappingSetLock(
		MasterSocket *socket, uint32_t id, bool isRemapped,
		Key &key, RemappingRecord &remappingRecord, bool success,
		uint32_t sockfd = UINT_MAX
	);
	// Remapping Records
	void syncRemappingRecords(
		MasterSocket *socket, std::vector<Packet*> *packets
	);
	// Pending
	void pending( MasterSocket *socket );
};

#endif
