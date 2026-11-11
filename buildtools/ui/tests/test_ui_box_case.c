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
		const uiBoxCaseQuad_t *faces[2];
		int face;
		int i;

		assert(UIBoxCase_Pose(slot, &pose));
		assert(UIBoxCase_Pose(-slot, &mirror));
		assert(area(&pose.front) > 1000.0f);
		assert(area(&pose.side) >= -0.02f);
		faces[0] = &pose.front;
		faces[1] = &pose.side;
		for(face = 0; face < 2; ++face) {
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

static void preparedStrips(void)
{
	uiBoxCaseQuad_t flat = {{{20.0f, 30.0f}, {220.0f, 30.0f},
		{220.0f, 330.0f}, {20.0f, 330.0f}}};
	uiBoxCasePose_t pose;
	uiBoxCaseStrips_t strips;
	int column;
	int end;
	int side;

	assert(UI_BOX_CASE_STRIPS == 8);
	assert(UIBoxCase_Strips(&flat, &strips));
	for(column = 0; column <= UI_BOX_CASE_STRIPS; ++column) {
		float u = 0.033333f + 0.933334f * (float)column / 8.0f;
		for(end = 0; end < 2; ++end) {
			float v = end ? 0.966667f : 0.033333f;
			closePoint(strips.column[column][end],
				(uiBoxCasePoint_t) {20.0f + 200.0f * u, 30.0f + 300.0f * v});
		}
	}
	for(side = -1; side <= 1; ++side) {
		float yaw = (float)side * 65.0f * 0.01745329252f;
		float centreX = (float)side * 190.0f;
		float depth = side ? 110.0f : 0.0f;

		assert(UIBoxCase_Pose((float)side, &pose));
		assert(UIBoxCase_Strips(&pose.front, &strips));
		for(column = 0; column <= UI_BOX_CASE_STRIPS; ++column) {
			float u = 0.033333f + 0.933334f * (float)column / 8.0f;
			float x = -90.0f + 180.0f * u;
			float z = 700.0f + depth - sinf(yaw) * x;
			for(end = 0; end < 2; ++end) {
				float v = end ? 0.966667f : 0.033333f;
				float y = -120.0f + 240.0f * v;
				/* Independent world-space projection, not the production
				 * height weighting or screen-space interpolation. */
				uiBoxCasePoint_t expected = {
					320.0f + 700.0f * (centreX + cosf(yaw) * x) / z,
					216.0f + 700.0f * y / z
				};
				closePoint(strips.column[column][end], expected);
				assert(column == 0 || strips.column[column][end].x >
					strips.column[column - 1][end].x);
			}
			assert(strips.column[column][0].y < strips.column[column][1].y);
		}
	}
}

/* The diagonals of a projected planar rectangle meet at its physical
 * centre. This geometric construction does not use its height weights. */
static uiBoxCasePoint_t diagonalCentre(const uiBoxCaseQuad_t *quad)
{
	uiBoxCasePoint_t a = quad->point[0];
	uiBoxCasePoint_t b = quad->point[1];
	uiBoxCasePoint_t r = {quad->point[2].x - a.x, quad->point[2].y - a.y};
	uiBoxCasePoint_t s = {quad->point[3].x - b.x, quad->point[3].y - b.y};
	float denominator = r.x * s.y - r.y * s.x;
	float t;
	uiBoxCasePoint_t centre;

	assert(fabsf(denominator) > 1.0f);
	t = ((b.x - a.x) * s.y - (b.y - a.y) * s.x) / denominator;
	centre = (uiBoxCasePoint_t) {a.x + t * r.x, a.y + t * r.y};
	return centre;
}

static void stripSweep(void)
{
	uiBoxCasePose_t pose;
	uiBoxCaseStrips_t previous;
	int step;

	assert(UIBoxCase_Pose(-3.0f, &pose));
	assert(UIBoxCase_Strips(&pose.front, &previous));
	for(step = -300; step <= 300; ++step) {
		uiBoxCasePose_t mirroredPose;
		uiBoxCaseStrips_t strips;
		uiBoxCaseStrips_t mirrored;
		uiBoxCasePoint_t centre;
		uiBoxCasePoint_t middle;
		uiBoxCaseQuad_t artwork;
		float totalArea = 0.0f;
		int column;
		int end;

		assert(UIBoxCase_Pose((float)step / 100.0f, &pose));
		assert(UIBoxCase_Pose(-(float)step / 100.0f, &mirroredPose));
		assert(UIBoxCase_Strips(&pose.front, &strips));
		assert(UIBoxCase_Strips(&mirroredPose.front, &mirrored));
		centre = diagonalCentre(&pose.front);
		middle = (uiBoxCasePoint_t) {
			(strips.column[4][0].x + strips.column[4][1].x) * 0.5f,
			(strips.column[4][0].y + strips.column[4][1].y) * 0.5f
		};
		closePoint(middle, centre);
		for(column = 0; column <= UI_BOX_CASE_STRIPS; ++column) {
			float x = strips.column[column][0].x;
			float across = (x - pose.front.point[0].x) /
				(pose.front.point[1].x - pose.front.point[0].x);
			float top = pose.front.point[0].y + across *
				(pose.front.point[1].y - pose.front.point[0].y);
			float bottom = pose.front.point[3].y + across *
				(pose.front.point[2].y - pose.front.point[3].y);

			/* Every cached column is vertical and strictly inside the
			 * projected front, with no inversion or folded strip. */
			assert(x > pose.front.point[0].x && x < pose.front.point[1].x);
			assert(fabsf(x - strips.column[column][1].x) < 0.001f);
			assert(top < strips.column[column][0].y);
			assert(strips.column[column][0].y < strips.column[column][1].y);
			assert(strips.column[column][1].y < bottom);
			for(end = 0; end < 2; ++end) {
				uiBoxCasePoint_t point = strips.column[column][end];
				uiBoxCasePoint_t reflected =
					mirrored.column[UI_BOX_CASE_STRIPS - column][end];
				assert(isfinite(point.x) && isfinite(point.y));
				closePoint(point, (uiBoxCasePoint_t) {640.0f - reflected.x,
					reflected.y});
				assert(fabsf(point.x - previous.column[column][end].x) < 5.0f);
				assert(fabsf(point.y - previous.column[column][end].y) < 5.0f);
			}
			if(column < UI_BOX_CASE_STRIPS) {
				uiBoxCaseQuad_t strip = {{strips.column[column][0],
					strips.column[column + 1][0], strips.column[column + 1][1],
					strips.column[column][1]}};
				assert(area(&strip) > 0.0f);
				totalArea += area(&strip);
			}
		}
		/* Shared column seams cover precisely the inset front once. */
		artwork = (uiBoxCaseQuad_t) {{strips.column[0][0],
			strips.column[UI_BOX_CASE_STRIPS][0],
			strips.column[UI_BOX_CASE_STRIPS][1], strips.column[0][1]}};
		assert(fabsf(totalArea - area(&artwork)) < 0.25f);
		previous = strips;
	}
}

static void invalidStrips(void)
{
	uiBoxCaseStrips_t strips;
	uiBoxCaseStrips_t before;
	uiBoxCaseQuad_t collapsed = {{{2.0f, 3.0f}, {6.0f, 3.0f},
		{6.0f, 3.0f}, {2.0f, 3.0f}}};
	const float bad[] = {NAN, INFINITY, -INFINITY};
	size_t i;
	int column;
	int corner;
	int coordinate;

	assert(UIBoxCase_Strips(&collapsed, &strips));
	for(column = 0; column <= UI_BOX_CASE_STRIPS; ++column) {
		float u = 0.033333f + 0.933334f * (float)column / 8.0f;
		closePoint(strips.column[column][0], (uiBoxCasePoint_t) {2.0f + 4.0f * u, 3.0f});
		closePoint(strips.column[column][0], strips.column[column][1]);
	}
	memset(&strips, 0x5a, sizeof(strips));
	before = strips;
	assert(!UIBoxCase_Strips(NULL, &strips));
	assert(memcmp(&before, &strips, sizeof(strips)) == 0);
	assert(!UIBoxCase_Strips(&collapsed, NULL));
	for(corner = 0; corner < 4; ++corner) {
		for(coordinate = 0; coordinate < 2; ++coordinate) {
			for(i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
				uiBoxCaseQuad_t invalid = collapsed;
				if(coordinate) invalid.point[corner].y = bad[i];
				else invalid.point[corner].x = bad[i];
				assert(!UIBoxCase_Strips(&invalid, &strips));
				assert(memcmp(&before, &strips, sizeof(strips)) == 0);
			}
		}
	}
}

int main(void)
{
	centre();
	neighbours();
	sweep();
	invalid();
	preparedStrips();
	stripSweep();
	invalidStrips();
	puts("ui_box_case: ok");
	return 0;
}
