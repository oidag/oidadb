#ifndef _edbJOBS_H_
#define _edbJOBS_H_

#include "include/ellemdb.h"
#include "edbs.h"

#define EDB_OID_OP1 0x0000ffffffffffff
#define EDB_OID_OP2 0x0000fffffffffffe

// For EDB_CCREATE
// EDB_OID_AUTOID - find the best deleted OID that can still be used.
#define EDB_OID_AUTOID EDB_OID_OP1

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

	// Perform CRUD operations with a single object.
	// see edb_obj() for description
	//
	// All cases:
	//     <- edb_oid (see also: EDB_OID_... constants)
	//     (additional params, if applicable)
	//     -> edb_err [1]
	//  EDB_CCOPY:
	//     (all cases)
	//     -> void *rowdata
	//     ==
	//  EDB_CWRITE:
	//     (all cases with ADDITIONAL PARAMS:)
	//        <- uint32 start (must be less than end, must be less than fixedlen)
	//        <- uint32 end (will be clamped to fixedlen, so set to (uint32)-1 for all)
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
	//  EDB_CUSRLK: todo: need to write out edbl before getting any deeper into this. I don't think persistant user locks are needed.
	//     (all cases)
	//     <- edb_usrlk
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
	//
	EDB_OBJ = 0x0003,

} edb_jobclass;

/*
 * typedef enum edb_cmd_em {
	EDB_CNONE  = 0x0000,
	EDB_CCOPY  = 0x0100,
	EDB_CWRITE = 0x0200,
	EDB_CLOCK  = 0x0400,
} edb_cmd;
 */

typedef struct {
	const edb_shm_t *shm;
	edb_job_t *job;
} edbs_jobhandler;

// all this does is build up a helper structure.
// Allows you to use the edbs_jobhandler functions
edbs_jobhandler edbs_jobhandle(const edb_shm_t *shm, unsigned int jobindex) {
	return (edbs_jobhandler){
		.shm = shm,
		.job = &shm->jobv[jobindex],
	};
}

// todo: put job install and job select here.

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
int edb_jobread(edbs_jobhandler *jh, void *buff, int count);
int edb_jobwrite(edbs_jobhandler *jh, const void *buff, int count);


#endif