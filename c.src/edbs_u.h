#include "edbs.h"


#define EDB_SHM_MAGIC_NUM 0x1A18BB0ADCA4EE22

typedef struct edbs_shmhead_t {
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

	// 0 for starting...
	// 1 for started (will signal)
	uint32_t futex_status;
} edbs_shmhead_t;

// job slot structures are met to be stored in shared memory
// and be accessed by multiple processes.
//
// each job has a buffer known as a 'transferbuffer' which is
// a set amount of bytes (allocated also in shared memory) that
// the worker and the handle can use to send information back
// and fourth.
//
// Note that there is no pointer to that particular buffer here.
// Infact, there's no pointers at all within this structure.
// This is because we're talking about multiple processes accessing
// shared memeory... a pointer's value in one process can
// mean something else entirely in another process. Even if they
// eventually point to the same spot in physical memory.
//
// So we must stick to offsets for guidance (transferbuffoff)
typedef struct edbs_shmjob_t {

	// Job desc is a xor'd value between 1 edb_jobclass, 1 edb_cmd.
	// if 0 then empty job.
	int jobdesc;

	// the transfer buffer for this job.
	// the offset from shm->transferbuff and how many bytes are there.
	//
	// do not use these directly, use edb_jobread and edb_jobwrite instead.
	unsigned long int transferbuffoff;
	unsigned int      transferbuffcapacity;
	uint32_t          futex_transferbuffc; // the amount of the buffer that is full.
	unsigned int      writehead; // data ends at this index
	unsigned int      readhead;  // data starts at this index
	int state;                   // 0 means closed, 1 means open.
	pthread_mutex_t   bufmutex;  // multiprocess mutex.

	// used by the worker pool.
	// only matters if class != EDB_JNONE.
	// 0 means its not owned.
	unsigned int owner;

	// this is reassigned by the handles everytime they
	// install it. uses edb_shmhead_st.nextjobid.
	unsigned long int jobid;

	// the use of data depends on the class and command.
	//edb_data_t data;

} edbs_shmjob_t;

// This structure is used to help you navigate the shared memory between
// host and handles. This structure itself is not stored in the
// shm, but the pointers within are pointing to parts of the shm.
//
// This is because you'll never find pointers inside of shared memory.
typedef struct edbs_handle_t {

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
	edbs_shmhead_t *head; // head will be == shm.

	// helper pointers
	edbs_shmjob_t   *jobv;    // job buffer.
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
	// todo: change these to edbd
	/*edbp_t const *indexpagec;
	edbp_t const * const *indexpagesv;
	edbp_t const *structpagec;
	edbp_t const * const *structpagesv;*/

	// shared memory file name. not stored in the shm itself.
	char shm_name[32];

} edbs_handle_t;