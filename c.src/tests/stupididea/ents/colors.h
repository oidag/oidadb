#ifndef COLORS_H_
#define COLORS_H_

#include <GL/gl.h>
#include "../glplotter/primatives.h"

typedef vec3ub color_t;

#define _COLOR(color,shade,hexcode) const static color_t color_##color##shade = { \
		(0x##hexcode >> 0x12) & 0xFF,\
		(0x##hexcode >> 0x8) & 0xFF,\
		0x##hexcode & 0xFF,\
};

// cyans
_COLOR(cyan, 50, ecfeff)
_COLOR(cyan, 100, cffafe)
_COLOR(cyan, 500, 06b6d4)
_COLOR(cyan, 600, 0284c7)
_COLOR(cyan, 700, 0369a1)
_COLOR(cyan, 800, 155e75)
_COLOR(cyan, 900, 164e63)

// pinks

// emeralds
_COLOR(emerald, 50, ecfdf5)
_COLOR(emerald, 100, d1fae5)
_COLOR(emerald, 200, a7f3d0)
_COLOR(emerald, 300, 6ee7b7)
_COLOR(emerald, 400, 34d399)
_COLOR(emerald, 500, 10b981)
_COLOR(emerald, 600, 059669)
_COLOR(emerald, 700, 047857)
_COLOR(emerald, 800, 065f46)
_COLOR(emerald, 900, 064e3b)

// slates
_COLOR(slate, 50,  f8fafc)
_COLOR(slate, 100, f1f5f9)
_COLOR(slate, 200, e2e8f0)
_COLOR(slate, 300, cbd5e1)
_COLOR(slate, 400, 94a3b8)
_COLOR(slate, 500, 64748b)
_COLOR(slate, 600, 475569)
_COLOR(slate, 700, 334155)
_COLOR(slate, 800, 1e293b)
_COLOR(slate, 900, 0f172a)

// stones
_COLOR(stone, 700, 44403c)
_COLOR(stone, 800, 292524)
_COLOR(stone, 900, 1c1917)

void static inline color_glset(color_t c) {
	glColor3ubv((GLubyte*)&c);
}

#endif

/*
 * https://open.spotify.com/track/22y2mqIBERBviZF0fa86iw?si=37b3fcd333654f94
 * https://open.spotify.com/track/1gNrhuOhbyF2YPuFQPvDob?si=a6442580d8334982
 */