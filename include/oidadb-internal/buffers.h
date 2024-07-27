#ifndef OIDADB_INTERNAL_BUFFERS_H
#define OIDADB_INTERNAL_BUFFERS_H

#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>

typedef struct odb_buf {
	struct odb_buffer_info info;

	/*
	 * These are privately-mapped.
	 */
	odb_ver      *user_versionv;
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
	 * Used to describe what has and hasn't been mapped via odbv_buffer_map
	 *
	 * map_statev is an array of uint32_t with each bit describing the
	 * associative page found in user_datam. Thus bit 0 represents page 0.
	 * The length of map_statev is (info->bcount / 32)+1.
	 */
	uint32_t *map_statev;
} odb_buf;

#endif