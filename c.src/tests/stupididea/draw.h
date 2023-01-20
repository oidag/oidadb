#ifndef draw_h_
#define draw_h_
// Do NOT include any glfw
// Do NOT include file.h

#include "window.h"
#include "text.h"
#include "primatives.h"


// default fonts
extern text_font monospace_font;


int draw_init();
void draw_close();

// drawaction is returned by a graphic's dwg_draw_func. It dictates how the
// renderer should treat the graphic in future frames.
//
// The engineering of this is to provide graphics means of not having to
// redraw unless they absolutely need to. Proper use of these actions
// can lead to some very efficient software.
//
// When graphics are added via draw_addgraphic, their drawaction will always
// be set to da_invalidated implicitly for it's first frame, thus always invoking
// their dwg_draw_func for their first frame.
//
// See DA_* constants below.
typedef unsigned int drawaction;

// _da_none is invalid. Do not use it.
#define _DA_BITMASK 0xF

// The graphic is set to sleep. It will not call
// dwg_draw_func with the exception that something was moved under
// the graphic's bounding box or was XOR'd with DAF (see next paragraph).
// Otherwise, the graphic's pixels are saved and are reused in future
// frames, thus keeping the graphic always visible without needing to
// call its dwg_draw_func.
//
// You xor da_sleep with any DAF_ON* constant so that the graphic's
// dwg_draw_func will be called only when such event(s) have been
// invoked in that frame.
//
#define DA_SLEEP 0x1

// The graphic's dwg_draw_func will be called next frame (but will not invoke
// another frame to be drawn on its own, use da_frame for that).
#define DA_INVALIDATED 0x2

// The graphic is completely removed from the renderer. It will never
// be seen again after this frame. It must be added again using
// draw_addgraphic.
#define DA_KILL 0x3

// Same as da_invalidate but will also cause the next frame to be
// invoked.
//
// Only return this if the graphic is animated.
#define DA_FRAME 0x4

// todo: document these
#define _DAF_BITMASK      0x1FFF0000
#define DAF_ONMOUSE_DOWN  0x10010000
#define DAF_ONMOUSE_UP    0x10020000
#define DAF_ONMOUSE_ENTER 0x10040000
#define DAF_ONMOUSE_LEAVE 0x10080000
// keys
#define DAF_ONKEYDOWN     0x11010000
#define DAF_ONKEYUP       0x11020000

typedef struct {

	// how this graphic should be treated next frame. See drawaction doc.
	drawaction action;

	// the pixel region this graphic has 'occupied' this frame.
	// The smaller the better, but never too small, otherwise pixels will
	// linger.
	recti_t bbox;

}drawres_t;

typedef struct cache_st {
	drawaction lastact;
	recti_t    lastviewport;


	float       *_lastimagev; // glReadPixels.
	unsigned int _lastimageq; // note this is q not c. The count is found in the lastdraawnregion

	unsigned int glbuffer;
	unsigned int glbufferc;

} cache_t;


typedef struct {
	// todo: what was pressed and mouse pos and shit.
} framedata_t;

typedef  drawaction (*dwg_draw_func)(void *self, const framedata_t *) ;

typedef struct graphic_st {
	cache_t cache;

	// also known as bbox.
	//
	// This viewport is in pixels relative to the rest of the window.
	// And gl matrixes are applied so that 0,0 starts at TOP LEFT of
	// the viewport and the coordnate with viewport.width,viewport.height
	// is the BOTTOM RIGHT of the viewport.
	//
	// Whatever the viewport is after a graphic's dwg_draw_func is where
	// it used in the /next/ draw frame.
	//
	// In a multithreaded environment: do NOT edit this outside of dwg_draw_func calls.
	recti_t viewport;

	// here you can execute gl functions.
	//
	// The region is clipped to whatever drawregion was before this call.
	// Meaning drawing at 0,0 will draw at the drawregion's x,y cords relative
	// to the entire window.
	dwg_draw_func draw;
} graphic_t;

typedef struct label_st {
	graphic_t parent;
	const char *label;
} label_t;


// an array of pointers to structures that have graphic_t
// as their first index.
// put this in .c
//graphic_t **graphics;

int draw_serve();

void draw_addgraphic(void *g);


// todo: componenets

#endif