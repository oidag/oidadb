#include "odb-structures.h"

void edba_u_initobj_pages(void *page, odb_spec_object header,
                          uint16_t fixedc, unsigned int objectsperpage) {

	// set up the header
	odb_spec_object *phead = (odb_spec_object *)page;
	*phead = (odb_spec_object){
			.structureid = header.structureid,
			.entryid = header.entryid,
			.trashvor = header.trashvor,
			.trashc = objectsperpage,
			.trashstart_off = 0,

			.head.pleft = header.head.pleft,
			.head.pright = 0,
			.head.ptype = EDB_TOBJ,
			.head.rsvd = 0,
	};

	// set up the body
	void *body = page + ODB_SPEC_HEADSIZE;
	for(int i = 0; i < objectsperpage; i++) {
		void *obj = body + fixedc * i;
		odb_spec_object_flags *flags = obj;
		// mark them as all deleted. And daisy chain the trash
		// linked list.
		*flags = EDB_FDELETED;
		uint16_t *nextdeleted_rowid = obj + sizeof(odb_spec_object_flags);
		if(i + 1 == objectsperpage) {
			// last one, set to -1.
			*nextdeleted_rowid = (uint16_t)-1;
		} else {
			*nextdeleted_rowid = ((uint16_t)i)+1;
		}
		// note we don't need to touch the dynamic pointers because they should all be
		// 0 (null). And we know any byte we don't touch will be 0.
	}
}