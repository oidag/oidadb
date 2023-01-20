#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "window.h"
#include "error.h"

int window_width,window_height;
float window_mousex = 0, window_mousey = 0;


static GLFWwindow *window;

static void cursor_position_callback(GLFWwindow* _, double xpos, double ypos) {
	window_mousex = (float)xpos;
	window_mousey = (float)ypos;
}

static void window_size(GLFWwindow* _, int w, int h) {
	window_width  = w;
	window_height = h;
	glViewport(0,0,w,h);
	glLoadIdentity();
}

int window_init(const char *name, float initwidth, float initheight) {
		if (!glfwInit()) {
			error("glfwInit");
			return -1;
		}
	window_width = initwidth;
	window_height = initheight;
		window = glfwCreateWindow(window_width, window_height, name, 0, 0);
		if (!window)
		{
			error("create window");
			glfwTerminate();
			return -1;
		}
		glfwMakeContextCurrent(window);
		glfwSetCursorPosCallback(window, cursor_position_callback);
		glfwSetWindowSizeCallback(window, window_size);
		return 0;
}

static int(* draw_cb)();

void window_ondraw(int(* cb)()) {
	draw_cb = cb;
}

int window_render() {

	int err = 0;
	while (!glfwWindowShouldClose(window))
	{
		int ret = draw_cb();
		glfwSwapBuffers(window);
		switch(ret) {
			case -1:
				err = -1;
			case -2:
				glfwSetWindowShouldClose(window, GLFW_TRUE);
				glfwPollEvents();
				break;
			case 0:
				glfwWaitEvents();
				break;
			case 1:
				glfwPollEvents();
				break;
		}

		/*
		frameid++;
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT);

		// page pannel
		{
			const float panel_padding = 8;
			const float panel_margin = 8;
			const float page_vmargin = 4;

			const float panelwidth = width / 5 - panel_margin;
			const float panelheight = height - panel_margin * 2;
			glPushMatrix();
			glTranslatef(panel_margin, panel_margin, 1);

			// draw border

			glBegin(GL_LINE_LOOP);
			glLineWidth(2);
			glColor3f(1,0,0);
			glVertex2f(0,0);
			glVertex2f(0,panelheight);
			glVertex2f(panelwidth,panelheight);
			glVertex2f(panelwidth,0);
			glEnd();

			// pages
			int pagec = getpagec();
			const float pageheight = (panelheight - panel_padding * 2 - page_vmargin*pagec) / ((float)pagec);
			const float pagewidth = panelwidth - panel_padding*2;
			for(int i = 0; i < pagec; i++) {
				glPushMatrix();
				glTranslatef(panel_padding, panel_padding + (page_vmargin + pageheight) * (float)i, 0);

				glBegin(GL_QUADS);
				if(i % 16 == 0) {
					glColor3f(0.4,0.4,0);
				} else {
					glColor3f(0.4,0.5,0);
				}
				glVertex2f(0,0);
				glVertex2f(0,pageheight);
				glVertex2f(pagewidth,pageheight);
				glVertex2f(pagewidth,0);
				glEnd();

				glPopMatrix();
			}

			glPopMatrix();
		}

		// reader
		{
			const float margin = 16;
			const float startwrite = (width / 5) + margin;
			text_setfont(debugfont);
			glColor3f(0, 1, 0);
			char buff[17];
			buff[16] = 0;
			glPushMatrix();
			glTranslatef(startwrite, margin, 0);
			for(int i = 0; i < getpagesize() / 16; i++) {
				lseek(f.fd, i * 16, SEEK_SET);
				read(f.fd, buff, 16);
				text_draw(buff, (screenpos) {.x = 0, .y = (float)i * 20});
			}
			glPopMatrix();
		}


		// frame/debug shit
		{
			text_setfont(debugfont);
			sprintf(debugbuf, "frame:%x\nmousepos: %.0f,%.0f", frameid, mousex, mousey);
			glColor3f(0, 1, 0);
			text_draw(debugbuf, (screenpos) {.x = width - 400, .y = height - 100});
		}*/
	}
	return err;
}

void window_close() {
	glfwTerminate();
}