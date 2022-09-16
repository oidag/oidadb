#ifndef _edbHOST_H_
#define _edbHOST_H_

#include <sys/types.h>
#include <unistd.h>

#include "sharedmem.h"
#include "include/ellemdb.h"
#include "errors.h"
#include "host.h"

#define EDB_SHM_MAGIC_NUM 0x1A18BB0ADCA4EE22



enum hoststate {
	HOST_NONE = 0,
	HOST_CLOSED,
	HOST_CLOSING,
	HOST_OPEN,
	HOST_OPENING,
	HOST_FAILED,
};

typedef struct edb_host_st edb_host_t;

// stored the pid of the host for a given database file.
// does not validate the file itself.
//
// Errors:
//    EDB_ENOHOST - no host for file
//    EDB_EERRNO - error with open(2).
edb_err edb_host_getpid(const char *path, pid_t *outpid);

// edb_host_shmlink loads the shared memory of a host based
// on the pid. To get the pid to pass in here, you must
// use edb_host_getpid.
//
// Errors:
//     EDB_ENOHOST - shared memeory object cannot be loaded
//                   due to its absence
//     EDB_EERRNO - error returned shm_open(3)
//     EDB_ENOTDB - shared memeory not properly formated/corrupted
edb_err edb_host_shmlink(edb_shm_t *outptr, pid_t hostpid);
void edb_host_shmunlink(edb_shm_t *outptr);

#endif