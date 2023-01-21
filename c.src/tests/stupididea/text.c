
#include <drawtext.h>
#include <GL/gl.h>

#include "error.h"
#include "text.h"

const char *monospace = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";

void text_setfont(text_font font) {
	dtx_use_font(font.font, font.size);
}
int text_addfont(const char *file, int size, text_font *o_font) {
	o_font->size = size;
	o_font->font = dtx_open_font(file, size);
	if(!o_font->font) {
		error("open font");
		return 1;
	}
	return 0;
}
void text_draw(float x, float y, const char *text) {
	glPushMatrix();
	//glTranslatef(x,y + (dtx_string_height(text) - dtx_line_height()),0);
	glTranslatef(x,y,0);
	dtx_string(text);
	glPopMatrix();
}