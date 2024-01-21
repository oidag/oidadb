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
	uint32_t          groups_created;
	uint32_t          groups_limit; /* -1 means no limit */
	/* all other space is reserved */
};


// the blockgroup
struct group_descriptor {
	uint32_t blocks_created;

	/* all other space is reserved */
};


#endif