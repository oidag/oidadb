#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <malloc.h>
#include <strings.h>

#include "glp_u.h"
#include "glplotter.h"
#include "error.h"

int window_width,window_height;
float window_mousex = 0, window_mousey = 0;

static GLFWwindow *window;

framedata_t framedata;
static void cursor_position_callback(GLFWwindow* _, double xpos, double ypos) {
	framedata.cx = (int)xpos;
	framedata.cy = (int)ypos;
	framedata.events |= DAF_ONMOUSE_MOVE;
}

void mouse_button_callback(GLFWwindow* _, int button, int action, int mods) {
	switch (action) {
		case GLFW_PRESS:
			framedata.events |= DAF_ONMOUSE_DOWN;
		case GLFW_RELEASE:
			framedata.events |= DAF_ONMOUSE_UP;
		default:
			return;
	}
	framedata.mousebuttons = button+1; // note: our mousedata is same defs as theirs + 1
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	//todo
}


static void window_size(GLFWwindow* _, int w, int h) {
	window_width  = w;
	window_height = h;
	framedata.wwidth = w;
	framedata.wheight = h;
	framedata.events |= DAF_ONWINDOWSIZE;
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	//todo
}

int window_init(const char *name, float initwidth, float initheight) {
	if (!glfwInit()) {
		error("glfwInit");
		return -1;
	}
	window_width = initwidth;
	window_height = initheight;
	window = glfwCreateWindow(window_width, window_height, name, 0, 0);
	if (!window)
	{
		error("create window");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// call backs (all static functions)
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetWindowSizeCallback(window, window_size);
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);

	return 0;
}

int draw_init() {
	int err;

	err = window_init("the tool", 1200, 800);
	if(err) {
		return err;
	}

	// set fonts
	err = text_addfont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12, &monospace_font);
	if(err) {
		error("failed to open debugfont: %d", err);
		return 1;
	}

	// set the draw callback
	window_ondraw(draw);

	// buffers
	graphicbufc = 0;
	graphicbufq = GRAPHICBUF_BLOCKC;
	graphicbufv = malloc(sizeof(void *) * GRAPHICBUF_BLOCKC);

	return 0;
}



int draw_serve() {
	int err = 0;
	while (!glfwWindowShouldClose(window))
	{
		int ret = draw(&framedata);
		glfwSwapBuffers(window);
		framedata.events = 0; // refersh events
		switch(ret) {
			case -1:
				err = -1;
			case -2:
				glfwSetWindowShouldClose(window, GLFW_TRUE);
				glfwPollEvents();
				break;
			case 0:
				glfwWaitEvents();
				break;
			case 1:
				glfwPollEvents();
				break;
		}
	}
	return err;
}

void draw_close() {
	free(graphicbufv);
	glfwTerminate();
}

void draw_addgraphic(void *vg) {

	// normalizgin
	graphic_t *g = (graphic_t *)vg;
	bzero(&g->cache, sizeof(g->cache));

	if(graphicbufc == graphicbufq) {
		// resizing needed
		graphicbufq += GRAPHICBUF_BLOCKC;
		graphicbufv = realloc(graphicbufv, graphicbufq * sizeof(void*));
	}
	g->cache.lastact = DA_INVALIDATED;
	graphicbufv[graphicbufc] = g;
	graphicbufc++;
}