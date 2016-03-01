#ifndef __SLAVE_EVENT_SLAVE_EVENT_HH__
#define __SLAVE_EVENT_SLAVE_EVENT_HH__

#include "../socket/server_socket.hh"
#include "../../common/ds/chunk.hh"
#include "../../common/event/event.hh"

enum SlaveEventType {
	SLAVE_EVENT_TYPE_UNDEFINED,
	SLAVE_EVENT_TYPE_PENDING
};

class SlaveEvent : public Event {
public:
	SlaveEventType type;
	ServerSocket *socket;

	void pending( ServerSocket *socket );
};

#endif
