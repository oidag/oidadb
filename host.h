#ifndef _edbHOST_H_
#define _edbHOST_H_

#include <sys/types.h>
#include <unistd.h>

#include "include/ellemdb.h"
#include "errors.h"

typedef struct edb_host_st edb_host_t;

typedef struct edb_job_st {

} edb_job_t;

#define EDB_SHM_MAGIC_NUM 0x1A18BB0ADCA4EE22

typedef struct edb_shm_st {

	// the shared memory itself.
	// This shared memoeyr stores the following in this order:
	//   - magnum
	//   - shmc
	//   - jobc
	//   - eventc
	//   - jobv
	//   - eventv
	//
	// You shouldn't really use this field in leu of the helper pointers.
	void *shm; // if 0, that means the shm is unlinked.

	// sizes of the buffers
	uint64_t magnum; // magicnumber (EDB_SHM_MAGIC_NUM)
	uint64_t shmc;   // total bytes in the shm
	uint64_t jobc;   // total count of jobs in jobv.
	uint64_t eventc; // total count of events in eventv.

	// helper pointers
	edb_job_t   *jobv;   // job buffer
	edb_event_t *eventv; // events buffer

	// shared memory file name. not stored in the shm itself.
	char shm_name[32];

} edb_shm_t;

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
edb_err edb_host_shmlink(pid_t hostpid, edb_shm_t *outptr);
void edb_host_shmunlink(edb_shm_t *outptr);

#endif