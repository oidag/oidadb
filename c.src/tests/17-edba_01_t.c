#include "../edbd.h"
#include "../edba.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"

#include <mariadb/mysql.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdatomic.h>

int newdeletedpages = 0;

// hypothosis: the more entries that are created and thus more spread out by
// threads, the more we can decrease our totalinserttime by taking advantage
// of exclusive-asyc-processing.
const int cachesize = 256;
const int extrathreads   = 1; // can be 0 for single threaded.


// the below config is per-thread
const int entries_to_create = 2;
const int records           = 100000;

void scramble(edb_oid *oids, edb_oid *o_random, int records) {

	memcpy(o_random, oids, records * sizeof(edb_oid ));
	size_t i;
	for (i = 0; i < records - 1; i++)
	{
		size_t j = i + rand() / (RAND_MAX / (records - i) + 1);
		edb_oid t = o_random[j];
		o_random[j] = o_random[i];
		o_random[i] = t;
	}
}


uint64_t time_total_insert = 0
		, time_random_read = 0
		, totaltimerandomdelete = 0;

struct threadpayload {
	pthread_t pthread;
	edba_handle_t *edbahandle;
	int totalthreads;

	// use this eid regardless of what you end up creating.
	int useeid;

	int fixedc; // fixedc to create your entries with.
};

void *createentry(void *pl) {
	struct threadpayload *payload = pl;
	edba_handle_t *edbahandle = payload->edbahandle;
	int fixedc = payload->fixedc;

	// create entry-structure pairs
	for(int i = 0; i < entries_to_create; i++) {
		edb_sid structid;
		odb_spec_struct_struct strct;
		strct.fixedc = fixedc;
		strct.flags = 0;
		strct.data_ptrc = 0;
		strct.confc = 0;
		err = edba_structopenc(edbahandle, &structid, strct);
		if (err) {
			test_error("struct create");
			return 1;
		}
		edba_structclose(edbahandle);

		// entry
		edb_eid eid;
		odb_spec_index_entry entryparams;
		entryparams.type = EDB_TOBJ;
		entryparams.memory = 0x2202;
		entryparams.structureid = structid;
		err = edba_entryopenc(edbahandle, &eid, EDBA_FCREATE | EDBA_FWRITE);
		if (err) {
			test_error("openc");
			return 1;
		}
		err = edba_entryset(edbahandle, entryparams);
		if (err) {
			test_error("entryset");
			return 1;
		}
		edba_entryclose(edbahandle);
	}
	return 0;
}

void *gogothread(void *pl) {
	struct threadpayload *payload = pl;
	edba_handle_t *edbahandle = payload->edbahandle;
	int eid = payload->useeid;

	// insert a load of records
	edb_oid *oids = malloc(sizeof(edb_oid) * records);
	edb_oid *oids_random = malloc(sizeof(edb_oid) * records);
	for (int i = 0; i < records; i++) {
		timer t = timerstart();
		oids[i] = ((edb_oid) eid) << 0x30;
		if ((err = edba_objectopenc(edbahandle, &oids[i], EDBA_FWRITE |
		                                                  EDBA_FCREATE))) {
			test_error("creating %d", i);
			return 1;
		}
		int fixedc = edba_objectstruct(edbahandle)->fixedc;
		uint8_t *data = edba_objectfixed(edbahandle);

		for (int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t)j;
		}
		edba_objectclose(edbahandle);
		atomic_fetch_add(&time_total_insert, timerend(t));
	}

	// randomly read through the records and make sure their as expected.
	scramble(oids, oids_random, records);
	test_log("reading %ld rows...", records);
	for(int i = 0; i < records; i++) {
		timer t = timerstart();
		if((err = edba_objectopen(edbahandle, oids_random[i], EDBA_FWRITE))) {
			test_error("reading %d", i);
		}
		int fixedc = edba_objectstruct(edbahandle)->fixedc;
		uint8_t *data = edba_objectfixed(edbahandle);
		for(int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			if(data[j] != (uint8_t)j) {
				test_error("invalid value");
				return 1;
			}
		}
		edba_objectclose(edbahandle);
		atomic_fetch_add(&time_random_read, timerend(t));
	}
	return 0;
}


uint64_t totalmysqlinsert = 0;
uint64_t totalmysqlupdate = 0;

void newdel(odbtelem_data d) {
	newdeletedpages++;
};
edbpcache_t *globalcache;



int main(int argc, const char **argv) {
	srand(47237427);

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
	if (fd == -1) {
		test_error("bad fd");
		return 1;
	}
	// edbd
	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if (err) {
		test_error("edbd_open failed");
		return 1;
	}
	// edbp
	err = edbp_cache_init(&dfile, &globalcache);
	if (err) {
		test_error("edbp_cache_init");
		return 1;
	}
	err = edbp_cache_config(globalcache, EDBP_CONFIG_CACHESIZE, cachesize);
	if (err) {
		test_error("edbp_cache_config");
		return 1;
	}
	// edba host.
	edba_host_t *edbahost;
	if ((err = edba_host_init(&edbahost, globalcache, &dfile))) {
		test_error("host");
		return 1;
	}

	struct threadpayload payloads[extrathreads+1];

	// edba handles
	test_log("initializing threads...");
	for(int i = 0; i < extrathreads+1; i++) {
		if ((err = edba_handle_init(edbahost, 69000+i, &payloads[i]
		.edbahandle))) {
			test_error("handle");
			return 1;
		}
		payloads[i].totalthreads = extrathreads+1;
		payloads[i].useeid = 4 + (rand() % (extrathreads+1));
		payloads[i].fixedc = 20 + (rand() % (100));
	}

	test_log("creating %d entries asyc...", (extrathreads+1)*2);
	for(int i = 0; i < extrathreads; i++) {
		pthread_create(&payloads[i].pthread
					   , 0
					   , createentry
					   , &payloads[i]);
	}
	// main thread
	createentry(&payloads[extrathreads]);
	// join threads
	test_log("joining threads...");
	for(int i = 0; i < extrathreads; i++) {
		pthread_join(payloads[i].pthread, 0);
	}
	if(test_waserror) {
		return 1;
	}

	test_log("inserting a total of %ld rows asyc...", records*(extrathreads+1));
	for(int i = 0; i < extrathreads; i++) {
		pthread_create(&payloads[i].pthread
				, 0
				, gogothread
				, &payloads[i]);
	}
	// main thread
	gogothread(&payloads[extrathreads]);
	// join threads
	test_log("joining threads...");
	for(int i = 0; i < extrathreads; i++) {
		pthread_join(payloads[i].pthread, 0);
	}
	if(test_waserror) {
		return 1;
	}

	test_log("closing db...");
	for(int i = 0;i < extrathreads+1;i++) {
		edba_handle_decom(payloads[i].edbahandle);
	}
	edba_host_free(edbahost);
	edbp_cache_free(globalcache);
	edbd_close(&dfile);
	close(fd);

	printf("oidadb total time inserting %d rows: %fs\n", records, timetoseconds
	(time_total_insert));
	printf("oidadb total time key-updating %d rows: %fs\n", records,
		   timetoseconds
			(time_random_read));
	printf("oidadb time-per-insert: %fns\n"
		   , (double)time_total_insert / (double)records);
	printf("oidadb time-per-select: %fns\n"
		   , (double)time_random_read / (double)records);
	printf("oidadb time-per-select-random: %fns\n"
		   , (double)totaltimerandomdelete/(double)(records));
}