#include <oidadb-internal/options.h>
#include <oidadb-internal/buffers.h>
#include <oidadb-internal/memory.h>
#include <oidadb-internal/errors.h>
#include <oidadb/blocks.h>
#include <oidadb/buffers.h>
#include <sys/mman.h>
#include <string.h>

odb_err odb_buffer_new(struct odb_buffer_info buf_info, odb_buf **o_buf) {

	if(!o_buf
	   || buf_info.buffer_data_size == 0
	   || (buf_info.buffer_data_size % ODB_BLOCKSIZE) != 0
	   || buf_info.buffer_version_size == 0) {
		return ODB_EINVAL;
	}
	if(buf_info.flags != 0 && buf_info.flags != ODB_UCOMMITS) {
		return ODB_EINVAL;
	}

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
	                           , buf_info.buffer_data_size / ODB_BLOCKSIZE
	                           , PROT_READ | PROT_WRITE | PROT_EXEC
	                           , MAP_ANON | MAP_PRIVATE
	                           , -1
	                           , 0);

	if (buf->user_datam == MAP_FAILED) {
		buf->user_datam = 0; /* due to how odbh_buffer_free works */
		odb_buffer_free(buf);
		return odb_mmap_errno;
	}

	buf->user_versionv = odb_malloc(buf_info.buffer_version_size);
	if (!buf->user_versionv) {
		odb_buffer_free(buf);
		return odb_mmap_errno;
	}
	memset(buf->user_versionv, 0, buf_info.buffer_version_size);

	if (buf->info.flags & ODB_UCOMMITS) {

		buf->buffer_versionv = odb_malloc(buf_info.buffer_version_size);
		if (!buf->buffer_versionv) {
			odb_buffer_free(buf);
			return odb_mmap_errno;
		}
		memset(buf->buffer_versionv, 0, buf_info.buffer_version_size);

		buf->buffer_datam = odb_mmap(0
		                             , buf_info.buffer_data_size / ODB_BLOCKSIZE
		                             , PROT_NONE
		                             , MAP_ANON | MAP_PRIVATE
		                             , -1
		                             , 0);

		if (buf->buffer_datam == MAP_FAILED) {
			buf->buffer_datam = 0; /* due to how odbh_buffer_free works */
			odb_buffer_free(buf);
			return odb_mmap_errno;
		}
	}

	// We have no need to malloc mapped_regionsv as odbv_buffer_map handles all
	// that. We just have to make sure mapped_regionsq is set to 0, which it is
	// due to the memset.
	//
	//buf->mapped_regionsv = odb_malloc(...


	return 0;
}

odb_err odb_buffer_free(odb_buf *buffer) {
	if(buffer == 0) {
		log_debugf("attempt to free null buffer");
		return 0;
	}

	odb_err err = 0;

	// undo buffer maps
	if(buffer->mapped_regionsv) {
		for(int i = 0; i < buffer->mapped_regionsc; i++) {
			odb_err merr = odbv_buffer_unmap(buffer
			                                 , buffer->mapped_regionsv[i].start_offset
			                                 , buffer->mapped_regionsv[i].end_offset - buffer->mapped_regionsv[i].start_offset);
			if (merr) {
				err = log_critf("failed to unmap something that should have been mapped (merr %d)", merr);
			}
		}
		odb_free(buffer->mapped_regionsv);
	}

	// undo maps
	if (buffer->user_datam) {
		odb_munmap(buffer->user_datam, buffer->info.buffer_data_size / ODB_BLOCKSIZE);
	}
	if (buffer->buffer_datam) {
		odb_munmap(buffer->buffer_datam, buffer->info.buffer_data_size / ODB_BLOCKSIZE);
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

odb_err odbv_buffer_versions(odb_buf *buffer
                             , void **o_verv) {
	*o_verv = buffer->user_versionv;
	return 0;
}

odb_err odbh_buffer_versions_current(odb_buf *buffer
                                     , const odb_ver **o_verv) {
	if (!(buffer->info.flags & ODB_UCOMMITS)) {
		return ODB_EBUFF;
	}
	*o_verv = buffer->buffer_versionv;
	return 0;
}
