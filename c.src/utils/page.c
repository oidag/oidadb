#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "odb-explorer.h"
#include "../edbd.h"
#include "../edbp.h"

#define pagetop "--------------------------------------------------------------------------------"
#define pagelen "76"

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
	for(int i = 0; i < head->refc; i++) {
		printf("| ");
		charsprintedthisline += printf("[ref: %ld, s/e-off: %ld] "
				, lrefs[i].ref
				, lrefs[i].startoff_strait);
		printedthisline++;
		if(i+1 == head->refc || printedthisline == 4) {
			for(int j = 0; j < (76 - charsprintedthisline); j++) {
				printf(" ");
			}
			printf(" |\n");
			printedthisline = 0;
			charsprintedthisline = 0;
		}
	}
	printf(pagetop "\n");
	free(buffv);
}

int page_print(odb_pid pid) {
	odb_err err = 0;

	// todo: remove this paragraph
	int fd = open(filename, O_RDWR);
	edbd_t handle;
	edbd_config config = edbd_config_default;
	config.forceopen = 1;
	err = edbd_open(&handle, fd, config);
	if(err) {
		printf("err : %s\n", odb_errstr(err));
		close(fd);
		log_critf("");
		return 1;
	}


	edbpcache_t *edbpcache = 0;
	edbphandle_t *edbp = 0;
	if((err = edbp_cache_init(&handle, &edbpcache))) {
		log_critf("");
		goto close;
	}
	edbp_cache_config(edbpcache, EDBP_CONFIG_CACHESIZE, 1);
	if((err = edbp_handle_init(edbpcache, 12, &edbp))) {
		log_critf("");
		goto close;
	}

	if((err = edbp_start(edbp, pid))) {
		log_critf("");
		goto close;
	}
	void *page = edbp_graw(edbp);

	odb_type type = ((_odb_stdhead *)page)->ptype;
	printf("page#%ld: %s\n", pid, odb_typestr(type));
	switch (type) {
		case ODB_ELMLOOKUP: lookup(page); break;
		default: printf("do not know how to parse this type of page\n"); break;
	}

	edbp_finish(edbp);


	close:
	if(err) {
		printf("err : %s\n", odb_errstr(err));
	}

	// todo: remove this paragraph
	if(edbp) edbp_handle_free(edbp);
	if(edbpcache) edbp_cache_free(edbpcache);
	edbd_close(&handle);
	close(fd);

	return err;
}