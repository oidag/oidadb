#define _LARGEFILE64_SOURCE

#include <sys/mman.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pages.h"
#include "errors.h"
#include "errno.h"
#include "mmap.h"

static odb_pid bid2pid(odb_bid bid) {
	uint64_t group_index = bid / ODB_SPEC_BLOCKS_PER_GROUP;
	uint64_t meta_pages  = (group_index + 1) * ODB_SPEC_METAPAGES_PER_GROUP;
	return meta_pages + bid;
}

static odb_gid bid2gid(odb_bid bid) {
	uint64_t group_index = bid / ODB_SPEC_BLOCKS_PER_GROUP;
	return group_index;
}

odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc) {
	for (; bid < blockc + bid; bid++) {
		page_lock(desc->fd, bid, 0);
	}
	return 0;
}

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc) {
	for (; bid < blockc + bid; bid++) {
		page_unlock(desc->fd, bid);
	}
}

/**
 *
 * Copies block information from a loaded group.
 *
 * @param desc note: const
 * @param group note: const
 * @param group_off 0 = first group of volume
 * @param block_off 0 = first block of group, max of ODB_SPEC_BLOCKS_PER_GROUP - 1
 * @param blockc amount of blocks to copy, max of ODB_SPEC_BLOCKS_PER_GROUP
 * @param o_blockv output pointer to write blocks to
 * @param o_versionv output pointer to write block versions to
 */
static odb_err blocks_copy_from_group(const odb_desc *desc
                                      , meta_pages group
                                      , odb_gid group_off
									  , int block_off
									  , int blockc
									  , void *o_blockv
									  , odb_revision *o_versionv) {
#ifdef EDB_FUCKUPS
	// invals
	if (blockc > ODB_SPEC_BLOCKS_PER_GROUP) {
		return log_critf("cannot copy blocks more than what is in group");
	}
	if (block_off >= ODB_SPEC_BLOCKS_PER_GROUP) {
		return log_critf("block offset over or equal to blocks per group");
	}
#endif

	odb_err err = 0;
	int fd = desc->fd;
	// convert the block offset to a page offset, we just need to add the
	// meta pages.
	int block_page_offset = ODB_SPEC_METAPAGES_PER_GROUP + block_off;

	// later: implement my own page caching using read(2) here fails to
	//  utilize proper copy-on-write behaviour. But, we cannot use mmap due
	//  to the fact that a copy-on-write is "unspecified" to occur when
	//  another process writes to the block we map.
	//  But I won't let this slow me down, I'll just use reads for now, it'll
	//  eat up ram, but we can fix it eventually without messing with the API

	// copy the blocks
	if (lseek64(fd
	            , ((off64_t) group_off * ODB_SPEC_PAGES_PER_GROUP
	               + (off64_t) block_page_offset)
	              * ODB_PAGESIZE
	            , SEEK_SET) == -1) {
		err = log_critf("failed call to lseek");
		return err;
	}
	if (read(fd
	         , o_blockv
	         , blockc * ODB_BLOCKSIZE) == -1) {
		switch (errno) {
		case EBADF: err = ODB_EBADF;
		default: err = log_critf("failed call to read");
		}
		return err;
	}

	// copy the versions
	for (int i = 0; i < blockc; i++) {
		o_versionv[i] = group.versionv[block_page_offset + i];
	}

	return 0;
}

odb_err blocks_copy(odb_desc *desc
                    , int blockc
                    , void *o_blockv
                    , odb_revision *o_versionv) {
	odb_err err = 0;

	odb_bid block_start = desc->cursor.cursor_bid;
	odb_bid block_end   = block_start + blockc;

	int     blocks_copied = 0;
	odb_gid group_start   = bid2gid(block_start);
	odb_gid group_end     = bid2gid(block_end);

	// starting_group_block: if they wanted to copy blocks that start in the
	// middle of the group.
	int        blockoff_group = (int)(block_start % ODB_SPEC_BLOCKS_PER_GROUP);

	meta_pages group;
	for (odb_gid group_off = group_start; group_off <= group_end; group_off++) {
		err = group_loadg(desc, group_off, &group);
		if(err) {
			break;
		}

		// The blocks in the group to which we must copy will either be whatever
		// is left to copy, or, whatever is left in the group.
		int blocks_in_group = blockc - blocks_copied;
		if (blocks_in_group > ODB_SPEC_BLOCKS_PER_GROUP) {
			blocks_in_group = ODB_SPEC_BLOCKS_PER_GROUP;
		}

		err = blocks_copy_from_group(desc
		                       , group
		                       , group_off
		                       , blockoff_group
		                       , blocks_in_group
		                       , o_blockv + blocks_copied * ODB_BLOCKSIZE
							   , &o_versionv[blocks_copied]);
		if(err) {
			group_unloadg(&group);
			break;
		}
		blocks_copied += blocks_in_group;

		// If we're moving to the next group, we start on block 0 of the
		// next group. Also load that next group.
		blockoff_group = 0;
		group_unloadg(&group);
	}

	if(err) {
		// don't update the cursor
	} else {
		desc->cursor.cursor_bid += blockc;
	}
	return err;
}