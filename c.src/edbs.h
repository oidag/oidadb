#ifndef _edbSHAREDMEM_H_
#define _edbSHAREDMEM_H_

#include "edbp.h"

typedef struct edbs_handle_t edbs_handle_t;



// edbs_host CREATES a shared memory object to which other processes can
// connect to via edbs_handle. It will refuse to connect to existing ones.
//
// Note this returns an edbs_handle_t despite you being the hoster. This is
// because the shared memory's true host is the OS itself. You've just been
// given a handle to this shared memory. All functions are technically
// interchangeable despite the caller being handle or host.
//
// only config.job* and config.event* vars are used.
//
// Immediately after calling edbs_host_close, all attempts to communicate with
// the host from the handle will return ODB_ECLOSED save for open job buffers.
//
// If any job transfer buffers are in use, edbs_host_close will block until
// they are closed naturally (see edbs_jobterm via executor).
//
// Once your confident that all threads will no longer attempt to call
// edbs_job* functions, you can call edbs_host_free to free out resources.
//
// later: make a edbs_host_free varient that will NOT wait for job buffers to
//        be closed.
//
// ERRORS:
//  - ODB_ENOMEM - not enough memory
//  - ODB_EEXIST - host shm file already exists (logged). This may be because
//                 of a un-graceful shutdown or calling edbs_host_init twice
//                 in the same process without freeing the first.
//  - ODB_ECRIT
//
// THREADING:
// None of these functions are MT safe for a given shm. Call only in explicit
// order.
odb_err edbs_host_init(edbs_handle_t **o_shm, odb_hostconfig_t config);
void    edbs_host_close(edbs_handle_t *shm);
void    edbs_host_free(edbs_handle_t *shm);

// edbs_handle loads the shared memory of a host based
// on the pid. To get the pid to pass in here, you must
// use edb_host_getpid.
//
// Errors:
//  - ODB_ENOHOST - shared memeory object cannot be loaded
//                  due to its absence
//  - ODB_EERRNO - error returned shm_open(3)
//  - ODB_ENOTDB - shared memeory not properly formated/corrupted
//  - ODB_ECRIT
odb_err edbs_handle_init(edbs_handle_t **o_shm, pid_t hostpid);
void    edbs_handle_free(edbs_handle_t *shm);

#endif