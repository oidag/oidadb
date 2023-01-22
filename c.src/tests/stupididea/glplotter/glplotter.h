#ifndef draw_h_
#define draw_h_
// Do NOT include any glfw
// Do NOT include file.h

#include "primatives.h"

// THREADING:
//    glplotter_init and glplotter_close must be called on the same thread.
int glplotter_init();
void glplotter_close();

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
typedef enum {
	// The graphic is set to sleep. It will avoid calling
	// dwg_draw_func with a few exceptions.
	// Otherwise, the graphic's pixels are saved and are reused in future
	// frames, thus keeping the graphic always visible without needing to
	// call its dwg_draw_func.
	GLP_SLEEPER,

	// The graphic's dwg_draw_func will be called next frame (but will not invoke
	// another frame to be drawn on its own, use da_frame for that).
	GLP_INVALIDATED,

	// Same as GLP_INVALIDATED but will also cause the next frame to be
	// invoked.
	//
	// Only return this if the graphic is animated.
	GLP_ANIMATE,
} glp_drawaction;



typedef enum glp_eventtype_t {

	// eventdata_t.mouse will be used
	DAF_ONMOUSE_DOWN,
	DAF_ONMOUSE_UP,

	// eventdata_t.pos will be used
	DAF_ONMOUSE_MOVE,

	// eventdata_t.scroll will be used.
	DAF_ONSCROLL,

	// eventdata_t.keyboard will be used.
	DAF_ONKEYDOWN,
	DAF_ONKEYREPEAT,
	DAF_ONKEYUP,

	// eventdata_t will be ignored.
	// To get the (new) window size, see glplotter_size
	DAF_ONWINDOWSIZE,

	// Do not use.
	_DAF_END_,

	DAF_ALL = _DAF_END_,
} glp_eventtype_t;

#include "glplotter-buttons.h"

typedef struct {
	// For an explination on this union, see drawevents
	glp_eventtype_t type;
	union {
		vec2i pos;
		struct {
			int width;
			int height;
		} size;
		struct {
			int x;
			int y;
		} scroll;
		struct {
			glp_key_t key;
		} keyboard;
		struct {
			mousebuttons_t button;
			int mods; // todo: remove?
		} mouse;
	};
} eventdata_t;

typedef struct graphic_t graphic_t;



typedef void (*glp_cb_events)(graphic_t *g, eventdata_t);

typedef recti_t glp_viewport_t;

// In this callback you can use OpenGL functions to hearts content.
// You cannot draw anything in here.
// You can adjust view ports if you like but they won't take effect
// until the draw method is invoked again
typedef  void (*glp_cb_draw)(graphic_t *g);




// Opens the window and begins drawing.
//
//
//
// THREADING: Will abduct the calling thread. Any subseqent call
//            will return -2 and do nothing.
// glplotter_stopserver can be called from any thread.
//
// Return 0 if all went well and closed out.
// Returns -1 if it closed out unexpectedly.
int glplotter_serve();
void glplotter_stopserver();


// returns the x-y in pixels of the plotter size.
//
// THREADING: MT-safe
vec2i glplotter_size();

// Allocate space for a new graphic or destroy an existing one.
//
// glp_new* is the start of a graphics lifecycle and glp_destroy* is
// the end of that lifecycle. After glp_new you should see glp_user,
// then glp_draw.
//
// As you read through these functions, keep an eye on threading. You'll
// notice you must make use of the callbacks.
//
// For all graphics you can set an ondestroy callback so you can clean up
// any userdata if applicable with glp_ondestroy.
//
// ERRORS:
//   Note, I'm lazy. If there's an error it just shits it out in stderr.
//   This goes for all glp_* functions.
//
// THREADING:
//   all glp_new functions must be called on the same glp_init was called on.
//   glp_destroy must be called either on the same thread as glp_new or in a callback.
//graphic_t *glp_newp(graphic_t *parent); // later
graphic_t *glp_new();
void       glp_destroy(graphic_t *g);

// Move 'user data' from your space into the graphic handling space.
// This user data will serve as a 'cookie' for glp_cb_* functions
// such as glp_events and glp_draw.
//
// This function is technically optional. You must manage the memory of
// user on your own. glp_destroy will not do anything with userdata but
// instead calling the callback ondestroy if its not null
//
//
// THREADING
//   Must be called in same thread as graphic_t's creation via glp_new*.
void       glp_user(graphic_t *g, void *user, void(*ondestroy)(graphic_t*));
void       glp_name(graphic_t *g, const char *name);
void      *glp_userget(graphic_t *g); // returns what was put into glp_user

// glp_viewport sets the view port in pixels that the graphic will be set to
// draw in. glp_viewport is required to before calling glp_draw. glp_viewport
// is designed to set the viewport every frame if nessacary. GLP_SLEEPERS
// calling glp_viewport will be re-drawn.
//
// glp_draw sets the draw callback for this graphic. During the inside execution
// of glp_cb_draw, you can use OpenGL to draw your shit. To unbind, pass in null
// for a callback (drawaction will be ignored). You can call this function as many
// times as you like to change the callback and/or the drawaction.
//
// glp_invalidate sets the redraw flag so that if g is a sleeper it
// will be redrawn on the next frame (but will not invoke the frame).
//
// THREADING
//    both must be called in the same thread as glp_new*, or,
//                 inside any glb_cb_* callback.
//    glp_invalidate is thread-safe.
//
// SEE ALSO: glp_drawaction, glp_viewport_t.
void       glp_viewport(graphic_t *, glp_viewport_t);
glp_viewport_t  glp_viewportget(graphic_t *);
void       glp_draw(graphic_t *, glp_drawaction, glp_cb_draw);
void       glp_invalidate(graphic_t *);

// Bind an event to a callback. The call back will then be executed
// with the relevant event data as well as ther user data.
//
// To unbind from a callback, set glp_cb_events to null.
//
// To bind/unbind from all callbacks, set glp_eventtype_t to DAF_DESTROY
void       glp_events(graphic_t *g, glp_eventtype_t , glp_cb_events);

#endif