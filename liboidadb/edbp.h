#ifndef _EDBP_PAGES_H_
#define _EDBP_PAGES_H_ 1

#include "odb-structures.h"
#include <oidadb/oidadb.h>
#include "edbd.h"
#include "errors.h"

#include <stdint.h>
#include <sys/user.h>
#include <pthread.h>

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

typedef enum edbp_hint {

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

typedef struct edbphandle_t edbphandle_t;
typedef struct edbpcache_t edbpcache_t;

// idk how to explain this. Make it bigger to make the
// hints less effective. Must be a minimum of the biggest
// possibile hint score.
#define EDBP_HMAXLIF 0x7 // 0000 1001
#define EDBP_SLOTBOOSTPER 0.10f // 10% of page capacity

// Create a new edbp cache.
//
//
//
// THREADING: Not MT-safe.
//
// ERRORS:
//   - ODB_EINVAL: file or o_cache is null.
//   - ODB_ENOMEM: not enough free memory
//   - ODB_ECRIT
//
// UNDEFINED: calling edbp_decom whilest edbphandle's are out using cache will result
// in undefined behaviour. If you wish to clean up a cache, make sure to free
// the handlers of the cache first.
// todo: update the above documentation
odb_err edbp_cache_init(const edbd_t *file, edbpcache_t **o_cache);
void    edbp_cache_free(edbpcache_t *cache);


typedef enum edbp_config_opts {
	EDBP_CONFIG_CACHESIZE,
} edbp_config_opts;

// Configures a cache. Depending on the compile options, some configures may
// be no-ops.
//
//  - EDBP_CONFIG_CACHESIZE (unsigned int): sets the amount of pages that can
//    be held in cache at once. Not required when compiling with EDB_OPT_OSPRA.
//
// ERRORS:
//
//  - ODB_EINVAL - cache is null, opts is invalid.
//  - ODB_EOPEN - cache has handles attached
//  - ODB_ENOMEM - (EDBP_CONFIG_CACHESIZE) not enough memory needed to resize
//                 the cache to this size.
//
odb_err edbp_cache_config(edbpcache_t *cache, edbp_config_opts opts, ...);


// create handles for the cache.
//
// calling freehandle will unlock all slots.
//
// name should be the worker id (used explicitly for telemetry)
//
// ERRORS:
//  - EINVAL - o_cache / o_handle is null
//  - ODB_ENOMEM - not enough memory
//  - ODB_ENOSPACE - cannot create another handle because not enough space in
//                   the cache. (did you call edbp_cache_config w/ EDBP_CONFIG_CACHESIZE?)
//  - ODB_ECRIT
//
// THREADING: Not MT safe.
odb_err edbp_handle_init(edbpcache_t *cache,
                         unsigned int name,
                         edbphandle_t **o_handle);
void    edbp_handle_free(edbphandle_t *handle);

typedef enum {
	// note that some of these enums are used as both args in
	// edbp_mod and also insidde the edb specification itself.
	EDBP_ECRYPT = 0x10,
	EDBP_CACHEHINT = 0x1002,
} edbp_options;

// edbp_start and edbp_finish allow workers to access pages in a file while
// managing page caches, access, allocations, ect.
//
// Only 1 handle can have a single page opened at one time. These functions are
// methods of starting and finishing operations on the pages, these functions
// ARE NOT used for locking mechanisms, only to ensure operations
// are performed to their completeness without having the page kicked out of
// cache.
//
// edbp_start will load an existing page of a given id.
//
// calling edbp_finish without having started a page will do nothing.
//
// THREADING:
//   edbp_start and edbp_finish must be called from the same thread per handle.
//   Async calls to edbp_start with the same pageid are handled nicely. Note
//   that if a swap is needed for a page to be loaded, and multiple threads
//   all attempt to access that same page, they will all return at once with
//   the same result but only the first call would have actually performed
//   the swap.
//
// ERRORS:
//   ODB_EINVAL - edbp_start id was 0.
//   ODB_EINVAL - edbp_start was called twice without calling
//                edbp_finish
//   ODB_EEOF   - Supplied id does not exist.
//   ODB_ENOMEM - no memory left
//   ODB_ECRIT
//
// UNDEFINED:
//   - using an unitialized handle / uninitialized cache
odb_err edbp_start (edbphandle_t *handle, odb_pid id);
void    edbp_finish(edbphandle_t *handle);

// called between edbp_start and edbp_finish. Simply returns the
// page that was locked successfully by edbp_start.
//
// If you attempt to call this without having a page locked, null
// is returned (which you should never do).
void *edbp_graw(const edbphandle_t *handle);

// get the pid of the currently loaded page.
odb_pid edbp_gpid(const edbphandle_t *handle);

// edbp_mod applies special modifiecations to the page. This function will effect the page
// that was referenced in the most recent edbp_start and must be called before the
// edbp_finish.
//
//   EDBP_ECRYPT todo: not implemented
//     Set this page to be encrypted.
//
//   EDBP_CACHEHINT (edbp_hint)
//     Set hints to this slot in how the cacher should treat it. See EDBP_H... constants
//     For full details. The first argument is a xor'd value of what hints you want.
//     Note that once a hint has been set, it cannot be removed.
//
// ERRORS:
//   EDB_INVAL - opts not recognized
//   ODB_ENOENT - edbp_mod was not called between successful edbp_start
//                and edbp_finish.
//   ODB_EAGAIN - see EDBP_XLOCK later:
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
odb_err edbp_mod(edbphandle_t *handle, edbp_options opts, ...);


#endif