#ifndef __COMMON_UTIL_TIME_HH__
#define __COMMON_UTIL_TIME_HH__

#include <stdint.h>
#include <signal.h>
#include <time.h>

#define MILLION	( 1000 * 1000 )

#define start_timer() ( { \
	struct timespec ts; \
	clock_gettime( CLOCK_REALTIME, &ts ); \
	ts; \
} )

#define get_timer() start_timer()

#define get_elapsed_time(start_time) ( { \
	struct timespec end_time = get_timer(); \
	double elapsed_time = end_time.tv_sec - start_time.tv_sec; \
	elapsed_time += 1.0e-9 * ( end_time.tv_nsec - start_time.tv_nsec ); \
	elapsed_time; \
} )

class Timer {
private:
	struct itimerspec timer;
	struct itimerspec dummy;
	struct sigevent sigev;
	timer_t id;

	void setInterval( uint32_t sec, uint32_t msec, struct itimerspec *target ) {
		target->it_value.tv_sec = sec;
		target->it_value.tv_nsec = msec * MILLION;
		target->it_interval.tv_sec = sec;
		target->it_interval.tv_nsec = msec * MILLION;
	}

public:
	Timer( uint32_t sec = 0, uint32_t msec = 0 ) {
		this->setInterval( sec, msec );
		this->setInterval( 0, 0, &this->dummy );
		this->sigev.sigev_notify = SIGEV_SIGNAL;
		this->sigev.sigev_signo = SIGALRM;
		this->sigev.sigev_value.sival_ptr = &this->id;
		timer_create( CLOCK_REALTIME, &this->sigev, &this->id );
	}

	void setInterval( uint32_t sec, uint32_t msec ) {
		this->setInterval( sec, msec, &this->timer );
	}

	int start() {
		return timer_settime( this->id, 0, &this->timer, NULL );
	}

	int stop() {
		return timer_settime( this->id, 0, &this->dummy, NULL );
	}
};

#endif
