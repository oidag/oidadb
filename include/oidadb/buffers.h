#ifndef OIDADB_BUFFERS_H
#define OIDADB_BUFFERS_H

#include "common.h"
#include "errors.h"
#include "pages.h"

typedef enum odb_usage {

	/**
	 * Buffer will be an array of odb_version. When executing
	 */
	ODB_UVERSIONS,

	/**
	 * buffer will be an array of odb_block
	 */
	ODB_UBLOCKS,

} odb_usage;

struct odb_block {
	odb_revision version;
	odb_page *page;
};


struct odb_buffer_info {
	uint32_t size;
	odb_usage usage;
	odb_ioflags flags;
};

typedef int odb_buf;





export odb_err odbh_buffer_new(odb_desc *desc, struct odb_buffer_info *buffer, odb_buf *o_buf);
export odb_err odbh_buffer_bind(odb_desc *desc, odb_buf buffer);
export odb_err odbh_buffer_map(odb_desc *desc, odb_buf buffer, void **o_data, int offset, int count);
export odb_err odbh_buffer_unmap(odb_desc *desc, odb_buf buffer);
export odb_err odbh_buffer_free(odb_desc *desc, odb_buf buffer);



#endif
