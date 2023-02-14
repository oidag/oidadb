#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1

#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>


// edbd special-functions. See namespaces.org, see edbd-edbl.c.

//analgous to open(2). Will reopen the file used by edbd with flags and mode.
// Needed for fcntl(2) open file descriptors (OFD)
extern int edbl_reopen(const edbd_t *file, int flags, mode_t mode);

// returns the byte-offset to the given eid in the file.
unsigned int edbl_pageoffset(const edbd_t *file, edb_eid eid);

typedef struct edbl_host_t {
	const edbd_t *fd;
	pthread_mutex_t mutex_index;
	pthread_mutex_t mutex_struct;
} edbl_host_t;

typedef struct edbl_handle_st {
	edbl_host_t *parent;
	int fd_d;
} edbl_handle_t;

edb_err edbl_host_init(edbl_host_t **o_lockdir, const edbd_t *file) {

	// invals
	if(!o_lockdir) {
		return EDB_EINVAL;
	}

	// ret
	edbl_host_t *ret = malloc(sizeof(edbl_host_t));
	if(ret == 0) {
		if (errno == ENOMEM) {
			return EDB_ENOMEM;
		}
		log_critf("malloc");
		return EDB_ECRIT;
	}
	bzero(ret, sizeof(edbl_host_t));
	*o_lockdir = ret;
	// **defer-on-error: free(ret);

	// lock array
	// todo: this needs to be configurable.
	ret->lockc = EDBL_LOCK_BUFFV;
	ret->lockv = malloc(sizeof(edbl_lockref_full) * ret->lockc);
	if(ret->lockv == 0) {
		free(ret);
		if (errno == ENOMEM) {
			return EDB_ENOMEM;
		}
		log_critf("malloc");
		return EDB_ECRIT;
	}
	bzero(ret->lockv, sizeof(edbl_lockref_full) * ret->lockc);
	// **defer-on-error: free(ret->lockv);

	// mutexes.
	int err = pthread_mutex_init(&ret->mutex_struct,0);
	if(err) {
		log_critf("failed to initialize pthread");
		free(ret->lockv);
		free(ret);
		return EDB_ECRIT;
	}
	err = pthread_mutex_init(&ret->mutex_index,0);
	if(err) {
		pthread_mutex_destroy(&ret->mutex_struct);
		free(ret->lockv);
		free(ret);
		log_critf("failed to initialize pthread");
		return EDB_ECRIT;
	}
	err = pthread_mutex_init(&ret->mutex_index,0);
	if(err) {
		pthread_mutex_destroy(&ret->mutex_struct);
		free(ret->lockv);
		free(ret);
		log_critf("failed to initialize pthread");
		return EDB_ECRIT;
	}
	err = pthread_mutex_init(&ret->lock_mutex,0);
	if(err) {
		pthread_mutex_destroy(&ret->mutex_struct);
		pthread_mutex_destroy(&ret->mutex_struct);
		free(ret->lockv);
		free(ret);
		log_critf("failed to initialize pthread");
		return EDB_ECRIT;
	}
	return 0;
}

void    edbl_host_free(edbl_host_t *h) {
	pthread_mutex_destroy(&h->mutex_struct);
	pthread_mutex_destroy(&h->mutex_struct);
	free(h->lockv);
	free(h);
}

edb_err edbl_handle_init(edbl_host_t *host, edbl_handle_t **oo_handle) {
	if(!host || !oo_handle) {
		return EDB_EINVAL;
	}

	// malloc the return value
	edbl_handle_t *h = malloc(sizeof(edbl_handle_t));
	if(h == 0) {
		if (errno == ENOMEM) {
			return EDB_ENOMEM;
		}
		log_critf("malloc");
		return EDB_ECRIT;
	}
	bzero(h, sizeof(edbl_handle_t));
	*oo_handle = h;
	h->parent = host;
	return 0;
}
void edbl_handle_free(edbl_handle_t *handle) {
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

// below are some attempts of basically re-implementing fcntl advisory locks manually.
// im not sure why I didn't just use fcntl initally but, this code may come into use
// in the future if I decide that locks shouldn't be based on file bytes,
// which can allow of defragging whilest locks are in place.
/*
typedef struct edbl_lock_st {

	// cannot access without locking install_mutex
	int shlocks;

	uint64_t id;

	pthread_mutex_t mutex_exclusive;
	pthread_mutex_t mutex_shared; // will be locked so long that shlocks > 0
	pthread_mutex_t install_mutex;

	// hmmm, but how would I unlock properly? maybe a build flag that enables the unlock checking?
	// there should really be no reason to return errors sense locks are not handle-facing

} edbl_lock;


// returns the index of id
static unsigned int installlock(edbl_lock *l,
                           pthread_mutex_t *mutex,
                           uint32_t *futex,
                           unsigned int c,
                           uint64_t id,
                           int exclusive) {
	unsigned int i;
	// after the for-loop, this variable will point to a index that
	// has nothing in it, we can use this cell to install a new
	// lock if we find that no current locks exist for id.
	unsigned int first_emptycell = -1;
	tryagain:
	// wait on the futex if the array is full.
	// never mind... what our lock is laready in there.
	//syscall(SYS_futex, futex, FUTEX_WAIT, 1, 0, 0, 0);
	pthread_mutex_lock(mutex);
	// loop through the whole lock directory
	// if we find a compatible existing one, then add too it.
	// if we cannot find one, then create it.
	for(i = 0; i < c; i++) {

		if(l[i].id == id) {
			break;
		}

		// todo: removing empty locks must be done here (just check if shlocks is 0 and xl lock is open)


		if(first_emptycell == -1 && l[i].id == 0) {
			first_emptycell = 1;
		}
	}
	if(i == c) {
		// we didn't find any existing locks for this item.
		if (first_emptycell == -1) {
			// what has happened here is there are no empty slots to install
			// new locks and the provided id has no locks existing onto it.
			// So we must wait until one of the locks are released.
			// We wait on the relative futex.
			//
			// Once that's done we can go back and try again.
			//
			// todo: make sure the destroy lock logic signals this futex.
			*futex = 1;
			pthread_mutex_unlock(mutex);
			syscall(SYS_futex, futex, FUTEX_WAIT, 1, 0, 0, 0);
			goto tryagain;
		}
		// if we're here that meas we don't have an existing lock, however,
		// we do have an empty cell to install a new lock in.
		//
		// note we do this inside the mutex because its cheeper than worrying
		// about futex navigation.
		l[first_emptycell].id = id;
		i = first_emptycell;
	}
	// at this point, i is pointing to a position in the lock directory
	// that matches our id. now to deal with the lock type.
	pthread_mutex_unlock(mutex);
	edbl_lock *locktomodify = &l[i];


	// if something is holding an exclusive lock on it, we must wait, regardless
	// of what type of lock we're trying to install. So we attempt to lock the
	// excuislve mutex either way.
	pthread_mutex_lock(&locktomodify->mutex_exclusive);
	if(exclusive) {

		// Note, normally, right here I would unlock the mutex_exclusive so that
		// other installations and removals may take place while we're waiting. But this
		// is one of those rare cases where we DON'T do that. This is because we actively
		// prevent any other locks of either types tacking place until we get the unlock
		// signal. In otherwords, its a simple way of making sure everyone must wait their
		// turn.

		// wait until the shared mutex is completely unlocked.
		pthread_mutex_lock(&locktomodify->mutex_shared);

		// sense we're installing an exclusive lock we LEAVE the mutex_exclusive and mutex_shared
		// locked up.

	} else {
		pthread_mutex_lock(&locktomodify->install_mutex);
		// we're installing a shared lock. make sure the shared mutex is locked up
		// if it isn't already.
		pthread_mutex_trylock(&locktomodify->mutex_shared);
		locktomodify->shlocks++;

		// with ou shared lock installed, we unlock the exclusive mutex so other
		// shared mutexes can be installed if they need be.
		pthread_mutex_unlock(&locktomodify->mutex_exclusive);
		pthread_mutex_unlock(&locktomodify->install_mutex);
	}
	return i;
}
static edb_err removelock(edbl_lock *l,
                           pthread_mutex_t *mutex,
                           uint32_t *futex,
                           unsigned int c,
                           uint64_t id) {

	// first we must lock the install mutex to begin our search
	pthread_mutex_lock(mutex);

	// find the lock.
	unsigned int i;
	for(i = 0; i < c; i++) {
		if(l[i].id == id) {
			// found the lock
			break;
		}
	}
	if(i == c) {
		pthread_mutex_unlock(mutex);
		// did not find the lock with that ID. Dump something
		// in the logs.
		log_debugf("attempt to remove lock that didn't exist.");
		return 0;
	}
	edbl_lock *rmv = &l[i];

	// lock the shared lock install mutex
	// this will prevent a shared lock being installed at the exact same time
	// its being removed which can cause the mutex_shared to end up unlocked
	// with shared locks installed.
	pthread_mutex_unlock(mutex);
	pthread_mutex_lock(&rmv->install_mutex);

	// now we perform some logic. What type of lock is installed?
	// and how do we unlock the array install mutex as soon as possible? Because
	// we shouldn't unlock it until we update the ID.

	// lock the shared lock regardless if we have shared locks. This will
	// prevent exclusive installs.
	//pthread_mutex_trylock(&rmv->mutex_shared);

	// firstly, we must find out if we're trying to either remove a shared
	// or exlusive lock. We test shared first, if there
	if(!rmv->shlocks) {

		// this means that when this remove was called, there was no shared locks,
		// but sense the id is still in the lock array we can logically deduce
		// it had a exclusive lock on it.

#ifdef EDBL_DEBUG
		if( pthread_mutex_trylock(&rmv->mutex_exclusive) == 0 ) {
			// the only way to get in here (as far as I know) is if unlock is
			// called one too many times and is done so in a multithreaded enviroment.
			// This should not happen, if in here that means we have stumbled into a
			// lock that has no exclusive lock nor shlocks yet has a valid ID.
			//
			// Again, it's /possible/ to get in here but only when the caller has not
			// followed protocol.
			log_critf("multiplex criticality");
		}
#endif
		// theres an exclusive lock. Unlock the xl lock and allow for shared locks to be installed.
		// mutex_exclusive should be the last thing to be unlocked sense its the first lock
		// required for installing any lock.
		pthread_mutex_unlock(&rmv->mutex_shared);
		pthread_mutex_unlock(&rmv->install_mutex);
		pthread_mutex_unlock(&rmv->mutex_exclusive);
		return 0;
	}
	rmv->shlocks--;
	if(!rmv->shlocks) {
		// all shared locks removed.
		// unlock the shared mutex.
		pthread_mutex_unlock(&rmv->mutex_shared);
	}
	pthread_mutex_unlock(&rmv->install_mutex);
	return 0;
}*/

edb_err edbl_index(edbl_handle_t *lockdir, edbl_type type) {
	switch (type) {
		case EDBL_EXCLUSIVE:
			pthread_mutex_lock(&lockdir->parent->mutex_index);
			return 0;
		case EDBL_TYPUNLOCK:
			pthread_mutex_unlock(&lockdir->parent->mutex_index);
			return 0;
		case EDBL_TYPSHARED:
			log_critf("shared locks not allowed here");
			return EDB_ECRIT;
	}
	log_critf("broken case");
	return EDB_ECRIT;
}
edb_err edbl_struct(edbl_handle_t *lockdir, edbl_type type) {
	switch (type) {
		case EDBL_EXCLUSIVE:
			pthread_mutex_lock(&lockdir->parent->mutex_struct);
			return 0;
		case EDBL_TYPUNLOCK:
			pthread_mutex_unlock(&lockdir->parent->mutex_struct);
			return 0;
		case EDBL_TYPSHARED:
			log_critf("shared locks not allowed here");
			return EDB_ECRIT;
	}
	log_critf("broken case");
	return EDB_ECRIT;
}

int edbl_get(edbl_handle_t *lockdir, edbl_lockref lock) {
#ifdef EDB_FUCKUPS
	if(lock.l_type == EDBL_TYPUNLOCK) {
		log_critf("edlb_get called with an unlock");
		return 1;
	}
#endif
	struct flock64 f = {
			.l_type = lock.l_type,
			.l_len = lock.l_len,
			.l_start = (off64_t)(lock.l_start),
			.l_whence = SEEK_SET,
			.l_pid = 0,
	};
	int ret = fcntl64(lockdir->fd_d, F_OFD_GETLK, &f);
#ifdef EDB_FUCKUPS
	if(ret == -1) {
		log_critf("failed to get lock");
		return 0;
	}
#endif
	return f.l_type == F_UNLCK;
}

edb_err edbl_set(edbl_handle_t *lockdir, edbl_lockref lock) {
#ifdef EDB_FUCKUPS
	if(lock.l_len == 0) {
		log_critf("edbl_set: EDB_EINVAL");
		return EDB_EINVAL;
	}
	switch (lock.l_type) {
		case EDBL_TYPUNLOCK:
		case EDBL_EXCLUSIVE:
		case EDBL_TYPSHARED:
			break;
		default:
			log_critf("unknown lock type");
			return EDB_ECRIT;
	}
	if(lock.l_start <= sysconf(_SC_PAGE_SIZE)) {
		log_critf("attemp to modify lock on first page");
		return EDB_ECRIT;
	}
#endif
	struct flock64 f = {
			.l_type = lock.l_type,
			.l_len = lock.l_len,
			.l_start = (off64_t)(lock.l_start),
			.l_whence = SEEK_SET,
			.l_pid = 0,
	};
	int ret = fcntl64(lockdir->fd_d, F_OFD_SETLKW, &f);
#ifdef EDB_FUCKUPS
	if(ret == -1) {
		log_critf("failed to install set lock");
		return EDB_ECRIT;
	}
#endif
	return 0;
}