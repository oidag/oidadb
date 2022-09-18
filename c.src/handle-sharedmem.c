#include <pthread.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>

#include "edbs.h"
#include "ellemdb.h"
#include "errors.h"
#include "edbs-jobs.h"

// todo: the args need to be class, command, and edb_obj
/*
static edb_err installjob(edb_job_t job) {
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

	// todo: make sure to reset the job buffer:
	//       but only do this if we're NOT deleting
	edb_jobreset(&jobv[self->jobpos]);

	// todo: install job (do NOT write to buffer, we don't need to do that this mutex)

	// unlock job install
	// todo: betterdoc

	head->futex_emptyjobs--;
	pthread_mutex_unlock(&shm->head->jobinstall);

	// todo: do whatever is needed to update handle for the newly inserted job.

	// todo: do whatever is needed for the jobbuffer either edb_jobread or edb_jobwrite

	// we must lock job accepts so a worker does not try to accept a job mid-way through us installing it.
	//
	// (this can probably be done a bit more nicely if the jobaccept mutex was held per-job slot)
	pthread_mutex_lock(&head->jobaccept);
	head->futex_newjobs++;
	pthread_mutex_unlock(&head->jobaccept);

	// broadcast to the workers that a new job was just installed.
	// send out a broadcast letting at least 1 waiting handler know theres another empty job
	err = syscall(SYS_futex, &head->futex_newjobs, FUTEX_WAKE, 1, 0, 0, 0);
	if(err == -1) {
		log_critf("failed to wake futex_emptyjobs: %d", errno);
	}

}*/