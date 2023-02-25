#ifndef _edbJOBS_H_
#define _edbJOBS_H_

#include "include/oidadb.h"
#include "edbs.h"

#define EDB_OID_OP1 0x0000ffffffffffff
#define EDB_OID_OP2 0x0000fffffffffffe

// For EDB_CCREATE
// EDB_OID_AUTOID - find the best deleted OID that can still be used.
#define EDB_OID_AUTOID EDB_OID_OP1

// edb_jobclass must take only the first 4 bits. (to be xor'd with
// edb_cmd).
typedef enum edb_jobclass {

	// means that whatever job was there is now complete and ready
	// for a handle to come in and install another job.
	EDB_JNONE = 0x0000,

	// structure ops
	//
	//   (EDB_CWRITE: not supported)
	//   EDB_CCREATE:
	//     <- edb_struct_t (no implicit fields)
	//     -> edb_err (parsing error)
	//     <- arbitrary configuration
	//     -> uint16_t new structid
	//   EDB_CDEL:
	//     <- uint16_t structureid
	//     -> edb_err
	EDB_STRUCT = 0x0001,

	// dynamic data ops
	// valuint64 - the objectid (0 for new)
	// valuint   - the length of that data that is to be written.
	// valbuff   - the name of the shared memory open via shm_open(3). this
	//             will be open as read only and will contain the content
	//             of the object. Can be null for deletion.
	EDB_DYN = 0x0002,

	// Perform CRUD operations with a single object.
	// see edb_obj() for description
	//
	// all cases:
	//     <- edb_oid (see also: EDB_OID_... constants)
	//     (additional params, if applicable)
	//     -> edb_err [1]
	//  EDB_CCOPY:
	//     (all cases)
	//     -> void *rowdata
	//     ==
	//  EDB_CWRITE:
	//     (all cases)
	//     <- void *rowdata
	//     ==
	//  EDB_CCREATE:
	//     (all cases)
	//     // todo: what if they think the structure is X bytes long and sense has been updated and thus now is X+/-100 bytes long?
	//     <- void *rowdata (note to self: keep this here, might as well sense we already did the lookup)
	//     -> created ID
	//     ==
	//  EDB_CDEL
	//     (all cases)
	//     ==
	//  EDB_CUSRLK(R/W): (R)ead or (W)rite the persistant locks on rows.
	//     (all cases)
	//     (R) -> edb_usrlk
	//     (W) <- edb_usrlk
	//     ==
	//
	// [1] This error will describe the efforts of locating the oid
	//     which will include:
	//       - EDB_EHANDLE - handle closed stream/stream is invalid
	//       - EDB_EINVAL - entry in oid was below 4.
	//       - EDB_EINVAL - jobdesc was invalid
	//       - EDB_EINVAL - (EDB_CWRITE) start wasn't less than end.
	//       - EDB_ENOENT - (EDB_CWRITE, EDB_CCOPY) oid is deleted
	//       - EDB_EOUTBOUNDS - (EDB_CWRITE): start was higher than fixedlen.
	//       - EDB_EULOCK - failed due to user lock (see EDB_FUSR... constants)
	//       - EDB_EEXIST - (EDB_CCREATE): Object already exists
	//       - EDB_ECRIT - unknown error
	//       - EDB_ENOSPACE - (EDB_CCREATE, using AUTOID): disk/file full.
	//       - EDB_EEOF - the oid's entry or row was larger than the most possible value
	//
	EDB_OBJ = 0x0004,

	// Modify the entries, updating the index itself.
	//
	// EDB_ENT | EDB_CCREATE
	//   <- edb_entry_t entry. Only the "parameters" group of the structure is used.
	//      This determains the type and other parameters.
	//   -> edb_err error
	//   -> (if no error) uint16_t entryid of created ID
	//
	// EDB_ENT | EDB_CDEL
	//   <- uint16_t entryid of ID that is to be deleted
	//   -> edb_err error
	//
	EDB_ENT = 0x0008,

} edb_jobclass;

// unlike many of the other namespaces, we actually expose some structures
// for self-allocation.
typedef struct edbs_job_t {
	const edbs_handle_t *shm;
	unsigned int jobpos;

	// 0: installer (edbs_jobinstall)
	// non-zero: executor (edbs_jobselect)
	unsigned int descriptortype;
} edbs_job_t;

// If there's no jobs installed in the yet, this function will block until
// there is one, or the handle is shutdown.
//
// o_job.descriptortype will be set to 'executor'.
//
// ownerid should be worker id: used only for analytical purposes.
//
// call edbs_jobclose if you're all done with the job thus marking it
// complete. edbs_jobclose will implicitly call edbs_jobterm.
//
// edbs_jobclose will do nothing if the descriptor is marked as the installer.
//
// ERRORS:
//   - EDB_ECLOSED - there are no jobs available AND edbs_host_close has been
//                   called, thus no more job installs are possible. This
//                   means that edbs_jobselect will only block so long that
//                   the host of the shm is running and accepting jobs.
//                   Futhermore, edbs_jobselect will return even after
//                   edbs_host_close has been called so long that there's jobs
//                   that need to be doing.
//   - EDB_ECRIT
edb_err edbs_jobselect(const edbs_handle_t *shm,
					   edbs_job_t  *o_job,
					   unsigned int ownerid);
void edbs_jobclose(edbs_job_t job);
//int     edbs_jobisclosed(edbs_job_t job); // todo: I don't think we need this

// Installs a job with given jobclass and outputs a handle to said job.
//
// name is used specifically for analytics, debugging purposes. Should be the
// handle id.
//
// o_job.descriptortype be set to be 'installer'.
//
// ERRORS
//  - EDB_ECLOSED - The host is closed, or is in the process of closing. So
//                  no new jobs are being accepted.
//  - EDB_ECRIT
edb_err edbs_jobinstall(const edbs_handle_t *shm,
						unsigned int jobclass,
						unsigned int name,
						edbs_job_t *o_job);

// A transfer buffer is structured as a pipe, though bi-directional. If both
// sides of the pipe do not follow specification, deadlocks will ensue. Here
// are the basic rules about reading and writting to the transfer buffer:
//
//  - Each transfer buffer is expected to have an 'installer' and an
//    'executer' (see descriptortype): constituting 'both sides' of the pipe.
//  - Write will block if the buffer becomes full, unless:
//    - if the opposite side has called edbs_jobterm then write will return 0
//      immediately.
//  - Read will block if the buffer becomes empty. unless:
//    - if the executor calls edbs_jobterm, read will return 0 immediately.
//  - The installer must perform the first operation and that operation must
//    be write. The installer can only call
//    edbs_jobterm before this write.
//  - Once the executer calls edbs_jobterm, the job is considered closed.
//  - So long that the installer hasn't called edbs_jobterm, either side
//    cannot write to the buffer without first reading all bytes the
//    opposing side has written.
//
// An edge case is if edbs_jobterm is called in the middle of edbs_jobwrite:
// in such case, edbs_jobwrite will return 0 despite it having
// possibility written bytes to the buffer.
//
// Another edge case is if edbs_jobterm is called by the executor while
// there's still bytes in the buffer that the installer has yet to read (in
// bi-directional mode). In such case, edbs_jobterm will block until these
// stray bytes are read. This is decided as to delay setting the transfer
// buffer as "closed" prematurely that would otherwise cause other jobs being
// installed over it. This edge case is not withstanding critical errors.
//
// RETURNS (all of them are logged)
//  - EDB_EINVAL - buff is 0 and count is not 0.
//  - EDB_EBADE - The executor tried to write first (before the installer), or,
//                the installer tries to perform a read as its first operation.
//  - EDB_EOPEN  - The installer called edbs_jobterm before its first write
//  - EDB_EPROTO - edbs_jobwrite called without first reading other side's
//                 bytes in bi-directional mode (due to failing to following
//                 protocol specs). This can also occur if one side reads
//                 and another side writes but their respective calls do
//                 not have the same counts.
//  - EDB_ECLOSED - the installer has tried to call read after successfully
//                  calling term
//  - EDB_EPIPE - read/write has been interupted/ignored because executor has
//                just called - or previously called - jobterm.
//  - EDB_ECRIT - the pipe was broken for unexpected reasons (ie: other side
//                no longer responding/bad network) and thus can no longer
//                read/write and there's no recovering.
//
// TREADING
//   For a given job, exclusively 1 thread must hold the installer role and
//   exclusively 1 thread must hold the executor role.
//
edb_err edbs_jobread(edbs_job_t j, void *buff, int count);
edb_err edbs_jobwrite(edbs_job_t j, const void *buff, int count);
edb_err edbs_jobterm(edbs_job_t j);


// returns the job description (see odbh_job)
int edbs_jobdesc(edbs_job_t j);

#endif