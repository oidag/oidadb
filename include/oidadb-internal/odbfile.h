#ifndef _edbtypes_h_
#define _edbtypes_h_

#include <oidadb/oidadb.h>

#include <unistd.h>
#include <stdint.h>


#define ODB_SPEC_HEADER_MAGIC (uint8_t []){0xA6, 0xF0}

#define blocksize() 4096

// the super block is at the very start of the volume.
// right after the superblock is
struct super_descriptor {

	// meta
	uint8_t magic[2];
	uint8_t entrysize;
	uint8_t intsize;
	uint16_t pagesize;

	// id
	char id[32];

	// (everything else is reserved)

} __attribute__((__packed__));
const int super_block_size = 4096;


// the blockgroup
struct group_descriptor {

};

struct meta_block {
	// super_descriptor may be 0val if copies are not needed.
	struct super_descriptor super;
	struct group_descriptor group;
};


#endif