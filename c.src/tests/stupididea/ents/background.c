#include <GL/gl.h>
#include "background.h"
#include "../glplotter/glplotter.h"

static void setviewport(graphic_t *g) {
	vec2i size = glplotter_size();
	glp_viewport(g, (glp_viewport_t){
			0,
			0,
			size.width,
			size.height,
	});
}

static void onresize(graphic_t *g, eventdata_t e) {
	setviewport(g);
	glp_invalidate(g);
}

static void drawbg(graphic_t *g){
	vec2i size = glplotter_size();
	float x = 0;
	float y = 0;
	float w = (float)size.width;
	float h = (float)size.height;

	glBegin(GL_QUADS);

	glColor3ub(32,25,0);
	glVertex2f(x,y);

	glColor3ub(40,34,11);
	glVertex2f(x,h);

	glColor3ub(26,37,17);
	glVertex2f(w,h);

	glColor3ub(43,34,0);
	glVertex2f(w,y);

	glEnd();
}

int ent_background_new(ent_background_t *o_bg) {

	graphic_t *g = glp_new();
	glp_user(g, o_bg, 0);

	setviewport(g);
	glp_draw(g, GLP_SLEEPER, drawbg);
	glp_events(g, DAF_ONWINDOWSIZE, onresize);
	return 0;
}
