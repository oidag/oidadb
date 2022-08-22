#ifndef _EDBP_PAGES_H_
#define _EDBP_PAGES_H_ 1

#include <stdint.h>
#include <sys/user.h>

#include "include/ellemdb.h"
#include "errors.h"
#include "file.h"

// EDBP_BODY_SIZE will always be a constant set at
// build time.
#define EDBP_SIZE PAGE_SIZE
#define EDBP_HEAD_SIZE sizeof(edbp_head)
#define EDBP_BODY_SIZE PAGE_SIZE - sizeof(edbp_head)

typedef uint64_t edbp_id;

/*
| Name             | Type     | Definition                                                                                |
|------------------+----------+-------------------------------------------------------------------------------------------|
| Checksum         | uint32_t | Checksum of the page (consitutes everything except itself, including the head)            |
| Host-Instance ID | uint32   | Written when first loaded into the host. Set to 0 when deloaded. See [[Host-Instance ID]]     |
| PRA Data         | uint32_t | Page replacement algorythem data, which depends on the host (reserved)                    |
| Flags            | uint8_t  | The page type as well as any flags.                                                       |
| Page Type        | uint8_t  | The page type as reflected in the chapter                                                 |
| rsvd0            | uint16_t |                                                                                           |
| Page Left        | [[Page ID]]  | The previous page to navigate too                                                         |
| Page Right       | [[Page ID]]  | The next page in the chapter to navigate too. If equal to =0= then there is no next page. |
| rsvd1            | ...      | Page-type specific header information                                                     |
 */


typedef struct _edbp_head {
	uint32_t checksum;
	uint32_t hiid;
	uint32_t pradat;
	uint8_t  pflags;
	uint8_t  ptype;
	uint16_t rsvd;
	uint64_t pleft;
	uint64_t pright;
	uint8_t  psecf[16];
} edbp_head;

// See database specification under page header for details.
typedef struct _edbp {
	edbp_head head;
	uint8_t   body[EDBP_BODY_SIZE];
} edbp;

// before you can start using pages, you must initialize the cache.
// once completely done, use edbp_freecache.
edb_err edbp_initcache(edb_pra algo);
void edbp_freecache();

// Sets *o_page to the pointer of the page requested. The pointer will be
// pointing to somewhere in the cache.
// edbp_sync will save changes to the page.
// edbp_free will NOT automatically call edbp_sync. Calling edbp_free without
// edbp_sync will discard all of your changes.
// todo: these may change when I start digging into how exactly the bookkeeping will work
//       with managing the pages between the file, memeory, and cache. make sure to
//       take memeory safety into account.
edb_err edbp_load(const edbp_id id, edbp *const*o_page);
edb_err edbp_sync(edbp *page);
void    edbp_free(edbp *page);

#endif