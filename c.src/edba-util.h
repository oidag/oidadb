// Everything in is for the exclusive use of all "edba*.c" files.
// edba-util.h should not be included by any file outside of that.

#ifndef _EDBA_UTIL_
#define _EDBA_UTIL_ 1

#include "edba.h"


// returns EDB_EEOF if eid is out of bounds
//
// updates: handle->clutchentry
//          handle->clutchentryeid
//
edb_err edba_u_clutchentry(edba_handle_t *handle, edb_eid eid);
void edba_u_clutchentry_release(edba_handle_t *host);

// loads and unloads pages into the handle's page assignment
// between pageload and pagedeload you can use edbp_ functions
// between edbp_start and edbp_finish with the pagecache handle
// in handle.
//
// updates: handle->lock, handle->objectdata, handle->objectc
//
// flags only check for EDBA_FWRITE, if true than makes the
// row lock exclusvive
edb_err edba_u_pageload_row(edba_handle_t *handle, edb_pid pid,
					 uint16_t page_byteoff, uint16_t fixedc,
					     edbf_flags flags);
void edba_u_pagedeload(edba_handle_t *handle);

// converts a intra-chapter row offset to a intra-chapter page offset
// as well as its intra-page byte offset.
void edba_u_rid2chptrpageoff(edba_handle_t *handle, edb_entry_t *entry, edb_rid rid,
							 edb_pid *o_chapter_pageoff, uint16_t *o_page_byteoff);
inline uint64_t edba_u_calcfileoffset(edba_handle_t *handle, edb_pid pid, uint16_t page_byteoff) {
	return edbp_pid2off(handle->parent->pagecache, pid) + page_byteoff;
}

// executes a b-tree lookup and converts the intra-chapter page
// ofsset to a pid.
//
// ERRORS:
//   EDB_EEOF - chapter_pageoff was out of bounds.
edb_err edba_u_lookupoid(edba_handle_t *handle, edb_pid lookuproot,
					  edb_pid chapter_pageoff, edb_pid *o_pid);

void inline edba_u_oidextract(edb_oid oid, edb_eid *o_eid, edb_rid *o_rid) {
	*o_eid = oid >> 0x30;
	*o_rid = oid & 0x0000FFFFFFFFFFFF;
}


#endif