#include "edba.h"
#include "edbp.h"
#include "edbd.h"

#include <strings.h>
#include <malloc.h>

edb_err edba_host_init(edba_host_t **o_host,
					   edbpcache_t *pagecache,
					   edbd_t *descriptor) {
	edba_host_t *ret = malloc(sizeof(edba_host_t));
	if(ret == 0) {
		log_critf("malloc");
		return EDB_ECRIT;
	}
	*o_host = ret;
	bzero(ret, sizeof(edba_host_t));
	ret->descriptor = descriptor;
	ret->pagecache = pagecache;
	edb_err err = edbl_host_init(&ret->lockhost, descriptor);
	if(err) {
		free(ret);
		return err;
	}
	return err;
}
void    edba_host_free(edba_host_t *host) {
	edbl_host_free(host->lockhost);
	free(host);
}

edb_err edba_handle_init(edba_host_t *host,
						 edba_handle_t **o_handle) {
	edba_handle_t *ret = malloc(sizeof(edba_handle_t));
	if(ret == 0) {
		log_critf("malloc");
		return EDB_ECRIT;
	}
	*o_handle = ret;
	bzero(ret, sizeof(edba_handle_t));
	edbl_handle_init(host->lockhost, &ret->lockh);
	return 0;
}

void edba_handle_decom(edba_handle_t *src) {
	edbl_handle_free(src->lockh);
	free(src);
}