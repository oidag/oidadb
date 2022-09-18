#ifndef _edbJOBS_H_
#define _edbJOBS_H_

#include "include/ellemdb.h"

// edb_jobclass must take only the first 4 bits. (to be xor'd with
// edb_cmd).
typedef enum _edb_jobclass {

	// means that whatever job was there is now complete and ready
	// for a handle to come in and install another job.
	EDB_JNONE = 0x0000,

	// structure ops
	EDB_STRUCT = 0x0001,

	// dynamic data ops
	// valuint64 - the objectid (0 for new)
	// valuint   - the length of that data that is to be written.
	// valbuff   - the name of the shared memory open via shm_open(3). this
	//             will be open as read only and will contain the content
	//             of the object. Can be null for deletion.
	EDB_DYN = 0x0002,

	// object ops
	// valuint64 - the objectid (cannot be 0)
	// valuint   - the amount of bytes to copy over. If this is zero, then it will
	//             be set to total
	// valbuff   - the name of the shared memeory via shm_open(3). This will
	//             be opened as write. Leave null to just
	EDB_OBJ = 0x0003,

	// locks for objects and structures.
	// todo: move these into EDB_OBJ and EDB_STRUCT?
	EDB_LCK = 0x0004

} edb_jobclass;

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
typedef struct edb_job_st {

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

} edb_job_t;

// analogous to read(2) and write(2).
// Write will block if the buffer becomes full.
// Read will block if the buffer becomes empty.
// They both return the total amount of bytes read/written or -1 on critical error (which shouldn't ever happen) (will be logged),
// and -2 on EOF.
//
// todo: confim the next statement:
// the returned amount of bytes read will always be equal to count if non-error.
//
// edb_jobclose and edb_jobreset simply change the state of the buffer. edb_jobclose will cause further reads
// and writes to return -2 until edb_jobreset is called.
//
// edb_jobreset is also sufficient in reseting the job's buffer.
//
//
// transferbuf should be equal to shm->transbuffer (found in the host/workers)
//
// TREADING
//   only 1 thread/process can call edb_jobread and another can call
//   edb_jobwrite on the same job at the same time.
//
int edb_jobread(edb_job_t *job, const void *transferbuf, void *buff, int count);
int edb_jobwrite(edb_job_t *job, void *transferbuf, const void *buff, int count);


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
int edb_jobclose(edb_job_t *job);
int edb_jobreset(edb_job_t *job);

// returns 1 if the job is closed, or 0 if it is open.
int edb_jobisclosed(edb_job_t *job);


#endif