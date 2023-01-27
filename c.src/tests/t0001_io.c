
#include <stdio.h>
#include "teststuff.h"
#include "../include/oidadb.h"

void t0001() {
	edb_hostconfig_t hostops = {0};

	err = odb_host(test_filenmae, hostops);
	if(err) {
		test_error("failed to host");
		return;
	}

}

int main(int argc, const char **argv) {
	test_mkdir();
	test_mkfile(argv[0]);
	t0001();
	return test_waserror;
}