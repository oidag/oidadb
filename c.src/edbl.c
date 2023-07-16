#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include "edbl.h"
#include "errors.h"
#include "pthread.h"

#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>


// edbd special-functions. See namespaces.org, see edbd-edbl.c.

//analgous to open(2). Will reopen the file used by edbd with flags and mode.
// Needed for fcntl(2) open file descriptors (OFD)
extern int edbl_reopen(const edbd_t *file, int flags, mode_t mode);

// returns the byte-offset to the given eid in the file.
unsigned int edbl_pageoffset(const edbd_t *file, odb_eid eid);

typedef struct edbl_host_t {
	const edbd_t *fd;
	pthread_mutex_t mutex_index;
	pthread_mutex_t mutex_struct;
} edbl_host_t;

typedef struct edbl_handle_t {
	edbl_host_t *parent;
	int fd_d;
} edbl_handle_t;

odb_err edbl_host_init(edbl_host_t **o_lockdir, const edbd_t *file) {

	// invals
	if(!o_lockdir) {
		return ODB_EINVAL;
	}

	// ret
	edbl_host_t *ret = malloc(sizeof(edbl_host_t));
	if(ret == 0) {
		if (errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		log_critf("malloc");
		return ODB_ECRIT;
	}
	bzero(ret, sizeof(edbl_host_t));
	*o_lockdir = ret;
	// **defer-on-error: free(ret);
	ret->fd = file;

	// mutexes.
	int err = pthread_mutex_init(&ret->mutex_struct,0);
	if(err) {
		log_critf("failed to initialize pthread");
		free(ret);
		return ODB_ECRIT;
	}
	err = pthread_mutex_init(&ret->mutex_index,0);
	if(err) {
		pthread_mutex_destroy(&ret->mutex_struct);
		free(ret);
		log_critf("failed to initialize pthread");
		return ODB_ECRIT;
	}
	err = pthread_mutex_init(&ret->mutex_index,0);
	if(err) {
		pthread_mutex_destroy(&ret->mutex_struct);
		free(ret);
		log_critf("failed to initialize pthread");
		return ODB_ECRIT;
	}
	return 0;
}
void    edbl_host_free(edbl_host_t *h) {
	pthread_mutex_destroy(&h->mutex_struct);
	pthread_mutex_destroy(&h->mutex_struct);
	free(h);
}
odb_err edbl_handle_init(edbl_host_t *host, edbl_handle_t **oo_handle) {
	if(!host || !oo_handle) {
		return ODB_EINVAL;
	}

	// We use O_DIRECT just cuz why not. We're never going to call read(2)
	// nor even mmap with this fd so let just make it as bare-bones as possible.
	int fd = edbl_reopen(host->fd, O_RDWR
	                               | O_DIRECT
								   | O_LARGEFILE
								   , 0666);
	if(fd == -1) {
		log_critf("failed to create open file descriptor");
		return ODB_ECRIT;
	}

	// malloc the return value
	edbl_handle_t *h = malloc(sizeof(edbl_handle_t));
	if(h == 0) {
		if (errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		log_critf("malloc");
		return ODB_ECRIT;
	}
	bzero(h, sizeof(edbl_handle_t));
	*oo_handle = h;
	h->parent = host;
	h->fd_d = fd;
	return 0;
}
void    edbl_handle_free(edbl_handle_t *handle) {
	if(handle == 0 || handle->parent == 0) {
		return;
	}
	// unlock everything
	struct flock64 f = {
			.l_type = F_UNLCK,
			.l_start = sysconf(_SC_PAGESIZE),
			.l_len = 0,
			.l_whence = SEEK_SET,
			.l_pid = 0,
	};
	fcntl64(handle->fd_d, F_OFD_SETLK, &f);
	close(handle->fd_d);
	free(handle);
}

// extracted so can be used between edbl_set and edbl_test functions.
// if test is non-null then will run fcntl(fd, F_OFD_GETLK, test), otherwise
// F_OFD_SETLKW will be used and nothing outputted.
static odb_err _edbl_set(edbl_handle_t *h, edbl_act action, edbl_lock lock,
                         struct flock64 *test) {
	struct flock64 *flock;
	struct flock64 flock_noptr;
	int cmd;
	if(!test) {
		flock = &flock_noptr;
		cmd = F_OFD_SETLKW;
	} else {
		flock = test;
		cmd = F_OFD_GETLK;
	}
	flock->l_whence = SEEK_SET;
	flock->l_pid = 0;
	if(test) {
		cmd = F_OFD_GETLK;
	}
	switch (action) {
		case EDBL_ARELEASE:
			flock->l_type = F_UNLCK;
			break;
		case EDBL_AXL:
			flock->l_type = F_WRLCK;
			break;
		case EDBL_ASH:
			flock->l_type = F_RDLCK;
			break;
		default:
			log_critf("edbl_set INVAL");
			return ODB_ECRIT;
	}

	// note: don't trust this after the switch statement.
	int fd = h->fd_d;
	const unsigned int pagesize = edbd_size(h->parent->fd);

	// set l_len and l_start
	flock->l_len = 1;
	switch (lock.type) {
		case EDBL_LFILE:
			flock->l_start = 0;
			if(fcntl64(fd, F_OFD_SETLKW, flock) == -1) break;
			return 0;

		case EDBL_LENTCREAT:
			flock->l_start = 0;
			lock.eid = EDBD_EIDINDEX;
			goto lock_entry_byte;
		case EDBL_LSTRUCTCREAT:
			flock->l_start = 0;
			lock.eid = EDBD_EIDSTRUCT;
			goto lock_entry_byte;
		case EDBL_LENTTRASH:
			// lets get cheeky.
			flock->l_start = offsetof(odb_spec_index_entry, trashlast);
			goto lock_entry_byte; // very cheeky.
		case EDBL_LREF0C:
			// ... keep it cheeky.
			flock->l_start = offsetof(odb_spec_index_entry, ref0c);
			goto lock_entry_byte;

		lock_entry_byte:
			flock->l_start += edbl_pageoffset(h->parent->fd, lock.eid);
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LENTRY:
			// clutch lock.
			flock->l_start = edbl_pageoffset(h->parent->fd, lock.eid);
			if(flock->l_type == F_UNLCK) {
				// simply release the second-byte lock.
				flock->l_start++;
				if(fcntl64(fd, cmd, flock) == -1) break;
				return 0;
			}
			// Engage the first-byte lock
			if(fcntl64(fd, cmd, flock) == -1) break;
			// engage the second-byte lock
			flock->l_start++;
			if(fcntl64(fd, cmd, flock) == -1) break;
			// release the first-byte lock.
			flock->l_start--;
			flock->l_type = F_UNLCK;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LLOOKUP_EXISTING:
			flock->l_start = pagesize * (off64_t)lock.lookup_pid;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LLOOKUP_NEW:
			flock->l_start = pagesize * (off64_t)lock.lookup_pid + 1;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LTRASHOFF:
			flock->l_start = pagesize * (off64_t)lock.object_pid;
			flock->l_start += offsetof(odb_spec_object, trashstart_off);
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LOBJPRIGHT:
			flock->l_start = pagesize * (off64_t)lock.object_pid;
			flock->l_start += offsetof(odb_spec_object, head.pright);
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LOBJBODY:
			flock->l_start = pagesize * (off64_t)lock.object_pid;
			flock->l_start += ODB_SPEC_HEADSIZE;
			flock->l_len = lock.page_size - ODB_SPEC_HEADSIZE;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LROW:
			flock->l_start = pagesize * (off64_t)lock.object_pid;
			flock->l_start += lock.page_ioffset;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		case EDBL_LARBITRARY:
			flock->l_start = lock.l_start;
			flock->l_len   = lock.l_len;
			if(fcntl64(fd, cmd, flock) == -1) break;
			return 0;

		default:
			log_critf("edbl_set INVAL");
			return ODB_ECRIT;
	}
	// if we're here then the above switch statement had an error.
	log_critf("critical error in edbl_set lock-phase switch");
	return ODB_ECRIT;
}

odb_err edbl_test(edbl_handle_t *h, edbl_act action, edbl_lock lock) {
	struct flock64 test;
	odb_err err = _edbl_set(h, action, lock, &test);
	if(err) {
		return err;
	}
	if(test.l_type == F_UNLCK) {
		return 0;
	} else {
		return ODB_EAGAIN;
	}

}
odb_err edbl_set(edbl_handle_t *h, edbl_act action, edbl_lock lock) {
	return _edbl_set(h, action, lock, 0);
}