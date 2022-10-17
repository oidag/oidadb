#ifndef _edbWORKER_H_
#define _edbWORKER_H_ 1

#include "edbs.h"
#include "edbp.h"
#include "edbl.h"

typedef enum _edb_workerstate {
	EDB_WWORKNONE = 0,
	EDB_WWORKASYNC,
	EDB_WWORKSTOP,
} edb_workerstate;

typedef struct edb_worker_st {
	edbpcache_t *cache;
	edbl_handle_t lockdir;
	const edb_shm_t *shm;

	// fhead points to static memory
	edb_fhead *fhead;

	edb_workerstate state;

	// the functional purpose of workerid and pthread are synonymous.
	// One is just an easier read. workerid is simply a plus-one index
	// the worker is inside the host's worker pool.
	//
	// pthread is also unique but a much larger number.
	unsigned int workerid;
	pthread_t pthread;

	// this is an index inside of the job buffer. Note that
	// this doesn't mean that its currently executing the job at this pos. You
	// must look at the job itself for that information.
	//
	// Once a job is complete, the worker will attempt to find a new job by
	// incrementing from its last position.
	unsigned int jobpos;

	// see pages.h
	edbphandle_t edbphandle;
}edb_worker_t;

// _init initializs a new worker and _decom decommissions it.
//
// edb_workerdecom will only crit out.
edb_err edb_workerinit(edb_worker_t *o_worker, edbpcache_t *edbpcache, edbl_t *lockdir, const edb_shm_t *shm, edb_fhead *fhead);
void edb_workerdecom(edb_worker_t *worker);

// once initialized, a worker can be started with either of these functions.
// starts a new thread.
//
// edb_workerasync will instantly.
// in either case, an error will only be returned if something wrong happened at
// startup.
//
// regardless of how they started, workers set to shutdown mode by calling
// edb_workerstop. edb_workerstop will return only when the worker has been
// successfully marked as shutdown mode, it won't wait for the worker to
// actually stop. To wait for a worker in shutdown mode to actually stop,
// use edb_workerjoin.
//
// edb_workerstop and edb_workerjoin have no adverse consiquences if the
// worker was already stopped / never started.
//
// Threading: edb_workerstop is completely thread safe. sync and async are not
// thread safe on the same worker. Limit edb_workerjoin to 1 call per thread
// per worker.
edb_err edb_workerasync(edb_worker_t *worker);
void edb_workerstop(edb_worker_t *worker);
edb_err edb_workerjoin(edb_worker_t *worker);


#endif