#include <strings.h>
#include "ents_u.h"
//#include "../../../include/ellemdb.h"

typedef enum ent_type {
	ENT_UNINIT, // unitialized (0)
	ENT_DESCRIPTOR,
	ENT_PAGE,
	ENT_WORKER,
	ENT_JOB,
	ENT_EVENT,
} ent_type;

typedef struct odbent {
	vec2i pos;

	graphic_t *selector;

	int selected; //bool
	int ishover;

	ent_type type;

	// draw the inner-contents of the selector.
	void (*drawselector)();

} odbent;

static void viewport(graphic_t *g, eventdata_t e) {
	vec2i size = glplotter_size();
	odbent *ent = glp_userget(g);
	int width = size.width / 18*1;
	int height = size.width / 100;
	glp_viewport(g, (glp_viewport_t) {
		ent->pos.x,
		ent->pos.y,
		width*2,
		height*2
	});
}

// view port for the dialog
static void viewport_dialog(graphic_t *g, eventdata_t e) {

}

static void draw(graphic_t *g) {
	odbent *ent = glp_userget(g);

	// todo: switch statement as to which color to use based on type.
	color_t bg_norm = color_cyan200;
	color_t bg_hover = color_cyan100;

	// special border (if selected)
	if(!ent->selected) {
		glp_draw(ent->selector, GLP_SLEEPER, draw);
	} else {
		glp_draw(ent->selector, GLP_ANIMATE, draw);

		// now to draw some magical shit. You can try to
		// read through this but who cares. If it's bad
		// then just make another.
		// constantly ticking up from 0 to 1.
		double c50 = (double)(glplotter_frameid % 110) / 110;
		double c = (double)(glplotter_frameid % 80) / 80;
		double t = sin( 3.1415*c);

		glPushMatrix();
		glLoadIdentity();
		glOrtho(0,1,0,1,1,-1);
		glBegin(GL_QUADS);
		color_glset(color_violet900);
		glVertex2f(0,0);
		glVertex2f(0,1);
		glVertex2f(1,1);
		glVertex2f(1,0);

		glColor4d(((double )color_pink400.red/(double )UINT8_MAX),
		          ((double )color_pink400.green/(double )UINT8_MAX),
		          ((double )color_pink400.blue/(double )UINT8_MAX),
				  t);
		glVertex2f(0,0);
		glVertex2f(0,1);
		glVertex2f(1,1);
		glVertex2f(1,0);
		glEnd();

		c = c50;
		c = c * 1.6;
		c -= 0.6;
		vec3ub scolor = color_slate50;
		glBegin(GL_QUADS);
		//start
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           0);
		glVertex2d(0+c,0);
		glVertex2d(0.2+c,1);
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           255);
		glVertex2d(0.4+c,1);
		glVertex2d(0.2+c,0);
		//end block
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           255);
		glVertex2d(0.2+c,0);
		glVertex2d(0.4+c,1);
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           0);
		glVertex2d(0.6+c,1);
		glVertex2d(0.4+c,0);
		glEnd();

		glPopMatrix();
	}

	// background
	// add margin to background (for border)
	int bordersize = 3;
	recti_t v = glp_viewportget(g);
	rect_growi(&v, -bordersize);
	glViewport(v.x, v.y, v.width, v.heigth);
	if(ent->ishover) {
		color_glset(bg_hover);
	} else {
		color_glset(bg_norm);
	}
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_QUADS);
	glVertex2f(0,0);
	glVertex2f(0,1);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();
	glPopMatrix();
	if(ent->drawselector) {
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0,v.width,0,v.heigth,1,-1);
		ent->drawselector;
		glPopMatrix();
	}
}

static void event(graphic_t *g, eventdata_t e) {
	odbent *ent = glp_userget(g);
	switch (e.type) {
		case DAF_ONMOUSE_DOWN:
			if(ent->ishover) {
				ent->selected = !ent->selected;
			}
			glp_invalidate(g);
			break;
		default:
		case DAF_ONMOUSE_MOVE:
			ent->ishover = rect_contains(glp_viewportget(g), e.pos);
			glp_invalidate(g);
			break;
	}
}

odbent _ent;
odbent *o_ent = &_ent;
// start
void ent_page_new() {

	bzero(o_ent, sizeof(odbent ));

	o_ent->selector = glp_new();
	glp_name(o_ent->selector, "oidadb-entity");
	glp_user(o_ent->selector, o_ent, 0);

	// todo: these should be passed in/generated
	o_ent->pos.x = 500;
	o_ent->pos.y = 500;

	viewport(o_ent->selector,(eventdata_t){0});
	glp_draw(o_ent->selector, GLP_SLEEPER, draw);
	glp_events(o_ent->selector, DAF_ONMOUSE_MOVE, event);
	glp_events(o_ent->selector, DAF_ONMOUSE_DOWN, event);
	glp_events(o_ent->selector, DAF_ONWINDOWSIZE, viewport);
}