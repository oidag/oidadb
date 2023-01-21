#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <malloc.h>
#include <strings.h>
#include <string.h>

#include "glp_u.h"
#include "glplotter.h"

graphic_t   *graphicbufv;
unsigned int graphicbufc; // count (length)
unsigned int graphicbufq;

GLFWwindow *window;


int window_init(const char *name, float initwidth, float initheight) {
	if (!glfwInit()) {
		error("glfwInit");
		return -1;
	}
	window = glfwCreateWindow(initwidth,
							  initheight,
							  name,
							  0,
							  0);
	if (!window)
	{
		error("create window");
		glfwTerminate();
		return -1;
	}
	// todo: remove the window pos line ... I'm using this to test easier.
	glfwSetWindowPos(window, 2560*2+100, 100);
	glfwMakeContextCurrent(window);

	return 0;
}

int glplotter_init() {
	int err;

	err = window_init("the tool", 1200, 800);
	if(err) {
		return err;
	}

	init_events();

	// buffers
	graphicbufc = 0;
	graphicbufq = GRAPHICBUF_BLOCKC;
	graphicbufv = malloc(sizeof(graphic_t) * GRAPHICBUF_BLOCKC);

	return 0;
}

void glplotter_stopserver() {
	glfwSetWindowShouldClose(window, GLFW_TRUE);
	glfwPostEmptyEvent();
}

int glplotter_serve() {
	int err = 0;
	int exit = 0;
	while (!glfwWindowShouldClose(window))
	{
		int ret = draw();
		glfwSwapBuffers(window);
		switch(ret) {
			case -1:
				err = -1;
				exit = -1;
				// fallthrough
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
	return exit;
}

vec2i glplotter_size() {
	vec2i ret;
	glfwGetWindowSize(window,&ret.width, &ret.height);
	return ret;
}

void glplotter_close() {
	for(int i = 0; i < graphicbufc; i++) {
		glp_destroy(&graphicbufv[i]);
	}
	close_events();
	free(graphicbufv);
	graphicbufv = 0;
	glfwDestroyWindow(window);
	glfwTerminate();
}

graphic_t *glp_new() {
	// normalizgin

	if(graphicbufc == graphicbufq) {
		// resizing needed
		graphicbufq += GRAPHICBUF_BLOCKC;
		graphicbufv = realloc(graphicbufv, graphicbufq * sizeof(graphic_t));
	}

	graphic_t *ret = &graphicbufv[graphicbufc];
	bzero(ret, sizeof(graphic_t));
	graphicbufc++;
	return ret;
}

void glp_user(graphic_t *g, void *user, void(*ondestroy)(graphic_t*)) {
	g->user = user;
	g->ondestroy = ondestroy;
}
void *glp_userget(graphic_t *g) {
	return g->user;
}

void glp_destroy(graphic_t *g) {
	// i is on an index of a graphic that needs to be killed.
	if(g->ondestroy) g->ondestroy(g);
	glp_draw(g, 0, 0);
	glp_events(g, DAF_ALL, 0);
}