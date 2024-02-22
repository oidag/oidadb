#include "teststuff.h"

odb_err err;
int test_waserror = 0;
char test_filenmae[100];

int main(int argc, const char **argv) {
	const char *arg0 = argv[0];
	// create an empty file
	printf("pid: %d\n", getpid());
	test_mkfile(argv[0]);
	errno = 0;
	test_main();
	if(test_waserror) {
		test_error("\n%s: test failed", arg0);
		return 1;
	} else {
		test_log("\n%s: test passed", arg0);
		return 0;
	}
}