#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>

#include "odbm.h"


#include <GL/gl.h>
#include <signal.h>
#include <GLFW/glfw3.h>

static void onfileload() {

	//element_host_start();
}

int main(void)
{
	int err;

	// file
	/*mkdir("testfile", 0777);
	if(file_init("testfile/dbtest.oidadb")) {
		return 1;
	}*/
	// **defer: file_close

	// drawer
	if(gman_init()) {
		return 1;
	}
	// **defer: draw_close

	// enter render cycle.
	err = gman_serve();

	// render cycle died for whatever reason.

	// close out everything.
	gman_close();
	//file_close();
    return err;
}