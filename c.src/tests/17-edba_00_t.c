#include "../edbd.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
int newdeletedpages = 0;

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

	// edbd open the file
	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return 1;
	}


	edbd_close(&dfile);
	close(fd);
}