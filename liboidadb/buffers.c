#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <sys/mman.h>
#include <string.h>

#include "pages.h"
#include "errors.h"
#include "mmap.h"

odb_err odbh_buffer_new(struct odb_buffer_info buf_info, odb_buf **o_buf) {

	odb_buf *buf = odb_malloc(sizeof(odb_buf));
	if(!buf) {
		return odb_mmap_errno;
	}
	memset(buf, 0, sizeof(odb_buf));

	// past this point, any non-successful return statement must be after
	// odbh_buffer_free(buf);

	*o_buf = buf;
	buf->info = buf_info;
	odb_err err = 0;

	buf->user_datam = odb_mmap(0
	                           , buf_info.bcount
	                           , PROT_READ | PROT_WRITE
	                           , MAP_ANON | MAP_PRIVATE
	                           , -1
	                           , 0);

	if (buf->user_datam == MAP_FAILED) {
		buf->user_datam = 0; /* due to how odbh_buffer_free works */
		odbh_buffer_free(buf);
		return odb_mmap_errno;
	}

	buf->user_versionv = odb_malloc(sizeof(odb_revision) * buf_info.bcount);
	if (!buf->user_versionv) {
		odbh_buffer_free(buf);
		return odb_mmap_errno;
	}

	if (buf->info.flags & ODB_UCOMMITS) {

		buf->buffer_versionv = odb_malloc(sizeof(odb_revision) * buf_info.bcount);
		if (!buf->buffer_versionv) {
			odbh_buffer_free(buf);
			return odb_mmap_errno;
		}

		buf->buffer_datam = odb_mmap(0
		                             , buf_info.bcount
		                             , PROT_NONE
		                             , MAP_ANON | MAP_PRIVATE
		                             , -1
		                             , 0);

		if (buf->buffer_datam == MAP_FAILED) {
			buf->buffer_datam = 0; /* due to how odbh_buffer_free works */
			odbh_buffer_free(buf);
			return odb_mmap_errno;
		}
	}


	return 0;
}

odb_err odbh_buffer_free(odb_buf *buffer) {
	if(buffer == 0) {
		log_debugf("attempt to free null buffer");
		return 0;
	}

	// undo maps
	if (buffer->user_datam) {
		odb_munmap(buffer->user_datam, buffer->info.bcount);
	}
	if (buffer->buffer_datam) {
		odb_munmap(buffer->buffer_datam, buffer->info.bcount);
	}

	// normal arrays
	if (buffer->user_versionv) {
		odb_free(buffer->user_versionv);
	}
	if (buffer->buffer_versionv) {
		odb_free(buffer->buffer_versionv);
	}
	odb_free(buffer);
	return 0;
}
