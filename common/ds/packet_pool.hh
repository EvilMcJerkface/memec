#ifndef __PACKET_POOL_HH__
#define __PACKET_POOL_HH__

#include "memory_pool.hh"
#include "../lock/lock.hh"

class Packet {
private:
	uint32_t referenceCount;
	uint32_t capacity;
	LOCK_T lock;

public:
	uint32_t size;
	char *data;
	bool inPool;

	Packet( bool inPool = true ) {
		this->referenceCount = 0;
		this->capacity = 0;
		this->size = 0;
		LOCK_INIT( &this->lock );
		this->data = 0;
		this->inPool = inPool;
	}

	~Packet() {
		if ( this->data )
			delete[] this->data;
		this->data = 0;
	}

	bool init( uint32_t capacity ) {
		this->capacity = capacity;
		this->data = new char[ capacity ];
		return true;
	}

	bool read( char *&data, size_t &size ) {
		if ( this->size == 0 ) {
			data = 0;
			size = 0;
			return false;
		}

		data = this->data;
		size = this->size;

		return true;
	}

	bool write( char *data, size_t size ) {
		if ( size > this->capacity )
			return false;

		memcpy( this->data, data, size );
		this->size = size;

		return true;
	}

	void setReferenceCount( uint32_t count ) {
		this->referenceCount = count;
	}

	bool decrement() {
		bool ret;

		LOCK( &this->lock );
		this->referenceCount--;
		ret = this->referenceCount == 0;
		UNLOCK( &this->lock );

		return ret;
	}

	static bool initFn( Packet *packet, void *argv ) {
		uint32_t size = *( ( uint32_t * ) argv );
		return packet->init( size );
	}
};

class PacketPool {
private:
	MemoryPool<Packet> *pool;
	uint32_t size;
	size_t capacity;
	LOCK_T lock;
	uint32_t extraCount;

public:
	PacketPool() {
		this->pool = 0;
		LOCK_INIT( &this->lock );
		this->extraCount = 0;
	}

	~PacketPool() {
		this->pool = 0;
	}

	void init( size_t capacity, size_t size ) {
		this->pool = MemoryPool<Packet>::getInstance();
		this->pool->init( capacity, Packet::initFn, ( void * ) &size );
		this->size = size;
		this->capacity = capacity;
	}

	Packet *malloc() {
		Packet *packet = this->pool->malloc( 0, 1, false );
		if ( ! packet ) {
			packet = new Packet( false );
			packet->init( size );
			LOCK( &this->lock );
			this->extraCount++;
			UNLOCK( &this->lock );
		}
		return packet;
	}

	void free( Packet *packet ) {
		if ( packet->decrement() ) { // Reference count == 0
			if ( packet->inPool )
				this->pool->free( packet );
			else {
				delete packet;
				LOCK( &this->lock );
				this->extraCount--;
				UNLOCK( &this->lock );
			}
		}
	}

	void print( FILE *f ) {
		fprintf( f, "Count : %lu / %lu\n", this->pool->getCount() + this->extraCount, this->capacity );
	}
};

#endif
