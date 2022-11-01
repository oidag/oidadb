#include <strings.h>

#include "edba.h"
#include "edbp.h"
#include "edbd.h"

edb_err edba_host_init(edba_host_t *o_host, edbpcache_t *pagecache, edbd_t *descriptor) {
	bzero(o_host, sizeof(edba_host_t));
	o_host->descriptor = descriptor;
	o_host->pagecache = pagecache;
	edb_err err = edbl_init(&o_host->lockhost, descriptor);
	return err;
}
void    edba_host_decom(edba_host_t *host) {
	edbl_decom(&host->lockhost);
}

edb_err edba_handle_init(edba_host_t *host, edba_handle_t *o_handle) {
	edbl_newhandle(&host->lockhost, &o_handle->lockh);
}

void edba_handle_decom(edba_handle_t *src) {
	edbl_destroyhandle(&src->lockh);
}