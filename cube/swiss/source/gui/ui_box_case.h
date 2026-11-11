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
	uiBoxCaseQuad_t top;
} uiBoxCasePose_t;

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

/* A physical (u, v) point on a projected front, including its perspective.
 * Unlike a bilinear screen-space sample, the middle of the artwork remains
 * the projection of the case's physical middle. The front has vertical
 * edges, as Pose and its transitions into Detail do. Finite u/v are clamped
 * to 0..1. A zero-height face falls back to bilinear sampling; NULL or
 * non-finite inputs return (0, 0). No output can contain an infinity/NaN. */
uiBoxCasePoint_t UIBoxCase_Point(const uiBoxCaseQuad_t *front, float u, float v);

#endif
