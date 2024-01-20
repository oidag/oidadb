#ifndef OIDADB_BUFFERS_H
#define OIDADB_BUFFERS_H

#include "common.h"
#include "errors.h"
#include "pages.h"

typedef enum odb_usage {

	/**
	 * Buffer will be an array of odb_version when mapped.
	 */
	ODB_UVERSIONS = 0x0001,

	/**
	 * buffer will be an array of odb_block when mapped
	 */
	ODB_UBLOCKS = 0x0002,

} odb_usage;


struct odb_buffer_info {
	uint32_t bcount;

	// Usage denotes what this buffer do when a checkout is performed.
	// It can be ODB_UVERSIONS to checkout versions, ODB_UBLOCKS to
	// checkout blocks, or ODB_UVERSIONS | ODB_UBLOCKS to checkout both.
	//odb_usage   usage;
	odb_ioflags flags;
};

typedef struct odb_buf odb_buf;


export odb_err odbp_bind_buffer(odb_desc *desc, odb_buf *buffer);


// flags for odbh_buffer_new
export odb_err odbh_buffer_new(struct odb_buffer_info buf_info
		, odb_buf **o_buf);

/**
 * component must be either ODB_UVERSIONS for versions or ODB_UBLOCKS for
 * blocks. Will fail if the buffer is not configured for that usage.
 */
export odb_err odbh_buffer_map(odb_buf *buffer
                               , int boff
                               , int bcount
                               , void *o_data);

export odb_err odbh_buffer_unmap(odb_buf *buffer);

export odb_err odbh_buffer_versions(odb_buf *buffer
									, const odb_revision **o_revision);

export odb_err odbh_buffer_free(odb_buf *buffer);


#endif
