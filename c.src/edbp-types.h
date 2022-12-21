#ifndef _edbtypes_h_
#define _edbtypes_h_

#include "include/ellemdb.h"
#include "edbp.h"
#include "stdint.h"

typedef struct {
	_edbp_stdhead head;
	uint8_t _tphead[16];

	// in memory after this structure is entries.
	//edb_entry_t entryv;
} edbp_index_t;


typedef struct {
	_edbp_stdhead head;
	uint8_t _tphead[16];

	// in memeory after this is structures.
	//edb_struct_t structv;
} edbp_struct_t;

typedef enum {
	EDB_FDELETED = 0x1000
} edb_sysflags;

// edb_object_flags is a xor'd combiniation between
// edb_usrlk and edb_sysflags
typedef uint32_t edb_object_flags;

typedef struct {
	_edbp_stdhead head;
	uint16_t structureid;
	uint16_t trashstart_off;
	uint64_t trashvor; // careful accessing this outside of a =trashlast= lock.
	uint16_t trashc;
	uint16_t entryid;

	// in memeory after this is structures.
	//edb_obj_t objects;
} edbp_object_t;

typedef struct {
	_edbp_stdhead head;
	uint16_t entryid;
	uint16_t refc;
	uint64_t parentlookup;
	uint8_t depth;
	uint8_t rsvd0;
	uint8_t rsvd1;

	// in memeory after this is lookup entries..
	//edb_lref_t objects;
} edbp_lookup_t;

// later: this is an edb structure. not sure if it should be here.
//        but its never in the sight of the handle. hmm.
typedef struct {
	edb_pid ref;
	uint64_t startoff_strait;
} edb_lref_t;

// see spec, 'DSM'
typedef struct {
	uint16_t subpagesize;
	uint16_t subpagecount;
	uint16_t paddingdel;
	uint16_t rsvd;
} edbp_dsm_t;

typedef struct {
	_edbp_stdhead head;
	uint32_t _rsvd;
	uint32_t garbagestart;
	uint8_t _tphead[8];

	// in memeory after this is (C)DSMs and their data.
	// Which are not static sizes.
	//edbp_dsm_t *
} edbp_dynamic_t;

// all edbp_g... functions type-cast the started page into the
// relevant structure.
//
// UNDEFINED:
//    - edbp_start was not called properly.
//    - the page that was selected is not the type its being
//      casted too.
edbp_struct_t  *edbp_gstruct(edbphandle_t *handle);
edbp_index_t   *edbp_gindex(edbphandle_t *handle);
edbp_lookup_t  *edbp_glookup(edbphandle_t *handle);
edbp_dynamic_t *edbp_gdynamic(edbphandle_t *handle);
edbp_object_t  *edbp_gobject(edbphandle_t *handle);

// returns the pointer to the start of the ref list.
edb_lref_t *edbp_lookup_refs(edbp_lookup_t *l);

// returns the amount of bytes into the object page until the start of the given row.
inline unsigned int edbp_object_intraoffset(uint64_t rowid, uint64_t pageoffset, uint16_t objectsperpage, uint16_t fixedlen)
{
	unsigned int ret = EDBP_HEADSIZE + (unsigned int)(rowid - pageoffset * (uint64_t)objectsperpage) * (unsigned int)fixedlen;
#ifdef EDB_FUCKUPS
	if(ret > (EDBP_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen)) {
		log_critf("intraoffset calculation corruption: calculated byte offset (%d) exceeds that of theoretical maximum (%d)",
				  ret, EDBP_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen);
	}
#endif
	return ret;

}
//inline void *edbp_body(edbp_t *page) {return page + EDBP_HEADSIZE;}

#endif