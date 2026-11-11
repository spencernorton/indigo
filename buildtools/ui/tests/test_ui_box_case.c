#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_box_case.h"

static float area(const uiBoxCaseQuad_t *quad)
{
	float value = 0.0f;
	int i;

	for(i = 0; i < 4; ++i) {
		uiBoxCasePoint_t a = quad->point[i];
		uiBoxCasePoint_t b = quad->point[(i + 1) % 4];
		value += a.x * b.y - b.x * a.y;
	}
	return value * 0.5f;
}

static void closePoint(uiBoxCasePoint_t a, uiBoxCasePoint_t b)
{
	assert(fabsf(a.x - b.x) < 0.001f);
	assert(fabsf(a.y - b.y) < 0.001f);
}

static void centre(void)
{
	uiBoxCasePose_t pose;

	assert(UIBoxCase_Pose(0.0f, &pose));
	closePoint(pose.front.point[0], (uiBoxCasePoint_t) {230.0f, 96.0f});
	closePoint(pose.front.point[1], (uiBoxCasePoint_t) {410.0f, 96.0f});
	closePoint(pose.front.point[2], (uiBoxCasePoint_t) {410.0f, 336.0f});
	closePoint(pose.front.point[3], (uiBoxCasePoint_t) {230.0f, 336.0f});
	assert(area(&pose.side) == 0.0f);
	assert(area(&pose.top) == 0.0f);
}

static void neighbours(void)
{
	uiBoxCasePose_t near;
	uiBoxCasePose_t far;

	assert(UIBoxCase_Pose(1.0f, &near));
	assert(UIBoxCase_Pose(2.0f, &far));
	/* Separate physical side surfaces, sharing the front's right edge. */
	assert(area(&near.side) > 100.0f);
	assert(area(&far.side) > 100.0f);
	closePoint(near.side.point[0], near.front.point[1]);
	closePoint(near.side.point[3], near.front.point[2]);
	assert(near.side.point[1].x > near.front.point[1].x + 5.0f);
	/* At rest neither neighbour covers the selected front, and the outer
	 * one fits behind the near one's right edge rather than over its art. */
	assert(near.front.point[0].x > 410.0f);
	assert(far.front.point[0].x > near.side.point[1].x);
	assert(far.side.point[1].x < 640.0f);
	assert(near.front.point[1].y < near.front.point[0].y);
}

static void sweep(void)
{
	uiBoxCasePose_t previous;
	int step;

	assert(UIBoxCase_Pose(-3.0f, &previous));
	for(step = -3000; step <= 3000; ++step) {
		float slot = (float)step / 1000.0f;
		uiBoxCasePose_t pose;
		uiBoxCasePose_t mirror;
		const uiBoxCaseQuad_t *faces[3];
		int face;
		int i;

		assert(UIBoxCase_Pose(slot, &pose));
		assert(UIBoxCase_Pose(-slot, &mirror));
		assert(area(&pose.front) > 1000.0f);
		assert(area(&pose.side) >= -0.02f);
		assert(fabsf(area(&pose.top)) < 0.02f);
		faces[0] = &pose.front;
		faces[1] = &pose.side;
		faces[2] = &pose.top;
		for(face = 0; face < 3; ++face) {
			for(i = 0; i < 4; ++i) {
				assert(isfinite(faces[face]->point[i].x));
				assert(isfinite(faces[face]->point[i].y));
				assert(faces[face]->point[i].y > 60.0f);
				assert(faces[face]->point[i].y < 350.0f);
			}
		}
		for(i = 0; i < 4; ++i) {
			int reflected = i ^ 1;
			closePoint(pose.front.point[i], (uiBoxCasePoint_t) {
				640.0f - mirror.front.point[reflected].x,
				mirror.front.point[reflected].y});
			assert(fabsf(pose.front.point[i].x -
				previous.front.point[i].x) < 0.4f);
			assert(fabsf(pose.front.point[i].y -
				previous.front.point[i].y) < 0.4f);
		}
		previous = pose;
	}
}

static void invalid(void)
{
	uiBoxCasePose_t pose;
	uiBoxCasePose_t before;
	const float bad[] = {NAN, INFINITY, -INFINITY, 3.001f, -3.001f};
	size_t i;

	memset(&pose, 0x5a, sizeof(pose));
	before = pose;
	assert(!UIBoxCase_Pose(0.0f, NULL));
	for(i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		assert(!UIBoxCase_Pose(bad[i], &pose));
		assert(memcmp(&before, &pose, sizeof(pose)) == 0);
	}
}

static void physicalPoints(void)
{
	uiBoxCasePose_t pose;
	uiBoxCasePoint_t point;
	uiBoxCaseQuad_t flat = {{{20.0f, 30.0f}, {220.0f, 30.0f},
		{220.0f, 330.0f}, {20.0f, 330.0f}}};
	int i;

	closePoint(UIBoxCase_Point(&flat, 0.25f, 0.75f),
		(uiBoxCasePoint_t) {70.0f, 255.0f});
	assert(UIBoxCase_Pose(1.0f, &pose));
	/* Independently project the case's physical centre and a point on its
	 * top edge. An affine quad incorrectly puts the texture centre about
	 * 12 pixels from here, on its triangle diagonal. */
	point = UIBoxCase_Point(&pose.front, 0.5f, 0.5f);
	closePoint(point, (uiBoxCasePoint_t) {320.0f + 700.0f * 190.0f / 810.0f,
		216.0f});
	point = UIBoxCase_Point(&pose.front, 0.5f, 0.0f);
	closePoint(point, (uiBoxCasePoint_t) {320.0f + 700.0f * 190.0f / 810.0f,
		216.0f - 700.0f * 120.0f / 810.0f});
	for(i = 0; i < 4; ++i) {
		closePoint(UIBoxCase_Point(&pose.front, i == 1 || i == 2,
			i == 2 || i == 3), pose.front.point[i]);
	}
	/* The entire physical midline is horizontal in this level camera,
	 * including strongly angled neighbours. */
	for(i = -30; i <= 30; ++i) {
		int u;
		assert(UIBoxCase_Pose((float)i / 10.0f, &pose));
		for(u = 0; u <= 10; ++u) {
			point = UIBoxCase_Point(&pose.front, (float)u / 10.0f, 0.5f);
			assert(fabsf(point.y - 216.0f) < 0.001f);
		}
	}
}

static void invalidPoints(void)
{
	uiBoxCasePoint_t zero = {0.0f, 0.0f};
	uiBoxCaseQuad_t collapsed = {{{2.0f, 3.0f}, {6.0f, 3.0f},
		{6.0f, 3.0f}, {2.0f, 3.0f}}};

	closePoint(UIBoxCase_Point(&collapsed, 0.5f, 0.5f),
		(uiBoxCasePoint_t) {4.0f, 3.0f});
	closePoint(UIBoxCase_Point(&collapsed, -1.0f, 2.0f),
		collapsed.point[3]);
	closePoint(UIBoxCase_Point(NULL, 0.5f, 0.5f), zero);
	closePoint(UIBoxCase_Point(&collapsed, NAN, 0.5f), zero);
	closePoint(UIBoxCase_Point(&collapsed, 0.5f, INFINITY), zero);
	collapsed.point[1].x = NAN;
	closePoint(UIBoxCase_Point(&collapsed, 0.5f, 0.5f), zero);
}

int main(void)
{
	centre();
	neighbours();
	sweep();
	invalid();
	physicalPoints();
	invalidPoints();
	puts("ui_box_case: ok");
	return 0;
}
