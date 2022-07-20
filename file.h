#ifndef _FILE_H_
#define _FILE_H_ 1

#include <stdint.h>
#include <sys/types.h>
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

typedef struct {
	int descriptor;

	// the stat it was opened with
	struct stat openstat;

	// points to head page
	//
	// total allocation of head will always be sysconf(_SC_PAGE_SIZE);
	// this page is allocated using MAP_SYNC.
	//
	edb_fhead *head;

} edb_file;


// open, create, and close valid edb files. does not edit
// Head-Meta after loading.
//
// edb_fileclose will only ever return EDB_ECRIT (log_crit will already be called)
// and errno is preserved.
edb_err edb_fileopen(edb_file *file, edb_open_t params);
edb_err edb_fileclose(edb_file *file);

#endif
