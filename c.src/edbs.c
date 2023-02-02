#define _GNU_SOURCE
#include "edbs.h"

#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>


// generates the file name for the shared memory
// buff should be at least 32bytes.
void static inline shmname(pid_t pid, char *buff) {
	sprintf(buff, "/EDB_HOST-%d", pid);
}

// helper function to hostclose, createshm, edb_host_shmunlink, edb_host_shmlink
//
//
// when calling from host: only works after createshm returned successfully.
// this must be called before edb_fileclose(&(host->file))
void deleteshm(edb_shm_t *host, int destroymutex) {
	if(host->shm == 0) {
		log_critf("attempting to delete shared memory that is not linked / initialized. prepare for errors.");
	}
	if (destroymutex) {
		pthread_mutex_destroy(&host->head->jobinstall);
		pthread_mutex_destroy(&host->head->jobaccept);
		for(int i = 0; i < host->head->jobc; i++) {
			pthread_mutex_destroy(&host->jobv[i].bufmutex);
		}
	}
	munmap(host->shm, host->head->shmc);
	shm_unlink(host->shm_name);
	host->shm = 0;
}

void edbs_dehost(edb_shm_t *host) {
	return deleteshm(host,1);
}

// helper function for edb_host
// builds, allocates, and initializes the static shared memory region.
//
//
edb_err edbs_host(edb_shm_t *host, odb_hostconfig_t config) {

	edb_err eerr;

	// initialize a head on the stack to be later copied into the shared memeory
	edb_shmhead_t stackhead = {0};
	stackhead.magnum = EDB_SHM_MAGIC_NUM;

	// initialize the head with counts and offsets.
	{
		stackhead.jobc   = config.job_buffq;
		stackhead.eventc = config.event_bufferq;
		stackhead.jobtransc = config.job_transfersize * stackhead.jobc;

		// we need to make sure that the transfer buffer gets place on a fresh page.
		// so lets start by getting the size of the first page(s) and round up.
		uint64_t p1 = sizeof (edb_shmhead_t)
		              + config.job_buffq * sizeof (edb_job_t);
		              + config.event_bufferq * sizeof (edb_event_t);
		unsigned int p1padding = p1 % sysconf(_SC_PAGE_SIZE);
		stackhead.shmc = p1 + p1padding + stackhead.jobtransc;

		// offsets
		stackhead.joboff      = sizeof(edb_shmhead_t);
		stackhead.eventoff    = sizeof (edb_shmhead_t) + config.job_buffq * sizeof (edb_job_t);
		stackhead.jobtransoff = p1 + p1padding;
	}


	// run shm_open(3) using the file name schema of opening /EDB_HOST-[pid]
	shmname(getpid(), host->shm_name);
	int shmfd = shm_open(host->shm_name, O_RDWR | O_CREAT | O_EXCL,
	                     0666);
	if (shmfd == -1) {
		// shm_open should have no reason to fail sense we already have the
		// file opened and locked.
		log_critf("shm_open(3) returned errno: %d", errno);
		return EDB_ECRIT;
	}
	int err = ftruncate64(shmfd, stackhead.shmc);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate(2) returned unexpected errno: %d", errnotmp);
		close(shmfd);
		errno = errnotmp;
		eerr = EDB_ECRIT;
		goto clean_shm;
	}
	host->shm = mmap(0, stackhead.shmc,
	                 PROT_READ | PROT_WRITE,
	                 MAP_SHARED,
	                 shmfd, 0);

	// sense it is documented in the manual, so long that we have it
	// mmap'd, we can close the descriptor. We'll do that now
	// so we don't have to worry about it later.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (host->shm == MAP_FAILED) {
		int errnotmp = errno;
		shm_unlink(host->shm_name);
		if (errno == ENOMEM) {
			// we handle this one.
			eerr= EDB_ECRIT;
		} else {
			// all others are criticals
			errno = errnotmp;
			log_critf("mmap(2) failed to map shared memory: %d", errno);
			eerr= EDB_ECRIT;
		}
		goto clean_shm;
	}

	// initialize the memory to all 0s.
	bzero(host->shm, stackhead.shmc);

	// install the stackhead into the shared memory.
	host->head = host->shm;
	*host->head = stackhead;

	// past this point, do not use stackhead. use host->head.

	// initialize mutexes.
	pthread_mutexattr_t mutexattr;
	if((err = pthread_mutexattr_init(&mutexattr))) {
		if (err == ENOMEM) eerr = EDB_ENOMEM;
		else eerr = EDB_ECRIT;
		log_critf("pthread_mutexattr_init(3): %d", err);
		goto clean_map;
	}
	pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
	if((err = pthread_mutex_init(&host->head->jobaccept, &mutexattr))) {
		if (err == ENOMEM) eerr = EDB_ENOMEM;
		else eerr = EDB_ECRIT;
		log_critf("pthread_mutex_init(3): %d", err);
		goto clean_mutex;
	}
	if((err = pthread_mutex_init(&host->head->jobinstall, &mutexattr))) {
		if (err == ENOMEM) eerr = EDB_ENOMEM;
		else eerr = EDB_ECRIT;
		log_critf("pthread_mutex_init(3): %d", err);
		goto clean_mutex;
	}


	// initialize the futexes in the head
	host->head->futex_newjobs   = 0;
	host->head->futex_emptyjobs = host->head->jobc;

	// assign buffer pointers
	host->jobv        = host->shm + host->head->joboff;
	host->eventv      = host->shm + host->head->eventoff;
	host->transbuffer = host->shm + host->head->jobtransoff;

	// initialize job buffer
	for(int i = 0; i < host->head->jobc; i++) {
		host->jobv[i].transferbuffoff        = config.job_transfersize * i;
		host->jobv[i].transferbuffcapacity   = config.job_transfersize;
		if((err = pthread_mutex_init(&host->jobv[i].bufmutex, &mutexattr))) {
			log_critf("pthread_mutex_init(3): %d", err);
			goto clean_mutex;
		}
	}
	pthread_mutexattr_destroy(&mutexattr);

	// initialize event buffer
	// todo


	return 0;
	// everything past here are bail-outs.

	clean_mutex:
	for(int i = 0; i < host->head->jobc; i++) pthread_mutex_destroy(&host->jobv[i].bufmutex);
	pthread_mutex_destroy(&host->head->jobinstall);
	pthread_mutex_destroy(&host->head->jobaccept);
	pthread_mutexattr_destroy(&mutexattr);

	clean_map:
	munmap(host->shm, host->head->shmc);

	clean_shm:
	shm_unlink(host->shm_name);
	host->shm = 0;

	return eerr;
}

void edbs_unhandle(edb_shm_t *outptr) {
	return deleteshm(outptr, 0);
}

edb_err edbs_handle(edb_shm_t *outptr, pid_t hostpid) {
	shmname(hostpid, outptr->shm_name);
	int shmfd = shm_open(outptr->shm_name, O_RDWR,
	                     0666);
	if(shmfd == -1) {
		// probably a bad pid or file is not hosted?
		int errnotmp = errno;
		if(errno == ENOENT) {
			// no host probably.
			errno = errnotmp;
			return EDB_ENOHOST;
		}
		log_critf("shm_open(3) returned errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_EERRNO;
	}

	// truncate it enough to make sure we have the counts
	int err = ftruncate64(shmfd, sizeof(edb_shmhead_t));
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate64(2) returned unexpected errno: %d", errnotmp);
		shm_unlink(outptr->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// get all the counts
	ssize_t n = read(shmfd, outptr->head, sizeof (edb_shmhead_t));
	if (n == -1) {
		// no reason read should fail...
		int errnotmp = errno;
		log_critf("read(2) on shared memory returned unexpected errno: %d", errnotmp);
		shm_unlink(outptr->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	if (outptr->head->magnum != EDB_SHM_MAGIC_NUM) {
		// failed to read in the shared memeory head.
		shm_unlink(outptr->shm_name);
		close(shmfd);
		log_noticef("shared memory does not contain magic number: expecting %lx, got %lx",
		            EDB_SHM_MAGIC_NUM,
		            outptr->head->magnum);
		return EDB_ENOTDB;
	}

	// now retruncate with the full size now that we know what it is.
	err = ftruncate64(shmfd, outptr->head->shmc);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate64(2) returned unexpected errno while truncating shm to %ld: %d", outptr->head->shmc, errnotmp);
		shm_unlink(outptr->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// map it
	outptr->shm = mmap(0, outptr->head->shmc,
	                   PROT_READ | PROT_WRITE,
	                   MAP_SHARED,
	                   shmfd, 0);
	outptr->head = outptr->shm;


	// see the comment in createshm regarding the closing of the fd.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (outptr->shm == MAP_FAILED) {
		// we cannot handle nomem because the memory should have already
		// been allocated by the host.
		int errnotmp = errno;
		shm_unlink(outptr->shm_name);
		errno = errnotmp;
		log_critf("mmap(2) failed to map shared memory: %d", errno);
		return EDB_ECRIT;
	}

	// assign buffer pointers
	outptr->jobv        = outptr->shm + outptr->head->joboff;
	outptr->eventv      = outptr->shm + outptr->head->eventoff;
	outptr->transbuffer = outptr->shm + outptr->head->jobtransoff;

	return 0;
}