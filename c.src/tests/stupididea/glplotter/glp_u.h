#ifndef glp_u_h_
#define glp_u_h_

#include <GLFW/glfw3.h>
#include "glplotter.h"
#include "options.h"

extern GLFWwindow *window;

typedef struct cache_st {
	recti_t    lastviewport;

	unsigned int glbuffer; // will be 0 if no buffer.
	unsigned int glbufferc;

} cache_t;

typedef struct eventbind eventbind;

#define GLP_GFLAGS_FORCEDRAW 2

typedef struct graphic_t {
	cache_t cache;

	const char *name;

	// see GLP_GFLAGS*
	int flags;

	// user-defined graphic data.
	void *user;
	void(*ondestroy)(graphic_t*);

	// see -events.c
	// Will back-reference point to somewhere in the event buffer.
	glp_cb_events events[_DAF_END_];

	// perfered action set either because of a previous draw or event.
	glp_drawaction drawact;

	// also known as bbox.
	//
	// This viewport is in pixels relative to the rest of the window.
	// And gl matrixes are applied so that 0,0 starts at BOTTOM LEFT of
	// the viewport and the coordnate with viewport.width,viewport.height
	// is the TOP RIGHT of the viewport.
	//
	// Whatever the viewport is after a graphic's dwg_draw_func is where
	// it used in the /next/ draw frame.
	recti_t viewport;

	// here you can execute gl functions.
	//
	// The region is clipped to whatever drawregion was before this call.
	// Meaning drawing at 0,0 will draw at the drawregion's x,y cords relative
	// to the entire window.
	glp_cb_draw draw;
} graphic_t;


// glplotter-internal header. do not use outside of glplotter/*.c

// graphicbufv: array of pointers
extern graphic_t   *graphicbufv;
extern unsigned int graphicbufc; // count (length)
extern unsigned int graphicbufq; // quality in buffer
// graphic block sizes
#define GRAPHICBUF_BLOCKC 16

// returns -2: stop drawing all together and close the window, window_render will return 0.
// returns -1: same as -2 but will have window_redner return -1.
// returns 0: draw-sleep. Don't execute cb again unless on an event
// retunrs 1: draw-fire. Draw again.
//
// The call back will be executed whenever the hell it wants to. Its up to you
// to find out what exactly needs to be redrawn for each draw.
int draw();

static void error(const char *m) {
	fprintf(stderr, "%s\n", m);
}

// call after window has been set. Sends it over to gplotter-events.
void init_events();
void close_events();

#endif