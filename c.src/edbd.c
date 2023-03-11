#define _GNU_SOURCE

#include "options.h"
#include "include/oidadb.h"
#include "edbd.h"
#include "errors.h"
#include "edbd_u.h"

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
#include <stddef.h>

// helper function to edb_open. returns 0 on success.
// it is assumed that path does not exist.
//
// can return EDB_EERRNO from open(2)
static edb_err createfile(int fd, odb_createparams params) {
	int err;
	int syspagesize = (int)sysconf(_SC_PAGE_SIZE);
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
	intro.magic[0] = ODB_SPEC_HEADER_MAGIC[0];
	intro.magic[1] = ODB_SPEC_HEADER_MAGIC[1];
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
		unsigned int seed = tv.tv_sec + tv.tv_usec + getpid();
		int neededints = sizeof(newhead.intro.id) / sizeof(int);
		for (int i = 0; i < neededints; i++) {
			int rint = rand_r(&seed);
			*((int *)(&(newhead.intro.id[i * sizeof(int)]))) = rint;
		}
	}

	// set index/struct counts
	newhead.indexpagec = params.indexpages;
	newhead.structpagec = params.structurepages;

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
	edb_pid indexstartp = 1;
	off64_t indexstart = finalpsize * indexstartp;
	edb_pid structstartp = (1+params.indexpages);
	off64_t structstart = finalpsize * structstartp;


	// initialize the index pages
	edb_pid thispid = 1;
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
			                             indexstartp,
			                             structstartp,
			                             params.indexpages,
			                             params.structurepages);
		} else {
			edbd_u_initindexpage(page, finalpsize);
		}
		munmap(page, finalpsize);
		thispid++;
	}

	// initialize the structure pages
	for(int i = 0; i < params.structurepages; i++) {
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
		if(i+1 == params.structurepages) {
			edbd_u_initstructpage(page, finalpsize, 0);
		} else {
			edbd_u_initstructpage(page, finalpsize, thispid+1);
		}
		munmap(page, finalpsize);
		thispid++;
	}


	return 0;
}

// helper function to edb_open.
// validates the headintro with the current system. returns
// EDB_ENOTDB if bad magic number (probably meaning not a edb file)
// EDB_EHW if invalid hardware.
static edb_err validateheadintro(odb_spec_headintro head) {
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
	return 0;
}

unsigned int edbd_size(const edbd_t *c) {
	return c->page_size;
}

void edbd_close(edbd_t *file) {

	// destroy mutex
	pthread_mutex_destroy(&file->adddelmutex);

	// dealloc delwindow
	if(file->delpagesv) {
		for(int i = 0; i < file->delpagesc; i++) {
			munmap(file->delpagesv[i], edbd_size(file));
		}
		free(file->delpagesv);
	}

	// dealloc head page
	if(file->head_page) {
		int err = munmap(file->head_page, edbd_size(file));
		if (err == -1) {
			log_critf("mummap(2)");
		}
	}

	// dealloc index/structure (they used the same map)
	if(file->edb_indexv) {
		int err = munmap(file->edb_indexv,
						 edbd_size(file)
						 * (file->edb_indexc
						 + file->edb_structc));
		if (err == -1) {
			log_critf("mummap(2)");
		}
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
	int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
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

// returns EDB_EINVAL if params are invalid
static edb_err openconfigparamsvalid(edbd_config config) {
	if(config.delpagewindowsize < 1) {
		return EDB_EINVAL;
	}
	return 0;
}

edb_err edbd_open(edbd_t *o_file, int descriptor, edbd_config config) {

	if(openconfigparamsvalid(config)) {
		return EDB_EINVAL;
	}

	// initialize memeory.
	bzero(o_file, sizeof(edbd_t));
	o_file->descriptor = descriptor;

	int err = 0;

	// mmap the first page so we can look at the header.
	// We cant use read(2) because the descriptor is likely open in O_DIRECT.
	{
		o_file->head_page = mmap64(0, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE,
		                           MAP_SHARED_VALIDATE,
		                           o_file->descriptor,
		                           0);
		if(o_file->head_page == MAP_FAILED) {
			log_critf("failed to call mmap(2) due to unpredictable errno");
			return EDB_ECRIT;
		}

		// validate the head intro on the system to make sure
		// it can run on this architecture.
		edb_err valdationerr = validateheadintro(o_file->head_page->intro);
		if(valdationerr != 0) {
			munmap(o_file->head_page, sysconf(_SC_PAGE_SIZE));
			return valdationerr;
		}
		// calculate the page size
		o_file->page_size    = o_file->head_page->intro.pagemul
		                       * o_file->head_page->intro.pagesize; // same as sys_pszie.

		// sense we mmap'd the head in only the system page size, we must
		// resize it so that we mmap the database page size. We'll unmap it
		// in this block but if you see the next lines of code will remap it
		// to the predictable size.
		munmap(o_file->head_page, sysconf(_SC_PAGE_SIZE));
	}

	// atp: we know this is a valid oidadb file and the achitecture can
	//      support it.

	// helper var
	int psize = o_file->page_size;

	// file open. load in the head_page.
	o_file->head_page = mmap64(0, psize, PROT_READ | PROT_WRITE,
	                           MAP_SHARED_VALIDATE,
	                           o_file->descriptor,
	                           0);
	if(o_file->head_page == MAP_FAILED) {
		int errnotmp = errno;
		log_critf("failed to call mmap(2) due to unpredictable errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// **defer-on-fail: edbd_close(o_file)

	// load in all the index/structure pages.
	// the counts
	o_file->edb_indexc = o_file->head_page->indexpagec;
	o_file->edb_structc = o_file->head_page->structpagec;
	// the mmap itself.
	ssize_t totalbytes = psize * (o_file->edb_indexc + o_file->edb_structc);
	o_file->edb_indexv = mmap64(0,
	                            totalbytes,
								PROT_READ | PROT_WRITE,
								MAP_SHARED,
								o_file->descriptor,
								psize); // skip the first full page
	o_file->edb_structv = o_file->edb_indexv + psize*o_file->edb_indexc;
	if(o_file->edb_indexv == MAP_FAILED) {
		if(errno == ENOMEM) {
			log_errorf("not enough memory to allocate database index and "
					   "structure "
					   "pages: need a total of %ld KiB", totalbytes/1024);
			edbd_close(o_file);
			return EDB_ENOMEM;
		}
		log_critf("failed to call mmap(2) due to unpredictable errno");
		edbd_close(o_file);
		return EDB_ECRIT;
	}

	// helper vars (needed to have edbd_index working)
	odb_spec_index_entry *ents = o_file->edb_indexv + sizeof(odb_spec_index);
	o_file->enteriesperpage = ents[EDBD_EIDINDEX].objectsperpage;
	o_file->structsperpage = ents[EDBD_EIDSTRUCT].objectsperpage;

	// atp: edbd_index is working.

	// deleted page window
	o_file->delpagesq = config.delpagewindowsize;
	o_file->delpagesv = malloc(o_file->delpagesq * sizeof(void *));
	odb_spec_index_entry *ent;
	edbd_index(o_file, EDBD_EIDDELTED, &ent);
	edb_pid pid = ent->ref1; // see spec
	for(; o_file->delpagesc < o_file->delpagesq; o_file->delpagesc++) {
		if(!pid) {
			// all pages loaded
			break;
		}
		int i = o_file->delpagesc;
		o_file->delpagesv[i] = mmap64(0,
		                              edbd_size(o_file),
		                              PROT_READ | PROT_WRITE,
		                              MAP_SHARED,
		                              o_file->descriptor,
		                              edbd_pid2off(o_file, pid));
		if(o_file->delpagesv[i] == MAP_FAILED) {
			if(errno == ENOMEM) {
				log_errorf("not enough memory to allocate database deletion "
						   "page window (%d max pages)", o_file->delpagesq);
				edbd_close(o_file);
				return EDB_ENOMEM;
			}
			log_critf("failed to call mmap(2) due to unpredictable errno");
			err = EDB_ECRIT;
			edbd_close(o_file);
		}
		// next loop we fill it up with the page before this one.
		pid = ((odb_spec_deleted *)o_file->delpagesv[i])->head.pleft;
	}

	// initialize the mutex.
	err = pthread_mutex_init(&o_file->adddelmutex, 0);
	if(err) {
		log_critf("failed to initialize eof mutex: %d", err);
		edbd_close(o_file);
		return EDB_ECRIT;
	}


	// file has been opened, validated, and edbd_t has been populated. we're
	// done here.
	return 0;
}

edb_err edbd_index(const edbd_t *file, edb_eid eid
				   , odb_spec_index_entry **o_entry) {

	int pageoff = eid / file->enteriesperpage;

	// check for EDB_EEOF
	if(pageoff >= file->edb_indexc) {
		return EDB_EEOF;
	}

	// go to the body section on that page
	void *page = file->edb_indexv + pageoff * edbd_size(file)
			+ ODB_SPEC_HEADSIZE;

	// now do the intra-page offset
	*o_entry = page + (eid % file->enteriesperpage)*sizeof
			(odb_spec_index_entry);
	return 0;
}

edb_err edbd_struct(const edbd_t *file, uint16_t structureid,
                    const odb_spec_struct_struct **o_struct) {

	int pageoff = structureid / file->structsperpage;

	// check for EDB_EEOF
	if(pageoff >= file->edb_structc) {
		return EDB_EEOF;
	}

	// go to the body section on that page
	void *page = file->edb_structv + pageoff * edbd_size(file)
	             + ODB_SPEC_HEADSIZE;

	// now do the intra-page offset
	*o_struct = page
			+ (structureid % file->structsperpage)
			* sizeof(odb_spec_struct_full_t)
			+ offsetof(odb_spec_struct_full_t, content) ;

	return 0;

}