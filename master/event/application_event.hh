#ifndef __MASTER_EVENT_APPLICATION_EVENT_HH__
#define __MASTER_EVENT_APPLICATION_EVENT_HH__

#include "../socket/application_socket.hh"
#include "../../common/ds/key.hh"
#include "../../common/ds/key_value.hh"
#include "../../common/ds/pending.hh"
#include "../../common/event/event.hh"

enum ApplicationEventType {
	APPLICATION_EVENT_TYPE_UNDEFINED,
	APPLICATION_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS,
	APPLICATION_EVENT_TYPE_REGISTER_RESPONSE_FAILURE,
	APPLICATION_EVENT_TYPE_GET_RESPONSE_SUCCESS,
	APPLICATION_EVENT_TYPE_GET_RESPONSE_FAILURE,
	APPLICATION_EVENT_TYPE_SET_RESPONSE_SUCCESS,
	APPLICATION_EVENT_TYPE_SET_RESPONSE_FAILURE,
	APPLICATION_EVENT_TYPE_UPDATE_RESPONSE_SUCCESS,
	APPLICATION_EVENT_TYPE_UPDATE_RESPONSE_FAILURE,
	APPLICATION_EVENT_TYPE_DELETE_RESPONSE_SUCCESS,
	APPLICATION_EVENT_TYPE_DELETE_RESPONSE_FAILURE,
	APPLICATION_EVENT_TYPE_PENDING
};

class ApplicationEvent : public Event {
public:
	ApplicationEventType type;
	ApplicationSocket *socket;
	bool needsFree;
	union {
		Key key;
		KeyValue keyValue;
		KeyValueUpdate keyValueUpdate;
	} message;

	void resRegister( ApplicationSocket *socket, bool success = true );
	void resGet( ApplicationSocket *socket, KeyValue &keyValue, bool needsFree = true );
	void resGet( ApplicationSocket *socket, Key &key, bool needsFree = true );
	void resSet( ApplicationSocket *socket, Key &key, bool success, bool needsFree = true );
	void resUpdate( ApplicationSocket *socket, KeyValueUpdate &keyValueUpdate, bool success, bool needsFree = true );
	void resDelete( ApplicationSocket *socket, Key &key, bool success, bool needsFree = true );
	void pending( ApplicationSocket *socket );
};

#endif
