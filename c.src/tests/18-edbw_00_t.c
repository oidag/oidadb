
#include <stdio.h>
#include <pthread.h>
#include <wait.h>
#include <stdlib.h>
#include "teststuff.h"

#include "../edbd.h"
#include "../edba.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "../include/oidadb.h"
#include "../edbw.h"
#include "../edbw_u.h"
#include "../wrappers.h"

int newdeletedpages = 0;

const int cachesize = 256;
const int records   = 20000;

struct threadpayload {
	pthread_t thread;
};

static struct shmobj {
} shmobj;

// forward declarations
pthread_t hostthread;
uint32_t host_futex = 0;
void *func_hostthread(void *v);
void *handlethread(void *payload);

////////////////////////////////////////////////////////////////////////////////
static const int handle_procs = 0; // how many extra processes to start (0
// means
// none)
static const int handle_threads = 1; // how many threads to start PER THREAD (0
// means none, meaning nothing will be done)


edbpcache_t *globalcache;

void test_main() {
	srand(47237427);

	// create an empty file
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return ;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if (fd == -1) {
		test_error("bad fd");
		return ;
	}

	// edbd
	edbd_t dfile;
	edbd_config config = edbd_config_default;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if (err) {
		test_error("edbd_open failed");
		return ;
	}

	// edbp
	err = edbp_cache_init(&dfile, &globalcache);
	if (err) {
		test_error("edbp_cache_init");
		return ;
	}
	err = edbp_cache_config(globalcache, EDBP_CONFIG_CACHESIZE, cachesize);
	if (err) {
		test_error("edbp_cache_config");
		return ;
	}

	// edba
	edba_host_t *edbahost;
	if ((err = edba_host_init(&edbahost, globalcache, &dfile))) {
		test_error("host");
		return ;
	}
	edba_handle_t *edbahandle;
	if ((err = edba_handle_init(edbahost, 69, &edbahandle))) {
		test_error("handle");
		return ;
	}

	// host the shm
	edbs_handle_t *shm_host;
	if((err = edbs_host_init(&shm_host, odb_hostconfig_default))) {
		test_error("host_init");
		return;
	}

	// edbw
	edb_worker_t worker;
	err = edbw_init(&worker, edbahost, shm_host);
	if(err) {
		test_error("edbw_init");
		return;
	}

	// start the async thread.
	err = edbw_async(&worker);
	if(err) {
		test_error("edbw_async");
		return;
	}

	// now start putting some jobs in there
	edbs_job_t job;
	err = edbs_jobinstall(shm_host, ODB_JTESTECHO, &job);
	if(err) {
		test_error("job install");
		return;
	}

	// write an echo buffer
	int buffc = 50;
	void *buffv = malloc(buffc);
	for(int i = 0; i < buffc; i++) {
		((char *)buffv)[i] = (char)i;
	}

	err = edbs_jobwrite(job, &buffc, sizeof(buffc));
	if(err) {
		test_error("");
		return;
	}
	err = edbs_jobwrite(job, buffv, buffc);
	if(err) {
		test_error("");
		return;
	}


	// now listen to the reply.
	int newbuffc;
	err = edbs_jobread(job, &newbuffc, sizeof(newbuffc));
	if(err || newbuffc != buffc) {
		test_error("echo failed");
		return;
	}
	void *newbuffv = malloc(newbuffc);
	err = edbs_jobread(job, newbuffv, newbuffc);
	if(err) {
		test_error("echo failed");
		return;
	}
	for(int i = 0; i < buffc; i++) {
		if(((char *)buffv)[i] != ((char *)newbuffv)[i]) {
			test_error("buffer mis-match");
			return;
		}
	}
	free(newbuffv);
	free(buffv);

	edbs_host_close(shm_host);
	edbw_join(&worker);
	edbw_decom(&worker);
	edbs_host_free(shm_host);
	edba_handle_decom(edbahandle);
	edba_host_free(edbahost);
	edbp_cache_free(globalcache);
	edbd_close(&dfile);
	close(fd);

}