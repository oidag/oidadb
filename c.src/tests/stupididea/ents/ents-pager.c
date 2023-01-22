#include <stdio.h>
#include <GL/gl.h>
#include "ents.h"
#include "../dbfile/dbfile.h"



typedef struct {

} page;

typedef struct {
	vec3f color;

	// point to viewport.
	int *width;
	int *height;

	int ishover;

}pager;

typedef struct {
	recti_t viewport;
	int width;
	int height;
} pagecontents;

static pager _user; // don't use.
static pager *user = &_user; // use this.

static void event(graphic_t *g, eventdata_t e) {
	switch (e.type) {
		case DAF_ONMOUSE_DOWN:
			if(user->ishover) {
				user->color.green += 0.1f;
			}
			glp_invalidate(g);
			break;
		default:
		case DAF_ONMOUSE_MOVE:
			user->ishover = rect_contains(glp_viewportget(g), e.pos);
			break;
	}

}

static void setviewport(graphic_t *g, eventdata_t _) {
	// move it to the middle of the screen
	vec2i size = glplotter_size();
	int width = size.width/5;
	int height = size.height;
	glp_viewport(g, (recti_t) {
			0,
			0,
			width,
			height,
	});
}

static void draw(graphic_t *g){
	glColor3f(user->color.red,user->color.green,user->color.blue);

	glLineWidth(2);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_LINES);
	glVertex2f(1,0);
	glVertex2f(1,1);
	glEnd();
	glPopMatrix();


	// get pagec
	int pagec = file_getpagec();


}


void ent_pager_new() {
	graphic_t *g = glp_new();

	// initialize structure
	user->color = (vec3f){
		1,0,0
	};

	// glp set up
	glp_user(g, &_user, 0);
	setviewport(g, (eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONMOUSE_MOVE, event);
	glp_events(g, DAF_ONMOUSE_DOWN, event);
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);
}