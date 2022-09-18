#ifndef _edbtypes_h_
#define _edbtypes_h_

#include "include/ellemdb.h"
#include "pages.h"
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

typedef uint32_t edb_object_flags;
typedef void     edb_object_content;
typedef struct {
	_edbp_stdhead head;
	uint16_t structureid;
	uint16_t deletestart;
	uint16_t fixedlen;
	uint8_t _tphead[8];

	// in memeory after this is structures.
	//edb_obj_t objects;
} edbp_object_t;

// later: this is an edb structure. not sure if it should be here.
//        but its never in the sight of the handle. hmm.
typedef struct {
	pid_t ref;
	uint64_t startoff_strait;
} edb_lref_t;

typedef struct {
	_edbp_stdhead head;
	uint16_t entryid;
	uint16_t refc;
	uint8_t _tphead[12];

	// in memeory after this is lookup entries..
	//edb_lookup_t objects;
} edbp_lookup_t;

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

// returns the pointer to the start of the objects.
void *edbp_object_body(edbp_object_t *o);

#endif