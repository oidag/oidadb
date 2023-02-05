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
 * The odbtelem namespaces looks at process data **within the same process**.
 * This means that you must enable and read telemetry in the same process
 * that \ref odb_host is to be active on.
 *
 * The sentiments of odbtelem:
 *   - odbtelem is strictly read/observe-only
 *   - odbtelem (when active) CAN slow down the database operations due to
 *     extra instructions and slow call-backs. So if you don't know what
 *     you're doing, don't use odbtelem (keep it disabled)
 *   - Not all libraries have odbtelem available at all. You'll see
 *     EDB_EVERSION returned a lot in those cases.
 *   - Telemetry is "lossless": this also means that all telemetry data is
 *     analyzed in the order that it happened.
 *   - Telemetry is not met for the detection of errors.
 *
 * \see odbtelem to start your journey into telemetry
 *
 * \{
 */

/**
 * \brief Enable/disable telemetry
 *
 * By default, the processes will have telemetry disabled. This function will
 * set it as active/deactive depending `enabled`.
 *
 * When turning off telemetry, all telemetry classes and their bindings are
 * destroyed.
 *
 * ## RETURNS
 *   - EDB_EVERSION - telementry not possible because this library was not
 *     built to have it.
 *
 * ## THREADING
 * Not MT-safe.
 */
edb_err odbtelem(int enabled);

/**
 * \brief Bind a telemetry class to a callback.
 *
 * All telemetry events are seperated into what are known "telemetry classes"
 * . Each telemetry class can have only a single callback bound to them. If
 * `cb` is null, then the class is set to be unbound.
 *
 * It is very important to note that these callbacks are executed in-thread,
 * so `cb` should be thread-safe sense it will be executed from any one of
 * the host's threads. This also means if your callback takes forever to
 * return, it will slow down the calling thread and thus slowing down the
 * database. Finally, if `cb` causes the thread to crash then this will
 * result in undefined behaviour, possibly ending in a corrupted database.
 *
 * `cb` will be provided `odbtelem_data`, how to interpret that data will
 * depend on \ref odbtelem_class.
 *
 * ## RETURNS
 *  - EDB_EINVAL - `class` is not valid
 *  - EDB_ENOENT - telemetry not enabled (see odbtelem())
 *  - EDB_EVERSION - Library version does not have `class` enabled.
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
} odbtelem_class;

typedef struct odbtelem_data {

	/// The invoking class
	odbtelem_class class;

	edb_pid pageid;

	union {
		edb_eid entryid;
		unsigned int workerid;
	};

	union {
		unsigned int pagec;
		unsigned int jobslot;
	};

} odbtelem_data;

edb_err odbtelem_bind(odbtelem_class class, void(*cb)(odbtelem_data data));
// odbtelem_bind
/// \}

// odbtelem
/// \}

#endif