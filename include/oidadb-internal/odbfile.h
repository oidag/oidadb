#ifndef _edbtypes_h_
#define _edbtypes_h_

#include <oidadb/oidadb.h>

#include <unistd.h>
#include <stdint.h>


#define ODB_SPEC_HEADER_MAGIC {0xA6, 0xF0}
#define ODB_SPEC_HEADER_VERSION 0x1

#define ODB_SPEC_PAGES_PER_GROUP 1024
#define ODB_SPEC_BLOCKS_PER_GROUP 1023
#define ODB_SPEC_METAPAGES_PER_GROUP 1

#define ODB_SPEC_FLAG_GROUP_INIT 0x01
#define ODB_SPEC_FLAG_BLOCK_GROUP 0x04


struct odb_block {
	uint16_t data_page_off;
	uint16_t rsvd0;
	uint32_t block_ver;
};

// the blockgroup
struct odb_block_group_desc {
	uint8_t magic[2];

	// see ODB_SPEC_FLAG_GROUP_* constants
	uint16_t flags;
	uint64_t rsvd0;

	struct odb_block blocks[1023];

};

/**
 * odb_datapage* will always be a void pointer. But I will typedef it here just
 * to make code a bit more readable.
 */
typedef void odb_datapage;


#endif