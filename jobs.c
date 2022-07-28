#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "jobs.h"
#include "errors.h"

// todo: really need to test this function.
int edb_jobwrite(edb_job_t *job, void *transferbuf, const void *buff, int count) {
	// we must block until we know we have room. So block if we are capacity.
	int err = syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAIT, job->transferbuffcapacity, 0, 0, 0);
	if(err == -1 && errno != EAGAIN) {
		log_critf("failed to listen to futex on buffer");
		return -1;
	}

	// we have room. now dump in as much buff as we can until the capacity is full.

	// we'll lock up the buffer so that reads and writes don't happen at the same time.
	pthread_mutex_lock(&job->bufmutex);

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

	// we must wait if there nothing in the buffer.
	int err = syscall(SYS_futex, &job->futex_transferbuffc, FUTEX_WAIT, 0, 0, 0, 0);
	if(err == -1 && errno != EAGAIN) {
		log_critf("failed to listen to futex on buffer");
		return -1;
	}

	// the buffer has stuff in it.

	// we'll lock up the buffer so that reads and writes don't happen at the same time.
	pthread_mutex_lock(&job->bufmutex);

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