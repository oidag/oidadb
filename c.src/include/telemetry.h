/**
 * \file
 * \brief Optional, *advanced* telemetry methods
 * Note that not all symbols may be available
 */
#ifndef OIDADB_TELEMETRY_H_
#define OIDADB_TELEMETRY_H_

#include "oidadb.h"

/**
 * \defgroup odbtelem odbtelem
 *
 * \brief Advanced telemetry options for OidaDB processes
 *
 * OidaDB allows for you to monitor the deep inter-workings of the database
 * library. Such inter-workings are trivial and impracticable for the use of
 * developing applications that uses the database: the only purpose of
 * `odbtelem` is for debugging, diagnosing, and monitoring the *reactivity* the
 * database has to your application.
 *
 * Accessing `odbtelem` comes in either intra-process or inner-process forms.
 * Intra-process telemetry is faster than inner-process telemetry for obvious
 * reasons.
 *
 * The sentiments of odbtelem:
 *   - odbtelem is strictly read/observe-only.
 *   - odbtelem (when active) will greatly slow down the database performance
 *     due to extra instructions and slow call-backs. So if you don't know what
 *     you're doing, nor why, don't use odbtelem (keep it disabled).
 *   - Not all libraries have odbtelem fully available, if at all. You'll see
 *     EDB_EVERSION returned a lot in those cases.
 *   - Telemetry is "lossless": this also means that all telemetry data is
 *     analyzed in the order that it happened.
 *   - Telemetry is not met for the detection of errors.
 *
 * \see odbtelem to start your journey into telemetry
 *
 * \{
 */

/// \brief Configure various aspects of the telemetry host.
typedef struct odbtelem_params_t {

	/// \brief Innprocess telemtry
	///
	/// If non-0, then outside processes will be able to attach to the
	/// telemetry via \ref odbtelem_attach.
	///
	/// Default value is 0. Some versions may have `odbtelem` return
	/// EDB_EVERSION if this is enabled.
	///
	int innerprocess; // todo: delete this. Make it innerpocess by default.
	//                         shm file permissions will take care of access.

	/// The size of the telemetry poll buffer in form of an exponent of 2. A
	/// smaller poll will increase the likely hood that \ref odbtelem_poll
	/// will return EDB_EMISSED.
	///
	/// buffersize_exp must be >= 0 and <= 15
	///
	/// Default value is 5 (2^5 = 32 buffer size).
	///
	/// EDB_EINVAL if buffersize_exp < 1
	///
	/// \see The only true fix to avoid EDB_EMISSED is discussed in
	///      odbtelem_poll
	int buffersize_exp;

} odbtelem_params_t;

/**
 *
 * \brief Enable/disable telemetry for a host process.
 *
 * By default, the processes will have telemetry disabled. This function will
 * set it as active/deactive depending `enabled`.
 *
 * When the telemetry is changing state from disabled to enabled, the
 * structure \ref odbtelem_params is used to configure various the aspects of
 * the telemetry. Otherwise, this argument is ignored
 *
 * When turning off telemetry, all telemetry classes and their bindings are
 * destroyed. It will revert everything back to as if odbtelem was never
 * called in the first place.
 *
 * ## RETURNS
 *   - EDB_EVERSION - telementry not possible because this library was not
 *                    built to have it.
 *   - EDB_EVERSION - See `odbtelem_params_t` structure
 *   - EDB_EINVAL   - See `odbtelem_params_t` structure
 *   - EDB_ECRIT
 *
 */
edb_err odbtelem(int enabled, odbtelem_params_t params);

/**
 * \brief Attach to a hosted database's telemetry stream
 *
 * Attach the calling process to whatever process has `path` open and hosted
 * and access the telemetry data. Note the calling process and host process
 * can be the same, or different provided that the host process has enabled
 * innerprocess telemtry.
 *
 * Once attached, \ref odbtelem_poll can be used to read the stream of
 * analytics.
 *
 * If the host process decides to disable telemetry or shutsdown after
 * `odbtelem_attach`, then it is as if odbtelem_detach was called.
 *
 * ## ERRORS
 *  - EDB_EVERSION - Library version does not provide telemetry attachments
 *  - EDB_EERRNO - An error was returned by open(2)... see errno.
 *  - EDB_ENOTDB - `odbtelem_attach` opened `path` and found not to be a
 *                  oidadb file.
 *  - EDB_ENOHOST - The file is a oidadb file, but is not being hosted.
 *  - EDB_EPIPE   - The host exists and is running, but analytics are not
 *                  enabled. (See \ref odbtelem)
 *  - EDB_EOPEN -   Already attached successfully.
 *  - \ref EDB_ECRIT
 *
 * \see odbtelem
 * \see odbtelem_poll
 * \see odbtelem_image
 *  \{
 */
edb_err odbtelem_attach(const char *path);
void    odbtelem_detach();
// odbtelem_attach
/// \}

/**
 * \defgroup odbtelem_poll odbtelem_poll
 *
 *
 * ## ERRORS
 *  - EDB_EPIPE   - Not attached to host process (see \ref odbtelem_attach)
 *  - EDB_EMISSED - `odbtelem_poll` was called too infrequently and wasn't
 *                  able to capture all events in the buffer before elements
 *                  of the buffer had to be replaced. This happens because
 *                  you were not polling fast enough.
 *
 * \{
 */
typedef enum odbtelem_class {

	/// New object pages were just created
	///  - data.entryid the entry id
	///  - data.pageid is the id of the first page
	///  - data.pagec is the amount of subsequent pages that were created
	ODBTELEM_PAGES_NEWOBJ,

	/// New deleted pages were just created
	///  - data.pageid is the id of the first page
	ODBTELEM_PAGES_NEWDEL,

	/// A page was just added/removed to/from the cache
	///  - data.pageid
	/// \{
	ODBTELEM_PAGES_CACHED,
	ODBTELEM_PAGES_DECACHED,
	/// \}

	/// Worked accepted a new job
	///  - data.workerid is the worker
	///  - data.jobslot is the slot that job is stored in
	ODBTELEM_WORKR_ACCEPTED,

	/// Worker loaded/unloaded a page
	///  - data.pageid is the page
	///  - data.workerid is the worker
	/// \{
	ODBTELEM_WORKR_PLOAD,
	ODBTELEM_WORKR_PUNLOAD,
	/// \}

	/// New job was just added
	///  - jobslot - the new job
	ODBTELEM_JOBS_ADDED,

	/// A job was completed.
	///  - jobslot - the job that was just completed.
	ODBTELEM_JOBS_COMPLETED,

	/// Do not use.
	_ODBTELEM_LAST
} odbtelem_class;

typedef struct odbtelem_data {

	/// The invoking class
	odbtelem_class class;

	union {
		uint64_t arg0;
		edb_pid pageid;
	};

	union {
		unsigned int arg1;
		edb_eid entryid;
		unsigned int workerid;
	};

	union {
		unsigned int arg2;
		unsigned int pagec;
		unsigned int jobslot;
	};

} odbtelem_data;
edb_err odbtelem_poll(odbtelem_data *o_data);
// odbtelem_poll
/// \}

/**
 * \brief Get a full in-memory snapshot of the attached host.
 *
 * odbtelem_image will provide the "full picture" of the attached host's memory.
 * Unlike odbtelem_poll which only provides the delta, this function provides
 * the entire landscape of the database.
 *
 * Keep in mind that by the time this function returns, `image` may already
 * be out of date. Being pensive in calling \ref odbtelem_poll is the only
 * way to stay up to date.
 *
 * ## ERRORS
 *  - EDB_EVERSION - Version does not support imaging.
 *  - EDB_EPIPE   - Not attached to host process (see \ref odbtelem_attach)
 *  - EDB_EINVAL - o_image is null
 */
typedef struct odbtelem_image_t {

	/// total count of pages in the database
	unsigned long pagec;

	/// number of workers
	unsigned int workerc;

	/// The page cache size and an array of equal size describing the page
	/// id's that are in cache. If a page id is 0, that means that slot has
	/// no page loaded.
	unsigned int pagecacheq;
	edb_pid     *pagecachev;

	// todo: locks?

	/// Associative to pagesc_cachedv: what workers have which pages checked
	/// out. If 0, then page is not checked out.
	unsigned int *pagecachev_worker;

	/// Job SLOTS. (not to be confused with jobs installed)
	/// Rheir descriptions, as well as their owners (worker id).
	/// jobdesc = 0 means no job installed.
	unsigned int jobsq;
	odb_jobdesc  *job_desc;
	unsigned int *job_workersv;

} odbtelem_image_t;
edb_err odbtelem_image(odbtelem_image_t *o_image);



// odbtelem
/// \}

#endif