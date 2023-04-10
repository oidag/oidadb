/*
 * LICENSE
 *
 * Copyright Property of Kevin Marschke, all rights reserved.
 * Copying, modifying, sharing is strictly prohibited. Use allowed by exclusive
 * written notice.
 */
/*
 * This file is NOT met to be a manual/documentation. See the documentation
 * files.
 */
#ifndef _EDB_H_
#define _EDB_H_
#include <stdint.h>
#include <syslog.h>
#include <sys/user.h>

#define export __attribute__((__visibility__("default")))

/** @name ID Types
 * Note that these typedefs are by-definition. These will be the same across
 * all builds of all architectures.
 *
 * \{
 */
/// dynamic data pointer
typedef uint64_t odb_dyptr;
/// (o)bject (id)
typedef uint64_t odb_oid;
/// (s)tructure (id)
typedef uint16_t odb_sid;
/// (e)ntity (id)
typedef uint16_t odb_eid;
/// (r)ow (id)
typedef uint64_t odb_rid;
/// (p)age (id)
typedef uint64_t odb_pid;
///\}

typedef struct odbh odbh;

typedef enum odb_err {
	ODB_ENONE = 0,
	ODB_ECRIT = 1,

	/// invalid input. You didn't read the documentation. (This error is your
	/// fault)
	ODB_EINVAL,

	/// Handle is closed, was never opened, or just null when it
	/// shoudn't have been.
	ODB_ENOHANDLE,

	/// Something went wrong with the handle.
	ODB_EHANDLE,
	
	/// Something does not exist.
	ODB_ENOENT,

	/// Something already exists.
	ODB_EEXIST,

	/// End of file/stream.
	ODB_EEOF,

	/// Something wrong with a file.
	ODB_EFILE,

	/// Not a oidadb file.
	ODB_ENOTDB,

	/// Something is already open.
	ODB_EOPEN,

	/// Something is closed.
	ODB_ECLOSED,

	/// No host present.
	ODB_ENOHOST,

	/// Out of bounds.
	ODB_EOUTBOUNDS,

	/// System error, errno(3) will be set.
	ODB_EERRNO,

	/// Something regarding hardware has gone wrong.
	ODB_EHW,

	/// Problem with (or lack of) memory.
	ODB_ENOMEM,

	/// Problem with (lack of) space/disk space
	ODB_ENOSPACE,

	/// Something is stopping.
	ODB_ESTOPPING,

	/// Try again, something else is blocking
	ODB_EAGAIN,

	/// Operation failed due to user-specified lock
	ODB_EULOCK,

	/// Something was wrong with the job description
	ODB_EJOBDESC,

	/// Invalid verison
	ODB_EVERSION,

	/// Something has not obeyed protocol
	ODB_EPROTO,

	/// Bad exchange
	ODB_EBADE,

	/// Something happened to the active stream/pipe
	ODB_EPIPE,

	/// Something was missed
	ODB_EMISSED,

	ODB_EDELETED
} odb_err;
export const char *odb_errstr(odb_err error);

typedef enum odb_logchannel_t {
	ODB_LCHAN_CRIT  = 1,
	EDB_LCHAN_ERROR = 2,
	EDB_LCHAN_WARN  = 4,
	EDB_LCHAN_INFO  = 8,
	EDB_LCHAN_DEBUG = 16,
} odb_logchannel_t;
typedef void(odb_logcb_t)(odb_logchannel_t channel, const char *log);
export odb_err odb_logcb(odb_logchannel_t channelmask, odb_logcb_t cb);

typedef struct odb_createparams_t {
	uint16_t page_multiplier;
	uint16_t structurepages;
	uint16_t indexpages;
} odb_createparams_t;

static const odb_createparams_t odb_createparams_defaults = (odb_createparams_t){
		.page_multiplier = 2,
		.indexpages = 32,
		.structurepages = 32,
};
export odb_err odb_create(const char *path, odb_createparams_t params);
export odb_err odb_createt(const char *path, odb_createparams_t params);


typedef struct odb_hostconfig_t {
	unsigned int job_buffq;
	unsigned int job_transfersize;
	unsigned int event_bufferq;
	unsigned int worker_poolsize;
	unsigned int slot_count;

	// Reserved.
	int flags;

} odb_hostconfig_t;
static const odb_hostconfig_t odb_hostconfig_default = {
		.job_buffq = 16,
		.job_transfersize = PAGE_SIZE,
		.event_bufferq = 32,
		.worker_poolsize = 4,
		.slot_count = 16,
		.flags = 0,
};
export odb_err odb_host(const char *path, odb_hostconfig_t hostops);
export odb_err odb_hoststop();

typedef unsigned int odb_hostevent_t;
#define ODB_EVENT_HOSTED 1
#define ODB_EVENT_CLOSED 2
#define ODB_EVENT_ANY (odb_event)(-1)
export odb_err odb_hostpoll(const char  *path
		, odb_hostevent_t  event
		, odb_hostevent_t *o_env);
export odb_err odb_handle(const char *path, odbh **o_handle);
export void    odb_handleclose(odbh *handle);




typedef uint8_t odb_type;
#define ODB_ELMINIT  0
#define ODB_ELMDEL   1
#define ODB_ELMSTRCT 2
#define ODB_ELMOBJ   3
#define ODB_ELMENTS  4
#define ODB_ELMPEND  5
#define ODB_ELMLOOKUP 6
#define ODB_ELMDYN 7





////////////////////////////////////////////////////////////////////////////////
/** \defgroup odbh The OidaDB Handle
 * \brief All functions provided to the database handles while connected.
 *
 * ## THREADING -> VOLATILITY
 * Unlike `odb_*` functions which disclaim, a general definition of threading
 * has been specified in \ref odb_handle "odb_handle's threading" chapter.
 * To restate: all of these functions are to be executed on the same thread
 * that had loaded the handle via \ref odb_handle.
 *
 * So instead, we will be focusing on these functions *Volatility*... that is
 * how this function may interact with other concurrently executing handles.
 * Ie., we will discuss the volatility in the event where Handle A is writing
 * and Handle B is reading: will Handle B's results be effected by Handle A?
 *
 * \see odb_handle
 *
 * \{
 */

/** \brief Get index data
 *
 * Write index information into `o_entry` with the given `eid`.
 *
 * If a certain entry is undergoing an exclusive-lock job, the function call
 * is blocked until that operation is complete.
 *
 * ## VOLATILITY
 *
 * When this function returns, `o_entry` may already be out of date if
 * another operation managed to update the entry's contents.
 *
 * ## ERRORS
 *
 *   ODB_EEOF - eid was too high (out of bounds)
 *
 * \see elements for information on what an entry is.
 */
export odb_err odbh_index(odbh *handle, odb_eid eid, const void **o_entry);

/** \brief Get structure data
 *
 * Write index information into `o_entry` with the given `eid`.
 *
 * If a certain entry is undergoing an exclusive-lock job, the function call
 * is blocked until that operation is complete.
 *
 * ## VOLATILITY
 *
 * When this function returns, `o_struct` may already be out of date if
 * another operation managed to update the structure contents.
 *
 * ## ERRORS
 *
 *   ODB_EEOF - sid was too high (out of bounds)
 *
 * \see elements to for information as to what a structure is.
 */
export odb_err odbh_structs(odbh *handle, odb_sid structureid, void *o_struct)
; //
// todo: what is o_struct?

/**
 * \brief Command enumeration
 *
 * This enum alone doesn't describe much. It depends on the context of which
 * odbh function is being used.
 */
typedef enum odb_cmd {
	ODB_CNONE   = 0x0000,
	ODB_CREAD   = 0x0100,
	ODB_CWRITE  = 0x0200,
	ODB_CCREATE = 0x0300,
	ODB_CDEL    = 0x0400,
	ODB_CUSRLKR  = 0x0500,
	ODB_CUSRLKW  = 0x0600,
	ODB_CSELECT  = 0x0700,
} odb_cmd;

// odb_jobclass must take only the first 4 bits. (to be xor'd with
// edb_cmd).
typedef enum odb_jobclass {

	// means that whatever job was there is now complete and ready
	// for a handle to come in and install another job.
	EDB_JNONE = 0x0000,

	// structure ops
	//
	//   (ODB_CWRITE: not supported)
	//   ODB_CCREATE:
	//     <- edb_struct_t (no implicit fields)
	//     -> odb_err (parsing error)
	//     <- arbitrary configuration
	//     -> uint16_t new structid
	//   ODB_CDEL:
	//     <- uint16_t structureid
	//     -> odb_err
	EDB_STRUCT = ODB_ELMSTRCT,

	// dynamic data ops
	// valuint64 - the objectid (0 for new)
	// valuint   - the length of that data that is to be written.
	// valbuff   - the name of the shared memory open via shm_open(3). this
	//             will be open as read only and will contain the content
	//             of the object. Can be null for deletion.
	EDB_DYN = ODB_ELMDYN,

	// Perform CRUD operations with a single object.
	// see edb_obj() for description
	//
	// all cases:
	//     <- odb_oid (see also: EDB_OID_... constants)
	//     (additional params, if applicable)
	//     -> odb_err [1]
	//  ODB_CREAD:
	//     (all cases)
	//     -> void *rowdata
	//     ==
	//  ODB_CWRITE:
	//     (all cases)
	//     <- void *rowdata
	//     ==
	//  ODB_CCREATE:
	//     (all cases)
	//     // todo: what if they think the structure is X bytes long and sense has been updated and thus now is X+/-100 bytes long?
	//     <- void *rowdata (note to self: keep this here, might as well sense we already did the lookup)
	//     -> created ID
	//     ==
	//  ODB_CDEL
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
	//       - ODB_EHANDLE - handle closed stream/stream is invalid
	//       - ODB_EINVAL - entry in oid was below 4.
	//       - ODB_EINVAL - jobdesc was invalid
	//       - ODB_EINVAL - (ODB_CWRITE) start wasn't less than end.
	//       - ODB_ENOENT - (ODB_CWRITE, ODB_CREAD) oid is deleted todo:
	//        todo: change this to entity not valid
	//       - ODB_EOUTBOUNDS - (ODB_CWRITE): start was higher than fixedlen.
	//       - ODB_EULOCK - failed due to user lock (see EDB_FUSR... constants)
	//       - ODB_EEXIST - (ODB_CCREATE): Object already exists
	//       - ODB_ECRIT - unknown error
	//       - ODB_ENOSPACE - (ODB_CCREATE, using AUTOID): disk/file full.
	//       - ODB_EEOF - the oid's entry or row was larger than the most possible value
	//
	EDB_OBJ = ODB_ELMOBJ,

	// Modify the entries, updating the index itself.
	//
	// EDB_ENT | ODB_CCREATE
	//   <- edb_entry_t entry. Only the "parameters" group of the structure is used.
	//      This determains the type and other parameters.
	//   -> odb_err error
	//   -> (if no error) uint16_t entryid of created ID
	//
	// EDB_ENT | ODB_CDEL
	//   <- uint16_t entryid of ID that is to be deleted
	//   -> odb_err error
	//
	EDB_ENT = ODB_ELMENTS,

} odb_jobclass;


// OR'd together values from odb_jobclass and odb_cmd
typedef enum odb_jobdesc {

	// Objects
	ODB_JCREATE
	, ODB_JDELETE
	, ODB_JWRITE
	, ODB_JREAD
	, ODB_JSELECT
	, ODB_JUPDATE

	// Structure
	, ODB_JSTRCTCREATE
	, ODB_JSTRCTDELETE

	// Entities

	// Dynamics

} odb_jobdesc;


/**
 * \brief User lock bit constants
 */
typedef enum odb_usrlk {

	/// Object cannot be deleted.
	EDB_FUSRLDEL   = 0x0001,

	/// Object cannot be written too.
	EDB_FUSRLWR    = 0x0002,

	/// Object cannot be read.
	EDB_FUSRLRD    = 0x0004,

	/// Object cannot be created, meaning if this
	/// object is deleted, it will stay deleted.
	/// If not already deleted, then it cannot be
	/// recreated once deleted.
	///
	/// This will also make it implicitly unable to be used
	/// with creating via an AUTOID.
	EDB_FUSRLCREAT = 0x0008,

	_EDB_FUSRLALL = 0x000F,
} odb_usrlk;



// todo: I would like to this function to always return immediately. Need to
//  find a clever way of doing returns. Maybe like a "poll return"... perhaps
//  you can submite 40 jobs all at once and then poll all the returns?
/***
\brief Install a job.

Here be the most important function of this entire library: the ability to
install a job. The host maintains a buffer of jobs and workers in the host
will scan that buffer and execute said jobs.

This function's signature is analogous to `fcntl(2)`: in which the first
argument (`handle`) is the handle, second argument (`jobclass`) is the
'command' and depending on the command dictates `args`.

 - The job buffer is full and thus this function must wait until
   other jobs complete for a chance of getting into the buffer.

`jobclass` is an XOR'd together integer with the following combinations:

## `ODB_ELMOBJ | ODB_CREAD` (odb_oid id, voido_bufv, int bufc, int offset)
    Read the contents of the object with `id` into `o_bufv` up to `bufc`
    bytes.
    todo: see above comment

## `ODB_ELMOBJ | ODB_CWRITE` (odb_oid id, voidbufv, int bufc, int offset)
    Write bytes stored at `bufv` up to `bufc` into the object with `id`.

## `ODB_ELMOBJ | ODB_CCREATE` (odb_oid *o_id, void *bufv, int bufc, int offset)
   Create a new object

   During creation, the new object is written starting at `offset` and
   writes `bufc` bytes and all other bytes are initializes as 0.


## EDB_CUSRLK
   Install a persisting user lock. These locks will affect future calls to
   odbh_obj

\param jobclass This is a XOR'd value between one \ref odb_type and one \ref
                odb_cmd

## ERRORS

 - ODB_EINVAL - handle is null or uninitialized
 - ODB_EJOBDESC - odb_jobdesc is not valid
 - ODB_ECLOSED - Host is closed or is in the process of closing.
 - \ref ODB_ECRIT

## VOLATILITY
Fuck.



\see odbh_structs
 */

// todo: replace this with what is documented.
typedef enum odb_jobhint_t {
	a
} odb_jobhint_t;
export odb_err odbh_jobmode(odbh *handle, odb_jobhint_t hint);

// todo:
typedef uint64_t odb_jobtype_t;
export odb_err odbh_job   (odbh *handle, odb_jobtype_t jobtype);
export odb_err odbh_jwrite(odbh *handle, const void *buf, int bufc);
export odb_err odbh_jread (odbh *handle, void *o_buf, int bufc);
export odb_err odbh_jclose(odbh *handle);


/**
\brief Install write-jobs regarding 1 structure



ODB_CWRITE (edb_struct_t *)

 Create, update, or delete structures. Creation takes place when id
 is 0 but binv is not null. Updates take place when id is not 0 and
 if one of the fields is different than the current value: binc,
 fixedc, data_ptrc. Deleteion takes place when id is not 0, fixedc
 is 0, and data_ptrc is 0.

 During creation, the new structure's configuration is written
 starting at binoff and writes binc bytes and all other bytes are
 initializes as 0. During updating, only the range from binoff to
 binoff+binc is modified.  binoff and binc are ignored for
 deletion.

 Upon successful creation, id is set.

 During updates and deletes, the structure is placed under a write
 lock, preventing any read operations from taking place on this
 same id.


 \see odbh_structs For reading structures
 \see elements to find out what an "structure" is.
*/
export odb_err odbh_struct (odbh *handle, odb_cmd cmd, int flags, ... /* arg
 * */);


//
// Thread safe.
//
/*odb_err edb_query(odbh *handle, edb_query_t *query);*/

typedef struct edb_select_st {
} edb_select_t;
/** \brief select/poll from the database event buffer

Listens for changes in the database by instrunctions set forth in
params.

This function blocks the calling thread and will return once a new
change is detected.

Limit 1 call to odbh_select to each handle. If you attempt to call
odbh_select asyncrounously with the same handle, this will cause an
error.

If this function is called too infrequently then there's a
possibility that the caller will miss certain events. But this is a
very particular edge case that is described elsewhere probably.
 */
export odb_err odbh_select(odbh *handle, edb_select_t *params);

/** \} */ // odbh






/********************************************************************
 *
 * Object reading and writting.
 *
 ********************************************************************/

typedef struct _edb_data {

	// note this id consists of the row and structure id.
	uint64_t id;
		
	unsigned int binc;
	unsigned int binoff;
	// note to self: while transversing between processes, the pointer
	// must be somewhere in the shared memory.
	// note to self: this will always be null inside of edb_job_t
	//    what must happen is reading from jobdataread.
	void        *binv;

} edb_data_t;











/********************************************************************
 *
 * Data reading and writting.
 *
 ********************************************************************/

// See [[RW Conjugation]]
//
// Reads and writes data to the database.
//
/*odb_err edb_datcopy (odbh *handle, edb_data_t *data);
odb_err edb_datwrite(odbh *handle, edb_data_t data);*/





/********************************************************************
 *
 * Query functions
 *
 ********************************************************************/

typedef struct edb_event_st {
	int filler;
} edb_event_t;


typedef struct edb_query_st {

	uint16_t eid;  // entry id (must be edb_new)
	
	int pagec;     // The max amount of sequencial pages to look
				   // through. Leave this to 0 to disable the use of
				   // pagec and pagestart
	
	int pagestart; // The page index to which to start
	
	int (*queryfunc)(edb_data_t *row); // query function. the
										 // pointer to row is not safe
										 // to save.
} edb_query_t;






/********************************************************************
 *
 * Debugging functions.
 *
 ********************************************************************/

typedef struct edb_infohandle_st {
} edb_infohandle_t;

typedef struct edb_infodatabase_st {
} edb_infodatabase_t;


// all these info- functions just return their relevant structure's
// data that can be used for debugging reasons.
export odb_err edb_infohandle(odbh *handle, edb_infohandle_t *info);
export odb_err edb_infodatabase(odbh *handle, edb_infodatabase_t *info);

// dump- functions just format the stucture and pipe it into fd.
// these functions provide no more information than the equvilient
// info- function.
export int edb_dumphandle(odbh *handle, int fd);
export int edb_dumpdatabase(odbh *handle, int fd);
	
#endif // _EDB_H_
