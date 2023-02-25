#include "edbs-jobs.h"
#include "edbs_u.h"
#include "errors.h"
#include "options.h"
#include "telemetry.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <strings.h>


// jobclose will force all calls to jobread and jobwrite, waiting or not. stop and imediately return -2.
// All subsequent calls to jobread and jobwrite will also return -2.
//
// jobreset will reopen the streams and allow for jobread and jobwrite to work as inteneded on a fresh buffer.
//
// By default, a 0'd out job starts in the closed state, so jobreset must be called beforehand. Multiple calls to
// edb_jobclose is ok. Multiple calls to edb_jobreset is ok.
//
// THREADING
//   At no point should jobclose and jobopen be called at the sametime with the same job. One
//   must return before the other can be called.
//
//   However, simultainous calls to the same function on the same job is okay (multiple threads calling
//   edb_jobclose is ok, multiple threads calling edb_jobreset is okay.)
edb_err edbs_jobterm(edbs_job_t jh) {

	// easy ptrs
	edbs_shmjob_t *job = &jh.shm->jobv[jh.jobpos];

	// check for EDB_EINVAL
	if(!jh.descriptortype) {
		// installer has called
		if(job->transferbuff_FLAGS & EDBS_JFINSTALLWRITE) {
			log_critf("installer called edbs_jobterm after it wrote to the buffer");
			return EDB_EOPEN;
		} else {
			job->transferbuff_FLAGS |= EDBS_JINSTALLERTERM;
			return 0;
		}
	} else {
		// executor has called

		// before we can mark the job as executer-terminated, are their still
		// bytes that the handle has yet to read? (provided we're in
		// bi-directional mode)
		futex_wait(&job->futex_exetermhold, 1);

		// atp: we know executorbytes is 0. And it will stay like that
		// even outside of the mutex because of the THREADING rules of edbs_job*


		// broadcast to interupt and transfers
		pthread_mutex_lock(&job->pipemutex); // lock so we can confidently set
		// See the structure comment for the futex holds. When this function
		// is called that means these cannot apply anymore.
		job->transferbuff_FLAGS |= EDBS_JEXECUTERTERM;
		job->futex_installerreadhold = 0;
		futex_wake(&job->futex_installerreadhold, 1);
		pthread_mutex_unlock(&job->pipemutex);
		return 0;
	}
}
edb_err edbs_jobwrite(edbs_job_t jh, const void *buff, int count) {
#ifdef EDB_FUCKUPS
	if(!buff && count) {
		log_critf("buff is 0 & count is not 0");
		return EDB_EINVAL;
	}
#endif
	edbs_shmjob_t *job = &jh.shm->jobv[jh.jobpos];
	uint8_t *jbuf = jh.shm->transbuffer + job->transferbuffoff;
	int totalbytes = 0;

	// get the descriptor
	unsigned int *pos, *opos;
	unsigned int *bytes, *obytes;
	uint32_t *hold, *ohold;
	if(jh.descriptortype) {

		// caller is executor

		// check for EDB_EBADE
		if(!(job->transferbuff_FLAGS & EDBS_JFINSTALLWRITE)) {
			log_critf("job executor's first directive was write, not read");
			return EDB_EBADE;
		}

		// has the executor specified this as 1-way?
		if(job->transferbuff_FLAGS & EDBS_JINSTALLERTERM) {
			// yup. So executor writes return immediately
			return 0;
		}

		// working vars
		pos = &job->executorhead;
		opos = &job->installerhead;
		bytes = &job->executorbytes;
		obytes = &job->installerbytes;
		hold = &job->futex_executorwritehold;
		ohold = &job->futex_installerreadhold;
	} else {
		// caller is installer

		// as per the above if statement, we can always assume this flag can be set.
		job->transferbuff_FLAGS |= EDBS_JFINSTALLWRITE;

		// working vars
		pos = &job->installerhead;
		opos = &job->executorhead;
		bytes = &job->installerbytes;
		obytes = & job->executorbytes;
		ohold = &job->futex_executorreadhold;
		hold = &job->futex_installerwritehold;
	}

	retry:

	// Wait if we've written so many bytes already that we've managed to fill
	// up the entire buffer up. In which case we must wait until they read
	// the bytes and decrement our byte count.
	futex_wait(hold, 1);

	// atp: we know that either room has been made in the buffer, or,
	//      edbs_jobclose has been called. We really don't know which one so
	//      our logic must not assume either one.

	// we'll lock up the buffer so that reads and writes don't happen at the
	// same time.
	pthread_mutex_lock(&job->pipemutex);

	// check for EDB_EPIPE
	if(job->transferbuff_FLAGS & EDBS_JEXECUTERTERM) {
		pthread_mutex_unlock(&job->pipemutex);
		return EDB_EPIPE;
	}

	// if the opposing side has bytes written in front of our head, then
	// thats an EDB_EPROTO error.
	if(*obytes) {
		pthread_mutex_unlock(&job->pipemutex);
		return EDB_EPROTO;
	}

#ifdef EDB_FUCKUPS
	if(*bytes == job->transferbuffcapacity) {
		log_critf("mutex aquired with full buffer capacity");
	}
#endif

	// atp: we know that we have no bytes infront of our head. We can write
	//      up until we hit the opposite side's head, they'd then have to
	//      read bytes to advance their head's position to make room in the
	//      buffer.

	// transfer up to count bytes into the buffer starting at the writehead
	// and only updating a byte if we haven't hit our capacity.
	int i;
	for(i = 0; i < count - totalbytes; i++) {
		jbuf[*pos] = ((const uint8_t *)(buff))[totalbytes+i]; // the actual
		// write
		*bytes = *bytes + 1; // increment the amount of unread bytes
		*pos = (*pos + 1) % job->transferbuffcapacity; // advance our head pos

		// if we have written so much into the buffer then we must break
		// out of this loop and leave the mutex and hold on the futex until
		// they read the bytes
		if(*bytes == job->transferbuffcapacity) {
			// we filled the buffer up writing this message to the other
			// side's head. We'll put a write hold on ourselves that they'll
			// be responsilbe for clearing
			*hold = 1;
			i++;
			break;
		}
	}

	// If the opposite side has a read hold, then we can clear it and
	// broadcast such event sense we know for a fact that we just read their
	// buffer.
	if(*ohold) {
		*ohold = 0;
		futex_wake(ohold, 1);
	}

	// if this is an executor call, this means executorbytes is no
	// longer 0.
	if(jh.descriptortype) {
		job->futex_exetermhold = 1;
	}

	// leave the mutex to allow the opposing read to execute.
	pthread_mutex_unlock(&job->pipemutex);

	// did we write all the bytes that the caller needed to write?
	totalbytes += i;
	if(totalbytes != count) {
		// We only wrote the partial amount of bytes they sent, so lets
		// perform this function again until we either get all the bytes we
		// need or the transfer buffer is closed.
		goto retry;
	}

	// we wrote totalbytes while == to count.
	return 0;

}
edb_err edbs_jobread(edbs_job_t jh, void *buff, int count) {
#ifdef EDB_FUCKUPS
	if(!buff && count) {
		log_critf("buff is 0 & count is not 0");
		return EDB_EINVAL;
	}
#endif
	edbs_shmjob_t *job = &jh.shm->jobv[jh.jobpos];
	uint8_t *jbuf = jh.shm->transbuffer + job->transferbuffoff;
	int totalbytes = 0;

	// get the descriptor
	unsigned int *pos, *opos;
	unsigned int *bytes, *obytes;
	uint32_t *hold, *ohold;
	if(jh.descriptortype) {

		// caller is executor

		// working vars
		pos = &job->executorhead;
		opos = &job->installerhead;
		bytes = &job->executorbytes;
		obytes = &job->installerbytes;
		hold   = &job->futex_executorreadhold;
		ohold  = &job->futex_installerwritehold;
	} else {

		// caller is installer

		// check for EDB_EBADE
		if(!(job->transferbuff_FLAGS & EDBS_JFINSTALLWRITE)) {
			return EDB_EBADE;
		}

		// check for EDB_ECLOSED
		if(job->transferbuff_FLAGS & EDBS_JINSTALLERTERM) {
			return EDB_ECLOSED;
		}

		// working vars
		pos = &job->installerhead;
		opos = &job->executorhead;
		bytes = &job->installerbytes;
		obytes = &job->executorbytes;
		hold   = &job->futex_installerreadhold;
		ohold  = &job->futex_executorwritehold;
	}

	retry:

	// Wait if we have no bytes in front of us
	futex_wait(hold, 1);

	// we'll lock up the buffer so that reads and writes don't happen at the
	// same time.
	pthread_mutex_lock(&job->pipemutex);

	// check for EDB_EPIPE
	if(job->transferbuff_FLAGS & EDBS_JEXECUTERTERM) {
		pthread_mutex_unlock(&job->pipemutex);
		return EDB_EPIPE;
	}

#ifdef EDB_FUCKUPS
	if(*obytes == 0) {
		log_critf("mutex aquired with 0 capacity");
	}
#endif

	// atp: we know that we have some bytes in front of our head. read them
	//      until we fill up the callers buffer or we run out of bytes in the
	//      transfer buffer to read in which case we wait for subsequent writes

	//
	int bytesread;
	for(bytesread = 0; bytesread < count - totalbytes; bytesread++) {
		((uint8_t *)buff)[totalbytes + bytesread] = jbuf[*pos];
		*obytes = *obytes - 1; // decrement the amount of unread bytes
		*pos = (*pos + 1) % job->transferbuffcapacity; // advance our head pos

		// if we have read everything from the opposing buffer.
		// We'll put a read hold on ourselves that they'll be responsible
		// clearing.
		if(*obytes == 0) {
			*hold = 1;
			bytesread++;
			break;
		}
	}

	// if we just cleared their write hold, broadcast it sense we just ate up
	// some of their buffer.
	*ohold = 0;
	futex_wake(ohold, 1);

	// if this call is the installer, and we've empied out all the bytes,
	// then the broadcast conditions of futex_exetermhold have been met.
	if(!jh.descriptortype && *obytes == 0) {
		job->futex_exetermhold = 0;
		futex_wake(&job->futex_exetermhold, 1);
	}

	// leave the mutex to allow the opposing read to execute.
	pthread_mutex_unlock(&job->pipemutex);

	// did we read all the bytes that the caller needed to read?
	totalbytes += bytesread;
	if(totalbytes != count) {
		// We only read the partial amount of bytes they sent, so lets
		// perform this function again until we either get all the bytes we
		// need or the transfer buffer is closed.
		goto retry;
	}

	// we wrote totalbytes while == to count.
	return 0;

}

edb_err edbs_jobselect(const edbs_handle_t *shm, edbs_job_t *o_job,
                       unsigned int ownerid) {
	if(ownerid == 0) {
		log_critf("jobselect has 0 ownerid");
		return EDB_EINVAL;
	}

	// save some pointers to the stack for easier access.
	o_job->shm = shm;
	o_job->descriptortype = 1;
	edbs_shmhead_t *const head = shm->head;
	const uint64_t jobc = head->jobc;
	edbs_shmjob_t *const jobv = shm->jobv;



	// we wait outside the mutex because if we were to wait inside of the
	// mutex that would prevent any jobs from being installed, thus
	// deadlocking us.
	rehold:
	futex_wait(&head->futex_selectorhold, 1);


	// atp: although our futex_selectorhold has returned, sense we're outside of a mutex,
	//      there's a chance that another thread has already changed this value.
	//      So you'll see the first if statement we ask
	//      inside of the mutex is if we should re-hold.

	// now we need to find a new job.
	// Firstly, we need to atomically lock all workers so that only one can accept a job at once to
	// avoid the possibility that 2 workers accidentally take the same job.
	pthread_mutex_lock(&head->jobmutex);

	// check for EDB_ECLOSED.
	if(!head->newjobs && head->futex_status != EDBS_SRUNNING) {
		pthread_mutex_unlock(&head->jobmutex);
		return EDB_ECLOSED;
	}

	// see above atp.
	if(head->futex_selectorhold == 1) {
		pthread_mutex_unlock(&head->jobmutex);
		goto rehold;
	}

	// atp: the host is EDBS_SRUNNING and newjobs is at least 1.
#ifdef EDB_FUCKUPS
	if(head->newjobs == 0) {
		log_critf("edbs_jobselect got in without any newjobs");
	}
#endif

	// if we're here that means we know that there's at least 1 new job.
	// lets find it.
	edbs_shmjob_t *job;
	{
		unsigned int i;
		for (i = 0; i < jobc; i++) {
			o_job->jobpos = head->jobacpt_next;
			job = &jobv[head->jobacpt_next];
			head->jobacpt_next = (head->jobacpt_next + 1) % jobc;
			if (job->owner == 0 && job->jobdesc != 0)  {
				// if we're here then we know this job is not owned
				// and has a job. We'll break out of the for loop.
				break;
			}
		}
		if (i == jobc) {
			// this is a critical error. If we're here that meas we went through
			// the entire stack and didn't find an open job. Sense newjobs
			// was supposed to be at least 1 that means I programmed something wrong.
			//
			// The first thread that enters into here will not be the last as futher
			// threads will discover the same thing.
			pthread_mutex_unlock(&head->jobmutex);
			log_critf("although newjobs was at least 1, an open job was not found in the stack.");
			return EDB_ECRIT;
		}
	}

	// at this point, job is pointing to an open job.
	// so lets go ahead and set the job ownership to us.
	job->owner = ownerid;
	head->newjobs--; // 1 less job that is without an owner.
	if(head->newjobs == 0) {
		head->futex_selectorhold = 1;
	}

	// we're done filing all the paperwork to have ownership over this job. thus no more need to have
	// the ownership logic locked for other thread. We can continue to do this job.
	telemetry_workr_accepted(ownerid, o_job->jobpos);
	pthread_mutex_unlock(&head->jobmutex);

	// if we're here that means we've accepted the job at jobv[self->jobpos] and we've
	// claimed it so other workers won't bother this job.
	return 0;
}

edb_err edbs_jobinstall(const edbs_handle_t *h,
                        unsigned int jobclass,
                        unsigned int name,
                        edbs_job_t *o_job) {
	// easy ptrs
	edb_err err;
	edbs_shmjob_t *jobv = h->jobv;
	edbs_shmhead_t *const head = h->head;
	const uint64_t jobc = head->jobc;
	o_job->shm = h;
	o_job->descriptortype = 0;

	rehold:
	// if there's no open spots in the job buffer we wait. Yes, we are
	// clogging up the mutex, but those threads will also have to wait.
	futex_wait(&head->futex_installerhold, 1);

	// atp: see the atp in edbs_jobselect after its futex_wait.

	// lock the job install mutex. So we don't try to install a job into a slot
	// that's currently being cleared out as completed.
	pthread_mutex_lock(&head->jobmutex);
	//**defer: pthread_mutex_unlock(&head->jobmutex);

	// check for EDB_ECLOSED
	if(head->futex_status != EDBS_SRUNNING) {
		pthread_mutex_unlock(&head->jobmutex);
		return EDB_ECLOSED;
	}

	// see above atp
	if(head->futex_installerhold == 1) {
		pthread_mutex_unlock(&head->jobmutex);
		goto rehold;
	}

	// if we're here we know there's at least 1 empty job slot and status is
	// EDBS_SRUNNING
	int i;
	edbs_shmjob_t *job;
	for(i = 0; i < jobc; i++) {
		o_job->jobpos = head->jobinst_next;
		job = &jobv[head->jobinst_next];
		head->jobinst_next = (head->jobinst_next + 1) % jobc;
		if (job->owner == 0 && job->jobdesc == 0) {
			// here we have an empty job with no owner, we can install it here.
			break;
		}
		if (i == jobc) {
			// this is a critical error. We should always have found an open
			// job because the futex let us through.
			pthread_mutex_unlock(&head->jobmutex);
			log_critf("although empty was at least 1, an open job was not "
					  "found in the stack.");
			return EDB_ECRIT;
		}
	}

	// install the job
	job->jobdesc = jobclass;
	job->jobid = h->head->nextjobid++;

	// futex signals
	head->emptyjobs--; // 1 less empty job slot
	if(!head->futex_hasjobs) {
		head->futex_installerhold = 1;
		head->futex_hasjobs = 1;
	}
	if(head->futex_selectorhold) {
		head->futex_selectorhold = 0;
		futex_wake(&head->futex_selectorhold, 1);
	}
	head->newjobs++;   // 1 more job for workers to adopt


#ifdef EDB_FUCKUPS
	if(!(job->transferbuff_FLAGS & EDBS_JEXECUTERTERM)) {
		log_critf("just accepted a job that has a buffer that wasn't marked "
				  "as terminated by its last executor");
	}
#endif

	// note to self: I had all the folowing "setup" logic in a weave lock as
	// to not take too much time in jobmutex. I ended up getting rid of the
	// weave lock because of an edge case where edbs_jobselect would return
	// with the job that edbs_jobinstall has yet to return from.

	// now reset all the descriptors for this job. that have nothing to do
	// with the job installment algo.
	job->executorbytes  = 0;
	job->executorhead   = 0;
	job->installerbytes = 0;
	job->installerhead  = 0;
	job->transferbuff_FLAGS = 0;
	// sense we know they're going to call
	// write first into a clean buffer
	job->futex_installerreadhold = 1;
	job->futex_installerwritehold = 0;
	// sense we know they're going to call read
	// first and they have to wait for the
	// write anyways
	job->futex_executorreadhold = 1;
	job->futex_executorwritehold = 0;
	job->name = name;


	// sense we set the jobclass, we can unlock. We now have installed our job.
	pthread_mutex_unlock(&head->jobmutex);

	return 0;
}

void  edbs_jobclose(edbs_job_t job) {
	if(!job.descriptortype) {
#ifdef EDB_FUCKUPS
		log_critf("edbs_jobclose when job is marked as installer");
#endif
		return;
	}

	// save some pointers to the stack for easier access.
	const edbs_handle_t *shm = job.shm;
	edbs_shmhead_t *const head = shm->head;
	const uint64_t jobc = head->jobc;
	edbs_shmjob_t *const jobv = &shm->jobv[job.jobpos];

	// close the transfer if it hasn't already.
	// note that per edb_jobclose's documentation on threading, edb_jobclose and edb_jobopen must
	// only be called inside the comfort of the jobinstall mutex.
	edbs_jobterm(job);

	// job has been completed. relinquish ownership.
	//
	// to do this, we have to lock the job install mutex to avoid the possiblity of a
	// handle trying to install a job mid-way through us relinqusihing it.
	//
	// (this can probably be done a bit more nicely if the jobinstall mutex was held per-job slot)
	pthread_mutex_lock(&head->jobmutex);

#ifdef EDB_FUCKUPS
	if(!jobv->owner) {
		log_critf("edbs_jobclose on a job that has already been closed");
	}
#endif

	// set this job slot to be empty and unowned
	jobv->jobdesc = 0;
	jobv->owner = 0;
	head->emptyjobs++;

	// futex signalling
	if(head->emptyjobs == jobc) {
		// we now have NO jobs left.
		head->futex_hasjobs = 0;
		futex_wake(&head->futex_hasjobs, INT32_MAX);
	}
	if(head->futex_installerhold) {
		// if we're in here, we know that per our structure definition for
		// futex_installerreadhold, it must be set to 0 sense we know that
		// emptyjobs is no longer 0.
		head->futex_installerhold = 0;
		futex_wake(&head->futex_installerhold, 1);
	}


	pthread_mutex_unlock(&head->jobmutex);
}
