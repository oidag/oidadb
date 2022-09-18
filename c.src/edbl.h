#ifndef _edbl_h_
#define _edbl_h_

#include "include/ellemdb.h"


typedef struct {} edbl_t;
edb_err edbl_init(edbl_t *o_lockdir);
void    edbl_decom(edbl_t *lockdir);
typedef enum {
	EDBL_SHARED,
	EDBL_EXCLUSIVE,
	EDBL_NONE
} edbl_type;
edb_err edbl_entry(edbl_t *lockdir, edb_eid eid, edbl_type type);
edb_err edbl_structure(edbl_t *lockdir, uint16_t structureid, edbl_type type);
edb_err edbl_object(edbl_t *lockdir, edb_oid oid, edbl_type type);

#endif