#define _GNU_SOURCE 1

#include "errors.h"
#include <oidadb-internal/odbfile.h>

#include <oidadb/pages.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>

enum hoststate {
	HOST_NONE = 0,

	HOST_OPENING_DESCRIPTOR,

	HOST_OPENING_XLLOCK,
	HOST_OPENING_FILE,
	HOST_OPENING_SHM,
	HOST_OPENING_PAGEBUFF,
	HOST_OPENING_ARTICULATOR,
	HOST_OPENING_WORKERS,
	HOST_OPEN,

	HOST_CLOSING,
};

struct odbd_pages {
	int fd;

	struct meta_block *block0;

	odb_ioflags flags;

	enum hoststate state;

	// todo: fields below this line need to be updated
	struct checkout_frame *stack; // prealloced
	int stack_current_offset;
	int stack_capacity;
	pthread_mutex_t stackmutex;
};

struct checkout_options {
// note that all pages for a single checkout_data all belong to the same group.
	odb_ioflags flags;
	// if 0, then create pages of strait size and return it
	odb_pid page;
	int strait;
	void *o_page;
}

struct checkout_data {
	struct checkout_options options;


	// (private vars)
	// the array of version of each page when we checked it out (associative).
	const odb_vid *version;
};

struct checkout_frame {
	struct checkout_data *data;
	int data_count;
};






// not thread/process safe when creating
// otherwise, if file exists (is initialized) then this function is therad and process safe
odb_err odb_open(const char *path, odb_ioflags flags, mode_t mode, struct odbd_pages **o_descriptor) {
	odb_err err = _odb_open(path, flags, mode, o_descriptor);
	if(err) {
		odb_close(*o_descriptor);
	}
	return err;
}
odb_err _odb_open(const char *path, odb_ioflags flags, mode_t mode, struct odbd_pages **o_descriptor) {

	// INVAL
	if(!strlen(path)) {
		return ODB_EINVAL;
	}

	struct odbd_pages host;
	host.state = HOST_NONE;
	host.flags = flags;

	// convert our flags to mmap(2)/open(2) flags
	int mmap_flags = 0;
	int open_flags = 0;
	if(flags & ODB_PREAD) {
		mmap_flags |= PROT_READ;
		open_flags = O_RDONLY;
	}
	if(flags & ODB_PWRITE) {
		mmap_flags |= PROT_WRITE;
		open_flags = O_WRONLY;
	}
	if((flags & (ODB_PWRITE | ODB_PREAD)) == (ODB_PWRITE | ODB_PREAD)) {
		open_flags = O_RDWR;
	}


	// open (and create if specified) the file
	host.state = HOST_OPENING_DESCRIPTOR;
	int created = 0;
	reopen:
	host.fd = open64(path, open_flags
	                             | O_SYNC
	                             | O_LARGEFILE, mode);
	if(host.fd == -1) {
		switch (errno) {
			case ENOENT:
				if((flags & ODB_PCREAT) && !created) {
					created = 1;
					open_flags |= (O_CREAT | O_EXCL);
					goto reopen;
				}
				log_errorf("failed to open file %s: path of file does not "
				           "exist",
				           path);
				return ODB_ENOENT;
			default:
				log_errorf("failed to open file %s", path);
				return ODB_EERRNO;
		}
	}
	if(created) {
		if(ftruncate(host.fd, blocksize()) == -1) {
			log_errorf("failed to truncate file %s", path);
			return ODB_EERRNO;
		}
	}

	// map the metablock
	host.state = HOST_OPENING_XLLOCK;
	host.block0 = mmap64(0, blocksize(), mmap_flags,
	                        MAP_SHARED,
	                        host.fd,
	                        0);
	if(host.block0 == (void*)-1) {
		log_errorf("failed to map super block");
		return ODB_EERRNO;
	}
	// verify the super / initialize the super
	if(created) {
		initialize_super(host.block0);
	} else {
		odb_err err = verify_super(host.block0);
		if(err) {
			log_errorf("failed to verify super block");
			return err;
		}
	}

	// all done
	host.state = HOST_OPENING_FILE;
	return 0;
}

odb_err odb_close(struct odbd_pages *descriptor) {
	odb_err err = 0;
	switch (descriptor->state) {
		case HOST_OPENING_FILE:
			munmap(descriptor->block0, blocksize());
		case HOST_OPENING_XLLOCK:
			close(descriptor->fd);
		case HOST_OPENING_DESCRIPTOR:
		case HOST_NONE:
			break;
	}
	return err;
}

odb_err addframedata(struct checkout_frame *target, struct checkout_options *opt) {

}

void dumpframedata(struct checkout_frame *target) {

}

odb_err odbh_checkout(struct odbd_pages *handle, struct checkout_options *opts, int opts_count) {

	// make sure all the pages are good
	for(int i = 0; i < opts_count; i++) {
		odb_pid page = opts[i].page;
		if(!page) continue;
		if(page % 1024 <= 1) {
			return ODB_EBLOCK;
		}
		if(page % 1024 + opts[i].strait > 1024) {
			return ODB_EBLOCK;
		}
	}

	pthread_mutex_lock(&handle->stackmutex);
	if(handle->stack_capacity == handle->stack_current_offset) {
		pthread_mutex_unlock(&handle->stackmutex);
		return ODB_ENOSPACE;
	}

	struct checkout_frame *stack = &handle->stack[handle->stack_current_offset];
	for(int i = 0; i < opts_count; i++) {
		odb_err err = addframedata(stack, &opts[i]);
		if(err) {
			dumpframedata(stack);
			pthread_mutex_unlock(&handle->stackmutex);
			return err;
		}
	}

	handle->stack_current_offset++;
	pthread_mutex_unlock(&handle->stackmutex);
	return 0;
}

// revert all changes and checkin (closes a checkout)
odb_err odbh_rollback(struct odbd_pages *handle) {
	pthread_mutex_lock(&handle->stackmutex);
	if(handle->stack_current_offset == 0) {
		pthread_mutex_unlock(&handle->stackmutex);
		return ODB_ESTACK;
	}

	handle->stack_current_offset--;

	struct checkout_frame *stack = &handle->stack[handle->stack_current_offset];
	dumpframedata(stack);
	pthread_mutex_unlock(&handle->stackmutex);
	return 0;
}

// attempts to apply all changes. If you attempt to save that had been saved elsewhere, a conflict arises.
// the conflict must merge itself. You can do this with pmerge.
//
// when commit sees a conflict it sees what pages need to be merged.
odb_err odbh_commit(struct odbd_pages *handle) {
	pthread_mutex_lock(&handle->stackmutex);
	if(handle->stack_current_offset == 0) {
		pthread_mutex_unlock(&handle->stackmutex);
		return ODB_ESTACK;
	}

	struct checkout_frame *stack = &handle->stack[handle->stack_current_offset - 1];
	odb_err err = commitframedata(stack);
	if(err) {
		pthread_mutex_unlock(&handle->stackmutex);
		return err;
	}

	handle->stack_current_offset--;

	pthread_mutex_unlock(&handle->stackmutex);
	return 0;
}

// todo: I'll have to complete the above to complete the below.

// returns the number of pages found in o_pagev. pagev, straitv, and pages are assocative
int odbh_conflicts(odbh *handle, odb_pid *o_pagev, int *straitv, const void **pages);

// resolve marks the page as resolved
odb_err odbh_resolve(odbh *handle, odb_pid page, int strait);



