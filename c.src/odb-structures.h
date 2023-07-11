#ifndef _edbtypes_h_
#define _edbtypes_h_

#include "include/oidadb.h"

#include <unistd.h>
#include <stdint.h>

// Always use this instead of sizeof(edbp_head) because
// edbp_head doesn't include the page-specific heading
#define ODB_SPEC_HEADSIZE 48

////////////////////////////////////////////////////////////////////////////////
//
// Child Structures.
//

typedef enum odb_sysflags {
	EDB_FDELETED = 0x1000
} odb_spec_sysflags;

// edb_object_flags is a xor'd combiniation between
// edb_usrlk and odb_sysflags
typedef uint32_t odb_spec_object_flags;

// later: this is an edb structure. not sure if it should be here.
//        but its never in the sight of the handle. hmm.
typedef struct {
	odb_pid ref;
	uint64_t startoff_strait;
} odb_spec_lookup_lref;

// see spec, 'DSM'
typedef struct odb_dsm_t {
	uint16_t subpagesize;
	uint16_t subpagecount;
	uint16_t paddingdel;
	uint16_t rsvd;
} odb_spec_dynamic_dsm;
typedef struct edb_deletedref_st {
	odb_pid ref;
	uint16_t straitc;
	uint16_t _rsvd2;
} odb_spec_deleted_ref;

typedef uint64_t eid;

// see spec.
typedef struct odb_spec_index_entry {

	// paramaters
	odb_type type;
	uint8_t rsvd;
	uint16_t memory;
	uint16_t structureid;

	// cached values
	uint16_t objectsperpage;
	uint16_t lookupsperpage;

	// references
	odb_pid ref0; // starting chapter start
	odb_pid ref1; // lookup chapter start
	odb_pid ref2; // dynamic chapter start
	odb_pid ref0c;
	odb_pid lastlookup;
	odb_pid ref2c;
	odb_pid trashlast;

} odb_spec_index_entry;

typedef struct odb_spec_struct_struct {
	//uint16_t     id;   // cant put ids in structures

	uint16_t    fixedc;    // total size (see spec)
	uint16_t    confc;     // configuration size
	uint16_t    version;   // structure version
	uint8_t     flags;     // flags see spec.
	uint8_t     data_ptrc; // data pointer count

	// implicit fields:
	// const uint8_t *subpagesizes; // = (edb_struct_t*) + sizeof(edb_struct_t)
	// const void    *confv;        // = (edb_struct_t*) + sizeof(edb_struct_t) + sizeof(uint8_t) * data_ptrc
} odb_spec_struct_struct;

// odb_spec_struct_full_t is the structure that fills all edbp_struct pages.
// It inherits its first 2 fields form it being accounted for as object pages.
// See spec.
//
// We have to mark it as __packed__ because it adds 4 bytes between obj_flags
// and dy_pointer that would not normally be there in the object pages (as
// dy_pointers is an implicit byte-offset field)
typedef struct{
	odb_spec_object_flags obj_flags;
	odb_dyptr dy_pointer;
	odb_spec_struct_struct content;
} __attribute__((__packed__)) odb_spec_struct_full_t;


////////////////////////////////////////////////////////////////////////////////
//
// Page Structures. See file spec.
//
//

#define ODB_SPEC_HEADER_MAGIC (uint8_t []){0xA6, 0xF0}

struct odb_spec_headintro {
	uint8_t magic[2];
	uint8_t intsize;
	uint8_t entrysize;
	uint16_t pagesize;
	uint16_t pagemul;
	char rsvd[24];
	char id[32];
} __attribute__((__packed__)); // we pack the intro to make it more universal.
typedef struct odb_spec_headintro odb_spec_headintro;
typedef struct odb_spec_head {
	// intro must be first in this structure.
	const odb_spec_headintro intro;
	uint64_t host;
	uint16_t indexpagec;
	uint16_t structpagec;
} odb_spec_head;
typedef struct _odb_stdhead {

	// Do not touch these fields outside of pages-*.c files:
	// these can only be modified by edbp_mod
	uint32_t _checksum;
	uint32_t _hiid;
	uint32_t _rsvd2; // used to set data regarding who has the exclusive lock.
	uint8_t  _pflags;

	// all of thee other fields can be modified so long the caller
	// has an exclusive lock on the page.
	odb_type ptype;
	uint16_t rsvd;
	uint64_t pleft;
	uint64_t pright;

	// 16 bytes left for type-specific.
	//uint8_t  psecf[16]; // page spcific. see types.h
} _odb_stdhead;

typedef struct odb_spec_index {
	_odb_stdhead head;
	uint8_t rsvd[16];
	// after this: array of odb_spec_index_entry
} odb_spec_index;
typedef struct odb_spec_deleted {
	_odb_stdhead head;
	uint16_t largeststrait; // largest strait on the page
	uint16_t refc; // non-null references.
	uint32_t pagesc; // total count of pages
	uint64_t _rsvd2;
} odb_spec_deleted;

typedef struct odb_spec_object {
	_odb_stdhead head;
	uint64_t trashvor; // careful accessing this outside of a =trashlast= lock.
	uint16_t structureid;
	uint16_t trashstart_off;
	uint16_t trashc;
	uint16_t entryid;

	// in memeory after this is structures.
	//edb_obj_t objects;
} odb_spec_object;

typedef odb_spec_object odb_spec_struct;
//typedef odb_spec_struct struct odb_spec_struct;

typedef struct odb_spec_lookup {
	_odb_stdhead head;
	uint64_t parentlookup;
	uint16_t entryid;
	uint16_t refc;
	uint8_t depth;
	uint8_t rsvd0;
	uint16_t rsvd1;

	// in memeory after this is lookup entries..
	//edb_lref_t objects;
} odb_spec_lookup;

typedef struct odb_spec_dynamic {
	_odb_stdhead head;
	uint32_t _rsvd;
	uint32_t garbagestart;
	uint8_t _tphead[8];

	// in memeory after this is (C)DSMs and their data.
	// Which are not static sizes.
	//edbp_dsm_t *
} odb_spec_dynamic;



////////////////////////////////////////////////////////////////////////////////
//
// helper functions

// initializes header according to spec.
// assumes the page is 0-initialized (so make sure it is)
//
// does NOT touch anything outside of page.
//
//		.structureid = header.structureid,
//		.entryid = header.entryid,
//		.trashvor = header.trashvor,
//		.trashc = objectsperpage,
//		.head.pleft = header.head.pleft,
//
// later: this probably belongs in edbd.
void edba_u_initobj_pages(void *page, odb_spec_object header,  uint16_t fixedc,
                          unsigned int objectsperpage);

// stored pid of the host for a given database file.
// does not validate the file itself.
//
// Errors:
//  - ODB_ENOTDB  - opened `path` but found not to be a oidadb file.
//  - ODB_ENOHOST - no host for file
//  - ODB_EERRNO  - error with open(2).
//  - ODB_ECRIT
odb_err edb_host_getpid(const char *path, pid_t *outpid);

#endif