#define _GNU_SOURCE 1

#include "edbx.h"
#include "edbd.h"
#include "errors.h"
#include "edbw.h"
#include "include/oidadb.h"
#include "edba.h"
#include "edbs.h"

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <linux/futex.h>
#include <sys/syscall.h>


typedef struct edb_host_st {

	// this is also a futex:
	//    if state == HOST_OPEN then you can:
	//      - set state to HOST_CLOSING
	//      - send FUTEX_WAKE to state.
	//    otherwise, if state != HOST_OPEN, then you must
	//    wait. (use FUTEX_WAIT on state)
	enum hoststate state;

	// file stuff
	int fdescriptor; // the master descriptor
	const char *fname; // file name

	// the file it is hosting
	edbd_t       file;

	// configuration it has when starting up
	odb_hostconfig_t config;

	// shared memory with handles
	edb_shm_t shm;

	// worker buffer, see worker.h
	unsigned int  workerc;
	edb_worker_t *workerv;

	// page IO, see pages.h
	edba_host_t ahost;
	edbpcache_t *pcache;


} edb_host_t;
static edb_host_t host = {0};


// helper function for edb_host to Check for EDB_EINVALs
static edb_err validatehostops(const char *path, odb_hostconfig_t hostops) {
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
		log_warnf("transfer buffer is not an multiple of system page size (%ld), "
			 "this may hinder performance", sysconf(_SC_PAGE_SIZE));
	}
	if(hostops.event_bufferq <= 0) {
		log_errorf("event buffer is <=0");
		return EDB_EINVAL;
	}
	if(hostops.worker_poolsize == 0) {
		log_errorf("worker_poolsize is 0");
		return EDB_EINVAL;
	}
	if(hostops.slot_count < hostops.worker_poolsize) {
		log_errorf("page buffer is smaller than worker_poolsize");
		return  EDB_EINVAL;
	}
	return 0;
}

// places a lock on the file according to locking spec. If a lock has already
// been placed on this file, EDB_EOPEN is returned and o_curhost is written too.
//
// Also retursn
//
// See unlock file to remove the lock
edb_err static lockfile(pid_t *o_curhost) {
	// read any fcntl locks
	struct flock dblock = (struct flock){
			.l_type = F_WRLCK,
			.l_whence = SEEK_SET,
			.l_start = 0,
			.l_len = 1, // first byte but mind you any host should have
			// locked the entire first page
			.l_pid = 0,
	};
	// note we use OFD locks becuase the host can be started in the same
	// process as handlers, but just use different threads.
	int err = fcntl(host.fdescriptor, F_OFD_SETLK, &dblock);
	if(err == -1) {
		if(errno == EACCES || errno == EAGAIN) {
			fcntl(host.fdescriptor, F_OFD_GETLK, &dblock);
			*o_curhost = dblock.l_pid;
			return EDB_EOPEN;
		}
		log_critf("fcntl(2) returned unexpected errno");
		return EDB_ECRIT;
	}
	return 0;
}
void static unlockfile() {
	// read any fcntl locks
	struct flock dblock = (struct flock){
			.l_type = F_UNLCK,
			.l_whence = SEEK_SET,
			.l_start = 0,
			.l_len = 1, // first byte but mind you any host should have
			// locked the entire first page
			.l_pid = 0,
	};
	int err = fcntl(host.fdescriptor, F_OFD_SETLK, &dblock);
	if(err == -1) {
		log_critf("failed to unlock file");
	}
}

edb_err odb_host(const char *path, odb_hostconfig_t hostops) {

	// check for EINVAL
	edb_err eerr;
	eerr = validatehostops(path, hostops);
	if(eerr) return eerr;

	// make sure host isn't already running
	if(host.state != HOST_NONE) {
		return EDB_EAGAIN;
	}

	// easy vals
	host.config = hostops;
	host.fname = path;
	host.state = HOST_OPENING_DESCRIPTOR;

	// open the actual file descriptor.
	//
	// I use O_DIRECT here because we will be operating our own cache and
	// operating exclusively on a block-by-block basis with mmap. Maybe
	// that's good reason?
	//
	// O_NONBLOCK - set just incase mmap(2)/read(2)/write(2) are enabled to
	//              wait for advisory locks, which they shouldn't... we manage
	//              those locks manually. (see Mandatory locking in fnctl(2)
	host.fdescriptor = open64(path, O_RDWR
	                                  | O_DIRECT
	                                  | O_SYNC
	                                  | O_LARGEFILE
	                                  | O_NONBLOCK);
	if(host.fdescriptor == -1) {
		switch (errno) {
			case ENOENT:
				log_errorf("failed to open file %s: path of file does not "
						   "exist",
						   path);
				return EDB_ENOENT;
			default:
				log_errorf("failed to open file %s", path);
				return EDB_EERRNO;
		}
	}
	// **defer: close(host.fdescriptor)
	host.state = HOST_OPENING_XLLOCK;

	// put exclusive locks on the file.
	{
		pid_t curhost;
		eerr = lockfile(&curhost);
		if (eerr) {
			goto ret;
		}
	}
	// **defer: unlockfile(&host);
	// todo: use host.state at the return block of this function to determian
	//       what must be deallocated.
	host.state = HOST_OPENING_FILE;

	// open the file
	eerr = edbd_open(&(host.file), host.fdescriptor, host.fname);
	if(eerr) {
		goto ret;
	}
	// **defer: edbd_fileclose(host.file)
	host.state = HOST_OPENING_SHM;

	eerr = edbs_host(&host.shm, hostops);
	if(eerr) {
		goto ret;
	}
	// **defer: edbs_dehost(host.shm);
	host.state = HOST_OPENING_PAGEBUFF;

	// page buffers & edba
	eerr = edbp_cache_init(&host.file, &host.pcache);
	if(eerr) {
		goto ret;
	}
	// **defer: edbp_decom(&host.pcache);
	host.state = HOST_OPENING_ARTICULATOR;
	eerr = edbp_cache_config(host.pcache,
	                  EDBP_CONFIG_CACHESIZE, host.config.slot_count);
	if(eerr) {
		goto ret;
	}

	eerr = edba_host_init(&host.ahost, host.pcache, &host.file);
	if(eerr) {
		goto ret;
	}
	// **defer: edba_host_decom(edba_host_t *host);
	host.state = HOST_OPENING_WORKERS;

	// configure all the workers.
	host.workerc = 0; // we'll increment this on success
	host.workerv = malloc(host.config.worker_poolsize * sizeof(edb_worker_t));
	if(host.workerv == 0) {
		if (errno == ENOMEM) {
			eerr = EDB_ENOMEM;
		} else {
			log_critf("malloc(3) returned an unexpected error: %d", errno);
			eerr = EDB_ECRIT;
		}
		goto ret;
	}
	// **defer: free(host.workerv);


	for(int i = 0; i < host.config.worker_poolsize; i++) {
		eerr = edbw_init(&(host.workerv[i]), &host.ahost, &host.shm);
		if(eerr) {
			goto ret;
		}
		host.workerc++;
	}
	// **defer: edb_workerdecom(&(host.workerv[0->host.workerc]));

	// enter hosting cycle
	// start all the workers.
	for(int i = 0; i < host.workerc; i++) {
		eerr = edbw_async(&(host.workerv[i]));
		if(eerr) {
			goto ret;
		}
	}
	// **defer: edb_workerjoin(&(host.workerv[0->host.workerc]));
	// **defer: edb_workerstop(&(host.workerv[0->host.workerc]));

	// at this point we have an open transferstate.
	// this by definition means clients can start filling up the job buffer even
	// though there's no workers to satsify the jobs yet.
	host.state = HOST_OPEN;

	// broadcast to any curious listeners that we're up and running.
	syscall(SYS_futex, (uint32_t *)&host.state,
	        FUTEX_WAKE, INT32_MAX, 0, 0, 0);


	// now we can freeze this thread until we get the futex signal to close.
	syscall(SYS_futex, (uint32_t *)&host.state,
	        FUTEX_WAIT, (uint32_t)HOST_OPEN, 0,
	        0, 0);

	// these two lines are only hit if it was a graceful shutdown.
	eerr = 0;
	errno = 0;
	host.state = HOST_CLOSING;

	// not we could have been brought here for any reason. That reason
	// depends on host.state, which will dicate what needs to be cleaned up.
	ret:
	log_infof("closing %s...", path);

	switch (host.state) {
		case HOST_CLOSING:
			// fallthrough
		case HOST_OPEN:
			log_infof("stopping %d workers...", host.workerc);
			for(int i = 0; i < host.workerc; i++) {
				edbw_stop(&(host.workerv[i]));
			}
			for(int i = 0; i < host.workerc; i++) {
				edbw_join(&(host.workerv[i]));
				log_infof("  ...worker %d joined", i);
			}
			log_infof("decommissioning workers...");
			for(int i = 0; i < host.workerc; i++) {
				edbw_decom(&(host.workerv[i]));
			}
			log_infof("freeing worker pool...");
			if(host.workerv) free(host.workerv);
			// fallthrough
		case HOST_OPENING_WORKERS:
			log_infof("decommissioning file articulator...");
			edba_host_decom(&host.ahost);
			// fallthrough
		case HOST_OPENING_ARTICULATOR:
			log_infof("decommissioning page buffer...");
			edbp_cache_free(host.pcache);
			// fallthrough
		case HOST_OPENING_PAGEBUFF:
			log_infof("closing shared memory...");
			edbs_dehost(&host.shm);
			//fallthrough
		case HOST_OPENING_SHM:
			log_infof("closing meta controller...");
			edbd_close(&(host.file));
			//fallthrough
		case HOST_OPENING_FILE:
			log_infof("unlocking file...");
			unlockfile();
			// fallthrough
		case HOST_OPENING_XLLOCK:
			log_infof("closing file descriptor...");
			close(host.fdescriptor);
			// fallthrough
		case HOST_OPENING_DESCRIPTOR:
			log_infof("database closed");
			host.state = HOST_NONE;
			// fallthrough
		case HOST_NONE:
			break;
	}

	// broadcast that we're closed.
	syscall(SYS_futex, (uint32_t *)&host.state,
	        FUTEX_WAKE, INT32_MAX, 0, 0, 0);
	return eerr;
}

// must NOT be called from a worker thread.
edb_err odb_hoststop() {
	if(host.state == HOST_NONE) {
		return EDB_ENOHOST;
	}
	if(host.state != HOST_OPEN) {
		return EDB_EAGAIN;
	}

	// we must set this before the FUTEX_WAKE to prevent lost wakes.
	host.state = HOST_CLOSING;
	syscall(SYS_futex, (uint32_t *)&host.state,
	        FUTEX_WAKE, 1, 0,
	        0, 0);
	return 0;
}

edb_err edb_host_getpid(const char *path, pid_t *outpid) {

	int err = 0;
	int fd = 0;
	struct flock dblock = {0};

	// open the file for the only purpose of reading locks.
	fd = open(path,O_RDONLY);
	if (fd == -1) {
		log_errorf("failed to open pid-check descriptor");
		if(errno == EDB_ENOENT) {
			return EDB_ENOENT;
		}
		return EDB_EERRNO;
	}
	// read any fcntl locks
	dblock = (struct flock){
			.l_type = F_WRLCK,
			.l_whence = SEEK_SET,
			.l_start = 0,
			.l_len = 1, // first byte but mind you any host should have
			            // locked the entire first page
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