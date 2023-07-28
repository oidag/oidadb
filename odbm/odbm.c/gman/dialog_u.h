#ifndef dialog_u_h_
#define dialog_u_h_

#include "dialog.h"
#include "colors.h"
#include "gman.h"
#include "glplotter/glplotter.h"

#include <GL/gl.h>

typedef struct dialog_t dialog_t;
void dialog_start();

void dialog_settitle(const char *title);

// float will return the height of the over all form to allow for scrolling
void dialog_drawbody(void (*draw)(int width));


// forces redraw.
void dialog_invalidatef();

// call this everytime your height changes. This will allow the scroll mechanic
// to behave.
void dialog_setheight(float height);


#endif