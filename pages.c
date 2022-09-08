
#define _LARGEFILE64_SOURCE     /* See feature_test_macros(7) */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <limits.h>


#include "file.h"
#include "pages.h"

off64_t static inline pid2off(edbp_id id);
edbp_id static inline off2pid(off64_t off);

// gets thee pages from either the cache or the file and returns an array of
// pointers to those pages. Will try to get up to len (at least 1 is guarenteed)
// but actual count will be set in o_len.
// o_pageslots should be an array len long, this will be the slots to where
// the pages were loaded into, thus the pages can be access via cache->pagebufv[o_pageslots[...]]
// pointers.
//
// returns only critical errors
//
// todo: right now we'll only ever return 1 page at a time. in the future we
//       can do strait-loading.
static edb_err lockpages(edbpcache_t *cache, edbp_id starting,
						 edbp_slotid len, edbp_slotid *o_len, edbp_slotid *o_pageslots) {
	// quick invals
	if(len == 0) {
		*o_len = 0;
		return 0;
	}

	// quick vars
	int fd = cache->file->descriptor;

	// we need to get smart here...
	// Theres a chance that some of our pages in our desired array are loaded and
	// others are not. So we have to make sure we fault the missing ones while
	// also taking advantage of loading them with minimum IO ops (mmap).
	//
	// ALL WHILE we do not over engineer this function to the point where
	// it would be more efficient to be dumb about this whole process.

	edbp_id il = starting;
	unsigned int pagebuflen = cache->pagebufc;
	unsigned int i;

	// if we have a page fault this is the index of the slot we must replace
	// These will be fully set after the for loop.
	//
	// Note that there should
	// never be a circumstance where all slots are locked to which the
	// for loop cannot find a possible replacement because the number of
	// slots will always be equal or more than the number of workers and
	// each worker can only lock a slot at a time. Thus, all workers will
	// always have call this function with 1 page unlocked.
	unsigned int slotswap;
	unsigned int lowestscore;

	// lock the page mutex until we have our slot locked.
	pthread_mutex_lock(&cache->mutexpagelock);
	cache->opcoutner++; // increase the op counter

	// lets loop through our cache and see if we have any of these pages loaded already.
	// We can also take this time to find what pages we can likely replace.
	for(i = 0; i < cache->pagebufc_w1; i++) {
		edbp_slot *slot = &cache->slots[i];

		if(slot->id != starting) {
			// not the page we're looking for.
			// but sense we're here, lets find do some calculations on which page to
			// replace on the cance of a page fault.
			if(slot->locks == 0 && slot->pra_score < lowestscore) {
				lowestscore = slot->pra_score;
				slotswap = i;
			}
			continue;
		}

		// we found our page in the cache at slot i. Add a lock so it doesn't deload
		slot->locks++;

		// rotate the LRU-1/LRU-2 history.
		slot->pra_k[1] = slot->pra_k[0];
		slot->pra_k[0] = cache->opcoutner;
		// quickly unlock the mutex because that's all we need to use it for.
		pthread_mutex_unlock(&cache->mutexpagelock);
		*o_len = 1;
		o_pageslots[0] = i;

		// before we return: in the case that the page was currently undergoing a swap
		// we'll wait for it here.
		syscall(SYS_futex, &slot->futex_swap, FUTEX_WAIT, 1, 0, 0, 0);

		// if the swap failed for whatever reason, we'll casecade fail. Sure we can try
		// to do the swap again, but frankly if we're out of memory then we're out of memory.
		if(slot->futex_swap == 2) {
			log_critf("fail-cascading because waiting on swap to finish had failed");
			return EDB_ECRIT;
		}

		return 0;
	}

	// At this point: Page fault.

	// At this point, however we have which slot we can swap stored in slotswap.
	// So swap the slot with slotswap.
	edbp_slot *slot = &cache->slots[slotswap];
	slot->locks = 1;
	// reset LRU-K history
	slot->pra_k[1] = 0;
	slot->pra_k[0] = cache->opcoutner;
	// assign the new id
	slot->id = starting;
	// set the strait size
	slot->strait = 1;
	// with the lock field set we can unlock the mutex and perform the
	// rest of our work in peace. Even though we didn't do the swap yet,
	// we're not going to slow everyone else down by keeping this page mutex
	// locked. So we set the futex_swap to 1 which will stop any subseqnet
	// locks from returning until the swap is complete.
	slot->futex_swap = 1;
	pthread_mutex_unlock(&cache->mutexpagelock);

	// perform the actual swap.
	// deload the page that was already there.
	if(slot->page != 0) {// (if there was antyhing there)

		// I invoke a explicit sync here.
		// todo: hmmm... is this needed with O_DIRECT? would this be faster without while still in our
		//       risk tolerance?
		msync(slot->page, slot->strait*EDBP_SIZE, MS_SYNC);

		// do the actual unmap
		munmap(slot->page, slot->strait * EDBP_SIZE);
	}
	slot->page = mmap(0, slot->strait * EDBP_SIZE,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED, fd, pid2off(starting));

	if(slot->page == (void *)-1) {
		// out of memory/some other critical error: bail out.
		pthread_mutex_lock(&cache->mutexpagelock);
		slot->page = 0;
		slot->pra_score = 0;
		// set swap to failure.
		slot->futex_swap = 2;
		syscall(SYS_futex, &slot->futex_swap, FUTEX_WAKE, INT_MAX, 0, 0, 0);
		pthread_mutex_unlock(&cache->mutexpagelock);

		log_critf("failed to map page(s) into slot");
		return EDB_ECRIT;
	}


	// swap is complete. let the futex know the page is loaded in now
	slot->futex_swap = 0;
	syscall(SYS_futex, &slot->futex_swap, FUTEX_WAKE, INT_MAX, 0, 0, 0);
	return 0;
}

// using pra_k and pra_hints this function will calculate and set pra_score
// before unlocking the page. The calculation is not technically thread-safe,
// but this is okay. The calculation will be a bit sloppy when doing intense
// multithreading but nonetheless will often result in similar results in
// terms of setting pra_score.
//
// Note to self: make sure that sloppyness doesn't impact performace too much
// by testing moving the calculation inside the lock.
//
// if the slot is already unlocked, nothing happens (logs will tho)
// will only ever return critical errors.
edb_err static unlockpage(edbpcache_t *cache, edbp_slotid slotid) {
	edb_err eerr = 0;
	edbp_slot *slot = &cache->slots[slotid];

	// calculate the pra_score
	//
	// This is a complex operation that has a good degree
	// of statsitical guess-work and will never be perfect.
	// Let me note this step by step:
	//
	// EDBP_HUSESOON - this will cause the LRU-K algo to use
	// LRU-1 instead of LRU-2.
	//
	// EDBP_HDIRTY and EDBP_HINDEX... will be added after the HINDEX is
	// shifted over 4 bits. After added, it will be devided by the MAXLIF
	// (which will always be bigger) and then multiplied by slotboost.
	if(slot->pra_hints & EDBP_HRESET) {
		slot->pra_score = 0;
	} else {
		slot->pra_score = slot->pra_k[1 - slot->pra_hints & 1] // see EDBP_HUSESOON
				+ (
						(slot->pra_score&EDBP_HDIRTY) + (slot->pra_score >> 4)
				   )
				   * cache->slotboost
				   / EDBP_HMAXLIF; // see EDBP_HINDEX...

	}

	pthread_mutex_lock(&cache->mutexpagelock);
	unsigned int locknum = slot->locks;
	if(slot->locks == 0) {
		// they're trying to unlock a page thats already unlocked.
		// return early.
		log_debugf("attempted to unlock a cache slot that was already completely unlocked (slot %d)", slot->locks);
		goto unlock;
	}

	// decrement the locknum.
	slot->locks--;

	// clean up
	unlock:
	pthread_mutex_unlock(&cache->mutexpagelock);
	return eerr;
}

// locks/unlocks the endoffile for new entries
void static lockeof(edbpcache_t *caache) {
	pthread_mutex_lock(&caache->eofmutext);
}
void static unlockeof(edbpcache_t *caache) {
	pthread_mutex_unlock(&caache->eofmutext);
}

// see conf->pra_algo
edb_err edbp_init(const edb_hostconfig_t conf, edb_file_t *file, edbpcache_t *o_cache) {
	bzero(o_cache, sizeof(edbpcache_t)); // initilize 0
	int err = 0;

	// pointers
	o_cache->file = file;
	o_cache->pagebufc    = conf.page_buffer + conf.page_buffer4 + conf.page_buffer8;
	o_cache->pagebufc_w1 = conf.page_buffer;
	o_cache->pagebufc_w4 = conf.page_buffer4;
	o_cache->pagebufc_w8 = conf.page_buffer8;

	// buffers
	// pageref array
	o_cache->slots = malloc(sizeof(edbp_slot) * o_cache->pagebufc);
	if(o_cache->slots == 0) {
		log_critf("cannot allocate page buffer");
		edbp_decom(o_cache);
		return EDB_ENOMEM;
	}
	bzero(o_cache->slots, sizeof(edbp_slot) * o_cache->pagebufc); // 0-out

	// mutexes
	err = pthread_mutex_init(&o_cache->eofmutext, 0);
	if(err) {
		log_critf("failed to initialize eof mutex: %d", err);
		edbp_decom(o_cache);
		return EDB_ECRIT;
	}
	err = pthread_mutex_init(&o_cache->mutexpagelock, 0);
	if(err) {
		log_critf("failed to initialize pagelock mutex: %d", err);
		edbp_decom(o_cache);
		return EDB_ECRIT;
	}

	// calculations
	o_cache->slotboostCc = EDBP_SLOTBOOSTPER;
	o_cache->slotboost = (unsigned int)(o_cache->slotboostCc * (float)o_cache->pagebufc);

	return 0;
}
void    edbp_decom(edbpcache_t *cache) {

	// kill mutexes
	pthread_mutex_destroy(&cache->eofmutext);
	pthread_mutex_destroy(&cache->mutexpagelock);

	// munmap all slots that have data in them.
	for(int i = 0; i < cache->pagebufc; i++) {
		if(cache->slots[i].page != 0) {
			msync(cache->slots[i].page, cache->slots[i].strait * EDBP_SIZE, MS_SYNC);
			munmap(cache->slots[i].page, cache->slots[i].strait * EDBP_SIZE);
			cache->slots[i].page = 0;
		}
	}

	// free memory
	if(cache->slots) free(cache->slots);

	// null out pointers
	cache->slots = 0;
	cache->pagebufc = 0;
	cache->file = 0;
}

// create handles for the cache
edb_err edbp_newhandle(edbpcache_t *cache, edbphandle_t *o_handle) {
	bzero(o_handle, sizeof(edbphandle_t));
	o_handle->parent = cache;
}
void    edbp_freehandle(edbphandle_t *handle); // todo: unlock anything

edb_err edbp_start (edbphandle_t *handle, edbp_id *id, unsigned int straits) {
	if(straits == 0) return EDB_EINVAL;
	if(id == 0 || *id == 0) return EDB_EINVAL;
	if(handle->lockedslotc != 0) {
		log_errorf("cache handle attempt to double-lock");
		return EDB_EINVAL;
	}

	// easy vars
	edbpcache_t *parent = handle->parent;
	int fd = handle->parent->file->descriptor;
	int err = 0;

	// are they trying to create a new page?
	if(*id == -1) {
		// new page must be created.
		lockeof(parent);
		//seek to the end
		*id = off2pid(lseek64(fd, SEEK_END, 0));
		// do old-fasioned writes(2)s of some fresh pages
		for(int i = 0; i < straits; i++) {
			edbp_t newpage = {0};
			err = write(fd, &newpage, sizeof(edbp_t));
			if(err == -1) {
				log_critf("failed to write new pages");
				unlockeof(parent);
				return EDB_ECRIT;
			}
		}
		unlockeof(parent);
		// new pages created.
	}

	// lock in the pages
	err = lockpages(parent, *id, straits,
					&handle->lockedslotc,
					handle->lockedslotv);
	return err;
}
void    edbp_finish(edbphandle_t *handle) {
	for(; handle->lockedslotc > 0; handle->lockedslotc--) {
		unlockpage(handle->parent,
				   handle->lockedslotv[handle->lockedslotc-1]);
	}
}