#ifndef _edbl_h_
#define _edbl_h_

#include "include/ellemdb.h"
#include "options.h"

// the lock directory is split up into 2 parts in a
// host/handle structure. The handle side is expected to
// be edbw's. The host is expected to be edbx.

typedef struct edbl_host_st edbl_host_t;

typedef enum {

	// Shared lock. Unlimited amount of shared locks
	// can be placed on something so long there is not
	// an exclusive lock.
	EDBL_TYPSHARED,

	// Exclusive lock. Only 1 exclusive lock can be
	// placed on something.
	EDBL_EXCLUSIVE,

	// Remove whatever lock was placed.
	EDBL_TYPUNLOCK
} edbl_type;

// Initialize and deinitialize a host of edbl.
edb_err edbl_init(edbl_host_t *o_lockdir);
void    edbl_decom(edbl_host_t *lockdir);

// edbl_... functions all lock various items of the database
// which provide for swift traffic control in a super multi-threaded
// enviromenet.
//
// edbl_index and edbl_struct:
// locks the structure write mutex, preventing other things from also writting/deleting
// mutexes. Reads will still be allowed.
//
// todo: document, just kinda reading my notes right now
//
// When placing a lock on something, you must pick one of the at a
// time. If you attempt to double-lock something with the same lock
// type, a debug message will spit out and nothing will happen.
//
// attempting to 'upgrade' or 'downgrade' a lock is not allowed.
// You must unlock it first.
//
// RETURNS:
//   All of these functions return only critical errors which
//   occour in unexpected hardware problems, or, the lack of properly
//   using them. If you're getting critical errors, then read the
//   documentation better. Otherwise, you can ignore the error in
//   nominal operation.
//
// THREADING:
//    Thread safe per-handle.
edb_err edbl_index(edbl_host_t *lockdir);
edb_err edbl_struct(edbl_host_t *lockdir);
edb_err edbl_entry(edbl_host_t *lockdir, edbl_type type, edb_eid entryid);
edb_err edbl_obj(edbl_host_t *lockdir, edbl_type type, edb_oid objectid);
edb_err edbl_page(edbl_host_t *lockdir, edbl_type type, edb_pid start, unsigned int len);
#endif