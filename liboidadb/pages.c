#define _LARGEFILE64_SOURCE

#include "pages.h"
#include "errors.h"

#include <oidadb-internal/odbfile.h>
#include <oidadb/buffers.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>


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

	// If we just created the file, we are obligated to initialize the first
	// descriptor block. We need to upgrade our magic number lock to a exclusive
	// lock.
	if (created) {
		desc->unitialized = path;
	}
	if (created) {

		// initialize the odb file. We are only obligated to initialize the
		// first meta pages.
		err = volume_initialize(desc->fd);
		if (err) {
			// we make sure that if the volume failed to initialize under the
			// circumstance of cricital or if it already exist, make sure the
			// bail-out call to odb-close doesn't delete the file.
			switch (err) {
			case ODB_EEXIST:
			case ODB_ECRIT: desc->unitialized = 0;
			default:
				break;
			}
			return err;
		}

		// after we leave this if statement, we can continue on to load it as
		// normal.
		desc->unitialized = 0;
	}

	// map the first super block
	desc->state = ODB_SALLOC;
	err = volume_load(desc);
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


void odb_close(odb_desc *descriptor) {
	if (!descriptor) return;
	switch (descriptor->state) {
	case ODB_SREADY: group_unload(descriptor, descriptor->cursor.loaded_group);
	case ODB_SPREP: volume_unload(descriptor);
	case ODB_SALLOC: close(descriptor->fd);
	case ODB_SFILE:
	case ODB_SNEW: break;
	}
	if (descriptor->unitialized) {
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
	odb_err    err;
	cursor.curosr_gid = block / ODB_SPEC_BLOCKS_PER_GROUP;
	cursor.cursor_bid = block;
	cursor.cursor_pid = bid2pid(block);
	cursor.cursor_off = pid2off(cursor.cursor_pid);

	err = group_truncate(desc, cursor.curosr_gid,
			block % ODB_SPEC_BLOCKS_PER_GROUP);
	if (err) {
		return err;
	}

	err = group_load(desc, cursor.curosr_gid, &cursor.loaded_group);
	if (err) {
		return err;
	}

	// unload the previous group if there was one.
	group_unload(desc, desc->cursor.loaded_group);

	desc->cursor = cursor;
	return 0;
}

odb_err odbp_bind_buffer(odb_desc *desc, odb_buf *buffer) {
	desc->boundBuffer = buffer;
	return 0;
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



