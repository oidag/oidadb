#include <strings.h>
#include "gman_u.h"

typedef struct dialog_t {
	int init;

	int hovering;
	float scrolly_off;
	float scrolly_maxoff;

	element_type type;
	graphic_t *g;
	const char *title;

	void (*bodydraw)(int width);
}dialog_t;
static dialog_t dialog = {0};

static void onscroll(graphic_t *g, eventdata_t e) {
	recti_t v;
	switch(e.type) {
		default:
		case DAF_ONMOUSE_MOVE:
			v = glp_viewportget(g);
			dialog.hovering = rect_contains(v, e.pos);
			return;
		case DAF_ONSCROLL:
			// only when hovering
			if(!dialog.hovering) return;

			dialog.scrolly_off -= (float)e.scroll.y * 10;

			// clamp
			if(dialog.scrolly_off < dialog.scrolly_maxoff) {
				dialog.scrolly_off = dialog.scrolly_maxoff;
			}
			if(dialog.scrolly_off > 0) {
				dialog.scrolly_off = 0;
			}
			glp_invalidate(g);
			return;
	}
}
static void viewport(graphic_t *g, eventdata_t e)
{
	vec2i size = glplotter_size();
	int w = size.width/12*7;
	int h = size.height/8*5;
	recti_t v = {
			size.width - w - (size.width/12/2),
			size.height/8*2 + size.height/8/2,
			w,
			h,
	};
	glp_viewport(g, v);
}
static void draw(graphic_t *g) {

	const glp_viewport_t vp = glp_viewportget(g);

	glPushMatrix();
	{
		glLoadIdentity();
		glOrtho(0, 1, 0, 1, 1, -1);

		// bg
		glBegin(GL_QUADS);
		color_glset(color_slate700);
		glVertex2f(0, 0);
		glVertex2f(0, 1);
		glVertex2f(1, 1);
		glVertex2f(1, 0);
		glEnd();
	}
	glPopMatrix();




	// header
	int h1width = vp.width;
	int h1heigth = vp.heigth/24*3;
	glViewport(vp.x,
			   vp.y + vp.heigth - h1heigth,
			   h1width,
			   h1heigth);

	glPushMatrix();
	{
		glLoadIdentity();
		glOrtho(0, 1, 0, 1, 1, -1);
		glBegin(GL_QUADS);
		color_glset(element_type2color(dialog.type));
		glVertex2f(0, 0);
		glVertex2f(0, 1);
		glVertex2f(1, 1);
		glVertex2f(1, 0);
		glEnd();
	}
	glPopMatrix();


	glPushMatrix();
	{
		glLoadIdentity();
		glOrtho(0, h1width, 0, h1heigth, 1, -1);

		// h1
		text_defaults_h1();
		float textwidth = text_width("W");
		float y = (float) h1heigth / 2 - text_height("W") / 2;
		color_glset(color_slate50);

		// specific h1
		text_draw(textwidth, y, dialog.title);
		// general h1
		const char *str = element_type2str(dialog.type);
		text_draw((float) h1width - text_width(str) - textwidth, y, str);
	}
	glPopMatrix();

	// draw the body.
	glPushMatrix();
	glViewport(vp.x,
	           vp.y,
	           vp.width,
	           vp.heigth - h1heigth);
	glTranslatef(0,
	             dialog.scrolly_off, 0);
	dialog.bodydraw(vp.width);
	glPopMatrix();




}
static void events(graphic_t *g, eventdata_t e);

void dialog_settitle(const char *title) {
	dialog.title=title;
}
static void defaultdraw() {

};

void dialog_drawbody(void (*draw)(int)) {
	dialog.bodydraw = draw;
}

void dialog_invalidatef() {
	glp_invalidatef(dialog.g, 1);
}

void dialog_setheight(float height) {
	dialog.scrolly_maxoff = height;
}

// start
void dialog_start() {
	if(dialog.init) return; // singleton
	dialog.init = 1;
	dialog.type = ELM_PAGE;
	graphic_t *g = dialog.g = glp_new();
	glp_name(g, "dialog");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
	glp_events(g, DAF_ONSCROLL, onscroll);

	// defaults
	dialog_settitle("NOTITLE");
	dialog_drawbody(defaultdraw);
}