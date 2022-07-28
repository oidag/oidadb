#include <pthread.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>

#include "sharedmem.h"
#include "ellemdb.h"
#include "errors.h"

static edb_err installjob(edb_job_t) {
	edb_shm_t *shm = 0; // todo: get shared memeory
	edb_shmhead_t *head = shm->head;
	int err;

	// lock job install
	// todo: better doc
	pthread_mutex_lock(&head->jobinstall);

	// wait for empty jobs if there are none
	// todo: better doc
	err = syscall(SYS_futex, &head->futex_emptyjobs, FUTEX_WAIT, 0, 0, 0, 0);
	if(err == -1 && errno != EAGAIN) {
		pthread_mutex_unlock(&head->jobinstall);
		log_critf("critical error while waiting on new jobs: %d", errno);
		return EDB_ECRIT;
	}

	head->nextjobid++;

	// todo: install job.

	// unlock job install
	// todo: betterdoc

	head->futex_emptyjobs--;
	pthread_mutex_unlock(&shm->head->jobinstall);

	// todo: do whatever is needed to update handle for the newly inserted job.


	// broadcast to the workers that a new job was just installed.
	// send out a broadcast letting at least 1 waiting handler know theres another empty job
	head->futex_newjobs++;
	err = syscall(SYS_futex, &head->futex_emptyjobs, FUTEX_WAKE, 1, 0, 0, 0);
	if(err == -1) {
		log_critf("failed to wake futex_emptyjobs: %d", errno);
	}

}