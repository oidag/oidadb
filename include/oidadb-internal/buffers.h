#ifndef OIDADB_INTERNAL_BUFFERS_H
#define OIDADB_INTERNAL_BUFFERS_H

#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>

typedef struct odb_buf_map_region {
	uint64_t start_offset; // inclusive (can be 0)
	uint64_t end_offset;   // exclusive (cannot be buffer length)
} odb_buf_map_region;

typedef struct odb_buf {

	/**
	 * not to be written to outside of buffers/
	 */
	struct odb_buffer_info info;

	/*
	 *
	 * checkout_versionv - the versions of user_datam when it was checked out
	 * These are privately-mapped.
	 *
	 * user_datam - the data itself
	 */
	const void *checkout_versionv;
	void *user_datam;

	/**
	 * The following are only needed when committing (ODB_UCOMMITS)
	 *
	 *  - buffer_version - needed when committing. equal length to user_versionv.
	 *    When committing (or in the future, signaled) will have updated versions
	 *    of the current blocks
	 *  - buffer_datam - used for committing. equal size ot user_datam. will be used
	 *    to hold the existing data maps.
	 */
	const void *upstream_versionv;
	const void *upstream_datam;
} odb_buf;

#endif