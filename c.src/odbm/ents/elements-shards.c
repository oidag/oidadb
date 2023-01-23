#include <strings.h>
#include "elements_u.h"
//#include "../../../include/ellemdb.h"

typedef struct element_t {
	graphic_t *shard;

	vec2i pos;

	//  fields set automatically.
	int isselected; //bool
	int ishover;
	element_type type;
	// draw the inner-contents of the selector.
	void (*drawselector)();
} shard_t;

static void viewport(graphic_t *g, eventdata_t e) {
	vec2i size = glplotter_size();
	shard_t *ent = glp_userget(g);
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
	shard_t *ent = glp_userget(g);

	// todo: switch statement as to which color to use based on type.
	color_t bg_norm = color_cyan200;
	color_t bg_hover = color_cyan100;

	// special border (if selected)
	if(!ent->isselected) {
		glp_draw(ent->shard, GLP_SLEEPER, draw);
	} else {
		glp_draw(ent->shard, GLP_ANIMATE, draw);

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
	shard_t *ent = glp_userget(g);
	switch (e.type) {
		case DAF_ONMOUSE_DOWN:
			if(ent->ishover) {
				ent->isselected = !ent->isselected;
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

// start
shard_t *element_new() {

	// todo: memory management with o_ent.

	bzero(o_ent, sizeof(shard_t ));

	o_ent->shard = glp_new();
	glp_name(o_ent->shard, "oidadb-entity");
	glp_user(o_ent->shard, o_ent, 0);

	// todo: these should be passed in/generated
	o_ent->pos.x = 0;
	o_ent->pos.y = 500;

	viewport(o_ent->shard, (eventdata_t){0});
	glp_draw(o_ent->shard, GLP_SLEEPER, draw);
	glp_events(o_ent->shard, DAF_ONMOUSE_MOVE, event);
	glp_events(o_ent->shard, DAF_ONMOUSE_DOWN, event);
	glp_events(o_ent->shard, DAF_ONWINDOWSIZE, viewport);
}