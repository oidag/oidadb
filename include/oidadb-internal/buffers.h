#ifndef OIDADB_INTERNAL_BUFFERS_H
#define OIDADB_INTERNAL_BUFFERS_H

#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>

typedef struct odb_buf_map_region {
	uint64_t start_offset; // inclusive (can be 0)
	uint64_t end_offset;   // exclusive (cannot be buffer length)
} odb_buf_map_region;

typedef struct odb_buf {
	struct odb_buffer_info info;

	/*
	 * These are privately-mapped.
	 */
	void         *user_versionv;
	odb_datapage *user_datam;

	/**
	 * The following are only needed when committing (ODB_UCOMMITS)
	 *
	 *  - buffer_version - needed when committing. equal length to user_versionv.
	 *    When committing (or in the future, signaled) will have updated versions
	 *    of the current blocks
	 *  - buffer_data - used for committing. equal size ot user_datam. will be used
	 *    to hold the existing data maps.
	 *  - buffer_group_desc - buffer to hold group descriptor pages inside
	 */
	odb_ver      *buffer_versionv;
	odb_datapage *buffer_datam;

	/**
	 * mapped regions is not a 1-to-1 relationship to the calls to
	 * odbv_buffer_map... what I mean is regions CAN be consolidated
	 */
	odb_buf_map_region *mapped_regionsv;
	int mapped_regionsc;
	int mapped_regionsq;
} odb_buf;

#endif