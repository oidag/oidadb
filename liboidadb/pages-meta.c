#define _LARGEFILE64_SOURCE

#include <sys/mman.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pages.h"
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

	if (ftruncate(fd, ODB_SPEC_SUPER_DESC_SIZE) == -1) {
		page_unlock(fd, 0);
		return ODB_EERRNO;
	}

	struct super_descriptor *d = mmap64(0
	                                    , ODB_SPEC_SUPER_DESC_SIZE
	                                    , PROT_READ | PROT_WRITE
	                                    , MAP_SHARED_VALIDATE
	                                    , fd
	                                    , 0);
	if (d == MAP_FAILED) {
		page_unlock(fd, 0);
		return ODB_EERRNO;
	}

	d->meta         = (struct super_meta) {
			.magic = ODB_SPEC_HEADER_MAGIC,
			.version = ODB_SPEC_HEADER_VERSION,
			.pagesize = ODB_PAGESIZE,
	};
	d->groups_limit = (uint32_t) -1;

	// sense meta_volume_initialize is so rarely called, lets dot our i's and
	// corss our t's by syncing right now.
	msync(d, ODB_SPEC_SUPER_DESC_SIZE, MS_SYNC);
	munmap(d, ODB_SPEC_SUPER_DESC_SIZE);
	page_unlock(fd, 0);
	return 0;
}

// helper to group_load.
static void group_initialize(struct group_descriptor *new_desc, odb_gid goff) {
	new_desc->flags |= ODB_SPEC_FLAG_GROUP_INIT;
	new_desc->group_offset = goff;
}

// note: desc is const.
odb_err group_loadg(const odb_desc *desc, odb_gid gid, struct meta_pages *o_mpages) {
#ifdef EDB_FUCKUPS
	if (desc->meta0.sdesc->groups_offset_last < gid) {
		return log_critf("cannot load group, index does not exist");
	}
#endif

	int     fd             = desc->fd;
	odb_pid pid            = gid * ODB_SPEC_PAGES_PER_GROUP;
	int     prot           = (int) desc->flags & 0x3;
	int     metapages_size = ODB_SPEC_SUPER_DESC_SIZE +
	                         ODB_SPEC_GROUP_DESC_SIZE +
	                         ODB_SPEC_VERSIONPAGE_SIZE;
	void    *metapages     = mmap64(0
	                                , metapages_size
	                                , prot
	                                , MAP_SHARED_VALIDATE
	                                , fd
	                                , (off64_t) pid);
	if (metapages == MAP_FAILED) {
		switch (errno) {
		case ENOMEM: return ODB_ENOMEM;
		default: return log_critf("failed to map meta pages");
		}
	}

	meta_pages newmetapages;
	newmetapages.sdesc = metapages;
	newmetapages.gdesc = metapages + ODB_SPEC_SUPER_DESC_SIZE;
	newmetapages.versionv =
			metapages + ODB_SPEC_SUPER_DESC_SIZE + ODB_SPEC_GROUP_DESC_SIZE;

	// remember, as per the definition of this function, we don't know if this
	// group has been initialized.

	// initialize the group if it hasn't already been initialized
	const volatile uint16_t *gflags = &newmetapages.gdesc->flags;
	if (!(*gflags & ODB_SPEC_FLAG_GROUP_INIT)) {
		// group has (probably) not been initialized... we need to XL lock it
		// to be sure.
		page_lock(fd, pid, 1);
		if (!(*gflags & ODB_SPEC_FLAG_GROUP_INIT)) {
			// yup, certainly not initialized, go ahead and do so.
			group_initialize(newmetapages.gdesc, gid);
		}
		page_unlock(fd, pid);
	}

	// switch out the loaded group.
	*o_mpages = newmetapages;
	return 0;
}

odb_err group_load(odb_desc *desc, odb_gid gid) {

	meta_pages ngroup;
	odb_err err = group_loadg(desc, gid, &ngroup);
	if(err) {
		return err;
	}

	// switch out the loaded group.
	group_unload(desc);
	desc->group = ngroup;
	return 0;
}

odb_err group_truncate(odb_desc *desc, odb_gid goff) {
	int     fd = desc->fd;

	// enable volatile because we use the 2-if-statement check before and
	// after the lock. We don't want the compiler to remove one.
	//
	// also: const volatile... that should help someone's bingo board.
	const volatile odb_gid *groups_index = &desc->meta0.sdesc->groups_offset_last;

	// first, see if the goff exists already. Remember that groups_index
	// never goes down, so we can perform this outside of locks confidently.
	if (goff <= *groups_index) {
		// group already exists
		return 0;
	}

	page_lock(fd, 0, 1);

	// yes, we run this same if statement again, because between now, and the
	// last if statement, another process could have just made the given group.
	if (goff <= *groups_index) {
		// group already exists
		page_unlock(fd, 0);
		return 0;
	}

	if (goff + 1 > desc->meta0.sdesc->groups_limit) {
		page_unlock(fd, 0);
		log_alertf(
				"failed to create any block groups: would exceed group limit");
		return ODB_ENOSPACE;
	}

	// We need to truncate the file to fit all groups up to this index as well as
	// the new metapages required for this group.
	off64_t newSize = (off64_t) goff * ODB_SPEC_PAGES_PER_GROUP +
	                  ODB_SPEC_METAPAGES_PER_GROUP;
	if (ftruncate64(fd, newSize) == -1) {
		page_unlock(fd, 0);
		switch (errno) {
		case EFBIG:
			log_alertf("failed to create block groups: would exceed OS limit");
			return ODB_ENOSPACE;
		default: return log_critf("failed to truncate file");
		}
	}

	// we increment group_index as the last step in case of crashes
	desc->meta0.sdesc->groups_offset_last = goff;
	page_unlock(fd, 0);
	return 0;
}

void group_unloadg(meta_pages *group) {
	int ret = munmap(group->sdesc, ODB_SPEC_SUPER_DESC_SIZE +
	                              ODB_SPEC_GROUP_DESC_SIZE +
	                              ODB_SPEC_VERSIONPAGE_SIZE);
	if (ret == -1) {
		log_critf("failed to munmap group");
	}
}

void group_unload(odb_desc *desc) {
	if (!desc) return;
	meta_pages group = desc->group;
	if (!group.sdesc) {
		log_debugf("mutliple calls to group_unload");
		return;
	}
	group_unloadg(&group);
	group.sdesc = 0;
	group.gdesc = 0;
	group.versionv = 0;
}

