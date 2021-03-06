#include <cassert>
#include "protocol.hh"
#include "../../common/ds/chunk_pool.hh"

char *ServerProtocol::reqSealChunk( size_t &size, uint16_t instanceId, uint32_t requestId, Chunk *chunk, uint32_t startPos, char *buf ) {
	// -- common/protocol/seal_protocol.cc --
	if ( ! buf ) buf = this->buffer.send;

	char *ptr = buf + PROTO_HEADER_SIZE + PROTO_CHUNK_SEAL_SIZE;
	size_t bytes = 0; // data length only
	Metadata metadata = ChunkUtil::getMetadata( chunk );

	int currentOffset = startPos, nextOffset = 0;
	uint32_t count = 0;
	char *key;
	uint8_t keySize;
	bool isLarge;
	while ( ( nextOffset = ChunkUtil::next( chunk, currentOffset, key, keySize, isLarge ) ) != -1 ) {
		// fprintf(
		// 	stderr, "reqSealChunk(): [%u, %u, %u] %.*s (%u) at %u%s\n",
		// 	metadata.listId, metadata.stripeId, metadata.chunkId,
		// 	keySize, key, keySize, currentOffset,
		// 	isLarge ? "; is large" : ""
		// );

		ptr[ 0 ] = keySize;
		*( ( uint32_t * )( ptr + 1 ) ) = htonl( currentOffset );
		ptr[ 5 ] = isLarge; // isLarge
		memmove( ptr + 6, key, keySize + ( isLarge ? SPLIT_OFFSET_SIZE : 0 ) );

		count++;
		bytes += PROTO_CHUNK_SEAL_DATA_SIZE + keySize + ( isLarge ? SPLIT_OFFSET_SIZE : 0 );
		ptr += PROTO_CHUNK_SEAL_DATA_SIZE + keySize + ( isLarge ? SPLIT_OFFSET_SIZE : 0 );

		currentOffset = nextOffset;
	}

	// The seal request should not exceed the size of the send buffer
	assert( bytes <= this->buffer.size );

	size = this->generateChunkSealHeader(
		PROTO_MAGIC_REQUEST,
		PROTO_MAGIC_TO_SERVER,
		PROTO_OPCODE_SEAL_CHUNK,
		instanceId, requestId,
		metadata.listId,
		metadata.stripeId,
		metadata.chunkId,
		count,
		bytes,
		buf
	);
	return buf;
}

char *ServerProtocol::reqRemappedDelete( size_t &size, uint16_t instanceId, uint32_t requestId, char *key, uint8_t keySize, char *buf, uint32_t timestamp ) {
	// -- common/protocol/normal_protocol.cc --
	if ( ! buf ) buf = this->buffer.send;
	size = this->generateKeyHeader(
		PROTO_MAGIC_REQUEST,
		PROTO_MAGIC_TO_SERVER,
		PROTO_OPCODE_REMAPPED_DELETE,
		instanceId, requestId,
		keySize,
		key,
		buf, timestamp
	);
	return buf;
}
