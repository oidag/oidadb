
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
#include <errno.h>
#include <stdarg.h>
#include "file.h"
#include "edbp.h"

unsigned int edbp_size(const edbpcache_t *c) {
	return c->page_size;
}

// changes the pid into a file offset.
off64_t static inline pid2off(const edbpcache_t *c, edb_pid id) {
	return (off64_t)id * edbp_size(c);
}
edb_pid static inline off2pid(const edbpcache_t *c, off64_t off) {
	return off / edbp_size(c);
}

// gets thee pages from either the cache or the file and returns an array of
// pointers to those pages. Will try to get up to len (at least 1 is guarenteed)
// but actual count will be set in o_len.
// o_pageslots should be an array len long, this will be the slots to where
// the pages were loaded into, thus the pages can be access via cache->pagebufv[o_pageslots[...]]
// pointers.
//
// returns only critical errors
//
// todo: update documentation to reflect signature
static edb_err lockpages(edbpcache_t *cache,
                         edb_pid starting,
                         edbp_slotid *o_pageslots) {
	// quick vars
	int fd = cache->fd;
	unsigned int pagesize = edbp_size(cache);

	// we need to get smart here...
	// Theres a chance that some of our pages in our desired array are loaded and
	// others are not. So we have to make sure we fault the missing ones while
	// also taking advantage of loading them with minimum IO ops (mmap).
	//
	// ALL WHILE we do not over engineer this function to the point where
	// it would be more efficient to be dumb about this whole process.

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
	unsigned int lowestscore = (unsigned int)(-1);

	// lock the page mutex until we have our slot locked.
	pthread_mutex_lock(&cache->mutexpagelock);
	cache->opcoutner++; // increase the op counter

	// lets loop through our cache and see if we have any of these pages loaded already.
	// We can also take this time to find what pages we can likely replace.
	for(unsigned int i = 0; i < cache->slot_count; i++) {
		unsigned int mslot = (cache->slot_nextstart + i) % cache->slot_count;
		edbp_slot *slot = &cache->slots[mslot];

		if(slot->id != starting) {
			// not the page we're looking for.
			// but sense we're here, lets find do some calculations on which page to
			// replace on the cance of a page fault.
			if(slot->locks == 0 && slot->pra_score < lowestscore) {
				lowestscore = slot->pra_score;
				slotswap = mslot;
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
		*o_pageslots = mslot;

		// before we return: in the case that the page was currently undergoing a swap
		// we'll wait for it here.
		syscall(SYS_futex, &slot->futex_swap, FUTEX_WAIT, 1, 0, 0, 0);

		// if the swap failed for whatever reason, we'll casecade fail. Sure we can try
		// to do the swap again, but frankly if we're out of memory then we're out of memory.
		switch (slot->futex_swap) {
			case 3:
				return EDB_ENOMEM;
			case 2:
				log_critf("fail-cascading because waiting on swap to finish had failed");
				return EDB_ECRIT;
			default:
				return 0;
		}
	}

	// At this point: Page fault.

	// increment the next start to decrease the chance of having page faults
	// happening on the same slot with equal scores.
	cache->slot_nextstart = (cache->slot_nextstart + 1) % cache->slot_count;

	// At this point, however we have which slot we can swap stored in slotswap.
	// So swap the slot with slotswap.
	edbp_slot *slot = &cache->slots[slotswap];
	slot->locks = 1;
	// reset LRU-K history
	slot->pra_k[1] = 0;
	slot->pra_k[0] = cache->opcoutner;
	// assign the new id
	slot->id = starting;
	// with the lock field set we can unlock the mutex and perform the
	// rest of our work in peace. Even though we didn't do the swap yet,
	// we're not going to slow everyone else down by keeping this page mutex
	// locked. So we set the futex_swap to 1 which will stop any subseqnet
	// locks from returning until the swap is complete.
	slot->futex_swap = 1;
	pthread_mutex_unlock(&cache->mutexpagelock);
	*o_pageslots = slotswap;

	// perform the actual swap.
	// deload the page that was already there.
	if(slot->page != 0) {// (if there was antyhing there)

		// recalculate checksum only when the page has been marked
		// as dirty.
		// note w=1 because the first word of the page is the checksum itself.
		if(slot->pra_hints & EDBP_HDIRTY) {
			uint32_t sum = 0;
			for (int w = 1; w < pagesize / sizeof(uint32_t); w++) {
				sum += ((uint32_t *) (slot->page))[w];
			}
			_edbp_stdhead *head = (_edbp_stdhead *)(slot->page);
			head->_checksum = sum;
		}

		// later: encrypt the body if page is supposed to be encrypted.

		// I invoke a explicit sync here.
		// later: hmmm... is this needed with O_DIRECT? would this be faster without while still in our
		//        risk tolerance?
		//msync(slot->page.head, pagesize, MS_SYNC);

		// reset the slot hints
		slot->pra_hints = 0;

		// do the actual unmap
		munmap(slot->page, pagesize);
	}

	// load in the new page
	void *newpage = mmap64(0, pagesize,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED, fd, pid2off(cache, starting));

	if(newpage == (void *)-1) {
		// out of memory/some other critical error: bail out.
		log_critf("failed to map page(s) into slot");
		int eno = errno;
		edb_err err;
		pthread_mutex_lock(&cache->mutexpagelock);
		slot->page = 0;
		slot->pra_score = 0;
		// handle nomem.
		if(eno == ENOMEM) {
			slot->futex_swap = 3;
			err = EDB_ENOMEM;
		} else {
			slot->futex_swap = 2;
			err = EDB_ECRIT;
		}
		syscall(SYS_futex, &slot->futex_swap, FUTEX_WAKE, INT_MAX, 0, 0, 0);
		pthread_mutex_unlock(&cache->mutexpagelock);
		errno = eno;
		return err;
	}

	// assign other memebers of the page data
	slot->page = newpage;


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
void static unlockpage(edbpcache_t *cache, edbp_slotid slotid) {
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
						(slot->pra_hints&EDBP_HDIRTY) + (slot->pra_hints >> 4)
				   )
				   * cache->slotboost
				   / EDBP_HMAXLIF;

	}

	pthread_mutex_lock(&cache->mutexpagelock);
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
	return;
}

// locks/unlocks the endoffile for new entries
void static lockeof(edbpcache_t *caache) {
	pthread_mutex_lock(&caache->eofmutext);
}
void static unlockeof(edbpcache_t *caache) {
	pthread_mutex_unlock(&caache->eofmutext);
}

// see conf->pra_algo
edb_err edbp_init(edbpcache_t *o_cache, const edb_file_t *file, edbp_slotid slotcount) {

	// invals
	if(!o_cache) return EDB_EINVAL;

	// initialize
	bzero(o_cache, sizeof(edbpcache_t)); // initilize 0
	int err = 0;

	// parent file
	o_cache->fd = file->descriptor;

	// page cache metrics
	o_cache->slot_count    = slotcount;
	o_cache->page_size    = file->head->intro.pagemul * file->head->intro.pagesize;

	// buffers
	// individual slots
	o_cache->slots = malloc(sizeof(edbp_slot) * o_cache->slot_count);
	if(o_cache->slots == 0) {
		log_critf("cannot allocate page buffer");
		edbp_decom(o_cache);
		return EDB_ENOMEM;
	}
	bzero(o_cache->slots, sizeof(edbp_slot) * o_cache->slot_count); // 0-out



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
	o_cache->slotboost = (unsigned int)(o_cache->slotboostCc * (float)o_cache->slot_count);
	o_cache->initialized = 1;
	return 0;
}
void    edbp_decom(edbpcache_t *cache) {
	if(!cache || !cache->initialized) return;

	// kill mutexes
	pthread_mutex_destroy(&cache->eofmutext);
	pthread_mutex_destroy(&cache->mutexpagelock);

	// munmap all slots that have data in them.
	for(int i = 0; i < cache->slot_count; i++) {
		if(cache->slots[i].page.head != 0) {
			msync(cache->slots[i].page.head, edbp_size(cache), MS_SYNC);
			munmap(cache->slots[i].page.head, edbp_size(cache));
			cache->slots[i].page.head = 0;
		}
	}

	// free memory
	if(cache->slots) free(cache->slots);

	// null out pointers
	cache->slots = 0;
	cache->slot_count = 0;
	cache->initialized = 0;
}

static unsigned int nexthandleid = 0;

// create handles for the cache
edb_err edbp_newhandle(edbpcache_t *cache, edbphandle_t *o_handle) {
	if (!cache || !o_handle) return EDB_EINVAL;
	bzero(o_handle, sizeof(edbphandle_t));
	o_handle->parent = cache;
	o_handle->id = ++nexthandleid;
	o_handle->lockedslotv = -1;
	return 0;
}
void    edbp_freehandle(edbphandle_t *handle) {
	if(!handle) return;
	if(!handle->parent) return;
	edbp_finish(handle);
	handle->parent = 0;
}

edb_err edbp_start (edbphandle_t *handle, edb_pid *id) {
	if(id == 0 || *id == 0) return EDB_EINVAL;
	if(handle->lockedslotv != -1) {
		log_errorf("cache handle attempt to double-lock");
		return EDB_EINVAL;
	}
	edb_pid setid = *id;

	// easy vars
	edbpcache_t *parent = handle->parent;
	int fd = handle->parent->fd;
	int err = 0;

	// are they trying to create a new page?
	if(setid == -1) {
		// new page must be created.
		lockeof(parent);
		//seek to the end and grab the next page id while we're there.
		off64_t fsize = lseek64(fd, 0, SEEK_END);
		setid = off2pid(parent, fsize + 1); // +1 to put us at the start of the next page id.
		// we HAVE to make sure that fsize/id is set properly. Otherwise
		// we end up truncating the entire file, and thus deleting the
		// entire database lol.
		if(setid == 0 || fsize == -1) {
			log_critf("failed to seek to end of file");
			unlockeof(parent);
			return EDB_ECRIT;
		}
		// do old-fasioned writes(2)s of some fresh pages
		size_t werr = ftruncate64(fd, fsize + edbp_size(parent));
		if(werr == -1) {
			log_critf("failed to truncate-extend for new pages");
			unlockeof(parent);
			return EDB_ECRIT;
		}
		unlockeof(parent);
		*id = setid;
	}

	// lock in the pages
	err = lockpages(parent, setid,
					&handle->lockedslotv);

	// later: HIID stuff should go here.
	return err;
}
void    edbp_finish(edbphandle_t *handle) {
	if(handle->lockedslotv != -1) {
		unlockpage(handle->parent,
		           handle->lockedslotv);
	}
	handle->lockedslotv = -1;
}

edbp_t *edbp_graw(edbphandle_t *handle) {
	if (handle->lockedslotv == -1) {
		log_errorf("call attempted to edbp_graw without having one locked");
		return 0;
	}
	return handle->parent->slots[handle->lockedslotv].page;
}

edb_err edbp_mod(edbphandle_t *handle, edbp_options opts, ...) {
	if(handle->lockedslotv == -1)
		return EDB_ENOENT;

	// easy pointers
	edbpcache_t *cache = handle->parent;

	edb_err err = 0;
	edbp_hint hints;
	va_list args;
	va_start(args, opts);
	switch (opts) {
		case EDBP_CACHEHINT:
			hints = va_arg(args, edbp_hint);
			cache->slots[handle->lockedslotv].pra_hints = hints;
			err = 0;
			break;
		case EDBP_ECRYPT:
		case EDBP_XLOCK:
		case EDBP_ULOCK:
		default:
			err = EDB_EINVAL;
			break;
	}
	va_end(args);
	return err;

}