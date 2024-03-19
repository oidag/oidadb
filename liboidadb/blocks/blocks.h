#ifndef OIDADB_PAGESI_H
#define OIDADB_PAGESI_H

#include <oidadb-internal/options.h>

#include <oidadb/blocks.h>
#include <oidadb/buffers.h>
#include <oidadb-internal/odbfile.h>


struct block_commit_buffers {
	odb_bid block_start;
	int     blockc;

	// incoming data to be committed
	const odb_datapage *restrict user_datam;
	const odb_ver      *restrict user_versionv;

	// If blocks_commit_attempt returns ODB_EVERSION then this will be
	// set to all the current versions of the blocks.
	// If the ODB_ENONE is returned then this will be set to the new
	// (updated) versions.
	odb_ver *restrict buffer_versionv;

	// used as a buffer when loading in and out of pages. You just have to
	// make sure this is the same length as user_datam.
	odb_datapage *restrict buffer_datam;

	// group descriptor buffer.. see descriptor_buffer_needed
	int buffer_group_descc;
	struct odb_block_group_desc *restrict buffer_group_descm;


};

enum hoststate {
	ODB_SNEW = 0,

	// opening file
	ODB_SFILE,

	ODB_SALLOC,
	ODB_SPREP,
	ODB_SREADY,
};

typedef struct odb_cursor {
	odb_gid curosr_gid;
	odb_bid cursor_bid;
} odb_cursor;

typedef struct odb_buf {
	struct odb_buffer_info info;

	/*
	 * These are privately-mapped.
	 */
	odb_ver      *user_versionv;
	odb_datapage *user_datam;

	/**
	 * The following are only needed when committing (ODB_UCOMMITS)
	 *
	 *  - buffer_version - needed when committing. equal length to user_versionv.
	 *    When committing (or in the future, signaled) will have updated versions
	 *    of the current blocks
	 *  - buffer_data - used for committing. equal size ot user_datam. will be used
	 *    to hold the existing data maps.
	 *  - buffer_group_desc - buffer to hold group descriptor pages inside
	 */
	odb_ver      *buffer_versionv;
	odb_datapage *buffer_datam;

	/**
	 * map_statev is an array of uint32_t with each bit describing the
	 * associative page found in user_datam. Thus bit 0 represents page 0.
	 * The length of map_statev is (info->bcount / 32)+1.
	 */
	uint32_t *map_statev;
} odb_buf;

typedef struct odb_desc {
	int fd;

	// special flag set to 1
	const char *unitialized;

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
 * Will make sure that the block offset exists in the file. If the file is a
 * regular file and does not contain the block offset, then the file is truncated
 * long enough to include said group. Does not initialize the group
 * (see group_load).
 *
 * You can also include any amount of blocks to initialize into the group if
 * they haven't already been.
 *
 * Handles process locking.
 */
odb_err block_truncate(odb_desc *desc, odb_bid goff);

/**
 * Loads the descriptor page for the provided group offset in the location of o_map.
 * (map is assumed to be pre-allocated)
 * If the given group has not been initialized, then it will be. Make sure the
 * group offset has been truncated to via block_truncate.
 *
 * Handles process locking.
 *
 * Note: desc is const
 */
odb_err group_loadg(const odb_desc *desc
                    , odb_gid gid
                    , struct odb_block_group_desc *o_group_descm);

/**
 * Will lock the given blocks so that no other process can modify them.
 */
odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc, int xl);

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc);

/**
 * Will make sure versions are a match on each block, then
 * attempt to commit the blocks. If there's a version mis-match, ODB_EVERSION
 * is returned promptly.
 *
 * If there is any error, then no changes are applied to the database.
 *
 * Does NOT handle process locking, see blocks_lock.
 *
 * See block_commit_buffers for more info.
 *
 * Note: desc is const.
 */
odb_err blocks_commit_attempt(const odb_desc *desc
                              , struct block_commit_buffers commit);

/**
 * Copies the amount of blocks blockc and their data into o_dpagev and their
 * versions into o_versionv. The starting block will be desc->cursor.cursor_bid.
 * If no error is returned, desc->cursor.cursor_bid is incremented by blockc.
 *
 * If o_dpagev is null, then the blocks are not loaded. Good for when you only
 * want to load the versions.
 *
 * blocks_copy only needs 1 page for buff_group_descm
 *
 * Does NOT handle process locking, see blocks_lock
 */
odb_err blocks_copy(odb_desc *desc
                    , int blockc
                    , struct odb_block_group_desc *restrict buff_group_descm
                    , odb_datapage *restrict o_dpagev
                    , odb_ver *restrict o_blockv);

// utility
void page_lock(int fd, odb_pid page, int xl);

void page_unlock(int fd, odb_pid page);

odb_pid bid2pid(odb_bid bid);

/**
 * descriptor_buffer_needed is a small helper function that will calculate the
 * maximum number of block description pages that are needed to be loaded during
 * the commit process.
 *
 * For example, when committing lets say 8 continuous blocks, you may think
 * you'll need to load but 1 group descriptor, however, there is a chance you
 * need to load 2 sense the first 4 blocks can be at the tail-end of a group and
 * the next 4 blocks will be at the start of the next group.
 *
 * Likewise, if you are committing 1024 (which is 1 more block than a single
 * group can hold) blocks you'll only ever need 2 groups. As the first
 * block will be the last one in group 1, and the remaining 1023 blocks fit
 * into the second group entirely. Only when you commit 1025 blocks do you
 * need 3 descriptor blocks.
 */
int descriptor_buffer_needed(odb_bid block_start, int blockc);

#endif //OIDADB_PAGESI_H
