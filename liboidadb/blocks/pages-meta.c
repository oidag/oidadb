#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mmap.h"
#include "blocks.h"
#include "errors.h"
#include "errno.h"

void page_lock(int fd, odb_pid page, int xl) {
	struct flock64 flock = {
			.l_type = F_RDLCK,
			.l_start = (off64_t) page * ODB_PAGESIZE,
			.l_whence = SEEK_SET,
			.l_len = ODB_PAGESIZE,
			.l_pid = 0,
	};
	if (xl) {
		flock.l_type = F_WRLCK;
	}
	int err = fcntl64(fd, F_SETLKW64, &flock);
	if (err == -1) {
		log_critf("fcntl lock failed");
	}
}

/**
 * will place an eof lock on the volume. This is defined as locking the SEEK_END
 * with a length of 0.
 *
 * The length of the file after the lock was aquired is returned.
 *
 * It is assumed that fd's size will only change if and only if an XL lock is
 * aquired before ftruncate is used.
 *
 * To unlock, use page_unlock_eof
 * @param fd
 * @param xl
 */
off64_t page_lock_eof(int fd, int xl) {
	struct flock64 flock = {
			.l_type = F_RDLCK,
			.l_start = 0,
			.l_whence = SEEK_END,
			.l_len = 0,
			.l_pid = 0,
	};
	if (xl) {
		flock.l_type = F_WRLCK;
	}
	int err = fcntl64(fd, F_SETLKW64, &flock);
	if (err == -1) {
		log_critf("fcntl lock failed");
	}
	return lseek64(fd, 0, SEEK_END);
}

void page_unlock_eof(int fd, off64_t lock_result) {
	struct flock64 flock = {
			.l_type = F_UNLCK,
			.l_start = lock_result,
			.l_whence = SEEK_SET,
			.l_len = 0,
			.l_pid = 0,
	};
	int            err   = fcntl64(fd, F_SETLKW64, &flock);
	if (err == -1) {
		log_critf("fcntl lock failed");
	}
}

void page_unlock(int fd, odb_pid page) {
	struct flock64 flock = {
			.l_type = F_UNLCK,
			.l_start = (off64_t) page * ODB_PAGESIZE,
			.l_whence = SEEK_SET,
			.l_len = ODB_PAGESIZE,
			.l_pid = 0,
	};
	int            err   = fcntl64(fd, F_SETLKW64, &flock);
	if (err == -1) {
		log_critf("fcntl lock failed");
	}
}

odb_err volume_initialize(int fd) {

	page_lock(fd, 0, 1);
	struct stat64 sbuf;
	if (fstat64(fd, &sbuf) == -1) {
		page_unlock(fd, 0);
		return log_critf("failed to stat file");
	}
	if ((sbuf.st_mode & S_IFMT) != S_IFREG) {
		// later: need to add block device support.
		page_unlock(fd, 0);
		return log_critf("only regular files supported");
	}
	off_t size = sbuf.st_size;
	if (size != 0) {
		page_unlock(fd, 0);
		return ODB_EEXIST;
	}

	page_unlock(fd, 0);
	return 0;
}

odb_err volume_load(odb_desc *desc) {
	return 0;
}

void volume_unload(odb_desc *desc) {
	return;
}

// helper to group_load.
static void group_initialize(struct odb_block_group_desc *new_desc, odb_gid goff) {
	new_desc->magic[0] = (uint8_t[2]) ODB_SPEC_HEADER_MAGIC[0];
	new_desc->magic[1] = (uint8_t[2]) ODB_SPEC_HEADER_MAGIC[1];
	new_desc->flags = ODB_SPEC_FLAG_GROUP_INIT | ODB_SPEC_FLAG_BLOCK_GROUP;
}

// note: desc is const.
odb_err group_loadg(const odb_desc *desc
                    , odb_gid gid
                    , struct odb_block_group_desc *buff_group_descm) {

	int     fd         = desc->fd;
	odb_pid pid        = gid * ODB_SPEC_PAGES_PER_GROUP;
	int     prot       = (int) desc->flags & 0x3;
	void    *metapages = odb_mmap(buff_group_descm
	                              , 1
	                              , prot
	                              , MAP_SHARED_VALIDATE | MAP_FIXED
	                              , fd
	                              , (off64_t) pid);
	if (metapages == MAP_FAILED) {
		return odb_mmap_errno;
	}

	// remember, as per the definition of this function, we don't know if this
	// group has been initialized.

	// initialize the group if it hasn't already been initialized
	const volatile uint16_t *gflags = &buff_group_descm->flags;
	if (!(*gflags & ODB_SPEC_FLAG_GROUP_INIT)) {
		// group has (probably) not been initialized... we need to XL lock it
		// to be sure.
		page_lock(fd, pid, 1);
		if (!(*gflags & ODB_SPEC_FLAG_GROUP_INIT)) {
			// yup, certainly not initialized, go ahead and do so.
			group_initialize(buff_group_descm, gid);
		}
		page_unlock(fd, pid);
	}

	// double-check the magic number
	if ((buff_group_descm->magic[0] != (uint8_t[2]) ODB_SPEC_HEADER_MAGIC[0]
	    || buff_group_descm->magic[1] != (uint8_t[2]) ODB_SPEC_HEADER_MAGIC[1])
		|| !(buff_group_descm->flags & ODB_SPEC_FLAG_BLOCK_GROUP)) {
		return ODB_ENOTDB;
	}

	// switch out the loaded group.
	return 0;
}

odb_err block_truncate(odb_desc *desc, odb_bid block_off) {
	int     fd = desc->fd;
	odb_pid needed_page_count, current_page_count;
	off64_t size;
	size = lseek64(fd, 0, SEEK_END);

	needed_page_count = bid2pid(block_off) + 1;
	current_page_count = (size / ODB_PAGESIZE);

	// first, check if we need to initialize any new groups.

	if (needed_page_count > current_page_count) {

		// if we need extra space, but we don't have write permission, then
		// there's nothing we can do.

		if(!(desc->flags & ODB_PWRITE)) {
			return ODB_ENOSPACE;
		}

		// so we need to truncate some extra space. But, another process could
		// have already done so sense we called the lseek64. So we need to lock
		// what we currently think is the end of file and make sure to
		// remeasure.

		size               = page_lock_eof(fd, 1);
		current_page_count = (size / ODB_PAGESIZE);

		if (needed_page_count <= current_page_count) {
			page_unlock_eof(fd, size);
			return 0;
		}

		off64_t newSize = (off64_t) needed_page_count * ODB_PAGESIZE;
		int     err     = ftruncate64(fd, newSize);
		page_unlock_eof(fd, size);
		if (err == -1) {
			switch (errno) {
			case EFBIG:
				log_alertf(
						"failed to create block groups: would exceed OS limit");
				return ODB_ENOSPACE;
			default: return log_critf("failed to truncate file");
			}
		}
	}
	return 0;
}

