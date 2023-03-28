/**
 * \file
 * \brief The only header file needed to do everything with oidadb databases.
 *
 *
 */
#ifndef _EDB_H_
#define _EDB_H_ 1
#define _GNU_SOURCE
#include <stdint.h>
//#include <sys/fcntl.h>
#include <syslog.h>
#include <sys/user.h>


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

typedef struct odbh odbh;

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




////////////////////////////////////////////////////////////////////////////////
/**
 *
 * \defgroup errors Errors
 * \brief 50% of your job is handling errors; the other 50% is making them.
 *
 * Nearly all functions in OidaDB will return an odb_err. If this is not 0
 * then this indicates an error has ocoured. The exact nature of the value
 * depends on the function.
 *
 * odb_err itself represents an enum of very general errors. Again, their
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

	/// no error - explicitly 0 - as all functions returning odb_err is
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
	ODB_ENONE = 0,

	/// critical error: not the callers fault.
	/// You should never get this.
	///
	/// If you do, try what you did again and look closely at the \ref Logs -
	/// Everytime this is returned, a \ref EDB_LCRIT message would have been
	/// generated. You should probably send this message to the maintainers
	/// and steps to reproduce it.
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

/**
 * \brief Returns the string representation of the error. Good for logging.
 */
const char *edb_errstr(odb_err error);
/**\}*/





////////////////////////////////////////////////////////////////////////////////
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
void edb_setlogger(odbh *handle, unsigned int logmask,
                   void (*cb)(edb_log_channel logtype, const char *log));

// todo: use polling method instead:
typedef struct odb_log_t {
	edb_log_channel channel;
	const char *log;
} odb_log_t;
odb_err odb_log_poll(odbh *handle, odb_log_t *o_log);

/// \}




////////////////////////////////////////////////////////////////////////////////
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

/** \defgroup odb_create odb_create
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
 *   - ODB_EINVAL - params is invalid (see \ref odb_createparams_t)
 *   - ODB_EERRNO - errno set, by either open(2) or stat(2).
 *   - ODB_EEXIST (odb_create) - file already exists
 *   - ODB_ENOENT (odb_createt) - file does not exist
 *   - \ref ODB_ECRIT
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
static const odb_createparams odb_createparams_defaults = (odb_createparams){
		.page_multiplier = 2,
		.indexpages = 32,
		.structurepages = 32,
};
odb_err odb_create(const char *path, odb_createparams params);
odb_err odb_createt(const char *path, odb_createparams params); // truncate existing

/**\}*/
/**\}*/




////////////////////////////////////////////////////////////////////////////////
/** \defgroup hostshandles Database Hosts/Handles
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

/** \defgroup odb_handle odb_handle
 *
 * This is how one "connects" to a database that is being hosted. This will
 * return a handle to which you an use to submit jobs and monitor the
 * database as a client. Once a handle is obtained successfully, you are then
 * to use the \ref odbh "`odbh_*`" family.
 *
 * odb_handleclose is a safe function. It will ensure that handle is not null
 * and not already free'd.
 *
 * \param path Path to the file that is being hosted.
 *
 * \param o_handle This is a pointer-to-output. You just need to allocate
 * where to place the pointer itself, memory allocation is handled between
 * odb_handle (allocations) and odb_handleclose (frees).
 *
 * ## ERRORS
 *  - ODB_EINVAL - o_handle is null/path is null.
 *  - ODB_EINVAL - params.path is null
 *  - ODB_EERRNO - error with open(2), (ie, file does not exist, permssions,
 *                 see errorno)
 *  - ODB_ENOHOST - file is not being hosted
 *  - ODB_ENOTDB - file/host is not oidadb format/protocol
 *  - ODB_ENOMEM - not enough memory
 *  - \ref ODB_ECRIT
 *
 * \subsection THREADING
 * All \ref odbh functions called between odb_handle and odb_handleclose are
 * not considered thread safe. A given thread must posses their own handle if
 * they are to use any \ref odbh functions. Failure to do this will result in
 * a deadlock.
 *
 * The thread that called odb_handle must be the same thread that calls
 * odb_handleclose.
 *
 * \see odbh
 * \see odb_host
 *
 * \{
 */
odb_err odb_handle(const char *path, odbh **o_handle);
void    odb_handleclose(odbh *handle);
/// \}

/** \defgroup odb_host odb_host
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
 * odb_host is not thread-safe. You should only call this once a 1 thread and
 * wait for it to return before calling it again
 *
 * odb_hoststop is MT-safe.
 *
 * The aforementioned write-locks use Open File Descriptors (see fcntl(2)),
 * this means that attempts to open the same file in two separate
 * threads will behave the same way as doing the same with two
 * separate processes. This also means the host can be used in the
 * same process as the handles (though its recommended not to do this on the
 * basis of good engineering and departmentalizing crashes).
 *
 * Only 1 odb_host can be active per process.
 *
 * odb_hoststop must be called in the same process as odb_host.
 *
 * ## ERRORS
 * odb_host can return:
 *   - ODB_EINVAL - hostops is invalid and/or path is null
 *   - ODB_EERRNO - Unexpected error from stat(2) or open(2), see errno.
 *   - ODB_EOPEN  - Another process is already hosting this file.
 *   - ODB_EAGAIN - odb_host is already active.
 *   - ODB_EFILE  - Path is not a regular file.
 *   - ODB_EHW    - this file was created on a different (non compatible)
 *   architecture and cannot be hosted on this machine.
 *   - ODB_ENOTDB - File is invalid format, possibly not a database.
 *   - ODB_ENOMEM - Not enough memory to reliably host database.
 *   - \ref ODB_ECRIT
 *
 * odb_hoststop can return
 *  - ODB_EAGAIN - odb_host is in the process of booting up. try again in a
 *                 little bit.
 *  - ODB_ENOHOST - odb_host not active at all.
 *
 *
 * \see odb_create
 * \see odb_hostconfig_t
 * \see odb_hostconfig_default
 * \see odb_handle
 *
 * \{
 */

/**
 * \brief All parameters for hosting
 * \see odb_hostconfig_default
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
static const odb_hostconfig_t odb_hostconfig_default = {
		.job_buffq = 16,
		.job_transfersize = PAGE_SIZE,
		.event_bufferq = 32,
		.worker_poolsize = 4,
		.slot_count = 16,
		.flags = 0,
};

odb_err odb_host(const char *path, odb_hostconfig_t hostops);
odb_err odb_hoststop();

// odb_host
/// \}


/** \defgroup odb_hostpoll odb_hostpoll
 * \brief Wait for hosting-related events on a file
 *
 * Anytime you want to "listen" to a given file and have your thread wait
 * around for a particular event(s), use this function.
 *
 * Calling this odb_hostpoll (on a valid file) will block the caller until
 * an event that was included into the odb_event bitmask is triggered. See
 * below for specific information about each event.
 *
 * odb_event is a XOR'd mask of which events you'd like to wait around for.
 * ODB_EVENT_ANY can be used to select any event.
 *
 * You may also optionally provide `o_env` so that when that when an event is
 * triggered, o_env will specify which event had triggered.
 *
 * Only one event is returned per call. Note that events will be triggered
 * retroactively.
 *
 *  - `ODB_EVENT_HOSTED` - Triggered when the file is now hosted, or is
 *  currently hosted when odb_hostpoll was called.
 *  - `ODB_EVENT_CLOSED` - Triggered when the file's host has closed, or is
 *  currently closed when odb_hostpoll was called.
 *
 *
 * ## THREADING
 * odb_hostselect is thread safe.
 *
 * ## ERRORS
 *   - ODB_EINVAL `event` was not an allowed value
 *   - ODB_EINVAL `path` was null
 *   - ODB_ENOENT file does not exist.
 *   - \ref ODB_ECRIT
 *
 *
 * \{
 */
typedef unsigned int odb_event;
#define ODB_EVENT_HOSTED 1
#define ODB_EVENT_CLOSED 2
#define ODB_EVENT_ANY (odb_event)(-1)
odb_err odb_hostpoll(const char *path, odb_event event, odb_event *o_env);
// odb_hostpoll
/// \}


// hostshandles
/// \}




////////////////////////////////////////////////////////////////////////////////
/** \defgroup elements OidaDB Elements
 * \brief The foundational elements of the database
 *
 * Elements AKA Types are the structures that together constitute the entire
 * database. They all have their own purpose.
 *
 * \{
 */
typedef uint8_t odb_type;
#define ODB_ELMINIT  0
#define ODB_ELMDEL   1
#define ODB_ELMSTRCT 2
#define ODB_ELMOBJ   3
#define ODB_ELMENTS  4
#define ODB_ELMPEND  5
#define ODB_ELMLOOKUP 6
#define ODB_ELMDYN 7
/// \}





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
odb_err odbh_index(odbh *handle, edb_eid eid, void *o_entry); //todo: what is
// o_entry?

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
odb_err odbh_structs(odbh *handle, edb_sid structureid, void *o_struct); //
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
} odb_cmd;

// edb_jobclass must take only the first 4 bits. (to be xor'd with
// edb_cmd).
typedef enum edb_jobclass {

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
	//     <- edb_oid (see also: EDB_OID_... constants)
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

} edb_jobclass;


// OR'd together values from odb_jobclass and odb_cmd
typedef unsigned int odb_jobdesc;


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

// - write-only?
// - wait-to-commit (wait until odbh_jobpoll is called to execute all jobs
//                   atomically)
// -
typedef enum odb_jobhint_t {

	// all jobs installed in odbh_job will be written too and will have no
	// response when jobread is called.

	what about streaming one job to another?

	ODB_JNOREAD,

	// force the jobs to execute sequencially as they were installed
	ODB_JSEQ,

	// does nothing right now. In the future this will make all jobs
	// installed in odbh_job not to start execution until jobreturn is called.
	ODB_JATOMIC,
} odb_jobhint_t;
odb_err odbh_jobmode(odbh *handle, odb_jobhint_t hint);

// an error that can happen with edbh_job is that the caller has too many
// jobs open: "too many" means they have more jobs open concurrently then
// there are threads. If we allow more jobs to remain open than threads, this
// will cause a deadlock sense all threads (ie: 2) will be doing something,
// if we have a 3rd job also open and streaming into, that third jobs buffer
// will fill up and then block. The original 2 jobs will then never have
// their buffers cleared. Dead locks can also happen due to a full edbp cache.
//
// This only applies to "open buffer" jobs though. If the job has been fully
// installed then no deadlock can happen.
//
// Further more, we can have multiple handles (ie: 4) all have an open buffer
// job installed and this will not cause a deadlock hmmm
//
// If we have 2 workers, 3 handles h1, h2, h3 . Each handle installs 1 open
// buffer job j1, j2, j3... no dead lock.
//
// But what if 1 handle installed 2 jobs j4 thus:
//
// h1 -> j1*
// h2 -> j2
// h3 -> j3, j4*
//
// * = worker adopted job.
//
// No... no deadlock. Sense each handle is on its own thread, they will all
// clear buffers so the worker will always be able to move on.
//
// Make sure handles are installed on different threads!

odb_err odbh_job(odbh *handle, odb_jobdesc jobdesc, odbj **o_jhandle);
odb_err odbh_jobwrite(odbj *handle, const void *buf, int bufc);
odb_err odbh_jobread(odbj *handle, void *o_buf, int bufc);


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
odb_err odbh_struct (odbh *handle, odb_cmd cmd, int flags, ... /* arg */);


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
odb_err odbh_select(odbh *handle, edb_select_t *params);

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
odb_err edb_infohandle(odbh *handle, edb_infohandle_t *info);
odb_err edb_infodatabase(odbh *handle, edb_infodatabase_t *info);

// dump- functions just format the stucture and pipe it into fd.
// these functions provide no more information than the equvilient
// info- function.
int edb_dumphandle(odbh *handle, int fd);
int edb_dumpdatabase(odbh *handle, int fd);
	
#endif // _EDB_H_
