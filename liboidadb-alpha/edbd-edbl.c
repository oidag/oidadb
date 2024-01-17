// this file is only met for edbl
#include "edbd_u.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

int edbl_reopen(const edbd_t *file, int flags, mode_t mode) {
	char buff[255];
	sprintf(buff, "/proc/%d/fd/%d", getpid(), file->descriptor);
	return open(buff, flags, mode);
}
unsigned int edbl_pageoffset(const edbd_t *file, odb_eid eid) {
	int pageoff = eid / file->enteriesperpage;
	return edbd_size(file) // skip intro page
	       + edbd_size(file) * pageoff // skip any pages containing previous eids
	       + ODB_SPEC_HEADSIZE // skip the head of that page.
		   + (eid % file->enteriesperpage *sizeof(odb_spec_index_entry));
}


// returns a pointer to the persistent memory of the structure pages.
void *edba_structurespages(const edbd_t *file) {
	return file->edb_structv;
}