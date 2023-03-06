#include "../edbd.h"
#include "../edba.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
int newdeletedpages = 0;

const int cachesize = 256;

void newdel(odbtelem_data d) {
	newdeletedpages++;
};

int main(int argc, const char **argv) {

	// create an empty file
	test_mkdir();
	test_mkfile(argv[0]);
	odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return 1;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if(fd == -1) {
		test_error("bad fd");
		return 1;
	}

	// edbd
	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return 1;
	}

	// edbp
	edbpcache_t *cache;
	err = edbp_cache_init(&dfile, &cache);
	if(err) {
		test_error("edbp_cache_init");
		return 1;
	}
	err = edbp_cache_config(cache, EDBP_CONFIG_CACHESIZE, cachesize);
	if(err) {
		test_error("edbp_cache_config");
		return 1;
	}

	// edba
	edba_host_t *edbahost;
	if((err = edba_host_init(&edbahost, cache, &dfile))) {
		test_error("host");
		return 1;
	}
	edba_handle_t *edbahandle;
	if((err = edba_handle_init(edbahost, &edbahandle))) {
		test_error("handle");
		return 1;
	}


	edba_host_free(edbahost);
	edbp_cache_free(cache);
	edbd_close(&dfile);
	close(fd);
}