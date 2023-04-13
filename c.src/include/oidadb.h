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
/// (r)ow (id)
typedef uint64_t odb_rid;
/// (p)age (id)
typedef uint64_t odb_pid;
///\}

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

typedef struct odb_createparams_t {
	uint16_t page_multiplier;
	uint16_t structurepages;
	uint16_t indexpages;
} odb_createparams_t;

static const odb_createparams_t odb_createparams_defaults = (odb_createparams_t){
		.page_multiplier = 2,
		.indexpages = 32,
		.structurepages = 32,
};
export odb_err odb_create(const char *path, odb_createparams_t params);
export odb_err odb_createt(const char *path, odb_createparams_t params);


typedef struct odb_hostconfig_t {
	unsigned int job_buffq;
	unsigned int job_transfersize;
	unsigned int event_bufferq;
	unsigned int worker_poolsize;
	unsigned int slot_count;

	// Reserved.
	int flags;

} odb_hostconfig_t;
static const odb_hostconfig_t odb_hostconfig_default = {
		.job_buffq = 16,
		.job_transfersize = PAGE_SIZE,
		.event_bufferq = 32,
		.worker_poolsize = 4,
		.slot_count = 16,
		.flags = 0,
};
export odb_err odb_host(const char *path, odb_hostconfig_t hostops);
export odb_err odb_hoststop();

typedef unsigned int odb_hostevent_t;
#define ODB_EVENT_HOSTED 1
#define ODB_EVENT_CLOSED 2
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

typedef struct odb_entstat_t {
	odb_type type;
	odb_sid structureid;
} odb_entstat_t;
export odb_err odbh_index(odbh *handle, odb_eid eid, odb_entstat_t *o_entry);

typedef struct odb_structstat_t {
	unsigned int fixedc;
	unsigned int dynmc;
	unsigned int confc;
	void *confv;
} odb_structstat_t;
export odb_err odbh_structs(odbh *handle
							, odb_sid structureid
							, odb_structstat_t *o_struct);

typedef enum odb_option_t {
	ODB_OFILTER,
	ODB_OCOOKIE,
} odb_option_t;
export odb_err odbh_tune(odbh *handle, odb_option_t option, ... /* args */);

typedef enum odb_jobtype_t {
	// Objects
	ODB_JCREATE
	, ODB_JDELETE
	, ODB_JWRITE
	, ODB_JREAD
	, ODB_JSELECT
	, ODB_JUPDATE

	// Structure
	, ODB_JSTRCTCREATE
	, ODB_JSTRCTDELETE

	// Entities
	, ODB_JENTCREATE
	, ODB_JENTDELETE

	// Dynamics
} odb_jobtype_t;
export odb_err odbh_job   (odbh *handle, odb_jobtype_t jobtype);
export odb_err odbh_jwrite(odbh *handle, const void *buf, int bufc);
export odb_err odbh_jread (odbh *handle, void *o_buf, int bufc);
export odb_err odbh_jclose(odbh *handle);

typedef enum odb_eventtype_t {
	ODB_VWRITE,
	ODB_VCREATE,
	ODB_VDELETE,
} odb_eventtype_t;
typedef struct odb_event_t {

} odb_event_t;
export odb_err odbh_poll(odbh *handle, odb_event_t *o_evt);

/**
 * \brief User lock bit constants
 */
typedef enum odb_usrlk {

	/// Object cannot be deleted.
	EDB_FUSRLDEL   = 0x0001,

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
	EDB_FUSRLCREAT = 0x0008,

	_EDB_FUSRLALL = 0x000F,
} odb_usrlk;
	
#endif // _EDB_H_
