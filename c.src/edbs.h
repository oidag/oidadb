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
// edbs_host_free will block so long that jobs are open.
//
// edbs_host_free will cause further edbs_jobselect to return an error.
//
// ERRORS:
//  - EDB_ENOMEM - not enough memory
//  - EDB_EEXIST - host shm file already exists (logged). This may be because
//                 of a un-graceful shutdown or calling edbs_host_init twice
//                 in the same process without freeing the first.
//  - EDB_ECRIT
edb_err edbs_host_init(edbs_handle_t **o_shm, odb_hostconfig_t config);
void    edbs_host_free(edbs_handle_t *shm);

// edbs_handle loads the shared memory of a host based
// on the pid. To get the pid to pass in here, you must
// use edb_host_getpid.
//
// Errors:
//  - EDB_ENOHOST - shared memeory object cannot be loaded
//                  due to its absence
//  - EDB_EERRNO - error returned shm_open(3)
//  - EDB_ENOTDB - shared memeory not properly formated/corrupted
//  - EDB_ECRIT
edb_err edbs_handle_init(edbs_handle_t **o_shm, pid_t hostpid);
void    edbs_handle_free(edbs_handle_t *shm);

#endif