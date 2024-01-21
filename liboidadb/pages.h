#ifndef OIDADB_PAGESI_H
#define OIDADB_PAGESI_H

#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>

typedef struct meta_pages {
	struct super_descriptor *sdesc;
	struct group_descriptor *gdesc;
	odb_revision (*versionv)[ODB_SPEC_PAGES_PER_GROUP];
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
	odb_pid    cursor_pid;
	odb_bid    cursor_bid;
	off64_t    cursor_off;
	meta_pages loaded_group;
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
 * long enough to include said group.
 *
 * You can also include any amount of blocks to initialize into the group if
 * they haven't already been.
 *
 * In a special block device, this function will only really make sure that
 * goff/boff don't exceed the size of the device.
 *
 * Handles process locking.
 */
odb_err group_truncate(odb_desc *desc, odb_gid goff, odb_bid bcount);

/**
 * Loads the meta pages for the provided group. Does not touch the super
 * descriptor. Will initialize everything else if it hasn't been already.
 *
 * Handles process locking.
 */
odb_err group_load(odb_desc *desc, odb_gid gid, meta_pages *o_group);

/**
 * If group is null, nothing happens.
 */
void group_unload(odb_desc *desc, meta_pages group);


/**
 * Will lock the given blocks so that no other process can modify them.
 */
odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc);

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc);

/**
 * will return ODB_EVERSION if there is a version mis-match between the proposed
 * vers and the current version of the given blocks.
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_match_versions(odb_desc *desc
                              , odb_bid bid
                              , int blockc
                              , const odb_revision *vers);

/**
 * Takes the given blocks and backs them up, so the next call to blocks_rollback
 * will revert any changes made by blocks_commit_attempt.
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_backup(odb_desc *desc, odb_bid bid, int blockc);

odb_err blocks_rollback(odb_desc *desc, odb_bid bid, int blockc);

/**
 * attempts to update all blocks provided in the bidv array with the associative
 * blockv array. If this function returns an error, you should definitely call
 * blocks_rollback. If successful, then all the blocks were successfully updated
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_commit_attempt(odb_desc *desc
                              , odb_bid bid
                              , int blockc
                              , const void *blockv);

/**
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err
blocks_versions(odb_desc *desc, odb_bid bid, int blockc, odb_revision *o_verv);

odb_err blocks_copy(odb_desc *desc, odb_bid bid, int blockc, void *o_blockv);

#endif //OIDADB_PAGESI_H
