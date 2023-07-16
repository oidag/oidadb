#ifndef _EDBA_h_
#define _EDBA_h_

#include "edbl.h"
#include "edbp.h"
#include "edbd.h"
#include "odb-structures.h"


// todo:
//  - Split this header file up into sub-namespaces
//  - normalize the format of all sub-namespaces (make sure all SWAFUR
//    methods using the same convensions)

typedef enum {
	EDBA_FREAD = 0, // for future refactorign. Just use it and pretend its not 0
	EDBA_FWRITE = 0x0001,
	EDBA_FCREATE = 0x0002,
} edbf_flags;

// edba host stuff.
//
// edba_host_t - for use in edba_handle
// edba_host_init - initialize a host
// edba_host_free - deallocate said host
typedef struct edba_host_st {
	edbl_host_t *lockhost;
	edbpcache_t *pagecache;
	edbd_t      *descriptor;
} edba_host_t;
odb_err edba_host_init(edba_host_t **o_host,
                       edbpcache_t *pagecache,
                       edbd_t *descriptor);
void    edba_host_free(edba_host_t *host);

typedef struct edba_handle_st {
	edba_host_t *parent;
	edbl_handle_t *lockh;
	edbphandle_t  *edbphandle;

	// internal stuff, don't touch outside of edba namespace:

	// Will be the entry that has a clutch locked established.
	// (note to self: points to persistant mem)
	odb_spec_index_entry *clutchedentry;
	odb_eid clutchedentryeid;

	uint16_t          objectrowoff; // intra-page row offset.
	unsigned int      objectc; // same as the object's struct->fixedc
	odb_spec_object_flags *objectflags; // pointer to the very start of the object
	unsigned int      dy_pointersc;
	odb_dyptr        *dy_pointers; // dynamic pointers
	unsigned int      contentc;
	union {
		void *content;  // ODB_ELMOBJ - start of the fixedc content
		void *pagebody; // ODB_ELMOBJPAGE - The pointer to the body of the page (past the header)
	};

	odb_spec_object *pagehead; // ODB_ELMOBJPAGE - pointer to page head.

	// Variables when opened == ODB_ELMSTRCT
	//
	// strct - points to persistent mem.
	odb_spec_struct_full_t *strct;
	union {
		odb_sid strctid;
		odb_pid pagepid; // ODB_ELMOBJPAGE
	};

	edbl_lock lock;

	// make sure this is only assigned after successful assigment of elemeht
	odb_type opened; // what type of operation was opened. Set to 0 if nothing is open.
	edbf_flags openflags;


} edba_handle_t;
odb_err edba_handle_init(edba_host_t *host
						 , unsigned int name
						 , edba_handle_t **o_handle);
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
//   open an object that has been marked as deleted (outputs oid of what is found).
//   Use EDBA_FCREATE to create room for a new one if no deleted ones
//   are found. *o_oid must have a valid entry id.* the row id is set as output.
//   The returned object will automatically be UNDELETED status.
//   todo: why not consolidate this into edba_objectopen using flags?
//
// edba_objectclose
//   Close the object without any special action. If EDBA_FWRITE was true
//   then the page's are closed with dirty bit modifiers. This function
//   is safe to be called redundantly but this will be logged.
//
// IGNORES ALL USER LOCKS.
//
// RETURNS:
//  - EDB_NOENT - oid's rowid was too high
//  - ODB_EEOF - oid's entryid was too high (did you set oid's entryid?)
//  - ODB_ENOSPACE - (edba_objectopenc w/ EDBA_FCREATE) failed to allocate more space, disk/file ful)
//  - ODB_ENOSPACE - (edba_objectopenc w/o EDBA_FCREATE) no free space without needing creation
odb_err edba_objectopen(edba_handle_t *h, odb_oid oid, edbf_flags flags);
odb_err edba_objectopenc(edba_handle_t *h, odb_oid *o_oid, edbf_flags flags);
void    edba_objectclose(edba_handle_t *h);

// edba_objectfixed
//   Will return a pointer to the fixed data of the object.
//   You can change the contents of the object so long you opened this
//   object with EDBD_FWRITE.
void   *edba_objectfixed(edba_handle_t *h);
const void *edba_objectfixed_get(edba_handle_t *h);

// edba_objectflags_get, edba_objectlocks_set
//   Getter and setter to the user locks on this object. setter will do nothing
//   if not open with right objectflags (and bitch in console)
//
// RETURNS (edba_objectlocks_set)
//  - ODB_EINVAL (EDB_FUCKUPS) object not open for writing
//  - ODB_EINVAL - lk is not a valid mask. You can prevent this error entirely by
//                 bitwise-and-ing lk with _EDB_FUSRLALL.
odb_usrlk edba_objectlocks_get(edba_handle_t *h);
odb_err edba_objectlocks_set(edba_handle_t *h, odb_usrlk lk);


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
//   edba_objectdelete there's no going back in terms of what data was there).
//
//   dyanmic data is also deleted.
//
// IGNORES ALL USER LOCKS.
//
// RETURNS:
//  - ODB_EINVAL (EDB_FUCKUPS) if object wasn't open for writing
unsigned int edba_objectdeleted(edba_handle_t *h);
odb_err edba_objectdelete(edba_handle_t *h);
odb_err edba_objectundelete(edba_handle_t *h);

// edba_objectstruct
//   Will return the (readonly) structure data.
//   This will point to whatever is given by edbd.
//
// edba_objectentry
//   Will return the (readonly) entry data.
//   This will point to whatever is given by edbd.
//
// edba_objectstructid - returns the structure id of the object.
const odb_spec_struct_struct *edba_objectstruct(edba_handle_t *h);
const odb_spec_index_entry  *edba_objectentry(edba_handle_t *h);
odb_sid edba_objectstructid(edba_handle_t *h);

// The actuator will prepare arrays of these commonly used assets and make sure
// it delivers these arrays to you atomically (to prevent tairing).
//
// These function's methods of resource management operate in the same: both
// take in pointers to counts and arrays. The count must not be null. The
// array may be null. If the array is not null, then the value pointed to by
// the count will specify the array's capacity and that array is filled with
// the relevant data, the count's value upon return will be the amount of
// elements that were written in the array. If the array is null, then the
// count will be written to as the total amount of data that is available
// (used for allocating the array, so you may call this twice). These
// functions are expected to be used as follows:
//
//    edba_entity_get(h, &count, 0);
//    array = malloc(sizeof(...) * count);
//    edba_entity_get(h, &count, array);
//    // array has count indexes
//    ...
//    free(array);
//
// edba_entity_get will only return object entities (ODB_ELMOBJ)
// Note: both of these functions, the output array must be free'd
//
// Cannot be called with anything clutched. Must be independently called.
//
// ERRORS:
//   - ODB_ECRIT
odb_err edba_entity_get(edba_handle_t *h
						, uint32_t *o_entc
						, struct odb_entstat *o_ents);
odb_err edba_stks_get(edba_handle_t *h
		, uint32_t *o_stkc
		, struct odb_structstat *o_stkv);

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
//   This will delete the entry and move all of its pages to garbage.
//   (for clarification, you do not need to call edba_entryclose after this.)
//
// ERRORS:
//
//   - ODB_ECRIT: programmer error (can be ignored) (will be logged)
//   - ODB_ENOSPACE: (edba_entryopenc) no more entry slots availabe
//   - ODB_ENOENT: (edba_entryopenc) (edba_entrydelete) eid is invalid
odb_err edba_entryopenc(edba_handle_t *h, odb_eid *o_eid, edbf_flags flags);
void    edba_entryclose(edba_handle_t *h);
odb_err edba_entrydelete(edba_handle_t *h, odb_eid eid);

// Get a pointer to the entry for read-only purposes.
const odb_spec_index_entry *edba_entrydatr(edba_handle_t *h);

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
//   - ODB_ECRIT - programmer failed to read documentation / other error
//   - ODB_EINVAL - (FUCKUPS) e.type was not ODB_ELMOBJ or handle doesn't have
//                  the entry open in write mode
//   - ODB_EEOF - e.structureid was too high / does not exist
//   - ODB_ENOSPACE - no more space left in file for blank pages.
//   - ODB_ENOMEM - no memory for operaiton
odb_err edba_entryset(edba_handle_t *h, odb_spec_index_entry e);

// ERRORS
//   - ODB_EEOF - offset is too high
//   - ODB_ENOENT - eid is invalid
odb_err edba_pageopen(edba_handle_t *h, odb_eid eid, odb_pid offset,
					  edbf_flags flags);
odb_err edba_pageclose(edba_handle_t *h);

// this allows you to close the current page you're on, and then, open the
// exact next page with a +1 offset. You must not call pageclose before
// calling pageadvance, call pageclose only when you're done with browsing
// through pages.
//
// not that pageadvance will open with the same flags as pageopen. Regardless of
// the error that this returns, you should still call edba_pageclose.
//
// ERRORS
//   - ODB_EEOF - no next page to flip too (you will still have the previous
//     page open). All edba_page* functions operate just as if you never called this.
//   - All other errors will make edba_page* operations unusable (execpt for edba_pageclose)
odb_err edba_pageadvance(edba_handle_t *h);

// edba_pageobjectv_count - returns the number of objects per page: NOTE NOT
// THE SIZE OF THE BODY, BUT THE COUNT OF OBJECTS.
//
// edba_pageobjectv_count - the total amount of objects in page (note: including deleted ones)
// edba_pageobjectc - the total number of bytes of the page body.
unsigned int edba_pageobjectv_count(edba_handle_t *h);
unsigned int edba_pageobjectc(edba_handle_t *h);
const void *edba_pageobjectv_get(edba_handle_t *h);
void       *edba_pageobjectv(edba_handle_t *h);
const odb_spec_struct_struct *edba_pagestruct(edba_handle_t *h);
odb_sid edba_pagestructid(edba_handle_t *h);

// open a new structure for editing.
// Requires strct so it can know how much space to allocate.
// Opens up edba_struct.. functions
//
// edba_structopen, edba_structopenc
//   One of these are required to call any subseqent edba_struct... functions.
//    - edba_structopen - open for just writting.
//    - edba_structopenc - create a new structure and open it for writting.
//    The following fields are used out of the structure, others are ignored:
//     - fixedc, confc, data_ptrc, flags
//
// edba_structclose
//    Closes the structure, completing the workflow.
//
// edba_structdelete
//   must be called after edba_structopen and before edba_structclose. This
//   function will implicitly call edba_structclose. Meaning this is a final
//   operation you can execute for this structure (even if returns non-EINVAL error).
//
// edba_struct_conf
//   get a read-only pointer to the arbitrary configuration.
//
// ERRORS:
//   - ODB_ENOSPACE - (edba_structopenc) cannot create another structure, out of space
//     in structure buffer.
//   - ODB_EINVAL - (edba_structopenc) strct.fixedc was less than 4 (note the spec defines the min. as 2, but I'm doing
//     4 here just incase). todo: remove this error.
//   - ODB_EEXIST - (edba_structdelete) cannot delete because an entry is using
//     this structure.
//   - ODB_ENOENT - (edba_structopen) structure at sid is not initialized/invalid
odb_err edba_structopen(edba_handle_t *h, odb_sid sid);
odb_err edba_structopenc(edba_handle_t *h, odb_sid *o_sid
						 , odb_spec_struct_struct strct);
void    edba_structclose(edba_handle_t *h);
odb_err edba_structdelete(edba_handle_t *h);

const void *edba_structconfv_get(edba_handle_t *h);
void       *edba_structconfv_set(edba_handle_t *h);

#endif