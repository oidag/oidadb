#include <strings.h>
#include <malloc.h>
#include "elements_u.h"
//#include "../../../include/ellemdb.h"

// defined in column. Cross-defined here.
extern void column_place_child(column_t *u, graphic_t *shard);
extern element_type column_typeget(column_t *u);

typedef struct shard_t {
	int isselected;
	int ishover;
	column_t *owner;
	graphic_t *g;

	void *cookie;

	void (*drawselector)(void *cookie);

} shard_t;

static void draw(graphic_t *g) {
	shard_t *ent = glp_userget(g);

	// todo: switch statement as to which color to use based on type.
	color_t bg_norm = color_cyan200;
	color_t bg_hover = color_cyan100;

	// special border (if selected)
	if(!ent->isselected) {
		glp_draw(ent->g, GLP_SLEEPER, draw);
	} else {
		glp_draw(ent->g, GLP_ANIMATE, draw);

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
		ent->drawselector(ent->cookie);
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

static void ondelete(graphic_t *g) {
	shard_t *u = glp_userget(g);
	free(u);
	glp_user(g,0,0);
}


void shard_cookie(shard_t *s, void *cookie) {
	s->cookie = cookie;
}

// can be set to put aditional shit on the shard
void shard_ondraw(shard_t *s, void (*cb)(void *cookie)) {
	s->drawselector = cb;
}

// adds an arrow from the src shard to the dest shard.
void shard_point(shard_t *src, shard_t *dest);

// start
shard_t *shard_new(column_t *owner) {

	shard_t *u = malloc(sizeof(shard_t));
	bzero(u, sizeof(shard_t ));

	u->g = glp_new();
	glp_name(u->g, "shard");
	glp_user(u->g, u, ondelete);

	// take care of the placement.
	column_place_child(owner, u->g);

	glp_draw(u->g, GLP_SLEEPER, draw);
	glp_events(u->g, DAF_ONMOUSE_MOVE, event);
	glp_events(u->g, DAF_ONMOUSE_DOWN, event);

	return u;
}

