#define _GNU_SOURCE
#include "odb-structures.h"
#include "options.h"
#include "errors.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef EDB_FUCKUPS
__attribute__((constructor))
static void checkstructures() {
	// all structures must be headsize
	if(sizeof(odb_spec_lookup) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_lookup (%ld)",
				  ODB_SPEC_HEADSIZE, sizeof(odb_spec_lookup));
	}
	if(sizeof(odb_spec_object) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_object (%ld)",
		          ODB_SPEC_HEADSIZE, sizeof(odb_spec_object));
	}
	if(sizeof(odb_spec_dynamic) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_dynamic (%ld)",
		          ODB_SPEC_HEADSIZE, sizeof(odb_spec_dynamic));
	}
	if(sizeof(odb_spec_struct) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_struct (%ld)",
		          ODB_SPEC_HEADSIZE, sizeof(odb_spec_struct));
	}
	if(sizeof(odb_spec_index) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_index (%ld)",
		          ODB_SPEC_HEADSIZE, sizeof(odb_spec_index));
	}
	if(sizeof(odb_spec_deleted) != ODB_SPEC_HEADSIZE) {
		log_critf("not headsize (%d): odb_spec_deleted (%ld)",
		          ODB_SPEC_HEADSIZE, sizeof(odb_spec_deleted));
	}
	if(sizeof(odb_spec_struct_full_t) !=
	   sizeof(odb_spec_struct_struct)
	   + sizeof(odb_dyptr)
	   + sizeof(odb_spec_object_flags)) {
		log_critf("full odb_struct structure not equal to its componenets");
	}
}
#endif

void edba_u_initobj_pages(void *page, odb_spec_object header,
                          uint16_t fixedc, unsigned int objectsperpage) {

	// set up the header
	odb_spec_object *phead = (odb_spec_object *)page;
	*phead = (odb_spec_object){
			.structureid = header.structureid,
			.entryid = header.entryid,
			.trashvor = header.trashvor,
			.trashc = objectsperpage,
			.trashstart_off = 0,

			.head.pleft = header.head.pleft,
			.head.pright = header.head.pright,
			.head.ptype = ODB_ELMOBJ,
			.head.rsvd = 0,
	};

	// set up the body
	void *body = page + ODB_SPEC_HEADSIZE;
	for(int i = 0; i < objectsperpage; i++) {
		void *obj = body + fixedc * i;
		odb_spec_object_flags *flags = obj;
		// mark them as all deleted. And daisy chain the trash
		// linked list.
		*flags = EDB_FDELETED;
		uint16_t *nextdeleted_rowid = obj + sizeof(odb_spec_object_flags);
		if(i + 1 == objectsperpage) {
			// last one, set to -1.
			*nextdeleted_rowid = (uint16_t)-1;
		} else {
			*nextdeleted_rowid = ((uint16_t)i)+1;
		}
		// note we don't need to touch the dynamic pointers because they should all be
		// 0 (null). And we know any byte we don't touch will be 0.
	}
}

odb_err edb_host_getpid(const char *path, pid_t *outpid) {

	int err = 0;
	int fd = 0;
	struct flock dblock = {0};

	// open the file for the only purpose of reading locks.
	fd = open(path,O_RDONLY);
	if (fd == -1) {
		log_errorf("failed to open pid-check descriptor");
		return ODB_EERRNO;
	}
	// read any fcntl locks
	dblock = (struct flock){
			.l_type = F_WRLCK,
			.l_whence = SEEK_SET,
			.l_start = 0,
			.l_len = 1, // first byte but mind you any host should have
			// locked the entire first page
			.l_pid = 0,
	};
	err = fcntl(fd, F_OFD_GETLK, &dblock);
	if(err == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("fcntl(2) returned unexpected errno: %d", errnotmp);
		errno = errnotmp;
		return ODB_ECRIT;
	}

	// read the head to make sure its an odb file
	odb_spec_head head;
	ssize_t n = read(fd, &head, sizeof(odb_spec_head));
	if(n == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("failed to read file header");
		errno = errnotmp;
		return ODB_ECRIT;
	}
	// we can close the file now sense we got all the info we needed.
	close(fd);

	// analyze the results of the lock.
	if(dblock.l_type == F_UNLCK) {
		// no (running) host connected to this file.
		return ODB_ENOHOST;
	}

	// atp: we know that there's a live process currently attached to this file.
	//      We need that processes's PID. We cannot use dblock.l_pid as we are
	//      dealing with OFD locks, which will always return l_pid == -1. But
	//      we can always just look at the head for the pid.

	// analyzie head
	if(n != sizeof(odb_spec_head)
	   || head.intro.magic[0] != ODB_SPEC_HEADER_MAGIC[0]
	   || head.intro.magic[1] != ODB_SPEC_HEADER_MAGIC[1]) {
		return ODB_ENOTDB;
	}
	// host successfully found.
	*outpid = head.host;
	return 0;
}