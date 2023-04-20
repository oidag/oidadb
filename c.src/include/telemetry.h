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
#ifndef OIDADB_TELEMETRY_H_
#define OIDADB_TELEMETRY_H_

#include "oidadb.h"

struct odbtelem_params {
	int buffersize_exp;
};
odb_err odbtelem(int enabled, struct odbtelem_params params);

odb_err odbtelem_attach(const char *path);
void    odbtelem_detach();

typedef enum odbtelem_class_t {
	ODBTELEM_PAGES_NEWOBJ,
	ODBTELEM_PAGES_NEWDEL,
	ODBTELEM_PAGES_CACHED,
	ODBTELEM_PAGES_DECACHED,
	ODBTELEM_WORKR_ACCEPTED,
	ODBTELEM_WORKR_PLOAD,
	ODBTELEM_WORKR_PUNLOAD,
	ODBTELEM_JOBS_ADDED,
	ODBTELEM_JOBS_COMPLETED,
	_ODBTELEM_LAST,
} odbtelem_class_t;
struct odbtelem_data {
	odbtelem_class_t class;
	union {
		uint64_t arg0;
		odb_pid pageid;
	};
	union {
		unsigned int arg1;
		odb_eid entityid;
		unsigned int workerid;
	};
	union {
		unsigned int arg2;
		unsigned int pagec;
		unsigned int jobslot;
	};
};
odb_err odbtelem_poll(struct odbtelem_data *o_data);


typedef void(*odbtelem_cb)(struct odbtelem_data);
odb_err odbtelem_bind(odbtelem_class_t class, odbtelem_cb cb);

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
 *  - ODB_EVERSION - Version does not support imaging.
 *  - ODB_EPIPE   - Not attached to host process (see \ref odbtelem_attach)
 *  - ODB_EINVAL - o_image is null
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
	odb_pid     *pagecachev;

	// todo: locks?

	/// Associative to pagesc_cachedv: what workers have which pages checked
	/// out. If 0, then page is not checked out.
	unsigned int *pagecachev_worker;

	/// Job SLOTS. (not to be confused with jobs installed)
	/// Rheir descriptions, as well as their owners (worker id).
	/// jobtype = 0 means no job installed.
	unsigned int jobsq;
	odb_jobtype_t *job_desc;
	unsigned int *job_workersv;

} odbtelem_image_t;
odb_err odbtelem_image(odbtelem_image_t *o_image);

#endif