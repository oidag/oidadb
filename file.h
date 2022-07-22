#ifndef _FILE_H_
#define _FILE_H_ 1

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct _edb_fhead_intro {
	char magic[2];
	uint8_t intsize;
	uint8_t entrysize;
	uint16_t pagesize;
	uint16_t flags;
	char rsvd[24];
	char id[32];
} __attribute__((__packed__)); // we pack the intro to make it more universal.
typedef struct _edb_fhead_intro edb_fhead_intro;

typedef uint64_t eid;

typedef struct {
	const edb_fhead_intro intro;
	eid newest;
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

} edb_file_t;


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
edb_err edb_fileopen(edb_file_t *file, const char *path, int flags);
void edb_fileclose(edb_file_t *file);

#endif
