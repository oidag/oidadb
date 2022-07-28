#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "host.h"
#include "file.h"
#include "errors.h"
#include "worker.h"
#include "jobs.h"
#include "include/ellemdb.h"



// helper function for edb_host to Check for EDB_EINVALs
static edb_err _validatehostops(const char *path, edb_hostconfig_t hostops) {
	if(path == 0) {
		log_errorf("path is null");
		return EDB_EINVAL;
	}
	if(hostops.job_buffersize <= 0 || hostops.job_buffersize % sizeof(edb_job_t) != 0) {
		log_errorf("job buffer is either <=0 or not a multiple of edb_job_t");
		return EDB_EINVAL;
	}
	if(hostops.job_transfersize == 0) {
		log_errorf("transfer buffer cannot be 0");
		return  EDB_EINVAL;
	}
	if(hostops.job_transfersize % sysconf(_SC_PAGE_SIZE) != 0) {
		log_noticef("transfer buffer is not an multiple of system page size (%ld), this may hinder performance", sysconf(_SC_PAGE_SIZE));
	}
	if(hostops.event_buffersize <= 0 || hostops.event_buffersize % sizeof(edb_event_t) != 0) {
		log_errorf("event buffer is either <=0 or not a multiple of edb_event_t");
		return EDB_EINVAL;
	}
	if(hostops.worker_poolsize == 0) {
		log_errorf("worker_poolsize is 0 or not a multiple of edb_event_t");
		return EDB_EINVAL;
	}
	if(hostops.page_buffermax % sysconf(_SC_PAGE_SIZE) != 0) {
		log_errorf("page buffer maximum is not a multiple of system page size");
		return  EDB_EINVAL;
	}
	return 0;
}

// helper function to hostclose, createshm, edb_host_shmunlink, edb_host_shmlink
//
//
// when calling from host: only works after createshm returned successfully.
// this must be called before edb_fileclose(&(host->file))
static void deleteshm(edb_shm_t *host, int destroymutex) {
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

// closes the host. syncs and closes all descriptors and buffers.
//
// all errors that can occour will be critical in nature.
//
// if waitjobs is not 0, then it will wait until the current job buffer is finished before
// starting the shutdown but will not accept any more jobs.
static void hostclose(edb_host_t *host) {
	if (host->state == HOST_NONE) {
		log_errorf("tried to close a host that has not been started");
		return;
	}

	// wait until we know that the host is booted up
	int err = pthread_mutex_lock(&host->bootup);
	if (err) {
		log_critf("failed to obtain host bootup mutex: %d", err);
		return;
	}

	// okay we know its booted up. Go ahead and release the return mutex.
	err = pthread_mutex_unlock(&host->retlock);
	if (err) {
		log_critf("failed to release retlock mutex: %d", err);
		return;
	}

	// and that's it. the edb_host function will clean up the rest.

	pthread_mutex_unlock(&host->bootup);
}


// generates the file name for the shared memory
// buff should be at least 32bytes.
static inline shmname(pid_t pid, char *buff) {
	sprintf(buff, "/EDB_HOST-%d", pid);
}

// helper function for edb_host
// builds, allocates, and initializes the static shared memory region.
//
//
static edb_err createshm(edb_shm_t *host, edb_hostconfig_t config) {

	edb_err eerr;

	// initialize a head on the stack to be later copied into the shared memeory
	edb_shmhead_t stackhead = {0};
	stackhead.magnum = EDB_SHM_MAGIC_NUM;

	// initialize the head with counts and offsets.
	{
		stackhead.jobc   = config.job_buffersize / sizeof (edb_job_t);
		stackhead.eventc = config.event_buffersize / sizeof (edb_event_t);
		stackhead.jobtransc = config.job_transfersize * stackhead.jobc;

		// we need to make sure that the transfer buffer gets place on a fresh page.
		// so lets start by getting the size of the first page(s) and round up.
		uint64_t p1 = sizeof (edb_shmhead_t)
		              + config.job_buffersize
		              + config.event_buffersize;
		unsigned int p1padding = p1 % sysconf(_SC_PAGE_SIZE);
		stackhead.shmc = p1 + p1padding + stackhead.jobtransc;

		// offsets
		stackhead.joboff      = sizeof(edb_shmhead_t);
		stackhead.eventoff    = sizeof (edb_shmhead_t) + config.job_buffersize;
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

edb_err edb_host(const char *path, edb_hostconfig_t hostops) {

	// check for EINVAL
	edb_err eerr = 0;
	int preserved_errno = 0;
	eerr = _validatehostops(path, hostops);
	if(eerr) return eerr;

	// start building out our host.
	edb_host_t host = {0};
	host.config = hostops;
	int err = pthread_mutex_init(&host.bootup, 0);
	if(err) {
		log_critf("failed to initialize state change mutex: %d", err);
		return EDB_ECRIT;
	}
	err = pthread_mutex_lock(&host.bootup);
	if(err) {
		log_critf("failed to lock bootup mutex: %d", err);
		pthread_mutex_destroy(&host.bootup);
		return EDB_ECRIT;
	}

	// we are now in the opening state: we have work to do but we can be hostclose
	// will work even before we're done
	host.state = HOST_OPENING;

	err = pthread_mutex_init(&host.retlock, 0);
	if(err) {
		log_critf("failed to initialize state change mutex: %d", err);
		pthread_mutex_destroy(&host.bootup);
		return EDB_ECRIT;
	}


	// past this point we need pthread_mutex_destroy(&(host.bootup));

	// open and lock the file
	eerr = edb_fileopen(&(host.file), path, hostops.flags);
	if(eerr) {
		goto clean_mutex;
	}

	// past this point, if we get an error we must close the file via edb_fileclose + pthread_mutex_destroy(&(host.bootup))

	eerr = createshm(&host.shm, hostops);
	if(eerr) {
		goto clean_file;
	}

	// past this point, we must deleteshm + edb_fileclose + pthread_mutex_destroy(&(host.bootup))

	// page buffers
	// TODO: I probably need to start working on the jobs to fully understand what needs to go here.

	// configure all the workers.
	host.workerc = host.config.worker_poolsize;
	host.workerv = malloc(host.workerc * sizeof(edb_worker_t)); //todo: free
	if(host.workerv == 0) {
		if (errno == ENOMEM) {
			eerr = EDB_ENOMEM;
		} else {
			preserved_errno = errno;
			log_critf("malloc(3) returned an unexpected error: %d", errno);
			eerr = EDB_ECRIT;
		}
		goto clean_shm;
	}
	for(int i = 0; i < host.workerc; i++) {
		eerr = edb_workerinit(&host, i+1, &(host.workerv[i]));
		if(eerr) {
			// shit.
			// okay we have to roll back through the ones we already initialized.
			for(int j = i-1; j >= 0; j--) {
				edb_workerdecom(&(host.workerv[j]));
			}
			goto clean_freepool;
		}
	}

	// before we start the workers, lets lock the finished
	pthread_mutex_lock(&host.retlock);
	// todo: error handling

	// from this point on, all we need to do is edb_hostclose to fully clean up everything.

	// at this point we have an open state.
	// this by definition means clients can start filling up the job buffer even
	// though there's no workers to satsify the jobs yet.
	host.state = HOST_OPEN;
	pthread_mutex_unlock(&host.bootup);
	// todo: error handling

	// enter hosting cycle
	// start all the async workers. Note we do int i = 1 because the first one
	// will be the sync worker.
	for(int i = 0; i < host.workerc; i++) {
		eerr = edb_workerasync(&(host.workerv[i]));
		if(eerr) {
			goto clean_stopwork;
		}
	}

	// double-lock the retlock. this will freeze this calling thread until
	// the closer is called to unlock us.
	pthread_mutex_lock(&host.retlock);

	log_infof("closing %s...", host.file.path);

	// clean up everything
	// stop all the workers.
	clean_stopwork:
	pthread_mutex_unlock(&host.retlock);
	log_infof("stopping %d workers...", host.workerc);
	for(int i = 0; i < host.workerc; i++) {
		edb_workerstop(&(host.workerv[i]));
	}
	for(int i = 0; i < host.workerc; i++) {
		edb_workerjoin(&(host.workerv[i]));
		edb_workerdecom(&(host.workerv[i]));
		log_infof("  ...worker %d joined", i);
	}

	// unallocate the worker space
	clean_freepool:
	log_infof("freeing worker pool...");
	free(host.workerv);

	// todo: deallocating the page buffer here.

	// unallocate the shared memory
	clean_shm:
	log_infof("deleting shared memory...");
	deleteshm(&host.shm, 1);

	// close the file
	clean_file:
	log_infof("closing database file...");
	edb_fileclose(&(host.file));

	clean_mutex:
	log_infof("destroying mutexes...");
	pthread_mutex_destroy(&host.retlock);
	pthread_mutex_destroy(&host.bootup);

	if(preserved_errno) {
		errno = preserved_errno;
	}
	return eerr;
}

// must NOT be called from a worker thread.
edb_err edb_hoststop(const char *path) {
	pid_t pid;
	edb_err eerr = edb_host_getpid(path, &pid);

	// todo: need to send an instrunction to the host... and some how the workers need
	//       to call hostclose?... hmmm see this functions comment though...
}

edb_err edb_host_getpid(const char *path, pid_t *outpid) {

	int err = 0;
	int fd = 0;
	struct flock dblock = {0};

	// open the file for the only purpose of reading locks.
	fd = open(path,O_RDONLY);
	if (fd == -1) {
		return EDB_EERRNO;
	}
	// read any fcntl locks
	dblock = (struct flock){
			.l_type = F_WRLCK,
			.l_whence = SEEK_SET,
			.l_start = 0,
			.l_len = 0,
			.l_pid = 0,
	};
	err = fcntl(fd, F_OFD_GETLK, &dblock);
	if(err == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("fcntl(2) returned unexpected errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	// we can close the file now sense we got all the info we needed.
	close(fd);
	// analyze the results of the lock.
	if(dblock.l_type == F_UNLCK) {
		// no host connected to this file.
		return EDB_ENOHOST;
	}
	// host successfully found.
	*outpid = dblock.l_pid;
	return 0;
}

void edb_host_shmunlink(edb_shm_t *outptr) {
	return deleteshm(outptr, 0);
}

edb_err edb_host_shmlink(edb_shm_t *outptr, pid_t hostpid) {
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