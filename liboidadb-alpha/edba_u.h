// Everything in is for the exclusive use of all "edba*.c" files.
// edba-util.h should not be included by any file outside of that.

#ifndef _EDBA_UTIL_
#define _EDBA_UTIL_ 1

#include "edba.h"
#include <oidadb-internal/odbfile.h>


// returns ODB_EEOF if eid is out of bounds
//
// updates: handle->clutchentry
//          handle->clutchentryeid
//
odb_err edba_u_clutchentry(edba_handle_t *handle, odb_eid eid, int xl);
void edba_u_clutchentry_release(edba_handle_t *host);

// must be called AFTER edba_u_clutchentry
// must be called AFTER edba_u_entrytrashlk (XL)
//
// Will add pages to the clutched chapter and update
// trashlast.
//
// returns ODB_ENOSPACE if theres no more lookup
// references left and/or was too large for the hardware.
//
// will update handle->clutchedentry->trashlast (assumed to be 0)
//
// This will also update lastlookup (the deep right lookup) if it finds
// the current deepright is full.
//
// will add lookup pages if needed. will always add object pages if successful
// todo: make sure entry creation makes at least a whole depth so deep right is
//       always at max depth.
// a return of 0 means that trashlast has been updated successfully.
//
// RETURNS:
//   - ODB_ENOMEM - no memory to perform operation.
//   - ODB_ENOSPACE - No more lookup refs are available to use to point to more object pages.
//   - ODB_ENOSPACE - no more space left in file / cannot expand (will output in crit)
//   - Everything else - either criticals or fuckups
odb_err edba_u_lookupdeepright(edba_handle_t *handle);

// loads and unloads pages into the handle's page assignment.
// between pageload and pagedeload you can use edbp_ functions
// between edbp_start and edbp_finish with the pagecache handle
// in handle.
//
// updates: handle->lock, handle->object...
//
// flags only check for EDBA_FWRITE, if true than makes the
// row lock exclusvive
odb_err edba_u_pageload_row(edba_handle_t *handle, odb_pid pid,
                            uint16_t page_byteoff, const odb_spec_struct_struct *structdat,
                            edbf_flags flags);
void edba_u_pagedeload(edba_handle_t *handle);


// blank page creators
//
// called agnostically, no prior calls needed. but... CANNOT be called
// in the middle of edbp_start / edbp_end as these functions load the
// created page and initialize it.
//
// edba_u_pagecreate_lookup - single lookup node
//   required fields (none are verified, everything else will be initialized):
//     - header.depth
//     - header.entryid
//     - header.parentlookup (can be 0 if root)
//     - header.head.pleft
//     - header.head.pright
//   ref will be the first reference written into the lookup before the function returns. (if ref is 0, then refc is not incremented)
//
// edba_u_pagecreate_objects - object page straits.
//   required header fields:
//     - header.structureid
//     - header.entryid
//     - header.trashvor
//       note this function will also automatically set the trashvor of each page in
//       the strait to the page behind it, except for the first page of the strait
//       which takes on header.trashvor. Make sure to update the entry's trashlast
//       with o_pid+straitc if you're creating these pages for just extra room.
//     - header.head.rsvdL (aka page offset)
//       subsequent pages in the strait have rsvdL incrementally.
//
//   And some notes:
//
//     - Note this will also set up header.head.pright for all the created pages, the last
//       page will thus have a pright of 0. Make sure, if you're adding more pages to
//       an existing chapter, you update the last page's pright to the o_pid.
//
// RETURNS:
//   - ODB_ENOSPACE - no more space left in file / cannot expand
//   - ODB_ENOMEM - no memeory left
odb_err edba_u_pagecreate_lookup(edba_handle_t *handle,
                                 odb_spec_lookup header,
                                 odb_pid *o_pid,
                                 odb_spec_lookup_lref ref);
odb_err edba_u_pagecreate_objects(edba_handle_t *handle,
                                  odb_spec_object header,
                                  const odb_spec_struct_struct *strct,
                                  uint8_t straitc, odb_pid *o_pid);


// converts a intra-chapter row offset to a intra-chapter page offset
// as well as its intra-page byte offset.
void edba_u_rid2chptrpageoff(edba_handle_t *handle, odb_spec_index_entry *entry, odb_rid rid,
                             odb_pid *o_chapter_pageoff, uint16_t *o_page_byteoff);

// executes a b-tree lookup and converts the intra-chapter page
// ofsset to a pid.
//
// ERRORS:
//   ODB_EEOF - chapter_pageoff was out of bounds.
odb_err  edba_u_lookupoid(edba_handle_t *handle, odb_spec_index_entry *entry,
                          odb_pid chapter_pageoff, odb_pid *o_pid);

static void inline edba_u_oidextract(odb_oid oid, odb_eid *o_eid, odb_rid
*o_rid) {
	*o_eid = odb_oid_get_eid(oid);
	*o_rid = oid & 0x0000FFFFFFFFFFFF;
}


// This function converts a odb in-file-memory structure (odb_spec_struct_struct) into a more friendly
// odb_structstat.
struct odb_structstat edba_u_stk2stat(odb_spec_struct_struct stkr, odb_sid sid);


// mark the data at the pointer as deleted.
// todo: locks?
// ERRORS:
//   - ODB_EINVAL: dynamicptr is 0
//   - Everything else: ODB_ECRIT (ie: invalid pointer)
static odb_err edba_u_dynamicdelete(edba_handle_t *handle,
                                    uint64_t dynamicptr){ return 0; } // todo

#endif