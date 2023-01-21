#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>

#include "file.h"
#include "glplotter/glplotter.h"
#include "ents/background.h"
#include "ents/debug.h"


#include <GL/gl.h>
#include <signal.h>

int main(void)
{
	int err;

	// file
	if(file_init("../c.src/tests/stupididea/test")) {
		//window_close();
		return 1;
	}
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
	ent_debug_new(&db);

	// enter render cycle.
	err = glplotter_serve();

	// render cycle died for whatever reason.

	// close out everything.
	glplotter_close();
	file_close();
    return err;
}