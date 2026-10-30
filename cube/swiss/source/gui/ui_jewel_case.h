#ifndef UI_JEWEL_CASE_H
#define UI_JEWEL_CASE_H

#include <stdbool.h>

#ifdef UI_ASSETS_HOST_BUILD
#include "ui_assets.h"	/* the host tests' GXTexObj */
#else
#include <gccore.h>
#endif

#include "ui_motion.h"

/* The Library's Jewel Case layout: the selected game as a clear CD jewel
 * case, its disc spinning inside, after portlandhodl's jewel case games view
 * for Swiss (github.com/portlandhodl/swiss-gc, GPL-2.0), itself a GX port of
 * Ann Nguyen's CD Case (bubbbly.com/cd-case). FrameBufferMagic.c places the
 * cases and supplies each disc's print; this draws them. */

/* One case on stage this frame. */
typedef struct {
	/* Its place on the ring: 0 at rest in the middle, and up to one place
	 * either side on its way in or out. */
	float slot;
	/* Its place in the library, so cases sway apart from one another. */
	u32 index;
	/* What is printed on the disc, or NULL for a bare one; rect is the
	 * square of the texture laid over the disc, s0, t0, s1, t1. */
	GXTexObj *print;
	float rect[4];
	/* How far the print has faded in, and the whole case, 0 to 1. */
	float printAlpha;
	float alpha;
} uiJewelCaseShot_t;

/* The studio's light and the case's meshes: arithmetic for a fraction of a
 * second on the console, done once, on the menu thread, before the layout's
 * first frame. False when there was no memory for it; Ready says the same. */
bool UIJewelCase_Prepare(void);
bool UIJewelCase_Ready(void);

/* Draws up to two cases, the one leaving and the one arriving. direction is
 * where the selection last moved: 1 on, -1 back, 0 at rest. seconds is the
 * frame's length; turnX and turnY are the C-stick, -1 to 1, turning the case
 * in the hands. spin false eases the disc to rest (Z in the Jewel Case).
 * Video thread only, with the print cache's publication lock held so no
 * supplied texels change during the draw. It leaves GX to drawInit to set
 * up again. */
void UIJewelCase_Draw(const uiJewelCaseShot_t *shots, int count, int direction,
	float seconds, float turnX, float turnY, uiMotionMode_t motion, bool spin);

#endif
