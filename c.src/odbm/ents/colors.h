#ifndef COLORS_H_
#define COLORS_H_

#include <GL/gl.h>
#include "../glplotter/primatives.h"

typedef vec3ub color_t;

#define _COLOR(color,shade,hexcode) const static color_t color_##color##shade = { \
		(0x##hexcode >> 0x10) & 0xFF,\
		(0x##hexcode >> 0x8) & 0xFF,\
		0x##hexcode & 0xFF,\
};

// cyans
_COLOR(cyan, 50, ecfeff)
_COLOR(cyan, 100, cffafe)
_COLOR(cyan, 200, a5f3fc)
_COLOR(cyan, 300, 67e8f9)
_COLOR(cyan, 400, 22d3ee)
_COLOR(cyan, 500, 06b6d4)
_COLOR(cyan, 600, 0284c7)
_COLOR(cyan, 700, 0369a1)
_COLOR(cyan, 800, 155e75)
_COLOR(cyan, 900, 164e63)

// pinks
_COLOR(pink, 50, fff1f2)
_COLOR(pink, 100, ffe4e6)
_COLOR(pink, 200, fecdd3)
_COLOR(pink, 300, fda4af)
_COLOR(pink, 400, fb7185)
_COLOR(pink, 500, f43f5e)
_COLOR(pink, 600, e11d48)
_COLOR(pink, 700, be123c)
_COLOR(pink, 800, 9f1239)
_COLOR(pink, 900, 881337)

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

// stones hmmm.. think I should remove these in leu of just using slate.
_COLOR(stone, 700, 44403c)
_COLOR(stone, 800, 292524)
_COLOR(stone, 900, 1c1917)


// violet
_COLOR(violet, 50,  faf5ff)
_COLOR(violet, 100, f3e8ff)
_COLOR(violet, 200, e9d5ff)
_COLOR(violet, 300, c4b5fd)
_COLOR(violet, 400, a78bfa)
_COLOR(violet, 500, a855f7)
_COLOR(violet, 600, 9333ea)
_COLOR(violet, 700, 6d28d9)
_COLOR(violet, 800, 5b21b6)
_COLOR(violet, 900, 4c1d95)

void static inline color_glset(color_t c) {
	glColor3ubv((GLubyte*)&c);
}

#endif

/*
 * https://open.spotify.com/track/22y2mqIBERBviZF0fa86iw?si=37b3fcd333654f94
 * https://open.spotify.com/track/1gNrhuOhbyF2YPuFQPvDob?si=a6442580d8334982
 */