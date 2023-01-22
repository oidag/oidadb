#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>

#include "dbfile/dbfile.h"
#include "glplotter/glplotter.h"
#include "ents/ents.h"


#include <GL/gl.h>
#include <signal.h>
#include <GLFW/glfw3.h>

static void onfileload() {
	ent_pager_new();
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
	if(glplotter_init()) {
		return 1;
	}
	// **defer: draw_close

	// add the background
	ent_background_t bg;
	ent_background_new(&bg);
	ent_debug_t db;
	ent_opener_new(onfileload);
	ent_debug_new(&db);

	// enter render cycle.
	err = glplotter_serve();

	// render cycle died for whatever reason.

	// close out everything.
	glplotter_close();
	//file_close();
    return err;
}