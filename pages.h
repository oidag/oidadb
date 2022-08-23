#ifndef _EDBP_PAGES_H_
#define _EDBP_PAGES_H_ 1

#include <stdint.h>
#include <sys/user.h>

#include "include/ellemdb.h"
#include "host.h"
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

// the cahce, installed in the host
typedef struct _edbpcache_t {

} edbpcache_t;

// the handle, installed in the worker.
typedef struct _edbphandle_t {

} edbphandle_t;

// EDBP_BODY_SIZE will always be a constant set at
// build time.
#define EDBP_SIZE PAGE_SIZE
#define EDBP_HEAD_SIZE sizeof(edbp_head)
#define EDBP_BODY_SIZE PAGE_SIZE - sizeof(edbp_head)

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

// See database specification under page header for details.
typedef struct _edbp {
	edbp_head head;
	uint8_t   body[EDBP_BODY_SIZE];
} edbp;

// Initialize the cache
//
// note to self: all errors returned by edbp_init needs to be documented
// in edb_host
edb_err edbp_init(const edb_hostconfig_t conf, edb_file_t *file, edbpcache_t *o_cache);
void    edbp_decom(edbpcache_t *cache);


// create handles for the cache
edb_err edbp_newhandle(edbpcache_t *o_cache, edbphandle_t *o_handle);
void    edbp_freehandle(edbphandle_t *handle);

typedef enum {
	EDBP_XLOCK,
	EDBP_ULOCK,
	EDBP_ECRYPT,
} edbp_options;

// edbp_start and edbp_finish allow workers to access pages in a file while managing
// page caches, access, allocations, ect.
//
// Using a page id of (*edbp_id)(0) indicates a new page must be created. In this case,
// id will be set to the newly created id.
//
// only 1 worker can have a single page opened at one time. These functions are
// methods of starting and finishing operations on the pages, these functions
// ARE NOT used for just normal locking mechanisms, only to ensure operations
// are performed to their completeness without having the page kicked out of cache.
//
// When starting, the caller has the option to specify the lock type. Either shared
// or exclusive. Shared locks are all-around useful 90% of the time. Exclusive locks
// means that edbp_start will return only once the caller is the only caller to have
// that page checked out, and all other calls will be blocked until the caller has
// finished. Exclusive locks are only useful when the page header needs to be
// changed.
//
// THREADING: edbp_start and edbp_finish must be called from the same thread per handle
edb_err edbp_start (edbphandle_t *handle, edbp_id *id, int *pagec);
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
//     Remove the locks on this page.
//
//   EDBP_ECRYPT (todo)
//     Set this page to be encrypted.
//     todo: need descrive the subseqent args
//
// THREADING: edbp_mod must be called on the same thread and inbetween edbp_start and edbp_finish
//   per handle
edb_err edbp_mod(edbphandle_t *handle, edbp_options opts, ...);

#endif