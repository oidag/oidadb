#ifndef _edbSHAREDMEM_H_
#define _edbSHAREDMEM_H_

#include "edbs-jobs.h"
#include "edbp.h"


#define EDB_SHM_MAGIC_NUM 0x1A18BB0ADCA4EE22

typedef struct edb_shmhead_st {
	uint64_t magnum;    // magicnumber (EDB_SHM_MAGIC_NUM)
	uint64_t shmc;      // total bytes in the shm

	uint64_t joboff;    // offset from shm until the the start of jobv.
	uint64_t jobc;      // total count of jobs in jobv.

	uint64_t eventoff;  // offset from shm until the start of eventv
	uint64_t eventc;    // total count of events in eventv.

	uint64_t jobtransoff; // offset from shm until the start of transbuffer
	uint64_t jobtransc;   // *total* count of bytes in jobv->transferbuff

	// futex vars will broadcast a futex after they have been changed
	uint32_t futex_emptyjobs; // the amount of empty slots in the job buffer.
	uint32_t futex_newjobs; // the amount of new jobs that are not owned.

	// the next jobid. It is not strict that jobs need to have sequencial jobids
	// they just need to be unique.
	unsigned long int nextjobid;

	// job accept mutex for workers (process shared)
	pthread_mutex_t jobaccept;

	// job install mutex for handlers (process shared)
	pthread_mutex_t jobinstall;

	uint32_t futex_job;
	uint32_t futex_event;
} edb_shmhead_t;


// This structure is used to help you navigate the shared memory between
// host and handles. This structure itself is not stored in the
// shm, but the pointers within are pointing to parts of the shm.
//
// This is because you'll never find pointers inside of shared memory.
typedef struct edb_shm_st {

	// the shared memory itself.
	// This shared memoeyr stores the following in this order:
	//   - head
	//   - jobv
	//   - eventv
	//   - (some padding until the next page)
	//   - jobtransferbuf
	//
	// You shouldn't really use this field in leu of the helper pointers.
	void *shm; // if 0, that means the shm is unlinked.

	// sizes of the buffers
	edb_shmhead_t *head; // head will be == shm.

	// helper pointers
	edb_job_t   *jobv;    // job buffer.
	edb_event_t *eventv;  // events buffer.
	void        *transbuffer; // start of transfer buffer

	// indexpagesv and structpagesv are NOT found within the shm.
	// they are their own seperate allocations. They are read-only
	// by the handles. Their content's can change at any time.
	//
	// The amount of index pages can be found on the first index
	// page (which will always exist) (see spec of edbp_index),
	// pointed to by indexpagesc;
	//
	// The amount of structure pages can be found in the first index
	// page on the second entry (see spec of edbp_index), pointed to by
	// structpagec;
	//
	// todo: these.
	edbp_t const *indexpagec;
	edbp_t const * const *indexpagesv;
	edbp_t const *structpagec;
	edbp_t const * const *structpagesv;

	// shared memory file name. not stored in the shm itself.
	char shm_name[32];

} edb_shm_t;

// todo: document
edb_err edbs_host(edb_shm_t *o_shm, edb_hostconfig_t config);
void    edbs_dehost(edb_shm_t *shm);

// edbs_handle loads the shared memory of a host based
// on the pid. To get the pid to pass in here, you must
// use edb_host_getpid.
//
// Errors:
//     EDB_ENOHOST - shared memeory object cannot be loaded
//                   due to its absence
//     EDB_EERRNO - error returned shm_open(3)
//     EDB_ENOTDB - shared memeory not properly formated/corrupted
edb_err edbs_handle(edb_shm_t *o_shm, pid_t hostpid);
void    edbs_unhandle(edb_shm_t *shm);


edb_err edbs_entry(const edb_shm_t *shm, edb_eid eid, edb_entry_t *entry);

// see edb_index and edb_structs.
// these do the exact same thing but only specifically needs the shm.
edb_err edbs_index(const edb_shm_t *shm, edb_eid eid, edb_entry_t *o_entry);
edb_err edbs_structs(const edb_shm_t *shm, uint16_t structureid, edb_struct_t *o_struct);


#endif