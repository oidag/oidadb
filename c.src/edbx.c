#define _GNU_SOURCE 1

#include "edbx.h"
#include "edbd.h"
#include "errors.h"
#include "edbw.h"
#include "include/oidadb.h"
#include "edba.h"
#include "edbs.h"
#include "wrappers.h"

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
	struct odb_hostconfig config;

	// shared memory with handles
	edbs_handle_t *shm;

	// worker buffer, see worker.h
	unsigned int  workerc;
	edb_worker_t *workerv;

	// page IO, see pages.h
	edba_host_t *ahost;
	edbpcache_t *pcache;


} edb_host_t;
static edb_host_t host = {0};


// helper function for edb_host to Check for EDB_EINVALs
static odb_err validatehostops(const char *path
							   , struct odb_hostconfig hostops) {
	if(path == 0) {
		log_errorf("path is null");
		return ODB_EINVAL;
	}
	if(hostops.job_buffq <= 0) {
		log_errorf("job buffer is either <=0 or not a multiple of edb_job_t");
		return ODB_EINVAL;
	}
	if(hostops.job_transfersize == 0) {
		log_errorf("transfer buffer cannot be 0");
		return  ODB_EINVAL;
	}
	if(hostops.job_transfersize % sysconf(_SC_PAGE_SIZE) != 0) {
		log_warnf("transfer buffer is not an multiple of system page size (%ld), "
			 "this may hinder performance", sysconf(_SC_PAGE_SIZE));
	}
	if(hostops.event_bufferq <= 0) {
		log_errorf("event buffer is <=0");
		return ODB_EINVAL;
	}
	if(hostops.worker_poolsize == 0) {
		log_errorf("worker_poolsize is 0");
		return ODB_EINVAL;
	}
	if(hostops.slot_count < hostops.worker_poolsize) {
		log_errorf("page buffer is smaller than worker_poolsize");
		return  ODB_EINVAL;
	}
	return 0;
}

// places a lock on the file according to locking spec. If a lock has already
// been placed on this file, ODB_EOPEN is returned and o_curhost is written too.
//
// Also retursn
//
// See unlock file to remove the lock
odb_err static lockfile(pid_t *o_curhost) {
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
			return ODB_EOPEN;
		}
		log_critf("fcntl(2) returned unexpected errno");
		return ODB_ECRIT;
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

odb_err odb_host(const char *path, struct odb_hostconfig hostops) {

	// deal with the futex stat here sense its not that hard.
	uint32_t *stat_futex = hostops.stat_futex;
	uint32_t dummy; // see if statement below.
	if(stat_futex == 0)  {
		// the caller doesn't care about the status futex, so we can just
		// point it to a dummy var.
		stat_futex = &dummy;
	}

	// check for EINVAL
	odb_err eerr;
	eerr = validatehostops(path, hostops);
	if(eerr) {
		*stat_futex = ODB_VERROR;
		futex_wake(stat_futex, INT32_MAX);
		return eerr;
	}

	// make sure host isn't already running
	if(host.state != HOST_NONE) {
		*stat_futex = ODB_VERROR;
		futex_wake(stat_futex, INT32_MAX);
		return ODB_EAGAIN;
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
		*stat_futex = ODB_VERROR;
		futex_wake(stat_futex, INT32_MAX);
		switch (errno) {
			case ENOENT:
				log_errorf("failed to open file %s: path of file does not "
						   "exist",
						   path);
				return ODB_ENOENT;
			default:
				log_errorf("failed to open file %s", path);
				return ODB_EERRNO;
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
	// todo: let the user set this config.
	edbd_config edbdconfig = edbd_config_default;
	eerr = edbd_open(&(host.file), host.fdescriptor, edbdconfig);
	if(eerr) {
		if(eerr == ODB_EOPEN) {
			// assuming this isn't a critical error: what has happened is that the
			// previous hoster of this file had crashed. This is because edbd_hostpid
			// returns ODB_EOPEN when either its currently hosted or the last host
			// had crashed: however we can deduce its the latter because sense we've
			// established a lock in the above section, we know that no such
			// processs is currently running.
			log_errorf("the previous host of this file had closed unexpectedly, "
			          "file may be corrupt.");
		}
		edbd_close(&host.file);
		goto ret;
	}
	// **defer: edbd_fileclose(host.file)
	host.state = HOST_OPENING_SHM;

	eerr = edbs_host_init(&host.shm, hostops);
	if(eerr) {
		goto ret;
	}
	// **defer: edbs_host_close ; edbs_host_free
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
	// **defer: edba_host_free(edba_host_t *host);
	host.state = HOST_OPENING_WORKERS;

	// configure all the workers.
	host.workerc = 0; // we'll increment this on success
	host.workerv = malloc(host.config.worker_poolsize * sizeof(edb_worker_t));
	if(host.workerv == 0) {
		if (errno == ENOMEM) {
			eerr = ODB_ENOMEM;
		} else {
			log_critf("malloc(3) returned an unexpected error: %d", errno);
			eerr = ODB_ECRIT;
		}
		goto ret;
	}
	// **defer: free(host.workerv);


	for(int i = 0; i < host.config.worker_poolsize; i++) {
		eerr = edbw_init(&(host.workerv[i]), host.ahost, host.shm);
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

	// at this point we have an open transferstate.
	// this by definition means clients can start filling up the job buffer even
	// though there's no workers to satsify the jobs yet.
	host.state = HOST_OPEN;

	// broadcast to any curious listeners that we're up and running.
	// todo: is this actually needed anywhere?
	futex_wake((uint32_t *)&host.state, INT32_MAX);

	// set our stat futex as active.
	*stat_futex = ODB_VACTIVE;
	futex_wake(stat_futex, INT32_MAX);

	// now we can freeze this thread until we get the futex signal to close.
	futex_wait((uint32_t *)&host.state, HOST_OPEN);

	// these lines are only hit if it was a graceful shutdown.
	eerr = 0;
	errno = 0;
	host.state = HOST_CLOSING;
	*stat_futex = ODB_VCLOSE; // (sense we know we are without error)
	futex_wake(stat_futex, INT32_MAX);

	// not we could have been brought here for any reason. That reason
	// depends on host.state, which will dicate what needs to be cleaned up.
	ret:
	log_infof("closing %s...", path);

	switch (host.state) {
		case HOST_CLOSING:
			// fallthrough
		case HOST_OPEN:
			log_infof("stopping %d workers...", host.workerc);
			edbs_host_close(host.shm);
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
			edba_host_free(host.ahost);
			// fallthrough
		case HOST_OPENING_ARTICULATOR:
			log_infof("decommissioning page buffer...");
			edbp_cache_free(host.pcache);
			// fallthrough
		case HOST_OPENING_PAGEBUFF:
			log_infof("closing shared memory...");
			edbs_host_free(host.shm);
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
	// todo: see above... do we need to broadcast this?
	futex_wake(&host.state, INT32_MAX);
	return eerr;
}

// must NOT be called from a worker thread.
odb_err odb_hoststop() {
	if(host.state == HOST_NONE) {
		return ODB_ENOHOST;
	}
	if(host.state != HOST_OPEN) {
		return ODB_EAGAIN;
	}

	// we must set this before the FUTEX_WAKE to prevent lost wakes.
	host.state = HOST_CLOSING;
	futex_wake(&host.state, 1);
	return 0;
}