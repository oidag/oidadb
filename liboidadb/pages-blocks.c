#define _LARGEFILE64_SOURCE

#include <sys/mman.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "mmap.h"
#include "pages.h"
#include "errors.h"
#include "errno.h"

struct blockmap {

	odb_bid block_start;

	int blockc;

	// array of pointers (that point to mapped memory to
	// the file). caller of blocks_map is responsible for
	// allocating this array.
	struct odb_block **blockv;

	// array (each page is mapped to the file)
	// caller of blocks_map is responsible for setting this to
	// an anonymous map.
	//
	// when mapped, data_pagev is WRITE ONLY (PROT_WRITE)
	odb_datapage *data_pagem;

	// should be a map buffer of groups_needed() page size.
	struct odb_block_group_desc *groupm;
	int                         groupc;


	// used for INVAL checking. Must be initialized to 0.
	int _groups_loaded;
};

odb_pid bid2pid(odb_bid bid) {
	uint64_t full_groups          = bid / ODB_SPEC_BLOCKS_PER_GROUP;
	uint64_t blocks_in_last_group = (bid % ODB_SPEC_BLOCKS_PER_GROUP);
	uint64_t pid = (full_groups * ODB_SPEC_PAGES_PER_GROUP) + blocks_in_last_group + ODB_SPEC_METAPAGES_PER_GROUP;
#ifdef EDB_FUCKUPS
	if (!(pid % ODB_SPEC_PAGES_PER_GROUP)) {
		log_critf("block locking mis-calculation: points to group page");
	}
#endif
	return pid;
}

static odb_gid bid2gid(odb_bid bid) {
	uint64_t group_index = bid / ODB_SPEC_BLOCKS_PER_GROUP;
	return group_index;
}

unsigned int static blocks_remaining_in_group(unsigned int blockc
								 , unsigned int blocks_mapped
								 , unsigned int blockoff_group) {
	unsigned int blocks_in_group = blockc - blocks_mapped;
	if (blockoff_group + blocks_in_group > ODB_SPEC_BLOCKS_PER_GROUP) {
		blocks_in_group = ODB_SPEC_BLOCKS_PER_GROUP - blockoff_group;
	}
	return blocks_in_group;
}

odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc, int xl) {
	odb_bid block_end = blockc + bid;
	for (; bid < block_end; bid++) {
		odb_pid pid = bid2pid(bid);
		page_lock(desc->fd, pid, xl);
	}
	return 0;
}

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc) {
	odb_bid block_end = blockc + bid;
	for (; bid < block_end; bid++) {
		odb_pid pid = bid2pid(bid);
		page_unlock(desc->fd, pid);
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
 * @param o_blockv output pointer to write blocks to. If null, blocks are not
 *                 loaded
 * @param o_versionv output pointer to write block versions to
 */
static odb_err blocks_copy_from_group(const odb_desc *desc
                                      , const struct odb_block_group_desc *groupm
                                      , odb_gid group_off
                                      , int block_off
                                      , int blockc
                                      , odb_datapage *o_blockv
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

	odb_err err               = 0;
	int     fd                = desc->fd;
	// convert the block offset to a page offset, we just need to add the
	// meta pages.
	int     block_page_offset = ODB_SPEC_METAPAGES_PER_GROUP + block_off;

	// later: implement my own page caching using read(2) here fails to
	//  utilize proper copy-on-write behaviour. But, we cannot use mmap due
	//  to the fact that a copy-on-write is "unspecified" to occur when
	//  another process writes to the block we map.
	//  But I won't let this slow me down, I'll just use reads for now, it'll
	//  eat up ram, but we can fix it eventually without messing with the API

	if (o_blockv) {
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
			default: err    = log_critf("failed call to read");
			}
			return err;
		}
	}

	// copy the versions
	if (lseek64(fd
	            , ((off64_t) group_off * ODB_SPEC_PAGES_PER_GROUP)
	              * ODB_PAGESIZE
	            , SEEK_SET) == -1) {
		err = log_critf("failed call to lseek");
		return err;
	}
	for (int i = 0; i < blockc; i++) {
		o_versionv[i] = groupm->blocks[block_off + i].block_ver;
	}

	return 0;
}

odb_err blocks_copy(odb_desc *desc
                    , int blockc
                    , struct odb_block_group_desc *restrict buff_group_descm
                    , odb_datapage *restrict o_dpagev
                    , odb_revision *restrict o_blockv) {
	odb_err err;

	odb_bid block_start = desc->cursor.cursor_bid;

	int     blocks_copied = 0;
	odb_gid group_start   = bid2gid(block_start);
	odb_gid group_end     = group_start + descriptor_buffer_needed(block_start, blockc);

	// starting_group_block: if they wanted to copy blocks that start in the
	// middle of the group.
	int blockoff_group = (int) (block_start % ODB_SPEC_BLOCKS_PER_GROUP);

	void *o_blockv_adjusted;

	for (odb_gid group_off = group_start; group_off < group_end; group_off++) {
		err = group_loadg(desc, group_off, buff_group_descm);
		if (err) {
			break;
		}

		// The blocks in the group to which we must copy will either be whatever
		// is left to copy, or, whatever is left in the group.
		unsigned int blocks_in_group = blocks_remaining_in_group(blockc
		                                                         , blocks_copied
		                                                         , blockoff_group);

		if (o_dpagev) {
			o_blockv_adjusted = o_dpagev + blocks_copied * ODB_BLOCKSIZE;
		} else {
			o_blockv_adjusted = 0;
		}

		err = blocks_copy_from_group(desc
		                             , buff_group_descm
		                             , group_off
		                             , blockoff_group
		                             , blocks_in_group
		                             , o_blockv_adjusted
		                             , &o_blockv[blocks_copied]);
		if (err) {
			break;
		}
		blocks_copied += blocks_in_group;

		// If we're moving to the next group, we start on block 0 of the
		// next group. Also load that next group.
		blockoff_group = 0;
	}



	if (err) {
		return err;
		// don't update the cursor
	}

#ifdef EDB_FUCKUPS
	if (blocks_copied != blockc) {
		return log_critf("block count mis-match");
	}
#endif

	// todo: remove the cursor?
	//desc->cursor.cursor_bid += blockc;
	return 0;
}


int descriptor_buffer_needed(odb_bid block_start, int blockc) {
	odb_gid group_start = bid2gid(block_start);
	odb_bid block_end   = block_start + blockc - 1;
	odb_gid group_end   = bid2gid(block_end);
	return (int) (group_end - group_start) + 1;
}

// helper function to blocks_commit_attempt
//
// does nothing with locking
//
// maps all the descriptor information into easy-to-access arrays inside of the
// blockmap structure, provided you do all the allocating yourself.
//
// does not touch bmap->data_pagev (see data_map)
static odb_err group_map(const odb_desc *desc
                         , struct blockmap *bmap) {
	if (bmap->blockc <= 0 || bmap->groupc <= 0) {
		return ODB_EINVAL;
	}
	if (bmap->groupm == 0) {
		return ODB_EINVAL;
	}

	odb_err err = 0;

	odb_gid group_start = bid2gid(bmap->block_start);
	int     groupc      = bmap->groupc;

	int     blockc      = bmap->blockc;
	odb_bid block_start = bmap->block_start;

	int     blocks_copied = 0;

	// starting_group_block: if they wanted to copy blocks that start in the
	// middle of the group.
	int blockoff_group = (int) (block_start % ODB_SPEC_BLOCKS_PER_GROUP);

	struct odb_block_group_desc *group_ptr;
	for (int                group_index = 0;
	     group_index < groupc; group_index++) {
		group_ptr = (((void *) bmap->groupm) + group_index * ODB_PAGESIZE);
		err       = group_loadg(desc, group_start + group_index, group_ptr);
		if (err) {
			break;
		}

		// The blocks in the group to which we must copy will either be whatever
		// is left to copy, or, whatever is left in the group.
		unsigned int blocks_in_group = blocks_remaining_in_group(blockc
		                                                         , blocks_copied
		                                                         , blockoff_group);

		for (int block_index = blockoff_group;
		     block_index < blockoff_group+blocks_in_group; block_index++) {
			bmap->blockv[blocks_copied] = &group_ptr->blocks[block_index];
			blocks_copied++;
		}

		// If we're moving to the next group, we start on block 0 of the
		// next group. Also load that next group.
		blockoff_group = 0;
	}
#ifdef EDB_FUCKUPS
	if(blocks_copied != bmap->blockc) {
		log_critf("block load mis-match");
	}
#endif
	bmap->_groups_loaded = 1;
	return err;
}

// helper function to blocks_commit_attempt
//
// requires group_map be called first.
//
// note that an blocks_munmap is not needed sense the contents of bmap are all
// pre-allocated.
static odb_err data_map(const odb_desc *desc
                        , struct blockmap *bmap) {
	if (bmap->blockc <= 0) {
		return ODB_EINVAL;
	}
	if (!bmap->_groups_loaded) {
		return ODB_EINVAL;
	}
	if (bmap->groupm == 0) {
		return ODB_EINVAL;
	}
	if (bmap->data_pagem == 0) {
		return ODB_EINVAL;
	}

	odb_err err = 0;

	int     blockc      = bmap->blockc;
	odb_bid block_start = bmap->block_start;

	unsigned int blocks_mapped = 0;
	odb_gid      group_start   = bid2gid(block_start);
	int     groupc        = bmap->groupc;

	// starting_group_block: if they wanted to copy blocks that start in the
	// middle of the group.
	int blockoff_group = (int) (block_start % ODB_SPEC_BLOCKS_PER_GROUP);


	for (int group_index = 0; group_index < groupc; group_index++) {

		// The blocks in the group to which we must copy will either be whatever
		// is left to copy, or, whatever is left in the group.
		unsigned int blocks_in_group = blocks_remaining_in_group(blockc
				, blocks_mapped
				, blockoff_group);
		// map all the pages in this group
		odb_datapage *dest_addr = bmap->data_pagem +
		                          ODB_PAGESIZE * blocks_mapped;
		odb_gid current_group          = group_start + group_index;
		odb_pid first_data_page_offset = current_group * ODB_SPEC_PAGES_PER_GROUP
		                                 + ODB_SPEC_METAPAGES_PER_GROUP
		                                 + blockoff_group;
		if (odb_mmap(dest_addr
		             , blocks_in_group
		             , PROT_WRITE
		             , MAP_SHARED | MAP_FIXED
		             , desc->fd
		             , first_data_page_offset) == MAP_FAILED) {
			return odb_mmap_errno;
		}

		blocks_mapped += blocks_in_group;

		// If we're moving to the next group, we start on block 0 of the
		// next group. Also load that next group.
		blockoff_group = 0;
	}
#ifdef EDB_FUCKUPS
	if(blocks_mapped != bmap->blockc) {
		log_critf("block load mis-match");
	}
#endif
	return err;
}

odb_err blocks_commit_attempt(const odb_desc *desc
                              , struct block_commit_buffers commit) {

	int blockc = commit.blockc;
	if (blockc <= 0) {
		return ODB_EINVAL;
	}
	odb_bid block_start = commit.block_start;
#ifdef EDB_FUCKUPS
	if(descriptor_buffer_needed(block_start, blockc) != commit.buffer_group_descc) {
		return log_critf("group descriptor buffer not correct");
	}
#endif
	odb_err err;

	struct blockmap bmap = {0};
	bmap.block_start = block_start;
	bmap.blockc     = blockc;
	bmap.blockv     = odb_malloc(sizeof(struct odb_block *) * blockc);
	bmap.data_pagem = commit.buffer_datam;
	bmap.groupm     = commit.buffer_group_descm;
	bmap.groupc     = commit.buffer_group_descc;

	// check the versions
	err = group_map(desc, &bmap);
	if (err) {
		odb_free(bmap.blockv);
		return err;
	}
	err = 0;
	for (int i = 0; i < blockc; i++) {
		commit.buffer_versionv[i] = bmap.blockv[i]->block_ver;
		if (commit.user_versionv[i] != commit.buffer_versionv[i]) {
			err = ODB_EVERSION;
			// we don't return here so we can make sure to return all the current
			// versions.
		}
	}
	if (err) {
		odb_free(bmap.blockv);
		return err;
	}

	// versions OK. Map the data pages.
	err = data_map(desc, &bmap);
	if (err) {
		odb_free(bmap.blockv);
		return err;
	}

	// later: if I need to implement any journalling, it will be about here.

	for (int i = 0; i < blockc; i++) {

		// copy the page over
		void       *dest = (void *)bmap.data_pagem + ODB_PAGESIZE * i;
		const void *src  = (void *)commit.user_datam + ODB_PAGESIZE * i;
		memcpy(dest, src, ODB_PAGESIZE);

		// update the version
		bmap.blockv[i]->block_ver++;
		commit.buffer_versionv[i] = bmap.blockv[i]->block_ver;
	}

	// done
	odb_free(bmap.blockv);
	return 0;
}