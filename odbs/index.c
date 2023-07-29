#include <stdio.h>
#include "odbs.h"

int index_print() {

	const odb_spec_head *head_page = odbfile_head();
	printf("page size    - %d bytes (%d x %d)\n", odbfile_pagesize(), head_page->intro.pagesize, head_page->intro.pagemul);
	printf("index pages  - %d pages\n", head_page->indexpagec);
	printf("struct pages - %d pages\n", head_page->structpagec);

	printf("\n");
	printf("| %5s | %-14s | %5s | %5s | %5s | %5s | %5s | %7s | %7s | %9s | %9s |\n"
		   , "eid"
		   , "type"
		   , "sid"
		   , "objpp" // objects per page
		   , "lkppp" // lookups per page
		   , "ref0"
		   , "ref0c"
		   , "bgn-lkp"
		   , "lst-lkp"
			, "lkp-depth"
			, "trashlast");
	const odb_spec_index_entry *ent;
	for(odb_eid eid = 0; ; eid++) {
		ent = odbfile_ent(eid);
		if(!ent) break;
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
		printf(" %9ld |", ent->trashlast);
		printf("\n");
	}
	printf("\n");
}
