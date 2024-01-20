#ifndef _edbtypes_h_
#define _edbtypes_h_

#include <oidadb/oidadb.h>

#include <unistd.h>
#include <stdint.h>


#define ODB_SPEC_HEADER_MAGIC (uint8_t []){0xA6, 0xF0}

#define ODB_SPEC_PAGES_PER_GROUP 1024
#define ODB_SPEC_BLOCKS_PER_GROUP 1022
#define ODB_SPEC_METAPAGES_PER_GROUP 2

// we export these fields in its own struct just so we make sure that
// its packed. As the meta structure should be portable.
struct super_meta {
	uint8_t  magic[2];
	uint16_t version;
	uint16_t pagesize;
	char     uuid[16];
} __attribute__((__packed__));

// the super block is at the very start of the volume.
// right after the superblock is
struct super_descriptor {

	struct super_meta meta;

};


// the blockgroup
struct group_descriptor {

};

struct meta_block {
	// super_descriptor may be 0val if copies are not needed.
	struct super_descriptor super;
	struct group_descriptor group;
};


#endif