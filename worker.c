#include <string.h>
#include <pthread.h>
#include "worker.h"



void static workermain(edb_worker_t *self) {

}


edb_err edb_workerinit(edb_host_t *host, edb_worker_t *worker) {
	//initialize
	bzero(worker, sizeof (edb_worker_t));
	worker->host = host;

	return 0;
}

void edb_workerdecom(edb_worker_t *worker) {

}


edb_err edb_workerasync(edb_worker_t *worker) {
	if(worker->state != EDB_WWORKNONE) {
		log_critf("attempting to start already-running worker");
		return EDB_ECRIT;
	}
	int err = pthread_create(&(worker->pthread), 0, workermain, worker);
	if(err) {
		log_critf("failed to create thread pthread_create(3) returned: %d", err);
		return EDB_ECRIT;
	}
	worker->state = EDB_WWORKASYNC;
	return 0;
}



void edb_workerstop(edb_worker_t *worker) {
	if(worker->state != EDB_WWORKASYNC &&
	   worker->state != EDB_WWORKSYNC) {
		log_noticef("attempted to stop worker when not in working state");
		return;
	}
	worker->state = EDB_WWORKSTOP;
}

edb_err edb_workerjoin(edb_worker_t *worker) {
	if (worker->state == EDB_WWORKNONE) {
		return 0; // already stopped and joined.
	}
	if(worker->state != EDB_WWORKSTOP) {
		log_critf("attempted to join on a worker without stopping it first");
		return EDB_ECRIT;
	}
	int err = pthread_join(worker->pthread, 0);
	if(err) {
		log_critf("pthread_join(3) returned error: %d", err);
	}
	worker->state = EDB_WWORKNONE;
	return 0;
}