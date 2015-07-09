#ifndef __COMMON_WORKER_WORKER_HH__
#define __COMMON_WORKER_WORKER_HH__

#include <cstdio>
#include <pthread.h>

class Worker {
protected:
	bool isRunning;
	pthread_t tid;

	virtual void free() = 0;

public:
	Worker() {
		this->isRunning = false;
		this->tid = 0;
	}

	inline void join() {
		if ( this->isRunning )
			pthread_join( this->tid, NULL );
		this->isRunning = false;
	}

	inline bool getIsRunning() {
		return this->isRunning;
	}

	inline pthread_t getThread() {
		return this->tid;
	}

	virtual bool start() = 0;
	virtual void stop() = 0;
	virtual void debug( FILE *f = stdout ) = 0;
};

#endif
