#include "edba.h"
#include "edbp.h"
#include "edbd.h"

#include <strings.h>
#include <malloc.h>

odb_err edba_host_init(edba_host_t **o_host,
                       edbpcache_t *pagecache,
                       edbd_t *descriptor) {
	edba_host_t *ret = malloc(sizeof(edba_host_t));
	if(ret == 0) {
		log_critf("malloc");
		return ODB_ECRIT;
	}
	*o_host = ret;
	bzero(ret, sizeof(edba_host_t));
	ret->descriptor = descriptor;
	ret->pagecache = pagecache;
	odb_err err = edbl_host_init(&ret->lockhost, descriptor);
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

odb_err edba_handle_init(edba_host_t *host,
                         unsigned int name,
                         edba_handle_t **o_handle) {
	odb_err err;
	edba_handle_t *ret = malloc(sizeof(edba_handle_t));
	if(ret == 0) {
		log_critf("malloc");
		return ODB_ECRIT;
	}
	*o_handle = ret;
	bzero(ret, sizeof(edba_handle_t));
	ret->parent = host;
	if((err = edbl_handle_init(host->lockhost, &ret->lockh))) {
		free(ret);
		return err;
	}
	if((err = edbp_handle_init(ret->parent->pagecache, name, &ret->edbphandle))) {
		edbl_handle_free(ret->lockh);
		free(ret);
		return err;
	}
	return 0;
}

void edba_handle_decom(edba_handle_t *src) {
	edbl_handle_free(src->lockh);
	edbp_handle_free(src->edbphandle);
	free(src);
}