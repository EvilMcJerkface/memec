#include "master_event.hh"

void MasterEvent::resRegister( MasterSocket *socket, uint32_t id, bool success ) {
	this->type = success ? MASTER_EVENT_TYPE_REGISTER_RESPONSE_SUCCESS : MASTER_EVENT_TYPE_REGISTER_RESPONSE_FAILURE;
	this->id = id;
	this->socket = socket;
}

void MasterEvent::resGet( MasterSocket *socket, uint32_t id, KeyValue &keyValue, bool isDegraded ) {
	this->type = MASTER_EVENT_TYPE_GET_RESPONSE_SUCCESS;
	this->id = id;
	this->isDegraded = isDegraded;
	this->socket = socket;
	this->message.keyValue = keyValue;
}

void MasterEvent::resGet( MasterSocket *socket, uint32_t id, Key &key, bool isDegraded ) {
	this->type = MASTER_EVENT_TYPE_GET_RESPONSE_FAILURE;
	this->id = id;
	this->isDegraded = isDegraded;
	this->socket = socket;
	this->message.key = key;
}

void MasterEvent::resSet( MasterSocket *socket, uint32_t id, Key &key, bool success ) {
	this->type = success ? MASTER_EVENT_TYPE_SET_RESPONSE_SUCCESS_PARITY : MASTER_EVENT_TYPE_SET_RESPONSE_FAILURE;
	this->id = id;
	this->socket = socket;
	this->message.set.key = key;
}

void MasterEvent::resSet( MasterSocket *socket, uint32_t id, uint32_t listId, uint32_t stripeId, uint32_t chunkId, Key &key ) {
	this->type = MASTER_EVENT_TYPE_SET_RESPONSE_SUCCESS_DATA;
	this->id = id;
	this->socket = socket;
	this->message.set.key = key;
	this->message.set.listId = listId;
	this->message.set.stripeId = stripeId;
	this->message.set.chunkId = chunkId;
}

void MasterEvent::resRemappingSet( MasterSocket *socket, uint32_t id, Key &key, uint32_t listId, uint32_t chunkId, bool success, bool needsFree, uint32_t sockfd, bool remapped ) {
	this->type = success ? MASTER_EVENT_TYPE_REMAPPING_SET_RESPONSE_SUCCESS : MASTER_EVENT_TYPE_REMAPPING_SET_RESPONSE_FAILURE;
	this->id = id;
	this->needsFree = needsFree;
	this->socket = socket;
	this->message.remap.key = key;
	this->message.remap.listId = listId;
	this->message.remap.chunkId = chunkId;
	this->message.remap.sockfd = sockfd;
	this->message.remap.isRemapped = remapped;
}

void MasterEvent::resUpdate( MasterSocket *socket, uint32_t id, Key &key, uint32_t valueUpdateOffset, uint32_t valueUpdateSize, bool success, bool needsFree, bool isDegraded ) {
	this->type = success ? MASTER_EVENT_TYPE_UPDATE_RESPONSE_SUCCESS : MASTER_EVENT_TYPE_UPDATE_RESPONSE_FAILURE;
	this->id = id;
	this->needsFree = needsFree;
	this->isDegraded = isDegraded;
	this->socket = socket;
	this->message.keyValueUpdate.key = key;
	this->message.keyValueUpdate.valueUpdateOffset = valueUpdateOffset;
	this->message.keyValueUpdate.valueUpdateSize = valueUpdateSize;
}

void MasterEvent::resDelete( MasterSocket *socket, uint32_t id, Key &key, bool success, bool needsFree, bool isDegraded ) {
	this->type = success ? MASTER_EVENT_TYPE_DELETE_RESPONSE_SUCCESS : MASTER_EVENT_TYPE_DELETE_RESPONSE_FAILURE;
	this->id = id;
	this->needsFree = needsFree;
	this->isDegraded = isDegraded;
	this->socket = socket;
	this->message.key = key;
}

void MasterEvent::pending( MasterSocket *socket ) {
	this->type = MASTER_EVENT_TYPE_PENDING;
	this->socket = socket;
}
