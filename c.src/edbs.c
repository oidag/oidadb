#define _GNU_SOURCE
#include "edbs_u.h"
#include "wrappers.h"

#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <malloc.h>
#include <linux/futex.h>
#include <sys/syscall.h>

// helper function to hostclose, createshm, edb_host_shmunlink, edb_host_shmlink
//
//
// when calling from host: only works after createshm returned successfully.
// this must be called before edb_fileclose(&(host->file))
void deleteshm(edbs_handle_t *h, int ishost) {
#ifdef EDB_FUCKUPS
	if(h->shm == 0) {
		log_critf("attempting to delete shared memory that is not linked / initialized. prepare for errors.");
	}
#endif

	// the host has extra steps.
	if (ishost) {
		if(h->head->futex_status != EDBS_SSTOPPED) {
			log_errorf("call to edbs_host_free without first calling "
					   "edbs_host_close may cause race conditions in "
					   "user-mutlithreaded enviroments. Calling implicitly");
			edbs_host_close(h);
		}

		// kill the mutex of all the jobs as we know no more can be installed.
		for(int i = 0; i < h->head->jobc; i++) {
			pthread_mutex_destroy(&h->jobv[i].pipemutex);
		}
	}
	uint32_t remaining = __sync_sub_and_fetch(&h->head->connected, 1);
	if(!remaining) {

		// "Last man out hit the lights"

		// we're the last one to disconnect, clean up all the
		// shm-malloc-specific data. This includes any shm mutexes.
		pthread_mutex_destroy(&h->head->jobmutex);
	}
	munmap(h->shm, h->head->shmc);
	shm_unlink(h->shm_name);
	h->shm = 0;
	free(h);
}

void edbs_host_free(edbs_handle_t *shm) {
	return deleteshm(shm, 1);
}

// helper function for edb_host
// builds, allocates, and initializes the static shared memory region.
//
//
odb_err edbs_host_init(edbs_handle_t **o_shm, struct odb_hostconfig config) {

	odb_err eerr;

	// initialize a head on the stack to be copied into the shared memeory
	edbs_shmhead_t stackhead = {0};
	stackhead.magnum = EDB_SHM_MAGIC_NUM;

	// initialize the head with counts and offsets.
	{
		stackhead.jobc   = config.job_buffq;
		stackhead.eventc = config.event_bufferq;
		stackhead.jobtransc = config.job_transfersize * stackhead.jobc;

		// we need to make sure that the transfer buffer gets place on a fresh page.
		// so lets start by getting the size of the first page(s) and round up.
		uint64_t p1 = sizeof (edbs_shmhead_t)
		              + config.job_buffq * sizeof (edbs_shmjob_t);
		              + config.event_bufferq * sizeof (struct odb_event);
		unsigned int p1padding = p1 % sysconf(_SC_PAGE_SIZE);
		stackhead.shmc = p1 + p1padding + stackhead.jobtransc;

		// offsets
		stackhead.joboff      = sizeof(edbs_shmhead_t);
		stackhead.eventoff    = sizeof (edbs_shmhead_t) + config.job_buffq * sizeof (edbs_shmjob_t);
		stackhead.jobtransoff = p1 + p1padding;
	}

	// perform the malloc
	edbs_handle_t *shm = malloc(sizeof(edbs_handle_t));
	if(shm == 0) {
		if(errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		log_critf("malloc");
		return ODB_ECRIT;
	}
	*o_shm = shm;


	// run shm_open(3) using the file name schema of opening /EDB_HOST-[pid]
	shmname(getpid(), shm->shm_name);
	int shmfd = shm_open(shm->shm_name, O_RDWR | O_CREAT | O_EXCL,
	                     0666);
	if (shmfd == -1) {
		// shm_open should have no reason to fail sense we already have the
		// file opened and locked.
		if(errno == EEXIST) {
			free(shm);
			log_errorf("shared object already exists (ungraceful shutdown?)");
			return ODB_EEXIST;
		}
		free(shm);
		log_critf("shm_open(3) returned errno: %d", errno);
		return ODB_ECRIT;
	}
	int err = ftruncate64(shmfd, (int64_t)stackhead.shmc);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate(2) returned unexpected errno: %d", errnotmp);
		close(shmfd);
		errno = errnotmp;
		eerr = ODB_ECRIT;
		goto clean_shm;
	}
	shm->shm = mmap(0, stackhead.shmc,
	                 PROT_READ | PROT_WRITE,
	                MAP_SHARED,
	                shmfd, 0);
	// sense it is documented in the manual, so long that we have it
	// mmap'd, we can close the descriptor. We'll do that now
	// so we don't have to worry about it later.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (shm->shm == MAP_FAILED) {
		int errnotmp = errno;
		if (errno == ENOMEM) {
			// we handle this one.
			eerr= ODB_ENOMEM;
		} else {
			// all others are criticals
			errno = errnotmp;
			log_critf("mmap(2) failed to map shared memory: %d", errno);
			eerr= ODB_ECRIT;
		}
		goto clean_shm;
	}

	// initialize the memory to all 0s.
	bzero(shm->shm, stackhead.shmc);

	// install the stackhead into the shared memory.
	shm->head = shm->shm;
	*shm->head = stackhead;

	// past this point, do not use stackhead. use host->head.

	// initialize mutexes.
	pthread_mutexattr_t mutexattr;
	if((err = pthread_mutexattr_init(&mutexattr))) {
		if (err == ENOMEM) eerr = ODB_ENOMEM;
		else eerr = ODB_ECRIT;
		log_critf("pthread_mutexattr_init(3): %d", err);
		goto clean_map;
	}
	pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
	if((err = pthread_mutex_init(&shm->head->jobmutex, &mutexattr))) {
		if (err == ENOMEM) eerr = ODB_ENOMEM;
		else eerr = ODB_ECRIT;
		log_critf("pthread_mutex_init(3): %d", err);
		goto clean_mutex;
	}


	// initialize the head with expected values
	shm->head->newjobs   = 0;
	shm->head->futex_status    = EDBS_SSTARTING;
	shm->head->emptyjobs = shm->head->jobc;

	// assign buffer pointers
	shm->jobv        = shm->shm + shm->head->joboff;
	shm->eventv      = shm->shm + shm->head->eventoff;
	shm->transbuffer = shm->shm + shm->head->jobtransoff;

	// initialize job buffer
	for(int i = 0; i < shm->head->jobc; i++) {
		shm->jobv[i].transferbuffoff        = config.job_transfersize * i;
		shm->jobv[i].transferbuffcapacity   = config.job_transfersize;
		// make sure were cannonically correct
		shm->jobv[i].transferbuff_FLAGS = EDBS_JEXECUTERTERM;
		if((err = pthread_mutex_init(&shm->jobv[i].pipemutex, &mutexattr))) {
			log_critf("pthread_mutex_init(3): %d", err);
			goto clean_mutex;
		}
	}
	pthread_mutexattr_destroy(&mutexattr);

	// initialize event buffer
	// todo

	// send out the futex saying we're ready to accept jobs
	shm->head->futex_status = EDBS_SRUNNING;
	futex_wake(&shm->head->futex_status, INT32_MAX);

	// increment the connection count sense we're technically a handle on the
	// shm.
	__sync_add_and_fetch(&shm->head->connected, 1);

	// finally, before returning as the host, we must make sure to enable selectorhold
	// sense its conditions have been met.
	shm->head->futex_selectorhold = 1;

	return 0;
	// everything past here are bail-outs.

	clean_mutex:
	for(int i = 0; i < shm->head->jobc; i++) pthread_mutex_destroy(&shm->jobv[i].pipemutex);
	pthread_mutex_destroy(&shm->head->jobmutex);
	pthread_mutexattr_destroy(&mutexattr);
	shm->head->futex_status = EDBS_SSTOPPED;
	futex_wake(&shm->head->futex_status, INT32_MAX);

	clean_map:
	munmap(shm->shm, shm->head->shmc);

	clean_shm:
	shm_unlink(shm->shm_name);
	shm->shm = 0;
	free(shm);

	return eerr;
}

void    edbs_host_close(edbs_handle_t *h) {
	// set to be in stopping mode. This will prevent any further jobs
	// from being installed.
	pthread_mutex_lock(&h->head->jobmutex);
	h->head->futex_status = EDBS_SSTOPPED;

	// sense we changed our futex_status, we must broadcast this to the
	// hold futexes.
	h->head->futex_selectorhold = 0;
	h->head->futex_installerhold = 0;
	futex_wake(&h->head->futex_selectorhold, INT32_MAX);
	futex_wake(&h->head->futex_installerhold, INT32_MAX);
	pthread_mutex_unlock(&h->head->jobmutex);

	if(h->head->futex_hasjobs) {
		// we use ~%d because we don't have a mutex lock so the number
		// could have changed sense this log would have got to the front.
		log_infof("haulting transfer buffer free: ~%ld jobs still left",
		          h->head->jobc - h->head->emptyjobs);
	}

	// wait for all jobs to be closed.
	futex_wait(&h->head->futex_hasjobs, 1);
}

int edbs_host_closed(const edbs_handle_t *h) {
	return h->head->futex_status == EDBS_SSTOPPED;
}

void edbs_handle_free(edbs_handle_t *shm) {
	return deleteshm(shm, 0);
}

odb_err edbs_handle_init(edbs_handle_t **o_shm, pid_t hostpid) {
	edbs_handle_t *shm = malloc(sizeof (edbs_handle_t ));
	if(shm == 0) {
		if(errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		log_critf("malloc");
		return ODB_ECRIT;
	}
	*o_shm = shm;

	shmname(hostpid, shm->shm_name);
	int shmfd = shm_open(shm->shm_name, O_RDWR,
	                     0666);
	if(shmfd == -1) {
		// probably a bad pid or file is not hosted?
		if(errno == ENOENT) {
			free(shm);
			return ODB_ENOHOST;
		}
		free(shm);
		log_critf("shm_open(3) returned errno: %d", errno);
		return ODB_EERRNO;
	}

	// Lets make sure we confirm the magic number safely before we can trust
	// casting edbs_shmhead_t.
	struct stat st;
	int err = fstat(shmfd,  &st);
	if(err == -1) {
		// no reason fstat should fail...
		int errnotmp = errno;
		log_critf("ftruncate64(2) returned unexpected errno: %d", errnotmp);
		shm_unlink(shm->shm_name);
		close(shmfd);
		errno = errnotmp;
		free(shm);
		return ODB_ECRIT;
	}
	if(st.st_size < sizeof(edbs_shmhead_t)) {
		log_noticef("hosted shm block not large enough to contain headers");
		shm_unlink(shm->shm_name);
		close(shmfd);
		free(shm);
		return ODB_ENOTDB;
	}

	// Load the shared memeory
	// get all the counts by reading in just the head
	edbs_shmhead_t tmphead;
	ssize_t n = read(shmfd, &tmphead, sizeof (edbs_shmhead_t));
	if (n == -1) {
		// no reason read should fail...
		log_critf("read(2) on shared memory returned unexpected errno");
		shm_unlink(shm->shm_name);
		close(shmfd);
		free(shm);
		return ODB_ECRIT;
	}
	if (tmphead.magnum != EDB_SHM_MAGIC_NUM) {
		// failed to read in the shared memeory head.
		shm_unlink(shm->shm_name);
		close(shmfd);
		log_noticef("shared memory does not contain magic number: expecting %lx, got %lx",
		            EDB_SHM_MAGIC_NUM,
		            tmphead.magnum);
		free(shm);
		return ODB_ENOTDB;
	}
	// we trust this shm to be the right size at this point: map it
	shm->shm = mmap(0, tmphead.shmc,
	                   PROT_READ | PROT_WRITE,
	                MAP_SHARED,
	                shmfd, 0);
	shm->head = shm->shm;


	// see the comment in createshm regarding the closing of the fd.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (shm->shm == MAP_FAILED) {
		// we cannot handle nomem because the memory should have already
		// been allocated by the host.
		int errnotmp = errno;
		shm_unlink(shm->shm_name);
		errno = errnotmp;
		log_critf("mmap(2) failed to map shared memory: %d", errno);
		free(shm);
		return ODB_ECRIT;
	}

	// wait if the host is still setting up
	futex_wait(&shm->head->futex_status, EDBS_SSTARTING);

	// make sure it hasn't shut down
	if(shm->head->futex_status == EDBS_SSTOPPED) {
		// this is a weird edge case if we're in here. But basically the host
		// had shut down while we were trying to connect to it.
		munmap(shm->shm, shm->head->shmc);
		shm_unlink(shm->shm_name);
		free(shm);
		return ODB_ENOHOST;
	}

	// assign buffer pointers
	shm->jobv        = shm->shm + shm->head->joboff;
	shm->eventv      = shm->shm + shm->head->eventoff;
	shm->transbuffer = shm->shm + shm->head->jobtransoff;

	// include our presence.
	uint32_t lastcount = __sync_fetch_and_add(&shm->head->connected, 1);
	if(!lastcount) {
		// yet another weird edge case. We just connected to a shm with no
		// other memebers on it, this means know the shm has been dismantled
		// between this if statement and the futex_status check. It can
		// theoretically happen.
		munmap(shm->shm, shm->head->shmc);
		shm_unlink(shm->shm_name);
		free(shm);
		return ODB_ENOHOST;
	}

	return 0;
}