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
#include "blocks.h"

#include <stdint.h>
#include <syslog.h>
#include <sys/user.h>


export const char *odb_version();


/*
typedef enum odb_logchannel_t {
	ODB_LCHAN_CRIT  = 1,
	EDB_LCHAN_ERROR = 2,
	EDB_LCHAN_WARN  = 4,
	EDB_LCHAN_INFO  = 8,
	EDB_LCHAN_DEBUG = 16,
} odb_logchannel_t;
typedef void(odb_logcb_t)(odb_logchannel_t channel, const char *log);
export odb_err odb_logcb(odb_logchannel_t channelmask, odb_logcb_t cb);
*/


#endif // _EDB_H_
