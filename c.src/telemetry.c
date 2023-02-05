#include <strings.h>
#include "include/telemetry.h"
#include "options.h"
#include "telemetry.h"
#include "errors.h"

static int telemenabled = 0;
static odbtelem_cb cbs[_ODBTELEM_LAST] = {0};

#ifdef EDBTELEM_DEBUG
static const char *class2str(odbtelem_class c) {
	switch (c) {
		case ODBTELEM_PAGES_NEWOBJ:
			return "ODBTELEM_PAGES_NEWOBJ";
	case ODBTELEM_PAGES_NEWDEL:
		return "ODBTELEM_PAGES_NEWDEL";
	case ODBTELEM_PAGES_CACHED:
		return "ODBTELEM_PAGES_CACHED";
	case ODBTELEM_PAGES_DECACHED:
		return "ODBTELEM_PAGES_DECACHED";
	case ODBTELEM_WORKR_ACCEPTED:
		return "ODBTELEM_WORKR_ACCEPTED";
	case ODBTELEM_WORKR_PLOAD:
		return "ODBTELEM_WORKR_PLOAD";
	case ODBTELEM_WORKR_PUNLOAD:
		return "ODBTELEM_WORKR_PUNLOAD";
	case ODBTELEM_JOBS_ADDED:
		return "ODBTELEM_JOBS_ADDED";
	case ODBTELEM_JOBS_COMPLETED:
		return "ODBTELEM_JOBS_COMPLETED";
		default:return "UNKNOWN";
	}
}
static void preret(odbtelem_data d) {
	log_debugf("%s(pid: %ld, eid/workid: %d, pagec/jobid: %d)",
			   class2str(d.class),
			   d.arg0,
			   d.arg1,
			   d.arg2);
}
#else
#define preret(...)
#endif

edb_err odbtelem(int enabled) {
#ifndef EDBTELEM
	return EDB_EVERSION;
#endif
	if(telemenabled && !enabled) {
		// clear out callbacks
		for(int i = 0; i < _ODBTELEM_LAST; i++) {
			cbs[i] = 0;
		}
	}
	telemenabled = enabled;
	return 0;
}

edb_err odbtelem_bind(odbtelem_class class, odbtelem_cb cb) {
	if(!telemenabled) {
		return EDB_ENOENT;
	}
	if(class >= _ODBTELEM_LAST) {
		return EDB_EINVAL;
	}
	cbs[class] = cb;
	return 0;
}

void telemetry_pages_newobj(unsigned int entryid,
                            edb_pid startpid, unsigned int straitc) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_NEWOBJ,
			.entryid = entryid,
			.pageid = startpid,
			.pagec = straitc,
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_pages_newdel(edb_pid startpid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_NEWDEL,
			.pageid = startpid,
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_pages_cached(edb_pid pid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_CACHED,
			.pageid = pid,
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_pages_decached(edb_pid pid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_DECACHED,
			.pageid = pid,
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_workr_accepted(unsigned int workerid, unsigned int jobslot) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_ACCEPTED,
			.workerid = workerid,
			.jobslot = jobslot,
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_workr_pload(unsigned int workerid, edb_pid pageid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_PLOAD,
			.workerid = workerid,
			.pageid = pageid
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}
void telemetry_workr_punload(unsigned int workerid, edb_pid pageid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_PUNLOAD,
			.workerid = workerid,
			.pageid = pageid
	};
	preret(d);
	if(cbs[d.class]) cbs[d.class](d);
}