#include <oidadb-internal/options.h>
#include "errors.h"
#include "blocks.h"

#include <oidadb/oidadb.h>
#include <oidadb-internal/odbfile.h>

#ifdef EDB_FUCKUPS
__attribute__((constructor))
static void checkstructures() {
	// all structures must be headsize
	if(sizeof(struct odb_block_group_desc) != ODB_PAGESIZE) {
		log_critf("not page size (%d): odb_block_group_desc (%ld)",
				ODB_PAGESIZE, sizeof(struct odb_block_group_desc));
	}
}
#endif