#ifndef _ODB_PAGES_H_
#define _ODB_PAGES_H_

#include "common.h"
#include "errors.h"

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
typedef void odb_page;
typedef uint64_t odb_revision;


// page vs block: a page is more primitive. another name for a block is a "user
// page".
typedef uint64_t odb_pid;
typedef uint64_t odb_bid;
typedef uint64_t odb_gid; // group
static const odb_bid ODB_BID_END = 0xFFFFFFFFFFFFFFFF;

export odb_err odb_open(const char *file, odb_ioflags flags, odb_desc **o_descriptor);
export void odb_close(odb_desc *desc);

/**
 * odbp_checkout and odbp_commit write to and read from the bound buffer
 * respectively.
 *
 * checkout requires that the bound buffer be either ODB_UBLOCKS or
 * ODB_UVERSIONS
 *
 * odbp_seek will set the descriptor's cursor to any given block offset in the
 * database. odbp_seek allows the block offset to be set beyond the end of the
 * file, but in contrast to lseek(2), will cause the (regular file) to expand
 * in size (rather than wait until read/write is performed).
 *
 * commit requires that the bound buffer be ODB_UBLOCKS
 */
export odb_err odbp_seek(odb_desc *desc, odb_bid block);
export odb_err odbp_checkout(odb_desc *desc, int blockc);
export odb_err odbp_commit(odb_desc *desc, int blockc);



#endif
