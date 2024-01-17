#ifndef _edbJOBS_H_
#define _edbJOBS_H_

#include <oidadb/oidadb.h>
#include "edbs.h"

#define EDB_OID_OP1 0x0000ffffffffffff
#define EDB_OID_OP2 0x0000fffffffffffe

// For ODB_CCREATE
// EDB_OID_AUTOID - find the best deleted OID that can still be used.
#define EDB_OID_AUTOID EDB_OID_OP1


// See spec/edbs-jobs.org
typedef enum odb_jobtype_t {

	_ODB_JNONE // 0 is not a valid job id.

	// Objects
	, ODB_JALLOC
	, ODB_JFREE
	, ODB_JWRITE
	, ODB_JREAD
	, ODB_JSELECT
	, ODB_JUPDATE

	// Structure
	, ODB_JSTKCREATE
	, ODB_JSTKDELETE

	// Entities
	, ODB_JENTCREATE
	, ODB_JENTDELETE

	// download
	, ODB_JENTDOWNLOAD
	, ODB_JSTKDOWNLOAD

	// Dynamics

	// Misc.

	// This one is undocumented, used for testing purposes.
	// installer will write and int for the count of bytes,
	// the install will then write said bytes
	// the executor will read those bytes, and write back to the installer
	// the same count/bytes
	, ODB_JTESTECHO
} odb_jobtype_t;

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
//   - ODB_ECLOSED - there are no jobs available AND edbs_host_close has been
//                   called, thus no more job installs are possible. This
//                   means that edbs_jobselect will only block so long that
//                   the host of the shm is running and accepting jobs.
//                   Futhermore, edbs_jobselect will return even after
//                   edbs_host_close has been called so long that there's jobs
//                   that need to be doing.
//   - ODB_EINVAL - (crit logged) ownerid is 0.
//   - ODB_ECRIT
odb_err edbs_jobselect(const edbs_handle_t *shm,
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
//  - ODB_ECLOSED - The host is closed, or is in the process of closing. So
//                  no new jobs are being accepted.
//  - ODB_EJOBDESC - odb_jobtype_t is not valid
//  - ODB_ECRIT
odb_err edbs_jobinstall(const edbs_handle_t *shm,
                        odb_jobtype_t jobclass,
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
//    - if the executor then calls edbs_jobterm, read will return 0
//      immediately if read buffer is empty.
//  - The installer must perform the first operation and that operation must
//    be write. The installer can only call
//    edbs_jobterm before this write.
//  - Once the executer calls edbs_jobterm:
//    - installer calls to read (provided no more bytes are present in the
//      buffer) will return ODB_EEOF
//    - installer calls to write will return 0.
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
// However, note that when edbs_jobterm is called and depending on the backend
// of the transferbuffer, edbs_jobterm will attempt to "flush" as many bytes
// as it can to the installer so edbs_jobterm will not block the executor
// despite the installer not having yet read all the bytes at that time. This
// will only work when the backing medium of the job pipe has buffers on
// either side (will not work in shmpipe mode).
//
// Yet another edge case is if the executor has called edbs_jobterm after
// writting say 5 bytes, and if the installer tries to read the buffer
// expecting 10 bytes, then ODB_EEOF is still returned (this will
// generate a log_warn)
//
// The -v variants of edbs_jobreadv & edbs_jobwritev are to easily chain
// together multiple reads/writes. Simply add the arguemtns pairs  of (void *,
// int) over and over again, to denote the end of the arguments, supply a 0
// for the subsequent argument.
//
// If edbs_jobread, edbs_jobwrite is called with a 0 count, then they will return instantly,
// without error, as if they were not called at all.
//
// RETURNS (all of them are logged)
//  - ODB_EINVAL - (first) buff is 0 and count is not 0.
//  - ODB_EBADE - The executor tried to write first (before the installer), or,
//                the installer tries to perform a read as its first operation.
//  - ODB_EOPEN  - The installer called edbs_jobterm before its first write
//  - ODB_EPROTO - edbs_jobwrite called without first reading other side's
//                 bytes in bi-directional mode (due to failing to following
//                 protocol specs). This can also occur if one side reads
//                 and another side writes but their respective calls do
//                 not have the same counts.
//  - ODB_ECLOSED - the caller has tried call read after successfully
//                  calling term.
//  - ODB_EPIPE - write has been ignored because executor has
//                just called - or previously called - jobterm.
//  - ODB_EEOF  - Installer tried to read on a pipe that has no more bytes in
//                it and executor has called edbs_jobterm. Or, installer
//                tried to read on a pipe that has bytes but not equal to count.
//                IMPORTANT: the -v variants will only return this only if
//                the first buff/count pair fits this condition, otherwise,
//                it is accounted for as no-error for subsequent buff/count
//                paris (see notes)
//  - ODB_ECRIT - the pipe was broken for unexpected reasons (ie: other side
//                no longer responding/bad network) and thus can no longer
//                read/write and there's no recovering.
//  - edbs_joberr_trunc - this symbol mearly makes it so that all of the
//                above errors exclusing ODB_EPIPE be converted to ODB_ECRIT.
//                The purpose of this is to mask all "programmer fault"
//                errors. This will also output into log_crit if a new
//                ODB_ECRIT is generated.
//
// TREADING
//   For a given job, exclusively 1 thread must hold the installer role and
//   exclusively 1 thread must hold the executor role.
//
// NOTES
//  regarding the edbs_jobreadv return ODB_EEOF. Each pair is executed
//  atomically. If the first pair returns ODB_EEOF then thats the fault on
//  the caller for not following protocol. However, what can happen are
//  dieerrors can be returned in the pipe, and then the pipe will be
//  terminated afterward. It is up to the caller to make sure that when using
//  edbs_jobreadv that they always read the die error in the first pairing
//  as to detect if they should trust subsequent pairings. If I were to allow
//  ODB_EEOF be returned in subsequent pairings, then there's no way to
//  detect if dieerror had been successfully read.
odb_err edbs_jobread(edbs_job_t j, void *buff, int count);
odb_err edbs_jobreadv(edbs_job_t j, ...);
odb_err edbs_jobwrite(edbs_job_t j, const void *buff, int count);
odb_err edbs_jobwritev(edbs_job_t j, ...);
odb_err edbs_jobterm(edbs_job_t j);
static odb_err edbs_joberr_trunc(odb_err err) {
	switch (err) {
		case ODB_EPIPE:
		case ODB_ECRIT:
			return err;
		default:
			log_critf("unhandled stream error: %d", err);
			return ODB_ECRIT;
	}
};


// returns the job description (see odbh_job)
odb_jobtype_t edbs_jobtype(edbs_job_t j);

#endif