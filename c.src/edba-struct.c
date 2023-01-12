#include "ellemdb.h"
#include "edba.h"
#include "edbl.h"
#include "edba-util.h"


// todo: make sure to initialize structure pages using edba_u_pagecreate_objects
edb_err edba_structopenc(edba_handle_t *h, uint16_t *o_sid, edb_struct_t strct) {

	// easy ptrs
	edbl_handle_t *lockh = &h->lockh;
	const edb_eid eid = EDBD_EIDSTRUCT;
	edbd_t *file = h->parent->descriptor;
	edb_err err;

	// value assumptions
	strct.flags = 0;
	strct.

	// validation
	if(strct.fixedc < 4) {
		return EDB_EINVAL;
	}

	// politics
	if(h->opened != 0) {
		log_critf("cannot open structure, something already opened");
		return EDB_EINVAL;
	}
	h->opened = EDB_TSTRCT;
	h->openflags = EDBA_FCREATE | EDBA_FWRITE;

	// as per spec we lock the creation structure index.
	// todo: make sure this in unclutched
	edba_u_clutchentry(h, eid, 1);
	uint16_t i;

	// but after that lock that's all we need to do. No need to lock anything else.

	// make sure we're not full.
	if(!h->clutchedentry->trashlast) {
		log_warnf("out of room in structure pages");
		edba_u_clutchentry_release(h);
		return EDB_ENOSPACE;
	}

	// sense we know structure pages are all loaded and are all in a striat,
	// we just need to get the offset of the trashpage.
	unsigned int trashpage_offset;
	void *structpages; // todo: where are we storing the structurepages?
	seektrashlast:
	trashpage_offset = h->clutchedentry->trashlast - h->clutchedentry->ref0;

	// as per spec we can parse this page as a edbp_object.
	edbp_object_t  *o = structpages + trashpage_offset * edbd_size(file);
	if(o->trashstart_off == (uint16_t)-1) {
		// trashfault. Not much to do here compared to edba_objectopenc other
		// than just update the trashlast.
		h->clutchedentry->trashlast = o->trashvor;
		o->trashvor = 0;
		goto seektrashlast;
	}

	// and just as easy as that, we have all the info to get the writable structure data.
	// much more simple than dealing with normal edbp_object pages.
	h->strctid = *o_sid = trashpage_offset * h->clutchedentry->objectsperpage + o->trashstart_off;
	h->strct = (edb_struct_full_t *)(o + EDBD_HEADSIZE + sizeof(edb_struct_full_t) * o->trashstart_off);

	// todo: allocate space for arbitrary configuration when edbp_dynamics are
	//       implemented. See h->strct.confc

	// update trash management on this page as we do in any edbp_object page.
	o->trashstart_off = *(uint16_t *)(h->strct + sizeof(edb_object_flags));

#ifdef EDB_FUCKUPS
	{
		// some redundant logic to make sure that structdat and
		// edbd_struct are aligned.
		edb_struct_t *test;
		edbd_struct(file, *o_sid, &test);
		if(test != &h->strct->content) {
			log_critf("edbd_struct and structure search logic misaligned.");
			edba_u_clutchentry_release(h);
			return EDB_ECRIT;
		}

		// make sure this structure data isn't deleted
		if(!(h->strct->obj_flags & EDB_FDELETED)) {
			log_critf("deleted structure logic returned a non-deleted structure");
			edba_u_clutchentry_release(h);
			return EDB_ECRIT;
		}
	}
#endif

	// set deleted flag as 0.
	h->strct->obj_flags = h->strct->obj_flags & ~EDB_FDELETED;


	return 0;
}
void    edba_structclose(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != EDB_EINVAL) {
		log_critf("edba_structclose: already closed");
		return;
	}
#endif
	edba_u_clutchentry_release(h);
	h->opened = 0;
	h->openflags = 0;
}
void   *edba_structconf(edba_handle_t *h) {
	// Need to first implement edbp_dynamic pages before I can deal with this
	implementme();

	//h->strct->dy_pointer...
}
edb_err edba_structdelete(edba_handle_t *h, uint16_t sid);