#ifndef _edbWORKER_H_
#define _edbWORKER_H_ 1

#include "edbs.h"
#include "edbp.h"
#include "edba.h"
#include "edbl.h"
#include "edbs-jobs.h"

typedef enum _edb_workerstate {
	EDB_WWORKNONE = 0,
	EDB_WWORKASYNC,
	EDB_WWORKSTOP,
} edb_workerstate;

typedef struct edb_worker_st {
	const edbs_handle_t *shm;

	edb_workerstate state;

	// the functional purpose of workerid and pthread are synonymous.
	// One is just an easier read. workerid is simply a plus-one index
	// the worker is inside the host's worker pool.
	//
	// pthread is also unique but a much larger number.
	unsigned int workerid;
	pthread_t pthread;

	edba_handle_t *edbahandle;

	// filled in the second the worker is operating on a job.
	// if curjob.job is null that means no job currently.
	edbs_job_t curjob;
}edb_worker_t;

// _init initializs a new worker and _decom decommissions it.
//
// edb_workerdecom will only crit out.
// edb_workerdecom will implicitly call edb_workerstop
odb_err edbw_init(edb_worker_t *o_worker, edba_host_t *edbahost, const edbs_handle_t *shm);
void edbw_decom(edb_worker_t *worker);

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
odb_err edbw_async(edb_worker_t *worker);
void edbw_stop(edb_worker_t *worker);
odb_err edbw_join(edb_worker_t *worker);


#endif