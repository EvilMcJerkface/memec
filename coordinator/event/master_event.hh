#ifndef __COORDINATOR_EVENT_MASTER_EVENT_HH__
#define __COORDINATOR_EVENT_MASTER_EVENT_HH__

#include "../socket/master_socket.hh"
#include "../../common/ds/latency.hh"
#include "../../common/event/event.hh"

enum MasterEventType {
	MASTER_EVENT_TYPE_UNDEFINED,
	MASTER_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS,
	MASTER_EVENT_TYPE_REGISTER_RESPONSE_FAILURE,
	MASTER_EVENT_TYPE_PUSH_LOADING_STATS,
	MASTER_EVENT_TYPE_PENDING
};

class MasterEvent : public Event {
public:
	MasterEventType type;
	MasterSocket *socket;
	union {
		struct {
			ArrayMap<ServerAddr, Latency> *slaveGetLatency;
			ArrayMap<ServerAddr, Latency> *slaveSetLatency;
		} slaveLoading;
	} message;

	void resRegister( MasterSocket *socket, bool success = true );
	void reqPushLoadStats ( MasterSocket *socket, ArrayMap<ServerAddr, Latency> *slaveGetLatency, 
			ArrayMap<ServerAddr, Latency> *slaveSetLatency );
	void pending( MasterSocket *socket );
};

#endif
