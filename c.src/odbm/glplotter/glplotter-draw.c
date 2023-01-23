#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <malloc.h>
#include <strings.h>
#include <GL/glext.h>
#include <GLES3/gl3.h>

#include "glplotter.h"
#include "glp_u.h"
#include "primatives.h"




static void cachepixels_draw(graphic_t *g) {

	recti_t viewport = g->viewport;

	glWindowPos2i(viewport.x,
	              viewport.y);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g->cache.glbuffer);
	glDrawPixels(viewport.width,
	             viewport.heigth,
	             GL_RGB,
	             GL_FLOAT,
	             0);
}
static void decachepixels(graphic_t *g) {
	if(!g->cache.glbuffer) {
		return;
	}
	glDeleteBuffers(1, &g->cache.glbuffer);
	g->cache.glbufferc = 0;
	g->cache.glbuffer = 0;
}

// takes everything inside of g->viewport and caches it
// for quick redraw using cachepixels_draw.
static int cachepixels(graphic_t *g) {
	recti_t viewport = g->viewport;

	// make sure our buffer for this graphic is large enough to store
	// the entire viewport.
	unsigned int memneeded = viewport.width
			* viewport.heigth
			* 3
			* sizeof(float);
	if(!g->cache.glbuffer) {
		glGenBuffers(1,&g->cache.glbuffer);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, g->cache.glbuffer);
	if(g->cache.glbufferc < memneeded) {
		glBufferData(GL_PIXEL_PACK_BUFFER,
		             memneeded,
					 0,
					 GL_DYNAMIC_DRAW);
		g->cache.glbufferc = memneeded;
	}
	glReadPixels(viewport.x,
	             viewport.y, // glReadPixels is weird with the y.
	             viewport.width,
	             viewport.heigth,
	             GL_RGB,
	             GL_FLOAT,
	             0);
}

void       glp_viewport(graphic_t *g, glp_viewport_t v) {
	if(!v.width || !v.heigth) {
		error("warning: viewport deminsion set to 0");
	}
	g->viewport = v;
	glp_invalidate(g);
}
glp_viewport_t  glp_viewportget(graphic_t *g) {
	return g->viewport;
}
void glp_invalidate(graphic_t *g) {
	glp_invalidatef(g, 0);
}
void glp_invalidatef(graphic_t *g, int invokedraw) {
	g->flags |= GLP_GFLAGS_FORCEDRAW;
	if(invokedraw) {
		glfwPostEmptyEvent();
	}
}

unsigned int glplotter_frameid = 1;
float glplotter_frameidf = 1;
double glplotter_frameidd = 1;

void glp_draw(graphic_t *g, glp_drawaction da, glp_cb_draw d) {
	if(d == 0) {
		// decache any image data that it may have.
		decachepixels(g);
		// setting draw to null will prevent it from being drawn
		// and eventually pushed out from the array on the next draw.
		g->draw = 0;
		return;
	}
	g->flags |= GLP_GFLAGS_FORCEDRAW;
	g->drawact = da;
	g->draw = d;
}
int draw() {

	glplotter_frameid++;
	if(glplotter_frameid == 39916801) {
		glplotter_frameid = 1;
	}
	glplotter_frameidf = (float)glplotter_frameid;
	glplotter_frameidd = (double)glplotter_frameid;

	unsigned int err;
	int ret = 0;

	// for each graphic, we only need to redraw it if its bounding box is either
	// invalidated, or is sitting on top of a bounding box that is invalided.
	//
	// We do not need to redraw a graphic if the contents below it had not changed.
	//
	// Now you may ask, "well, what If I move a box over a circle, sure the circle doesn't
	// need to redraw then, but if I move the box somewhere else then the circle would
	// remain stained with the box on top of it?". What we do is just draw back the image
	// data the sleeper submitted back on the boundingbox of that box.
	//
	// If a graphic is redrawn, regardless if its marking itself as sleeping, the bounding
	// box for the newly drawn graphic will be marked as invalidated for the
	// items potential above it for this frame. This is because if a graphic is under another,
	// and the sub graphic is redrawn, it would redraw itself ontop of the super
	// ficial graphics.
	for(unsigned int i = 0; i < graphicbufc; i++) {

		// skip empty graphics.
		if(graphicbufv[i].draw == 0 || !graphicbufv[i].alive) {
			continue;
		}

		// easy pointers
		graphic_t *g = &graphicbufv[i];
		glp_drawaction drawact = g->drawact;

		// the last viewport that was used last frame.
		recti_t lastbbox = g->cache.lastviewport;

		// This switch statement will either end in break or continue:
		//   - continue: don't call dwg_draw_func.
		//   - break: call dwg_draw_func.
		// later: pre-draw we should only be handling DA_SLEEP.
		switch (drawact) {
			case GLP_ANIMATE:
				ret = 1;
				// fallthrough
			case GLP_INVALIDATED:
				break;
			default:
				error("graphic has unknown draw action, killing");
				glp_draw(g,0,0);
				continue;
			case GLP_SLEEPER:
				if(g->flags & GLP_GFLAGS_FORCEDRAW) {
					// invoke a force-redraw. We can clear this bit now sense
					// we're breaking.
					g->flags &= ~GLP_GFLAGS_FORCEDRAW;
					break;
				}
				cachepixels_draw(g);
				continue;
		}

		// So they must be redrawn. Which means:
		// - they are going to change from what they were last frame.
		// - anything that was /under/ that graphic last frame is unaffected by this.
		// - anything /above/ that graphic last frame MUST have their dwg_draw_func invoked
		//   - because things above this graphic may have transperancy, or a large bounding box
		//     and if they were not to be redrawn, you could still see the old graphic's (outdated)
		//     pixels beneath it.
		// - anything /above/ the graphics *NEW* location will also need to be redrawn.

		// So lets perfrom the actual draw.
		// prepare the draw region
		glViewport(g->viewport.x,
		           g->viewport.y,
		           g->viewport.width,
		           g->viewport.heigth);
		glPushMatrix();
		glOrtho(0, g->viewport.width, 0, g->viewport.heigth, 1, -1);

		err = glGetError(); // clear error bit
		if(err) {
			error("error: pre draw() glGetError log_error");
		}
		g->draw(g);
		err = glGetError();
		if(err) {
			error("notice: draw() glGetError non-null");
		}
		glPopMatrix();
		vec2i windowsize = glplotter_size();
		glViewport(0,
		           0,
		           windowsize.width,
		           windowsize.height);

		// get the new viewport just incase it changed that frame.
		recti_t newview = g->viewport;
		glp_drawaction newact  = g->drawact;

		// Now we have their last location in lastbbox.
		// And we have their new location in newres->bbox

		// So lets force-draw all sleepers above our previous and new location
		// by changing their registered action to DA_INVALDATED
		for(unsigned int j = i+1; j < graphicbufc; j++) {
			if(graphicbufv[j].drawact != GLP_SLEEPER) {
				// the draw action here is no DA_SLEEP, which means it will be
				// drawn regardless. nothing to worry about.
				continue;
			}

			// okay this is a sleeper, is it above our new or last location?...
			recti_t sleeperbbox = graphicbufv[j].cache.lastviewport;
			if(rect_intersectsi(sleeperbbox, newview) ||
					rect_intersectsi(sleeperbbox, lastbbox)) {
				// ...it is. Remove its DA_SLEEP flag and add the
				// DA_INVALIDATED flag. Note that this will destroy
				// any DAF flags. But thats okay sense the draw method should
				// be setting those every draw.
				graphicbufv[j].flags |= GLP_GFLAGS_FORCEDRAW;
			}
		}

		// The only thing left to do is to check if this graphic is a sleeper that
		// needed to redraw. If it is we copy the pixels so we can redraw this
		// graphic without invoking its draw method.
		if(newact == GLP_SLEEPER) {
			cachepixels(g);
		}
		// as a last little step, we can free up any memory of cached pixels if
		// the graphic /was/ in sleep state but now wants to be something else.
		else if(g->cache.glbuffer) {
			decachepixels(g);
		}

		// Finally, update the lastres to match the latest draw for this graphic.
		g->cache.lastviewport = newview;
	}

#ifdef GLPLOTTER_DEBUGBOXES
	for(int i = 0; i < graphicbufc; i++)
	{
		if(!graphicbufv[i].alive || !graphicbufv[i].draw) continue;
		glViewport(graphicbufv[i].viewport.x,
		           graphicbufv[i].viewport.y,
		           graphicbufv[i].viewport.width,
		           graphicbufv[i].viewport.heigth);
		glLineWidth(2);
		glBegin(GL_LINE_LOOP);
		glColor3f(1, 0, 0);
		glVertex2f(-1, -1);
		glVertex2f(-1, 1);
		glVertex2f(1, 1);
		glVertex2f(1, -1);
		glEnd();
		vec2i windowsize = glplotter_size();
		glViewport(0,
		           0,
		           windowsize.width,
		           windowsize.height);
	}
#endif

	// return rather or not we should redraw.
	return ret;
}