#include "edbd_u.h"

#include <strings.h>

// note: this is weird to include a _u file from another namespace. Infact,
// its illigal. but we do it to borrow page initializtion
#include "edba_u.h"

void edbd_u_initindexpage(void *page, unsigned int psize)  {
	// header
	odb_spec_index *p = page;
	bzero(p, sizeof(odb_spec_index));
	p->head.ptype = EDB_TENTS;
	// body
	unsigned int entsperbody = (psize - ODB_SPEC_HEADSIZE)/sizeof
			(odb_spec_index_entry);
	odb_spec_index_entry *body = page + ODB_SPEC_HEADSIZE;
	for(int i = 0; i < entsperbody; i++) {
		// initialize blank entry
		odb_spec_index_entry *e = &body[i];
		e->type = EDB_TINIT;
	}
}

void edbd_u_initstructpage(void *page, unsigned int pszie) {
	// 0 the whole page
	bzero(page, pszie);
	odb_spec_object header;
	header.structureid = 0;
	header.entryid = EDBD_EIDSTRUCT;
	header.trashvor = 0;
	header.head.pleft = 0;

	unsigned int objectsperpage = (pszie - ODB_SPEC_HEADSIZE)/sizeof
			(odb_spec_struct_struct);
	edba_u_initobj_pages(page,
						 header,
						 sizeof(odb_spec_struct_struct),
						 objectsperpage);
	odb_spec_struct *phead = (odb_spec_struct *)page;
	phead->head.ptype = EDB_TSTRCT;
}


// same as edbd_u_initindexpage but also puts in the reserved enteries
void edbd_u_initindex_rsvdentries(void *page,
                                  unsigned int psize,
                                  edb_pid indexstart,
								  edb_pid structstart,
								  unsigned int indexpagec,
                                  unsigned int structurepagec) {
	// run edbd_u_initindexpage to get the blank slate.
	edbd_u_initindexpage(page, psize);

	// now to set up the reserved entries
	odb_spec_index_entry *entries = page + ODB_SPEC_HEADSIZE;
	odb_spec_index_entry *rsvd_index, *rsvd_deleted, *rsvd_struct, *rsvd_3;

	rsvd_index  = &entries[EDBD_EIDINDEX];
	rsvd_struct = &entries[EDBD_EIDSTRUCT];
	rsvd_deleted = &entries[EDBD_EIDDELTED];
	rsvd_3 = &entries[EDBD_EIDRSVD3];

	// the index
	rsvd_index->type = EDB_TENTS;
	rsvd_index->objectsperpage = (psize-ODB_SPEC_HEADSIZE) / sizeof
			(odb_spec_index_entry);
	rsvd_index->ref0 = indexstart;
	rsvd_index->ref0c = indexpagec;

	// structure chapter
	rsvd_struct->type = EDB_TSTRCT;
	rsvd_struct->objectsperpage = (psize-ODB_SPEC_HEADSIZE) / sizeof
			(odb_spec_struct_struct);
	rsvd_struct->ref0c = structurepagec;
	rsvd_struct->ref0 = structstart;
	// todo: dynamic pages: structures need dynamic info

	// deleted
	rsvd_deleted->type = EDB_TDEL;



}