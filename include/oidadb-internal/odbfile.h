#ifndef _edbtypes_h_
#define _edbtypes_h_

#include <oidadb/oidadb.h>

#include <unistd.h>
#include <stdint.h>


#define ODB_SPEC_HEADER_MAGIC {0xA6, 0xF0}
#define ODB_SPEC_HEADER_VERSION 0x1

#define ODB_SPEC_SUPER_DESC_SIZE (ODB_PAGESIZE/2)
#define ODB_SPEC_GROUP_DESC_SIZE (ODB_PAGESIZE/2)
#define ODB_SPEC_VERSIONPAGE_SIZE ODB_PAGESIZE

#define ODB_SPEC_PAGES_PER_GROUP 1024
#define ODB_SPEC_BLOCKS_PER_GROUP 1022
#define ODB_SPEC_METAPAGES_PER_GROUP 2

#define ODB_SPEC_FLAG_GROUP_INIT 0x01

// we export these fields in its own struct just so we make sure that
// its packed. As the meta structure should be portable.
struct super_meta {
	uint8_t  magic[2];
	uint16_t version;
	uint16_t pagesize;

	char rsvd0[32];

} __attribute__((__packed__));

// the super block is at the very start of the volume.
// right after the superblock is
struct super_descriptor {
	struct super_meta meta;

	// groups_offset_last is set to the last group index to which the file had
	// been truncated too. So a new database
	// would have 0, but when a second group is created, this will be 1.
	//
	// groups_offset_last does not describe what groups have been initialized, just
	// how much room had been made.
	//
	// groups_limit is the maximum amount of groups that can possibly exist in
	// this db. In a block device, groups_offset_last and groups_limit will always
	// be equal.
	odb_gid groups_offset_last;
	odb_gid groups_limit; /* (odb_gid)-1 means no limit */
	/* all other space is reserved */
};

// the blockgroup
struct group_descriptor {

	// see ODB_SPEC_FLAG_GROUP_* constants
	uint16_t flags;
	uint16_t blocks_created;

	odb_gid group_offset;


	/* all other space is reserved */
};


#endif