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

	buf->user_datam = odb_mmap(0
	                           , buf_info.bcount
	                           , PROT_READ | PROT_WRITE | PROT_EXEC
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
	memset(buf->user_versionv, 0, sizeof(odb_revision) * buf_info.bcount);

	if (buf->info.flags & ODB_UCOMMITS) {

		buf->buffer_versionv = odb_malloc(sizeof(odb_revision) * buf_info.bcount);
		if (!buf->buffer_versionv) {
			odbh_buffer_free(buf);
			return odb_mmap_errno;
		}
		memset(buf->buffer_versionv, 0, sizeof(odb_revision) * buf_info.bcount);

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

	// note we must do this last because of how buffer_free works.
	buf->map_statev = odb_malloc(sizeof(uint32_t) * ((buf_info.bcount/32)+1));
	if (!buf->map_statev) {
		odbh_buffer_free(buf);
		return odb_mmap_errno;
	}
	memset(buf->map_statev, 0, sizeof(uint32_t) * ((buf_info.bcount/32)+1));


	return 0;
}

odb_err odbh_buffer_free(odb_buf *buffer) {
	if(buffer == 0) {
		log_debugf("attempt to free null buffer");
		return 0;
	}

	odb_err err = 0;

	// undo buffer maps
	if (buffer->map_statev) {
		for(int i = 0; i < buffer->info.bcount; i++) {
			uint32_t statemask = buffer->map_statev[i/32];
			if(i % 32 == 0 && !statemask) {
				i += 32;
				continue;
			}
			for (int j = 0; j < 32; j++) {
				if((statemask >> j) & 1) {
					odb_err merr = odbh_buffer_unmap(buffer, i * 32 + j, 1);
					if (merr) {
						err = log_critf("failed to unmap something that should have been mapped (merr %d)", merr);
					}
				}
			}
		}
		odb_free(buffer->map_statev);
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
	return err;
}

odb_err odbh_buffer_versions(odb_buf *buffer
                             , odb_revision **o_verv) {
	*o_verv = buffer->user_versionv;
	return 0;
}

odb_err odbh_buffer_versions_current(odb_buf *buffer
                                     , const odb_revision **o_verv) {
	if (!(buffer->info.flags & ODB_UCOMMITS)) {
		return ODB_EBUFF;
	}
	*o_verv = buffer->buffer_versionv;
	return 0;
}
