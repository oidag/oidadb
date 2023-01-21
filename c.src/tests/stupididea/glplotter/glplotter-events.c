#include <malloc.h>
#include <strings.h>
#include "glp_u.h"

typedef struct eventbind {
	graphic_t *g;
	glp_cb_events cb;
} eventbind;
// 2d array.
// x-axis: mirrors the glp_eventtype_t (will always be _DAF_END_ in length.
// y-axis: the list of graphics subscribed to that found on the x-axis.
//
// eventbindingc/eventbindingq are the counts and capacities of the rows.
static eventbind  *eventbindingv[_DAF_END_];
static int          eventbindingc[_DAF_END_];
static int          eventbindingq[_DAF_END_];
#define EVENTBINDING_INC 8

// this will make sure data.type is set to type.
static void invokeevents(glp_eventtype_t type, eventdata_t data) {
	data.type = type;
	int kills = 0;
	for(int i = 0; i < eventbindingc[type]; i++) {
		if(eventbindingv[type][i].cb == 0) {
			kills++;
			continue;
		}
		eventbindingv[type][i].cb(eventbindingv[type][i].g,
								  data);
	}
	for(unsigned int i = 0; kills > 0; i++) {
		if(eventbindingv[type][i].cb != 0) {
			continue;
		}
		// i is on an index of a graphic that needs to be killed.
		for (unsigned int j = i + 1; j < eventbindingc[type]; j++) {
			eventbindingv[type][j - 1] = eventbindingv[type][j];
		}
		kills--;
		eventbindingc[type]--;
	}
}

static void cursor_position_callback(GLFWwindow* _, double xpos, double ypos) {
	eventdata_t data;
	data.pos.x = (int)xpos;
	data.pos.y = (int)ypos;
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

	// allocate the event stack buffers.
	for(int i = 0; i < _DAF_END_; i++) {
		eventbindingv[i] = 0;
		eventbindingc[i] = 0;
		eventbindingq[i] = 0;
	}
}

void close_events() {
	for(int i = 0; i < _DAF_END_; i++) {
		if(eventbindingv[i]) {
			free(eventbindingv[i]);
			eventbindingv[i] = 0;
		}
	}
}

static eventbind *addsub(graphic_t *g, glp_cb_events cb, glp_eventtype_t type) {
	if(eventbindingq[type] == eventbindingc[type]) {
		// not enough space... add more.
		eventbindingq[type] += EVENTBINDING_INC;
		eventbindingv[type] = realloc(eventbindingv[type],
									  eventbindingq[type] * sizeof(eventbind));
	}
	eventbind *ret = &eventbindingv[type][eventbindingc[type]];
	ret->g = g;
	ret->cb = cb;
	eventbindingc[type]++;
	return ret;
}

static void rmsub(graphic_t *g, glp_eventtype_t type) {
	g->events[type]->cb = 0;
}


void glp_events(graphic_t *g, glp_eventtype_t eventid, glp_cb_events cb) {
	if(eventid == DAF_ALL) {
		for(glp_eventtype_t t = 0; t < _DAF_END_; t++) {
			glp_events(g, t, cb);
		}
		return;
	}
	if(g->events[eventid]) {
		// this graphic is already subscribed to this event.
		if(cb == 0) {
			rmsub(g, eventid);
			g->events[eventid] = 0;
		} else {
			g->events[eventid]->cb = cb;
		}
	} else {
		if(cb == 0) {
			// doesn't have this event and also isn't try to set it.
			// so just return
			return;
		}
		// new sub. add it to the pool
		g->events[eventid] = addsub(g, cb, eventid);
	}
}