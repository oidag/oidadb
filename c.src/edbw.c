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
/*int static inline edbw_jobread(edb_worker_t *self, void *bufv, int bufc) {
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
}*/


// function to easily verbosely log worker and job ids
#define edbw_logverbose(workerp, fmt, ...) \
log_debugf("worker#%d executing job#%ld: " fmt, workerp->workerid, workerp->curjob.job->jobid, ##__VA_ARGS__)

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
	edbs_jobhandler *job = &self->curjob;
	int jobdesc = job->job->jobdesc;
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
	if(edbs_jobisclosed(job)) {
		err = EDB_ECRIT;
		log_critf("job accepted by worker but the handle did not open job buffer");
		goto closejob;
	}

	// check for some common errors regarding the edb_jobclass
	edb_oid oid;
	int ret;
	switch (jobdesc & 0x00FF) {
		case EDB_OBJ:
		case EDB_DYN:
			// all of these job classes need an id parameter
			ret = edbs_jobread(job, &oid, sizeof(oid));
			if(ret == -1) {
				err = EDB_ECRIT;
				edbs_jobwrite(job, &err, sizeof(err));
				goto closejob;
			} else if(ret == -2) {
				err = EDB_EHANDLE;
				edbs_jobwrite(job, &err, sizeof(err));
				goto closejob;
			}
			break;

		default: break;
	}


	// check for some common errors regarding the command
	switch (jobdesc & 0xFF00) {
		case EDB_CDEL:
		case EDB_CCOPY:
			break;
	}


	const edb_struct_t *st;
	edb_usrlk *lks;
	void *f;

	// if err is non-0 after this then it will close
	switch (jobdesc) {
		case EDB_OBJ | EDB_CDEL:
		case EDB_OBJ | EDB_CUSRLKW:
		case EDB_OBJ | EDB_CWRITE:
			// all require write access
			err = edba_objectopen(self->edbahandle, oid, EDBA_FWRITE);
			break;

		case EDB_OBJ | EDB_CCREATE:
			err = edba_objectopenc(self->edbahandle, &oid, EDBA_FWRITE | EDBA_FCREATE);
			break;

		case EDB_OBJ | EDB_CUSRLKR:
		case EDB_OBJ | EDB_CCOPY:
			// read only
			err = edba_objectopen(self->edbahandle, oid, 0);
			break;

		default:break;
	}
	if(err) {
		edbs_jobwrite(&self->curjob, &err, sizeof(err));
		goto closejob;
	}

	// do the routing
	switch (jobdesc) {
		case EDB_OBJ | EDB_CCREATE:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// lock check
			lks = edba_objectlocks(self->edbahandle);
			if(*lks | EDB_FUSRLCREAT) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// read in and create object
			st = edba_objectstruct(self->edbahandle);
			f  = edba_objectfixed(self->edbahandle);
			edbs_jobread(&self->curjob, f, st->fixedc);
			break;

		case EDB_OBJ | EDB_CCOPY:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// lock check
			lks = edba_objectlocks(self->edbahandle);
			if(*lks | EDB_FUSRLRD) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// write out the object.
			st = edba_objectstruct(self->edbahandle);
			f  = edba_objectfixed(self->edbahandle);
			edbs_jobwrite(&self->curjob, f, st->fixedc);
			break;

		case EDB_OBJ | EDB_CWRITE:
			edbw_logverbose(self, "edit object 0x%016lX", oid);

			// lock check
			lks = edba_objectlocks(self->edbahandle);
			if(*lks | EDB_FUSRLWR) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// update the record
			st = edba_objectstruct(self->edbahandle);
			f  = edba_objectfixed(self->edbahandle);
			edbs_jobread(&self->curjob, f, st->fixedc);

			break;
		case EDB_OBJ | EDB_CDEL:
			// object-deletion
			edbw_logverbose(self, "delete object 0x%016lX", oid);

			// lock check
			lks = edba_objectlocks(self->edbahandle);
			if(*lks | EDB_FUSRLWR) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// (note we don't mark as no-error until after the delete)

			// perform the delete
			err = edba_objectdelete(self->edbahandle);
			edbs_jobwrite(&self->curjob, &err, sizeof(err));
			break;

		case EDB_OBJ | EDB_CUSRLKR:
			edbw_logverbose(self, "cuserlock write object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// read-out locks
			lks = edba_objectlocks(self->edbahandle);
			edbs_jobwrite(&self->curjob, lks, sizeof(edb_usrlk));
			break;

		case EDB_OBJ | EDB_CUSRLKW:
			edbw_logverbose(self, "cuserlock read object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// update locks
			lks = edba_objectlocks(self->edbahandle);
			edbs_jobread(&self->curjob, lks, sizeof(edb_usrlk));
			break;

		default:
			err = EDB_EINVAL;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));
			log_critf("execjob was given a bad jobid: %04x", jobdesc);
			goto closejob;
	}

	if(jobdesc | EDB_OBJ) {
		edba_objectclose(self->edbahandle);
	}

	closejob:
	edbs_jobclose(&self->curjob);
	return err;
}


void static *workermain(void *_selfv) {
	edb_worker_t *self = _selfv;
	edb_err err;
	log_infof("worker %lx starting...", self->pthread);
	while(self->state == EDB_WWORKASYNC) {
		err = edbs_jobselect(self->shm, &self->curjob, self->workerid);
		if(err) {
			log_critf("worker %d: failed to select job: %d", self->workerid, err);
		} else {
			execjob(self);
			edbs_jobclose(&self->curjob);
		}
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
	o_worker->curjob.jobpos = 0;
	o_worker->edbahandle = edba_somethingsomething();
	if(!o_worker->edbahandle) {
		return EDB_ECRIT;
	}
	eerr = edbp_newhandle(edbpcache, &o_worker->edbphandle);
	if(eerr) {
		edba_somethingclose(o_worker->edbahandle);
		return eerr;
	}
	return 0;
}

void edb_workerdecom(edb_worker_t *worker) {
	edbp_freehandle(&worker->edbphandle);
	edba_somethingclose(worker->edbahandle);
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