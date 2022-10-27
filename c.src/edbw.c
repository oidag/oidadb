#include <stddef.h>
#define _LARGEFILE64_SOURCE
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>


#include "include/ellemdb.h"
#include "edba.h"
#include "edbw.h"
#include "edbs-jobs.h"
#include "edbd.h"
#include "edbp-types.h"

typedef enum {
	// constantly keep retrying to wait on the futex until
	// it switches to operational.
	EDB_FUTEX_RETRY = 0,

	// futex is operational.
	EDB_FUTEX_OPERATIONAL = 1,

	// its time to stop listening to the futex.
	EDB_FUTEX_CLOSE = 2,
} futexops;

#define EDB_BTREE0 0x0000
#define EDB_BTREE1 0x1000
#define EDB_BTREE2 0x2000
#define EDB_BTREE3 0x3000

// helper wrappers for accessing the job buffer more easily
int static inline edbw_jobread(edb_worker_t *self, void *bufv, int bufc) {
	return edb_jobread(self->curjob, self->shm->transbuffer, bufv, bufc);
}
int static inline edbw_jobwrite(edb_worker_t *self, void *bufv, int bufc) {
	return edb_jobwrite(self->curjob, self->shm->transbuffer, bufv, bufc);
}
void static inline edbw_jobclose(edb_worker_t *self) {
	edb_jobclose(self->curjob);
}
int static inline edbw_jobisclosed(edb_worker_t *self) {
	return edb_jobisclosed(self->curjob);
}


// function to easily verbosely log worker and job ids
#define edbw_logverbose(workerp, fmt, ...) \
log_debugf("worker#%d executing job#%ld: " fmt, workerp->workerid, workerp->curjob->jobid, ##__VA_ARGS__)

// helper func to selectjob.
//
// executes the job only. does not mark the job as complete nor relinquish ownership.
// thats the callers responsibility.
//
// self->curjob is assumed to be non-null (will not null out on completion)
//
// This function's only purpose is to route the information into the relevant execjob_...
// function.
//
// Will only return EDB_ECRIT, can be ignored and continued.
static edb_err execjob(edb_worker_t *self) {

	// easy pointers
	edb_job_t *job = self->curjob;
	edb_err err = 0;

	// note to self: inside this function we have our own thread to ourselves.
	// its slightly better to be organized than efficient in here sense we have
	// nothing serious waiting on us. All the other jobs that are being submitted
	// are being handled elsewhere.
	// Take your time Kev :-)

	// "FRH" - when you see this acroynm in the comments that means the referenced
	// code is "function redundant to handle". This means that the code is redundant
	// to what the handle checks for. It can be excluded, but just a safety check.
	// Best to have this type of code wrapped in macros that can enable and disable
	// "extra safety features"
	//
	// Useful for when handle process mysteriously start misbehaving.

	// FRH
	// Cannot take jobs with a closed buffer
	if(edbw_jobisclosed(self)) {
		err = EDB_ECRIT;
		log_critf("job accepted by worker but the handle did not open job buffer");
		goto closejob;
	}

	// check for some common errors regarding the edb_jobclass
	edb_oid oid;
	edb_eid entryid;
	uint64_t data_identity; // generic term, can be rowid or otherwise structid
	int ret;
	edb_err c_err;
	switch (job->jobdesc & 0x00FF) {
		case EDB_OBJ:
		case EDB_DYN:
			// all of these job classes need an id parameter
			ret = edbw_jobread(self, &oid, sizeof(oid));
			if(ret == -1) {
				c_err = EDB_ECRIT;
				edbw_jobwrite(self, &c_err, sizeof(c_err));
				goto closejob;
			} else if(ret == -2) {
				c_err = EDB_EHANDLE;
				edbw_jobwrite(self, &c_err, sizeof(c_err));
				goto closejob;
			}
			entryid = oid >> 0x30;
			data_identity = oid & 0x0000FFFFFFFFFFFF;
			if(entryid < 4) {
				// this entry is invalid per spec
				err = EDB_EINVAL;
				edbw_jobwrite(self, &err, sizeof(err));
				goto closejob;
			}
			break;

		default: break;
	}


	// check for some common errors regarding the command
	switch (job->jobdesc & 0xFF00) {
		case EDB_CDEL:
		case EDB_CCOPY:
			break;
	}

	// do the routing
	switch (job->jobdesc) {
		case EDB_OBJ | EDB_CCREATE:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);
			execjob_objcreate(self, entryid, data_identity);
			break;
		case EDB_OBJ | EDB_CCOPY:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);
			execjob_objcopy(self, entryid, data_identity);
			break;
		case EDB_OBJ | EDB_CWRITE:
			// we have an open stream and a id, thus, it is an edit.
			edbw_logverbose(self, "edit object 0x%016lX", oid);
			execjob_objedit(self, entryid, data_identity);
			break;
		case EDB_OBJ | EDB_CDEL:
			// object-deletion
			edbw_logverbose(self, "delete object 0x%016lX", oid);
			execjob_objdelete(self, job);
			break;

		case EDB_OBJ | EDB_CUSRLK:
		default:
			err = EDB_EINVAL;
			edbw_jobwrite(self, &err, sizeof(err));
			log_critf("execjob was given a bad jobid: %04x", job->jobdesc);
			goto closejob;
	}

	closejob:
	edbw_jobclose(self);
	return c_err;
}

// helper func to workermain
//
// return an error if you need the thread to restart.
//
// EDB_ESTOPPING: the host has closed the futex.
static edb_err selectjob(edb_worker_t * const self) {
	int err;

	// save some pointers to the stack for easier access.
	edb_shmhead_t * const head = self->shm->head;
	const uint64_t        jobc = head->jobc;
	edb_job_t     * const jobv = self->shm->jobv;


	// now we need to find a new job.
	// Firstly, we need to atomically lock all workers so that only one can accept a job at once to
	// avoid the possibility that 2 workers accidentally take the same job.
	err = pthread_mutex_lock(&head->jobaccept);
	if(err) {
		log_critf("critical error while attempting to lock jobaccept mutex: %d", err);
		return EDB_ECRIT;
	}

	// note on critical errors: the job accept must be unlocked if a critical error occurs.

	// at this point, we have locked the job accept mutex. But that doesn't mean there's
	// jobs available.
	//
	// if there's no new jobs then we must wait. We do this by running futext_wait
	// on the case that newjobs is 0. In which case a handle will eventually wake us after it increments
	// newjobs.
	err = syscall(SYS_futex, &head->futex_newjobs, FUTEX_WAIT, 0, 0, 0, 0);
	if(err == -1 && errno != EAGAIN) {
		pthread_mutex_unlock(&head->jobaccept);
		log_critf("critical error while waiting on new jobs: %d", errno);
		return EDB_ECRIT;
	}

	// if we're here that means we know that there's at least 1 new job. lets find it.
	{
		int i;
		for (i = 0; i < jobc; i++) {

			if (jobv[self->jobpos].owner != 0 || jobv[self->jobpos].jobdesc != 0) {
				// this job is already owned.
				// so this job is not owned however there's no job installed.
				goto next;
			}

			// if we're here then we know this job is not owned
			// and has a job. We'll break out of the for loop.
			break;

			next:
			// increment the worker's position.
			self->jobpos = (self->jobpos + 1) % jobc;
		}
		if (i == jobc) {
			// this is a critical error. If we're here that meas we went through
			// the entire stack and didn't find an open job. Sense futex_newjobs
			// was supposed to be at least 1 that means I programmed something wrong.
			//
			// The first thread that enters into here will not be the last as futher
			// threads will discover the same thing.
			pthread_mutex_unlock(&head->jobaccept);
			log_critf("although newjobs was at least 1, an open job was not found in the stack.");
			return EDB_ECRIT;
		}
	}

	// at this point, jobv[self->jobpos] is pointing to an open job.
	// so lets go ahead and set the job ownership to us.
	jobv[self->jobpos].owner = self->workerid;
	head->futex_newjobs--;

	// we're done filing all the paperwork to have ownership over this job. thus no more need to have
	// the ownership logic locked for other thread. We can continue to do this job.
	pthread_mutex_unlock(&head->jobaccept);

	// if we're here that means we've accepted the job at jobv[self->jobpos] and we've
	// claimed it so other workers won't bother this job.

	// set curjob
	self->curjob = edbs_jobhandle(self->shm, self->jobpos);
	execjob(self);
	self->curjob.job = 0; // null out curjob.

	// job has been completed. relinquish ownership.
	//
	// to do this, we have to lock the job install mutex to avoid the possiblity of a
	// handle trying to install a job mid-way through us relinqusihing it.
	//
	// (this can probably be done a bit more nicely if the jobinstall mutex was held per-job slot)
	pthread_mutex_lock(&head->jobinstall);

	// we must do this by removing the owner, and then setting class to EDB_JNONE in that
	// exact order. This is because handles look exclusively at the class to determine
	// its ability to load in another job and we don't want it loading another job into
	// this position while its still owned.
	jobv[self->jobpos].jobdesc = 0;
	jobv[self->jobpos].owner   = 0;
	head->futex_emptyjobs++;

	// close the transfer if it hasn't already.
	// note that per edb_jobclose's documentation on threading, edb_jobclose and edb_jobopen must
	// only be called inside the comfort of the jobinstall mutex.
	edb_jobclose(&jobv[self->jobpos]);

	pthread_mutex_unlock(&head->jobinstall);

	// send out a broadcast letting at least 1 waiting handler know theres another empty job
	err = syscall(SYS_futex, &head->futex_emptyjobs, FUTEX_WAKE, 1, 0, 0, 0);
	if(err == -1) {
		log_critf("failed to wake futex_emptyjobs: %d", errno);
	}

	return 0;
}


void static *workermain(void *_selfv) {
	edb_worker_t *self = _selfv;
	log_infof("worker %lx starting...", self->pthread);
	while(self->state == EDB_WWORKASYNC) {
		selectjob(self);
	}
}

unsigned int nextworkerid = 1;
edb_err edb_workerinit(edb_worker_t *o_worker, edbpcache_t *edbpcache, edbl_t *lockdir, const edb_shm_t *shm, edb_fhead *fhead) {
	edb_err eerr = 0;
	//initialize
	bzero(o_worker, sizeof (edb_worker_t));
	o_worker->workerid = nextworkerid++;
	o_worker->shm = shm;
	o_worker->lockdir = lockdir; // todo, need to create handles for the lockdir.
	o_worker->fhead = fhead;
	eerr = edbp_newhandle(edbpcache, &o_worker->edbphandle);
	if(eerr) {
		return eerr;
	}
	return 0;
}

void edb_workerdecom(edb_worker_t *worker) {
	edbp_freehandle(&worker->edbphandle);
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
	if(worker->state != EDB_WWORKASYNC) {
		log_noticef("attempted to stop worker when not in working transferstate");
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