#ifndef _edbl_h_
#define _edbl_h_
#include "options.h"
#include "edbd.h"
#include "include/oidadb.h"

#include <unistd.h>
#include <fcntl.h>

// the lock directory is split up into 2 parts in a
// host/handle structure. The handle side is expected to
// be edbw's. The host is expected to be edbx.
typedef struct edbl_host_t edbl_host_t;
typedef struct edbl_handle_t edbl_handle_t;



// Initialize and deinitialize a host of edbl.
//
// ERRORS:
//  - ODB_EINVAL - file,o_lockdir was null
//  - ODB_ENOMEM - not enough memory
odb_err edbl_host_init(edbl_host_t **o_lockdir, const edbd_t *file);
void    edbl_host_free(edbl_host_t *h);

// ERRORS:
//  - ODB_EINVAL - file,o_lockdir was null
//  - ODB_ENOMEM - not enough memory
odb_err edbl_handle_init(edbl_host_t *host, edbl_handle_t **o_handle);
void    edbl_handle_free(edbl_handle_t *handle);

typedef enum edbl_act {

	// Shared lock. Unlimited amount of shared locks
	// can be placed on something so long there is not
	// an exclusive lock.
	EDBL_ASH = F_RDLCK,

	// Exclusive lock. Only 1 exclusive lock can be
	// placed on something.
	EDBL_AXL = F_WRLCK,

	// Remove whatever lock was placed.
	EDBL_ARELEASE = F_UNLCK
} edbl_act;

/*typedef struct {
	edbl_act    l_type;
	off64_t      l_start;
	unsigned int l_len;
} edbl_lockref;*/

// See locking.org. I'll only be documenting the arguments here, not the
// purpose of them.
typedef enum edbl_type {

	// no args.
	EDBL_LFILE,
	EDBL_LENTCREAT,
	EDBL_LSTRUCTCREAT,

	// eid
	EDBL_LENTRY,
	EDBL_LENTTRASH,
	EDBL_LREF0C,

	// lookup_pid
	EDBL_LLOOKUP_EXISTING,
	EDBL_LLOOKUP_NEW,

	// object_pid
	EDBL_LTRASHOFF,

	// object_pid, page_ioffset
	EDBL_LROW,

	// l_start, l_len
	EDBL_LARBITRARY,
} edbl_type;

typedef struct edbl_lock {
	edbl_type type;
	union {
		odb_eid eid;
		odb_pid lookup_pid;
		odb_pid object_pid;
		int64_t l_start;
	};
	union {
		unsigned int page_ioffset; // page (i)ntra offset
		unsigned int l_len;
	};
} edbl_lock;

// see locking.org.
//
// edbl_test will not place any lock but only return ODB_EAGAIN if such a
// lock described will result in blocking. Or will return 0 if such a lock
// would have been successful placed. (May also return CRIT, see RETURNS)
//
// RETURNS:
//   This function will only return critical errors which
//   occour in unexpected hardware problems, or, the lack of properly
//   using them (ODB_EINVAL). If you're getting errors, then read the
//   documentation better. Otherwise, you can always assume no errors.
//
// THREADING:
//    Thread safe per-handle.
odb_err edbl_set(edbl_handle_t *, edbl_act action, edbl_lock lock);
odb_err edbl_test(edbl_handle_t *, edbl_act action, edbl_lock lock);

#endif