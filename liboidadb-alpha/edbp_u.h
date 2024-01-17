#ifndef EDBP_U_H_
#define EDBP_U_H_

#include "options.h"
#include "telemetry.h"
#include "edbd.h"
#include "edbp.h"

// a slot is an index within the cache to where the page is.
typedef unsigned int edbp_slotid;
typedef struct {
	void *page;
	odb_pid id; // cache's mutexpagelock must be locked to access

	// the amount of workers that have this page locked. 0 for none.
	// you must use a futex call to wait until the swap is complete.
	// must have the caches pagelock locked to access
	unsigned int locks;

	// if 1 that means its currently undergoing a swap.
	// if 0 then no swap in process / ready to be locked
	// if 2 then swap had failed critically.
	// if 3 then swap had failed due to ENOMEM
	uint32_t futex_swap;

	// the pra_k is the LRU-K algo that will be set to 1 or 2 numbers to which
	// symbolize the opcouter to when they were locked. See LRU-K for more
	// information. pra_k is only modified inside lockpages.
	//
	// pra_hints are a bitmask to modify the behaviour of what happens to
	// the LRU-K algorythem. This must be set before unlockpages.
	//
	// pra_score is calculated inside of unlockpages based off of
	// pra_hints and pra_k and what ultimiately determains the
	// swapability of a particular slot.
	unsigned int pra_k[2]; // [0]=LRU-1 and [1]=LRU-2
	edbp_hint pra_hints;
	unsigned int pra_score;
} edbp_slot;

// the cahce, installed in the host
typedef struct edbpcache_t {
	int initialized; // 0 for not, 1 for yes.
	const edbd_t *fd;

	// slots
	edbp_slot     *slots;
	edbp_slotid    slot_count; // the total amount of slots regardless of width

	// used explicitly for returning ODB_EINVAL in edbp_newhandle when this
	// exceeds slot_count.
	unsigned int handles;

	// Every time there's a page fault, we increment next start and modulus it
	// against slot count. This will make the iterator start not at the
	// same spot everytime. Adding a bit of randomness to the mix, which is
	// helpful, the more spread out the workers are in the slots the more
	// evenly slots will be used.
	edbp_slotid    slot_nextstart;

	// slotboostCc is a number from 0 to 1 that is multipled by
	// slot_count and the result is stored in slotboost. This is done
	// once during cache startup.
	//
	// slotboostCc is what is used to assign the power of the edbp_hints.
	// whereas 1 is halfing a slot's chances of being swapped out whereas
	// 0 is no impact at all with all positive hints.
	//
	// The exact details of this are murky. Honestly you should only play with
	// these numbers for expermiental reaons.
	float slotboostCc; //(assigned to constant on startup)
	unsigned int   slotboost;

	pthread_mutex_t mutexpagelock;


	// mutexpagelock must be locked to access opcounter;
	unsigned long int opcoutner;

} edbpcache_t;

// the handle, installed in the worker.
typedef struct edbphandle_t {
	edbpcache_t *parent;

	// modified via edbp_start and edbp_finish.
	// -1 means nothing is locked.
	// later: this will remain one until I get strait-pras in.
	//        but just assume this is always an array lockedslotc in size.
	edbp_slotid lockedslotv;

	unsigned int name;
} edbphandle_t;




#endif