#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <error.h>
#include <errno.h>

#include "include/ellemdb.h"
#include "file.h"
#include "errors.h"

// helper function to edb_open. returns 0 on success.
// it is assumed that path does not exist.
//
// can return EDB_EERRNO from open(2)
static edb_err createfile(const char *path, int flags) {
	int err = 0;
	int minpagesize = sysconf(_SC_PAGE_SIZE);

	// create the file itself.
	int fd = creat(path, 0666);
	if(fd == -1) {
		return EDB_EERRNO;
	}

	// truncate the file to the full length of a page.
	err = ftruncate(fd, minpagesize);
	if(err == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("ftruncate(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// generate the head structure.
	edb_fhead_intro intro = {0};
	intro.magic[0] = 0xA6;
	intro.magic[1] = 0xF0;
	intro.intsize  = sizeof(int);
	intro.entrysize = sizeof(edb_entry_t);
	intro.pagesize = minpagesize;
	edb_fhead newhead = {.intro = intro};
	// zero out the rest of the structure explicitly
	bzero(&(newhead.newest), sizeof(edb_fhead) - sizeof(edb_fhead_intro));

	// generate a random newhead.intro.id
    {
		struct timeval tv;
		gettimeofday(&tv, 0);
		srand(tv.tv_sec + tv.tv_usec + getpid());
		int neededints = sizeof(newhead.intro.id) / sizeof(int);
		for (int i = 0; i < neededints; i++) {
			int rint = rand();
			*((int *)(&(newhead.intro.id[i * sizeof(int)]))) = rint;
		}
	}

	// write the head structure into the file
	ssize_t n = write(fd, &newhead, sizeof(newhead));
	if(n == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("write(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	if(n != sizeof(newhead)) {
		close(fd);
		log_critf("write(2) failed to write out entire head during creation");
		return EDB_ECRIT;
	}

	close(fd);
	return 0;
}

// helper function to edb_open.
// validates the headintro with the current system. returns
// EDB_ENOTDB if bad magic number (probably meaning not a edb file)
// EDB_EHW if invalid hardware.
static edb_err validateheadintro(edb_fhead_intro head) {
	if(head.magic[0] != 0xA6 || head.magic[1] != 0xF0) {
		log_errorf("invalid magic number: got {0x%02X, 0x%02X} but expecting {0x%02X, 0x%02X}",
				   head.magic[0], head.magic[1],
				   0xA6, 0xF0);
		return EDB_ENOTDB;
	}
	if(head.intsize != sizeof(int)) {
		log_errorf("integer size mismatch: got %d but accepting %ld",
				   head.intsize,
				   sizeof(int));
		return EDB_EHW;
	}
	if(head.entrysize != sizeof(edb_entry_t)) {
		log_errorf("entry size mismatch: got %d but accepting %ld",
				   head.entrysize,
				   sizeof(edb_entry_t));
		return EDB_EHW;
	}
	if(head.pagesize != sysconf(_SC_PAGE_SIZE)) {
		log_errorf("minimum page size mismatch: got %d but accepting %ld",
				   head.pagesize,
				   sysconf(_SC_PAGE_SIZE));
		return EDB_EHW;
	}
	return 0;
}



void edb_fileclose(edb_file_t *file) {
	// dealloc memeory
	int err = munmap(file->head, sysconf(_SC_PAGE_SIZE));
	if(err == -1) {
		int errtmp = errno;
		log_critf("failed to close file descriptor from nummap(2) errno: %d", errtmp);
		errno = errtmp;
		return;
	}

	// close the descriptor
	err = close(file->descriptor);
	if(err == -1) {
		int errtmp = errno;
		log_critf("failed to close file descriptor from close(2) errno: %d", errtmp);
		errno = errtmp;
		return;
	}
}

edb_err edb_fileopen(edb_file_t *dbfile, const char *path, int flags) {

	// initialize memeory.
	bzero(dbfile, sizeof(edb_file_t));
	dbfile->path = path;

	int err = 0;
	int trycreate = flags & EDB_HCREAT;

	// get the stat of the file, and/or create the file if params dicates it.
	restat:
	err = stat(path, &(dbfile->openstat));
	if(err == -1) {
		int errnotmp = errno;
		if(errno == ENOENT && trycreate) {
			// file does not exist and they would like to create it.
			edb_err createrr = createfile(path, flags);
			if(createrr != 0) {
				// failed to create
				return createrr;
			}
			// create successful, go back and perform the operation
			// with the explicit assumption that it is created.
			trycreate = 0;
			goto restat;
		}
		errno = errnotmp;
		return EDB_EERRNO;
	}

	// make sure this is a file.
	if((dbfile->openstat.st_mode & S_IFMT) != S_IFREG) {
		return EDB_EFILE;
	}

	// open the actual file descriptor.
	//
	// I use O_DIRECT here because we will be operating our own cache and
	// operating exclusively on a block-by-block basis with mmap. Maybe
	// that's good reason?
	//
	dbfile->descriptor = open(path, O_RDWR
	                                | O_DIRECT
	                                | O_SYNC
	                                | O_LARGEFILE);
	if(dbfile->descriptor == -1) {
		return EDB_EERRNO;
	}


	// lock the file so that only this process can use it.
	// we enable noblock so we know if something else has it open.
	struct flock dbflock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start  = 0,
		.l_len    = 0,
		.l_pid    = 0,
	};
	// note we use OFD locks becuase the host can be started in the same
	// process as handlers, but just use different threads.
	err = fcntl(dbfile->descriptor, F_OFD_SETLK, &dbflock);
	if(err == -1) {
		int errnotmp = errno;
		close(dbfile->descriptor);
		if(errno == EACCES || errno == EAGAIN)
			return EDB_EOPEN;
		// no other error is possible here except for out of memory error.
		log_critf("failed to call flock(2) due to unpredictable errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// file open and locked. load in the head.
	int psize = sysconf(_SC_PAGE_SIZE);
	dbfile->head = mmap(0, psize, PROT_READ | PROT_WRITE,
						   MAP_SHARED_VALIDATE | MAP_SYNC,
						   dbfile->descriptor,
						   0);
	if(dbfile->head == MAP_FAILED) {
		int errnotmp = errno;
		close(dbfile->descriptor);
		log_critf("failed to call mmap(2) due to unpredictable errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// past this point, if we fail and need to return an error,
	// we must run only closefile(dbfile).

	// validate the head intro on the system to make sure
	// it can run on this architecture.
	{
		edb_err valdationerr = validateheadintro(dbfile->head->intro);
		if(valdationerr != 0) {
			edb_fileclose(dbfile);
			return valdationerr;
		}
	}

	// file has been opened/created, locked, and validated. we're done here.
	return 0;
}
