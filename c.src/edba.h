#ifndef _EDBA_h_
#define _EDBA_h_

#include "edbl.h"
#include "edbp.h"
#include "edbd.h"

typedef enum {
	EDBA_FWRITE = 0x0001,
	EDBA_FCREATE = 0x0002,
} edbf_flags;

// edba host stuff.
//
// edba_host_t - for use in edba_handle
// edba_host_init - initialize a host
// edba_host_decom - deallocate said host
typedef struct edba_host_st {
	edbl_host_t  lockhost;
	edbpcache_t *pagecache;
	edbd_t      *descriptor;
} edba_host_t;
edb_err edba_host_init(edba_host_t *o_host, edbpcache_t *pagecache, edbd_t *descriptor);
void    edba_host_decom(edba_host_t *host);

typedef struct edba_handle_st {
	edba_host_t *parent;
	edbl_handle_t lockh;
	edbphandle_t  edbphandle;

	// internal stuff, don't touch outside of edba namespace:

	edb_entry_t *clutchedentry; // (note to self: points to persistant mem)
	edb_eid clutchedentryeid;

	uint16_t objectoff; // byte offset from the page until objectdata.
	void *objectdata; // pointer starts at object flags
	unsigned int objectc; // same as the object's struct->fixedc

	edbl_lockref lock;
	edb_type opened; // what type of operation was opened
	edbf_flags openflags;


} edba_handle_t;
edb_err edba_handle_init(edba_host_t *host, edba_handle_t *o_handle);
void    edba_handle_decom(edba_handle_t *src); // hmmm... do we need a close?

// todo: rename
//
// returns null on error (logged)


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
//   open an object that has been marked as deleted (outputs oid of what is found)
//   using EDBA_FCREATE create room for a new one if no deleted ones
//   are found. o_oid must have a valid entry id. the row id is set as output.
//   The returned object will automatically be UNDELETED status.
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
//  - EDB_ENOSPACE - (edba_objectopenc w/ EDBA_FCREATE) failed to allocate more space, disk/file ful)
//  - EDB_ENOSPACE - (edba_objectopenc w/o EDBA_FCREATE) no free space without needing creation
edb_err edba_objectopen(edba_handle_t *h, edb_oid oid, edbf_flags flags);
edb_err edba_objectopenc(edba_handle_t *h, edb_oid *o_oid, edbf_flags flags);
void    edba_objectclose(edba_handle_t *h);

// edba_objectfixed
//   Will return a pointer to the fixed data of the object.
//   You can change the contents of the object so long you opened this
//   object with EDBD_FWRITE.
//
//
// later: I need to have 'read-only variants' of these functions that return
//        const pointers so that way I can check for open flags on a low level.
void   *edba_objectfixed(edba_handle_t *h);

// edba_objectflags_get, edba_objectlocks_set
//   Getter and setter to the user locks on this object. setter will do nothing
//   if not open with right flags (and bitch in console)
//
// RETURNS (edba_objectlocks_set)
//  - EDB_EINVAL (EDB_FUCKUPS) object not open for writing
//  - EDB_EINVAL - lk is not a valid mask. You can prevent this error entirely by
//                 bitwise-and-ing lk with _EDB_FUSRLALL.
edb_usrlk edba_objectlocks_get(edba_handle_t *h);
edb_err edba_objectlocks_set(edba_handle_t *h, edb_usrlk lk);


// edba_objectdeleted
//   Returns non-0 if this object is deleted. (note if it is deleted then
//   the return number will be effectively random, just not 0).
//
// edba_objectdelete, edba_objectundelete
//   Place/remove this object in the trash. place/remove will only work if
//   EDBA_FWRITE is enabled.
//
//   If you try to delete an object twice, no error is given sense the redundant
//   operation was technically successful.
//
//   Also despite the simple structure between these two functions you must
//   remember that when objectdelete is called, the data that was in that object
//   IS actually clobbered. So "undelete" doesn't undo the delete, it just
//   makes the object open to being written too again. (in other words, once calling
//   edba_objectdelete there's no going back in terms of what data was there)
//
// IGNORES ALL USER LOCKS.
//
// RETURNS:
//  - EDB_EINVAL (EDB_FUCKUPS) if object wasn't open for writing
unsigned int edba_objectdeleted(edba_handle_t *h);
edb_err edba_objectdelete(edba_handle_t *h);
edb_err edba_objectundelete(edba_handle_t *h);

// edbf_objectstruct
//   Will return the (readonly) structure data.
//   This will point to whatever is given by edbd.
//
// edbf_objectentry
//   Will return the (readonly) entry data.
//   This will point to whatever is given by edbd.
const edb_struct_t *edba_objectstruct(edba_handle_t *h);
const edb_entry_t  *edba_objectentry(edba_handle_t *h);

// entry (ent) mods

// While an entry is opened, only the calling handle has access to anything
// within that entry. So if you're screwing with existing entries, be quick.
//
// edba_entryopenc
//   create an entry and return the eid.
//   Flags can include EDBA_FWRITE.
//   Flags must include EDBA_FCREATE.
//   Once opened you can use subsequent edba_entry... functions.
//
// edba_entryclose
//   close out of the entry once done editting it.
//
// edba_entrydelete
//   unlike the edba_object... family, entries do not need to be checked out
//   if you wish to delete them. You must do it witout anything checked out.
//   This will delete the entry and move all of its pages to garbage
//
// ERRORS:
//
//   - EDB_ECRIT: programmer error (can be ignored) (will be logged)
//   - EDB_ENOSPACE: (edba_entryopenc) no more entry slots availabe
edb_err edba_entryopenc(edba_handle_t *h, edb_eid *o_eid, edbf_flags flags);
void    edba_entryclose(edba_handle_t *h);
edb_err edba_entrydelete(edba_handle_t *h, edb_eid eid);

// Get a pointer to the entry for read-only purposes.
const edb_entry_t *edba_entrydatr(edba_handle_t *h);

// update the entry's contents to match that supplied of
// e. This may not be successful, as several validation steps
// must happen. Only certain fields in e are actionable.
// If no error is returned then the changes are successfully applied
// and will reflect in edba_entrydatr as well as be stored in the
// persistant index memory.
//
// e.type - ignored unless EDBA_FCREATE
// e.memory - ignored unless EDBA_FCREATE (will be normallized)
// e.structureid - can be changed only with EDBA_FCREATE or EDBA_FWRITE
// everything else: ignored.
//
// ERRORS:
//   - EDB_ECRIT - programmer failed to read documentation / other error
//   - EDB_EINVAL - (FUCKUPS) e.type was not EDB_TOBJ or handle doesn't have
//                  the entry open in write mode
//   - EDB_EEOF - e.structureid was too high / does not exist
//   - EDB_ENOSPACE - no more space left in file for blank pages.
//   - EDB_ENOMEM - no memory for operaiton
edb_err edba_entryset(edba_handle_t *h, edb_entry_t e);
#endif