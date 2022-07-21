#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "host.h"
#include "file.h"
#include "errors.h"
#include "include/ellemdb.h"

typedef struct edb_worker_st {
	// todo: keep in mind that the calling thread is in the pool.
	pthread_t thread;
} edb_worker_t;

typedef struct edb_host_st {
	edb_file_t       file;
	edb_hostconfig_t config;

	// job buffer
	off_t      jobc;
	edb_job_t *jobv;

	// event buffer
	off_t       eventc;
	edb_event_t eventv;

	// workers
	unsigned int  workerc;
	edb_worker_t *workerv;

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
static void hostclose(edb_host_t *host, int waitjobs) {
	log_infof("closing %s...", host->file.path);
	// close the file
	edb_fileclose(&(host->file));
}

edb_err edb_host(const char *path, edb_hostconfig_t hostops) {

	// check for EINVAL
	edb_err eerr;
	eerr = _validatehostops(path, hostops);
	if(eerr) return eerr;

	// start building out our host.
	edb_host_t host = {0};
	host.config = hostops;

	// open and lock the file
	eerr = edb_fileopen(&(host.file), path, hostops.flags);
	if(eerr) return eerr;

	// past this point, if we get an error we must close the file via edb_fileclose

	// allocate the buffers
	todo: stopped here.
	host.eventv = malloc()
	host.eventc = host.config.event_buffersize / sizeof (edb_event_t);


}

edb_err edb_hoststop(const char *path) {

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