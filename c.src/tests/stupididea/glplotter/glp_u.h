#ifndef glp_u_h_
#define glp_u_h_
// glplotter-internal header. do not use outside of glplotter/*.c

// in pixels.
extern int window_width,window_height;
extern float window_mousex, window_mousey;

// graphicbufv: array of pointers
graphic_t  **graphicbufv;
unsigned int graphicbufc; // count (length)
unsigned int graphicbufq; // quality in buffer
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

#endif