#define _LARGEFILE64_SOURCE     /* See feature_test_macros(7) */

#include "edbp_u.h"

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
                         edbphandle_t *h) {
	// quick vars
	edbp_slotid *o_pageslots = &h->lockedslotv;
	int fd = cache->fd->descriptor;
	unsigned int pagesize = edbd_size(cache->fd);

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
		errno = 0;

		// if the swap failed for whatever reason, we'll casecade fail. Sure we can try
		// to do the swap again, but frankly if we're out of memory then we're out of memory.
		switch (slot->futex_swap) {
			case 3:
				return EDB_ENOMEM;
			case 2:
				log_critf("fail-cascading because waiting on swap to finish had failed");
				return EDB_ECRIT;
			default:
				// was not undergoing a swap.
				return 0;
		}
	}

	// At this point: Page fault.

	// see slot_nextstart doc.
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
			// even though its a no-op on linux. Lets be a good boy and
			// explicitly call MS_ASYNC.
			msync(slot->page, pagesize, MS_ASYNC);
#ifdef EDB_OPT_CHECKSUMS
			uint32_t sum = 0;
			for (int w = 1; w < pagesize / sizeof(uint32_t); w++) {
				sum += ((uint32_t *) (slot->page))[w];
			}
			_odb_stdhead *head = (_odb_stdhead *)(slot->page);
			head->_checksum = sum;
#endif
		}

		// later: encrypt the body if page is supposed to be encrypted.

		// reset the slot hints
		slot->pra_hints = 0;

		// do the actual unmap
		munmap(slot->page, pagesize);
		telemetry_pages_decached(slot->id);
	}

	// load in the new page
	void *newpage = mmap64(0, pagesize,
		 PROT_READ | PROT_WRITE,
		                   MAP_SHARED, fd, edbd_pid2off(cache->fd, starting));

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
	telemetry_pages_cached(slot->id);
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
}

edb_err edbp_cache_config(edbpcache_t *pcache, edbp_config_opts opts, ...) {

	if(!pcache) return EDB_EINVAL;
	if(pcache->handles != 0) return EDB_EOPEN;

	va_list args;
	va_start(args, opts);
	switch (opts) {
		case EDBP_CONFIG_CACHESIZE:
			break;
		default:
			return EDB_EINVAL;
	}
	unsigned int slotcount = va_arg(args, unsigned int);
	va_end(args);

	// (re)allocate slots
	void *slots = realloc(pcache->slots,
							sizeof(edbp_slot) * slotcount);
	if (slots == 0) {
		if(errno == ENOMEM)
			return EDB_ENOMEM;
		log_critf("realloc");
		return EDB_ECRIT;
	}
	bzero(slots, sizeof(edbp_slot) * slotcount); // 0-out

	// assignments
	pcache->slots = slots;
	pcache->slot_count = slotcount;
	pcache->slotboostCc = EDBP_SLOTBOOSTPER;
	pcache->slotboost = (unsigned int) (pcache->slotboostCc *
	                                    (float) pcache->slot_count);
	return 0;
}

// see conf->pra_algo
edb_err edbp_cache_init(const edbd_t *file, edbpcache_t **o_cache) {

	// invals
	if(!file || !o_cache) return EDB_EINVAL;

	// malloc the actual handle
	*o_cache = malloc(sizeof(edbpcache_t));
	if(*o_cache == 0) {
		if(errno == ENOMEM)
			return EDB_ENOMEM;
		log_critf("malloc failed");
		return EDB_ECRIT;
	}
	edbpcache_t *pcache = *o_cache;

	// initialize
	bzero(pcache, sizeof(edbpcache_t));
	edb_err err = 0;

	// parent file
	pcache->fd = file;

	// mutexes
	err = pthread_mutex_init(&pcache->mutexpagelock, 0);
	if (err) {
		log_critf("failed to initialize pagelock mutex: %d", err);
		edbp_cache_free(pcache);
		return EDB_ECRIT;
	}
	pcache->initialized = 1;
	return 0;
}
void    edbp_cache_free(edbpcache_t *cache) {
	if(!cache) return;

	// kill mutexes
	pthread_mutex_destroy(&cache->mutexpagelock);

	// munmap all slots that have data in them.
	for(int i = 0; i < cache->slot_count; i++) {
		if(cache->slots[i].page != 0) {
			msync(cache->slots[i].page, edbd_size(cache->fd), MS_ASYNC);
			munmap(cache->slots[i].page, edbd_size(cache->fd));
			cache->slots[i].page = 0;
		}
	}

	// free slot memory
	if(cache->slots) free(cache->slots);

	// null out pointers
	free(cache);
}

// create handles for the cache
edb_err edbp_handle_init(edbpcache_t *cache,
						 unsigned int name,
						 edbphandle_t **o_handle) {
	if (!cache || !o_handle) return EDB_EINVAL;
	if (cache->slot_count < cache->handles + 1) return EDB_ENOSPACE;
	cache->handles++;

	// malloc the actual handle
	*o_handle = malloc(sizeof(edbphandle_t));
	if(*o_handle == 0) {
		if(errno == ENOMEM)
			return EDB_ENOMEM;
		log_critf("malloc failed");
		return EDB_ECRIT;
	}
	edbphandle_t *phandle = *o_handle;

	// initialize
	bzero(phandle, sizeof(edbphandle_t));
	phandle->parent = cache;
	phandle->name = name;
	phandle->lockedslotv = -1;

	return 0;
}
void    edbp_handle_free(edbphandle_t *handle) {
	if(!handle) return;
	if(!handle->parent) return;
	edbp_finish(handle);
	handle->parent->handles--;
	handle->parent = 0;
	free(handle);
}

edb_err edbp_start (edbphandle_t *handle, edb_pid id) {

	// invals
	if(id == 0) {
		log_critf("attempting to start id 0");
		return EDB_EINVAL;
	}
	if(handle->lockedslotv != -1) {
		log_errorf("cache handle attempt to double-lock");
		return EDB_EINVAL;
	}


	// easy vars
	edbpcache_t *parent = handle->parent;
	int err = 0;

	// make sure the id is within range
#ifdef EDB_FUCKUPS
	off64_t size = lseek64(parent->fd->descriptor, 0, SEEK_END);
	if((off64_t)id * (off64_t) edbd_size(parent->fd) > size) {
		log_critf("attempting to access page id that doesn't exist");
		return EDB_EEOF;
	}
#endif

	// lock in the pages
	err = lockpages(parent, id, handle);

	// later: HIID stuff should go here.
	if(!err) {
		telemetry_workr_pload(handle->name, id);
	}
	return err;
}

void    edbp_finish(edbphandle_t *handle) {
	if(handle->lockedslotv != -1) {
		unlockpage(handle->parent,
		           handle->lockedslotv);
		telemetry_workr_punload(handle->name,
								handle->parent->slots[handle->lockedslotv].id);
	}
	handle->lockedslotv = -1;
}

edb_pid edbp_gpid(const edbphandle_t *handle) {
	return handle->parent->slots[handle->lockedslotv].id;
}

void *edbp_graw(const edbphandle_t *handle) {
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
		default:
			err = EDB_EINVAL;
			break;
	}
	va_end(args);
	return err;
}