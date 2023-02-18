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
void edb_jobclose(edbs_shmjob_t *job) {
	pthread_mutex_lock(&job->bufmutex);

	if (job->state == 0) {
		pthread_mutex_unlock(&job->bufmutex);
		return;
	}

	job->state = 0;
	// if there's anything waiting right now then wake them up. They'll preceed to find
	// that the state is closed and do nothing.
	syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAKE, 16, 0, 0, 0);
	pthread_mutex_unlock(&job->bufmutex);
	return;
}
int edb_jobreset(edbs_shmjob_t *job) {
	pthread_mutex_lock(&job->bufmutex);

	if (job->state == 1) {
		pthread_mutex_unlock(&job->bufmutex);
		return 0;
	}

	job->futex_transferbuffc = 0;
	job->writehead = 0;
	job->readhead = 0;
	job->state = 1;

	pthread_mutex_unlock(&job->bufmutex);
	return 0;
}

// todo: really need to test this function.
int edbs_jobread(edbs_job_t jh, void *buff, int count) {
	edbs_shmjob_t *job = &jh.shm->jobv[jh.jobpos];
	void *transferbuf = jh.shm->transbuffer;
	int err;
	{
		// we must block until we know we have room. So block if we are capacity.
		// However, we don't want to start waiting if the job transfer is closed.
		// We have to lock the mutex to ensure we don't run into race conditions
		// with edb_jobclose
		pthread_mutex_lock(&job->bufmutex);
		if(job->state == 0) {
			// this job buffer is closed.
			return -2;
		}
		pthread_mutex_unlock(&job->bufmutex);
		err = syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAIT, job->transferbuffcapacity, 0,
		              0, 0);
		if (err == -1 && errno != EAGAIN) {
			log_critf("failed to listen to futex on buffer");
			return -1;
		}
	}

	// we have room. now dump in as much buff as we can until the capacity is full.

	// we'll lock up the buffer so that reads and writes don't happen at the same time.
	pthread_mutex_lock(&job->bufmutex);

	// we have to ask this again because the closed status could have been changed
	// before the FUTEX_WAKE was called.
	if(job->state == 0) {
		// this job buffer is closed.
		return -2;
	}

	// transfer up to count bytes into the buffer starting at the writehead
	// and only updating a byte if we haven't hit our capacity.
	char *jbuf = transferbuf + job->transferbuffoff;
	int i;
	for(i = 0; i < count; i++) {
		if(job->futex_transferbuffc == job->transferbuffcapacity) {
			// we filled the buffer up.
			break;
		}
		jbuf[job->writehead] = ((const char *)(buff))[i];
		job->futex_transferbuffc++;
		job->writehead = job->writehead+1 % job->transferbuffcapacity;
	}

	// send a broadcast out too the reader if it happens to be waiting on something to be sent
	// to the buffer.
	syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAKE, 1, 0, 0, 0);
	pthread_mutex_unlock(&job->bufmutex);
	return i;

}
int edbs_jobwrite(edbs_job_t jh, const void *buff, int count) {
	edbs_shmjob_t *job = &jh.shm->jobv[jh.jobpos];
	void *transferbuf = jh.shm->transbuffer;
	int err;
	{
		// we must wait if there nothing in the buffer.
		// However, we don't want to start waiting if the job transfer is closed.
		// We have to lock the mutex to ensure we don't run into race conditions
		// with edb_jobclose
		pthread_mutex_lock(&job->bufmutex);
		if(job->state == 0) {
			// this job buffer is closed.
			return -2;
		}
		pthread_mutex_unlock(&job->bufmutex);
		err = syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAIT, 0, 0, 0, 0);
		if (err == -1 && errno != EAGAIN) {
			log_critf("failed to listen to futex on buffer");
			return -1;
		}
	}

	// the buffer has stuff in it.

	// we'll lock up the buffer so that reads and writes don't happen at the same time.
	pthread_mutex_lock(&job->bufmutex);

	// we have to ask this again because the closed status could have been changed
	// before the FUTEX_WAKE was called.
	if(job->state == 0) {
		// this job buffer is closed.
		return -2;
	}

	// transfer up to count bytes into the buffer starting at the writehead
	// and only updating a byte if we haven't hit our capacity.
	const char *jbuf = transferbuf + job->transferbuffoff;
	int i;
	for(i = 0; i < count; i++) {
		if(job->futex_transferbuffc == 0) {
			// buffer is empty.
			break;
		}
		((char *)buff)[i] = jbuf[job->readhead];
		job->futex_transferbuffc--;
		job->readhead = job->readhead+1 % job->transferbuffcapacity;
	}

	// wake up any write calls waiting for the buffer to be emtpied.
	syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAKE, 1, 0, 0, 0);
	pthread_mutex_unlock(&job->bufmutex);
	return i;

}

edb_err edbs_jobselect(const edbs_handle_t *shm, edbs_job_t *o_job,
                       unsigned int ownerid) {
	int err;

	// save some pointers to the stack for easier access.
	o_job->shm = shm;
	edbs_shmhead_t *const head = shm->head;
	const uint64_t jobc = head->jobc;
	edbs_shmjob_t *const jobv = shm->jobv;


	// now we need to find a new job.
	// Firstly, we need to atomically lock all workers so that only one can accept a job at once to
	// avoid the possibility that 2 workers accidentally take the same job.
	err = pthread_mutex_lock(&head->jobaccept);
	if (err) {
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
	if (err == -1 && errno != EAGAIN) {
		pthread_mutex_unlock(&head->jobaccept);
		log_critf("critical error while waiting on new jobs: %d", errno);
		return EDB_ECRIT;
	}

	// if we're here that means we know that there's at least 1 new job. lets find it.
	{
		int i;
		for (i = 0; i < jobc; i++) {

			if (jobv[o_job->jobpos].owner != 0 || jobv[o_job->jobpos].jobdesc != 0) {
				// this job is already owned.
				// so this job is not owned however there's no job installed.
				goto next;
			}

			// if we're here then we know this job is not owned
			// and has a job. We'll break out of the for loop.
			break;

			next:
			// increment the worker's position.
			o_job->jobpos = (o_job->jobpos + 1) % jobc;
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
	jobv[o_job->jobpos].owner = ownerid;
	head->futex_newjobs--;

	// we're done filing all the paperwork to have ownership over this job. thus no more need to have
	// the ownership logic locked for other thread. We can continue to do this job.
	telemetry_workr_accepted(ownerid, o_job->jobpos);
	pthread_mutex_unlock(&head->jobaccept);

	// if we're here that means we've accepted the job at jobv[self->jobpos] and we've
	// claimed it so other workers won't bother this job.
	return 0;
}
void    edbs_jobclose(edbs_job_t job) {

	// save some pointers to the stack for easier access.
	const edbs_handle_t *shm = job.shm;
	edbs_shmhead_t *const head = shm->head;
	const uint64_t jobc = head->jobc;
	edbs_shmjob_t *const jobv = &shm->jobv[job.jobpos];

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
	jobv->jobdesc = 0;
	jobv->owner   = 0;
	head->futex_emptyjobs++;

	// close the transfer if it hasn't already.
	// note that per edb_jobclose's documentation on threading, edb_jobclose and edb_jobopen must
	// only be called inside the comfort of the jobinstall mutex.
	edb_jobclose(jobv);

	pthread_mutex_unlock(&head->jobinstall);

	// send out a broadcast letting at least 1 waiting handler know theres another empty job
	int err = syscall(SYS_futex, &head->futex_emptyjobs, FUTEX_WAKE, 1, 0, 0, 0);
	if(err == -1) {
		log_critf("failed to wake futex_emptyjobs: %d", errno);
	}
}