
#include <stdio.h>
#include "teststuff.h"
#include "../include/oidadb.h"

void t0001() {
	odb_createparams c = {0};

	err = odb_create(test_filenmae, odb_createparams_defaults);
	if(err) {
		test_error("failed to create file");
		return;
	}

}

int main(int argc, const char **argv) {
	test_mkdir();
	test_mkfile(argv[0]);
	t0001();
	return test_waserror;
}