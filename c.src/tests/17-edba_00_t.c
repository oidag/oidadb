#include "../edbd.h"
#include "../edba.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
int newdeletedpages = 0;

const int cachesize = 256;
const int records   = 40;

void newdel(odbtelem_data d) {
	newdeletedpages++;
};
edbpcache_t *globalcache;
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
	err = edbp_cache_init(&dfile, &globalcache);
	if(err) {
		test_error("edbp_cache_init");
		return 1;
	}
	err = edbp_cache_config(globalcache, EDBP_CONFIG_CACHESIZE, cachesize);
	if(err) {
		test_error("edbp_cache_config");
		return 1;
	}

	// edba
	edba_host_t *edbahost;
	if((err = edba_host_init(&edbahost, globalcache, &dfile))) {
		test_error("host");
		return 1;
	}
	edba_handle_t *edbahandle;
	if((err = edba_handle_init(edbahost, 69, &edbahandle))) {
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
		if(err) {
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
	edb_oid oids[records];
	for(int i = 0; i < records; i++) {
		oids[i] = ((edb_oid)eid) << 0x30;
		if((err = edba_objectopenc(edbahandle, &oids[i], EDBA_FWRITE |
		EDBA_FCREATE))) {
			test_error("creating %d", i);
		}
		uint8_t *data = edba_objectfixed(edbahandle);

		for(int j = 0; j < (fixedc - sizeof(odb_spec_object_flags)); j++) {
			data[j] = (uint8_t)j;
		}

		test_log("created object 0x%lx", oids[i]);
		edba_objectclose(edbahandle);
	}

	// todo: close the database here to test persistancy.

	// read through the records and make sure their as expected.
	for(int i = 0; i < records; i++) {

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
		// make sure struct works.
		const odb_spec_struct_struct *strc = edba_objectstruct(edbahandle);
		if(strc->fixedc != 100) {
			test_error("failed to get structure");
			return 1;
		}
		// delete it if its i % 11
		if(i % 11 == 0) {
			if((err = edba_objectdelete(edbahandle))) {
				test_error("delete");
				return 1;
			}
			if(!edba_objectdeleted(edbahandle)) {
				test_error("deleted object not deleted");
				return 1;
			}
		} else if(edba_objectdeleted(edbahandle)) {
			test_error("non-deleted object is deleted");
			return 1;
		}
		edba_objectclose(edbahandle);
	}


	edba_handle_decom(edbahandle);
	edba_host_free(edbahost);
	edbp_cache_free(globalcache);
	edbd_close(&dfile);
	close(fd);
}