#ifndef text_h_
#define text_h_

typedef struct {
	struct dtx_font *font;
	int size;
} text_font;

int text_addfont(const char *file, int size, text_font *o_font);

void text_setfont(text_font font);
void text_draw(float x, float y, const char *text);

#endif