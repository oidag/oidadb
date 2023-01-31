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
#include <pthread.h>

#include "options.h"
#include "include/oidadb.h"
#include "edbd.h"
#include "errors.h"
#include "edbd_u.h"

// helper function to edb_open. returns 0 on success.
// it is assumed that path does not exist.
//
// can return EDB_EERRNO from open(2)
static edb_err createfile(int fd, odb_createparams params) {
	int err = 0;
	int syspagesize = sysconf(_SC_PAGE_SIZE);
	unsigned int pagemul = params.page_multiplier;
	int pagesneeded = 1 // for the head
			+ params.indexpages // for index pages
			+ params.structurepages; // for structures.

	// truncate the file to the full length of a page so we can write the
	// header.
	err = ftruncate(fd, pagesneeded * syspagesize * pagemul);
	if(err == -1) {
		int errnotmp = errno;
		log_critf("ftruncate(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// generate the head-intro structure.
	odb_spec_headintro intro = {0};
	intro.magic[0] = 0xA6;
	intro.magic[1] = 0xF0;
	intro.intsize  = sizeof(int);
	intro.entrysize = sizeof(odb_spec_index_entry);
	intro.pagesize = syspagesize;
	intro.pagemul  = pagemul;
	odb_spec_head newhead = {.intro = intro};
	// zero out the rest of the structure explicitly
	bzero(&(newhead.host), sizeof(odb_spec_head) - sizeof(odb_spec_headintro));
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
		log_critf("write(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	if(n != sizeof(newhead)) {
		log_critf("write(2) failed to write out entire head during creation");
		return EDB_ECRIT;
	}

	// now the reserved indexes.
	unsigned int finalpsize = sysconf(_SC_PAGE_SIZE) * newhead.intro.pagemul;
	off64_t indexstart = finalpsize * 1;
	off64_t structstart = finalpsize * (1+params.indexpages);

	// initialize the index pages
	for(int i = 0; i < params.indexpages; i++) {
		void *page = mmap64(0,
		                    finalpsize,
		                    PROT_READ | PROT_WRITE,
		                    MAP_SHARED,
		                    fd,
		                    indexstart + i * finalpsize);
		if(page == (void*)-1) {
			log_critf("mmap64 failed");
			return EDB_ECRIT;
		}
		if(i==0) {
			// initialize the resevered slots.
			edbd_u_initindex_rsvdentries(page, finalpsize,
			                             indexstart,
			                             structstart,
			                             params.indexpages,
			                             params.structurepages);
		} else {
			edbd_u_initindexpage(page, finalpsize);
		}
		munmap(page, finalpsize);
	}

	// initialize the structure pages
	for(int i = 0; i < params.indexpages; i++) {
		void *page = mmap64(0,
		                    finalpsize,
		                    PROT_READ | PROT_WRITE,
		                    MAP_SHARED,
		                    fd,
		                    structstart + i * finalpsize);
		if(page == (void*)-1) {
			log_critf("mmap64 failed");
			return EDB_ECRIT;
		}
		edbd_u_initstructpage(page, finalpsize);
		munmap(page, finalpsize);
	}


	return 0;
}

// helper function to edb_open.
// validates the headintro with the current system. returns
// EDB_ENOTDB if bad magic number (probably meaning not a edb file)
// EDB_EHW if invalid hardware.
static edb_err validateheadintro(odb_spec_headintro head, int pagemul) {
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
	if(head.entrysize != sizeof(odb_spec_index_entry)) {
		log_errorf("entry size mismatch: got %d but accepting %ld",
				   head.entrysize,
				   sizeof(odb_spec_index_entry));
		return EDB_EHW;
	}
	if(head.pagesize != sysconf(_SC_PAGE_SIZE)) {
		log_errorf("minimum page size mismatch: got %d but accepting %ld",
				   head.pagesize,
				   sysconf(_SC_PAGE_SIZE));
		return EDB_EHW;
	}
	if(head.pagemul != 1 &&
	   head.pagemul != 2 &&
	   head.pagemul != 4 &&
	   head.pagemul != 8) {
		log_errorf("page multiplier is not an acceptable number: got %d",
		           head.pagemul);
		return EDB_EHW;
	}
	if(head.pagemul != pagemul) {
		log_errorf("page multiplier of database does not match host: database is %d, host is %d",
		           head.pagemul,
				   pagemul);
		return EDB_EHW;
	}
	return 0;
}

unsigned int edbd_size(const edbd_t *c) {
	return c->page_size;
}

void edbd_close(edbd_t *file) {

	// destroy mutex
	pthread_mutex_destroy(&file->adddelmutex);

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

static int paramsvalid(odb_createparams params) {
	if(params.page_multiplier != 1 &&
	   params.page_multiplier != 2 &&
	   params.page_multiplier != 4 &&
	   params.page_multiplier != 8)
	{
		log_errorf("odb_createparams.page_multiplier must be 1,2,4, or 8, got "
				   "%d", params
		.page_multiplier);
		return 0;
	}
	if(params.indexpages <= 0) {
		log_errorf("odb_createparams.indexpages must be at least 1");
		return 0;
	}
	if(params.structurepages <= 0) {
		log_errorf("odb_createparams.structurepages must be at least 1");
		return 0;
	}
	return 1;
}

edb_err odb_create(const char *path, odb_createparams params) {
	if(!paramsvalid(params)) return EDB_EINVAL;

	// create the file itself.
	int fd = open(path, 0666, O_CREAT | O_EXCL | O_RDWR);
	if(fd == -1) {
		if(errno == EEXIST) {
			return EDB_EEXIST;
		}
		return EDB_EERRNO;
	}
	edb_err err = createfile(fd, params);
	close(fd);
	return err;
}

edb_err odb_createt(const char *path, odb_createparams params) {
	if(!paramsvalid(params)) return EDB_EINVAL;

	// create the file itself.
	int fd = open(path, 0666, O_TRUNC | O_RDWR);
	if(fd == -1) {
		if(errno == ENOENT) {
			return EDB_ENOENT;
		}
		return EDB_EERRNO;
	}
	edb_err err = createfile(fd, params);
	close(fd);
	return err;
}

edb_err edbd_open(edbd_t *o_file, const char *path) {

	// initialize memeory.
	bzero(o_file, sizeof(edbd_t));
	o_file->path = path;

	int err = 0;

	// get the stat of the file, and/or create the file if params dicates it.
	restat:
	err = stat(path, &(o_file->openstat));
	if(err == -1) {
		return EDB_EERRNO;
	}

	// make sure this is a file.
	if((o_file->openstat.st_mode & S_IFMT) != S_IFREG) {
		return EDB_EFILE;
	}

	// open the actual file descriptor.
	//
	// I use O_DIRECT here because we will be operating our own cache and
	// operating exclusively on a block-by-block basis with mmap. Maybe
	// that's good reason?
	//
	// O_NONBLOCK - set just incase mmap(2)/read(2)/write(2) are enabled to
	//              wait for advisory locks, which they shouldn't... we manage
	//              those locks manually. (see Mandatory locking in fnctl(2)
	o_file->descriptor = open64(path, O_RDWR
	                                  | O_DIRECT
	                                  | O_SYNC
	                                  | O_LARGEFILE
	                                  | O_NONBLOCK);
	if(o_file->descriptor == -1) {
		return EDB_EERRNO;
	}

	int psize = sysconf(_SC_PAGE_SIZE);


	// lock the first page of file so that only this process can use it.
	// we enable noblock so we know if something else has it open.
	struct flock64 dbflock = {
		.l_type   = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start  = 0,
		.l_len    = psize,
		.l_pid    = 0,
	};
	// note we use OFD locks becuase the host can be started in the same
	// process as handlers, but just use different threads.
	err = fcntl64(o_file->descriptor, F_OFD_SETLK, &dbflock);
	if(err == -1) {
		int errnotmp = errno;
		close(o_file->descriptor);
		if(errno == EACCES || errno == EAGAIN)
			return EDB_EOPEN; // another process has this open.
		// no other error is possible here except for out of memory error.
		log_critf("failed to call flock(2) due to unpredictable errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// file open and locked. load in the head.
	o_file->head = mmap64(0, psize, PROT_READ | PROT_WRITE,
	                      MAP_SHARED_VALIDATE,
	                      o_file->descriptor,
	                      0);
	if(o_file->head == MAP_FAILED) {
		int errnotmp = errno;
		log_critf("failed to call mmap(2) due to unpredictable errno: %d", errnotmp);
		close(o_file->descriptor);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// past this point, if we fail and need to return an error,
	// we must run only closefile(dbfile).

	// validate the head intro on the system to make sure
	// it can run on this architecture.
	{
		edb_err valdationerr = validateheadintro(o_file->head->intro,
												 o_file->head->intro.pagemul);
		if(valdationerr != 0) {
			edbd_close(o_file);
			return valdationerr;
		}
	}

	// initialize the mutex.
	err = pthread_mutex_init(&o_file->adddelmutex, 0);
	if(err) {
		log_critf("failed to initialize eof mutex: %d", err);
		edbd_close(o_file);
		return EDB_ECRIT;
	}

	// calculate the page size
	o_file->page_size    = o_file->head->intro.pagemul * o_file->head->intro.pagesize;

	// file has been opened/created, locked, and validated. we're done here.
	return 0;
}