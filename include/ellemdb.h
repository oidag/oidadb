#ifndef _EDB_H_
#define _EDB_H_ 1

#include <stdint.h>

#include "edb.h"


// easy typedefs.
typedef edb_did uint64_t;
typedef edb_oid uint64_t;

// hanlder

// internal structures
typedef struct edbh_st edbh;
typedef uint64_t edb_pid;

// input structures

/********************************************************************
 *
 * Prefix (no code, just comments)
 *
 ********************************************************************/

// RW Conjugation
//
// Function groups that use this conjugation all have 2 parameters,
// the first parameter being a pointer to the handle and the second
// being a structure of the specific item (either object or data).
// -load and -free will be pass-by-refrence in the second parameter.
//
//  -load(*handle, *struct)
//  -free(*handle, *struct)
//  -write(*handle, struct)
//
// The second parameter's structure will contain a void pointer named
// "binv" (array of binary) as well as an id simply called "id". It
// may also contain some other fields not covered in this
// description. But the behaviour of binv will be dictated by the
// choice of the conjugation.
//
// This conjugation contains -load, -free. and -write.
//
//   -load will have binv point to internal memory and cannot be
//   modified, once done you must use the -free function. 
//
//   -write will not modify binv but instead binv should point to the
//   data to which will be written to the database. Write also takes
//   into account id to determain the nature of the operation:
//
//     - creating: id  = 0, binv != 0
//     - updating: id != 0, binv != 0
//     - deleting: id != 0, binv  = 0
//
// THREADING: It is important that if you need to perform extensive
//   operations to the data returned by -load, you must copy binv to a
//   seperate space in memory and then free it. As long as binv's have
//   not been free'd they are read-locked, meaning write operations
//   will be blocked until their respective -free is called. -write
//   operations will be done attomically and all -load functions will
//   be blocked until the -write is complete.




/********************************************************************
 *
 * Errors
 *
 ********************************************************************/

// logmask. See syslog(3)'s "option" argument.
#include <syslog.h>
#define EDB_LCRIT    LOG_CRIT // critical (not the caller's fault)
#define EDB_LWARNING LOG_WARNING // warning
#define EDB_LINFO    LOG_INFO  // informational / verbose
#define EDB_LDEBUG   LOG_DEBUG // debug message. may be redundant/useless


// All of these functions will work regardless of the open state of
// the handle so long that the handle was initialized with bzero(3).
//
// edb_errstr converts the relevant error into a string message.
//
// edb_setlogger sets the logger for the handle. The callback must be
// threadsafe. The callback will only be called on the OR'd bitmask
// specified in logmask (see EDB_L... enums). This function is simular
// to syslog(3). Setting cb to null will disable it.
const char *edb_errstr(edb_err error);
edb_err edb_setlogger(edbh *handle, int logmask,
					  void (*cb)(int logtype, const char *log));

// error enums.
enum edb_err {
	
	// no error - explicitly 0 - as all functions returning edb_err is
	// expected to be layed out as follows for error handling:
	//
	//   if(err = edb_func())
	//   {
	//        // handle error
	//   } else {
	//        // no error
	//   }
	//   // regardless of error
	//
	EDB_ENONE = 0,

	// critical error, not the callers fault. You should never get
	// this. If you do, try what you did again and look closely at
	// the logger callback with all logmask.
	//
	// Everytime this is returned, a EDB_LCRIT message would have been
	// generated.
	EDB_ECRIT = 1,

	// Handle is closed, was never opened, or just null when it
	// shoudn't have been. Use edb_open.
	EDB_ENOHANDLE,

	// invalid input. You didn't read the documentation properly.
	EDB_EINVAL,
	
	// does not exist
	EDB_ENOENT,

	// exists
	EDB_EEXIST,

	// end of file / stream
	EDB_EEOF,

	// something wrong with file
	EDB_EFILE,

	EDB_ENOTVALID,

	// something is already open
	EDB_EOPEN,
	EDB_ENOHOST,

	// system error, check errno.
	EDB_EERRNO,

	// something regarding hardware
	EDB_EHW,
};




/********************************************************************
 *
 * Database creating, opening and closing
 *
 ********************************************************************/

// edb_open will create the file if not exists.
#define EDB_OCREAT O_CREAT

typedef struct edb_open_st {

	// the file path
	const char *path;

	// the agent id (arbitrary, more of a cookie)
	uint64_t agent;

	// see EDB_O... options
	int openoptions;
	
} edb_open_t;

// edb_open errors:
//   EDB_EINVAL - handle is null.
//   EDB_EINVAL - params.path is null
//   EDB_EERRNO - error with stat(2), open(2), (ie, file does not exist, permssions)
//   EDB_EFILE  - path is not a regular file.
//   EDB_ENOTVALID - file is invalid format, possibliy not a database.
//   EDB_EHW    - this file was created on a different (non compatible) architecture
//   EDB_EOPEN  - annother process already has the file open.
//   EDB_ENOHOST - no host process found and cannot be started
//
// These functions provide access to a database provided by the
// instrunctions set forth in params. edb_open will write to edbh and
// mark it as open so it can be used in all other functions.
//
// There is a race condition to where 2 processes attempt to call
// edb_open with both containing instunctions to start the host
// proccess. In this case, 1 call will succeed and the other
// will have EDB_EOPEN returned. In that case the process that failed
// to open should attempt to run edb_open again.
//
// edb_create will fail if the file already exists.
edb_err edb_open(edbh *handle, edb_open_t params);   // will create if not existing. Not thread safe.
edb_err edb_close(edbh *handle);
			   



/********************************************************************
 *
 * Entries
 *
 ********************************************************************/

// see spec.
typedef struct edb_entry_st {
	edb_eid id;
	edb_pid page_start;
	uint8_t type;
	uint8_t page_size;
	uint16_t rsvd;
	uint64_t pagecount;
	edb_eid ref;
} edb_entry_t;


// shared-memory access functions
//
// The returned arrays from all these functions are pointers to parts
// of the handle. They do not need to be freed, all memeory is managed
// within edb_open and edb_close.
//
// edb_structs and edb_index will take in pointers to integer values
// and arrays
//
// Volitility:
//
//   Both structv and entryv are pointers to shared memory that can
//   and will be edited by multiple processes at once at any time. To
//   properly keep track of these changes you should use
//   edb_select. But be aware that structv and entryv's contents will
//   change.
//
edb_err edb_index(edbh *handle, const int *entryc, edb_entry_t *const *entryv);





/********************************************************************
 *
 * Structures
 *
 ********************************************************************/


typedef struct edb_struct_st {
	uint16_t    id;               // structure id
	uint16_t    fixedc;            // fixed length size
	uint8_t     data_ptrc;         // data pointer size
	uint8_t     flags;             // flags
	uint16_t    binc;    // configuration byte size
	const void *binv;    // arbitrary configuration
} edb_struct_t;

// See [[RW Conjugation]]
//
// Reads and writes structures.
//
edb_err edb_structload (edbh *handle, const edb_struct_t *strct);
edb_err edb_structfree (edbh *handle, const edb_struct_t *strct);
edb_err edb_structwrite(edbh *handle, const edb_struct_t strct);




/********************************************************************
 *
 * Object reading and writting.
 *
 ********************************************************************/

typedef struct edb_obj_st {
	edb_oid id;

	// fixed-length data of the row. The length is found in the
	// structure.
	void *binv;
} edb_obj_t;


// See [[RW Conjugation]]
//
// Reads and writes objects to the database. Leaves non-fixed data
// untouched.
//
edb_err edb_objload (edbh *handle, edb_obj_t *obj);
edb_err edb_objfree (edbh *handle, edb_obj_t *obj);
edb_err edb_objwrite(edbh *handle, edb_obj_t obj);




/********************************************************************
 *
 * Data reading and writting.
 *
 ********************************************************************/

typedef struct edb_data_st {
	edb_did id;
	
	uint16_t offset;
	uint16_t size; // the overal size of the data.

	// binc will be 0 binv will be null when returned from
	// edb_datload!
	//
	// See edb_datnext!
	uint16_t binc;
	void    *binv;

	// stream sense data can go across multiple pages
	int (*nextrange)(void *buf);
} edb_data_t;

// See [[RW Conjugation]]
//
// Reads and writes data to the database.
//
edb_err edb_datload (edbh *handle, edb_data_t *data);
edb_err edb_datfree (edbh *handle, edb_data_t *data);
edb_err edb_datwrite(edbh *handle, edb_data_t data);

// between calling edb_datload and edb_datfree, if you want to read
// the contents of data.binv or data.binc you must use edb_datnext.
//
// When initially returned by edb_datload, data.binc will be 0 and
// data.binv will be null. In order to see the actual data you must
// use this function, which will set data.binc and data.binv to the
// "next part".
//
// You may need to call edb_datnext multiple times. Only when
// data.binc is once again 0 is when you know that you've read through
// all the data.
//
// The purpose of this function is becuase a datarange may strattle
// across multiple pages.
edb_err edb_datnext (edbh *handle, edb_data_t *data);




/********************************************************************
 *
 * Query functions
 *
 ********************************************************************/

// Select
//
// Listens for changes in the database by instrunctions set forth in
// params.
//
// This function blocks the calling thread and will return once a new
// change is detected.
//
// Limit 1 call to edb_select to each handle. If you attempt to call
// edb_select asyncrounously with the same handle, this will cause an
// error.
//
// If this function is called too infrequently then there's a
// possibility that the caller will miss certain events. But this is a
// very particular edge case that is described elsewhere probably.
edb_err edb_select(edbh *handle, edb_select_t *params);


typedef struct edb_query_st {

	uint16_t eid;  // entry id (must be edb_new)
	
	int pagec;     // The max amount of sequencial pages to look
				   // through. Leave this to 0 to disable the use of
				   // pagec and pagestart
	
	int pagestart; // The page index to which to start
	
	int (*queryfunc)(edb_obj *row) func; // query function. the
										 // pointer to row is not safe
										 // to save.
} edb_query_t;

//
// Thread safe.
//
edb_err edb_query(edbh *handle, edb_query_t *query);




/********************************************************************
 *
 * Debugging functions.
 *
 ********************************************************************/

typedef struct edb_infohandle_st {
} edb_infohandle_t;

typedef struct edb_infodatabase_st {
} edb_infodatabase_t;


// all these info- functions just return their relevant structure's
// data that can be used for debugging reasons.
edb_err edb_infohandle(edbh *handle, info *edb_infohandle_t);
edb_err edb_infodatabase(edbh *handle, info *edb_infodatabase_t);

// dump- functions just format the stucture and pipe it into fd.
// these functions provide no more information than the equvilient
// info- function.
int edb_dumphandle(edbh *handle, fd int);
int edb_dumpdatabase(edbh *handle, fd int);
	
#endif // _EDB_H_
