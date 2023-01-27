/**
 * \file
 * \brief The only header file needed to do everything with oidadb databases.
 *
 *
 */
#ifndef _EDB_H_
#define _EDB_H_ 1

#include <stdint.h>
#include <sys/fcntl.h>


/** @name ID Types
 * Note that these typedefs are by-definition. These will be the same across
 * all builds of all architectures.
 *
 * \{
 */
/// dynamic data pointer
typedef uint64_t edb_dyptr;
/// (o)bject (id)
typedef uint64_t edb_oid;
/// (s)tructure (id)
typedef uint16_t edb_sid;
/// (e)ntry (id)
typedef uint16_t edb_eid;
/// (r)ow (id)
typedef uint64_t edb_rid;
/// (p)ow (id)
typedef uint64_t edb_pid;
///\}

typedef struct edbh edbh;
typedef struct edb_job_st edb_job_t;

/********************************************************************
 *
 * Prefix (no code, just comments)
 *
 ********************************************************************/

// RW Conjugation
//
// Function groups that use this conjugation all have 2 parameters,
// the first parameter being a pointer to the handle and the second
// being a structure of the specific item. -copy will be
// pass-by-refrence in the second parameter.
//
//  -copy(*handle, *struct)
//  -write(*handle, struct)
//
// The second parameter's structure will contain a void pointer named
// "binv" (array of binary) as well as an id simply called "id". It
// may also contain some other fields not covered in this
// description. But the behaviour of binv will be dictated by the
// choice of the conjugation.
//
// This conjugation contains -copy and -write.
//
//   -copy will copy binc bytes into memory pointed by binv. Thus, it
//   is up to the caller to manage that memory. Obtaining the proper
//   binc varies depending on the use of this conjugation.
//
//   -write binv should point to the data to which will be written to
//   the database. Write also takes into account id to determain the
//   nature of the operation:
//
//     - creating: id  = 0, binv != 0
//     - updating: id != 0, binv != 0
//     - deleting: id != 0, binv  = 0
//
// THREADING: -copy and -write are both threadsafe. The exact
//   mechanics of how this obtained depends on the use of this
//   conjugation. But the caller can rest assured that they are thread
//   safe.
//
//   Note that subsequent -copy calls can yeild different results due
//   to interweaving -write calls. Furthermore, structure values
//   passed through -write are not guarnteed to be returned by
//   subsequent -read calls, even on the same thread. As long as the
//   -write call is valid, it will only be scheduled, it may take time
//   for it to fully process.




/**
 *
 * \defgroup errors Errors
 * \brief 50% of your job is handling errors; the other 50% is making them.
 *
 * Nearly all functions in OidaDB will return an edb_err. If this is not 0
 * then this indicates an error has ocoured. The exact nature of the value
 * depends on the function.
 *
 * edb_err itself represents an enum of very general errors. Again, their
 * exact practical purpose depends on which function returned them.
 *
 * With the exception of \ref EDB_CRITICAL. This will mean the same thing
 * every time.
 *
 * \see Logs
 *
 * \{
 *
 */
typedef enum edb_err {

	/// no error - explicitly 0 - as all functions returning edb_err is
	/// expected to be layed out as follows for error handling:
	///
	///```
	///   if(err = edb_func())
	///   {
	///        // handle error
	///   } else {
	///        // no error
	///   }
	///   // regardless of error
	///```
	EDB_ENONE = 0,

	/// critical error: not the callers fault.
	/// You should never get this.
	///
	/// If you do, try what you did again and look closely at the \ref Logs -
	/// Everytime this is returned, a \ref EDB_LCRIT message would have been
	/// generated. You should probably send this message to the maintainers
	/// and steps to reproduce it.
	EDB_ECRIT = 1,

	/// invalid input. You didn't read the documentation. (This error is your
	/// fault)
	EDB_EINVAL,

	/// Handle is closed, was never opened, or just null when it
	/// shoudn't have been.
	EDB_ENOHANDLE,

	/// Something went wrong with the handle.
	EDB_EHANDLE,
	
	/// Something does not exist.
	EDB_ENOENT,

	/// Something already exists.
	EDB_EEXIST,

	/// End of file/stream.
	EDB_EEOF,

	/// Something wrong with a file.
	EDB_EFILE,

	/// Not a oidadb file.
	EDB_ENOTDB,

	/// Something is already open.
	EDB_EOPEN,

	/// No host present.
	EDB_ENOHOST,

	/// Out of bounds.
	EDB_EOUTBOUNDS,

	/// System error, errno(3) will be set.
	EDB_EERRNO,

	/// Something regarding hardware has gone wrong.
	EDB_EHW,

	/// Problem with (or lack of) memory.
	EDB_ENOMEM,

	/// Problem with (lack of) space/disk space
	EDB_ENOSPACE,

	/// Something is stopping.
	EDB_ESTOPPING,

	/// Try again, something else is blocking
	EDB_EAGAIN,

	/// Operation failed due to user-specified lock
	EDB_EULOCK,

	/// Something was wrong with the job description
	EDB_EJOBDESC,
} edb_err;

/**
 * \brief Returns the string representation of the error. Good for logging.
 */
const char *edb_errstr(edb_err error);
/**\}*/

#include <syslog.h>
#include <sys/user.h>

/**
 * \defgroup telem_basic Logs
 * \brief Basic telemetry information.
 *
 * Log messages can be generated by any thread at any time for any reason.
 * Their purpose depends on which \ref edb_log_channel "channel" they come
 * through.
 *

 *
 * edb_setlogger sets the logger for the handle. The callback must be
 * threadsafe. The callback will only be called on the OR'd bitmask
 * specified in logmask (see EDB_L... enums). This function is simular
 * to syslog(3). Setting cb to null will disable it.
 * logmask. See syslog(3)'s "option" argument.
 *
 * \see Errors
 *
 * \{
 */

/**
 * Notice the simularities between this enum and syslog.
 */
typedef enum edb_log_channel {

	/// Messages that have been generated when something that
	/// definitely wasn't suppose to happen has happened. The purpose of this
	/// channel is to inform you of *our* mistakes.
	///
	/// Messages sent through here will be because of some crazy edge case
	/// the developer did not deem possible. If you see a message pass
	/// through this channel, please contact the developers with the messages
	/// and any possible information you may have.
	EDB_LCRIT =     LOG_CRIT,

	/// Error messages are generated when something happened when it
	/// objectively shouldn't have *because of you*. The purpose of this
	/// channel is to inform you of *your* mistakes.
	EDB_LERROR = LOG_ERR,

	/// Warnings is anything that is supposed to happen, but should be
	/// avoided from happening for one reason or the other.
	EDB_LWARNING =  LOG_WARNING,

	/// Info messages also known as "verbose" messages. This will describe
	/// pretty much everything that is going on. Regardless of its importance
	/// to you.
	///
	/// This maybe disabled on some builds.
	EDB_LINFO =     LOG_INFO,

	/// Debugging messages will be generated when something non-obvious that
	/// can  impact predictability has happened.
	///
	/// This maybe disabled on some builds.
	EDB_LDEBUG =    LOG_DEBUG, // debug message. may be redundant/useless
} edb_log_channel;

/**
 *
 * Set all messages sent via `logmask` to invoke the callback method
 * `cb`. Only one callback method can be set at once per handle.
 *
 * This function will work regardless of the open state of the `handle`
 * so long that it is at least initialized.
 *
 * \param logmask This is an XOR'd bitmask of 1-to-many \ref edb_log_channel. If
 * 0, then `cb` will never be called.
 *
 * \param cb When invoked, `logtype` will specify which exact channel the
 * `log` was sent through.
 *
 * ## THREADING
 * This function can be called on any thread. And keep in mind that `cb` will
 * be invoked by any random thread.
 */
void edb_setlogger(edbh *handle, unsigned int logmask,
                   void (*cb)(edb_log_channel logtype, const char *log));
/// \}



/********************************************************************
 *
 * Database creating, opening and closing
 *
 ********************************************************************/

/**
 * \defgroup database_io Database Creating/Deleting
 * \brief All the info about creating and deleting database files. Then the
 *        subsequent opening and closing.
 *
 * The first step to the workflow for oidadb starts on the premise of simple
 * file IO. A single database is stored in a single file. So creating a
 * database is will intern create a file. And deleting a database will intern
 * delete said file.
 *
 * With that being said, to create a new database see \ref odb_create. This
 * will create the file itself (or use an existing file in some cases) and
 * initialize that file to be a oidadb database.
 *
 *
 * And to delete a database... well you can just delete the file.
 *
 * \see odb_host
 * \see odb_handle
 *
 * \{
 */

/**
 * \brief Parameters used for creation-time in the oidadb lifecycle.
 */
typedef struct odb_createparams {

	/**
	 * \brief Database Page multiplier
	 *
	 * This is a multiplier for the overall page size of the database. This
	 * cannot be changed after the database's creation.
	 *
	 * Must be either 1, 2, 4, or 8.
	 *
	 * If you don't know what this is or how databases work, set it to 2 and
	 * move on.
	 *
	 * ### Advanced
	 *
	 * If you're still reading its because you know exactly what page sizes are
	 * and how they effect databases. So let me derive for you the
	 * information that you should be aware about regarding oidadb compared
	 * to other databases...
	 *
	 * Although larger page sizes are typically associated with faster
	 * database performance (see the mariadb.com article in see also), what
	 * must be considered is the heavy multi-threaded environment that oidadb
	 * is in compared to other databases. A single page can only be accessed
	 * by a single worker at a given time (note though workers can often
	 * take turns accessing a page within the same job). But because of this,
	 * if you have a massive page size, this means a single work will
	 * "lock" more data than it needs too.
	 *
	 * On the other hand, larger page sizes will reduce CPU time due to less
	 * calcuations needed to find pages sense their will be so far fewer of
	 * them.
	 *
	 * If you wish to truly optimize this number, it will take some testing.
	 * But with that said, I can provide you with some heuristics that I'm
	 * sure you'll agree with. I'll split it up into /volume/ (the amount of
	 * bytes read/write) and /frequency/ (the avg. amount of concurrent jobs)
	 * . These metrics completely abstract, though.
	 *
	 *  - For high volume, low frequency: use a large page size
	 *  - For low volume, high frequency: use a small page size.
	 *  - For equal volume and frequency: use a medium page size
	 *
	 * \see https://mariadb.com/resources/blog/does-innodb-page-size-matter/
	 *
	 */
	uint16_t page_multiplier;

	/**
	 * The total number of structure pages the database will have.
	 *
	 * Recommended value is 32 for beginners.
	 *
	 * Note that `indexpages`*`pagesize` bytes of RAM must be available upon
	 * startup.
	 */
	uint16_t structurepages;

	/**
	 * The number of index pages the database will have.
	 *
	 * Recommended value is 32 for beginners.
	 *
	 * Note that `indexpages`*`pagesize` bytes of RAM must be available upon
	 * startup.
	 */
	uint16_t indexpages;

} odb_createparams;

/**
 * \brief Default parameters for a typical beginner.
 */
const odb_createparams odb_createparams_defaults = (odb_createparams){
		.page_multiplier = 2,
		.indexpages = 32,
		.structurepages = 32,
};

/** \name odb_create
 *
 * Create and initialize a file to be an oidadb file. `odb_create` does this
 * by creating a new file all together. `odb_createt` will require the file
 * itself be created, but will truncate its contents.
 *
 * Upon successful execution, the file can then be opened in \ref odb_host
 * and \ref odb_handle.
 *
 * When using odb_create, you must study \ref odb_createparams. As there are
 * many aspects of the database that cannot be changed after creation without
 * a ton of hassle.
 *
 * ## ERRORS
 *   - EDB_EINVAL - params is invalid (see \ref odb_createparams_t)
 *   - EDB_EEXIST (odb_create) - file already exists
 *   - EDB_ENEXIST (odb_createt) - file does not exist
 *   - \ref EDB_ECRIT
 *
 * \{
 */
edb_err odb_create(const char *path, odb_createparams params);
edb_err odb_createt(const char *path, odb_createparams params); // truncate existing
/**\}*/
/**\}*/


/** \defgroup hostshandles Hosts/Handles
 * \brief Everything needed to host a oidadb file, and everything to connect
 * to such host.
 *
 * After a oidadb file has been \ref odb_create "created", it is now ready to
 * be **hosted**. Hosting means to give a process special & exclusive rights to
 * that file. A file can only be attached to a single hosting process at a
 * given time.
 *
 * Once a file is hosted, an unlimited number of processes can connect to the
 * host and establish what are known as **handles** on that file. Handles
 * need to be provided the file location and can then find the host based on
 * hints set by both the file and OS as to what is the host process.
 *
 * Handles can submit jobs, read events, and read telemetry. A host does
 * everything else. Host/handle is a server/client relationship.
 *
 * \{
 */

//
// edb_open errors:
//   EDB_EINVAL - handle is null.
//   EDB_EINVAL - params.path is null
//   EDB_EERRNO - error with open(2), (ie, file does not exist, permssions)
//   EDB_ENOHOST - file is not being hosted (see edb_host)
//   EDB_ENOTDB - file/host is not edb format/protocol
//
// These functions provide access to a database provided by the
// instrunctions set forth in params. edb_open will write to edbh and
// mark it as open so it can be used in all other functions.
//
// There is a race condition to where 2 processes attempt to call
// edb_open with both containing instunctions to start the host
// proccess. In this case, 1 call will succeed and the other
// will have EDB_EOPEN returned. In that case the process that failed
// to open should attempt to run edb_open again.
//
// edb_create will fail if the file already exists.
edb_err odb_handle(edbh *handle, edb_open_t params);   // will create if not existing. Not thread safe.
edb_err odb_handleclose(edbh *handle);


/**
 * \brief All parameters for hosting
 * \see odb_hostconfig_defaults
 */
typedef struct odb_hostconfig_t {

	/** \brief Job buffer size
	 * The host will manage memory that will be shared between
	 * host and handles known as the job buffer. Submitting a job entails
	 * writing to the job buffer.
	 *
	 * Installed jobs will stay there until a worker goes through
	 * and completes that job. If the job buffer fills up, subsequent
	 * calls that interact with this buffer will start blocking.
	 *
	 * The optimal job buffer is related to the worker_poolsize,
	 * the speed of individual cores of the hardware, and the frequency
	 * of expensive operations.
	 *
	 * The job buffer must be greater than 0 and /should/ (but not required)
	 * be equal or larger than worker_poolsize; otherwise, you risk workers
	 * doing nothing but taking up resources.
	 *
	 * A good heuristic here is to have the buffer size equal to the
	 * worker_poolsize squared:
	 *
	 *  `job_buffersize = worker_poolsize * worker_poolsize`
	 */
	unsigned int job_buffq;

	/** \brief Job Transfer Buffer size
	 * The amount of **Transfer Buffer** that is allocated for each job in the
	 * job buffer. The Transfer Buffer acts as the input/output of a given job
	 * between the host and the handle that submitted the job.
	 *
	 * This must be at least 1... but that is very much not
	 * recommended. For maximum efficiency, it's recommend that this
	 * be a multiple of the system page size
	 * (sysconf(_SC_PAGE_SIZE)). For databases that will experience
	 * larger amounts of data transfer, this number should be bigger.
	 * There's no drawback for having this number too big other than
	 * unnecessary allocation of memory.
	 *
	 * For the most part, 1 * sysconf(_SC_PAGE_SIZE) will be suitable
	 * for most applications both big and small. Unless you expect
	 * data to be transferred between host and handle to exceed that.
	 */
	unsigned int job_transfersize;

	/** \brief Event Buffer size
	 * As things happen in the database (something is updated/deleted/ect)
	 * this will constitute as an "event". This event is then stored in the
	 * event buffer which can be read by handles. Handles must read this
	 * buffer before newer events start to replace the older ones to stay up
	 * to date.
	 *
	 * Small buffers save memory but can result in more lost-events
	 * for slower performing handles. There's no scientific way to
	 * completely remove the chance of lost events, at least not in
	 * the functionality of this library alone. This is because the
	 * collection of events by their respective handlers is not
	 * allowed to compromise the efficiency of the database (ie. if 1
	 * handle is being very slow, oidabd is designed to prevent that
	 * slowness from spreading to other handles).
	 *
	 * This must be at least 1. The proper buffer size is directly related
	 * to the amount of operations per second and the speed of the handles.
	 * A good heuristic would be 32 for new users. Once
	 * you start seeing event loss, you should first work on the
	 * efficiency of handles and then look to increasing this number.
	 */
	unsigned int event_bufferq;

	/** \brief Worker pool size
	 * The worker pool count that will be managed to
	 * execute jobs. On paper, the optimial amount of workers is
	 * equal to the number of cores on the hardware (see
	 * get_nprocs(2)). But it is up to you to and your knowledge of
	 * your hardware to decide.
	 *
	 * worker_poolsize must be at least 1. Note that if
	 * worker_poolsize is indeed 1 this will result in no new workers
	 * to be created outside the thread that was used to call
	 * edb_host.
	 */
	unsigned int worker_poolsize;

	/** \brief Slot Count AKA Page Buffer
	 *
	 * Jobs sent to the database will need to move pages to and from
	 * the underlying storage and memory. Thus pages that are moved
	 * into memory are moved into what are known as **slots**.
	 *
	 * slot_count cannot be smaller than worker_poolsize.
	 *
	 * At the time of writting this, I really have no idea what would be the
	 * optimal page buffer. More research needed. Lets just say it'll be 4x
	 * the worker size.... why not.
	 */
	uint32_t slot_count;

	// Reserved.
	int flags;

} odb_hostconfig_t;


/// \brief Default hosting configuration for entry-level developers.
const odb_hostconfig_t odb_hostconfig_default = {
		.job_buffq = 16,
		.job_transfersize = PAGE_SIZE,
		.event_bufferq = 32,
		.worker_poolsize = 4,
		.slot_count = 16,
		.flags = 0,
};

/** \name odb_host
 *
 * \brief Starts hosting a database for the given oidadb file.
 *
 * odb_host will place special locks and hints on the file that will prevent
 * other processes and threads from trying to host it.
 *
 * odb_host will block the calling thread and will only (naturally) return
 * once odb_hosstop is called on a separate thread. Any time a host has been
 * shut down in this manner will return non-errornous.
 *
 * There is a boat load of configuration available, these are discussed
 * inside of \ref odb_hostconifg_t. Use \ref odb_hostconfig_default if this
 * sounds scary though.
 *
 * ## THREADING
 * Both of these functions are MT-safe.
 *
 * The aforementioned write-locks use Open File Descriptors (see fcntl(2)),
 * this means that attempts to open the same file in two separate
 * threads will behave the same way as doing the same with two
 * separate processes. This also means the host can be used in the
 * same process as the handles (though its recommended not to do this on the
 * basis of good engineering of departmentalizing crashes).
 *
 * ## ERRORS
 * odb_host can return:
 *   - EDB_EINVAL - hostops is invalid and/or path is null
 *   - EDB_EERRNO - Unexpected error from stat(2) or open(2), see errno.
 *   - EDB_EOPEN  - Another process is already hosting this file.
 *   - EDB_EFILE  - Path is not a regular file.
 *   - EDB_EHW    - this file was created on a different (non compatible)
 *   architecture and cannot be hosted on this machine.
 *   - EDB_ENOTDB - File is invalid format, possibly not a database.
 *   - EDB_ENOMEM - Not enough memory to reliably host database.
 *   - \ref EDB_ECRIT
 *
 * odb_hoststop can return
 *  - EDB_ENOHOST - no host for file
 *  - EDB_EERRNO - error with open(2).
 *
 *
 * \see odb_create
 * \see odb_hostconfig_t
 * \see odb_hostconfig_default
 * \see odb_handle
 *
 * \{
 */
edb_err odb_host(const char *path, odb_hostconfig_t hostops);
edb_err odb_hoststop(const char *path);
/// \}

/// \}


#define EDB_TINIT  0
#define EDB_TDEL   1
#define EDB_TSTRCT 2
#define EDB_TOBJ   3
#define EDB_TENTS  4
#define EDB_TPEND  5
#define EDB_TLOOKUP 6
typedef uint8_t edb_type;

/********************************************************************
 *
 * Entries
 *
 ********************************************************************/

// see spec.
typedef struct edb_entry_st {

	// paramaters
	edb_type type;
	uint8_t rsvd;
	uint16_t memory;
	uint16_t structureid;

	// cached values
	uint16_t objectsperpage;
	uint16_t lookupsperpage;

	// references
	edb_pid ref0; // starting chapter start
	edb_pid ref1; // lookup chapter start
	edb_pid ref2; // dynamic chapter start
	edb_pid ref0c;
	edb_pid lastlookup;
	edb_pid ref2c;
	edb_pid trashlast;

} edb_entry_t;

typedef struct edb_struct_st {

	//uint16_t     id;   // cant put ids in structures

	uint16_t    fixedc;    // total size (see spec)
	uint16_t    confc;     // configuration size
	uint8_t     flags;     // flags see spec.
	uint8_t     data_ptrc; // data pointer count

	// implicit fields:
	// const uint8_t *subpagesizes; // = (edb_struct_t*) + sizeof(edb_struct_t)
	// const void    *confv;        // = (edb_struct_t*) + sizeof(edb_struct_t) + sizeof(uint8_t) * data_ptrc
} edb_struct_t;

#define EDB_FSTRCT_INIT 0x80;


// shared-memory access functions
//
// todo: these should probably be jobs instead of shared memory, otherwise
//       locking management will be out of wack. ie: what if someones in the
//       middle of an alter statement?
//
// edb_structs and edb_index will take in ids and copy the respective
// information into their o_ parameters. If the o_ parameters are
// null, then they are ignored.
//
// To get all indecies for instance, you'd start at eid=0 and work your
// way up until you get an EOF.
//
// If a certain entry is undergoing an operation, the function call
// is blocked until that operation is complete.
//
// RETURNS:
//
//   EDB_EEOF - eid / sid was out of bounds
//
// Volitility:
//
//   Both structv and entryv are pointers to shared memory that can
//   and will be edited by multiple processes at once at any time. To
//   properly keep track of these changes you should use
//   edb_select. But be aware that structv and entryv's contents will
//   change.
//
// THREADING:
//
//   These functions are completely thread safe. However, if something
//   is making substantial modifications to the chapter, thus locking the
//   entry, these functions will block until that lock is released.
edb_err edb_index(edbh *handle, edb_eid eid, edb_entry_t *o_entry);
edb_err edb_structs(edbh *handle, uint16_t structureid, edb_entry_t *o_struct);


/*edb_err edb_structcopy (edbh *handle, const edb_struct_t *strct);
  edb_err edb_structwrite(edbh *handle, const edb_struct_t strct);*/


// All EDB_C... constants must take
// only the first 2nd set of 4 bits (0xff00 mask)
typedef enum edb_cmd_em {
	EDB_CNONE   = 0x0000,
	EDB_CCOPY   = 0x0100,
	EDB_CWRITE  = 0x0200,
	EDB_CCREATE = 0x0300,
	EDB_CDEL    = 0x0400,
	EDB_CUSRLKR  = 0x0500,
	EDB_CUSRLKW  = 0x0600,
} edb_cmd;

// See spec for more specific details.
// General details provided here is all you
// really need to worry about.
typedef enum _edb_usrlk {

	// Object cannot be deleted.
	EDB_FUSRLDEL   = 0x0001,

	// Object cannot be written too.
	EDB_FUSRLWR    = 0x0002,

	// Object cannot be read.
	EDB_FUSRLRD    = 0x0004,

	// Object cannot be created, meaning if this
	// object is deleted, it will stay deleted.
	// If not already deleted, then it cannot be
	// recreated once deleted.
	//
	// This will also make it implicitly unable to be used
	// with creating via an AUTOID.
	EDB_FUSRLCREAT = 0x0008,

	// Used for normalizing / masking
	_EDB_FUSRLALL = 0x000F,
} edb_usrlk;



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


// Installs a job into the queue that is regarding an object in the
// database. This leaves dynamic data untouched.
//
// This function will block the calling thread in the following
// circumstances:
//
// - The job buffer is full and thus this function must wait until
//   other jobs complete for a chance of getting into the buffer.
// - (EDB_CCOPY, EDB_CWRITE) binc is larger than the database's
//   configured job transfer buffer (see
//   edb_hostconfig_t.job_transfersize). And thus the function must
//   wait until the job is accepted by a worker and that worker is
//   able to execute the oposite side of the transfer buffer so the
//   transfer can be complete.
//
//
// note to self: for binv, mmap binv as shared memory using mmaps first
// argument, and use binc as mmaps 2nd. map it using MAP_SHARED.
//
// What this function does and its arguments depend on arg.
//
//
// EDB_CCOPY (edb_data_t *)
//
//  Copy the contents of the object into binv up too binc bytes
//  starting at binoff.
//
//  Note that to properly get the exact binc needed to copy the whole
//  object (given binoff is 0) you must look at the object's
//  structure. Setting binc to a higher value that it needs to be will
//  result in undefined behaviour.
//
//
// EDB_CWRITE (edb_data_t *)
//
//  Create, update, or delete an object. Creation takes place when id
//  is 0 but binv is not null. Updates take place when id is not 0 and
//  binv is not null. Deletion takes place when id is not 0 and binv
//  is null.
//
//  During creation, the new object is written starting at binoff and
//  writes binc bytes and all other bytes are initializes as 0. During
//  updating, only the range from binoff to binoff+binc is modified.
//  binoff and binc are ignored for deletion.
//
//  Upon successful creation, id is set.
//
//  During writes, the object is placed under a write lock, preventing
//  any read operations from taking place on this same id.
//
// EDB_CUSRLK
//
//  Install a persisting user lock. These locks will affect future
//  calls to edb_obj
//
// ERRORS:
//
//  EDB_EINVAL - handle is null or uninitialized
//  EDB_EINVAL - cmd is not recongized
//  EDB_EINVAL - EDB_CWRITE: id is 0 and binv is null.
edb_err edb_obj (edbh *handle, edb_cmd cmd, int flags, ... /* arg */);


// For reading structures, use edb_structs.
//
// EDB_CWRITE (edb_struct_t *)
//
//  Create, update, or delete structures. Creation takes place when id
//  is 0 but binv is not null. Updates take place when id is not 0 and
//  if one of the fields is different than the current value: binc,
//  fixedc, data_ptrc. Deleteion takes place when id is not 0, fixedc
//  is 0, and data_ptrc is 0.
//
//  During creation, the new structure's configuration is written
//  starting at binoff and writes binc bytes and all other bytes are
//  initializes as 0. During updating, only the range from binoff to
//  binoff+binc is modified.  binoff and binc are ignored for
//  deletion.
//
//  Upon successful creation, id is set.
//
//  During updates and deletes, the structure is placed under a write
//  lock, preventing any read operations from taking place on this
//  same id.
//
// ERRORS:
//
//  EDB_EINVAL - handle is null or uninitialized
//  EDB_EINVAL - cmd is not recongized
//  EDB_EINVAL - EDB_CWRITE: id is 0 and binv is null.
//
edb_err edb_struct (edbh *handle, edb_cmd cmd, int flags, ... /* arg */);





/********************************************************************
 *
 * Data reading and writting.
 *
 ********************************************************************/

// See [[RW Conjugation]]
//
// Reads and writes data to the database.
//
edb_err edb_datcopy (edbh *handle, edb_data_t *data);
edb_err edb_datwrite(edbh *handle, edb_data_t data);





/********************************************************************
 *
 * Query functions
 *
 ********************************************************************/

typedef struct edb_event_st {
	int filler;
} edb_event_t;


typedef struct edb_select_st {
} edb_select_t;

// Select
//
// Listens for changes in the database by instrunctions set forth in
// params.
//
// This function blocks the calling thread and will return once a new
// change is detected.
//
// Limit 1 call to edb_select to each handle. If you attempt to call
// edb_select asyncrounously with the same handle, this will cause an
// error.
//
// If this function is called too infrequently then there's a
// possibility that the caller will miss certain events. But this is a
// very particular edge case that is described elsewhere probably.
edb_err edb_select(edbh *handle, edb_select_t *params);


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

//
// Thread safe.
//
edb_err edb_query(edbh *handle, edb_query_t *query);




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
edb_err edb_infohandle(edbh *handle, edb_infohandle_t *info);
edb_err edb_infodatabase(edbh *handle, edb_infodatabase_t *info);

// dump- functions just format the stucture and pipe it into fd.
// these functions provide no more information than the equvilient
// info- function.
int edb_dumphandle(edbh *handle, int fd);
int edb_dumpdatabase(edbh *handle, int fd);
	
#endif // _EDB_H_
