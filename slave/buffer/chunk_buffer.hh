#ifndef __SLAVE_BUFFER_CHUNK_BUFFER_HH__
#define __SLAVE_BUFFER_CHUNK_BUFFER_HH__

#include <cstdio>
#include <pthread.h>
#include "../../common/ds/chunk.hh"
#include "../../common/ds/key_value.hh"
#include "../../common/ds/memory_pool.hh"

#define CHUNK_BUFFER_FLUSH_THRESHOLD	4 // excluding metadata (4 bytes)

class ChunkBuffer {
protected:
	uint32_t capacity;            // Chunk size
	uint32_t count;               // Number of chunks
	Chunk **chunks;               // Allocated chunk buffer
	pthread_mutex_t *locks;       // Lock for each chunk
	MemoryPool<Chunk> *chunkPool; // Memory pool for chunks

public:
	ChunkBuffer( MemoryPool<Chunk> *chunkPool, uint32_t capacity, uint32_t count );
	virtual KeyValue set( char *key, uint8_t keySize, char *value, uint32_t valueSize ) = 0;
	virtual uint32_t flush( bool lock = true ) = 0;
	virtual Chunk *flush( int index, bool lock = true ) = 0;
	virtual void print( FILE *f = stdout ) = 0;
	virtual void stop() = 0;
	virtual ~ChunkBuffer();
};

#endif
