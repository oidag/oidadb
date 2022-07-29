#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "jobs.h"
#include "errors.h"

int edb_jobclose(edb_job_t *job) {
	pthread_mutex_lock(&job->bufmutex);

	if (job->state == 0) {
		pthread_mutex_unlock(&job->bufmutex);
		return 0;
	}

	job->state = 0;
	// if there's anything waiting right now then wake them up. They'll preceed to find
	// that the state is closed and do nothing.
	syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAKE, 16, 0, 0, 0);
	pthread_mutex_unlock(&job->bufmutex);
}
int edb_jobreset(edb_job_t *job) {
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
int edb_jobwrite(edb_job_t *job, void *transferbuf, const void *buff, int count) {

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


int edb_jobread(edb_job_t *job, const void *transferbuf, void *buff, int count) {

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