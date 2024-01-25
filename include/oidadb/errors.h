#ifndef _ODB_ERRORS_H_
#define _ODB_ERRORS_H_

#include "common.h"

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

	/// Bad descriptor
	ODB_EBADF,

	/// Something happened to the active stream/pipe
	ODB_EPIPE,

	/// Something was missed
	ODB_EMISSED,

	ODB_EDELETED,

	/// Something wrong with buffer size.
	ODB_EBUFFSIZE,

	/// Something wrong with buffer
	ODB_EBUFF,

	// issue regarding out-of-sync data changes.
	ODB_ECONFLICT,

	// whatever happened was because of the user.
	ODB_EUSER,
} odb_err;

export const char *odb_errstr(odb_err error);

#endif