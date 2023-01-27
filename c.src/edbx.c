#define _GNU_SOURCE 1
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>

#include "edbx.h"
#include "edbd.h"
#include "errors.h"
#include "edbw.h"
#include "include/oidadb.h"
#include "edba.h"
#include "edbs.h"


typedef struct edb_host_st {

	enum hoststate state;

	// todo: these locks may not be needed sense the only thing
	//       allowed to acces host structures directly are the
	//       workers... and they only exist after HOST_OPEN is true
	// bootup - edb_host locks this until its in the HOST_OPEN transferstate.
	pthread_mutex_t bootup;
	// retlock - locked before switching to HOST_OPEN and double locked.
	// unlock this to have edb_host return.
	pthread_mutex_t retlock;

	// the file it is hosting
	edbd_t       file;

	// configuration it has when starting up
	edb_hostconfig_t config;

	// shared memory with handles
	edb_shm_t shm;

	// worker buffer, see worker.h
	unsigned int  workerc;
	edb_worker_t *workerv;

	// page IO, see pages.h
	edba_host_t ahost;
	edbpcache_t pcache;


} edb_host_t;


// helper function for edb_host to Check for EDB_EINVALs
static edb_err _validatehostops(const char *path, edb_hostconfig_t hostops) {
	if(path == 0) {
		log_errorf("path is null");
		return EDB_EINVAL;
	}
	if(hostops.job_buffq <= 0) {
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
	if(hostops.event_bufferq <= 0) {
		log_errorf("event buffer is either <=0 or not a multiple of edb_event_t");
		return EDB_EINVAL;
	}
	if(hostops.worker_poolsize == 0) {
		log_errorf("worker_poolsize is 0 or not a multiple of edb_event_t");
		return EDB_EINVAL;
	}
	if(hostops.slot_count < hostops.worker_poolsize) {
		log_errorf("page buffer is smaller than worker_poolsize");
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


edb_err odb_host(const char *path, edb_hostconfig_t hostops) {

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
		log_critf("failed to initialize transferstate change mutex: %d", err);
		return EDB_ECRIT;
	}
	err = pthread_mutex_lock(&host.bootup);
	if(err) {
		log_critf("failed to lock bootup mutex: %d", err);
		pthread_mutex_destroy(&host.bootup);
		return EDB_ECRIT;
	}

	// we are now in the opening transferstate: we have work to do but we can be hostclose
	// will work even before we're done
	host.state = HOST_OPENING;

	err = pthread_mutex_init(&host.retlock, 0);
	if(err) {
		log_critf("failed to initialize transferstate change mutex: %d", err);
		pthread_mutex_destroy(&host.bootup);
		return EDB_ECRIT;
	}


	// past this point we need pthread_mutex_destroy(&(host.bootup));

	// open and lock the file
	eerr = edbd_open(&(host.file), path, hostops.page_multiplier, hostops.flags);
	if(eerr) {
		goto clean_mutex;
	}

	// past this point, if we get an error we must close the file via edb_fileclose + pthread_mutex_destroy(&(host.bootup))

	eerr = edbs_host(&host.shm, hostops);
	if(eerr) {
		goto clean_file;
	}

	// past this point, we must edbs_dehost + edb_fileclose + pthread_mutex_destroy(&(host.bootup))

	// page buffers & edba
	eerr = edbp_init(&host.pcache, &host.file, host.config.slot_count);
	if(eerr) {
		goto clean_shm;
	}
	eerr = edba_host_init(&host.ahost, &host.pcache, &host.file);
	if(eerr) {
		goto clean_pages;
	}

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
		goto clean_ahost;
	}
	for(int i = 0; i < host.workerc; i++) {
		eerr = edb_workerinit(&(host.workerv[i]), &host.pcache, &host.shm);
		if(eerr) {
			goto clean_decomworkers;
		}
	}

	// before we start the workers, lets lock the finished
	pthread_mutex_lock(&host.retlock);
	// todo: error handling

	// from this point on, all we need to do is edb_hostclose to fully clean up everything.

	// at this point we have an open transferstate.
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
		log_infof("  ...worker %d joined", i);
	}

	clean_decomworkers:
	// decomission all the workers
	for(int i = 0; i < host.workerc; i++) {
		edb_workerdecom(&(host.workerv[i]));
	}

	// unallocate the worker space
	clean_freepool:
	log_infof("freeing worker pool...");
	free(host.workerv);

	clean_ahost:
	log_infof("decommissioning file actuator...");
	edba_host_decom(&host.ahost);

	clean_pages:
	log_infof("freeing page cache...");
	edbp_decom(&host.pcache);

	// unallocate the shared memory
	clean_shm:
	log_infof("deleting shared memory...");
	edbs_dehost(&host.shm, 1);

	// close the file
	clean_file:
	log_infof("closing database file...");
	edbd_close(&(host.file));

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
edb_err odb_hoststop(const char *path) {
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