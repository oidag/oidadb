#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "include/ellemdb.h"
#include "edbw.h"
#include "edbs-jobs.h"
#include "file.h"
#include "edbl.h"
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

// converts a pid offset to the actual page address
// note to self: the only error returned by this should be a critical error
//
// assumptions:
//     pidoffset_search is less than the total amount of pages in the edbp_object chapter
edb_err static rowoffset_lookup(edb_worker_t *self,
								int depth,
								edb_pid lookuppage,
								edb_pid pidoffset_search,
								edb_pid *o_pid) {


	// install sh lock on first byte of page per Object-Reading spec
	edbl_lockref lock = (edbl_lockref){
			.l_type = EDBL_TYPSHARED,
			.l_len = 1,
			.l_start = edbp_pid2off(self->cache, lookuppage),
	};
	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&self->lockdir, lock);
	lock.l_type = EDBL_TYPUNLOCK; // doing this in advance

	// ** defer: edbp_finish(&self->edbphandle);
	edb_err err = edbp_start(&self->edbphandle, &lookuppage); // (ignore error audaciously)
	if(err) {
		edbl_set(&self->lockdir, lock);
		return err;
	}
	// set the lookup hint now
	// generate the proper EDBP_HINDEX... value
	edbp_hint h = EDBP_HINDEX0 - depth * 0x10;
	edbp_mod(&self->edbphandle, EDBP_CACHEHINT, h);
	edbp_lookup_t *l = edbp_glookup(&self->edbphandle);
	edb_lref_t *refs = edbp_lookup_refs(l);

	if(depth == 0) {
		// if depth is 0 that means this page is full of leaf node references.
		// more specifically, full of leaf node /page strait/ references.
		// We know that pidoffset_search is somewhere in one of these straits.
		int i;
		for(i = 0; i < l->refc; i++) {
			// it is now the END offset
			if(refs[i].startoff_strait > pidoffset_search) {
				// here is the same logic as the non-depth0 for loop but instead
				// of reference
				break;
			}
		}
		// todo: what if ref is 0?
		// we know that this reference contains our page in its strait.
		// So if our offset search is lets say 5, and this strait contained
		// pageoffsets 4,5,6,7,8 and associating pageids of 42,43,44,45,46.
		// that means offset 8ref46 would be the one referenced in this strait
		// and thus we subtract the end offset with the offset we know thats
		// in there and that will give us an /ref-offset/ of 2. We then
		// take the end-referance and subtrack our ref offset which gives
		// us the page offset.
		// ie: *o_pid = 46 - (8 - 5)
		*o_pid = refs[i].ref - (refs[i].startoff_strait - pidoffset_search);

		// So lets finish out of this page...
		edbp_finish(&self->edbphandle);
		// and release the lock
		edbl_set(&self->lockdir, lock);
		return 0;
	}

	int i;
	for(i = 0; i < l->refc; i++) {
		if(i+1 == l->refc || refs[i+1].startoff_strait > pidoffset_search) {
			// logically, if we're in here that means the next interation (i+1)
			// will be the end ouf our reference list, or, will be a reference
			// that has a starting offset that is larger than this current
			// iteration. Thus, we can logically deduce that our offset is somewhere
			// down in this iteration.
			//
			// To clear out some scope, lets break out and continue
			break;
		}
		// todo: what if refs[i].ref is 0?
	}
	// note: based on our logic in the if statement, i will never equal l->refc.

	// at this point, we know that refs[i] is the reference we must follow.
	// Lets throw the important number in our stack.
	edb_pid nextstep = refs[i].ref;

	// So lets finish out of this page...
	edbp_finish(&self->edbphandle);
	// and release the lock
	edbl_set(&self->lockdir, lock);

	// now we can recurse down to the next lookup page.
	rowoffset_lookup(self,
	                 depth-1,
	                 nextstep,
	                 pidoffset_search,
	                 o_pid);
	return 0;
}

typedef enum obj_searchflags_em {
	// exclusive lock on the object binary instead of shared
	OBJ_XL = 0x0001,

} obj_searchflags;

typedef struct obj_searchparams_st {

	// inputs:
	edb_worker_t   *self;
	edb_eid         entryid;
	uint64_t        rowid;
	obj_searchflags flags;

	// outputs:
	// all o_ params are optional, but all previous o_ params must be non-null
	// for a given o_ param to be written too.
	//
	//   - o_entrydat: the entry data pulled from entryid
	//   - o_structdata: the structure data pulled from structures
	//   - o_objectoff: the total amount of bytes offset from the start of the file
	//                  requires transversing the edbp_lookup btree
	//   - o_objectdat: a pointer to the object data itself. note that o_objectdat
	//                  will point to the start of the whole object (head/flags included)
	edb_entry_t   **o_entrydat;
	edb_struct_t  **o_structdata;
	uint64_t       *o_objectoff;
	void          **o_objectdat;
} obj_searchparams;

// helper function to all execjob_obj... functions.
//
// This will install proper locks and get meta data needed to
// work with objects.
//
// make sure to run execjob_obj_post when done with the same arguments.
//
// returns 1 if something is wrong and you should close out of
// the job (return from your function). Logs and error reports all handled.
// If returns 1 then no need for execjob_obj_post.
//

//
int static execjob_obj_pre(obj_searchparams dat) {
	edb_err err;

	edb_worker_t *self = dat.self;
	edb_eid entryid = dat.entryid;
	uint64_t rowid = dat.rowid;

	// SH lock the entry
	// ** defer: edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
	edbl_entry(&self->lockdir, entryid, EDBL_TYPSHARED);

	// get the index entry
	if(!dat.o_entrydat) {
		return 0;
	}
	err = edbs_index(self->shm, entryid, dat.o_entrydat);
	if(err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_errorf("the supplied edb_oid does not have a valid entryid: %d", entryid);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}
	edb_entry_t *entrydat = *dat.o_entrydat;

	// get the structure data
	if(!dat.o_structdata) {
		return 0;
	}
	err = edbs_structs(self->shm,
	                   entrydat->structureid,
	                   dat.o_structdata);
	if (err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_critf("failed to load structure information (%d) for some reqson for a valid entry: %d",
		          entrydat->structureid, entryid);
		err = EDB_ECRIT;
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// get the page offset for the rowid from the start of the chapter.
	if(!dat.o_objectoff) {
		return 0;
	}
	edb_pid pageoffset;
	edb_pid foundpage;
	edb_struct_t *structdata = *dat.o_structdata;
	pageoffset = rowid / entrydat->objectsperpage;
	if (pageoffset >= entrydat->ref0c) {
		// the page offset is larger than the amount of pages we have.
		// thus, this rowid is impossible.
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_errorf("the supplied edb_oid has a page offset that is too large: %ld", pageoffset);
		err = EDB_EEOF;
		edbw_jobwrite(self, &err, sizeof(err));
		return 0;
	}

	// now we know how many pages we need to go in. So we now need to go
	// into the lookup b+tree. So lets pull up that information.
	// the following info is extracted from spec.
	int btree_depth = entrydat->memory >> 3;

	// do the b-tree lookup
	err = rowoffset_lookup(self,
	                       btree_depth,
	                       entrydat->ref1,
	                       pageoffset,
	                       &foundpage);
	if(err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// page with the row was found.

	// So we calculate all the offset stuff.
	// get the intrapage byte offset
	// use math to get the byte offset of the start of the row data
	unsigned int intrapage_row = edbp_object_intraoffset(rowid,
	                                                     pageoffset,
	                                                     entrydat->objectsperpage,
	                                                     structdata->fixedc);
	*dat.o_objectoff = edbp_pid2off(self->cache, pageoffset) +intrapage_row;

	if(!dat.o_objectdat) {
		return 0;
	}
	// as per locking spec, need to place the lock on the data before we load the page.
	// install the SH lock as per Object-Reading
	// or install an XL lock as per Object-Writing
	edbl_lockref lock = (edbl_lockref) {
			.l_type  = EDBL_TYPSHARED,
			.l_start = (*dat.o_objectdat),
			.l_len   = structdata->fixedc,
	};
	if(dat.flags & OBJ_XL) {
		lock.l_type = EDBL_EXCLUSIVE;
	}
	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&self->lockdir, lock);
	lock.l_type = EDBL_TYPUNLOCK; // set this in advance for back-outs

	// lock the page in cache
	err = edbp_start(&self->edbphandle, &foundpage);
	if(err) {
		edbl_set(&self->lockdir, lock);
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_critf("unhandled error %d", err);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// parse the page into an edbp_object page
	edbp_object_t *o = edbp_gobject(&self->edbphandle);

	// get the offset for the record
	*dat.o_objectdat = o + intrapage_row;

	return 0;
}
void static execjob_obj_post(obj_searchparams dat) {
	edb_worker_t *self = dat.self;

	// finis the page. This function will execute all the defers in the _pre function
	if(dat.o_objectdat) {
		// let the cache know we're done with the page
		edbp_finish(&self->edbphandle);
		// let the traffic control know we're done with the record
		edbl_lockref lock = (edbl_lockref) {
				.l_type  = EDBL_TYPUNLOCK,
				.l_start = (*dat.o_objectoff),
				.l_len   = (*dat.o_structdata)->fixedc,
		};
		edbl_set(&self->lockdir, lock);
	}
	// let the traffic control know we're done with the entry
	edbl_entry(&self->lockdir, dat.entryid, EDBL_TYPUNLOCK);
}


// copies the object (not including dynamic data) from the
// database to the job buffer.
void static execjob_objcopy(edb_worker_t *self, edb_eid entryid, uint64_t rowid) {

	// easy pointers
	edb_job_t *job = self->curjob;

	// first find the object we need based of the ID from the transfer buffer
	edb_err err = 0;

	edb_entry_t *entrydat;
	edb_struct_t *structdata;
	uint64_t dataoff;
	edb_pid pageoffset;
	void *recorddata;
	obj_searchparams dat = {0};
	dat = (obj_searchparams){
			self,
			entryid,
			rowid,
			0,
			&entrydat,
			&structdata,
			&dataoff,
			&recorddata
	};
	err = execjob_obj_pre(dat);
	if(err) {
		return;
	}

	// get the flags
	uint32_t flags = *(uint32_t *)recorddata;
	void     *body  = recorddata + sizeof(uint32_t); // plus uint32_t to get past the flags
	// is it deleted?
	if(flags & EDB_FDELETED) {
		err = EDB_ENOENT;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	// is it locked?
	if(flags & EDB_FUSRLRD) {
		err = EDB_EULOCK;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}

	// its all good.
	err = 0;
	edbw_jobwrite(self, &err, sizeof(err));

	// throw it all in the transfer buffer
	edbw_jobwrite(self, body, structdata->fixedc);

	finishpage:
	execjob_obj_post(dat);
}

void static execjob_objedit(edb_worker_t *self, edb_eid entryid, uint64_t rowid) {

	// easy vars
	edb_job_t *job = self->curjob;

	edb_err err = 0;

	// get additional parameters
	uint32_t start,end;
	int ret;
	ret = edbw_jobread(self, &start, sizeof(start));
	ret += edbw_jobread(self, &end, sizeof(end));
	if(ret != sizeof(uint32_t)*2) {
		err = EDB_EHANDLE;
		edbw_jobwrite(self, &err, sizeof(err));
		return;
	}
	if(start >= end) {
		err = EDB_EINVAL;
		edbw_jobwrite(self, &err, sizeof(err));
		return;
	}

	edb_entry_t *entrydat;
	edb_struct_t *structdata;
	uint64_t dataoff;
	void *recorddata;
	obj_searchparams dat = (obj_searchparams){
			self,
			entryid,
			rowid,
			OBJ_XL,
			&entrydat,
			&structdata,
			&dataoff,
			&recorddata
	};
	err = execjob_obj_pre(dat);
	if(err) {
		return;
	}

	// check for flags
	// was it deleted?
	uint32_t flags = *(uint32_t *)recorddata;
	void     *body  = recorddata + sizeof(uint32_t); // plus uint32_t to get past the flags
	if(flags & EDB_FDELETED) {
		err = EDB_ENOENT;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	// is it write-locked?
	if(flags & EDB_FUSRLWR) {
		err = EDB_EULOCK;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}

	// clamp parameters
	if(start > structdata->fixedc) {
		err = EDB_EOUTBOUNDS;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	if(end > structdata->fixedc) {
		end = structdata->fixedc;
	}

	// its all good.
	err = 0;
	edbw_jobwrite(self, &err, sizeof(err));

	// read in the data
	edbw_jobread(self, body + start, (int)(end - start));

	// sense we just modified the page then hint that its dirty.
	edbp_mod(&self->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);

	finishpage:
	execjob_obj_post(dat);
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
	self->curjob = &jobv[self->jobpos]; // set curjob
	execjob(self);
	self->curjob = 0; // null out curjob.

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