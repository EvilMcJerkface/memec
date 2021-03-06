#include "remapped_buffer.hh"

RemappedBuffer::RemappedBuffer() {
	LOCK_INIT( &this->keysLock );
	LOCK_INIT( &this->recordsLock );
}

bool RemappedBuffer::insert( uint32_t listId, uint32_t chunkId, uint32_t *original, uint32_t *remapped, uint32_t remappedCount, char *keyStr, uint8_t keySize, bool isLarge, char *valueStr, uint32_t valueSize ) {
	RemappedKeyValue remappedKeyValue;
	uint32_t splitOffset = 0;

	if ( isLarge )
		splitOffset = LargeObjectUtil::readSplitOffset( keyStr + keySize );

	remappedKeyValue.listId = listId;
	remappedKeyValue.chunkId = chunkId;
	remappedKeyValue.remappedCount = remappedCount;
	remappedKeyValue.keyValue.dup( keyStr, keySize, valueStr, valueSize, splitOffset );

	if ( remappedCount ) {
		remappedKeyValue.original = new uint32_t[ remappedCount * 2 ];
		remappedKeyValue.remapped = new uint32_t[ remappedCount * 2 ];
		for ( uint32_t i = 0; i < remappedCount; i++ ) {
			remappedKeyValue.original[ i * 2     ] = original[ i * 2     ];
			remappedKeyValue.original[ i * 2 + 1 ] = original[ i * 2 + 1 ];
			remappedKeyValue.remapped[ i * 2     ] = remapped[ i * 2     ];
			remappedKeyValue.remapped[ i * 2 + 1 ] = remapped[ i * 2 + 1 ];
		}
	} else {
		remappedKeyValue.original = 0;
		remappedKeyValue.remapped = 0;
	}

	Key key = remappedKeyValue.keyValue.key( true );

	std::pair<Key, RemappedKeyValue> p( key, remappedKeyValue );
	std::pair<std::unordered_map<Key, RemappedKeyValue>::iterator, bool> ret;

	LOCK( &this->keysLock );
	ret = this->keys.insert( p );
	UNLOCK( &this->keysLock );

	if ( ! ret.second ) {
		if ( remappedCount ) {
			delete[] remappedKeyValue.original;
			delete[] remappedKeyValue.remapped;
		}
		remappedKeyValue.keyValue.free();
	}

	return ret.second;
}

bool RemappedBuffer::insert( uint32_t listId, uint32_t chunkId, uint32_t *original, uint32_t *remapped, uint32_t remappedCount, char *keyStr, uint8_t keySize, bool isLarge ) {
	RemappingRecord remappingRecord;

	remappingRecord.listId = listId;
	remappingRecord.chunkId = chunkId;
	remappingRecord.remappedCount = remappedCount;
	remappingRecord.key.dup( keySize, keyStr, 0, isLarge );

	if ( remappedCount ) {
		remappingRecord.original = new uint32_t[ remappedCount * 2 ];
		remappingRecord.remapped = new uint32_t[ remappedCount * 2 ];
		for ( uint32_t i = 0; i < remappedCount; i++ ) {
			remappingRecord.original[ i * 2     ] = original[ i * 2     ];
			remappingRecord.original[ i * 2 + 1 ] = original[ i * 2 + 1 ];
			remappingRecord.remapped[ i * 2     ] = remapped[ i * 2     ];
			remappingRecord.remapped[ i * 2 + 1 ] = remapped[ i * 2 + 1 ];
		}
	} else {
		remappingRecord.original = 0;
		remappingRecord.remapped = 0;
	}

	std::pair<Key, RemappingRecord> p( remappingRecord.key, remappingRecord );
	std::pair<std::unordered_map<Key, RemappingRecord>::iterator, bool> ret;

	LOCK( &this->recordsLock );
	ret = this->records.insert( p );
	UNLOCK( &this->recordsLock );

	if ( ! ret.second ) {
		if ( remappedCount ) {
			delete[] remappingRecord.original;
			delete[] remappingRecord.remapped;
		}
		remappingRecord.key.free();
	}

	return ret.second;
}

bool RemappedBuffer::update( uint8_t keySize, char *keyStr, bool isLarge, uint32_t valueUpdateSize, uint32_t valueUpdateOffset, char *valueUpdate, RemappedKeyValue *remappedKeyValue ) {
	std::unordered_map<Key, RemappedKeyValue>::iterator it;
	Key key;
	bool ret;
	key.set( keySize, keyStr, 0, isLarge );

	LOCK( &this->keysLock );
	it = this->keys.find( key );
	ret = ( it != this->keys.end() );
	if ( ret ) {
		// Perform update
		uint32_t offset = KEY_VALUE_METADATA_SIZE + keySize + ( isLarge ? SPLIT_OFFSET_SIZE : 0 ) + valueUpdateOffset;
		KeyValue &keyValue = it->second.keyValue;
		memcpy( keyValue.data + offset, valueUpdate, valueUpdateSize );
		if ( remappedKeyValue ) *remappedKeyValue = it->second;
	}
	UNLOCK( &this->keysLock );

	return ret;
}

bool RemappedBuffer::find( uint8_t keySize, char *keyStr, bool isLarge, RemappedKeyValue *remappedKeyValue ) {
	std::unordered_map<Key, RemappedKeyValue>::iterator it;
	Key key;
	bool ret;
	key.set( keySize, keyStr, 0, isLarge );

	LOCK( &this->keysLock );
	it = this->keys.find( key );
	ret = ( it != this->keys.end() );
	if ( ret && remappedKeyValue ) {
		*remappedKeyValue = it->second;
	}
	UNLOCK( &this->keysLock );

	return ret;
}

bool RemappedBuffer::find( uint8_t keySize, char *keyStr, bool isLarge, RemappingRecord *remappingRecord ) {
	std::unordered_map<Key, RemappingRecord>::iterator it;
	Key key;
	bool ret;
	key.set( keySize, keyStr, 0, isLarge );

	LOCK( &this->recordsLock );
	it = this->records.find( key );
	ret = ( it != this->records.end() );
	if ( ret && remappingRecord ) {
		*remappingRecord = it->second;
	}
	UNLOCK( &this->recordsLock );

	return ret;
}

std::unordered_map<Key, RemappedKeyValue>::iterator RemappedBuffer::erase( std::unordered_map<Key, RemappedKeyValue>::iterator it, bool needsLock, bool needsUnlock ) {
	if ( needsLock ) LOCK( &this->keysLock );

	RemappedKeyValue v = it->second;

	if ( v.original ) delete[] v.original;
	if ( v.remapped ) delete[] v.remapped;
	v.keyValue.free();

	it = this->keys.erase( it );

	if ( needsUnlock ) UNLOCK( &this->keysLock );

	return it;
}
