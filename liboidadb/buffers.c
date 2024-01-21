#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <malloc.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "pages.h"
#include "errors.h"

odb_err odbh_buffer_new(struct odb_buffer_info buf_info, odb_buf **o_buf) {

	odb_buf *buf = malloc(sizeof(odb_buf));
	memset(buf, 0, sizeof(odb_buf));
	*o_buf = buf;
	buf->info = buf_info;
	odb_err err = 0;

	// Make a dummy map that has PROT_NONE. This will allocate
	// space and thus, we can re-map using the addresses found
	// in the pre-allocated space during checkouts.
	buf->blockv = mmap(0, ODB_PAGESIZE * buf_info.bcount
	                   , PROT_NONE
	                   , MAP_ANON | MAP_PRIVATE
	                   , -1
	                   , 0);

	if (buf->blockv == MAP_FAILED) {
		buf->blockv = 0; /* due to how odbh_buffer_free works */
		switch (errno) {
		case ENOMEM: err = ODB_ENOMEM;
			break;
		default:
			err = log_critf(
					"mmap failed for unknown reason (errno %d)"
					, errno);
		}
		odbh_buffer_free(buf);
		return err;
	}

	buf->revisionv = malloc(sizeof(odb_revision) * buf_info.bcount);
	if (!buf->revisionv) {
		switch (errno) {
		case ENOMEM: err = ODB_ENOMEM;
			break;
		default:
			err = log_critf("malloc failed for unknown reason (errno %d)"
					, errno);
		}
		odbh_buffer_free(buf);
		return err;
	}

	return 0;
}

odb_err odbh_buffer_free(odb_buf *buffer) {
	if (buffer->blockv) {
		munmap(buffer->blockv, ODB_PAGESIZE * buffer->info.bcount);
	}
	if (buffer->revisionv) {
		free(buffer->revisionv);
	}
	free(buffer);
	return 0;
}
