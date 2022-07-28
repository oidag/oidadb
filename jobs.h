#ifndef _edbJOBS_H_
#define _edbJOBS_H_

#include "include/ellemdb.h"

typedef enum _edb_jobclass {

	// means that whatever job was there is now complete and ready
	// for a handle to come in and install another job.
	EDB_JNONE = 0,

	// structure ops
	EDB_STRUCT,

	// valuint64 - the objectid (0 for new)
	// valuint   - the length of that data that is to be written.
	// valbuff   - the name of the shared memory open via shm_open(3). this
	//             will be open as read only and will contain the content
	//             of the object. Can be null for deletion.
	EDB_DAT,

	// valuint64 - the objectid (cannot be 0)
	// valuint   - the amount of bytes to copy over. If this is zero, then it will
	//             be set to total
	// valbuff   - the name of the shared memeory via shm_open(3). This will
	//             be opened as write. Leave null to just
	EDB_OBJ,

} edb_jobclass;

typedef struct edb_job_st {
	edb_jobclass class;
	edb_cmd      command;

	// the transfer buffer for this job.
	// the offset from shm->transferbuff and how many bytes are there.
	//
	// do not use these directly, use edb_jobread and edb_jobwrite instead.
	unsigned long int transferbuffoff;
	unsigned int      transferbuffcapacity;
	uint32_t          futex_transferbuffc; // the amount of the buffer that is full.
	unsigned int      writehead; // data ends at this index
	unsigned int      readhead;  // data starts at this index
	pthread_mutex_t   bufmutex; // todo: initialize and deinitialize

	// used by the worker pool.
	// only matters if class != EDB_JNONE.
	// 0 means its not owned.
	unsigned int owner;

	// this is reassigned by the handles everytime they
	// install it. uses edb_shmhead_st.nextjobid.
	unsigned long int jobid;

	// the use of data depends on the class and command.
	edb_data_t data;

} edb_job_t;

// analogous to read(2) and write(2).
// Write will block if the buffer becomes full.
// Read will block if the buffer becomes empty.
// They both return the total amount of bytes read/written or -1 on critical error (which shouldn't ever happen).
//
// transferbuf should be equal to shm->transbuffer.
//
// TREADING: Limit 1 thread per job, per function. Meaning 1 thread can call edb_jobread and another thread can call
// edb_jobwrite on the same job. But thats it.
int edb_jobread(edb_job_t *job, const void *transferbuf, void *buff, int count);
int edb_jobwrite(edb_job_t *job, void *transferbuf, const void *buff, int count);


#endif