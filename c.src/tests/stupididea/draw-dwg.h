#ifndef draw_comps_h_
#define draw_comps_h_

#include "glplotter/glplotter.h"
#include "glplotter/primatives.h"
#include "text.h"

// some macros that assist with type casting to avoid constant wanrings
#define DWG_DEF(name) typedef struct dwg_##name##_st { \
graphic_t dwg;
#define DWG_END(name) } dwg_##name##_t; \
drawaction dwg_draw_##name(dwg_##name##_t *, framedata_t *); \
dwg_##name##_t dwg_##name##_new();


DWG_DEF(debug)
	const text_font *font;
	colorf color;
	vec2f vel;
DWG_END(debug)

DWG_DEF(background)
DWG_END(background)

#endif