
#include <GL/gl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "error.h"
#include "glplotter/primatives.h"
#include "draw-dwg.h"

#define DWG_INIT(val, name) val.dwg.draw = (dwg_draw_func)dwg_draw_##name
int test = 0;
drawaction dwg_draw_debug(dwg_debug_t *g, framedata_t *f){
	test++;
	/*if(test == 2) {
		g->dwg.drawaction = da_kill;
		return;
	}*/
//	if(test & 16) {
	recti_t *viewport = &g->dwg.viewport;
	int k = 0;
	if(((float)viewport->x + g->vel.x < 0&&g->vel.x<0) ||
	                                           ((float)viewport->x + g->vel.x > (float)window_width)&&g->vel.x > 0) {
		g->vel.x = -g->vel.x;
		k++;
	}
	if(((float)viewport->y + g->vel.y < 0&&g->vel.y<0) ||
			((float)viewport->y + g->vel.y > (float)window_height&&g->vel.y > 0)) {
		g->vel.y = -g->vel.y;
		k++;
	}
	if(k == 2) {
		g->vel.x = (float)(rand() % 10);
		g->vel.y = (float)(rand() % 10);
	}
	viewport->x += (int)g->vel.x;
	viewport->y += (int)g->vel.y;

	glColor3f(g->color.r,g->color.g,g->color.b);
	glRecti(-100,-100,100,100);

	return DA_FRAME;
}

dwg_debug_t dwg_debug_new() {
	dwg_debug_t ret;
	DWG_INIT(ret, debug);
	ret.color = (colorf) {
			(float)(rand()&0x1),
			(float)(rand()&0x1),
			(float)(rand()&0x1),
	};
	ret.dwg.viewport = (recti_t){
			.x = 0,
			.y = 0,
			.width = 10,
			.heigth = 10,
	};

	ret.vel.x = (float)(rand() % 10);
	ret.vel.y = (float)(rand() % 10);
	return ret;
}

drawaction dwg_draw_background(dwg_background_t *g, framedata_t *f){
	glBegin(GL_POLYGON);
	glColor3f(1.f,0,0);
	glVertex2i(0,0);
	glColor3f(0.1f,0,1.f);
	glVertex2i(0,g->dwg.viewport.width);
	glColor3f(0,1.f,0.1f);
	glVertex2i(g->dwg.viewport.width,g->dwg.viewport.heigth);
	glColor3f(0,1.f,0);
	glVertex2i(g->dwg.viewport.width,0);
	glEnd();
	return DA_SLEEP;
}

dwg_background_t dwg_background_new() {
	dwg_background_t ret;
	DWG_INIT(ret, background);
	ret.dwg.viewport = (recti_t) {
		0,0,window_width, window_height
	};
	return ret;
}