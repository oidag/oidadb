#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>

#include "file.h"
#include "draw.h"


#include <GL/gl.h>

#include "draw-dwg.h"

int main(void)
{
	int err;

	// file
	if(file_init("../c.src/tests/stupididea/test")) {
		window_close();
		return 1;
	}
	// **defer: file_close

	// drawer
	if(draw_init()) {
		return 1;
	}
	// **defer: draw_close

	// add the background
	dwg_background_t b = dwg_background_new();
	draw_addgraphic(&b);

	// add the debugger
	srand(getpid());
	dwg_debug_t d1 = dwg_debug_new();
	dwg_debug_t d2 = dwg_debug_new();
	dwg_debug_t d3 = dwg_debug_new();
	draw_addgraphic(&d1);
	draw_addgraphic(&d2);
	draw_addgraphic(&d3);

	// enter render cycle.
	err = draw_serve();

	// render cycle died for whatever reason.

	// close out everything.
	draw_close();
	file_close();
    return err;
}