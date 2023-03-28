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
const int extrathreads   = 11; // can be 0 for single threaded.
const int page_multiplier = 2;

// if 1, then each thread will perform the random look up AND write to the
// object thus requiring a XL lock
const int random_write = 1;

// if 1, then there's a chance that a given thread will run through the
// routine_randread with a scranbled array of another threads OID's. This
// means 2 threads could have the chance of butting heads.
const int colliding_oids = 0;

// if 1, then all threads will have their exclusive entries and will not
// bother eachother during routine_insert.
const int exclusiveeids = 1;

const int memorysettings    = 0x2202; // depth, ref2 strait, rsvd, ref0 strait

// the below config is per-thread
const int entries_to_create = 7;
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


uint64_t time_individual_insert = 0
		, time_individual_random_read = 0
		, time_total_insert = 0
		, time_total_random_read = 0;

struct threadpayload {
	pthread_t pthread;
	edba_handle_t *edbahandle;
	int totalthreads;

	// use this eid regardless of what you end up creating.
	int useeid;

	int fixedc; // fixedc to create your entries with.


	edb_oid *oids, *oids_scrambled;
};

void *createentry(void *pl) {
	struct threadpayload *payload = pl;
	edba_handle_t *edbahandle = payload->edbahandle;
	int fixedc = payload->fixedc;
	edb_err err; // make sure we have errs in our thread's stack.

	// create entry-structure pairs
	// we do +1 here so we can create the last entry just to delete it.
	for(int i = 0; i < entries_to_create+1; i++) {
		edb_sid structid;
		odb_spec_struct_struct strct;
		strct.fixedc = fixedc;
		strct.flags = 0;
		strct.data_ptrc = 0;
		strct.confc = 0;
		err = edba_structopenc(edbahandle, &structid, strct);
		if (err) {
			test_error("struct create %d", err);
			return 1;
		}
		edba_structclose(edbahandle);

		// entry
		edb_eid eid;
		odb_spec_index_entry entryparams;
		entryparams.type = ODB_ELMOBJ;
		entryparams.memory = memorysettings;
		entryparams.structureid = structid;
		err = edba_entryopenc(edbahandle, &eid, EDBA_FCREATE | EDBA_FWRITE);
		if (err) {
			test_error("openc %d", err);
			return 1;
		}
		err = edba_entryset(edbahandle, entryparams);
		if (err) {
			test_error("entryset %d", err);
			return 1;
		}
		edba_entryclose(edbahandle);
		if(exclusiveeids && payload->useeid == 0) {
			payload->useeid = eid;
		}
		if(i + 1 == entries_to_create + 1) {
			// this is our last entry we create just to delete
			err = edba_entrydelete(edbahandle, eid);
			if(err) {
				test_error("entry delete %d", err);
				return 1;
			}
		}
	}


	return 0;
}

void *routine_insert(void *pl) {
	struct threadpayload *payload = pl;
	edba_handle_t *edbahandle = payload->edbahandle;
	int eid = payload->useeid;

	// insert a load of records
	test_log("thread %lx inserting into eid %d", &payload->pthread,
			 payload->useeid);
	for (int i = 0; i < records; i++) {
		timer t = timerstart();
		payload->oids[i] = ((edb_oid) eid) << 0x30;
		if ((err = edba_objectopenc(edbahandle, &payload->oids[i], EDBA_FWRITE |
		                                                           EDBA_FCREATE))) {
			test_error("creating %d", i);
			return 1;
		}
		int fixedc = edba_objectstruct(edbahandle)->fixedc;
		uint8_t *data = edba_objectfixed(edbahandle);

		for (int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t) j;
		}
		edba_objectclose(edbahandle);
		atomic_fetch_add(&time_individual_insert, timerend(t));
	}
}
void *routine_randread(void *pl) {
	struct threadpayload *payload = pl;
	edba_handle_t *edbahandle = payload->edbahandle;
	int eid = payload->useeid;

	// randomly read through the records and make sure their as expected.
	for(int i = 0; i < records; i++) {
		timer t = timerstart();
		int flags = 0;
		if(random_write) {
			flags = EDBA_FWRITE;
		}
		if((err = edba_objectopen(edbahandle, payload->oids_scrambled[i], flags))) {
			test_error("reading %d", i);
		}

		// verify data
		int fixedc = edba_objectstruct(edbahandle)->fixedc;
		uint8_t *data = edba_objectfixed(edbahandle);
		for (int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			if (data[j] != (uint8_t) j) {
				test_error("invalid value");
				return 1;
			}
		}
		edba_objectclose(edbahandle);
		atomic_fetch_add(&time_individual_random_read, timerend(t));
	}
	return 0;
}


uint64_t totalmysqlinsert = 0;
uint64_t totalmysqlupdate = 0;

void newdel(odbtelem_data d) {
	newdeletedpages++;
};
edbpcache_t *globalcache;

edbd_t globaledbd_file;

int main(int argc, const char **argv) {
	srand(47237427);

	// create an empty file
	test_mkdir();
	test_mkfile(argv[0]);
	odb_createparams createparams = odb_createparams_defaults;
	createparams.page_multiplier = page_multiplier;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return 1;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR | O_DIRECT);
	if (fd == -1) {
		test_error("bad fd");
		return 1;
	}
	// edbd
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&globaledbd_file, fd, config);
	if (err) {
		test_error("edbd_open failed");
		return 1;
	}
	// edbp
	err = edbp_cache_init(&globaledbd_file, &globalcache);
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
	if ((err = edba_host_init(&edbahost, globalcache, &globaledbd_file))) {
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
		if(!exclusiveeids) {
			payloads[i].useeid = 4 + (rand() % (extrathreads + 1) / 2);
		} else {
			payloads[i].useeid = 0; // set in createentries
		}
		payloads[i].fixedc = 20 + (rand() % (100));
		payloads[i].oids = malloc(sizeof(edb_oid) * records);
		payloads[i].oids_scrambled = malloc(sizeof(edb_oid) * records);
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

	// inserts
	test_log("inserting a total of %ld rows asyc...", records*(extrathreads+1));
	timer t = timerstart();
	for(int i = 0; i < extrathreads; i++) {
		pthread_create(&payloads[i].pthread, 0, routine_insert, &payloads[i]);
	}
	// main thread
	routine_insert(&payloads[extrathreads]);
	// join threads
	test_log("joining threads...");
	for(int i = 0; i < extrathreads; i++) {
		pthread_join(payloads[i].pthread, 0);
	}
	time_total_insert = timerend(t);
	if(test_waserror) {
		return 1;
	}

	// random reads
	// scramble the oids
	for(int i = 0; i < extrathreads+1; i++) {
		if(!colliding_oids) {
			scramble(payloads[i].oids, payloads[i].oids_scrambled, records);
		} else {
			if(i % 2 && (rand() % 2)) {
				scramble(payloads[i-1].oids,
						 payloads[i].oids_scrambled, records);
			} else {
				scramble(payloads[i].oids, payloads[i].oids_scrambled, records);
			}
		}
	}
	test_log("random-reading a total of %ld rows asyc...",
			 records*(extrathreads+1));
	t = timerstart();
	for(int i = 0; i < extrathreads; i++) {
		pthread_create(&payloads[i].pthread, 0, routine_randread, &payloads[i]);
	}
	// main thread
	routine_randread(&payloads[extrathreads]);
	// join threads
	test_log("joining threads...");
	for(int i = 0; i < extrathreads; i++) {
		pthread_join(payloads[i].pthread, 0);
	}
	time_total_random_read = timerend(t);
	if(test_waserror) {
		return 1;
	}

	test_log("decoming threads...");
	for(int i = 0; i < extrathreads+1; i++) {
		free(payloads[i].oids);
		free(payloads[i].oids_scrambled);
	}

	test_log("closing db...");
	for(int i = 0;i < extrathreads+1;i++) {
		edba_handle_decom(payloads[i].edbahandle);
	}
	edba_host_free(edbahost);
	edbp_cache_free(globalcache);
	edbd_close(&globaledbd_file);
	close(fd);


	{
		int total_records = records * (extrathreads+1);
				printf("oidadb total time inserting %d rows: %fs\n", total_records,
				       timetoseconds
						       (time_individual_insert));
		printf("oidadb total time key-updating %d rows: %fs\n", total_records,
		       timetoseconds
				       (time_individual_random_read));
		printf("oidadb individual time-per-insert: %fns/record\n",
		       (double) time_individual_insert / (double) total_records);
		printf("oidadb individual time-per-random-read: %fns/record\n",
		       (double) time_individual_random_read / (double) total_records);

		printf("oidadb total time-per-insert: %fns/record\n",
		       (double) time_total_insert / (double) total_records);
		printf("oidadb total time-per-random-read: %fns/record\n\n\n",
		       (double) time_total_random_read / (double) total_records);


		printf("inserts per second: %.2lf\n", (double)total_records /
		                                    (double)timetoseconds(time_total_insert));
		printf("random selects per second: %.2lf\n", (double)total_records /
		                                         (double)timetoseconds(time_total_random_read));
	}
}