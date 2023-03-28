#ifndef _edbdFILE_H_
#define _edbdFILE_H_ 1
#define _GNU_SOURCE

#include "odb-structures.h"
#include "include/oidadb.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

// todo: move these to odb_spec
#define EDBD_EIDINDEX  0
#define EDBD_EIDDELTED 1
#define EDBD_EIDSTRUCT 2
#define EDBD_EIDRSVD3  3
#define EDBD_EIDSTART  4 // start of the objects


typedef struct edb_file_st {
	int descriptor;

	// points to head page
	//
	// total allocation of head will always be sysconf(_SC_PAGE_SIZE);
	// this page is allocated using MAP_SYNC.
	//
	odb_spec_head *head_page;

	// calculated by native page size by page mutliplier.
	// doesn't change after init.
	uint16_t       page_size;

	// adddelmutex that controls the creation/deletion of pages.
	pthread_mutex_t adddelmutex;

	// static deleted page window.
	// delpages is an array of pointers to deleted pages.
	// Each page in delpages is full of edb_deletedref_t after their head.
	void **delpagesv; // the first page in the window will always be the (current) last page in the chapter.
	int    delpagesc; // current page amount that exist in delwindow
	int    delpagesq; // max amount of pages tha exist in delwindow

	// reserved entereis
	// An array of pointers. Each pointer is pointed to the first page of
	// edb_indexv.
	odb_spec_index_entry *rsvdents[EDBD_EIDSTART];

	// All edb_index/edb_structv pages. Memory is persistant.
	void *edb_indexv;
	int   edb_indexc;
	void *edb_structv;
	int   edb_structc;

	// helper vars (do not change after init)
	int enteriesperpage;
	int structsperpage;

} edbd_t;

typedef struct {
	// the max amount of odb_deleted pages to keep int the "deleted page window"
	// 8 should be good.
	//
	// must be at least 1.
	int delpagewindowsize;
} edbd_config;

// simply returns the size of the pages found in this cache.
// note: this can be replaced with a hardcoded macro in builds
// that only support a single page multiplier
unsigned int edbd_size(const edbd_t *c);

// open, create, and close valid edb files. does not edit
// Head-Meta after loading.
//
// Does nothing with locks.
//
// The descriptor is the main master descriptor. path is used for opening
// child descriptors and log messages.
//
// edbd_open can return the following:
//   ODB_EINVAL - invalid config
//   ODB_ENOMEM - not enough memory.
//   ODB_EERRNO - from stat(2) or open(2)
//   ODB_EFILE  - path is not a regular file,
//   ODB_ENOTDB - if bad magic number (probably meaning not a edb file)
//   ODB_EHW    - if invalid hardware.
//
// edb_fileclose will preserve errno.
//
odb_err edbd_open(edbd_t *o_file, int descriptor, edbd_config config);
void    edbd_close(edbd_t *file);

// Gives you a pointer to edbd memory in the index and structure buffers.
//
// This memory is mapped to the file. Changes are persistant (except for edbd_struct,
// I put the const constraint on it. You must edit structure data via edba).
//
// These functions are dumb; does not check validitity of eid/structiid.
// Doesn't check if said index is even initialized.
//
// Note: does nothing with locks. Be sure to use edbl properly.
//
// ERRORS:
//   - ODB_EEOF: returned by both when the submitted id is out of bounds.
//     Note that if you submit an id that hasn't have itself initialized,
//     then it will return errorless and the out pointer
//     will point to an unititalized entry.
odb_err edbd_index(const edbd_t *file, edb_eid eid,
                   odb_spec_index_entry **o_entry);
odb_err edbd_struct(const edbd_t *file, uint16_t structureid,
                    const odb_spec_struct_struct **o_struct);

// edbd_add
//   is the most primative way to create. will create a page strait of length straitc and will return the
//   first page in that strait's id in o_pid. The pages' binary will is NOT gaurenteed to be initialized,
//   meaning the whole page could be junk, its best to 0-initialize the whole page.
//
// edbd_del
//   This will delete the pages.
//
// ERRORS:
//   - ODB_EINVAL: (edbd_add) pid was null
//   - ODB_EINVAL: (edbd_del) pid was 0
//   - ODB_EINVAL: straitc is 0.
//   - ODB_ENOSPACE (edbd_add): file size would exceed maximum file size
//   - ODB_ECRIT: other critical error (logged)
//
// THREADING:
//   Thread safe per file.
odb_err edbd_add(edbd_t *file, uint8_t straitc, edb_pid *o_id);
odb_err edbd_del(edbd_t *file, uint8_t straitc, edb_pid id);

// helper functions

// changes the pid into a file offset.
static inline off64_t edbd_pid2off(const edbd_t *c, edb_pid id) {
	return (off64_t)id * edbd_size(c);
}
static edb_pid edbd_off2pid(const edbd_t *c, off64_t off) {
	return off / edbd_size(c);
}


#endif
