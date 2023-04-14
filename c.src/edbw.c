#include "edbw.h"
#include "edbw_u.h"

#include <strings.h>

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
// Will only return ODB_ECRIT, can be ignored and continued.
static odb_err execjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t *job = &self->curjob;
	int jobdesc = edbs_jobtype(*job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// route the job to the relevant sub-namespace
	switch (jobdesc & 0x00FF) {
		case EDB_OBJ:
			err = edbw_u_objjob(self);
			break;
		case EDB_ENT:
			err = edbw_u_entjob(self);
			break;
		case EDB_STRUCT:
			err = edbw_u_structjob(self);
			break;
		default:
			err = ODB_EJOBDESC;
			break;
	}
	if(err == ODB_EJOBDESC) {
		log_errorf("invalid job description hash: %04x", jobdesc);
	}

	closejob:
	edbs_jobclose(self->curjob);
	return err;
}

void static *workermain(void *_selfv) {
	edb_worker_t *self = _selfv;
	odb_err err;
	log_infof("worker %lx starting...", self->pthread);
	while(self->state == EDB_WWORKASYNC) {
		err = edbs_jobselect(self->shm, &self->curjob, self->workerid);
		if(err) {
			log_critf("worker %d: failed to select job: %d", self->workerid, err);
		} else {
			execjob(self);
			edbs_jobclose(self->curjob);
		}
	}
	return 0;
}

unsigned int nextworkerid = 1;
odb_err edbw_init(edb_worker_t *o_worker, edba_host_t *edbahost, const edbs_handle_t *shm) {
	odb_err eerr;
	//initialize
	bzero(o_worker, sizeof (edb_worker_t));
	o_worker->workerid = nextworkerid++;
	o_worker->shm = shm;
	o_worker->curjob.jobpos = 0;
	eerr = edba_handle_init(edbahost,
							o_worker->workerid,
							&o_worker->edbahandle);
	if(eerr) {
		return eerr;
	}
	return 0;
}

void edbw_decom(edb_worker_t *worker) {
	edbw_stop(worker);
	edba_handle_decom(worker->edbahandle);
}


odb_err edbw_async(edb_worker_t *worker) {
	if(worker->state != EDB_WWORKNONE) {
		log_critf("attempting to start already-running worker");
		return ODB_ECRIT;
	}
	int err = pthread_create(&(worker->pthread), 0, workermain, worker);
	if(err) {
		log_critf("failed to create thread pthread_create(3) returned: %d", err);
		return ODB_ECRIT;
	}
	worker->state = EDB_WWORKASYNC;
	return 0;
}



void edbw_stop(edb_worker_t *worker) {
	if(worker->state != EDB_WWORKASYNC) {
		log_noticef("attempted to stop worker when not in working transferstate");
		return;
	}
	worker->state = EDB_WWORKSTOP;
}

odb_err edbw_join(edb_worker_t *worker) {
	if (worker->state == EDB_WWORKNONE) {
		return 0; // already stopped and joined.
	}
	if(worker->state != EDB_WWORKSTOP) {
		log_critf("attempted to join on a worker without stopping it first");
		return ODB_ECRIT;
	}
	int err = pthread_join(worker->pthread, 0);
	if(err) {
		log_critf("pthread_join(3) returned error: %d", err);
	}
	worker->state = EDB_WWORKNONE;
	return 0;
}