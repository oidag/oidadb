/*
 * LICENSE
 *
 * Copyright Property of Kevin Marschke, all rights reserved.
 * Copying, modifying, sharing is strictly prohibited. Use allowed by exclusive
 * written notice.
 */
/*
 * This file is NOT met to be a manual/documentation. See the documentation
 * files.
 */
#ifndef _EDB_H_
#define _EDB_H_
#include <stdint.h>
#include <syslog.h>
#include <sys/user.h>

#define export __attribute__((__visibility__("default")))

/** @name ID Types
 * Note that these typedefs are by-definition. These will be the same across
 * all builds of all architectures.
 *
 * \{
 */
/// dynamic data pointer
typedef uint64_t odb_dyptr;
/// (o)bject (id)
typedef uint64_t odb_oid;
/// (s)tructure (id)
typedef uint16_t odb_sid;
/// (e)ntity (id)
typedef uint16_t odb_eid;
static odb_eid odb_oid_get_eid(odb_oid oid) {return (odb_eid)(oid >> 0x30);}
static odb_oid odb_oid_set_eid(odb_oid oid, odb_eid eid) {
	return ((odb_oid)(eid) << 0x30) | (oid & 0x0000FFFFFFFFFFFF);
}
/// (r)ow (id)
typedef uint64_t odb_rid;
/// (p)age (id)
typedef uint64_t odb_pid;
///\}

export const char *odb_version();

// odbh is incomplete and a private structure.
typedef struct odbh odbh;

typedef enum odb_err {
	ODB_ENONE = 0,
	ODB_ECRIT = 1,

	/// invalid input. You didn't read the documentation. (This error is your
	/// fault)
	ODB_EINVAL,

	/// Handle is closed, was never opened, or just null when it
	/// shoudn't have been.
	ODB_ENOHANDLE,

	/// Something went wrong with the handle.
	ODB_EHANDLE,
	
	/// Something does not exist.
	ODB_ENOENT,

	/// Something already exists.
	ODB_EEXIST,

	/// End of file/stream.
	ODB_EEOF,

	/// Something wrong with a file.
	ODB_EFILE,

	/// Not a oidadb file.
	ODB_ENOTDB,

	/// Something is already open.
	ODB_EOPEN,

	/// Something is closed.
	ODB_ECLOSED,

	/// No host present.
	ODB_ENOHOST,

	/// Out of bounds.
	ODB_EOUTBOUNDS,

	/// System error, errno(3) will be set.
	ODB_EERRNO,

	/// Something regarding hardware has gone wrong.
	ODB_EHW,

	/// Problem with (or lack of) memory.
	ODB_ENOMEM,

	/// Problem with (lack of) space/disk space
	ODB_ENOSPACE,

	/// Something is stopping.
	ODB_ESTOPPING,

	/// Try again, something else is blocking
	ODB_EAGAIN,

	/// Operation failed due to user-specified lock
	ODB_EULOCK,

	/// Something was wrong with the job description
	ODB_EJOBDESC,

	/// Invalid verison
	ODB_EVERSION,

	/// Something has not obeyed protocol
	ODB_EPROTO,

	/// Bad exchange
	ODB_EBADE,

	/// Something happened to the active stream/pipe
	ODB_EPIPE,

	/// Something was missed
	ODB_EMISSED,

	ODB_EDELETED,

	/// Something wrong with buffer size.
	ODB_EBUFFSIZE,

	// issue regarding out-of-sync data changes.
	ODB_ECONFLICT,

	// whatever happened was because of the user.
	ODB_EUSER,
} odb_err;
export const char *odb_errstr(odb_err error);

typedef enum odb_logchannel_t {
	ODB_LCHAN_CRIT  = 1,
	EDB_LCHAN_ERROR = 2,
	EDB_LCHAN_WARN  = 4,
	EDB_LCHAN_INFO  = 8,
	EDB_LCHAN_DEBUG = 16,
} odb_logchannel_t;
typedef void(odb_logcb_t)(odb_logchannel_t channel, const char *log);
export odb_err odb_logcb(odb_logchannel_t channelmask, odb_logcb_t cb);

struct odb_createparams {
	uint16_t page_multiplier;
	uint16_t structurepages;
	uint16_t indexpages;
};

static const struct odb_createparams odb_createparams_defaults = {
		.page_multiplier = 2,
		.indexpages = 32,
		.structurepages = 32,
};
export odb_err odb_create(const char *path, struct odb_createparams params);
export odb_err odb_createt(const char *path, struct odb_createparams params);


struct odb_hostconfig {
	unsigned int job_buffq;
	unsigned int job_transfersize;
	unsigned int event_bufferq;
	unsigned int worker_poolsize;
	unsigned int slot_count;
	uint32_t    *stat_futex;

	// Reserved.
	int flags;

};
static const struct odb_hostconfig odb_hostconfig_default = {
		.job_buffq = 16,
		.job_transfersize = PAGE_SIZE,
		.event_bufferq = 32,
		.worker_poolsize = 4,
		.slot_count = 16,
		.stat_futex = 0,
		.flags = 0,
};
export odb_err odb_host(const char *path, struct odb_hostconfig hostops);
export odb_err odb_hoststop();

typedef unsigned int odb_hostevent_t;
#define ODB_EVENT_HOSTED 1
#define ODB_EVENT_CLOSED 2
#define ODB_O
#define ODB_EVENT_ANY (odb_event)(-1)
export odb_err odb_hostpoll(const char  *path
		, odb_hostevent_t  event
		, odb_hostevent_t *o_env);
export odb_err odb_handle(const char *path, odbh **o_handle);
export void    odb_handleclose(odbh *handle);

typedef uint8_t odb_type;
#define ODB_ELMINIT  0
#define ODB_ELMDEL   1
#define ODB_ELMSTRCT 2
#define ODB_ELMTRASH 3
#define ODB_ELMOBJ   4
#define ODB_ELMENTS  5
#define ODB_ELMPEND  6
#define ODB_ELMLOOKUP 7
#define ODB_ELMDYN 8
#define ODB_ELMOBJPAGE 9

struct odb_entstat {
	odb_type type;
	odb_sid structureid;
	odb_pid pagec;

	// todo: document this
	uint16_t memorysettings;
};
export odb_err odbh_index(odbh *handle, odb_eid eid
						  , struct odb_entstat *o_entry);

struct odb_structstat {
	unsigned int fixedc; // the entire structre, including dynamic list and
	// flags
	unsigned int start; // the starting byte
	unsigned int dynmc;
	unsigned int confc;
	void *confv;

	unsigned int svid;
};
export odb_err odbh_structs(odbh *handle
							, odb_sid structureid
							, struct odb_structstat *o_struct);
export odb_err odbh_structs_conf(odbh *handle
								 , odb_sid sid
								 , const struct odb_structstat *structstat);

typedef enum odb_option_t {
	ODB_OFILTER,
	ODB_OCOOKIE,
} odb_option_t;
export odb_err odbh_tune(odbh *handle, odb_option_t option, ... /* args */);

struct odbh_jobret {
	odb_err err;
	union {
		odb_oid oid;
		uint32_t length;
	};
	union {
		odb_sid sid;
		odb_eid eid;
	};
};
typedef int (odb_select_cb)(void *cookie, int usrobjc, const void *usrobjv);
typedef int (odb_update_cb)(void *cookie, int usrobjc, void *usrobjv);

export struct odbh_jobret odbh_jobj_alloc(odbh *handle
		, odb_eid eid
		, const void *usrobj);
export struct odbh_jobret odbh_jobj_free(odbh *handle
		, odb_oid oid);
export struct odbh_jobret odbh_jobj_write(odbh *handle
		, odb_oid oid, const void *usrobj);
export struct odbh_jobret odbh_jobj_read(odbh *handle
		, odb_oid oid
		, void *usrobj);
// jobs - if 0 then auto
//        if > 0, then ???
//        if < 0, then "use maximum amount of jobs"
export struct odbh_jobret odbh_jobj_selectcb(odbh *handle
		, odb_eid eid
		, odb_select_cb cb);
export struct odbh_jobret odbh_jobj_updatecb(odbh *handle
		, odb_eid eid
		, odb_update_cb cb);
export struct odbh_jobret odbh_jstk_create(odbh *handle
		, struct odb_structstat);
export struct odbh_jobret odbh_jstk_free(odbh *handle
		, odb_sid sid);
export struct odbh_jobret odbh_jent_create(odbh *handle
		, struct odb_entstat);
export struct odbh_jobret odbh_jent_free(odbh *handle
		, odb_eid eid);
export struct odbh_jobret odbh_jdyn_read(odbh *handle
		, odb_oid oid
		, int idx
		, void *datv
		, int datc);
export struct odbh_jobret odbh_jdyn_write(odbh *handle
		, odb_oid oid
		, int idx
		, const void *datv
		, int datc);
export struct odbh_jobret odbh_jdyn_free(odbh *handle
		, odb_oid oid
		, int idx);

typedef enum odb_eventtype_t {
	ODB_VVOID = 0,
	ODB_VERROR,
	ODB_VACTIVE,
	ODB_VCLOSE,
	ODB_VWRITE,
	ODB_VCREATE,
	ODB_VDELETE,
} odb_eventtype_t;
struct odb_event {

};
export odb_err odbh_poll(odbh *handle, struct odb_event *o_evt);

/**
 * \brief User lock bit constants
 */
typedef enum odb_usrlk {

	/// Object cannot be deleted.
	//EDB_FUSRLDEL   = 0x0001,

	/// Object cannot be written too.
	EDB_FUSRLWR    = 0x0002,

	/// Object cannot be read.
	EDB_FUSRLRD    = 0x0004,

	/// Object cannot be created, meaning if this
	/// object is deleted, it will stay deleted.
	/// If not already deleted, then it cannot be
	/// recreated once deleted.
	///
	/// This will also make it implicitly unable to be used
	/// with creating via an AUTOID.
	//EDB_FUSRLCREAT = 0x0008,

	_EDB_FUSRLALL = 0x000F,
} odb_usrlk;
	
#endif // _EDB_H_
