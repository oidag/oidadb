#ifndef _edbSHAREDMEM_H_
#define _edbSHAREDMEM_H_

#include "edbp.h"

typedef struct edb_shm_t edb_shm_t;

// jobclose will force all calls to jobread and jobwrite, waiting or not. stop and imediately return -2.
// All subsequent calls to jobread and jobwrite will also return -2.
//
// jobreset will reopen the streams and allow for jobread and jobwrite to work as inteneded on a fresh buffer.
//
// By default, a 0'd out job starts in the closed state, so jobreset must be called beforehand. Multiple calls to
// edb_jobclose is ok. Multiple calls to edb_jobreset is ok.
//
// THREADING
//   At no point should jobclose and jobopen be called at the sametime with the same job. One
//   must return before the other can be called.
//
//   However, simultainous calls to the same function on the same job is okay (multiple threads calling
//   edb_jobclose is ok, multiple threads calling edb_jobreset is okay.)
void edb_jobclose(edb_job_t *job);
int edb_jobreset(edb_job_t *job);

// returns 1 if the job is closed, or 0 if it is open.
int edb_jobisclosed(edb_job_t *job);



// edbs_host CREATES a shared memory object to which other processes can
// connect to via edbs_handle. It will refuse to connect to existing ones.
//
// ERRORS:
//  - EDB_ENOMEM - not enough memory
//  - EDB_EEXIST - host shm file already exists (logged). This may be because
//                 of a un-graceful shutdown or calling edbs_host_init twice
//                 in the same process without freeing the first.
//  - EDB_ECRIT
edb_err edbs_host_init(edb_shm_t **o_shm, odb_hostconfig_t config);
void    edbs_host_free(edb_shm_t *shm);

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
edb_err edbs_handle_init(edb_shm_t **o_shm, pid_t hostpid);
void    edbs_handle_free(edb_shm_t *shm);

#endif