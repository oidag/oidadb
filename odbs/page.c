#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <oidadb-internal/odbfile.h>
#include "odbs.h"

#define pagetop "--------------------------------------------------------------------------------"
#define pagelen "76"
#define pagelend 76

void static lookup(void *page) {
	odb_spec_lookup *head = page;
	odb_spec_lookup_lref *lrefs = page + ODB_SPEC_HEADSIZE;
	printf(pagetop "\n");
	int buffc = strlen(pagetop);
	char *buffv = malloc(buffc);

	// the header
	sprintf(buffv, "Left Sibling: %ld    Parent: %ld    Depth: %d    Refc: %d    "
			, head->head.pleft
			, head->parentlookup
			, head->depth
			, head->refc);
	printf("| %-"pagelen"s |\n", buffv);

	int printedthisline = 0;
	int charsprintedthisline = 0;
	printf("| ");
	for(int i = 0; i < head->refc; i++) {
		charsprintedthisline += printf("[ref: %5ld, s/e-off: %5ld] "
				, lrefs[i].ref
				, lrefs[i].startoff_strait);
		printedthisline++;
		if(i+1 == head->refc || printedthisline == 2) {
			for(int j = 0; j < (pagelend - charsprintedthisline); j++) {
				printf(" ");
			}
			printf(" |\n| ");
			printedthisline = 0;
			charsprintedthisline = 0;
		}
	}
	printf(pagetop "\n");
	free(buffv);
}

void static object(void *page) {
	odb_spec_object *head = page;
	void *objs = page + ODB_SPEC_HEADSIZE;
	printf(pagetop "\n");
	int buffc = strlen(pagetop);
	char *buffv = malloc(buffc);

	// get the structure/index
	const odb_spec_struct_struct *stk = odbfile_stk(head->structureid);
	const odb_spec_index_entry   *ent = odbfile_ent(head->entryid);

	// the header
	sprintf(buffv, "off: %ld    next page: %ld    structure id: %d    fixedc: %d"
			, head->head.pleft
			, head->head.pright
			, head->structureid
			, stk->fixedc);
	printf("| %-"pagelen"s |\n", buffv);
	sprintf(buffv, "trash start: %d    trash count: %d    trashvor: %ld"
			, head->trashstart_off
			, head->trashc
			, head->trashvor);
	printf("| %-"pagelen"s |\n", buffv);



	int printedthisline = 0;
	int charsprintedthisline = 0;
	int objsperline = pagelend / (strlen(" [ 0000 -0000 ] "));
	int objsonline = 0;
	int buffoff = 0;
	for(int i = 0; i < (odbfile_pagesize() / stk->fixedc); i++) {
		odb_spec_object_flags *objflags = objs + i * stk->fixedc;
		if(*objflags & EDB_FDELETED) {
			buffoff += sprintf(buffv + buffoff, " [ %04x -%04x ] "
				   , i
				   , *((uint16_t *)( (void *)objflags + sizeof(odb_spec_object_flags) ))
				   );
		} else {
			buffoff += sprintf(buffv + buffoff, " [ %04x +%02x.. ] ", i
				   , (*((uint16_t *)((void *)objflags)+sizeof(odb_spec_object_flags))&0xFF00) >> 8);
		}
		objsonline++;
		if(objsonline >= objsperline) {
			printf("| %-"pagelen"s |\n", buffv);
			objsonline = 0;
			buffoff = 0;
		}

	}
	printf(pagetop "\n");
	free(buffv);
}

int page_print() {
	odb_pid pid = cmd_arg.pid;
	odb_err err = 0;

	void *page = odbfile_page(pid);

	odb_type type = ((_odb_stdhead *)page)->ptype;
	printf("page#%ld: %s\n", pid, odb_typestr(type));
	switch (type) {
		case ODB_ELMLOOKUP: lookup(page); break;
		case ODB_ELMOBJ: object(page); break;
		default: printf("do not know how to parse this type of page\n"); break;
	}

	return err;
}
