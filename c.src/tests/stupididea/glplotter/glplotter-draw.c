#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <malloc.h>
#include <strings.h>
#include <GL/glext.h>
#include <GLES3/gl3.h>

#include "glplotter.h"
#include "glp_u.h"
#include "error.h"
#include "primatives.h"




static void cachepixels_draw(graphic_t *g) {

	unsigned int err = glGetError();
	recti_t viewport = g->viewport;

	glWindowPos2i(viewport.x,
	              viewport.y);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g->cache.glbuffer);
	glDrawPixels(viewport.width,
	             viewport.heigth,
	             GL_RGB,
	             GL_UNSIGNED_BYTE,
	             0);
}
static void decachepixels(graphic_t *g) {
	if(!g->cache.glbuffer) {
		error("double-decache");
		return;
	}
	glDeleteBuffers(1, &g->cache.glbuffer);
	g->cache.glbuffer = 0;
}

// takes everything inside of g->viewport and caches it
// for quick redraw using cachepixels_draw.
static int cachepixels(graphic_t *g) {

	recti_t viewport = g->viewport;

	// make sure our buffer for this graphic is large enough to store
	// the entire viewport.
	unsigned int memneeded = viewport.width * viewport.heigth * 3;
	/*if(g->cache._lastimageq < memneeded) {
		g->cache._lastimagev = realloc(g->cache._lastimagev, memneeded * sizeof(float) * 3);
		g->cache._lastimageq = memneeded;
	}*/

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
	             window_height - (viewport.y + viewport.heigth), // glReadPixels is weird with the y.
	             viewport.width,
	             viewport.heigth,
	             GL_RGB,
	             GL_UNSIGNED_BYTE,
	             0);
}

int draw(const *framedata_t) {

	int ret = 0;
	int kills = 0;

	framedata_t framedata ; // todo

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

		// easy pointers
		graphic_t *g = graphicbufv[i];
		drawaction lastact = g->cache.lastact;
		recti_t lastbbox = g->cache.lastviewport;

		// This switch statement will either end in break or continue:
		//   - continue: don't call dwg_draw_func.
		//   - break: call dwg_draw_func.
		// later: pre-draw we should only be handling DA_SLEEP.
		switch (lastact & _DA_BITMASK) {
			case DA_FRAME:
				ret = 1;
				// fallthrough
			case DA_INVALIDATED:
				break;
			default:
				error("graphic has unknown draw action, killing");
				g->cache.lastact = DA_KILL;
				// fallthrough
			case DA_KILL:
				// We'll remove this from the index after we exit the for loop.
				kills++;
				continue;
			case DA_SLEEP:
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
		glOrtho(0, g->viewport.width, 0, g->viewport.heigth, -1, 1);
		unsigned int err = glGetError(); // clear error bit
		drawaction newact = g->draw(g, &framedata);
		err = glGetError();
		if(err) {
			error("notice: glGetError non-null");
		}
		glPopMatrix();
		glViewport(0,
		           0,
		           window_width,
		           window_height);
		recti_t newview = g->viewport;

		// Now we have their last location in lastbbox.
		// And we have their new location in newres->bbox

		// So lets force-draw all sleepers above our previous and new location
		// by changing their registered action to DA_INVALDATED
		for(unsigned int j = i+1; j < graphicbufc; j++) {
			drawaction *aboveaction = &graphicbufv[j]->cache.lastact;
			if((*aboveaction & DA_SLEEP) == 0) {
				// the draw action here is no DA_SLEEP, which means it will be
				// drawn regardless. nothing to worry about.
				continue;
			}
			recti_t sleeperbbox = graphicbufv[j]->cache.lastviewport;

			// okay this is a sleeper, is it above our new or last location?...
			if(rect_intersectsi(sleeperbbox, newview) ||
					rect_intersectsi(sleeperbbox, lastbbox)) {
				// ...it is. Remove its DA_SLEEP flag and add the
				// DA_INVALIDATED flag. Note that this will destroy
				// any DAF flags. But thats okay sense the draw method should
				// be setting those every draw.
				*aboveaction = DA_INVALIDATED;
			}
		}

		// The only thing left to do is to check if this graphic is setting itself
		// to a sleeping state. If it is we copy the pixels so we can redraw this
		// graphic without invoking its draw method.
		if(newact & DA_SLEEP) {
			cachepixels(g);
		}
		// as a last little step, we can free up any memory of cached pixels if
		// the graphic /was/ in sleep state but now wants to be something else.
		else if(g->cache.lastact & DA_SLEEP) {
			decachepixels(g);
		}

		// Finally, update the lastres to match the latest draw for this graphic.
		g->cache.lastact = newact;
		g->cache.lastviewport = newview;
	}

	// splice out graphics that were killed this frame.
	for(unsigned int i = 0; kills > 0; i++) {
		// i is on an index of a graphic that needs to be killed.
		for(unsigned int j = i + 1; j < graphicbufc; j++) {
			graphicbufv[j-1] = graphicbufv[j];
		}
		kills--;
		graphicbufc--;
	}

	// return rather or not we should redraw.
	return ret;
}