#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "include/ellemdb.h"
#include "worker.h"
#include "jobs.h"
#include "edb.h"
#include "locks.h"
#include "pages-types.h"

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

// converts a pid offset to the actual page address
void static rowoffset_lookup(edb_worker_t *self,
								int depth,
								edb_pid lookupchapter,
								edb_pid pidoffset,
								edb_pid *o_pid) {

	// root
	edbp_start(&self->edbphandle, &lookupchapter); // (ignore error audaciously)
	edbp_lookup_t *l = edbp_glookup(&self->edbphandle);
	edb_lref_t *refs = edbp_lookup_refs(l);
	for(int i = 0; i < l->refc; i++) {
		// todo:
		HEAD HURT;
	}
	edbp_finish(&self->edbphandle);

}

// copies the object (not including dynamic data) from the
// database to the job buffer.
//
// later: documentat that this should be expected in the job buffer:
//        <- = incoming, -> = outgoing
//        <- edb_oid
//        -> error (edb_err, 0 means go to next step) // todo: document what errors happen
//        -> void *rowdata
//
todo! go through this function and clean it up.
its a damn good function. and I had to make a fuck load of prototypes for it.
just make sure its super clean.
edb_err static execjob_objcopy(edb_worker_t *self, edb_job_t *job) {
	// first find the object we need based of the ID.
	edb_err err = 0;
	edb_oid search;
	int ret = edb_jobread(job, self->shm->transbuffer, &search, sizeof(search));
	if(ret == -1) {
		return EDB_ECRIT;
	}
	if(ret == -2) {
		log_errorf("handle failed to send edb_oid through the job buffer on objcopy");
		return EDB_EHANDLE;
	}

	// later: the below should probably be exported into a function
	// now we have the object id, lets unpack it.
	unsigned int entryid;
	unsigned int rowid;
	{ // (new scope to be ready to extract a function)
		entryid = search & 0xFFFF;
		rowid   = search >> 0x10;
		// perform the lookup to get the entry data
		edb_entry_t entrydat;
		edbl_object(&self->lockdir, search, EDBL_SHARED); // todo: check for errors? // todo: make sure its unlocked
		err = edbs_index(self->shm, entryid, &entrydat);
		if(err) {
			log_errorf("the supplied edb_oid does not have a valid entryid: %d", entryid);
			edbl_object(&self->lockdir, search, EDBL_NONE);
			return EDB_EHANDLE;
		}

		// get the page offset from the start of the chapter.
		edb_pid pageoffset = rowid / entrydat.objectsperpage;
		if(pageoffset >= entrydat.ref0c) {
			// the page offset is larger than the amount of pages we have.
			// thus, this rowid is impossible.
			log_errorf("the supplied edb_oid has a page offset that is too large: %ld", pageoffset);
			edbl_object(&self->lockdir, search, EDBL_NONE);
			return EDB_EHANDLE;
		}

		// now we know how many pages we need to go in. So we now need to go
		// into the lookup b+tree. So lets pull up that information.
		// the following info is extracted from spec.
		int btree_depth = entrydat.memory >> 3;

		// do the b-tree lookup
		edb_pid foundpage;
		rowoffset_lookup(self, // todo: check for errors?
		                 btree_depth,
		                 entrydat.ref1,
		                 pageoffset,
		                 &foundpage);




		// page with the row was found. lets load it.

		// get the intrapage offset
		unsigned int intrapageoff = (rowid % entrydat.objectsperpage);

		// lock the page in cache
		err = edbp_start(&self->edbphandle, &foundpage);
		if(err) {
			log_critf("unhandled error %d", err);
			edbl_object(&self->lockdir, search, EDBL_NONE);
			return EDB_ECRIT;
		}

		// parse the page into objecs.
		edbp_object_t *o = edbp_gobject(&self->edbphandle);
		void *recorddata = edbp_object_body(o) + intrapageoff * o->fixedlen;

		// get the flags
		uint32_t flags = *(uint32_t *)recorddata;
		void     *body  = recorddata + sizeof(uint32_t);
		// is it read locked?
		if(flags & EDB_FUSRLRD) {
			// read-locked. cannot access it.
			err = EDB_EULOCK;
			edb_jobwrite(job, &self->shm, &err, sizeof(edb_err));
			goto finishpage;
		}
		// is it deleted?
		if(flags & EDB_FDELETED) {
			err = EDB_ENOENT;
			edb_jobwrite(job, &self->shm, &err, sizeof(edb_err));
			goto finishpage;
		}

		// its all good.
		err = 0;
		edb_jobwrite(job, &self->shm, &err, sizeof(edb_err));

		// throw it all in the transfer buffer
		edb_jobwrite(job, &self->shm, body, o->fixedlen);


		finishpage:
		edbp_finish(&self->edbphandle);

		unlockobj:
		edbl_object(&self->lockdir, search, EDBL_NONE);

	}


}

// helper func to selectjob.
//
// executes the job only. does not mark the job as complete nor relinquish ownership.
// thats the callers responsibility.
//
// This function's only purpose is to route the information into the relevant execjob_...
// function.
//
// returns EDB_ECRIT on critical error
// returns EDB_EINVAL if something went wrong because the handle didn't format something correctly.
// returns EDB_EHANDLE if something went wrong with the handle.
// returns EDB_??? on any other error that is the handle's fault.
static edb_err execjob(edb_worker_t *self, edb_job_t *job) {


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

	// check for some common errors regarding the edb_jobclass
	switch (job->jobdesc & 0x00FF) {
		case EDB_STRUCT:
		case EDB_OBJ:
			// FRH
			// All objects commands need at least either the id or the transferbuffer.
			if(job->data.id == 0 && edb_jobisclosed(job)) {
				// invalid. this shouldn't have been installed.
				log_critf("data id nor open stream was supplied in job");
				return -1;
			}
	}

	// check for some common errors regarding the command
	switch (job->jobdesc & 0xFF00) {
	}

	// do the routing
	switch (job->jobdesc) {
		case EDB_OBJ | EDB_CCOPY:
			// no reouting needed.
			return execjob_objcopy(self, job);

		case EDB_OBJ | EDB_CWRITE:
			// routing
			if(edb_jobisclosed(job)) {
				// deletion
				return execjob_objdelete(self, job);
			}
			if(job->data.id == 0) {
				// creation
				return execjob_objcreate(self, job);
			}
			// we have an open stream and a id, thus, it is an edit.
			return execjob_objedit(self, job);



		default:
			log_critf("execjob was given an non-job: %0x", job->jobdesc);
			return 1;
	}

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

	// if we're here that means we've accepted the job at jobv[self->jobpos].
	err = execjob(self, &jobv[self->jobpos]);
	if (err) {
		log_critf("critical error while executing job");
	}

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
edb_err edb_workerinit(edb_worker_t *o_worker, edbpcache_t *edbpcache, edbl_t *lockdir, const edb_shm_t *shm) {
	edb_err eerr = 0;
	//initialize
	bzero(o_worker, sizeof (edb_worker_t));
	o_worker->workerid = nextworkerid++;
	o_worker->shm = shm;
	o_worker->lockdir = lockdir; // todo, need to create handles for the lockdir.
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