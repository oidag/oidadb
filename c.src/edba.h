#ifndef _EDBA_h_
#define _EDBA_h_

#include "edbl.h"
#include "edbp.h"
#include "edbd.h"

typedef struct edbf_st edbf_t;

typedef enum {
	EDBA_FWRITE = 0x0001,
	EDBA_FCREATE = 0x0002,
} edbf_flags;

// need to fidn a place to put this one...
#define EDB_FDELETED 0x1000

// todo: rename
//
// returns null on error (logged)
edbf_t *edba_somethingsomething();
void edba_somethingclose(edbf_t *src);

// note to self: heres the flow
//
// edba_objectopen*
//  |
//  |-> edba_object(fixed|delete|undelete|locs|ect..)
//  |-> ...
//  V
// edba_objectclose*


// objects.
//
// All objects must be open then closed quickly. Between them being open
// and closed, you can perform the given actions.
//
// Make sure you close for fuck sake. Opening causes pages to be started,
// locks to be installed, ect. The longer you have it open the more you put
// other things in waiting.
//
// edba_objectopen
//   open an existing oid (regardless of delete status). Using EDBA_FWRITE
//   will have you the ability to modify its contents.
//
// edba_objectopenc
//   open an object that has been marked as deleted.
//   using EDBA_FCREATE create room for a new one if no deleted ones
//   are found.
//
// edba_objectclose
//   Close the object without any special action. If EDBA_FWRITE was true
//   then the page's are closed with dirty bit modifiers. This function
//   is safe to be called redundantly but this will be logged.
//
// IGNORES ALL USER LOCKS.
//
// RETURNS:
//  - EDB_EINVAL - oid's entry id was invalid (below min/above max)
//  - EDB_NOENT - oid's rowid was too high
//  - EDB_ENOSPACE - (edba_objectopenc) failed to allocate more space, disk/file ful)
edb_err edba_objectopen(edbf_t *h, edb_oid oid, edbf_flags flags);
edb_err edba_objectopenc(edbf_t *h, edb_oid *o_oid, edbf_flags flags);
void    edba_objectclose(edbf_t *h);


// edba_objectfixed
//   Will return a pointer to the fixed data of the object.
//   You can change the contents of the object so long you opened this
//   object with EDBD_FWRITE.
//
// edba_objectflags
//   Will return a pointer to object the object header to modify
//   its flags. You can modify so long that EDBD_FWRITE is true.
//   (note to self: do not use EDB_FDELETED here)
void   *edba_objectfixed(edbf_t *h);
edb_usrlk *edba_objectlocks(edbf_t *h);

// edba_objectdelete, edba_objectundelete, edba_objectdeleted
//   Place/remove/test this object in the trash. This will only work if
//   EDBA_FWRITE is enabled.
int     edba_objectdeleted(edbf_t *h);
edb_err edba_objectdelete(edbf_t *h);
edb_err edba_objectundelete(edbf_t *h);


// edbf_objectstruct
//   Will return the (readonly) structure data.
//   This will point to whatever is given by edbd.
//
// edbf_objectentry
//   Will return the (readonly) structure data.
//   This will point to whatever is given by edbd.
const edb_struct_t *edba_objectstruct(edbf_t *h);
const edb_entry_t *edba_objectentry(edbf_t *h);


#endif