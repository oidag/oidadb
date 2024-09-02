#ifndef OIDADB_BUFFERS_H
#define OIDADB_BUFFERS_H

#include "common.h"
#include "errors.h"

typedef enum odb_usage {

	/**
	 * Buffer will be used to preform commits
	 */
	ODB_UCOMMITS = 0x0001,
} odb_usage;


struct odb_buffer_info {
	//uint32_t bcount;

	// buffer_version_size is the size of the buffer that will be responsible
	// for holding version data.
	uint32_t buffer_version_size;

	// buffer_data_size is the size of the buffer that will be responsible for
	// holding user data.
	//
	// Must be divisible by ODB_BLOCKSIZE
	uint64_t  buffer_data_size;
	odb_usage flags;
};

typedef struct odb_buf odb_buf;

// flags for odbh_buffer_new
export odb_err odb_buffer_new(struct odb_buffer_info buf_info
                              , odb_buf **o_buf);

/**
 *

 Same behaviour as versions, but will return the CURRENT block versions, thus
 this will be updated everytime a commit is performed (regardless if ODB_EVERSION
 is returned.

 ODB_EBUFF - buffer does not have ODB_UCOMMITS flag.

export odb_err odbh_buffer_versions_current(odb_buf *buffer
                                            , const odb_revision **o_verv);
*/

/**
 * Will automatically unmap any outstanding map, though not very efficiently.
 * It's recommended that you manually do your unmapping for better performance.
 */
export odb_err odb_buffer_free(odb_buf *buffer);


#endif
