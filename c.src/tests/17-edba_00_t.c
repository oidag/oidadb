#include "../edbd.h"
#include "../edba.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"

#include <mariadb/mysql.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

int newdeletedpages = 0;

const int cachesize = 256;
const int records   = 100000;


uint64_t totalmysqlinsert = 0;
uint64_t totalmysqlupdate = 0;


uint64_t totaltimeinsert = 0
		, totaltimereadupdate = 0
, totaltimerandomdelete = 0;

void mysql();

void newdel(odbtelem_data d) {
	newdeletedpages++;
};
edbpcache_t *globalcache;

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

	// edba
	edba_host_t *edbahost;
	if ((err = edba_host_init(&edbahost, globalcache, &dfile))) {
		test_error("host");
		return 1;
	}
	edba_handle_t *edbahandle;
	if ((err = edba_handle_init(edbahost, 69, &edbahandle))) {
		test_error("handle");
		return 1;
	}


	const int fixedc = 100;

	// structure create
	edb_sid structid;
	{
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
	}


	// entry create
	edb_eid eid;
	{
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

	// insert a load of records
	test_log("inserting %ld rows...", records);
	edb_oid *oids = malloc(sizeof(edb_oid) * records);
	for (int i = 0; i < records; i++) {
		timer t = timerstart();
		oids[i] = ((edb_oid) eid) << 0x30;
		if ((err = edba_objectopenc(edbahandle, &oids[i], EDBA_FWRITE |
		                                                  EDBA_FCREATE))) {
			test_error("creating %d", i);
		}
		uint8_t *data = edba_objectfixed(edbahandle);

		for (int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t) j;
		}
		edba_objectclose(edbahandle);
		totaltimeinsert += timerend(t);
	}


	// to test persistancy: we close and reopen the database.
	{
		edba_handle_decom(edbahandle);
		edba_host_free(edbahost);
		edbp_cache_free(globalcache);
		edbd_close(&dfile);
		close(fd);
		// reopen.
		fd = open(test_filenmae, O_RDWR);
		if (fd == -1) {
			test_error("bad fd");
			return 1;
		}
		err = edbd_open(&dfile, fd, config);
		if (err) {
			test_error("edbd_open failed");
			return 1;
		}
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
		if ((err = edba_host_init(&edbahost, globalcache, &dfile))) {
			test_error("host");
			return 1;
		}
		if ((err = edba_handle_init(edbahost, 69, &edbahandle))) {
			test_error("handle");
			return 1;
		}
	}

	// read through the records and make sure their as expected.
	test_log("reading %ld rows...", records);
	for(int i = 0; i < records; i++) {

		timer t = timerstart();
		if((err = edba_objectopen(edbahandle, oids[i], EDBA_FWRITE))) {
			test_error("reading %d", i);
		}
		uint8_t *data = edba_objectfixed(edbahandle);
		for(int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			if(data[j] != (uint8_t)j) {
				test_error("invalid value");
				return 1;
			}
		}
		totaltimereadupdate += timerend(t);
		// make sure struct works.
		const odb_spec_struct_struct *strc = edba_objectstruct(edbahandle);
		if(strc->fixedc != 100) {
			test_error("failed to get structure");
			return 1;
		}
		if(edba_objectdeleted(edbahandle)) {
			test_error("non-deleted object is deleted");
			return 1;
		}
		// also if their oid is % 11 then give them a flag. We'll test this
		// below.
		if(oids[i] % 11 == 0) {
			edba_objectlocks_set(edbahandle, EDB_FUSRLWR);
		}
		edba_objectclose(edbahandle);
	}

	// delete the records in random order
	edb_oid *oids_random = malloc(sizeof(edb_oid ) * records);
	scramble(oids, oids_random, records);
	test_log("deleting %ld rows...", records);
	for(int i = 0; i < records; i++) {
		/*test_log("deleting rowid %lx (p%d)...", oids_random[i],
		         (oids[i]&0x0000FFFFFFFFFFFF)/81);*/
		timer t = timerstart();
		if((err = edba_objectopen(edbahandle, oids_random[i], EDBA_FWRITE))) {
			test_error("open-delete %d", i);
			return 1;
		}
		if((err = edba_objectdelete(edbahandle))) {
			test_error("delete");
			return 1;
		}
		if(!edba_objectdeleted(edbahandle)) {
			test_error("deleted onbject not deleted");
			return 1;
		}
		edba_objectclose(edbahandle);
		totaltimerandomdelete += timerend(t);
	}

	struct test {
		odb_spec_object_flags flags;
		uint16_t nextinlist;
		char data[94];
	};
	struct test test;

	// now recreate using half using autoid, and the other half non id.
	// non-auto
	test_log("manual-id creating %ld rows...", records/2);
	for(int i = 0; i < records/2; i++) {
		// We do NOT use EDBA_FCREATE here because we know for a fact we have
		// space sense we've previously made room.
		if((err = edba_objectopen(edbahandle, oids[i], EDBA_FWRITE))) {
			test_error("open-post-delete %d", i);
			return 1;
		}
		// we know that everything in oids[] is deleted
		if(!edba_objectdeleted(edbahandle)) {
			test_error("opening a record not marked as deleted that should "
					   "have been deleted");
			return 1;
		}
		/*test_log("undeleteding rowid %lx (p%d row %d)...", oids[i],
		         (oids[i]&0x0000FFFFFFFFFFFF)/81,
		         (oids[i]&0x0000FFFFFFFFFFFF) % 81);*/
		if((err = edba_objectundelete(edbahandle))) {
			test_error("undelete");
		}
		uint8_t *data = edba_objectfixed(edbahandle);
		for(int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t)j;
		}
		// test the lock-set condition we set up during the reading phase.
		if(oids[i] % 11 == 0) {
			odb_usrlk locks = edba_objectlocks_get(edbahandle);
			if(locks != EDB_FUSRLWR) {
				test_error("locks failed to install on oid with mod 11 "
						   "condition");
				return 1;
			}
		}
		edba_objectclose(edbahandle);
	}

	// auto-id
	test_log("auto-id creating %ld rows by reusing pages...",
			 records-records/2);
	for(int i = records/2; i < records; i++) {
		// We do NOT use EDBA_FCREATE here because we know for a fact we have
		// space sense we've previously made room.
		if((err = edba_objectopenc(edbahandle, &oids[i], EDBA_FWRITE))) {
			test_error("creating-post-delete %d", i);
			return 1;
		}
		uint8_t *data = edba_objectfixed(edbahandle);
		for(int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t)j;
		}
		edba_objectclose(edbahandle);
	}

	free(oids_random);
	free(oids);
	edba_handle_decom(edbahandle);
	edba_host_free(edbahost);
	edbp_cache_free(globalcache);
	edbd_close(&dfile);
	close(fd);

	printf("oidadb total time inserting %d rows: %fs\n", records, timetoseconds
	(totaltimeinsert));
	printf("oidadb total time key-updating %d rows: %fs\n", records,
		   timetoseconds
			(totaltimereadupdate));
	printf("oidadb time-per-insert: %fns\n"
		   , (double)totaltimeinsert/(double)records);
	printf("oidadb time-per-select: %fns\n"
		   , (double)totaltimereadupdate/(double)records);
	printf("oidadb time-per-select-random: %fns\n"
		   , (double)totaltimerandomdelete/(double)(records));

	// hmmm... lets do a mysql benchmark
	//mysql();
}

void mysql() {
	printf("executing mysql...\n");
	MYSQL *con = mysql_init(NULL);
	if (mysql_real_connect(con, "localhost", "admin", "admin",
	                       "test", 0, NULL, 0) == NULL)
	{
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return;
	}

	const char *q0 = "truncate table test";
	if(mysql_query(con, q0)) {
		test_error("mysql query");
		return;
	}

	MYSQL_STMT *stmt = mysql_stmt_init(con);
	MYSQL_STMT *stmt2 = mysql_stmt_init(con);
	if(!stmt || !stmt2) {
		test_error("mysql stmt init");
		return;
	}
	const char *q = "insert into test (bin) values (?)";
	const char *q2 = "update test set bin=? where id=?";
	if(mysql_stmt_prepare(stmt, q, strlen(q)) ||
	mysql_stmt_prepare(stmt2, q2, strlen(q2))) {
		test_error("mysql stmt prep");
		return;
	}
	MYSQL_BIND bind[2];
	bind[0].buffer_type = MYSQL_TYPE_BLOB;
	bind[0].is_null = 0;
	bind[0].buffer_length = 100;
	bind[0].length = 0;
	char buff[100];
	bind[0].buffer = buff;

	bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
	bind[1].is_null = 0;
	bind[1].length = 0;
	bind[1].buffer = buff;
	if(mysql_stmt_bind_param(stmt, bind)) {
		test_error("mysql stmt bind");
		return;
	}
	if(mysql_stmt_bind_param(stmt2, bind)) {
		test_error("mysql stmt bind");
		return;
	}

	long long *oids = malloc(sizeof(long long) * records);

	// inserts
	test_log("mysql inserts...");
	for(int i = 0; i < records; i++) {
		timer t = timerstart();
		for(int j = 0; j < 100; j++) {
			buff[j] = (char)j;
		}
		if(mysql_stmt_execute(stmt)) {
			test_error("execute error");
		}
		oids[i] = mysql_stmt_insert_id(stmt);
		totalmysqlinsert += timerend(t);
	}

	// selects/updates
	test_log("mysql updates...");
	for(int i = 0; i < records; i++) {
		timer t = timerstart();

		bind[1].buffer = &oids[i];
		for(int j = 99; j >= 0; j--) {
			buff[j] = (char)(100-j);
		}
		if(mysql_stmt_execute(stmt2)) {
			test_error("execute error");
		}
		totalmysqlupdate += timerend(t);
	}
	free(oids);
	mysql_stmt_close(stmt);
	mysql_stmt_close(stmt2);
	mysql_close(con);


	printf("mysql total time inserting %d rows: %fs\n", records, timetoseconds
			(totalmysqlinsert));
	printf("mysql total time key-updating %d rows: %fs\n", records,
	       timetoseconds
			       (totalmysqlupdate));
	printf("mysql time-per-insert: %fns\n", (double)totalmysqlinsert/(double)
			records);
	printf("mysql time-per-update: %fns\n", (double)totalmysqlupdate/(double)
			records);
	//printf("MySQL client version: %s\n", mysql_get_client_info());
}