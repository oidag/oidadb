#ifndef window_h_
#define window_h_


// anyone dealing with including window.h should also
// be exposed to gl functions
//#define GLFW_INCLUDE_GLEXT
//#include <GLFW/glfw3.h>
//#include <GL/gl.h>

// in pixels.
extern int window_width,window_height;
extern float window_mousex, window_mousey;

int  window_init(const char *name, float initwidth, float initheight);

// returns -2: stop drawing all together and close the window, window_render will return 0.
// returns -1: same as -2 but will have window_redner return -1.
// returns 0: draw-sleep. Don't execute cb again unless on an event
// retunrs 1: draw-fire. Draw again.
//
// The call back will be executed whenever the hell it wants to. Its up to you
// to find out what exactly needs to be redrawn for each draw.
void window_ondraw(int(* cb)());
int  window_render();
void window_close();

#endif