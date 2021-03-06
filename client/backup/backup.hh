#ifndef __CLIENT_BACKUP_BACKUP_HH__
#define __CLIENT_BACKUP_BACKUP_HH__

#include <unordered_map>
#include "../../common/ds/key.hh"
#include "../../common/ds/metadata.hh"
#include "../../common/lock/lock.hh"

class Backup {
public:
	LOCK_T lock;
	std::unordered_map<Key, MetadataBackup> ops;
	// Timestamp |--> Metadata
	std::unordered_multimap<uint32_t, Metadata> sealed;

	Backup();
	void insert(
		uint8_t keySize, char *keyStr, bool isLarge,
		uint8_t opcode, uint32_t timestamp,
		uint32_t listId, uint32_t stripeId, uint32_t chunkId
	);
	void insert(
		uint8_t keySize, char *keyStr, bool isLarge,
		uint8_t opcode, uint32_t timestamp,
		uint32_t listId, uint32_t stripeId, uint32_t chunkId,
		uint8_t sealedCount, Metadata *sealed
	);
	size_t erase( uint32_t fromTimestamp, uint32_t toTimestamp );
	void print( FILE *f = stdout );
	~Backup();
};

#endif
