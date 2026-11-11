#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_scene.h"

static unsigned checks;
static const uiHomeCapabilities_t caps = {.hasSource = true, .hasRecent = true,
	.hasApps = false, .style = UI_HOME_CUBE_INFINITE};
/* With an app on the source: a ring of five faces. */
static const uiHomeCapabilities_t appsCaps = {.hasSource = true, .hasRecent = true,
	.hasApps = true, .style = UI_HOME_CUBE_INFINITE};
/* Setup > Console > Cube set to Classic, with Apps: every side has a face. */
static const uiHomeCapabilities_t classicCaps = {.hasSource = true, .hasRecent = true,
	.hasApps = true, .style = UI_HOME_CUBE_CLASSIC};
static const float identity[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

#define CHECK(c) do { ++checks; if(!(c)) { \
 fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)
static void near(float actual, float expected, float tolerance)
{
	CHECK(isfinite(actual)); CHECK(fabsf(actual - expected) <= tolerance);
}
static void matrixNear(const float *actual, const float *expected, float tolerance)
{
	for(int i = 0; i < 9; ++i) near(actual[i], expected[i], tolerance);
}
static void checkRotation(const float *m)
{
	for(int row = 0; row < 3; ++row) for(int other = 0; other < 3; ++other) {
		float dot = 0.0f;
		for(int k = 0; k < 3; ++k) dot += m[row*3+k] * m[other*3+k];
		near(dot, row == other ? 1.0f : 0.0f, 0.00002f);
	}
	float det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) +
		m[2]*(m[3]*m[7]-m[4]*m[6]);
	near(det, 1.0f, 0.00002f);
}
static void tick(float dt, uiMotionMode_t mode)
{
	UIScene_Update(dt, mode);
	const uiSceneFrame_t *f = UIScene_Frame();
	checkRotation(&f->homeOrientation[0][0]);
	checkRotation(&f->homeTargetOrientation[0][0]);
	CHECK(f->homeFocusProgress >= 0.0f && f->homeFocusProgress <= 1.0f);
	for(int face = 0; face < UI_HOME_FACE_COUNT; ++face)
		CHECK(f->homeMotifAlpha[face] >= 0.0f && f->homeMotifAlpha[face] <= 1.0f);
	CHECK(isfinite(f->cubeYaw) && isfinite(f->cubePitch));
}
static void advance(float seconds, float dt, uiMotionMode_t mode)
{
	for(int frame = 0; frame < (int)ceilf(seconds/dt); ++frame) tick(dt, mode);
}
static void settle(float dt, uiMotionMode_t mode)
{
	advance(2.8f, dt, mode);
	CHECK(!UIScene_Frame()->transitioning);
	near(UIScene_Frame()->homeFocusProgress, 1.0f, 0.00001f);
}
static uiHomeState_t startWith(uiHomeCapabilities_t with, float dt,
	uiMotionMode_t mode)
{
	uiHomeState_t home;
	UIHome_Init(&home, with);
	UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
	advance(4.0f, dt, mode);
	CHECK(UIScene_Frame()->scene == UI_SCENE_HOME);
	CHECK(UIScene_Frame()->visible && !UIScene_Frame()->transitioning);
	near(UIScene_Frame()->cubeYaw, 0.28f, 0.0001f);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.0f);
	return home;
}
static uiHomeState_t start(float dt, uiMotionMode_t mode)
{
	return startWith(caps, dt, mode);
}
static void turn(uiHomeState_t *home, uiHomeInput_t input)
{
	CHECK(UIHome_Apply(home, input, caps) == UI_HOME_EFFECT_NONE);
	UIScene_RequestHome(home);
}
/* Independent full rotation-product oracle, never calling production helpers. */
static void oracleTurn(float *m, uiHomeInput_t input)
{
	float r[9] = {1,0,0,0,1,0,0,0,1}, before[9];
	int step = input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP ? -1 : 1;
	if(input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_RIGHT) {
		r[0] = r[8] = 0; r[2] = (float)-step; r[6] = (float)step;
	} else {
		r[4] = r[8] = 0; r[5] = (float)step; r[7] = (float)-step;
	}
	memcpy(before, m, sizeof(before));
	for(int row = 0; row < 3; ++row) for(int col = 0; col < 3; ++col) {
		m[row*3+col] = 0.0f;
		for(int k = 0; k < 3; ++k) m[row*3+col] += r[row*3+k] * before[k*3+col];
	}
}
static void testPhysicalAxesAndArrival(void)
{
	for(int hz = 50; hz <= 60; hz += 10) for(int input = 1; input <= 4; ++input) {
		float dt = 1.0f/(float)hz, expected[9];
		memcpy(expected, identity, sizeof(expected));
		uiHomeState_t home = start(dt, UI_MOTION_FULL);
		turn(&home, (uiHomeInput_t)input); oracleTurn(expected, (uiHomeInput_t)input);
		tick(0.0f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.000001f);
		matrixNear(&UIScene_Frame()->homeTargetOrientation[0][0], expected, 0.0f);
		advance(0.10f, dt, UI_MOTION_FULL);
		const uiSceneFrame_t *f = UIScene_Frame();
		/* Transform front normal (0,0,1): vertical commands move it in Y,
		 * horizontal commands in X, with no unwanted cross-axis movement. */
		int step = input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP ? -1 : 1;
		if(input <= UI_HOME_INPUT_RIGHT) {
			CHECK(f->homeTurnAxis == UI_HOME_TURN_HORIZONTAL);
			CHECK(f->homeOrientation[0][2] * (float)-step > 0.05f);
			near(f->homeOrientation[1][2], 0.0f, 0.000001f);
		} else {
			CHECK(f->homeTurnAxis == UI_HOME_TURN_VERTICAL);
			CHECK(f->homeOrientation[1][2] * (float)step > 0.05f);
			near(f->homeOrientation[0][2], 0.0f, 0.000001f);
		}
		advance(0.30f, dt, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeFocusProgress > 0.85f);
		settle(dt, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
		near(UIScene_Frame()->cubeYaw, 0.28f, 0.00001f);
		CHECK(UIScene_Frame()->homeFace == home.face);
		CHECK(UIScene_Frame()->homeRevision == home.revision);
		CHECK(UIScene_Frame()->homeTurnDirection == step);
	}
}
static void testHalfTurnDirection(void)
{
	for(int input = 1; input <= 4; ++input) {
		uiHomeState_t home = start(0.02f, UI_MOTION_FULL);
		turn(&home, (uiHomeInput_t)input); turn(&home, (uiHomeInput_t)input);
		advance(0.16f, 0.02f, UI_MOTION_FULL);
		int step = input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP ? -1 : 1;
		const float (*m)[3] = UIScene_Frame()->homeOrientation;
		if(input <= UI_HOME_INPUT_RIGHT) CHECK(m[0][2] * (float)-step > 0.2f);
		else CHECK(m[1][2] * (float)step > 0.2f);
		settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_SETTINGS);
	}
}
static void testHalfTurnsFromEveryOrientation(void)
{
	uiHomeState_t starts[24]; unsigned count = 1;
	UIHome_Init(&starts[0], caps);
	for(unsigned head = 0; head < count; ++head) {
		for(int input = 1; input <= 4; ++input) {
			uiHomeState_t next = starts[head];
			UIHome_Apply(&next, (uiHomeInput_t)input, caps);
			unsigned found;
			for(found = 0; found < count; ++found)
				if(memcmp(&starts[found].orientation, &next.orientation,
					sizeof(next.orientation)) == 0) break;
			if(found == count) { CHECK(count < 24); starts[count++] = next; }
		}
	}
	CHECK(count == 24);
	for(unsigned initial = 0; initial < count; ++initial) {
		for(int input = 1; input <= 4; ++input) {
			uiHomeState_t home = starts[initial];
			UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
			tick(0.0f, UI_MOTION_OFF);
			float before[9], expected[9], relative[9];
			memcpy(before, UIScene_Frame()->homeOrientation, sizeof(before));
			memcpy(expected, before, sizeof(expected));
			turn(&home, (uiHomeInput_t)input); turn(&home, (uiHomeInput_t)input);
			oracleTurn(expected, (uiHomeInput_t)input);
			oracleTurn(expected, (uiHomeInput_t)input);
			advance(0.12f, 0.02f, UI_MOTION_FULL);
			const float *current = &UIScene_Frame()->homeOrientation[0][0];
			/* Relative screen rotation = current * transpose(start). */
			for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) {
				relative[r*3+c] = 0.0f;
				for(int k = 0; k < 3; ++k)
					relative[r*3+c] += current[r*3+k] * before[c*3+k];
			}
			int step = input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP ? -1 : 1;
			if(input <= UI_HOME_INPUT_RIGHT) {
				CHECK(relative[2] * (float)-step > 0.1f);
				near(relative[5], 0.0f, 0.00001f);
			} else {
				CHECK(relative[5] * (float)step > 0.1f);
				near(relative[2], 0.0f, 0.00001f);
			}
			settle(0.02f, UI_MOTION_FULL);
			matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
		}
	}
}

static void testMixedCoalescingAndInterruptions(void)
{
	static const uiHomeInput_t route[] = {UI_HOME_INPUT_UP, UI_HOME_INPUT_RIGHT,
		UI_HOME_INPUT_DOWN, UI_HOME_INPUT_LEFT, UI_HOME_INPUT_RIGHT, UI_HOME_INPUT_UP};
	for(int mode = UI_MOTION_FULL; mode <= UI_MOTION_OFF; ++mode) {
		uiHomeState_t home = start(1.0f/60.0f, (uiMotionMode_t)mode);
		float expected[9]; memcpy(expected, identity, sizeof(expected));
		for(int repeat = 0; repeat < 32; ++repeat) {
			for(unsigned i = 0; i < sizeof(route)/sizeof(route[0]); ++i) {
				float before[9]; memcpy(before, UIScene_Frame()->homeOrientation, sizeof(before));
				turn(&home, route[i]); oracleTurn(expected, route[i]);
				/* Coalesce two mixed updates without a video frame. */
				if((i & 1u) == 0u) continue;
				tick(0.0f, (uiMotionMode_t)mode);
				matrixNear(&UIScene_Frame()->homeTargetOrientation[0][0], expected, 0.0f);
				if(mode != UI_MOTION_OFF)
					matrixNear(&UIScene_Frame()->homeOrientation[0][0], before, 0.000002f);
				else matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
				advance(0.04f, 1.0f/60.0f, (uiMotionMode_t)mode);
			}
		}
		settle(1.0f/60.0f, (uiMotionMode_t)mode);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
	}
	/* Noncommuting routes have the same semantic menu but different bodies. */
	float upRight[9], rightUp[9]; memcpy(upRight, identity, sizeof(upRight));
	memcpy(rightUp, identity, sizeof(rightUp));
	oracleTurn(upRight, UI_HOME_INPUT_UP); oracleTurn(upRight, UI_HOME_INPUT_RIGHT);
	oracleTurn(rightUp, UI_HOME_INPUT_RIGHT); oracleTurn(rightUp, UI_HOME_INPUT_UP);
	CHECK(memcmp(upRight, rightUp, sizeof(upRight)) != 0);
}
static void testModeChangesAndBadTiming(void)
{
	uiHomeState_t home = start(0.02f, UI_MOTION_FULL);
	turn(&home, UI_HOME_INPUT_UP); advance(0.08f, 0.02f, UI_MOTION_FULL);
	float before[9]; memcpy(before, UIScene_Frame()->homeOrientation, sizeof(before));
	tick(0.0f, UI_MOTION_REDUCED);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], before, 0.000002f);
	turn(&home, UI_HOME_INPUT_RIGHT); tick(0.0f, UI_MOTION_REDUCED);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], before, 0.000002f);
	tick(NAN, UI_MOTION_FULL); tick(-2.0f, UI_MOTION_FULL);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], before, 0.000002f);
	tick(0.0f, UI_MOTION_OFF);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0],
		&UIScene_Frame()->homeTargetOrientation[0][0], 0.0f);
	CHECK(!UIScene_Frame()->transitioning);
	/* Every face of the ring lit; Apps, outside it, not. */
	for(int face = 0; face < UI_HOME_FACE_APPS; ++face)
		near(UIScene_Frame()->homeMotifAlpha[face], 1.0f, 0.0f);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_APPS], 0.0f, 0.0f);
	tick(1.0f/60.0f, UI_MOTION_FULL);
	CHECK(!UIScene_Frame()->transitioning);
	for(int i = 0; i < 100; ++i) {
		turn(&home, i & 1 ? UI_HOME_INPUT_LEFT : UI_HOME_INPUT_DOWN);
		tick(i & 1 ? 100.0f : 0.002f, UI_MOTION_FULL);
	}
	settle(0.02f, UI_MOTION_FULL);
}
static void testContextAndSceneReturns(void)
{
	uiHomeState_t home = start(0.02f, UI_MOTION_FULL);
	turn(&home, UI_HOME_INPUT_DOWN); settle(0.02f, UI_MOTION_FULL);
	CHECK(home.face == UI_HOME_FACE_SOURCE);
	float target[9]; memcpy(target, UIScene_Frame()->homeOrientation, sizeof(target));
	/* The Source face's short list is Home's: its rows tilt the cube. */
	UIHome_Apply(&home, UI_HOME_INPUT_ACTIVATE, caps); UIScene_RequestHome(&home);
	settle(0.02f, UI_MOTION_FULL);
	float rowY = UIScene_Frame()->cubeY, rowPitch = UIScene_Frame()->cubePitch;
	float rowScale = UIScene_Frame()->cubeScale;
	UIHome_Apply(&home, UI_HOME_INPUT_DOWN, caps); UIScene_RequestHome(&home);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeSelection == 1);
	CHECK(UIScene_Frame()->cubeY < rowY && UIScene_Frame()->cubePitch > rowPitch);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], target, 0.0f);
	/* Change Source's picker lifts the cube, smaller, above its row of
	 * devices, still on the Source face; leaving it puts the cube back. */
	UIHome_Apply(&home, UI_HOME_INPUT_UP, caps); UIScene_RequestHome(&home);
	UIScene_Request(UI_SCENE_SOURCE); settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->cubeY > rowY + 0.5f);
	CHECK(UIScene_Frame()->cubeScale < rowScale * 0.6f);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], target, 0.0f);
	/* Resting there is not Home's rest: no idle motion (Sway's included). */
	advance(2.0f, 0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->homeIdleBlend, 0.0f, 0.0001f);
	UIScene_Request(UI_SCENE_HOME); settle(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->cubeY, rowY, 0.0001f);
	near(UIScene_Frame()->cubeScale, rowScale, 0.0001f);
	advance(2.0f, 0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->homeIdleBlend, 1.0f, 0.0001f);
	UIHome_Apply(&home, UI_HOME_INPUT_BACK, caps); UIScene_RequestHome(&home);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeSurface == UI_HOME_SURFACE_RING);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], target, 0.0f);
	for(int destination = UI_SCENE_LIBRARY; destination < UI_SCENE_COUNT; ++destination) {
		UIScene_Request((uiSceneId_t)destination); tick(0.0f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], target, 0.000002f);
		matrixNear(&UIScene_Frame()->homeTargetOrientation[0][0], &identity[0][0], 0.0f);
		settle(0.02f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.0f);
		near(UIScene_Frame()->homeIdleBlend, 0.0f, 0.0001f);
		UIScene_Request(UI_SCENE_HOME); tick(0.0f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.000002f);
		settle(0.02f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], target, 0.0f);
	}
}
static void testPublicationAndInvalidRequests(void)
{
	uiHomeState_t home;
	UIHome_Init(&home, caps); UIHome_Apply(&home, UI_HOME_INPUT_UP, caps);
	UIScene_Reset(); UIScene_RequestHome(&home); tick(0.02f, UI_MOTION_FULL);
	CHECK(!UIScene_Frame()->visible);
	UIScene_Activate(); settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_SYSTEM);
	for(int which = 0; which < 16; ++which) {
		uiHomeState_t invalid = home;
		switch(which) {
		case 0: invalid.face = (uiHomeFace_t)-1; break;
		case 1: invalid.turnOrdinal = 2; break;
		case 2: invalid.surface = UI_HOME_SURFACE_SOURCE; break;
		case 3: invalid.selection = 1; break;
		case 4: invalid.orientation.m[0][0] = 2; break;
		case 5: invalid.turnAxis = (uiHomeTurnAxis_t)99; break;
		case 6: invalid.turnDirection = 0; break;
		/* A count that isn't the sides' ring, Apps in front of a ring
		 * without it, and an ordinal that lands elsewhere in a ring of
		 * five. */
		case 7: invalid.faceCount = 3; break;
		case 8: invalid.faceCount = 6; break;
		case 9: invalid.face = UI_HOME_FACE_APPS; invalid.turnOrdinal = 4; break;
		case 10: invalid.faceCount = 5; break;
		/* A face on two sides, an absent face on none, a side naming no
		 * face. */
		case 12: invalid.sides[UI_HOME_SIDE_DOWN] = UI_HOME_FACE_SOURCE; break;
		case 13: invalid.sides[UI_HOME_SIDE_DOWN] = UI_HOME_FACE_LIBRARY; break;
		case 14: invalid.sides[UI_HOME_SIDE_UP] = UI_HOME_FACE_COUNT; break;
		/* A cube that is neither Infinite nor Classic. */
		case 11: invalid.style = UI_HOME_CUBE_COUNT; break;
		default: invalid.surface = UI_HOME_SURFACE_COUNT; break;
		}
		invalid.revision++;
		UIScene_RequestHome(&invalid); tick(0.02f, UI_MOTION_OFF);
		CHECK(UIScene_Frame()->homeRevision == home.revision);
	}
	UIScene_RequestHome(NULL); UIScene_Request((uiSceneId_t)99);
	tick(0.02f, UI_MOTION_OFF);
	CHECK(UIScene_Frame()->scene == UI_SCENE_HOME);
}
/* System's four rows each publish, Restart's (the last) included, and its
 * cube stays up above them all, only tilting: they start a row higher. */
static void testSystemRows(void)
{
	uiHomeState_t home;
	uiHomeState_t invalid;
	float rowY;
	float rowPitch;

	UIHome_Init(&home, caps); UIHome_Apply(&home, UI_HOME_INPUT_UP, caps);
	UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
	UIHome_Apply(&home, UI_HOME_INPUT_ACTIVATE, caps);
	UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeSurface == UI_HOME_SURFACE_SYSTEM);
	rowY = UIScene_Frame()->cubeY;
	rowPitch = UIScene_Frame()->cubePitch;
	for(int row = 1; row < 4; ++row) {
		UIHome_Apply(&home, UI_HOME_INPUT_DOWN, caps);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeSelection == row);
		near(UIScene_Frame()->cubeY, rowY, 0.0001f);
		CHECK(UIScene_Frame()->cubePitch > rowPitch);
	}
	invalid = home; invalid.selection = 4; invalid.revision++;
	UIScene_RequestHome(&invalid); tick(0.02f, UI_MOTION_OFF);
	CHECK(UIScene_Frame()->homeSelection == 3);
}
/* Apps' face is published with its ring: the frame names it, and the
 * glyphs are placed for five faces, then four again when the apps go. */
static void testAppsRing(void)
{
	uiHomeState_t home;

	UIHome_Init(&home, appsCaps);
	UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
	settle(0.02f, UI_MOTION_FULL);
	/* Library in front: Apps lit on the side before it, Settings (two
	 * ahead) with no side of its own. */
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_APPS], 1.0f, 0.0f);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SETTINGS], 0.0f, 0.0f);
	CHECK(UIHome_Apply(&home, UI_HOME_INPUT_LEFT, appsCaps) ==
		UI_HOME_EFFECT_NONE);
	UIScene_RequestHome(&home);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_APPS);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_APPS], 1.0f, 0.0f);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SOURCE], 0.0f, 0.0f);
	CHECK(!UIScene_Frame()->transitioning);
	(void)UIHome_Apply(&home, UI_HOME_INPUT_NONE, caps);
	UIScene_RequestHome(&home);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_LIBRARY);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_APPS], 0.0f, 0.0f);
	near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SOURCE], 1.0f, 0.0f);
}

/* How far a quarter turn has come seconds after it was asked for. */
static float turnedAfter(uiHomeCapabilities_t with, uiMotionMode_t mode,
	float seconds)
{
	uiHomeState_t home = startWith(with, 1.0f/60.0f, mode);
	CHECK(UIHome_Apply(&home, UI_HOME_INPUT_LEFT, with) == UI_HOME_EFFECT_NONE);
	UIScene_RequestHome(&home);
	advance(seconds, 1.0f/60.0f, mode);
	return UIScene_Frame()->homeFocusProgress;
}

/* Classic: its turns arrive as the GameCube's do, nine tenths of the way in
 * a quarter of a second, where Infinite's keep their pace; Reduced is
 * faster still. Its style travels with the state, and is checked. Every
 * glyph keeps its side through every turn and every scene, so none fades. */
static void testClassicCube(void)
{
	uiHomeCapabilities_t infinite = classicCaps;
	infinite.style = UI_HOME_CUBE_INFINITE;
	CHECK(turnedAfter(classicCaps, UI_MOTION_FULL, 0.25f) > 0.88f);
	CHECK(turnedAfter(infinite, UI_MOTION_FULL, 0.25f) < 0.80f);
	CHECK(turnedAfter(classicCaps, UI_MOTION_FULL, 0.15f) < 0.80f);
	CHECK(turnedAfter(classicCaps, UI_MOTION_REDUCED, 0.15f) > 0.88f);
	CHECK(turnedAfter(infinite, UI_MOTION_REDUCED, 0.15f) < 0.80f);

	/* Every face from Library and back: the target is the face's own side,
	 * and no glyph moves or fades on the way. */
	static const uiHomeInput_t out[4] = {UI_HOME_INPUT_LEFT, UI_HOME_INPUT_RIGHT,
		UI_HOME_INPUT_UP, UI_HOME_INPUT_DOWN};
	uiHomeState_t home = startWith(classicCaps, 0.02f, UI_MOTION_FULL);
	float basis[UI_HOME_FACE_COUNT][3][3];
	memcpy(basis, UIScene_Frame()->homeMotifBasis, sizeof(basis));
	for(int i = 0; i < 4; ++i) {
		float expected[9];
		memcpy(expected, identity, sizeof(expected));
		oracleTurn(expected, out[i]);
		CHECK(UIHome_Apply(&home, out[i], classicCaps) == UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home);
		for(int frame = 0; frame < 60; ++frame) {
			tick(0.02f, UI_MOTION_FULL);
			for(int face = 0; face < UI_HOME_FACE_COUNT; ++face)
				near(UIScene_Frame()->homeMotifAlpha[face],
					face <= UI_HOME_FACE_APPS ? 1.0f : 0.0f, 0.0f);
			CHECK(memcmp(basis, UIScene_Frame()->homeMotifBasis, sizeof(basis)) == 0);
		}
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
		CHECK(UIScene_Frame()->homeFace == home.face);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_BACK, classicCaps) == UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home);
		settle(0.02f, UI_MOTION_FULL);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.0f);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_LIBRARY);
	}
	/* Leaving Home for Settings from its face, and coming back: the cube
	 * turns to Library and back, and still no glyph moves. */
	CHECK(UIHome_Apply(&home, UI_HOME_INPUT_LEFT, classicCaps) == UI_HOME_EFFECT_NONE);
	UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
	static const uiSceneId_t away[2] = {UI_SCENE_SETTINGS, UI_SCENE_HOME};
	for(int i = 0; i < 2; ++i) {
		UIScene_Request(away[i]);
		for(int frame = 0; frame < 120; ++frame) {
			tick(0.02f, UI_MOTION_FULL);
			for(int face = 0; face < UI_HOME_FACE_COUNT; ++face)
				near(UIScene_Frame()->homeMotifAlpha[face],
					face <= UI_HOME_FACE_APPS ? 1.0f : 0.0f, 0.0f);
			CHECK(memcmp(basis, UIScene_Frame()->homeMotifBasis, sizeof(basis)) == 0);
		}
	}

	/* The style alone changing, Library in front either way, reaches the
	 * scene: Source's glyph goes from the right side to the top. */
	uiHomeCapabilities_t classicFour = caps;
	classicFour.style = UI_HOME_CUBE_CLASSIC;
	home = start(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SOURCE][0][2], 1.0f, 0.0f);
	uint32_t revision = home.revision;
	(void)UIHome_Apply(&home, UI_HOME_INPUT_NONE, classicFour);
	CHECK(home.revision == revision);
	UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SOURCE][1][2], 1.0f, 0.0f);
	near(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SETTINGS][0][2], -1.0f, 0.0f);
	matrixNear(&UIScene_Frame()->homeOrientation[0][0], &identity[0][0], 0.0f);
}

/* Faces on other sides travel with the state. Classic puts each glyph on its
 * face's side and turns there; Infinite turns round the faces on a side, in
 * the order Up, Left, Right, Down, and a face on none has no glyph. */
static void testCustomSides(void)
{
	/* The issue's cube from today's faces: Settings down, Apps left. */
	uiHomeCapabilities_t moved = classicCaps;
	uiHomeState_t home;
	float defaults[UI_HOME_FACE_COUNT][3][3];

	moved.customSides = true;
	moved.sides[UI_HOME_SIDE_UP] = UI_HOME_FACE_SOURCE;
	moved.sides[UI_HOME_SIDE_LEFT] = UI_HOME_FACE_APPS;
	moved.sides[UI_HOME_SIDE_RIGHT] = UI_HOME_FACE_SYSTEM;
	moved.sides[UI_HOME_SIDE_DOWN] = UI_HOME_FACE_SETTINGS;
	(void)startWith(classicCaps, 0.02f, UI_MOTION_FULL);
	memcpy(defaults, UIScene_Frame()->homeMotifBasis, sizeof(defaults));
	home = startWith(moved, 0.02f, UI_MOTION_FULL);
	/* Each glyph where today's face on that side has its own. */
	CHECK(memcmp(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_APPS],
		defaults[UI_HOME_FACE_SETTINGS], sizeof(defaults[0])) == 0);
	CHECK(memcmp(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SETTINGS],
		defaults[UI_HOME_FACE_APPS], sizeof(defaults[0])) == 0);
	CHECK(memcmp(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SOURCE],
		defaults[UI_HOME_FACE_SOURCE], sizeof(defaults[0])) == 0);
	CHECK(memcmp(defaults[UI_HOME_FACE_APPS], defaults[UI_HOME_FACE_SETTINGS],
		sizeof(defaults[0])) != 0);
	/* The sides alone changing, Library in front, reaches the scene: no
	 * other published field moves, the revision included. */
	{
		uiHomeState_t before = startWith(classicCaps, 0.02f, UI_MOTION_FULL);

		home = before;
		(void)UIHome_Apply(&home, UI_HOME_INPUT_NONE, moved);
		CHECK(home.face == before.face && home.revision == before.revision &&
			home.turnOrdinal == before.turnOrdinal &&
			home.faceCount == before.faceCount &&
			memcmp(&home.orientation, &before.orientation,
			sizeof(home.orientation)) == 0);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		CHECK(memcmp(UIScene_Frame()->homeMotifBasis[UI_HOME_FACE_SETTINGS],
			defaults[UI_HOME_FACE_APPS], sizeof(defaults[0])) == 0);
	}
	/* Down turns down to Settings. */
	{
		float expected[9];

		memcpy(expected, identity, sizeof(expected));
		oracleTurn(expected, UI_HOME_INPUT_DOWN);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_DOWN, moved) ==
			UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_SETTINGS);
		matrixNear(&UIScene_Frame()->homeOrientation[0][0], expected, 0.0f);
	}

	/* Infinite with no Source or System: Library, Settings and Apps. */
	{
		uiHomeCapabilities_t three = appsCaps;

		three.customSides = true;
		three.sides[UI_HOME_SIDE_LEFT] = UI_HOME_FACE_SETTINGS;
		three.sides[UI_HOME_SIDE_DOWN] = UI_HOME_FACE_APPS;
		home = startWith(three, 0.02f, UI_MOTION_FULL);
		CHECK(home.faceCount == 3);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SOURCE], 0.0f, 0.0f);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SYSTEM], 0.0f, 0.0f);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SETTINGS], 1.0f, 0.0f);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_APPS], 1.0f, 0.0f);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_RIGHT, three) ==
			UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_SETTINGS);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_RIGHT, three) ==
			UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_APPS);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SOURCE], 0.0f, 0.0f);
	}

	/* Away from Home, Infinite shows Library in front of its own ring, not
	 * the default four: Source, on no side, stays off the cube. */
	{
		uiHomeCapabilities_t noSource = caps;

		noSource.customSides = true;
		noSource.sides[UI_HOME_SIDE_LEFT] = UI_HOME_FACE_SETTINGS;
		noSource.sides[UI_HOME_SIDE_RIGHT] = UI_HOME_FACE_SYSTEM;
		noSource.sides[UI_HOME_SIDE_DOWN] = UI_HOME_FACE_APPS;
		home = startWith(noSource, 0.02f, UI_MOTION_FULL);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_RIGHT, noSource) ==
			UI_HOME_EFFECT_NONE);
		UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
		static const uiSceneId_t away[] = {UI_SCENE_LIBRARY, UI_SCENE_SETTINGS};
		for(int i = 0; i < 2; ++i) {
			UIScene_Request(away[i]);
			advance(4.0f, 0.02f, UI_MOTION_FULL);
			near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SOURCE], 0.0f, 0.0f);
			near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_LIBRARY], 1.0f, 0.0f);
			near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SETTINGS], 1.0f, 0.0f);
			near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_SYSTEM], 1.0f, 0.0f);
		}
	}

	/* File Browser in place of each side's face, Classic and Infinite: the
	 * scene takes every snapshot that turns to it, shows its glyph and not
	 * the face it replaced, and is back on it after Swiss's list (the
	 * Library scene) hands Home back. */
	for(int isClassic = 0; isClassic < 2; ++isClassic)
	for(int side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		static const uiHomeInput_t toward[UI_HOME_SIDE_COUNT] = {
			UI_HOME_INPUT_UP, UI_HOME_INPUT_LEFT, UI_HOME_INPUT_RIGHT,
			UI_HOME_INPUT_DOWN
		};
		uiHomeCapabilities_t files = isClassic ? classicCaps : caps;
		int turns = 0;

		files.customSides = true;
		for(int s = 0; s < UI_HOME_SIDE_COUNT; ++s)
			files.sides[s] = (uint8_t)(s + 1);
		files.sides[side] = UI_HOME_FACE_FILES;
		home = startWith(files, 0.02f, UI_MOTION_FULL);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_FILES], 1.0f, 0.0f);
		near(UIScene_Frame()->homeMotifAlpha[side + 1], 0.0f, 0.0f);
		while(home.face != UI_HOME_FACE_FILES && turns++ < UI_HOME_RING_MAX) {
			CHECK(UIHome_Apply(&home, isClassic ? toward[side] :
				UI_HOME_INPUT_RIGHT, files) == UI_HOME_EFFECT_NONE);
			UIScene_RequestHome(&home); settle(0.02f, UI_MOTION_FULL);
			CHECK(UIScene_Frame()->homeFace == home.face);
		}
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_FILES);
		CHECK(UIScene_Frame()->homeRevision == home.revision);
		CHECK(UIHome_Apply(&home, UI_HOME_INPUT_ACTIVATE, files) ==
			UI_HOME_EFFECT_OPEN_FILES);
		UIScene_Request(UI_SCENE_LIBRARY);
		advance(4.0f, 0.02f, UI_MOTION_FULL);
		UIScene_Request(UI_SCENE_HOME); UIScene_RequestHome(&home);
		settle(0.02f, UI_MOTION_FULL);
		CHECK(UIScene_Frame()->scene == UI_SCENE_HOME);
		CHECK(UIScene_Frame()->homeFace == UI_HOME_FACE_FILES);
		near(UIScene_Frame()->homeMotifAlpha[UI_HOME_FACE_FILES], 1.0f, 0.0f);
	}
}

/* Exercise visible composition, not just arrival: posters remain transparent
 * while the cube is Home-sized and only become opaque once it has retreated. */
static void testLibraryRetreatReveal(void)
{
	for(int hz = 50; hz <= 60; hz += 10) for(int mode = UI_MOTION_FULL;
		mode <= UI_MOTION_OFF; mode++) {
		float dt = 1.0f / (float)hz;
		start(dt, (uiMotionMode_t)mode);
		UIScene_Request(UI_SCENE_LIBRARY);
		tick(0.0f, (uiMotionMode_t)mode);
		near(UIScene_Frame()->libraryReveal, mode == UI_MOTION_OFF ? 1.0f : 0.0f, 0.0f);
		float previous = 0.0f;
		for(int frame = 0; frame < hz; frame++) {
			tick(dt, (uiMotionMode_t)mode);
			const uiSceneFrame_t *f = UIScene_Frame();
			CHECK(f->libraryReveal >= previous && f->libraryReveal <= 1.0f);
			if(f->cubeScale >= 0.88f) near(f->libraryReveal, 0.0f, 0.0f);
			if(f->libraryReveal == 1.0f) CHECK(f->cubeScale <= 0.640001f);
			previous = f->libraryReveal;
		}
		near(previous, 1.0f, 0.0f);
		UIScene_Request(UI_SCENE_GAME_DETAIL);
		for(int frame = 0; frame < hz; frame++) {
			tick(dt, (uiMotionMode_t)mode);
			near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
		}
		UIScene_Request(UI_SCENE_HOME);
		tick(dt, (uiMotionMode_t)mode);
		near(UIScene_Frame()->libraryReveal, 0.0f, 0.0f);
		advance(0.15f, dt, (uiMotionMode_t)mode);
		float scale = UIScene_Frame()->cubeScale;
		UIScene_Request(UI_SCENE_LIBRARY);
		tick(0.0f, (uiMotionMode_t)mode);
		if(mode != UI_MOTION_OFF) near(UIScene_Frame()->cubeScale, scale, 0.0f);
		settle(dt, (uiMotionMode_t)mode);
		near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	}
}

/* Home and Source quiet the waves (indigo_background.c scales them by
 * HOME_DECORATIVE_STRENGTH). The quieting eases with the cube rather than
 * stepping on the frame the scene changes; Source is Home's own, and Off
 * still changes at once. */
static void testHomeDecorativeBlend(void)
{
	for(int hz = 50; hz <= 60; hz += 10) {
		float dt = 1.0f / (float)hz;
		float previous;

		start(dt, UI_MOTION_FULL);
		near(UIScene_Frame()->homeDecorativeBlend, 1.0f, 0.0001f);
		UIScene_Request(UI_SCENE_SOURCE);
		advance(1.0f, dt, UI_MOTION_FULL);
		near(UIScene_Frame()->homeDecorativeBlend, 1.0f, 0.0001f);
		UIScene_Request(UI_SCENE_LIBRARY);
		previous = 1.0f;
		for(int frame = 0; frame < 3 * hz; frame++) {
			tick(dt, UI_MOTION_FULL);
			float blend = UIScene_Frame()->homeDecorativeBlend;
			CHECK(blend <= previous && previous - blend < 0.06f);
			previous = blend;
		}
		near(previous, 0.0f, 0.001f);
		UIScene_Request(UI_SCENE_HOME);
		for(int frame = 0; frame < 3 * hz; frame++) {
			tick(dt, UI_MOTION_FULL);
			float blend = UIScene_Frame()->homeDecorativeBlend;
			CHECK(blend >= previous && blend - previous < 0.06f);
			previous = blend;
		}
		near(previous, 1.0f, 0.001f);
	}
	start(0.02f, UI_MOTION_OFF);
	UIScene_Request(UI_SCENE_SYSTEM);
	tick(0.02f, UI_MOTION_OFF);
	near(UIScene_Frame()->homeDecorativeBlend, 0.0f, 0.0f);
	UIScene_Request(UI_SCENE_HOME);
	tick(0.02f, UI_MOTION_OFF);
	near(UIScene_Frame()->homeDecorativeBlend, 1.0f, 0.0f);
	/* It keeps step with the orbit's strength, whose spring it shares, even
	 * when Reduced takes the travel it had gathered from them both. */
	start(1.0f / 60.0f, UI_MOTION_FULL);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_HORIZONTAL);
	UIScene_Request(UI_SCENE_LIBRARY);
	for(int frame = 0; frame < 60; frame++) {
		tick(1.0f / 60.0f, frame < 8 ? UI_MOTION_FULL : UI_MOTION_REDUCED);
		near(1.0f - UIScene_Frame()->homeDecorativeBlend,
			(1.0f - UIScene_Frame()->orbitStrength) / (1.0f - 0.72f), 0.0005f);
	}
}

/* The Library's cube pose follows its layout: Horizontal keeps the one the
 * carousel always had, Vertical tucks the cube behind the column's cover
 * (Detail's place), and Grid, Spotlight and Jewel Case move it clear of their
 * screens; a layout picked while the Library shows moves the cube there, and
 * Home is never touched. */
static void testLibraryLayoutPoses(void)
{
	float carouselX, verticalX;

	start(0.02f, UI_MOTION_FULL);
	UIScene_Request(UI_SCENE_LIBRARY); settle(0.02f, UI_MOTION_FULL);
	carouselX = UIScene_Frame()->cubeX;
	near(carouselX, -1.08f, 0.002f); near(UIScene_Frame()->cubeScale, 0.56f, 0.002f);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_VERTICAL);
	tick(0.0f, UI_MOTION_FULL);
	near(UIScene_Frame()->cubeX, carouselX, 0.0f); /* retargets, never jumps */
	settle(0.02f, UI_MOTION_FULL);
	verticalX = UIScene_Frame()->cubeX;
	CHECK(verticalX < carouselX - 0.2f);
	near(UIScene_Frame()->cubeScale, 0.44f, 0.002f);
	near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	UIScene_Request(UI_SCENE_GAME_DETAIL); settle(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->cubeScale, 0.44f, 0.002f);
	CHECK(fabsf(UIScene_Frame()->cubeX - verticalX) < 0.1f);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_GRID);
	UIScene_Request(UI_SCENE_LIBRARY); settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->cubeX < verticalX - 1.5f);
	near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_SPOTLIGHT);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->cubeX < verticalX - 1.5f);
	near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	/* The jewel case stands in the middle, where the carousel's cube sits. */
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_HORIZONTAL);
	settle(0.02f, UI_MOTION_FULL);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_JEWEL_CASE);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->cubeX < verticalX - 1.5f);
	near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_BOXES);
	settle(0.02f, UI_MOTION_FULL);
	CHECK(UIScene_Frame()->cubeX < verticalX - 1.5f);
	near(UIScene_Frame()->libraryReveal, 1.0f, 0.0f);
	UIScene_Request(UI_SCENE_HOME); settle(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->cubeX, 0.0f, 0.002f);
	/* Away from the Library a new layout moves nothing. */
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_HORIZONTAL);
	settle(0.02f, UI_MOTION_FULL);
	near(UIScene_Frame()->cubeX, 0.0f, 0.002f);
	UIScene_Request(UI_SCENE_LIBRARY); settle(0.02f, UI_MOTION_OFF);
	near(UIScene_Frame()->cubeX, carouselX, 0.002f);
	/* Unknown layouts are the carousel's; Off snaps like any pose. */
	UIScene_RequestLibraryLayout((uiGameflowLayout_t)77);
	tick(0.02f, UI_MOTION_OFF);
	near(UIScene_Frame()->cubeX, carouselX, 0.002f);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_GRID);
	tick(0.02f, UI_MOTION_OFF);
	CHECK(UIScene_Frame()->cubeX < verticalX - 1.5f);
	UIScene_Reset();
	UIScene_Activate(); settle(0.02f, UI_MOTION_OFF);
	UIScene_Request(UI_SCENE_LIBRARY); tick(0.02f, UI_MOTION_OFF);
	near(UIScene_Frame()->cubeX, carouselX, 0.002f); /* Reset forgets it */
}
/* The boot: Full flies the cube in from far off, spinning, and has it at rest
 * before the light comes up; nothing jumps between frames; the destination's
 * chrome fades in with the boot instead of appearing when it ends. */
static void testBootFly(void)
{
	for(int hz = 50; hz <= 60; hz += 10) for(int mode = 0; mode < UI_MOTION_COUNT; ++mode) {
		float dt = 1.0f/(float)hz, distance = 60.0f, spin = 12.5664f;
		float chrome = 0.0f, seconds = 0.0f;
		uiHomeState_t home;
		UIHome_Init(&home, caps);
		UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
		while(UIScene_Frame()->introProgress < 1.0f || seconds == 0.0f) {
			tick(dt, (uiMotionMode_t)mode); seconds += dt;
			const uiSceneFrame_t *f = UIScene_Frame();
			CHECK(f->scene == UI_SCENE_HOME && f->visible);
			CHECK(f->chromeProgress >= chrome && f->chromeProgress <= 1.0f);
			chrome = f->chromeProgress;
			if(mode != UI_MOTION_FULL) {
				CHECK(f->introDistance == 0.0f && f->introSpin == 0.0f);
				continue;
			}
			CHECK(f->introDistance <= distance && f->introSpin <= spin);
			CHECK(spin - f->introSpin < 1.1f);
			/* It eases into the stop: the last step before rest is tiny. */
			if(spin > 0.0f && f->introSpin == 0.0f) CHECK(spin < 0.1f);
			if(distance > 0.0f && f->introDistance == 0.0f) CHECK(distance < 0.1f);
			distance = f->introDistance; spin = f->introSpin;
			if(seconds <= dt) CHECK(distance > 40.0f && spin > 10.0f);
			if(seconds < 0.45f) CHECK(distance > 0.0f && spin > 0.0f);
			if(f->introProgress >= 0.625f) CHECK(distance == 0.0f && spin == 0.0f);
		}
		CHECK(seconds <= 0.8f + dt);
		near(chrome, 1.0f, 0.0f);
		CHECK(mode != UI_MOTION_FULL || (distance == 0.0f && spin == 0.0f));
	}
}

int main(void)
{
	testBootFly();
	testLibraryRetreatReveal();
	testPhysicalAxesAndArrival(); testHalfTurnDirection();
	testHalfTurnsFromEveryOrientation();
	testMixedCoalescingAndInterruptions(); testModeChangesAndBadTiming();
	testContextAndSceneReturns(); testPublicationAndInvalidRequests();
	testSystemRows();
	testAppsRing();
	testClassicCube();
	testCustomSides();
	testLibraryLayoutPoses();
	testHomeDecorativeBlend();
	printf("ui_scene: %u checks passed\n", checks);
	return 0;
}
