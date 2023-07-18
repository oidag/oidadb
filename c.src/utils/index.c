#include <stdio.h>
#include "odb-explorer.h"
#include "../edbd.h"

int index_print() {
	int fd = open(filename, O_RDWR);
	edbd_t handle;
	edbd_config config = edbd_config_default;
	config.forceopen = 1;
	odb_err err = edbd_open(&handle, fd, config);
	if(err) {
		printf("err : %s\n", odb_errstr(err));
		close(fd);
		return 1;
	}
	printf("opened %s\n", filename);
	printf("page size    - %d bytes (%d x %d)\n", handle.page_size, handle.head_page->intro.pagesize, handle.head_page->intro.pagemul);
	printf("index pages  - %d pages\n", handle.head_page->indexpagec);
	printf("struct pages - %d pages\n", handle.head_page->structpagec);

	printf("\n");
	printf("| %5s | %-14s | %5s | %5s | %5s | %5s | %5s | %7s | %7s | %9s\n"
		   , "eid"
		   , "type"
		   , "sid"
		   , "objpp" // objects per page
		   , "lkppp" // lookups per page
		   , "ref0"
		   , "ref0c"
		   , "bgn-lkp"
		   , "lst-lkp"
			, "lkp-depth");
	odb_spec_index_entry *ent;
	for(odb_eid eid = 0; ; eid++) {
		if((err = edbd_index(&handle, eid, &ent))) {
			if(err == ODB_EEOF) {
				break;
			}
			printf("eid %d: Error loading: %s\n", eid, odb_errstr(err));
			continue;
		}
		if(ent->type == ODB_ELMINIT) {
			continue;
		}
		printf("| %5d |", eid);
		printf(" %-14s |", odb_typestr(ent->type));
		printf(" %5d |", ent->structureid);
		printf(" %5d |", ent->objectsperpage);
		printf(" %5d |", ent->lookupsperpage);
		printf(" %5ld |", ent->ref0);
		printf(" %5ld |", ent->ref0c);
		printf(" %7ld |", ent->ref1);
		printf(" %7ld |", ent->lastlookup);
		printf(" %9d |", ent->memory >> 12);
		printf("\n");
	}
	printf("\n");

	edbd_close(&handle);
	close(fd);
}