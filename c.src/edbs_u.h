#include "edbs.h"
#include "options.h"

#include <linux/futex.h>
#include <stdint.h>
#include <syscall.h>
#include <sys/time.h>
#include <errno.h>

#define EDB_SHM_MAGIC_NUM 0x1A18BB0ADCA4EE22

#define EDBS_SSTARTING 0 // must be 0 because thats the init value.
#define EDBS_SRUNNING 1
#define EDBS_SSTOPPED 3

typedef struct edbs_shmhead_t {

	////////////////////////////////////////////////////////////////////////////
	// Memory navigation
	////////////////////////////////////////////////////////////////////////////

	uint64_t magnum;    // magicnumber (EDB_SHM_MAGIC_NUM)
	uint64_t shmc;      // total bytes in the shm

	uint64_t joboff;    // offset from shm until the the start of jobv.
	uint64_t jobc;      // total count of jobs in jobv.

	uint32_t jobinst_next; // next-index-to-start: requires jobinstall.lock
	uint32_t jobacpt_next; // next-index-to-start: requires jobaccept.lock

	uint64_t eventoff;  // offset from shm until the start of eventv
	uint64_t eventc;    // total count of events in eventv.

	uint64_t jobtransoff; // offset from shm until the start of transbuffer
	uint64_t jobtransc;   // *total* count of bytes in jobv->transferbuff

	////////////////////////////////////////////////////////////////////////////
	// Inventory control
	////////////////////////////////////////////////////////////////////////////

	// the amount of empty slots in the job buffer.
	// futex vars will broadcast a futex after they have been changed
	// threading: requires jobmutex to read/write too.
	uint32_t emptyjobs;

	// The amount of new jobs that are not owned.
	// threading: requires jobmutex to read/write too.
	uint32_t newjobs;

	////////////////////////////////////////////////////////////////////////////
	// Traffic control. Lots of brain-hurt here. Just read the field-comments
	// and accept it.
	////////////////////////////////////////////////////////////////////////////

	// job mutex for workers and handlers (process shared).
	pthread_mutex_t jobmutex;

	// Used to track when all jobs have been closed during shutdown.
	//
	// Will be 1 if the following are true:
	//   - emptyjobs != jobc
	//
	// Will only broadcast when set to 0.
	//
	// THREADING
	//   - for setting, waking: jobmutex must be locked
	//   - for getting: no locks required
	//   - for waiting: jobmutex must not be locked.
	uint32_t futex_hasjobs;


	// for futex_selectorhold: set to 1 if the following is is true:
	//   - futex_status == EDBS_SRUNNING
	//   - AND newjobs == 0
	//
	// for futex_installerhold: set to 1 if the following are true:
	//   - futex_status == EDBS_SRUNNING
	//   - AND emptyjobs == 0
	//
	// will only broadcast when its set to 0.
	//
	// THREADING
	//  - for setting, waking: lock jobmutex (except in edbs_host_init)
	//  - for getting: no locks required
	//  - for waiting: jobmutex must not be locked
	uint32_t futex_selectorhold;
	uint32_t futex_installerhold;

	// will signal on changes
	// EDBS_SSTARTING for starting...
	// EDBS_SRUNNING for started
	// EDBS_SSTOPPED for shut down
	//
	// threading: requires jobmutex to read and write too.
	uint32_t futex_status;


	////////////////////////////////////////////////////////////////////////////
	// politics/analytics:
	////////////////////////////////////////////////////////////////////////////

	// the next jobid. It is not strict that jobs need to have sequencial jobids
	// they just need to be unique.
	// requires jobinstall.lock to increment
	unsigned long int nextjobid;
} edbs_shmhead_t;

// these valuse can also be used in futex_wait_bitset
enum edbs_jobflags {
	// the installer has called the first write
	EDBS_JFINSTALLWRITE = 0x0001,

	// installer successfully called term, thus executor write calls will be
	// ignored.
	EDBS_JINSTALLERTERM = 0x0002,

	// the executor successfully called term, thus the job buffer is fully
	// closed.
	EDBS_JEXECUTERTERM = 0x0004,

};

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

	////////////////////////////////////////////////////////////////////////////
	// regarding job ownership
	//
	// these require edbs_shmhead_t.
	//
	////////////////////////////////////////////////////////////////////////////

	// threading: requires jobmutex
	// Job desc is a xor'd value between 1 edb_jobclass, 1 edb_cmd.
	// if 0 then empty job.
	unsigned int jobdesc;

	// threading: requires jobmutex
	// used by the worker pool.
	// 0 means its not owned.
	unsigned int owner;

	////////////////////////////////////////////////////////////////////////////
	// regarding atomically editing the transfer buffer:
	////////////////////////////////////////////////////////////////////////////

	// shared memory mutex
	pthread_mutex_t   pipemutex;

	// heads = where their raster is (acts like what lseek sets).
	// bytes = the amount of bytes it has written in front of the other head.
	// both byte fields will have futex-on-change.
	//
	// Will also broadcast futex when the executor term'd (after being set to
	// -1)
	//
	// executorbytes, installerbytes requires pipemutex to be locked
	// to access.
	uint32_t executorbytes, installerbytes;
	unsigned int installerhead, executorhead;

	// Both of these futex holds are equal to 1 if the following are true:
	//  - the executor has not called term
	//  - their opposing byte fields (see above) are 0.
	//
	// Both of these futex holds are equal to 2 if the following are true:
	// - the executor has not called term
	// - their RESPECTIVE byte fields (see above) are equal to
	//   transferbuffcapacity
	//
	// will only broadcast when set to 0.
	//
	// THREADING:
	//   - setting, waking: requires pipemutex
	//   - getting: no mutex required
	//   - waiting: pipemutex must not be locked.
	uint32_t futex_executorhold, futex_installerhold;

	// Used to handle edge case discussed in edbs_jobterm's documentation
	// where edbs_jobterm will block.
	//
	// Will be set to 1 if the following are true:
	//  - the executorbytes is not equal to 0.
	//
	// will only broadcast when set to 0.
	//
	// THREADING:
	//   - setting, waking: requires pipemutex
	//   - getting: no mutex required
	//   - waiting: pipemutex must not be locked.
	uint32_t futex_exetermhold;


	// 0 means new job / initialized. see edbs_jobflags.
	enum edbs_jobflags transferbuff_FLAGS;

	////////////////////////////////////////////////////////////////////////////
	// politics/analytics:
	////////////////////////////////////////////////////////////////////////////

	// this is reassigned by the handles everytime they
	// install it. uses edb_shmhead_st.nextjobid.
	unsigned long int jobid;

	// see edbs_jobinstall.
	unsigned int name;

	////////////////////////////////////////////////////////////////////////////
	// helpers
	////////////////////////////////////////////////////////////////////////////

	// the transfer buffer for this job.
	// the offset from shm->transferbuff and how many bytes are there.
	//
	// these values never change once after edbs_host_init, they are constant.
	unsigned long int transferbuffoff;
	unsigned int      transferbuffcapacity;

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


// wrappers. See futex(2)
//
// futex_wait returns -1 if EAGAIN was returned (which is not really an error)
static int futex_wait(uint32_t *uaddr, uint32_t val) {
	int err = (int)syscall(SYS_futex, uaddr, FUTEX_WAIT, val, 0, 0, 0);
	if (err == -1 && errno != EAGAIN) {
		log_critf("critical futex_wait: %d", errno);
		return 0;
	}
	// reset errno to keep it clean.
	if(err == -1) {
		errno = 0;
	}
	return err;
}

// same as futex_wait, except if it ends up waiting will be equiped with a
// bitset, see futex_wake_bitset to learn about that.
//
// wait_bitset must not be 0.
static int futex_wait_bitset(uint32_t *uaddr, uint32_t val,
							 uint32_t wait_bitset) {
	int err = (int)syscall(SYS_futex, uaddr, FUTEX_WAIT_BITSET, val, 0, 0,
						   wait_bitset);
	if (err == -1 && errno != EAGAIN) {
		log_critf("critical futex_wait: %d", errno);
		return 0;
	}
	// reset errno to keep it clean.
	if(err == -1) {
		errno = 0;
	}
	return err;
}
// returns the count of waiters that were woken
static int futex_wake(uint32_t *uaddr, uint32_t count) {
	int ret = (int)syscall(SYS_futex, uaddr, FUTEX_WAKE, count, 0, 0, 0);
	return ret;
}
// same as futex_wake, but also:
//
// will only wake up waiters on futex_wait_bitset in which ~wait_bitset &
// wake_bitset~ returns true. Thus supplying (uint32_t)-1 will wake all
// bitset waiters.
//
// This will also wake any and all normal futex_wait-ers of uaddr.
// Conversely, a normal futex_wake will wake any and all futex_wait_bitset.
//
// wake_bitset must not be 0.
static int futex_wake_bitset(uint32_t *uaddr, uint32_t count,
							 uint32_t wake_bitset) {
	int ret = (int)syscall(SYS_futex, uaddr, FUTEX_WAKE_BITSET, count, 0, 0,
						   wake_bitset);
	return ret;
}