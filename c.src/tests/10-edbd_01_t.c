
#include "../edbd.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
int newdeletedpages = 0;
odb_err _pid_odbtelem_attach(pid_t hostpid);
void test_main() {
	struct odb_createparams createparams  =odb_createparams_defaults;
	createparams.structurepages = 32;
	createparams.indexpages = 32;
	createparams.page_multiplier = 2;
	err = odb_create(test_filenmae, createparams);
	if(err) {
		test_error("failed to create file");
		return;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if(fd == -1) {
		test_error("bad fd");
		return;
	}

	if(err) {
		test_error("odbtelem");
		return;
	}
	//odbtelem_bind(ODBTELEM_PAGES_NEWDEL, newdel);

	edbd_t dfile;
	edbd_config config = edbd_config_default;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return;
	}

	// testing configs
	const int pagec = 16;
	const int page_strait = 1;
	const int page2c = 4;
	const int page2_strait = 4;
	const int structsize = sizeof(odb_spec_struct_full_t);
	const int strksperpage =( (int)edbd_size(&dfile)-ODB_SPEC_HEADSIZE) / structsize;
	const int totalstks = strksperpage * createparams.structurepages;

	const int entsize = sizeof(odb_spec_index_entry);
	const int entsperpage =( (int)edbd_size(&dfile)-ODB_SPEC_HEADSIZE) / entsize;
	const int totalents = entsperpage * createparams.indexpages;

	int i;
	for(i = 0; i < totalstks + 1; i++) {
		const odb_spec_struct_full_t *stk;
		err = edbd_structf(&dfile, i, &stk);
		if(err) {
			if(err == ODB_EEOF) {
				break;
			}
			test_error("unknown error");
			return;
		}
	}
	if(!err || i != totalstks) {
		test_error("total structs counted not equal to theoretical / ODB_EEOF not returned correctly.");
		return;
	}

	for(i = 0; i < totalents + 1; i++) {
		odb_spec_index_entry *ent;
		err = edbd_index(&dfile, i, &ent);
		if(err) {
			if(err == ODB_EEOF) {
				break;
			}
			test_error("unknown error");
			return;
		}
	}
	if(!err || i != totalents) {
		test_error("total ents counted not equal to theoretical / ODB_EEOF not returned correctly.");
		return;
	}


	// close the file.
	edbd_close(&dfile);


	ret:
	return;
}