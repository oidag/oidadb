#include "ellemdb.h"
#include "edba.h"
#include "edbl.h"
#include "edba-util.h"


edb_err edba_structopenc(edba_handle_t *h, uint16_t *o_sid, edb_struct_t strct) {

	// value assumptions
	strct.flags &= (0x80 | _EDB_FUSRLALL);
	strct.flags |= 0x80;

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

	// easy ptrs
	edbl_handle_t *lockh = &h->lockh;
	edb_err err;

	// as per spec we lock the creation structure index
	edbl_struct(lockh, EDBL_EXCLUSIVE);
	uint16_t i;

	for(h->strctid = 0; edbd_struct(h->parent->descriptor, i);) {
		err = edbd_struct(h->parent->descriptor, h->strctid, &h->strct);
		if(err) {
			edba_structclose(h);
			if(err == EDB_EEOF) {
				return EDB_ENOSPACE;
			}
			return err;
		}
		if(h->strct->flags & EDB_FSTRCT_INIT) {
			// this structure is already initialized. Can't use it.
			continue;
		}

		// this structure is not initialized. We can use this one given it has
		// enough space.
		break;

		// todo: I need space management for structures. If we delete one, what do we do with that extra
		//       space? We can't move the other structures back because that would change their IDs.
		//       AHHHHHHHHHHHHHHHH
	}
}
void    edba_structclose(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != EDB_EINVAL) {
		log_critf("edba_structclose: already closed");
		return;
	}
#endif
	edbl_struct(&h->lockh, EDBL_TYPUNLOCK);
	h->opened = 0;
	h->openflags = 0;
}
void   *edba_structconf(edba_handle_t *h);
edb_err edba_structdelete(edba_handle_t *h, uint16_t sid);