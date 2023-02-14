#ifndef _edbl_h_
#define _edbl_h_
#define _GNU_SOURCE
#include "include/oidadb.h"
#include "options.h"
#include "edbd.h"

#include <unistd.h>
#include <fcntl.h>

// the lock directory is split up into 2 parts in a
// host/handle structure. The handle side is expected to
// be edbw's. The host is expected to be edbx.
typedef struct edbl_host_t edbl_host_t;
typedef struct edbl_handle_t edbl_handle_t;

typedef enum {

	// Shared lock. Unlimited amount of shared locks
	// can be placed on something so long there is not
	// an exclusive lock.
	EDBL_TYPSHARED = F_RDLCK,

	// Exclusive lock. Only 1 exclusive lock can be
	// placed on something.
	EDBL_EXCLUSIVE = F_WRLCK,

	// Remove whatever lock was placed.
	EDBL_TYPUNLOCK = F_UNLCK
} edbl_type;

// Initialize and deinitialize a host of edbl.
//
// ERRORS:
//  - EDB_EINVAL - file,o_lockdir was null
//  - EDB_ENOMEM - not enough memory
edb_err edbl_host_init(edbl_host_t **o_lockdir, const edbd_t *file);
void    edbl_host_free(edbl_host_t *h);

// ERRORS:
//  - EDB_EINVAL - file,o_lockdir was null
//  - EDB_ENOMEM - not enough memory
edb_err edbl_handle_init(edbl_host_t *host, edbl_handle_t **o_handle);
void    edbl_handle_free(edbl_handle_t *handle);

typedef struct {
	edbl_type    l_type;
	off64_t      l_start;
	unsigned int l_len;
} edbl_lockref;

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
//   using them (EDB_EINVAL). If you're getting errors, then read the
//   documentation better. Otherwise, you can ignore the error in
//   nominal operation.
//
// THREADING:
//    Thread safe per-handle.
edb_err edbl_set(edbl_handle_t *, edbl_lockref lock);

// returns 1 if the lock can be installe, returns 0 otherwise.
// note by the time this function returns, the answer may be out of date.
int edbl_get(edbl_handle_t *, edbl_lockref lock);

// returns 1 if there's a clutch on this entry
//
// will automatically deal with clutch locks. (See Entry-* chapters)
//
// edbl_entrycreation locks and unlocks the creation mutex
int edbl_entry(edbl_handle_t *, edb_eid, edbl_type);
int edbl_entrycreaiton_lock(edbl_handle_t *);
int edbl_entrycreaiton_release(edbl_handle_t *);

// note: if you try to lock 2-times-in-row, this is no error. That's fine, just send out a debug message
// the lock is uneffected.
int edbl_entryref0c(edbl_handle_t *, edb_eid, edbl_type);

#endif