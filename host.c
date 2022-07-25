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

	edb_shm_t shm;

	// workers
	unsigned int  workerc;
	edb_worker_t *workerv; // in shm

	// page buffer
	off_t pagec_max;
	off_t pagec;
	// todo: hmm... pages will be different sizes.

} edb_host_t;

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
static void deleteshm(edb_shm_t *host) {
	if(host->shm == 0) {
		log_critf("attempting to delete shared memory that is not linked / initialized. prepare for errors.")
	}
	munmap(host->shm, host->shmc);
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

	pthread_mutex_lock(&host->statechange);
	if (host->state == HOST_CLOSED || host->state == HOST_CLOSING) {
		pthread_mutex_unlock(&host->statechange);
		log_infof("attempted to close host that's already been marked for closing");
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
	deleteshm(&host->shm);

	// close the file
	edb_fileclose(&(host->file));

	host->state = HOST_CLOSED;

	// unlock the statechange
	pthread_mutex_unlock(&host->statechange);

	// this will cause future calls to pthread_mutex_lock to error out
	// but thats okay because the state has already been changed so
	// the subsquent if statements will do their job normally and
	// prevent duplicate states.
	pthread_mutex_destroy(&(host->statechange));
}


// generates the file name for the shared memory
// buff should be at least 32bytes.
static inline shmname(pid_t pid, char *buff) {
	sprintf(buff, "/EDB_HOST-%d", pid);
}

// helper function for edb_host
// builds, allocates, and initializes the static shared memory region.
static edb_err createshm(edb_shm_t *host, edb_hostconfig_t config) {

	// calculate the size we need upfront.
	host->shmc = (sizeof (uint64_t) * 4) // all the counts
	              + config.job_buffersize
	              + config.event_buffersize;

	// run shm_open(3) using the file name schema of opening /EDB_HOST-[pid]
	shmname(getpid(), host->shm_name);
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
	int err = ftruncate64(shmfd, host->shmc);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate(2) returned unexpected errno: %d", errnotmp);
		shm_unlink(host->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	host->shm = mmap(0, host->shmc,
	                         PROT_READ | PROT_WRITE,
	                MAP_SHARED,
	                shmfd, 0);

	// sense it is documented in the manual, so long that we have it
	// mmap'd, we can close the descriptor. We'll do that now
	// so we don't have to worry about it later.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (host->shm == 0) {
		int errnotmp = errno;
		shm_unlink(host->shm_name);
		if (errno == ENOMEM) {
			// we handle this one.
			return EDB_ENOMEM;
		}
		// all others are criticals
		errno = errnotmp;
		log_critf("mmap(2) failed to map shared memory: %d", errno);
		return EDB_ECRIT;
	}

	// initialize counts
	bzero(host->shm, host->shmc);
	uint64_t *countstart = (uint64_t *)(host->shm);
	countstart[0] = EDB_SHM_MAGIC_NUM;
	countstart[1] = host->shmc;
	countstart[2] = host->jobc   = config.job_buffersize / sizeof (edb_job_t);
	countstart[3] = host->eventc = config.event_buffersize / sizeof (edb_event_t);

	// assign buffer pointers
	void *bufferstart = (host->shm + sizeof(uint64_t)*4);
	host->jobv     = (edb_job_t *)(bufferstart);
	host->eventv   = (edb_event_t *)(host->jobv + host->jobc*sizeof(edb_job_t));
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
	int err = pthread_mutex_init(&host.statechange, 0);
	host.state = HOST_OPENING; // technically a useless state, but oh well.
	if(err) {
		log_critf("failed to initialize state change mutex: %d", err);
		return EDB_ECRIT;
	}

	// past this point we need pthread_mutex_destroy(&(host.statechange));

	// open and lock the file
	eerr = edb_fileopen(&(host.file), path, hostops.flags);
	if(eerr) return eerr;

	// past this point, if we get an error we must close the file via edb_fileclose + pthread_mutex_destroy(&(host.statechange))

	eerr = createshm(&host.shm, hostops);
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
		deleteshm(&host.shm);
		edb_fileclose(&(host.file));
		pthread_mutex_destroy(&(host.statechange));
		if (errnotmp == ENOMEM) {
			return EDB_ENOMEM;
		}
		errno = errnotmp;
		log_critf("malloc(3) returned an unexpected error: %d", errno);
		return EDB_ECRIT;
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
			deleteshm(&host.shm);
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
	for(int i = 1; i < host.workerc; i++) {
		eerr = edb_workerasync(&(host.workerv[i]));
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
	return 0;
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
	return deleteshm(outptr);
}

edb_err edb_host_shmlink(pid_t hostpid, edb_shm_t *outptr) {
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
	int err = ftruncate64(shmfd, sizeof(uint64_t) * 4);
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
	ssize_t n = read(shmfd, &(outptr->magnum), sizeof (uint64_t) * 4);
	if (n == -1) {
		// no reason read should fail...
		int errnotmp = errno;
		log_critf("read(2) on shared memory returned unexpected errno: %d", errnotmp);
		shm_unlink(outptr->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	if (outptr->magnum != EDB_SHM_MAGIC_NUM) {
		// failed to read in the shared memeory head.
		shm_unlink(outptr->shm_name);
		close(shmfd);
		log_noticef("shared memory does not contain magic number: expecting %lx, got %lx",
					EDB_SHM_MAGIC_NUM,
					outptr->magnum);
		return EDB_ENOTDB;
	}

	// now retruncate with the full size now that we know what it is.
	err = ftruncate64(shmfd, outptr->shmc);
	if(err == -1) {
		// no reason ftruncate should fail...
		int errnotmp = errno;
		log_critf("ftruncate64(2) returned unexpected errno while truncating shm to %ld: %d", outptr->shmc, errnotmp);
		shm_unlink(outptr->shm_name);
		close(shmfd);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// map it
	outptr->shm = mmap(0, outptr->shmc,
	                 PROT_READ | PROT_WRITE,
	                 MAP_SHARED,
	                 shmfd, 0);


	// see the comment in createshm regarding the closing of the fd.
	close(shmfd);

	// /now/ we check for errors from the mmap
	if (outptr->shm == 0) {
		// we cannot handle nomem because the memory should have already
		// been allocated by the host.
		int errnotmp = errno;
		shm_unlink(outptr->shm_name);
		errno = errnotmp;
		log_critf("mmap(2) failed to map shared memory: %d", errno);
		return EDB_ECRIT;
	}

	// assign buffer pointers
	void *bufferstart = (outptr->shm + sizeof(uint64_t)*4);
	outptr->jobv     = (edb_job_t *)(bufferstart);
	outptr->eventv   = (edb_event_t *)(outptr->jobv + outptr->jobc*sizeof(edb_job_t));

	return 0;
}