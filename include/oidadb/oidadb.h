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

#include "common.h"
#include "errors.h"
#include "pages.h"

#include <stdint.h>
#include <syslog.h>
#include <sys/user.h>



export const char *odb_version();





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





const char *odb_typestr(odb_type);

struct odb_entstat {
	odb_type type;
	odb_sid structureid;
	odb_pid pagec;

	// todo: document this
	uint16_t memorysettings;
};
export odb_err odbh_index(odbh *handle, odb_eid eid
						  , struct odb_entstat *o_entry);




	
#endif // _EDB_H_
