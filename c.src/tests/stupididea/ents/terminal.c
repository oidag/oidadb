#include <malloc.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "ents_u.h"

enum termline_source {
	// from this process's STDOUT/STDERR
	APP_STDOUT,
	APP_STDERR,
};

typedef struct {
	// not copied, must be string literal
	enum termline_source source;
	// copied from source, must be free'd.
	char *msg;
} termline;

static struct {
	int init;

	graphic_t *g;

	int hovering; // set to 1 if mouse is hovering over this.

	float scrollx_off, scrolly_off, scolly_maxoff;

	int pthread_shutdown; // 1 for the pthread shoud shut down.
	pthread_t pthread;
	int fd_app_stdout_pipe[2];

	// the first index is the newest line
	termline linev[500];
	int lineq;
} terminal = {0};

static void setviewport(graphic_t *g, eventdata_t e) {
	vec2i size = glplotter_size();
	const int margin = 8;
	recti_t viewport = {
			size.width/12*4,
			0,
			size.width/12*8,
			size.height/12*3,
	};
	rect_growi(&viewport, -margin);
	glp_viewport(g, viewport);
}

static void draw(graphic_t *g) {

	// cache the viewport so we can reset it
	glp_viewport_t v = glp_viewportget(g);
	const int padding = 20;
	color_glset(color_stone800);
	glRecti(0,0,10000,10000);

	rect_growi(&v, -padding);
	glViewport(v.x,
			   v.y,
			   v.width,
			   v.heigth);

	text_defaults_monospace();
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,v.width,0,v.heigth,1, -1);
	// scroll offset
	glTranslatef(terminal.scrollx_off,
				 terminal.scrolly_off, 0);
	float linestarty = 0;
	const char *prefix;
	vec3ub prefix_color;
	vec3ub text_color;
	for(int i = 0; i < terminal.lineq; i++) {
		switch (terminal.linev[i].source) {
			case APP_STDOUT:
				prefix = "STDOUT";
				prefix_color = color_slate100;
				text_color = color_slate100;
				break;
			default:
				prefix = "UNKNOWN";
				prefix_color = color_slate100;
				text_color = color_slate100;
				break;
		}
		const char *msg = terminal.linev[i].msg;
		if(msg == 0) {
			continue;
		}
		color_glset(prefix_color);
		text_draw(0,linestarty,prefix);
		color_glset(text_color);
		text_draw(70,linestarty,msg);
		linestarty += text_height(msg);
	}
	terminal.scolly_maxoff = -(linestarty - (float)v.heigth);
	if(terminal.scolly_maxoff > 0) terminal.scolly_maxoff = 0;
	glPopMatrix();
}


// note: thread safe
void addline(enum termline_source source, const char *message) {

	// dealloc the message that's leaving if its there
	if(terminal.linev[terminal.lineq-1].msg) {
		free(terminal.linev[terminal.lineq-1].msg);
	}
	// shift all the messages toward the back of the array.
	// note i>0, so terminal[0-1] never happens
	for(int i = terminal.lineq-1; i > 0; i--) {
		terminal.linev[i] = terminal.linev[i-1];
	}
	terminal.linev[0].source = source;
	terminal.linev[0].msg = malloc(strlen(message) + 1);
	memcpy(terminal.linev[0].msg, message, strlen(message) + 1);

	glp_invalidatef(terminal.g, 1);
}

static void onscroll(graphic_t *g, eventdata_t e) {
	recti_t v;
	switch(e.type) {
		default:
		case DAF_ONMOUSE_MOVE:
			v = glp_viewportget(g);
			terminal.hovering = rect_contains(v, e.pos);
			return;
		case DAF_ONSCROLL:
			// only when hovering
			if(!terminal.hovering) return;

			terminal.scrollx_off += (float)e.scroll.x;
			terminal.scrolly_off -= (float)e.scroll.y * 10;

			// clamp
			if(terminal.scrolly_off < terminal.scolly_maxoff) {
				terminal.scrolly_off = terminal.scolly_maxoff;
			}
			if(terminal.scrolly_off > 0) {
				terminal.scrolly_off = 0;
			}
			glp_invalidate(g);
			return;
	}
}

void *listener(void *_) {
	char buff[255];
	while(1) {
		ssize_t n = read(terminal.fd_app_stdout_pipe[0], buff, 254); // -1 for null term
		if(n == -1) {break;}
		buff[n] = 0;
		addline(APP_STDOUT, buff);
	}
}

void closelistener() {
	close(terminal.fd_app_stdout_pipe[0]);
	close(terminal.fd_app_stdout_pipe[1]);
	fclose(stdout);
	pthread_join(terminal.pthread, 0);
}


void ent_terminal_ondestroy() {
	closelistener();
}

// start
void ent_terminal_start() {
	if(terminal.init) return; // singleton
	terminal.lineq = 500;
	graphic_t *g = glp_new();
	glp_name(g, "terminal");
	setviewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_user(g, &terminal, ent_terminal_ondestroy);
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);

	glp_events(g, DAF_ONSCROLL, onscroll);
	glp_events(g, DAF_ONMOUSE_MOVE, onscroll);
	terminal.g = g;


	pipe(terminal.fd_app_stdout_pipe);
	dup2(terminal.fd_app_stdout_pipe[1], fileno(stdout));
	setvbuf(stdout, NULL, _IONBF, 0);


	// start the listener
	pthread_create(&terminal.pthread, 0, listener, 0);
}

