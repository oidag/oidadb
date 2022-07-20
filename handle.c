#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "include/ellemdb.h"
#include "handle.h"
#include "errors.h"

static edb_err starthost(edb_open_t params);

edb_err edb_open(edbh *handle, edb_open_t params) {

	// check for easy EDB_EINVAL
	if(handle == 0 || params.path == 0)
		return EDB_EINVAL;

	int err = 0;
	int openhost = 1;
	int fd = 0;
	struct flock dblock = {0};

	// open the file for the only purpose of reading locks.
	findhost:
	fd = open(params.path,O_RDONLY);
	if (fd == -1) {
		if(errno == ENOENT && params.openoptions & EDB_OCREAT && openhost) {
			// the file does not exist but the caller would
			// like us to attempt to create it. This leads us to these assumptions:
			//  1. there is no host so we don't need to worry about finding locks
			//  2. the file needs to be made and the host can do that.
			log_infof("%s: creating new file...", params.path)
			openhost = 0;
			edb_err starterr = starthost(params);
			if(starterr != 0) {
				return starterr;
			}
			goto findhost;
		}
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
	err = fcntl(fd, F_ODF_GETLK, &dblock);
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
		if(!openhost) {
			// we cannot try to start the host
			// explicitly. thus we give up.
			return EDB_ENOHOST;
		}
		// no process is attached to this file.
		edb_err starterr = starthost(params);
		if(starterr != 0) {
			return starterr;
		}
		// now with the host process started,
		// we can try it all again.
		openhost = 0;
		goto findhost;
	}
	// host successfully found.
	handle->hostpid = dblock.l_pid;

	// at this point, the file exist and has a host attached to it.
	// said host's pid is stored in hostpid.

	// todo: figure out how the handle will call other things from the host...

}


edb_err edb_close(edbh *handle);