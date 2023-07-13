#include "edba.h"
#include "edba_u.h"
#include "include/oidadb.h"

odb_err edba_pageopen(edba_handle_t *h, odb_eid eid, odb_pid offset,
                      edbf_flags flags) {
	odb_pid pid;
	odb_err err;

	// handle-status politics.
	if(h->opened != 0) {
		log_critf("cannot open page, something already opened");
		return ODB_ECRIT;
	}
	h->opened = ODB_ELMOBJ;
	h->openflags = flags;

	// cluth lock the entry
	err = edba_u_clutchentry(h, eid, 0);
	if(err) {
		return err;
	}

	// get the struct data
	const odb_spec_struct_struct *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);

	// do the oid lookup to get the page.
	odb_pid foundpid;
	err = edba_u_lookupoid(h, h->clutchedentry, offset, &foundpid);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	// load the page and put the row data in the handle
	err = edba_u_pageload_row(h, foundpid,
	                          page_byteoff, structdat, flags);
	hmmmmmm
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	return 0;

}

odb_err edba_pageadvance(edba_handle_t *h);
odb_err edba_pageclose(edba_handle_t *h);

unsigned int edba_pageobjectv_count(edba_handle_t *h);
unsigned int edba_pageobjectc(edba_handle_t *h);
const void *edba_pageobjectv_get(edba_handle_t *h);
void       *edba_pageobjectv(edba_handle_t *h);
const odb_spec_struct_struct *edba_pagestruct(edba_handle_t *h);
odb_sid edba_pagestructid(edba_handle_t *h);