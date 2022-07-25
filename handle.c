

#include "include/ellemdb.h"
#include "handle.h"
#include "errors.h"
#include "host.h"

edb_err edb_open(edbh *handle, edb_open_t params) {

	// check for easy EDB_EINVAL
	if(handle == 0 || params.path == 0)
		return EDB_EINVAL;

	// get the hostpid.
	edb_err hosterr = edb_host_getpid(params.path, &(handle->hostpid));
	if(hosterr) {
		return hosterr;
	}

	// at this point, the file exist and has a host attached to it.
	// said host's pid is stored in handle->hostpid

	// todo: figure out how the handle will call other things from the host...

}


edb_err edb_close(edbh *handle);