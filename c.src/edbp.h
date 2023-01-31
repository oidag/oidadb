#ifndef _EDBP_PAGES_H_
#define _EDBP_PAGES_H_ 1

#include <stdint.h>
#include <sys/user.h>
#include <pthread.h>


#include "odb-structures.h"
#include "include/oidadb.h"
#include "edbd.h"
#include "errors.h"

/*
 * Quick overview of how pages.h works:
 *
 *  You have a page cache, this must be initialized first.
 *  Then with that cache, you can create any number of handlers.
 *  And with those handlers you can interact with the cache.
 *
 *  Thus, the cache is in the context of a host.
 *  And the handles are in the context of a worker.
 */

typedef enum {

	// This page will be used soon by the same worker in a given
	// operation. For example swaping pages will result the worker going
	// between 2 pages, and thus should mark the first one as "use soon"
	// to hint that it'll need that first page again after dealing with
	// the second page. If there's any less than an 80% probably of this
	// page being used soon, then do not use this hint.
	EDBP_HUSESOON = 0x01, // 0000 0001

	// The contents of this page were modified. This will inherently
	// make the page fault expensive, so this hint will cause the
	// cacher to hold onto this page just a /little/ longer in order
	// to minimize that chance.
	EDBP_HDIRTY  = 0x02,  // 0000 0010

	// The page's use has completely changed and any
	// previous assumptions about its caching should be completely
	// forgotten. For instnace: the pages were deleted. This will
	// more than likely result in the page being the first to be
	// swapped out in the next operation.
	EDBP_HRESET = 0x04,  // 0000 0100

	// This page is specifically used for indexing/lookups and
	// thus should be considered more important to hold into
	// cache than normal pages sense the frequency of index pages
	// being access will inherintly be higher.
	//
	// If you did not actually use this page as an index, do
	// not use these.
	//
	// With 0 being a root page and 3 being a 3-depth page. This
	// means 0 has the highest priority to be in cache, and 3 being
	// the loweset.
	//
	// They will always be these exact values, you're welcome to
	// mathmatically generate them:
	//
	//   EDBP_HINDEX3 * (4-depth)
	//
	// Only one of these may be xord at once.
	EDBP_HINDEX0 = 0x40, // 0100 0000
	EDBP_HINDEX1 = 0x30, // 0011 0000
	EDBP_HINDEX2 = 0x20, // 0010 0000
	EDBP_HINDEX3 = 0x10, // 0001 0000

} edbp_hint;

// idk how to explain this. Make it bigger to make the
// hints less effective. Must be a minimum of the biggest
// possibile hint score.
#define EDBP_HMAXLIF 0x7 // 0000 1001
#define EDBP_SLOTBOOSTPER 0.10f // 10% of page capacity

// the raw content of the page. This is byte-by-byte what is
// in the database.
//
// See database specification under page header for details.
//
// edbp_t can be casted to any edbp_XXX_t structure so long
// you know which structure it belongs too.
typedef void edbp_t;

// a slot is an index within the cache to where the page is.
typedef unsigned int edbp_slotid;
typedef struct {
	edbp_t *page;
	edb_pid id; // cache's mutexpagelock must be locked to access

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
typedef struct _edbpcache_t {
	int initialized; // 0 for not, 1 for yes.
	const edbd_t *fd;

	// slots
	edbp_slot     *slots;
	edbp_slotid    slot_count; // the total amount of slots regardless of width

	// When starting up, or when all the slots have simular scores, the cache
	// tends to stick on a single slot to perform all page swaps.
	//
	// This is bad because its not allowing the other pages that were
	// previously loaded to get a history developed.
	//
	// So every time there's a page fualt, we increment next start and modulus it
	// against slot count. This will make the iterator start not at the
	// same spot everytime.
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
typedef struct _edbphandle_t {
	edbpcache_t *parent;

	// modified via edbp_start and edbp_finish.
	// -1 means nothing is locked.
	// later: this will remain one until I get strait-pras in.
	//        but just assume this is always an array lockedslotc in size.
	edbp_slotid lockedslotv;

	unsigned int id; // unique id for each handle assigned at newhandle time.
} edbphandle_t;

#define EDBP_HANDLE_MAXSLOTS 1

// Initialize a cache into o_cache
//
// note to self: all errors returned by edbp_init needs to be documented
// in edb_host.
//
// ERRORS:
//   EDB_EINVAL - file is null
//   EDB_EINVAL - o_cache is null
//   EDB_NOMEM - not enough memory
//   EDB_ECRIT - unprecidiented critical error
//
// THREADING:
//
// UNDEFINED: calling edbp_decom whilest edbphandle's are out using cache will result
// in undefined behaviour. If you wish to clean up a cache, make sure to free
// the handlers of the cache first.
//
// todo: update the above documentation
edb_err edbp_init(edbpcache_t *o_cache, const edbd_t *file, edbp_slotid slotcount);
void    edbp_decom(edbpcache_t *cache);


// create handles for the cache.
//
// calling freehandle will unlock all slots.
//
// ERRORS:
//   EINVAL - o_cache / o_handle is null
edb_err edbp_newhandle(edbpcache_t *o_cache, edbphandle_t *o_handle);
void    edbp_freehandle(edbphandle_t *handle);

typedef enum {
	// note that some of these enums are used as both args in
	// edbp_mod and also insidde the edb specification itself.
	EDBP_XLOCK = 0x1,
	EDBP_ECRYPT = 0x10,
	EDBP_ULOCK = 0x1001,
	EDBP_CACHEHINT = 0x1002,
} edbp_options;

// edbp_start and edbp_finish allow workers to access pages in a file while managing
// page caches, access, allocations, ect., also allows for the creation of new pages.
//
// Only 1 handle can have a single page opened at one time. These functions are
// methods of starting and finishing operations on the pages, these functions
// ARE NOT used for just normal locking mechanisms, only to ensure operations
// are performed to their completeness without having the page kicked out of
// cache.
//
// edbp_start will load an existing page of a given id.
//
// calling edbp_finish without having started a page will do nothing (safe to call
// twice)
//
// THREADING:
//   edbp_start and edbp_finish must be called from the same thread per handle.
//   Async calls to edbp_start with the same pageid is handled nicely. Note that
//   if a swap is needed for a page to be loaded, and multiple threads all attempt
//   to access that same page, they will all return at once with the same result
//   but only the first call would have actually performed the swap.
//
//   There is an internal EOF-mutex that makes sure that concurrent calls to
//   edbp_startc doesn't result in weaving page straits.
//
// ERRORS:
//   EDB_EINVAL - (EDB_FUCKUPS) edbp_start id was 0.
//   EDB_EINVAL - (EDB_FUCKUPS) edbp_start was called twice without calling edbp_finish
//   EDB_EEOF   - (EDB_FUCKUPS) supplied id does not exist
//   EDB_ENOMEM - no memory left
//   EDB_ECRIT
//
// UNDEFINED:
//   - using an unitialized handle / uninitialized cache
edb_err edbp_start (edbphandle_t *handle, edb_pid id);
void    edbp_finish(edbphandle_t *handle);

// edbd_add will initialize blank object pages. Thread safe per handle.
//
//  - pagec: the amount of pages you want in strait. Must be at least 1.
//  - o_startpid: output pointer that will be the pid of the FIRST page in the striat.
//
// You cannot use any edbd_add function will a page is started.
//
// Does not touch any lookup pages or any references, you'll have to deal with that.
//
// It does set the page's trashvor's so that one points to the next for ease
// of use with auto ids. So if you create pages A, B, and C. the trashvors
// will link A -> B -> C. Thus you'll just have to set the entry's trashlast
// to A. Note that C's trashvor will be 0.
// (note to self: if I ever need to create pages whilest the entry has
// a non-0 trashlast, I should add another arguemnt that will set C's trashvor
// to the current trashlast)
//
//
// ERRORS:
//   EDB_ENOSPACE - no disk space of file would simply be too big (logged)
//   EDB_ECRIT
edb_err edbp_createobj(edbphandle_t *handle, uint16_t pagec, edb_pid *o_startpid);

// called between edbp_start and edbp_finish. Simply returns the
// page that was locked successfully by edbp_start.
//
// If you attempt to call this without having a page locked, null
// is returned (which you should never do).
edbp_t *edbp_graw(const edbphandle_t *handle);

// get the pid of the currently loaded page.
edb_pid edbp_gpid(const edbphandle_t *handle);

// edbp_mod applies special modifiecations to the page. This function will effect the page
// that was referenced in the most recent edbp_start and must be called before the
// edbp_finish.
//
//   EDBP_XLOCK (void) later: not implemented: no reason to have this at this level.
//     Install an exclusive lock on this page(s) that last between locks and unlocks,
//     preventing all other workers from accessing this page by blocking all
//     subseqent calls to edbp_start until the the calling worker
//     sets EDBP_ULOCK mod on this page. If this page has already been locked then
//     EDB_EAGAIN is returned, to which the errornous caller should finish the page
//     and call edbp_start and wait until it stops blocking.
//
//     edbp_mod will not return until all other locks on the pages are unlocked such
//     that when edbp_mod returns non-error it ensures the caller is indeed the only
//     caller that can access this page until its unlocked
//
//   EDBP_ULOCK (void) later: no xlock.
//     Remove the exclusive lock on this page.
//
//   EDBP_ECRYPT todo: not implemented
//     Set this page to be encrypted.
//     todo: need descrive the subseqent args
//
//   EDBP_CACHEHINT (edbp_hint)
//     Set hints to this slot in how the cacher should treat it. See EDBP_H... constants
//     For full details. The first argument is a xor'd value of what hints you want.
//     Note that once a hint has been set, it cannot be removed.
//
// ERRORS:
//   EDB_INVAL - opts not recognized
//   EDB_ENOENT - edbp_mod was not called between successful edbp_start
//                and edbp_finish.
//   EDB_EAGAIN - see EDBP_XLOCK later:
//
// THREADING:
//   edbp_mod must be called on the same thread and inbetween edbp_start and edbp_finish
//   per-handle.
//
//   If multiple handles are calling this function with the same page(s) locked
//   than that can cause some issues.
//
// UNDEFINED:
//   - calling with an unitialized handle/cache
edb_err edbp_mod(edbphandle_t *handle, edbp_options opts, ...);




// all edbp_g... functions type-cast the started page into the
// relevant structure.
//
// UNDEFINED:
//    - edbp_start was not called properly.
//    - the page that was selected is not the type its being
//      casted too.
odb_spec_lookup  *edbp_glookup(edbphandle_t *handle);
odb_spec_object  *edbp_gobject(edbphandle_t *handle);

// returns the pointer to the start of the ref list.
odb_spec_lookup_lref *edbp_lookup_refs(odb_spec_lookup *l);

// returns the amount of bytes into the object page until the start of the given row.
inline unsigned int edbp_object_intraoffset(uint64_t rowid, uint64_t pageoffset, uint16_t objectsperpage, uint16_t fixedlen)
{
	unsigned int ret = ODB_SPEC_HEADSIZE + (unsigned int)(rowid - pageoffset * (uint64_t)objectsperpage) * (unsigned int)fixedlen;
#ifdef EDB_FUCKUPS
	if(ret > (ODB_SPEC_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen)) {
		log_critf("intraoffset calculation corruption: calculated byte offset (%d) exceeds that of theoretical maximum (%d)",
		          ret, ODB_SPEC_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen);
	}
#endif
	return ret;

}
//inline void *edbp_body(edbp_t *page) {return page + EDBD_HEADSIZE;}


#endif