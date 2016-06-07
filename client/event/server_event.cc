#include "server_event.hh"

void ServerEvent::reqRegister( ServerSocket *socket, uint32_t addr, uint16_t port ) {
	this->type = SERVER_EVENT_TYPE_REGISTER_REQUEST;
	this->socket = socket;
	this->message.address.addr = addr;
	this->message.address.port = port;
}

void ServerEvent::send( ServerSocket *socket, Packet *packet ) {
	this->type = SERVER_EVENT_TYPE_SEND;
	this->socket = socket;
	this->message.send.packet = packet;
}

void ServerEvent::syncMetadata( ServerSocket *socket ) {
	this->type = SERVER_EVENT_TYPE_SYNC_METADATA;
	this->socket = socket;
}

void ServerEvent::ackParityDelta( ServerSocket *socket, std::vector<uint32_t> timestamps, uint16_t targetId, pthread_cond_t *condition, LOCK_T *lock, uint32_t *counter ) {
	this->type = SERVER_EVENT_TYPE_ACK_PARITY_DELTA;
	this->socket = socket;
	this->message.ack.timestamps = ( timestamps.empty() )? 0 : new std::vector<uint32_t>( timestamps );
	this->message.ack.requests = 0;
	this->message.ack.targetId = targetId;
	this->message.ack.condition = condition;
	this->message.ack.lock = lock;
	this->message.ack.counter = counter;
}

void ServerEvent::revertDelta( ServerSocket *socket, std::vector<uint32_t> timestamps, std::vector<Key> requests, uint16_t targetId, pthread_cond_t *condition, LOCK_T *lock, uint32_t *counter ) {
	this->type = SERVER_EVENT_TYPE_REVERT_DELTA;
	this->socket = socket;
	this->message.ack.timestamps = ( timestamps.empty() )? 0 : new std::vector<uint32_t>( timestamps );
	this->message.ack.requests = ( requests.empty() )? 0 : new std::vector<Key>( requests );
	this->message.ack.targetId = targetId;           // source data server
	this->message.ack.condition = condition;
	this->message.ack.lock = lock;
	this->message.ack.counter = counter;
}

void ServerEvent::pending( ServerSocket *socket ) {
	this->type = SERVER_EVENT_TYPE_PENDING;
	this->socket = socket;
}
