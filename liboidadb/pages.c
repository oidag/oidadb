#define _LARGEFILE64_SOURCE

#include "errors.h"
#include "buffers.h"
#include "blocks.h"
#include "meta.h"

#include <oidadb-internal/odbfile.h>
#include <oidadb/buffers.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>

enum hoststate {
	ODB_SNEW = 0,

	// opening file
	ODB_SFILE,

	ODB_SALLOC,
	ODB_SGROUP_LOAD,
	ODB_SPREP,
	ODB_SREADY,
};

typedef struct odb_cursor {
	meta_pages group_meta;
	odb_gid curosr_gid;
	odb_pid cursor_pid;
	odb_bid cursor_bid;
	off64_t cursor_off;
} odb_cursor;

typedef struct odb_desc {
	int fd;

	// special flag set to 1
	const char *unitialized;

	meta_pages meta0;

	odb_ioflags flags;

	enum hoststate state;

	odb_buf *boundBuffer;

	// where the cursor is sense the last successful call
	// to odbp_seek. Will always be a multiple of ODB_PAGESIZE
	odb_cursor cursor;
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

odb_err _odb_open(const char *path
				  , odb_ioflags flags
				  , mode_t mode
				  , odb_desc *desc) {

	odb_err err;

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
	int open_flags = 0;
	if (flags & ODB_PREAD) {
		open_flags = O_RDONLY;
	}
	if (flags & ODB_PWRITE) {
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
	unsigned int created = flags & ODB_PCREAT;
	desc->fd = open64(path, open_flags, mode);
	if (desc->fd == -1) {
		switch (errno) {
		case ENOENT: return ODB_ENOENT;
		case EEXIST: return ODB_EEXIST;
		default: return ODB_EERRNO;
		}
	}

	// prepare the lock on the magic number.
	struct flock64 flock = {
			.l_start = 0,
			.l_type = F_RDLCK,
			.l_whence = SEEK_SET,
			.l_pid = 0,
			.l_len = 2,
	};

	// If we just created the file, we are obligated to initialize the first
	// descriptor block. We need to upgrade our magic number lock to a exclusive
	// lock.
	if (created) {
		desc->unitialized = path;
		flock.l_type = F_WRLCK;
	}
	if(fcntl64(desc->fd, F_SETLKW64, flock) == -1) {
		return ODB_EERRNO;
	}
	flock.l_type = F_UNLCK;
	if (created) {

		// initialize the odb file. We are only obligated to initialize the
		// first meta pages.
		err = meta_volume_initialize(desc->fd);
		if (err) {
			fcntl64(desc->fd, F_SETLKW64, flock);
			return err;
		}

		// after we leave this if statement, we can continue on to load it as
		// normal.
	}

	// map the first super block
	desc->state  = ODB_SALLOC;
	err = meta_load(desc->fd, 0, &desc->meta0);
	if(err) {
		fcntl64(desc->fd, F_SETLKW64, flock);
		return err;
	}

	if (desc->meta0.sdesc->meta.magic != ODB_SPEC_HEADER_MAGIC) {
		fcntl64(desc->fd, F_SETLKW64, flock);
		return ODB_ENOTDB;
	}
	desc->unitialized = 0;
	fcntl64(desc->fd, F_SETLKW64, flock);

	desc->state = ODB_SGROUP_LOAD;
	// we use 2 different mappings for our first block when initializing the
	// cursor sense odbp_seek will de-load the group_meta when it doesn't need
	// it anymore and we don't want it de-loading our meta0.
	err = meta_load(desc->fd, 0, &desc->cursor.group_meta);
	if(err) {
		return 0;
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


void odb_close(odb_desc *descriptor) {
	if(!descriptor) return;
	switch (descriptor->state) {
	case ODB_SREADY:
	case ODB_SPREP: meta_unload(descriptor->cursor.group_meta);
	case ODB_SGROUP_LOAD: meta_unload(descriptor->meta0);
	case ODB_SALLOC: close(descriptor->fd);
	case ODB_SFILE:
	case ODB_SNEW: break;
	}
	if(descriptor->unitialized) {
		unlink(descriptor->unitialized);
	}
	free(descriptor);
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

	odb_err err;

	if(cursor.curosr_gid != desc->cursor.curosr_gid) {
		// group has changed, load the next group's meta
		err = meta_load(desc->fd, cursor.curosr_gid, &cursor.group_meta);
		if(err) {
			return err;
		}
	} else {
		cursor.group_meta = desc->cursor.group_meta;
	}

	off64_t off = lseek64(desc->fd, cursor.cursor_off, SEEK_SET);
	if (off == (off64_t) -1) {
		log_errorf("failed to seek to position");
		return ODB_EERRNO;
	}

	// if we happened to switch groups, de-load the old meta
	if(cursor.curosr_gid != desc->cursor.curosr_gid) {
		meta_unload(desc->cursor.group_meta);
	}


	desc->cursor = cursor;
	return 0;
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



