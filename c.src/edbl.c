#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "edbl.h"
#include "errors.h"
#include "pthread.h"

/*
thinking out loud...

 lock mechanisms, as i know, are very important to have very tight rules.

 my first thoughts:

 multi-row locking must go from highest to lowest... can we get a dead lock?
 no. but is it practical?

 We can also just be like 'if you want to lock XYZ, you have to lock them
 all at the same time'.

 just gotta make sure that locks fail if the lock id is lower than
 the last one.

 For atomic transactions, another command should be ran by the handle
 to 'start transaciotn'... a smooth solution is that when that command
 is ran, no locks are released implicitly until the 'commit' is ran.
 That would be suuuuper smooth and I wish it would be that simple.
 We will see... my first instinct is if theres a select and then right after that
 there is a write, which, without unlocking would require elevating
 locks.... big nono. so i must be certain that will not cause a deadlock.

okay just got done with a multi-month hiatus: the order should be top to bottom and then there's an order on the types that need to be locked as well.

 but also.. the data structure.. need a quick way to find and install locks. which means use of an pre-alloc array.

 How many locks will a single handle need?

 acid: hmmm...
 update: no more than 5 lookups + an object page + a data page
 select: no more than 5 lookups + an object page + a data page
 alter: a whole entry, but thats it (a structure + all of objects + lookups + data)

 okay just keep a big ole single array of locks

 */

typedef struct edbl_lock_st {
	uint64_t id; // requires the array's mutex lock to access.

	//
	// bitmask:
	//   0010 = wait until no exclusive locks
	//   0001 = wait until no locks whatsoever
	//
	// 1 = shared locks present
	// 0 = no locks present
	hmmmmmmmmmmmm
	uint32_t lock_futex_bitmap;

	pthread_mutex_t install_mutex; // todo: rename ot exclusive_mutex

	// hmmm, but how would I unlock properly? maybe a build flag that enables the unlock checking?
	// there should really be no reason to return errors sense locks are not handle-facing

} edbl_lock;

typedef struct edbl_handle_st {
	edbl_host_t *parent;

#ifdef EDBL_DEBUG
	uint64_t laststructid;
	uint64_t lastentryid;
	uint64_t lastlookupid;
	uint64_t lastobjectid;
#endif

} edbl_handle_t;

// todo: inits / decoms

typedef struct edbl_host_st {
	edbl_lock *objectv;
	unsigned int objectc;
	pthread_mutex_t objmutex;
	// 1 = full
	uint32_t objfutex;

	edbl_lock *lookupv;
	unsigned int lookupc;
	pthread_mutex_t lookupmutex;
	uint32_t lookupfutex;

	edbl_lock *structv;
	unsigned int structc;
	pthread_mutex_t structmutex;
	uint32_t structfutex;

	edbl_lock *entryv;
	unsigned int entryc;
	pthread_mutex_t entrymutex;
	uint32_t entryfutex;
} edbl_host_t;

edb_err edbl_lobject(edbl_handle_t *lockdir, edb_oid oid, edbl_type type) {

#ifdef EDBL_DEBUG
	if(lockdir->lastobjectid !=0 && lockdir->lastobjectid <= oid) {
		log_critf("invalid lock order, try to lock %ld but last lock was less than that being %ld",
				  oid,
				  lockdir->lastobjectid);
		return EDB_ECRIT;
	}
#endif
	edbl_lock *l = lockdir->parent->objectv;
	pthread_mutex_t *mutex = &lockdir->parent->objmutex;
	uint32_t *futex = &lockdir->parent->objfutex;
	unsigned int c = lockdir->parent->objectc;
	uint64_t id = oid;

	// past this point: try to use generic terms to extract a shared funciton
	unsigned int i;
	// after the for-loop, this variable will point to a cell that
	// has nothing in it, we can use this cell to install a new
	// lock if we find that no current locks exist for id.
	unsigned int first_emptycell = -1;
	tryagain:
	// wait on the futex if the array is full
	syscall(SYS_futex, futex, FUTEX_WAIT, 1, 0, 0, 0);
	pthread_mutex_lock(mutex);
	// loop through the whole lock directory
	// if we find a compatible existing one, then add too it.
	// if we cannot find one, then create it.
	for(i = 0; i < c; i++) {
		if(l[i].id == id) {
			break;
		}
		if(first_emptycell == -1 && l[i].id == 0) {
			first_emptycell = 1;
		}
	}
	if(i == c) {
#ifdef EDBL_DEBUG
		if (type == EDBL_UNLOCK) {
			// someone just attempted to unlock something that doesn't
			// exist.
			log_critf("attempt to unlock id %ld when no lock exists", id);
			pthread_mutex_unlock(mutex);
			return EDB_ECRIT;
		}
#endif
		if (first_emptycell == -1) {
			// what has happened here is there are no empty slots to install
			// new locks and the provided id has no locks existing onto it.
			// So we must wait until one of the locks are released.
			// We wait on the relative futex.
			//
			// Once that's done we can go back and try again.
			*futex = 1;
			pthread_mutex_unlock(mutex);
			goto tryagain;
		}
		// if we're here that meas we don't have an existing lock, however,
		// we do have an empty cell to install a new lock in.
		//
		// note we do this inside the mutex because its cheeper than worrying
		// about futex navigation.
		l[first_emptycell].id = id;
		switch (type) {
			case EDBL_SHARED:
				l[first_emptycell].lock_futex = 1;
				break;
			case EDBL_EXCLUSIVE:
				l[first_emptycell].lock_futex = 2;
				break;
		}
		pthread_mutex_unlock(mutex);
		// later: put in tracking information here for the handle
		return 0;
	}
	// at this point, i is pointing to a position in the lock directory
	// that matches our id. now to deal with the lock type.
	pthread_mutex_unlock(mutex);
	edbl_lock *locktomodify = &l[i];


	stopping here. see the hmmm in 204. that is revealiving to be a killing blow to the flow.

	// if something is holding an exclusive lock on it, we must wait.
	//
	// note there's a small chance that even though the futex prooved valid outisde of the
	// cell's install mutex, but sense we've got into the install mutex that valid
	// sense changed to invalid. hense this label.
	uint32_t waitonval = 1;
	retrycell:
	syscall(SYS_futex, &(locktomodify->lock_futex_bitmap), FUTEX_WAIT_BITSET, 1, 0, 0, 0);
	pthread_mutex_lock(&locktomodify->install_mutex); // todo: unlock

	switch (type) {
		case EDBL_UNLOCK:
			hmmmmmmmmmmmmm how do i know if their last lock was shared or exclusive?
		case EDBL_SHARED:
			if(locktomodify->lock_futex == -1) goto retrycell;
			locktomodify->lock_futex++;
			break;
		case EDBL_EXCLUSIVE:
			if(locktomodify)
			 break;
	}

	l[i].lock_futex++


}