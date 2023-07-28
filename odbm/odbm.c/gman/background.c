#include <GL/gl.h>
#include "gman_u.h"

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


	color_glset(color_stone900);
	glVertex2f(x,y);


	color_glset(color_slate900);
	glVertex2f(x,h);

	color_glset(color_stone900);
	glVertex2f(w,h);

	color_glset(color_slate900);
	glVertex2f(w,y);

	glEnd();
}

ent_background_t bg;

int background_start() {

	graphic_t *g = glp_new();
	glp_user(g, &bg, 0);
	setviewport(g);
	glp_name(g, "background");
	glp_draw(g, GLP_SLEEPER, drawbg);
	glp_events(g, DAF_ONWINDOWSIZE, onresize);
	return 0;
}
