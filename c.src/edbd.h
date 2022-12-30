#ifndef _FILE_H_
#define _FILE_H_ 1

#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "include/ellemdb.h"

struct _edb_fhead_intro {
	uint8_t magic[2];
	uint8_t intsize;
	uint8_t entrysize;
	uint16_t pagesize;
	uint16_t pagemul;
	char rsvd[24];
	char id[32];
} __attribute__((__packed__)); // we pack the intro to make it more universal.
typedef struct _edb_fhead_intro edb_fhead_intro;

typedef uint64_t eid;

typedef struct {
	// intro must be first in this structure.
	const edb_fhead_intro intro;

	edb_eid newest;
	uint32_t loadedpages;
	pid_t host;
	uint64_t handlers;
	uint64_t lastsync;
	char rsvd[72];
} edb_fhead;

typedef struct edb_file_st {
	int descriptor;

	// the path that was used to open the file.
	const char *path;

	// the stat it was opened with
	struct stat openstat;

	// points to head page
	//
	// total allocation of head will always be sysconf(_SC_PAGE_SIZE);
	// this page is allocated using MAP_SYNC.
	//
	edb_fhead *head;

	// calculated by native page size by page mutliplier.
	// doesn't change after init.
	uint16_t       page_size;

	// eofmutext that controls the creation/deletion of pages.
	pthread_mutex_t eofmutext;

} edbd_t;

typedef struct edb_deletedref_st {
	edb_pid ref;
	uint16_t straitc;
	uint16_t _rsvd;
} edb_deletedref_t;

// simply returns the size of the pages found in this cache.
// note: this can be replaced with a hardcoded macro in builds
// that only support a single page multiplier
unsigned int edbd_size(const edbd_t *c);

// open, create, and close valid edb files. does not edit
// Head-Meta after loading.
//
// flags is an or'd bitflags of the EDB_H... constant family.
//
// edb_fileopen can return the following:
//   EDB_EERRNO - from stat(2) or open(2)
//   EDB_EFILE  - path is not a regular file,
//   EDB_ENOTDB - if bad magic number (probably meaning not a edb file)
//   EDB_EHW    - if invalid hardware.
//
// edb_fileclose will preserve errno.
//
edb_err edbd_open(edbd_t *file, const char *path, unsigned int pagemul, int flags);
void    edbd_close(edbd_t *file);


// see edb_index and edb_structs.
// these do the exact same thing but only specifically needs the shm and will return
// the pointer to the mmap'd region rather than copy the data.
//
// This memory is mapped to the file. Changes are persistant.
//
// These functions are dumb; does not check validitity of eid/structiid, thus no errors
// can be returned.
//
// Note: does nothing with locks. Be sure to use edbl properly.
//
// ERRORS:
//   - EDB_EEOF: returned by both when the submitted id is out of bounds.
edb_err edbd_index(const edbd_t *file, edb_eid eid, edb_entry_t **o_entry);
edb_err edbd_struct(const edbd_t *file, uint16_t structureid, edb_struct_t **o_struct);

// edbd_add
//   is the most primative way to create. will create a page strait of length straitc and will return the
//   first page in that strait's id in o_pid. The pages' binary will be 0-initialized.
//
// edbd_del
//   This will delete the pages.
//
// ERRORS:
//   - EDB_EINVAL: (edbd_add) pid was null
//   - EDB_EINVAL: (edbd_del) pid was 0
//   - EDB_EINVAL: straitc is 0.
//   - EDB_ENOSPACE (edbd_add): file size would exceed maximum file size
//   - EDB_ECRIT: other critical error (logged)
//
// THREADING:
//   Thread safe per file.
edb_err edbd_add(edbd_t *desc, uint8_t straitc, edb_pid *o_id);
edb_err edbd_del(edbd_t *parent, uint8_t straitc, edb_pid id);

// helper functions

// changes the pid into a file offset.
off64_t inline edbd_pid2off(const edbd_t *c, edb_pid id) {
	return (off64_t)id * edbd_size(c);
}
edb_pid inline edbd_off2pid(const edbd_t *c, off64_t off) {
	return off / edbd_size(c);
}


#endif
