#define _GNU_SOURCE 1

#include "errors.h"
#include <oidadb-internal/odbfile.h>
#include "buffers.h"
#include "blocks.h"

#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>

enum hoststate {
	ODB_SNEW = 0,

	ODB_SFILE,

	ODB_SALLOC,
	ODB_SLOAD,
	ODB_SPREP,
	ODB_SREADY,
};

typedef struct odb_cursor {
	odb_gid curosr_gid;
	odb_pid cursor_pid;
	odb_bid cursor_bid;
	off64_t cursor_off;
} odb_cursor;

typedef struct odb_desc {
	int fd;

	struct meta_block *block0;

	odb_ioflags flags;

	enum hoststate state;

	odb_buf *boundBuffer;

	// where the cursor is sense the last successful call
	// to odbp_seek. Will always be a multiple of ODB_PAGESIZE
	odb_cursor cursor;

	// todo: fields below this line need to be updated
	struct checkout_frame *stack; // prealloced
	int                   stack_current_offset;
	int                   stack_capacity;
	pthread_mutex_t       stackmutex;
} odb_desc;

struct checkout_options {
// note that all pages for a single checkout_data all belong to the same group.
	odb_ioflags flags;
	// if 0, then create pages of strait size and return it
	odb_pid     page;
	int         strait;
	void        *o_page;
};

struct checkout_data {
	struct checkout_options options;


	// (private vars)
	// the array of version of each page when we checked it out (associative).
	const odb_revision *version;
};

struct checkout_frame {
	struct checkout_data *data;
	int                  data_count;
};

odb_err
_odb_open(const char *path, odb_ioflags flags, mode_t mode, odb_desc *desc) {

	odb_err err = 0;

	// invals
	if (!strlen(path)) {
		return ODB_EINVAL;
	}
	if ((flags & ODB_PWRITE) && !(flags & ODB_PREAD)) {
		return ODB_EINVAL;
	}

	// set the structure to a 0val.
	memset(desc, 0, sizeof(*desc));

	desc->state = ODB_SNEW;
	desc->flags = flags;

	// convert our flags to mmap(2)/open(2) flags
	int mmap_flags = 0;
	int open_flags = 0;
	if (flags & ODB_PREAD) {
		mmap_flags |= PROT_READ;
		open_flags = O_RDONLY;
	}
	if (flags & ODB_PWRITE) {
		mmap_flags |= PROT_WRITE;
		open_flags = O_WRONLY;
	}
	if (flags & ODB_PCREAT) {
		open_flags |= O_CREAT | O_EXCL;
	}
	if ((flags & (ODB_PWRITE | ODB_PREAD)) == (ODB_PWRITE | ODB_PREAD)) {
		open_flags = O_RDWR;
	}
	open_flags |= O_CLOEXEC | O_LARGEFILE | O_SYNC;

	// open (and create if specified) the file
	desc->state = ODB_SFILE;
	int created = flags & ODB_PCREAT;
	desc->fd = open64(path, open_flags, mode);
	if (desc->fd == -1) {
		switch (errno) {
		case ENOENT: return ODB_ENOENT;
		case EEXIST: return ODB_EEXIST;
		default: return ODB_EERRNO;
		}
	}
	// initialize the first super block
	if (created) {
		if (ftruncate(desc->fd, ODB_PAGESIZE) == -1) {
			log_errorf("failed to truncate file %s", path);
			return ODB_EERRNO;
		}
	}

	// map the super block
	desc->state  = ODB_SALLOC;
	desc->block0 = mmap64(0, ODB_PAGESIZE, mmap_flags, MAP_SHARED, desc->fd, 0);
	if (desc->block0 == (void *) -1) {
		log_errorf("failed to map super block");
		return ODB_EERRNO;
	}

	// verify the super / initialize the super
	desc->state = ODB_SLOAD;
	if (created) {
		err = super_create(desc->block0);
	} else {
		err = super_load(desc->block0);
	}
	if (err) {
		return err;
	}

	// set the cursor
	desc->state = ODB_SPREP;
	err = odbp_seek(desc, 0);
	if (err) {
		return err;
	}

	// all done
	desc->state = ODB_SREADY;
	return 0;
}

// not thread/process safe when creating
// otherwise, if file exists (is initialized) then this function is therad and process safe
odb_err odb_open(const char *file, odb_ioflags flags, odb_desc **o_descriptor) {
	odb_desc *desc = malloc(sizeof(odb_desc));
	*o_descriptor = desc;
	odb_err err = _odb_open(file, flags, 0777, desc);
	if (err) {
		odb_close(*o_descriptor);
		return err;
	}
	return 0;
}


odb_err odb_close(odb_desc *descriptor) {
	odb_err err = 0;
	switch (descriptor->state) {
	case ODB_SREADY:
	case ODB_SPREP: super_free(desc->block0);
	case ODB_SLOAD: munmap(descriptor->block0, ODB_PAGESIZE);
	case ODB_SALLOC: close(descriptor->fd);
	case ODB_SFILE:
	case ODB_SNEW: break;
	}
	free(descriptor);
	return err;
}

static odb_pid bid2pid(odb_bid bid) {
	uint64_t group_index = bid / ODB_SPEC_BLOCKS_PER_GROUP;
	uint64_t meta_pages  = (group_index + 1) * ODB_SPEC_METAPAGES_PER_GROUP;
	return meta_pages + bid;
}

static off64_t pid2off(odb_pid pid) {
	return (off64_t) pid * ODB_PAGESIZE;
}

odb_err odbp_seek(odb_desc *desc, odb_bid block) {

	odb_cursor cursor;
	cursor.curosr_gid = block / ODB_SPEC_BLOCKS_PER_GROUP;
	cursor.cursor_bid = block;
	cursor.cursor_pid = bid2pid(block);
	cursor.cursor_off = pid2off(cursor.cursor_pid);

	off64_t off = lseek64(desc->fd, cursor.cursor_off, SEEK_SET);
	if (off == (off64_t) -1) {
		log_errorf("failed to seek to position");
		return ODB_EERRNO;
	}
	desc->cursor = cursor;
	return 0;
}

odb_err
addframedata(struct checkout_frame *target, struct checkout_options *opt) {

}

void dumpframedata(struct checkout_frame *target) {

}

odb_err odbp_bind_buffer(odb_desc *desc, odb_buf *buffer) {
	desc->boundBuffer = buffer;
}

odb_err odbp_checkout(odb_desc *desc, int bcount) {

	if (!desc->boundBuffer) {
		return ODB_EBUFF;
	}
	odb_buf *buffer = desc->boundBuffer;

	struct odb_buffer_info bufinf = buffer->info;

	// invals
	if (bufinf.bcount < bcount) {
		return ODB_EBUFFSIZE;
	}

	int          blockc    = bcount;
	odb_bid      bid_start = desc->cursor.cursor_bid;
	void         *blockv   = buffer->blockv;
	odb_revision *verv     = buffer->revisionv;
	odb_err      err;


	err = blocks_lock(desc, bid_start, blockc);
	if (err) {
		return err;
	}

	err = blocks_versions(desc, bid_start, blockc, verv);
	if (err) {
		blocks_unlock(desc, bid_start, blockc);
		return err;
	}

	err = blocks_copy(desc, bid_start, blockc, blockv);
	blocks_unlock(desc, bid_start, blockc);
	if (err) {
		return err;
	}

	return 0;
}

// attempts to apply all changes. If you attempt to save that had been saved elsewhere, a conflict arises.
// the conflict must merge itself. You can do this with pmerge.
//
// when commit sees a conflict it sees what pages need to be merged.
odb_err odbp_commit(odb_desc *desc, int bcount) {

	if (!desc->boundBuffer) {
		return ODB_EBUFF;
	}
	odb_buf *buffer = desc->boundBuffer;

	struct odb_buffer_info bufinf = buffer->info;

	// invals
	if (bufinf.bcount < bcount) {
		return ODB_EBUFFSIZE;
	}

	int                blockc    = bcount;
	odb_bid            bid_start = desc->cursor.cursor_bid;
	const void         *blockv   = buffer->blockv;
	const odb_revision *verv     = buffer->revisionv;

	odb_err err;

	err = blocks_lock(desc, bid_start, blockc);
	if (err) {
		return err;
	}

	err = blocks_match_versions(desc, bid_start, blockc, verv);
	if (err) {
		blocks_unlock(desc, bid_start, blockc);
		return err;
	}

	err = blocks_backup(desc, bid_start, blockc);
	if (err) {
		blocks_unlock(desc, bid_start, blockc);
		return err;
	}

	err = blocks_commit_attempt(desc, bid_start, blockc, blockv);
	blocks_unlock(desc, bid_start, blockc);
	if (err) {
		blocks_rollback(desc, bid_start, blockc);
		return err;
	}

	return 0;
}



