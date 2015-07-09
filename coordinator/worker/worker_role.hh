#ifndef __COORDINATOR_WORKER_ROLE_HH__
#define __COORDINATOR_WORKER_ROLE_HH__

enum WorkerRole {
	WORKER_ROLE_UNDEFINED,
	WORKER_ROLE_MIXED,
	WORKER_ROLE_APPLICATION,
	WORKER_ROLE_COORDINATOR,
	WORKER_ROLE_MASTER,
	WORKER_ROLE_SLAVE
};

#endif
