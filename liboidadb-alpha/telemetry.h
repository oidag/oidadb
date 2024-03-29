#ifndef  _EDB_TELEM_H_
#ifndef _edf_options_
#error "options.h must be included before telemetry.h"
#endif

#include "options.h"
#include <oidadb/oidadb.h>
#include <oidadb/telemetry.h>




#ifdef EDBTELEM

// See include/telementry.h / "odbtelem_class"
void telemetry_pages_newobj(unsigned int entryid, odb_pid startpid, unsigned int straitc);
void telemetry_pages_newdel(odb_pid startpid);
void telemetry_pages_cached(odb_pid pid);
void telemetry_pages_decached(odb_pid pid);
void telemetry_workr_accepted(unsigned int workerid, unsigned int jobslot);
void telemetry_workr_pload(unsigned int workerid, odb_pid pageid);
void telemetry_workr_punload(unsigned int workerid, odb_pid pageid);
//void telemetry_job_added(unsigned int workerid, unsigned int jobslot); // later
void telemetry_job_complete(unsigned int workerid, unsigned int jobslot);


//todo:
//void telemetry_jobs_added(unsigned int jobslot);
//void telemetry_jobs_completed(unsigned int jobslot);
#else  // EDBTELEM
// if EDBTELEM is not defined, then we instead make turn these funcitons into
// "nothing symbols".

#define telemetry_pages_newobj(...)
#define telemetry_pages_newdel(...)
#define telemetry_pages_cached(...)
#define telemetry_pages_decached(...)
#define telemetry_workr_accepted(...)
#define telemetry_workr_pload(...)
#define telemetry_workr_punload(...)
#define telemetry_jobs_added(...)
#define telemetry_jobs_completed(...)
#endif // EDBTELEM


#endif //_EDB_TELEM_H_