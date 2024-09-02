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
 * the mapping functions here are synomomous to the mmap(2) family.

mdata will point to a output pointer to which will be set to the address of
 the mapped data.

 The mapped data is equivalent to MAP_PRIVATE. Modifications made to mapped
 memory are not reflected in the database only until a commit is performed.

 Furthermore, sense this is a private map, there is no read/write/execute
 protection on this mapping.

 If byte_offset is 0 or otherwise divisible by the system's page size, then
 mdata will be a page-aligned pointer.

 Note that a MAP_FIXED equivalent is not possible.

 When a buffer area is mapped, that region of the buffer is marked as mapped and
 thus cannot be mapped again until it is unmapped. This behaviour is process-wide
 so multithreaded applications should be careful not to double-map a region.

 ERRORS:
    - ODB_EINVAL - byte_count is 0.
    - ODB_EMAPPED - all or part of the requested region has already been mapped
    - ODB_ENMAP - all or part of the requested region is not mapped
    - ODB_EOUTBOUNDS - boff/blockc exceeds calculations of buffer size


 */
export odb_err odbv_buffer_map(odb_buf *buffer
                               , void **mdata
                               , uint64_t byte_offset
                               , uint64_t byte_count);

export odb_err odbv_buffer_unmap(odb_buf *buffer
                                 , uint64_t byte_offset
                                 , uint64_t byte_count);


/**
 Sets o_verv to point to a array of all the versions of the blocks
 that have been checked out. This array is owned by the buffer so don't try
 to free it or anything freaky like that.

 These versions are associative to the user data you get when using the mpa
 functions - rather they be elements/blocks/ect. - and only change when
 checkouts are performed with this buffer.

 You can set the versions to be whatever you want via this array, these will
 be the versions that are used when committing.
 */
export odb_err odbv_buffer_versions(odb_buf *buffer
                                    , void **o_verv);

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
