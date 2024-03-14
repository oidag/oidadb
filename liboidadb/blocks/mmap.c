#define _LARGEFILE64_SOURCE
#include <oidadb-internal/options.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <errno.h>
#include <malloc.h>

#include "mmap.h"
#include "errors.h"

odb_err __thread mmap_error = 0;

void *odb_mmap (void *addr
                , unsigned int page_count
                , int prot
                , int flags
                , int fd
                , odb_pid page_offset) {

	void *ret;

	ret = mmap64(addr
	             , page_count * ODB_PAGESIZE
	             , prot
	             , flags
	             , fd
	             , (off64_t)page_offset * ODB_PAGESIZE);

	if (ret == MAP_FAILED) {
		switch (errno) {
		case ENOMEM: mmap_error = ODB_ENOMEM;
			break;
		default:
			mmap_error = log_critf(
					"mmap failed for unknown reason (errno %d)"
					, errno);
		}
		return (void *)-1;
	}
	return ret;
}

void odb_munmap(void *addr, unsigned int page_count) {
	int err = munmap(addr, page_count * ODB_PAGESIZE);
	if (err) {
		log_critf("munmap failed for unknown reason");
	}
}

void *odb_mmap_alloc(unsigned int page_count) {
	return odb_mmap(0
					, page_count
					, PROT_NONE
					, MAP_ANON | MAP_PRIVATE
					, -1
					, 0);
}

void *odb_malloc(size_t size) {
	void *ret = malloc(size);
	if(!ret) {
		switch (errno) {
		case ENOMEM: mmap_error = ODB_ENOMEM;
		default:
			mmap_error = log_critf(
					"malloc failed for unknown reason (errno %d)"
					, errno);
		}
	}
	return ret;
}

void odb_free(void *ptr) {
	return free(ptr);
}



odb_err *_odb_mmap_err_location() {
	return &mmap_error;
}
