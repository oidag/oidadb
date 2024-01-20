#ifndef OIDADB_META_H
#define OIDADB_META_H

#include <oidadb/oidadb.h>

typedef struct meta_pages {
	struct super_descriptor *sdesc;
	struct group_descriptor *gdesc;
	odb_revision (*versionv)[ODB_SPEC_PAGES_PER_GROUP];
} meta_pages;


/**
 * Will initialize the first super descriptor file described by fd so
 * long that it hasn't already been initialized.
 */
odb_err meta_volume_initialize(int fd);

/**
 * Loads the meta pages for the provided group. Does not touch the super
 * descriptor. Will initialize everything else if it hasn't been already.
 */
odb_err meta_load(int fd, odb_gid gid, meta_pages *o_pages);
void meta_unload(meta_pages pages);

#endif //OIDADB_META_H
