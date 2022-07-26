#ifndef _edbSHAREDMEM_H_
#define _edbSHAREDMEM_H_


typedef struct edb_shmhead_st {
	uint64_t magnum; // magicnumber (EDB_SHM_MAGIC_NUM)
	uint64_t shmc;   // total bytes in the shm
	uint64_t jobc;   // total count of jobs in jobv.
	uint64_t eventc; // total count of events in eventv.

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

typedef struct edb_shm_st {

	// the shared memory itself.
	// This shared memoeyr stores the following in this order:
	//   - head
	//   - jobv
	//   - eventv
	//
	// You shouldn't really use this field in leu of the helper pointers.
	void * shm; // if 0, that means the shm is unlinked.

	// sizes of the buffers
	edb_shmhead_t *head; // head will be == shm.

	// helper pointers
	edb_job_t   *jobv;   // job buffer
	edb_event_t *eventv; // events buffer

	// shared memory file name. not stored in the shm itself.
	char shm_name[32];

} edb_shm_t;

#endif