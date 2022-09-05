#ifndef _EDBP_PAGES_H_
#define _EDBP_PAGES_H_ 1

#include <stdint.h>
#include <sys/user.h>
#include <pthread.h>

#include "include/ellemdb.h"
#include "errors.h"
#include "file.h"

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

// EDBP_BODY_SIZE will always be a constant set at
// build time.
#define EDBP_SIZE PAGE_SIZE
#define EDBP_HEAD_SIZE sizeof(edbp_head)
#define EDBP_BODY_SIZE PAGE_SIZE - sizeof(edbp_head)


// Page id: simply the offset as to where to find it in
// the database.
typedef uint64_t edbp_id;

typedef struct _edbp_head {

	// Do not touch these fields outside of pages-*.c files:
	uint32_t _checksum;
	uint32_t _hiid;
	uint32_t _pradat;
	uint8_t  _pflags;

	// all of thee other fields can be modified so long the caller
	// has an exclusive lock on the page.
	uint8_t  ptype;
	uint16_t rsvd;
	uint64_t pleft;
	uint64_t pright;
	uint8_t  psecf[16]; // page spcific
} edbp_head;

// the raw content of the page. This is byte-by-byte what is
// in the database.
//
// See database specification under page header for details.
typedef struct _edbp {
	edbp_head head;
	uint8_t   body[EDBP_BODY_SIZE];
} edbp_t;

// a slot is an index within the cache to where the page is.
typedef unsigned int edbp_slotid;
typedef struct {
	edbp_t *page;
	edbp_id id; // cache's mutexpagelock must be locked to access

	// the amount of workers that have this page locked. 0 for none.
	// you must use a futex call to wait until the swap is complete.
	// must have the caches pagelock locked to access
	unsigned int locks;

	// if 1 that means its currently undergoing a swap.
	// if 0 then no swap in process
	// if 2 then swap had failed critically.
	uint32_t futex_swap;

	unsigned int pra_k[2]; // [0]=LRU-1 and [1]=LRU-2
	unsigned int pra_score; // page replacement score

	// not used yet:
	unsigned int width;
	unsigned int strait;
} edbp_slot;

// the cahce, installed in the host
typedef struct _edbpcache_t {
	edb_file_t *file;

	// slots
	edbp_slot     *slots;
	edbp_slotid    pagebufc; // the total amount of slots regardless of width
	edbp_slotid    pagebufc_w1; // width 1 slot count
	edbp_slotid    pagebufc_w4; // width 4 slot count
	edbp_slotid    pagebufc_w8; // width 8 slot count

	pthread_mutex_t eofmutext;
	pthread_mutex_t mutexpagelock;


	// mutexpagelock must be locked to access opcounter;
	unsigned long int opcoutner;

} edbpcache_t;

// the handle, installed in the worker.
typedef struct _edbphandle_t {
	edbpcache_t *parent;

	// modified via edbp_start and edbp_finish.
	unsigned int   checkedc;
	edbp_t       **checkedv; // array of pointers checkedc long.
	edbp_slotid     *checked_slotsv; // assoc. to checkedv
} edbphandle_t;

// Initialize the cache
//
// note to self: all errors returned by edbp_init needs to be documented
// in edb_host.
//
// THREADING: calling edbp_decom whilest edbphandle's are out using cache will result
// in undefined behaviour.
edb_err edbp_init(const edb_hostconfig_t conf, edb_file_t *file, edbpcache_t *o_cache);
void    edbp_decom(edbpcache_t *cache);


// create handles for the cache
edb_err edbp_newhandle(edbpcache_t *o_cache, edbphandle_t *o_handle);
void    edbp_freehandle(edbphandle_t *handle);

typedef enum {
	EDBP_XLOCK,
	EDBP_ULOCK,
	EDBP_ECRYPT,
	EDBP_CACHEHINT,
} edbp_options;

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
	// You may only use one of these in a bitmask.
	//
	// These will always be sequencial in value (ie. EDBP_HINDEX0 + 3 = EDBP_HINDEX3)
	EDBP_HINDEX0 = 0x10, // 0001 0000
	EDBP_HINDEX1 = 0x20, // 0010 0000
	EDBP_HINDEX2 = 0x30, // 0011 0000
	EDBP_HINDEX3 = 0x40, // 0100 0000

} edbp_hint;

// edbp_start and edbp_finish allow workers to access pages in a file while managing
// page caches, access, allocations, ect.
//
// id (wich is functionally the offset) and pagec are used to describe which page
// how many pages should be allocated. Passing in a pointer to (edbp_id)(-1)
// for id will result in a new pages being created and id being written to
// as the newly created id. Note that when creating pages, strait pages will
// always be created, but not all of them may be loaded at that time.
//
// straits is the amount of pages that
// will be loaded in, must be at least 1. This is the
// best way to load multiple pages in a strait at once. The actual amount of
// pages loaded may be less than straits (see edbphandle_t.checkedc)... but
// will always be at least 1.
//
// only 1 handle can have a single page opened at one time. These functions are
// methods of starting and finishing operations on the pages, these functions
// ARE NOT used for just normal locking mechanisms, only to ensure operations
// are performed to their completeness without having the page kicked out of cache.
//
// THREADING: edbp_start and edbp_finish must be called from the same thread per handle. Note
// that multiple handles can lock the same page and it's up to them to negitionate what happens
// where.
//
// ERRORS:
//   EDB_EINVAL - edbp_start straits was 0.
//   EDB_EINVAL - edbp_start id was null or *id was 0.
//   EDB_EINVAL - edbp_start was called twice without calling edbp_finish
edb_err edbp_start (edbphandle_t *handle, edbp_id *id, unsigned int straits, int mode);
void    edbp_finish(edbphandle_t *handle);

// edbp_mod applies special modifiecations to the page. This function will effect the page
// that was referenced in the most recent edbp_start and must be called before the
// edbp_finish.
//
//   EDBP_XLOCK (void)
//     Install an exclusive lock on this page, preventing all other workers from accessing
//     this page by blocking all subseqent calls to edbp_start until the the calling worker
//     sets EDBP_ULOCK mod on this page. If this page has already been locked then
//     EDB_EAGAIN is returned, to which the errornous caller should finish the page
//     and call edbp_start and wait until it stops blocking.
//
//   EDBP_ULOCK (void)
//     Remove the exclusive lock on this page.
//
//   EDBP_ECRYPT (todo)
//     Set this page to be encrypted.
//     todo: need descrive the subseqent args
//
//   EDBP_CACHEHINT (edbp_hint)
//     Set hints to this page in how the cacher should treat it.
//
// THREADING: edbp_mod must be called on the same thread and inbetween edbp_start and edbp_finish
//   per handle
edb_err edbp_mod(edbphandle_t *handle, edbp_options opts, ...);

#endif