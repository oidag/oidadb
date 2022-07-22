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

#include "host.h"
#include "file.h"
#include "errors.h"
#include "worker.h"
#include "include/ellemdb.h"

enum hoststate {
	HOST_CLOSED,
	HOST_CLOSING,
	HOST_OPEN,
	HOST_OPENING,
	HOST_FAILED,
};

typedef struct edb_host_st {

	enum hoststate state;
	pthread_mutex_t statechange;


	edb_file_t       file;
	edb_hostconfig_t config;

	// static memory
	char     shm_name[32];
	void    *alloc_static;
	uint64_t alloc_static_size;

	// job buffer
	off_t      jobc;
	edb_job_t *jobv; // in alloc_static

	// event buffer
	off_t        eventc;
	edb_event_t *eventv; // in alloc_static

	// workers
	unsigned int  workerc;
	edb_worker_t *workerv; // in alloc_static

	// page buffer
	off_t pagec_max;
	off_t pagec;
	// todo: hmm... pages will be different sizes.

} edb_host_t;

// helper function for edb_host to Check for EDB_EINVALs
static edb_err _validatehostops(const char *path, edb_host_t hostops) {
	if(path == 0) {
		log_errorf("path is null");
		return EDB_EINVAL;
	}
	if(hostops.job_buffersize <= 0 || hostops.job_buffersize % sizeof(edb_job_t) != 0) {
		log_errorf("job buffer is either <=0 or not a multiple of edb_job_t");
		return EDB_EINVAL;
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

// closes the host. syncs and closes all descriptors and buffers.
//
// all errors that can occour will be critical in nature.
//
// if waitjobs is not 0, then it will wait until the current job buffer is finished before
// starting the shutdown but will not accept any more jobs.
static void hostclose(edb_host_t *host) {

	pthread_mutex_lock(host->statechange);
	if (host->state == HOST_CLOSED || host->state == HOST_CLOSING) {
		pthread_mutex_unlock(host->statechange);
		log_infof("attempted to close host that's already been marked for closing")
		return;
	}

	host->state = HOST_CLOSING;
	log_infof("closing %s...", host->file.path);

	// stop all the workers.
	for(int i = 0; i < host->workerc; i++) {
		edb_workerstop(&(host->workerv[i]));
	}
	for(int i = 0; i < host->workerc; i++) {
		edb_workerjoin(&(host->workerv[i]));
		edb_workerdecom(&(host->workerv[i]));
	}

	// unallocate the worker space
	free(host->workerv);

	// todo: deallocating the page buffer here.

	// unallocate the shared memory
	deleteshm(host);

	// close the file
	edb_fileclose(&(host->file));

	host->state = HOST_CLOSED;

	// unlock the statechange
	pthread_mutex_unlock(host->statechange);

	// this will cause future calls to pthread_mutex_lock to error out
	// but thats okay because the state has already been changed so
	// the subsquent if statements will do their job normally and
	// prevent duplicate states.
	pthread_mutex_destroy(&(host.statechange));
}

// helper function to hostclose and createshm.
// only works after createshm returned successfully.
// this must be called before edb_fileclose(&(host->file))
static void deleteshm(edb_host_t *host) {
	munmap(host->alloc_static,host->alloc_static_size);
	shm_unlink(host->shm_name);
}

// helper function for edb_host
// builds and allocates the static shared memory region.
static edb_err createshm(edb_host_t *host, edb_hostconfig_t hostops) {

	// calculate the size we need upfront.
	host->alloc_static_size = host->config.job_buffersize
	                          + host->config.event_buffersize;

	// run shm_open(3) using the file name schema of opening /EDB_HOST-[pid]
	sprintf(host->shm_name, "/EDB_HOST-%d", getpid());
	int shmfd = shm_open(host->shm_name, O_RDWR | O_CREAT | O_EXCL,
						 0666);
	if (shmfd == -1) {
		// shm_open should have no reason to fail sense we already have the
		// file opened and locked.
		int errnotmp = errno;
		log_critf("shm_open(3) returned errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	int err = ftruncate64(shmfd, host->alloc_static_size);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate(2) returned unexpected errno: %d", errnotmp);
		shm_unlink(host->shm_name)
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	host.alloc_static = mmap(0, host->alloc_static_size,
	                         PROT_READ | PROT_WRITE,
	                         MAP_SHARED,
	                         shmfd,0);

	// sense it is documented in the manual, so long that we have it
	// mmap'd, we can close the descriptor. We'll do that now
	// so we don't have to worry about it later.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (host.alloc_static == 0) {
		int errnotmp = errno;
		shm_unlink(host->shm_name);
		if (errno == ENOMEM) {
			// we handle this one.
			return EDB_ENOMEM;
		}
		// all others are criticals
		errno = errnotmp;
		log_critf("mmap(2) failed to map shared memory: %d", errno)
		return EDB_ECRIT
	}
	// assign pointers
	host->jobv    = (edb_job_t *)(host->alloc_static);
	host->eventv  = (edb_event_t *)(host->jobv + host->config.job_buffersize);
	// assign counts
	host->jobc    = host->config.job_buffersize / sizeof(edb_job_t);
	host->eventc  = host->config.event_buffersize / sizeof (edb_event_t);
	return 0;
}

edb_err edb_host(const char *path, edb_hostconfig_t hostops) {

	// check for EINVAL
	edb_err eerr;
	eerr = _validatehostops(path, hostops);
	if(eerr) return eerr;

	// start building out our host.
	edb_host_t host = {0};
	host.config = hostops;
	int err = pthread_mutex_init(&host.statechange);
	host.state = HOST_OPENING; // technically a useless state, but oh well.
	if(err) {
		log_critf("failed to initialize state change mutex: %d", err);
		return EDB_ECRIT
	}

	// past this point we need pthread_mutex_destroy(&(host.statechange));

	// open and lock the file
	eerr = edb_fileopen(&(host.file), path, hostops.flags);
	if(eerr) return eerr;

	// past this point, if we get an error we must close the file via edb_fileclose + pthread_mutex_destroy(&(host.statechange))

	eerr = createshm(&host, hostops);
	if(eerr) {
		edb_fileclose(&(host.file));
		pthread_mutex_destroy(&(host.statechange));
		return eerr;
	}

	// past this point, we must deleteshm + edb_fileclose + pthread_mutex_destroy(&(host.statechange))

	// page buffers
	// TODO: I probably need to start working on the jobs to fully understand what needs to go here.

	// configure all the workers.
	host.workerc = host.config.worker_poolsize;
	host.workerv = malloc(host.workerc * sizeof(edb_worker_t)); //todo: free
	if(host.workerv == 0) {
		int errnotmp = errno;
		deleteshm(&host);
		edb_fileclose(&(host.file));
		pthread_mutex_destroy(&(host.statechange));
		if (errnotmp == ENOMEM) {
			return EDB_ENOMEM;
		}
		errno = errnotmp;
		log_critf("malloc(3) returned an unexpected error: %d", errno);
		return EDB_ECRIT
	}
	for(int i = 0; i < host.workerc; i++) {
		eerr = edb_workerinit(&host, &(host.workerv[i]));
		if(eerr) {
			// shit.
			// okay we have to roll back through the ones we already initialized.
			for(int j = i-1; j >= 0; j--) {
				edb_workerdecom(&(host.workerv[j]));
			}
			free(host.workerv);
			deleteshm(&host);
			edb_fileclose(&(host.file));
			pthread_mutex_destroy(&(host.statechange));
			return eerr;
		}
	}

	// from this point on, all we need to do is edb_hostclose to fully clean up everything.

	// at this point we have an open state.
	// this by definition means clients can start filling up the job buffer even
	// though there's no workers to satsify the jobs yet...
	host.state = HOST_OPEN;

	// enter hosting cycle
	// start all the async workers. Note we do int i = 1 because the first one
	// will be the sync worker.
	for(int i = 1; i < host->workerc; i++) {
		eerr = edb_workerasync(&(host->workerv[i]));
		if(eerr) {
			hostclose(&host);
			return eerr;
		}
	}
	// now start the sync worker and let the magic happen.
	eerr = edb_workersync(&(host.workerv[0]));
	if(eerr) {
		hostclose(&host);
		return eerr;
	}

	// at this point, someone had called edb_hoststop.
	// this inherantly means that hostclose() was called.
	// so there's really nothing for us to do but to return a successful
	// exeuction.
	hostclose(&host);
	return
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
	fd = open(params.path,O_RDONLY);
	if (fd == -1) {
		return EDB_EERRNO;
	}
	// read any fcntl locks
	dblock = {
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