#ifndef _ODB_PAGES_H_
#define _ODB_PAGES_H_

#include "common.h"
#include "errors.h"
#include "buffers.h"

#define ODB_PAGESIZE 0x2000 /* 4096 * 2 */
#define ODB_BLOCKSIZE ODB_PAGESIZE

typedef enum odb_ioflags {

	// Can only be read
	ODB_PREAD = 1,

	//  Can only be written
	ODB_PWRITE = 2,

	// Puts the function into create mode. The pages are created and the first page id
	// is set to the page output variable.
	ODB_PCREAT = 4,

	// For odbh_page_open - This will prevent anything else from modifying the page so long that
	// the page is open. If this is false, then when the page is closed, and it was modify elsewhere,
	// a merge will be needed.
	ODB_PEXCL = 8,
} odb_ioflags;

// odb_open_file - is way faster than upstream, but only works with block devices

typedef struct odb_desc odb_desc;
typedef void     odb_block;
typedef uint64_t odb_ver;


// page vs block: a page is more primitive. another name for a block is a "user
// page".
typedef uint64_t odb_pid;
typedef uint64_t odb_bid;
typedef uint64_t odb_gid; // group
static const odb_bid ODB_BID_END = 0xFFFFFFFFFFFFFFFF;

export odb_err odb_open(const char *file, odb_ioflags flags, odb_desc **o_descriptor);
export void odb_close(odb_desc *desc);

/**
 * odbb_checkout and odbb_commit write to and read from the bound buffer
 * respectively.
 *
 * checkout requires that the bound buffer be either ODB_UBLOCKS or
 * ODB_UVERSIONS
 *
 * odbb_seek will set the descriptor's cursor to any given block offset in the
 * database. odbb_seek allows the block offset to be set beyond the end of the
 * file, but in contrast to lseek(2), will cause the (regular file) to expand
 * in size (rather than wait until read/write is performed).
 *
 * commit requires that the bound buffer be ODB_UBLOCKS
 */

// resolve - updates the block's user version to be equal to upstream versions


/*
 * odbb_commit->confliction will be an array associative to the blocks that are
 * being committed. It is a two-way argument. It dictates the negotiation of
 * what to do if the upstream version differs from the checked out version
 *
 *
 */

typedef enum odb_confliction {

	// If the upstream version is not equal to the committing version then this
	// will be set to ODB_CONFLICT_RAISED and ODB_EVERSION will be returned.
	//
	// Otherwise, if the upstream version is equal to the committing version,
	// this is left untouched.
	ODB_CONFLICT_ACCEPT  = 0,

	// Assume there's no conflict if the version has not changed sense the last
	// call to commit (this behaves the same as ODB_CONFLICT_ACCEPT if the buffer
	// had not been used in a commit prior.)
	//
	// After calling commit, this will be set to ODB_CONFLICT_ACCEPT if no error
	// was returned, or, will be set to ODB_CONFLCIT_RAISED if the item still
	// had a conflict.
	ODB_CONFLCIT_RESOLVE = 1,

	// No conflicts can be raised. The commit will force-update.
	//
	// After calling commit, regardless of what is returned, this will not be
	// modified.
	ODB_CONFLICT_REJECT  = 2,

	// If set will going INTO the commit function, then ODB_EVERSION is returned
	// automatically.
	// Set by commit: a conflict has been raised for this item. See upstream.
	ODB_CONFLCIT_RAISED  = 3,
} odb_confliction;

export odb_err odbb_seek(odb_desc *desc, odb_bid block_offset);
export odb_err odbb_bind_buffer(odb_desc *desc, odb_buf *buffer);
export odb_err odbb_checkout(odb_desc *desc, int blockc, odb_block **o_blockv);
export odb_err odbb_commit(odb_desc *desc, int blockc, odb_confliction *conflictionv);
export odb_err odbb_upstream(odb_desc *desc, int blockc, const odb_block **o_blockv);



#endif
