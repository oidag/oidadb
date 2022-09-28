#ifndef _edbl_h_
#define _edbl_h_

#include "include/ellemdb.h"
#include "options.h"

// the lock directory is split up into 2 parts in a
// host/handle structure. The handle side is expected to
// be edbw's. The host is expected to be edbx.

typedef struct edbl_handle_st edbl_handle_t;
typedef struct edbl_host_st edbl_host_t;
typedef enum {

	// Shared lock. Unlimited amount of shared locks
	// can be placed on something so long there is not
	// an exclusive lock.
	EDBL_SHARED,

	// Exclusive lock. Only 1 exclusive lock can be
	// placed on something.
	EDBL_EXCLUSIVE,

	// Remove whatever lock was placed.
	EDBL_UNLOCK
} edbl_type;

// Initialize and deinitialize a host of edbl.
edb_err edbl_init(edbl_host_t *o_lockdir);
void    edbl_decom(edbl_host_t *lockdir);

// edbl_l... functions all lock various items of the database
// which provide for swift traffic control in a super multi-threaded
// enviromenet.
//
// When placing a lock on something, you must pick one of the at a
// time. If you attempt to double-lock something with the same lock
// type, a debug message will spit out and nothing will happen.
//
// attempting to 'upgrade' or 'downgrade' a lock is not allowed.
// You must unlock it first.
//
// todo: blocking? what if something has 5 shared locks, then 2 exclusive lock requests comes in...
//       who gets it and when?
//
// RETURNS:
//   All of these functions return only critical errors which
//   occour in unexpected hardware problems, or, the lack of properly
//   using them. If you're getting critical errors, then read the
//   documentation better. Otherwise, you can ignore the error in
//   nominal operation.
//
// THREADING:
//    These functions are not thread safe per-handle. But
//    are all thread safe per-host.
edb_err edbl_lentry(edbl_handle_t *lockdir, edb_eid eid, edbl_type type);
edb_err edbl_lstructure(edbl_handle_t*lockdir, uint16_t structureid, edbl_type type);
edb_err edbl_lobject(edbl_handle_t *lockdir, edb_oid oid, edbl_type type);
edb_err edbl_llookup(edbl_handle_t *lockdir, edb_pid pid, edbl_type type);

#endif