#include <malloc.h>
#include <strings.h>
#include "glplotter_u.h"

typedef struct eventbind {
//	graphic_t *g;
	glp_cb_events cb;
} eventbind;
// 2d array.
// x-axis: mirrors the glp_eventtype_t (will always be _DAF_END_ in length.
// y-axis: the list of graphics subscribed to that found on the x-axis.
//
// eventbindingc/eventbindingq are the counts and capacities of the rows.
/*static eventbind  *eventbindingv[_DAF_END_];
static int          eventbindingc[_DAF_END_];
static int          eventbindingq[_DAF_END_];*/
#define EVENTBINDING_INC 8

// this will make sure data.type is set to type.
static void invokeevents(glp_eventtype_t type, eventdata_t data) {
	data.type = type;
	for(int i = 0; i < graphicbufc; i++) {
		graphic_t *g = &graphicbufv[i];
		if(!g->events[type] || !g->alive) {
			continue;
		}
		g->events[type](g, data);
	}
}

static void cursor_position_callback(GLFWwindow* w, double xpos, double ypos) {
	eventdata_t data;
	data.pos.x = (int)xpos;
	// invert y
	glfwGetWindowSize(w, 0, &data.pos.y);
	data.pos.y = (data.pos.y - (int)ypos);
	invokeevents(DAF_ONMOUSE_MOVE, data);
}

void mouse_button_callback(GLFWwindow* _, int button, int action, int mods) {
	eventdata_t data;
	data.mouse.button = button;
	switch (action) {
		case GLFW_PRESS:
			invokeevents(DAF_ONMOUSE_DOWN, data);
			return;
		case GLFW_RELEASE:
			invokeevents(DAF_ONMOUSE_UP, data);
			return;
		default:
			return;
	}
}

void key_callback(GLFWwindow* _, int key, int scancode, int action, int mods) {
	eventdata_t data;
	data.keyboard.key = key;
	switch (action) {
		default:
		case GLFW_PRESS:
			invokeevents(DAF_ONKEYDOWN, data);
		return;
		case GLFW_REPEAT:
			invokeevents(DAF_ONKEYREPEAT, data);
		return;
		case GLFW_RELEASE:
			invokeevents(DAF_ONKEYUP, data);
		return;
	}
}


static void window_size(GLFWwindow* _, int w, int h) {
	eventdata_t data;
	invokeevents(DAF_ONWINDOWSIZE, data);
}
void scroll_callback(GLFWwindow* _, double xoffset, double yoffset) {
	eventdata_t data;
	data.scroll.x = (int)xoffset;
	data.scroll.y = (int)yoffset;
	invokeevents(DAF_ONSCROLL, data);
}



void init_events() {

	// call backs (all static functions)
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetWindowSizeCallback(window, window_size);
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);

}

void close_events() {
}


void glp_events(graphic_t *g, glp_eventtype_t eventid, glp_cb_events cb) {
	if(eventid == DAF_ALL) {
		for(glp_eventtype_t t = 0; t < _DAF_END_; t++) {
			glp_events(g, t, cb);
		}
		return;
	}
	g->events[eventid] = cb;
}