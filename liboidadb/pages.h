#ifndef OIDADB_PAGESI_H
#define OIDADB_PAGESI_H

#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>

typedef struct meta_pages {
	struct super_descriptor *sdesc;
	struct group_descriptor *gdesc;
	odb_revision *versionv; // [ODB_SPEC_PAGES_PER_GROUP]
} meta_pages;

enum hoststate {
	ODB_SNEW = 0,

	// opening file
	ODB_SFILE,

	ODB_SALLOC,
	ODB_SPREP,
	ODB_SREADY,
};

typedef struct odb_cursor {
	odb_gid    curosr_gid;
	odb_bid    cursor_bid;
} odb_cursor;

typedef struct odb_buf {
	struct odb_buffer_info info;

	// bidv will never be nil and will always be filled with the block ids
	// associative to revsionv and blockv.
	//
	// if info->usage is ODB_UBLOCKS then blockv will be mapped. Otherwise, with
	// ODB_UVERIONS, revisionsv will be mapped.
	//
	// all arrays will have info.bcount elements.
	//odb_bid      *bidv;
	odb_revision *revisionv;
	void         *blockv;

} odb_buf;

typedef struct odb_desc {
	int fd;

	// special flag set to 1
	const char *unitialized;

	meta_pages meta0;
	meta_pages group; // todo: do we need this?

	odb_ioflags flags;

	enum hoststate state;

	odb_buf *boundBuffer;

	// where the cursor is sense the last successful call
	// to odbp_seek. Will always be a multiple of ODB_PAGESIZE
	odb_cursor cursor;
} odb_desc;

/**
 * Will initialize the first super descriptor file described by fd so
 * long that it hasn't already been initialized.
 *
 * If fd shows that it was already initialized, ODB_EXISTS is returned.
 *
 * If the fd initialization status couldn't be determined, ODB_ECRIT is returned.
 *
 * If the fd has been found to be uninitialized, but otherwise failed to
 * initialize, ODB_EERRNO is returned.
 *
 * Handles process locking
 */
odb_err volume_initialize(int fd);

/**
 * will fail if the meta is not valid.
 *
 * Handles process locking.
 */
odb_err volume_load(odb_desc *desc);

void volume_unload(odb_desc *desc);

/**
 * Will make sure that the group offset exists in the file. If the file is a
 * regular file and does not contain the group offset, then the file is truncated
 * long enough to include said group. Does not initialize the group
 * (see group_load).
 *
 * You can also include any amount of blocks to initialize into the group if
 * they haven't already been.*
 *
 * In a special block device, this function will only really make sure that
 * goff/boff don't exceed the size of the device.
 *
 * Handles process locking.
 */
odb_err group_truncate(odb_desc *desc, odb_gid goff);

/**
 * Loads the meta pages for the provided group offset into desc->group. If
 * desc->group is non-null then it is unloaded. If the given group has not
 * been initialized, then it will be. Make sure the group offset has been
 * truncated to via group_truncate.
 *
 * Does not touch the super descriptor associated with the group.
 *
 * On error, desc->group is not touched at all.
 *
 * Handles process locking.
 *
 * The -g variant does the same but you can manually choose the load other than
 * desc (note: desc is const). Does not handle unloading already loaded pages,
 * which makes it prone to memory leaks if not used properly with group_unloadg.
 */
odb_err group_load(odb_desc *desc, odb_gid goff);
odb_err group_loadg(const odb_desc *desc, odb_gid gid, meta_pages *o_mpages);

/**
 * Will unload desc->group and set it to null.
 * If group is already null, desc is null, or already unloaded, nothing happens.
 *
 * the -g variant is mor cruel in that undefined behaviour will occur if called
 * with the group already unloaded
 */
void group_unload(odb_desc *desc);
void group_unloadg(meta_pages *group);

// All blocks_* methods require that cursor.loaded_group be valid

/**
 * Will lock the given blocks so that no other process can modify them.
 */
odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc);

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc);

/**
 * Will make sure versions are a match on each block, back up said block, then
 * attempt to commit the block. If there is any error, all changes to all blocks
 * are reverted.
 *
 * Will update cursor on success
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_commit_attempt(odb_desc *desc
                              , odb_bid bid
                              , int blockc
                              , const void *blockv
							  , const odb_revision *verv);

/**
 * Copies the amount of blocks blockc and their versions into o_blockv and their
 * versions into o_versionv. The starting block will be desc->cursor.cursor_bid.
 * If no error is returned, desc->cursor.cursor_bid is incremented by blockc.
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_copy(odb_desc *desc
					, int blockc
					, void *o_blockv
					, odb_revision *o_versionv);

// utility
void page_lock(int fd, odb_pid page, int xl);
void page_unlock(int fd, odb_pid page);

#endif //OIDADB_PAGESI_H
