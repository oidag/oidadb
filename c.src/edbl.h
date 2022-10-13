#ifndef _edbl_h_
#define _edbl_h_

#include <unistd.h>
#include <fcntl.h>

#include "include/ellemdb.h"
#include "options.h"
#include "file.h"

// the lock directory is split up into 2 parts in a
// host/handle structure. The handle side is expected to
// be edbw's. The host is expected to be edbx.

typedef struct edbl_host_st {
	const edb_file_t *fd;
	pthread_mutex_t mutex_index;
	pthread_mutex_t mutex_struct;
} edbl_host_t;

typedef struct edbl_handle_st {
	edbl_host_t *parent;
	int fd_d;
} edbl_handle_t;

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
edb_err edbl_init(edbl_host_t *o_lockdir, const edb_file_t *file);
void    edbl_decom(edbl_host_t *lockdir);

edb_err edbl_newhandle(edbl_host_t *host, edbl_handle_t *o_handle);
void edbl_destroyhandle(edbl_handle_t *handle);

typedef struct {
	edbl_type    l_type;
	uint64_t     l_start;
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
//   using them. If you're getting critical errors, then read the
//   documentation better. Otherwise, you can ignore the error in
//   nominal operation.
//
// THREADING:
//    Thread safe per-handle.
edb_err edbl_index(edbl_handle_t *lockdir,  edbl_type type);
edb_err edbl_struct(edbl_handle_t *lockdir, edbl_type type);
edb_err edbl_set(edbl_handle_t *lockdir, edbl_lockref lock);

// returns 1 if the lock can be installe, returns 0 otherwise.
// note by the time this function returns, the answer may be out of date.
int edbl_get(edbl_handle_t *lockdir, edbl_lockref lock);


// just use edbl_set for these.
/*
typedef enum {
	EDBL_CLUTCH_RELEASE,
	EDBL_CLUTCH_ACTIVATE,
	EDBL_CLUTCH_READ,
} edbl_clutcht;
// install/remove/read a clutch lock on a page.
// lock should be 0 for unlock, 1 for lock or 2 for no change.
// it will always return what the current state is.
//
// if setting to 1 when already 1, then it will wait.
//
// Traffic control should be done via edbl_set.
int edbl_clutch(edbl_host_t *lockdir, edb_pid page, edbl_clutcht lock);
 */
#endif