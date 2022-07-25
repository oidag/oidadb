#ifndef _edbWORKER_H_
#define _edbWORKER_H_ 1

#include "host.h"

typedef struct edb_worker_st {

}edb_worker_t;

// _init initializs a new worker and _decom decommissions it.
//
// edb_workerdecom will only crit out.
edb_err edb_workerinit(edb_host_t *host, edb_worker_t *worker);
void edb_workerdecom(edb_worker_t *worker);

// once initialized, a worker can be started with either of these functions.
// edb_workersync consumes the calling thread and edb_worker_startasync
// starts a new thread.
//
// edb_workersync will return only when shutdown. edb_workerasync will instantly.
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
// thread safe on the same worker.
edb_err edb_workersync(edb_worker_t *worker);
edb_err edb_workerasync(edb_worker_t *worker);
void edb_workerstop(edb_worker_t *worker);
void edb_workerjoin(edb_worker_t *worker);


#endif