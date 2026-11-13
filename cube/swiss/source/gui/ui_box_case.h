#ifndef UI_BOX_CASE_H
#define UI_BOX_CASE_H

#include <stdbool.h>

typedef struct {
	float x;
	float y;
} uiBoxCasePoint_t;

typedef struct {
	uiBoxCasePoint_t point[4];
} uiBoxCaseQuad_t;

typedef struct {
	uiBoxCaseQuad_t front;
	uiBoxCaseQuad_t side;
} uiBoxCasePose_t;

enum { UI_BOX_CASE_STRIPS = 8 };

typedef struct {
	/* Each adjacent pair supplies one strip's left/right top/bottom. */
	uiBoxCasePoint_t column[UI_BOX_CASE_STRIPS + 1][2];
} uiBoxCaseStrips_t;

/* Project a closed retail case into the 640 x 480 menu stage. slot is the
 * signed carousel position: 0 faces forward, 1 and 2 are its neighbours,
 * and 3 is the edge at which the renderer fades a case away. Quads share
 * their seams exactly and wind clockwise. Hidden faces collapse to an edge.
 * The camera is level with the case, so the top stays hidden; the front of
 * the selected case is a clean 180 x 240 rectangle at (230, 96)..(410, 336).
 *
 * Stateless: no allocation, I/O or retained textures. NULL, non-finite and
 * out-of-range inputs return false without changing the output. */
bool UIBoxCase_Pose(float slot, uiBoxCasePose_t *pose);

/* Prepare the eight front-art strips once per case. Physical u/v are inset
 * by 0.033333 of the face; texture UVs remain 0..0.75 by 0..1. Validate the
 * front and measure its edge heights once, then share each column between
 * its neighbouring strips. A zero-height face uses bilinear sampling. NULL,
 * non-finite inputs or non-finite results return false without changing out. */
bool UIBoxCase_Strips(const uiBoxCaseQuad_t *front, uiBoxCaseStrips_t *out);

#endif
