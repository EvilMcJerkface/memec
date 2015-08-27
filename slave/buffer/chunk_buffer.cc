#include "chunk_buffer.hh"
#include "../main/slave.hh"

uint32_t ChunkBuffer::capacity;
uint32_t ChunkBuffer::dataChunkCount;
MemoryPool<Chunk> *ChunkBuffer::chunkPool;
MemoryPool<Stripe> *ChunkBuffer::stripePool;
SlaveEventQueue *ChunkBuffer::eventQueue;
Map *ChunkBuffer::map;

void ChunkBuffer::init() {
	Slave *slave = Slave::getInstance();
	ChunkBuffer::capacity = slave->config.global.size.chunk;
	ChunkBuffer::dataChunkCount = slave->config.global.coding.params.getDataChunkCount();
	ChunkBuffer::chunkPool = slave->chunkPool;
	ChunkBuffer::stripePool = slave->stripePool;
	ChunkBuffer::eventQueue = &slave->eventQueue;
	ChunkBuffer::map = &slave->map;
}

ChunkBuffer::ChunkBuffer( uint32_t listId, uint32_t stripeId, uint32_t chunkId ) {
	this->listId = listId;
	this->stripeId = stripeId;
	this->chunkId = chunkId;
	pthread_mutex_init( &this->lock, 0 );
}

ChunkBuffer::~ChunkBuffer() {}
