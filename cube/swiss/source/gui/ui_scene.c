#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ui_cube_motif.h"
#include "ui_home_layout.h"
#include "ui_stage.h"

#include "ui_scene.h"

#define UI_SCENE_BOOT_HOLD_SECONDS 0.8f
#define UI_SCENE_CHROME_REVEAL_START 0.45f
/* The boot fly-in: the cube starts far off, spinning fast, and rushes to its
 * Home pose. The distance eases out cubically, so it looms and then brakes;
 * the spin eases out quadratically: fastest while the cube is still small,
 * about 24 degrees a frame at 60 Hz once it is half way in. */
#define UI_SCENE_BOOT_FLY_SECONDS 0.5f
#define UI_SCENE_BOOT_FLY_DISTANCE 60.0f
#define UI_SCENE_BOOT_SPIN_RADIANS 12.5663706f /* two turns */
#define UI_SCENE_ORIENTATION_EPSILON 0.0002f
#define UI_SCENE_CUBE_YAW_RESPONSE 6.5f
#define UI_SCENE_HOME_TURN_RESPONSE 10.0f
/* Classic turns arrive as the GameCube's own menu does: nine tenths of a
 * quarter turn in a quarter of a second. */
#define UI_SCENE_CLASSIC_TURN_RESPONSE 15.6f

typedef struct {
	float cubeX;
	float cubeY;
	float cubeScale;
	float cubePitch;
	float cubeYaw;
	float orbitStrength;
} uiScenePose_t;

typedef uiHomeState_t uiSceneHomeRequest_t;

typedef struct {
	uiMotionSpring_t cubeX;
	uiMotionSpring_t cubeY;
	uiMotionSpring_t cubeScale;
	uiMotionSpring_t cubePitch;
	uiMotionSpring_t cubeYaw;
	uiMotionSpring_t orbitStrength;
	uiMotionSpring_t homeIdleBlend;
	uiMotionSpring_t homeDecorative;
	uiSceneFrame_t frame;
	float bootElapsed;
	uiMotionSpring_t orientation[4]; /* Unit quaternion: w, x, y, z. */
	uiHomeOrientation_t homeTarget;
	uiHomeOrientation_t navigationTarget;
	uiCubeMotifState_t motifs;
	uiSceneHomeRequest_t home;
	uiHomeTurnAxis_t homeTurnAxis;
	float homeFocusProgress;
	float homeFocusDistance;
	uiSceneId_t appliedScene;
	uiHomeFace_t appliedHomeFace;
	int32_t appliedHomeTurnOrdinal;
	uiHomeSurface_t appliedHomeSurface;
	int appliedHomeSelection;
	uint32_t appliedHomeRevision;
	uiMotionMode_t appliedMotionMode;
	uiGameflowLayout_t libraryLayout;
	float stagePixelWidth;
	int homeTurnDirection;
	bool active;
	bool bootComplete;
	bool motionModeKnown;
} uiSceneState_t;

/* One menu producer publishes a coherent latest-value snapshot. Every field
 * is atomic, including the exact orientation; readers never mix revisions. */
static uint32_t requestedScene = UI_SCENE_HOME;
static uint32_t requestedHomeSequence;
static uint32_t requestedHomeFace = UI_HOME_FACE_LIBRARY;
static int32_t requestedHomeTurnOrdinal;
static int32_t requestedHomeFaceCount = UI_HOME_FACE_APPS;
static uint32_t requestedHomeSurface = UI_HOME_SURFACE_RING;
static int32_t requestedHomeSelection;
static uint32_t requestedHomeRevision;
static int32_t requestedHomeOrientation[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
static uint32_t requestedHomeTurnAxis;
static int32_t requestedHomeTurnDirection;
static uint32_t requestedHomeStyle = UI_HOME_CUBE_INFINITE;
static uint32_t requestedHomeLayout;
static uint32_t sceneReady;
static uint32_t requestedLibraryLayout = UI_GAMEFLOW_LAYOUT_HORIZONTAL;
static uiSceneState_t state;

static const uiScenePose_t poses[UI_SCENE_COUNT] = {
	/* The cube flies in at its Home size and turn, seen a little from above. */
	[UI_SCENE_BOOT] = {0.0f, 0.22f, 0.92f, 0.22f, 0.28f, 0.0f},
	[UI_SCENE_HOME] = {0.0f, 0.22f, 0.92f, 0.0f, 0.28f, 1.0f},
	/* The Source picker keeps the Home face's turn and lifts the cube,
	 * smaller and tipped towards the viewer, above its row of devices. */
	[UI_SCENE_SOURCE] = {0.0f, 1.10f, 0.50f, 0.18f, 0.28f, 0.72f},
	[UI_SCENE_LIBRARY] = {-1.08f, 0.0f, 0.56f, -0.08f, 0.32f, 0.72f},
	[UI_SCENE_GAME_DETAIL] = {-1.30f, 0.0f, 0.44f, 0.10f, 0.58f, 0.48f},
	[UI_SCENE_SYSTEM] = {0.0f, -0.02f, 0.70f, -0.10f, -0.18f, 1.0f},
	[UI_SCENE_SETTINGS] = {1.16f, 0.0f, 0.50f, 0.08f, -0.48f, 0.58f}
};
/* The Library's other layouts. Vertical's cover sits where Game Detail's
 * does, so the cube keeps Detail's place behind it; the grid, Spotlight and
 * the jewel case fill the screen, so the cube waits beyond its left edge. */
static const uiScenePose_t verticalLibraryPose =
	{-1.36f, 0.0f, 0.44f, 0.10f, 0.58f, 0.48f};
static const uiScenePose_t gridLibraryPose =
	{-3.60f, 0.0f, 0.44f, 0.10f, 0.58f, 0.48f};

/* How far off the cube still is this many seconds in, and how far it still
 * has to spin. */
static void bootFly(float seconds, float *distance, float *spin)
{
	float left = 1.0f - seconds / UI_SCENE_BOOT_FLY_SECONDS;

	if(!(left > 0.0f)) left = 0.0f;
	if(left > 1.0f) left = 1.0f;
	*distance = UI_SCENE_BOOT_FLY_DISTANCE * left * left * left;
	*spin = UI_SCENE_BOOT_SPIN_RADIANS * left * left;
}

static bool isHomeYawScene(uiSceneId_t scene)
{
	return scene == UI_SCENE_HOME || scene == UI_SCENE_SOURCE;
}

static bool isHomeFace(int face)
{
	return face >= (int)UI_HOME_FACE_LIBRARY &&
		face < (int)UI_HOME_FACE_COUNT;
}

static bool isHomeSurface(int surface)
{
	return surface >= (int)UI_HOME_SURFACE_RING &&
		surface < (int)UI_HOME_SURFACE_COUNT;
}

/* The sides, four bits each, and the absent faces above them: the layout
 * travels as one word. */
static uint32_t packHomeLayout(const uiHomeState_t *home)
{
	uint32_t word = (uint32_t)home->absent << 16;

	for(int side = 0; side < UI_HOME_SIDE_COUNT; ++side)
		word |= (uint32_t)(home->sides[side] & 15u) << (4 * side);
	return word;
}

static void unpackHomeLayout(uint32_t word, uiHomeState_t *home)
{
	for(int side = 0; side < UI_HOME_SIDE_COUNT; ++side)
		home->sides[side] = (uint8_t)((word >> (4 * side)) & 15u);
	home->absent = (uint16_t)(word >> 16);
}

static bool homeRequestValid(const uiHomeState_t *home)
{
	if(home == NULL) return false;
	uiHomeFace_t face = home->face;
	int32_t turnOrdinal = home->turnOrdinal;
	uiHomeSurface_t surface = home->surface;
	int selection = home->selection;
	bool selectionValid = surface == UI_HOME_SURFACE_RING ? selection == 0 :
		selection >= 0 && selection < UI_HOME_LAYOUT_MAX_ROWS;
	bool surfaceMatchesFace = surface == UI_HOME_SURFACE_RING ||
		(surface == UI_HOME_SURFACE_SOURCE && face == UI_HOME_FACE_SOURCE) ||
		((surface == UI_HOME_SURFACE_SYSTEM ||
			surface == UI_HOME_SURFACE_RESTART_CONFIRM) &&
			face == UI_HOME_FACE_SYSTEM);

	/* Library and the faces on its sides, the face in front one of them. */
	if(!UIHome_LayoutValid(home))
		return false;
	if(home->style != UI_HOME_CUBE_INFINITE &&
		home->style != UI_HOME_CUBE_CLASSIC)
		return false;
	return isHomeFace((int)face) && UIHome_RingIndex(home, face) >= 0 &&
		UIHome_RingFace(home, turnOrdinal) == face &&
		isHomeSurface((int)surface) && selectionValid && surfaceMatchesFace &&
		UIHome_OrientationValid(&home->orientation) &&
		((home->turnAxis == UI_HOME_TURN_NONE && home->turnDirection == 0) ||
		 ((home->turnAxis == UI_HOME_TURN_HORIZONTAL ||
		   home->turnAxis == UI_HOME_TURN_VERTICAL) &&
		  (home->turnDirection == -1 || home->turnDirection == 1)));
}

static uiScenePose_t homePose(void)
{
	/* Memory Cards, Emulators, File Browser and Web sit as Apps does. */
	static const float faceLift[UI_HOME_FACE_COUNT] = {
		0.000f, 0.065f, -0.045f, 0.035f, -0.030f, -0.030f, -0.030f, -0.030f, -0.030f
	};
	static const float facePitch[UI_HOME_FACE_COUNT] = {
		0.000f, 0.070f, -0.090f, 0.045f, 0.055f, 0.055f, 0.055f, 0.055f, 0.055f
	};
	uiScenePose_t pose = poses[UI_SCENE_HOME];
	int face = isHomeFace((int)state.appliedHomeFace) ?
		(int)state.appliedHomeFace : (int)UI_HOME_FACE_LIBRARY;

	pose.cubeY += faceLift[face];
	pose.cubePitch += facePitch[face];
	/* Only true vertical row surfaces add a selection tilt. Restart confirmation
	 * is a horizontal safety choice and deliberately keeps the System pose. */
	if(state.appliedHomeSurface == UI_HOME_SURFACE_SOURCE ||
		state.appliedHomeSurface == UI_HOME_SURFACE_SYSTEM) {
		float rowDirection = state.appliedHomeSelection == 0 ? 1.0f : -1.0f;
		/* System's four rows start a row higher, so its cube stays up. */
		pose.cubeY += state.appliedHomeSurface == UI_HOME_SURFACE_SYSTEM ?
			0.055f : rowDirection * 0.055f;
		pose.cubePitch -= rowDirection * 0.095f;
		pose.cubeScale += 0.030f;
	}
	return pose;
}

static uiSceneId_t sanitizeScene(uint32_t scene)
{
	if(scene <= (uint32_t)UI_SCENE_BOOT ||
		scene >= (uint32_t)UI_SCENE_COUNT) {
		return UI_SCENE_HOME;
	}
	return (uiSceneId_t)scene;
}

static uiMotionMode_t sanitizeMotionMode(uiMotionMode_t motionMode)
{
	if(motionMode != UI_MOTION_FULL &&
		motionMode != UI_MOTION_REDUCED &&
		motionMode != UI_MOTION_OFF) {
		return UI_MOTION_FULL;
	}
	return motionMode;
}

static uiSceneHomeRequest_t loadHomeRequest(void)
{
	uiSceneHomeRequest_t request;
	uint32_t sequenceBefore;
	uint32_t sequenceAfter;
	int row, col;

	for(;;) {
		sequenceBefore = __atomic_load_n(&requestedHomeSequence,
			__ATOMIC_ACQUIRE);
		if((sequenceBefore & 1u) != 0u) {
			continue;
		}
		request.face = (uiHomeFace_t)__atomic_load_n(&requestedHomeFace,
			__ATOMIC_RELAXED);
		request.turnOrdinal = __atomic_load_n(&requestedHomeTurnOrdinal,
			__ATOMIC_RELAXED);
		request.faceCount = (int)__atomic_load_n(&requestedHomeFaceCount,
			__ATOMIC_RELAXED);
		request.surface = (uiHomeSurface_t)__atomic_load_n(
			&requestedHomeSurface, __ATOMIC_RELAXED);
		request.selection = __atomic_load_n(&requestedHomeSelection,
			__ATOMIC_RELAXED);
		request.revision = __atomic_load_n(&requestedHomeRevision,
			__ATOMIC_RELAXED);
		request.turnAxis = (uiHomeTurnAxis_t)__atomic_load_n(
			&requestedHomeTurnAxis, __ATOMIC_RELAXED);
		request.turnDirection = __atomic_load_n(&requestedHomeTurnDirection,
			__ATOMIC_RELAXED);
		request.style = (uiHomeCubeStyle_t)__atomic_load_n(
			&requestedHomeStyle, __ATOMIC_RELAXED);
		unpackHomeLayout(__atomic_load_n(&requestedHomeLayout,
			__ATOMIC_RELAXED), &request);
		for(row = 0; row < 3; ++row) {
			for(col = 0; col < 3; ++col) {
				request.orientation.m[row][col] = (int8_t)__atomic_load_n(
					&requestedHomeOrientation[row][col], __ATOMIC_RELAXED);
			}
		}
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		sequenceAfter = __atomic_load_n(&requestedHomeSequence,
			__ATOMIC_ACQUIRE);
		if(sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0u) {
			break;
		}
	}

	return request;
}

static void clearSpringVelocities(void)
{
	state.cubeX.velocity = 0.0f;
	state.cubeY.velocity = 0.0f;
	state.cubeScale.velocity = 0.0f;
	state.cubePitch.velocity = 0.0f;
	state.cubeYaw.velocity = 0.0f;
	state.orbitStrength.velocity = 0.0f;
	state.homeDecorative.velocity = 0.0f;
	state.homeIdleBlend.velocity = 0.0f;
	for(int i = 0; i < 4; ++i) state.orientation[i].velocity = 0.0f;
}

static void applyMotionModeTransition(uiMotionMode_t motionMode)
{
	if(!state.motionModeKnown) {
		state.appliedMotionMode = motionMode;
		state.motionModeKnown = true;
		return;
	}
	if(motionMode == state.appliedMotionMode) {
		return;
	}
	/* Entering Reduced deliberately removes inherited secondary travel while
	 * leaving the current pose and destination intact. */
	if(motionMode == UI_MOTION_REDUCED) {
		clearSpringVelocities();
	}
	if(motionMode == UI_MOTION_OFF) {
		state.homeFocusProgress = 1.0f;
	}
	state.appliedMotionMode = motionMode;
}

static void orientationQuaternion(const uiHomeOrientation_t *orientation,
	float q[4])
{
	float m[3][3];
	float scale;
	UIHome_OrientationMatrix(orientation, m);
	float trace = m[0][0] + m[1][1] + m[2][2];
	if(trace > 0.0f) {
		scale = sqrtf(trace + 1.0f) * 2.0f;
		q[0] = scale * 0.25f;
		q[1] = (m[2][1] - m[1][2]) / scale;
		q[2] = (m[0][2] - m[2][0]) / scale;
		q[3] = (m[1][0] - m[0][1]) / scale;
	}
	else {
		int i = m[0][0] >= m[1][1] && m[0][0] >= m[2][2] ? 0 :
			(m[1][1] >= m[2][2] ? 1 : 2);
		int j = (i + 1) % 3, k = (i + 2) % 3;
		scale = sqrtf(1.0f + m[i][i] - m[j][j] - m[k][k]) * 2.0f;
		q[0] = (m[k][j] - m[j][k]) / scale;
		q[i + 1] = scale * 0.25f;
		q[j + 1] = (m[j][i] + m[i][j]) / scale;
		q[k + 1] = (m[k][i] + m[i][k]) / scale;
	}
}

static bool orientationSettled(void)
{
	for(int i = 0; i < 4; ++i) {
		if(!UIMotion_SpringSettled(&state.orientation[i], 0.0002f, 0.001f))
			return false;
	}
	return true;
}

static float orientationDistance(void)
{
	float dot = 0.0f;
	for(int i = 0; i < 4; ++i)
		dot += state.orientation[i].value * state.orientation[i].target;
	return 2.0f * acosf(fminf(1.0f, fabsf(dot)));
}

static void retargetOrientation(uiSceneId_t scene, uiMotionMode_t motionMode)
{
	uiHomeOrientation_t target;
	float q[4], current[4], dot = 0.0f;
	UIHome_OrientationInit(&target);
	if(isHomeYawScene(scene)) target = state.homeTarget;
	if(memcmp(&target, &state.navigationTarget, sizeof(target)) == 0) return;
	state.navigationTarget = target;
	/* Each leg takes its pace from the style it is turned in. */
	for(int i = 0; i < 4; ++i)
		state.orientation[i].response = isHomeYawScene(scene) &&
			state.home.style == UI_HOME_CUBE_CLASSIC ?
			UI_SCENE_CLASSIC_TURN_RESPONSE : UI_SCENE_HOME_TURN_RESPONSE;
	orientationQuaternion(&target, q);
	for(int i = 0; i < 4; ++i) {
		current[i] = state.orientation[i].value;
		dot += current[i] * q[i];
	}
	bool flip = dot < 0.0f;
	/* A half-turn has two equally short quaternion arcs. Choose its screen
	 * rotation sign from the actual command, including coalesced double taps. */
	if(fabsf(dot) < 0.00001f && isHomeYawScene(scene)) {
		int axis = state.homeTurnAxis == UI_HOME_TURN_VERTICAL ? 1 : 2;
		int j = axis % 3 + 1, k = j % 3 + 1;
		float relativeAxis = current[0] * q[axis] - q[0] * current[axis] -
			q[j] * current[k] + q[k] * current[j];
		if(fabsf(relativeAxis) > 0.00001f)
			flip = relativeAxis * (float)-state.homeTurnDirection < 0.0f;
	}
	for(int i = 0; i < 4; ++i)
		UIMotion_SpringRetarget(&state.orientation[i], flip ? -q[i] : q[i], motionMode);
	state.homeFocusDistance = orientationDistance();
	state.homeFocusProgress = motionMode == UI_MOTION_OFF ? 1.0f : 0.0f;
}

static void updateOrientation(float deltaSeconds, uiMotionMode_t motionMode)
{
	float q[4], norm = 0.0f, radialVelocity = 0.0f;
	for(int i = 0; i < 4; ++i) {
		q[i] = UIMotion_SpringUpdate(&state.orientation[i], deltaSeconds, motionMode);
		norm += q[i] * q[i];
	}
	norm = sqrtf(norm);
	/* Hemisphere selection keeps interpolated norms away from zero. Retain a
	 * defensive exact target for invalid arithmetic rather than publish NaNs. */
	if(!isfinite(norm) || norm < 0.00001f) {
		for(int i = 0; i < 4; ++i) {
			UIMotion_SpringSnap(&state.orientation[i], state.orientation[i].target);
			q[i] = state.orientation[i].value;
		}
		norm = 1.0f;
	}
	for(int i = 0; i < 4; ++i) {
		q[i] /= norm;
		state.orientation[i].value = q[i];
		radialVelocity += q[i] * state.orientation[i].velocity;
	}
	for(int i = 0; i < 4; ++i)
		state.orientation[i].velocity =
			(state.orientation[i].velocity - q[i] * radialVelocity) / norm;
	float w = q[0], x = q[1], y = q[2], z = q[3];
	float (*m)[3] = state.frame.homeOrientation;
	m[0][0] = 1.0f - 2.0f * (y*y + z*z);
	m[0][1] = 2.0f * (x*y - z*w);
	m[0][2] = 2.0f * (x*z + y*w);
	m[1][0] = 2.0f * (x*y + z*w);
	m[1][1] = 1.0f - 2.0f * (x*x + z*z);
	m[1][2] = 2.0f * (y*z - x*w);
	m[2][0] = 2.0f * (x*z - y*w);
	m[2][1] = 2.0f * (y*z + x*w);
	m[2][2] = 1.0f - 2.0f * (x*x + y*y);
	UIHome_OrientationMatrix(&state.navigationTarget, state.frame.homeTargetOrientation);
	/* Cardinal targets are exact at rest and in Off, without float residue. */
	if(orientationSettled()) {
		for(int i = 0; i < 4; ++i)
			UIMotion_SpringSnap(&state.orientation[i], state.orientation[i].target);
		memcpy(state.frame.homeOrientation, state.frame.homeTargetOrientation,
			sizeof(state.frame.homeOrientation));
	}
}

static void retargetPose(uiSceneId_t scene, uiMotionMode_t motionMode)
{
	uiScenePose_t resolvedHomePose;
	const uiScenePose_t *pose;
	float cubeYaw;

	if(isHomeYawScene(scene)) {
		resolvedHomePose = scene == UI_SCENE_SOURCE ?
			poses[UI_SCENE_SOURCE] : homePose();
		pose = &resolvedHomePose;
		cubeYaw = pose->cubeYaw;
	}
	else if(scene == UI_SCENE_LIBRARY &&
		state.libraryLayout == UI_GAMEFLOW_LAYOUT_VERTICAL) {
		pose = &verticalLibraryPose;
		cubeYaw = pose->cubeYaw;
	}
	else if(scene == UI_SCENE_LIBRARY &&
		(state.libraryLayout == UI_GAMEFLOW_LAYOUT_GRID ||
		state.libraryLayout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT ||
		state.libraryLayout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE)) {
		/* The perspective widens with the stage. Keep the parked cube
		 * beyond the actual left edge, including Menu Widescreen. */
		resolvedHomePose = gridLibraryPose;
		resolvedHomePose.cubeX *= UIStage_PixelWidth();
		pose = &resolvedHomePose;
		cubeYaw = pose->cubeYaw;
	}
	else {
		pose = &poses[scene];
		cubeYaw = pose->cubeYaw;
	}

	/* Semantic Home turns should read as arrived in roughly 250-400 ms.
	 * Other scene transitions retain the slower cinematic yaw response. */
	state.cubeYaw.response = state.bootComplete && isHomeYawScene(scene) ?
		UI_SCENE_HOME_TURN_RESPONSE : UI_SCENE_CUBE_YAW_RESPONSE;
	UIMotion_SpringRetarget(&state.cubeX, pose->cubeX, motionMode);
	UIMotion_SpringRetarget(&state.cubeY, pose->cubeY, motionMode);
	UIMotion_SpringRetarget(&state.cubeScale, pose->cubeScale, motionMode);
	UIMotion_SpringRetarget(&state.cubePitch, pose->cubePitch, motionMode);
	UIMotion_SpringRetarget(&state.cubeYaw, cubeYaw, motionMode);
	UIMotion_SpringRetarget(&state.orbitStrength, pose->orbitStrength, motionMode);
	UIMotion_SpringRetarget(&state.homeDecorative,
		isHomeYawScene(scene) ? 1.0f : 0.0f, motionMode);
	retargetOrientation(scene, motionMode);
	/* Classic's glyphs keep their sides in every scene, so leaving Home
	 * moves none of them. Away from Home Infinite shows Library in front of
	 * its ring: the authored background for the default sides. */
	if(isHomeYawScene(scene) || state.home.style == UI_HOME_CUBE_CLASSIC) {
		UICubeMotif_Request(&state.motifs, &state.home, motionMode);
	}
	else if(UIHome_DefaultSides(&state.home)) {
		UICubeMotif_Request(&state.motifs, NULL, motionMode);
	}
	else {
		uiHomeState_t away = state.home;

		away.face = UI_HOME_FACE_LIBRARY;
		away.turnOrdinal = 0;
		away.surface = UI_HOME_SURFACE_RING;
		away.selection = 0;
		UIHome_OrientationInit(&away.orientation);
		away.turnAxis = UI_HOME_TURN_NONE;
		away.turnDirection = 0;
		UICubeMotif_Request(&state.motifs, &away, motionMode);
	}
	state.appliedScene = scene;
}

static void applyHomeRequest(uiMotionMode_t motionMode)
{
	uiSceneHomeRequest_t request = loadHomeRequest();
	if(request.face == state.appliedHomeFace &&
		request.turnOrdinal == state.appliedHomeTurnOrdinal &&
		request.faceCount == state.home.faceCount &&
		request.surface == state.appliedHomeSurface &&
		request.selection == state.appliedHomeSelection &&
		request.revision == state.appliedHomeRevision &&
		request.turnAxis == state.homeTurnAxis &&
		request.turnDirection == state.homeTurnDirection &&
		request.style == state.home.style &&
		request.absent == state.home.absent &&
		memcmp(request.sides, state.home.sides, sizeof(request.sides)) == 0 &&
		memcmp(&request.orientation, &state.homeTarget, sizeof(state.homeTarget)) == 0)
		return;
	state.home = request;
	state.homeTarget = request.orientation;
	state.appliedHomeFace = request.face;
	state.appliedHomeTurnOrdinal = request.turnOrdinal;
	state.appliedHomeSurface = request.surface;
	state.appliedHomeSelection = request.selection;
	state.appliedHomeRevision = request.revision;
	state.homeTurnAxis = request.turnAxis;
	state.homeTurnDirection = request.turnDirection;
	if(state.bootComplete && isHomeYawScene(state.appliedScene)) {
		/* Face, orientation and row selection share one pose transaction. */
		retargetPose(state.appliedScene, motionMode);
	}
}

static void updateHomeFocus(uiMotionMode_t motionMode)
{
	float progress;
	float remaining;

	if(motionMode == UI_MOTION_OFF) {
		state.homeFocusProgress = 1.0f;
		return;
	}
	if(state.homeFocusDistance <= UI_SCENE_ORIENTATION_EPSILON) {
		state.homeFocusProgress = 1.0f;
		return;
	}
	remaining = orientationDistance();
	progress = 1.0f - remaining / state.homeFocusDistance;
	if(progress < 0.0f) {
		progress = 0.0f;
	}
	else if(progress > 1.0f) {
		progress = 1.0f;
	}
	/* Focus is a monotonic readout of the current turn leg. Inertial travel may
	 * briefly overshoot, but labels never reverse after they have arrived. */
	if(progress > state.homeFocusProgress) {
		state.homeFocusProgress = progress;
	}
	if(orientationSettled()) {
		state.homeFocusProgress = 1.0f;
	}
}

void UIScene_Reset(void)
{
	const uiScenePose_t *bootPose = &poses[UI_SCENE_BOOT];

	__atomic_store_n(&requestedScene, UI_SCENE_HOME, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSequence, 0u, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeFace, UI_HOME_FACE_LIBRARY,
		__ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeTurnOrdinal, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeFaceCount, UI_HOME_FACE_APPS,
		__ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSurface, UI_HOME_SURFACE_RING,
		__ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSelection, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeRevision, 0u, __ATOMIC_RELAXED);
	__atomic_store_n(&sceneReady, 0u, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedLibraryLayout, UI_GAMEFLOW_LAYOUT_HORIZONTAL,
		__ATOMIC_RELAXED);
	state = (uiSceneState_t) {0};
	UIHome_OrientationInit(&state.homeTarget);
	UIHome_OrientationInit(&state.navigationTarget);
	UIHome_Init(&state.home, (uiHomeCapabilities_t){.hasSource = true});
	state.home.revision = 0u;
	__atomic_store_n(&requestedHomeLayout, packHomeLayout(&state.home),
		__ATOMIC_RELAXED);
	UICubeMotif_Reset(&state.motifs);
	for(int i = 0; i < 4; ++i)
		UIMotion_SpringInit(&state.orientation[i], i == 0 ? 1.0f : 0.0f,
			UI_SCENE_HOME_TURN_RESPONSE);
	__atomic_store_n(&requestedHomeTurnAxis, UI_HOME_TURN_NONE, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeTurnDirection, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeStyle, UI_HOME_CUBE_INFINITE, __ATOMIC_RELAXED);
	for(int row = 0; row < 3; ++row)
		for(int col = 0; col < 3; ++col)
			__atomic_store_n(&requestedHomeOrientation[row][col], row == col ? 1 : 0,
				__ATOMIC_RELAXED);
	state.homeFocusProgress = 1.0f;
	state.appliedScene = UI_SCENE_BOOT;
	state.appliedHomeFace = UI_HOME_FACE_LIBRARY;
	state.appliedHomeSurface = UI_HOME_SURFACE_RING;
	UIMotion_SpringInit(&state.cubeX, bootPose->cubeX, 7.5f);
	UIMotion_SpringInit(&state.cubeY, bootPose->cubeY, 7.5f);
	UIMotion_SpringInit(&state.cubeScale, bootPose->cubeScale, 7.0f);
	UIMotion_SpringInit(&state.cubePitch, bootPose->cubePitch, 6.5f);
	UIMotion_SpringInit(&state.cubeYaw, bootPose->cubeYaw,
		UI_SCENE_CUBE_YAW_RESPONSE);
	UIMotion_SpringInit(&state.orbitStrength, bootPose->orbitStrength, 5.5f);
	/* The boot arrives on Home. */
	UIMotion_SpringInit(&state.homeDecorative, 1.0f, 5.5f);
	UIMotion_SpringInit(&state.homeIdleBlend, 0.0f, 7.0f);
	state.frame = (uiSceneFrame_t) {
		.scene = UI_SCENE_BOOT,
		.cubeX = bootPose->cubeX,
		.cubeY = bootPose->cubeY,
		.cubeScale = bootPose->cubeScale,
		.cubePitch = bootPose->cubePitch,
		.cubeYaw = bootPose->cubeYaw,
		.orbitStrength = bootPose->orbitStrength,
		.homeDecorativeBlend = 1.0f,
		.introProgress = 0.0f,
		.chromeProgress = 0.0f,
		.homeFace = UI_HOME_FACE_LIBRARY,
		.homeTurnDirection = 0,
		.homeFocusProgress = 1.0f,
		.homeIdleBlend = 0.0f,
		.homeRevision = 0u,
		.homeSurface = UI_HOME_SURFACE_RING,
		.homeSelection = 0,
		.visible = false,
		.transitioning = false
	};
	UIHome_OrientationMatrix(&state.homeTarget, state.frame.homeOrientation);
	UIHome_OrientationMatrix(&state.homeTarget, state.frame.homeTargetOrientation);
	memcpy(state.frame.homeMotifBasis, state.motifs.basis.face,
		sizeof(state.frame.homeMotifBasis));
	memcpy(state.frame.homeMotifAlpha, state.motifs.alpha,
		sizeof(state.frame.homeMotifAlpha));
}

void UIScene_Activate(void)
{
	__atomic_store_n(&sceneReady, 1u, __ATOMIC_RELAXED);
}

void UIScene_Request(uiSceneId_t scene)
{
	__atomic_store_n(&requestedScene, sanitizeScene((uint32_t)scene),
		__ATOMIC_RELAXED);
}

void UIScene_RequestLibraryLayout(uiGameflowLayout_t layout)
{
	__atomic_store_n(&requestedLibraryLayout,
		(uint32_t)layout < UI_GAMEFLOW_LAYOUT_COUNT ?
		(uint32_t)layout : UI_GAMEFLOW_LAYOUT_HORIZONTAL, __ATOMIC_RELAXED);
}

void UIScene_RequestHome(const uiHomeState_t *home)
{
	if(!homeRequestValid(home)) return;
	/* Acquire-release RMW prevents following field stores from moving before
	 * the odd marker. The final release publishes the complete snapshot. */
	uint32_t sequence = __atomic_fetch_add(&requestedHomeSequence, 1u,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&requestedHomeFace, (uint32_t)home->face, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeTurnOrdinal, home->turnOrdinal, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeFaceCount, (int32_t)home->faceCount,
		__ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSurface, (uint32_t)home->surface, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSelection, home->selection, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeRevision, home->revision, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeTurnAxis, (uint32_t)home->turnAxis, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeTurnDirection, home->turnDirection, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeStyle, (uint32_t)home->style, __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeLayout, packHomeLayout(home), __ATOMIC_RELAXED);
	for(int row = 0; row < 3; ++row)
		for(int col = 0; col < 3; ++col)
			__atomic_store_n(&requestedHomeOrientation[row][col],
				home->orientation.m[row][col], __ATOMIC_RELAXED);
	__atomic_store_n(&requestedHomeSequence, sequence + 2u, __ATOMIC_RELEASE);
}

void UIScene_Update(float deltaSeconds, uiMotionMode_t motionMode)
{
	uiSceneId_t targetScene;
	float idleTarget;

	if(!__atomic_load_n(&sceneReady, __ATOMIC_RELAXED)) {
		return;
	}

	if(!isfinite(deltaSeconds) || deltaSeconds < 0.0f) deltaSeconds = 0.0f;
	if(deltaSeconds > 0.050f) deltaSeconds = 0.050f;
	motionMode = sanitizeMotionMode(motionMode);
	applyMotionModeTransition(motionMode);
	targetScene = sanitizeScene(__atomic_load_n(&requestedScene,
		__ATOMIC_RELAXED));
	applyHomeRequest(motionMode);
	{
		uiGameflowLayout_t layout = (uiGameflowLayout_t)__atomic_load_n(
			&requestedLibraryLayout, __ATOMIC_RELAXED);
		float stagePixelWidth = UIStage_PixelWidth();

		if(layout != state.libraryLayout || stagePixelWidth != state.stagePixelWidth) {
			/* Layout and screen-shape changes move the parked cube while
			 * the Library is showing, through the existing spring. */
			state.libraryLayout = layout;
			state.stagePixelWidth = stagePixelWidth;
			if(state.bootComplete && state.appliedScene == UI_SCENE_LIBRARY &&
				targetScene == UI_SCENE_LIBRARY) {
				retargetPose(UI_SCENE_LIBRARY, motionMode);
			}
		}
	}
	if(!state.active) {
		state.active = true;
		state.frame.visible = true;
		if(motionMode == UI_MOTION_OFF) {
			state.bootComplete = true;
			retargetPose(targetScene, motionMode);
		}
		else {
			retargetPose(UI_SCENE_HOME, motionMode);
			state.appliedScene = UI_SCENE_BOOT;
		}
	}

	if(!state.bootComplete) {
		state.bootElapsed += deltaSeconds;
		if(motionMode == UI_MOTION_OFF ||
			state.bootElapsed >= UI_SCENE_BOOT_HOLD_SECONDS) {
			state.bootComplete = true;
			retargetPose(targetScene, motionMode);
		}
	}
	else if(targetScene != state.appliedScene) {
		retargetPose(targetScene, motionMode);
	}

	updateOrientation(deltaSeconds, motionMode);
	state.frame.cubeX = UIMotion_SpringUpdate(&state.cubeX, deltaSeconds,
		motionMode);
	state.frame.cubeY = UIMotion_SpringUpdate(&state.cubeY, deltaSeconds,
		motionMode);
	state.frame.cubeScale = UIMotion_SpringUpdate(&state.cubeScale,
		deltaSeconds, motionMode);
	state.frame.cubePitch = UIMotion_SpringUpdate(&state.cubePitch,
		deltaSeconds, motionMode);
	state.frame.cubeYaw = UIMotion_SpringUpdate(&state.cubeYaw, deltaSeconds,
		motionMode);
	updateHomeFocus(motionMode);
	/* Home's alone: the Source picker's smaller cube holds still. */
	idleTarget = motionMode == UI_MOTION_FULL &&
		state.appliedScene == UI_SCENE_HOME &&
		UIMotion_SpringSettled(&state.cubeY, 0.0002f, 0.001f) &&
		UIMotion_SpringSettled(&state.cubeScale, 0.0002f, 0.001f) &&
		UIMotion_SpringSettled(&state.cubePitch, 0.0002f, 0.001f) &&
		UIMotion_SpringSettled(&state.cubeYaw, 0.0002f, 0.001f) &&
		orientationSettled() && !state.motifs.changing ? 1.0f : 0.0f;
	UIMotion_SpringRetarget(&state.homeIdleBlend, idleTarget, motionMode);
	state.frame.homeIdleBlend = UIMotion_SpringUpdate(&state.homeIdleBlend,
		deltaSeconds, motionMode);
	state.frame.orbitStrength = UIMotion_SpringUpdate(&state.orbitStrength,
		deltaSeconds, motionMode);
	state.frame.homeDecorativeBlend = UIMotion_SpringUpdate(
		&state.homeDecorative, deltaSeconds, motionMode);
	state.frame.introProgress = state.bootComplete ? 1.0f :
		state.bootElapsed / UI_SCENE_BOOT_HOLD_SECONDS;
	state.frame.chromeProgress = UIMotion_EaseOutCubic(
		(state.frame.introProgress - UI_SCENE_CHROME_REVEAL_START) /
		(1.0f - UI_SCENE_CHROME_REVEAL_START));
	/* Full motion flies the cube in; Reduced fades it in where it rests. */
	state.frame.introDistance = 0.0f;
	state.frame.introSpin = 0.0f;
	if(motionMode == UI_MOTION_FULL && !state.bootComplete) {
		bootFly(state.bootElapsed, &state.frame.introDistance,
			&state.frame.introSpin);
	}
	/* Library posters must not appear at full opacity over the Home-sized
	 * cube on the first destination frame. Derive their reveal from the live
	 * retreat pose: interruption and Off therefore need no separate timeline.
	 * Library/detail changes stay fully visible once the cube is in the back. */
	state.frame.libraryReveal = (targetScene == UI_SCENE_LIBRARY ||
		targetScene == UI_SCENE_GAME_DETAIL) ?
		UIMotion_Smoothstep((0.88f - state.frame.cubeScale) / 0.24f) : 0.0f;
	/* The boot already shows its destination, so the chrome that scene draws
	 * fades in with chromeProgress rather than appearing when the boot ends. */
	state.frame.scene = targetScene;
	UICubeMotif_Update(&state.motifs, deltaSeconds, motionMode);
	memcpy(state.frame.homeMotifBasis, state.motifs.basis.face,
		sizeof(state.frame.homeMotifBasis));
	memcpy(state.frame.homeMotifAlpha, state.motifs.alpha,
		sizeof(state.frame.homeMotifAlpha));
	state.frame.homeTurnAxis = state.homeTurnAxis;
	state.frame.homeFace = state.appliedHomeFace;
	state.frame.homeTurnDirection = state.homeTurnDirection;
	state.frame.homeFocusProgress = state.homeFocusProgress;
	state.frame.homeRevision = state.appliedHomeRevision;
	state.frame.homeSurface = state.appliedHomeSurface;
	state.frame.homeSelection = state.appliedHomeSelection;
	state.frame.transitioning =
		!UIMotion_SpringSettled(&state.cubeX, 0.002f, 0.01f) ||
		!UIMotion_SpringSettled(&state.cubeY, 0.002f, 0.01f) ||
		!UIMotion_SpringSettled(&state.cubeScale, 0.002f, 0.01f) ||
		!UIMotion_SpringSettled(&state.cubePitch, 0.002f, 0.01f) ||
		!UIMotion_SpringSettled(&state.cubeYaw, 0.002f, 0.01f) ||
		!orientationSettled() || !UICubeMotif_Settled(&state.motifs) ||
		state.homeFocusProgress < 1.0f;
}

const uiSceneFrame_t *UIScene_Frame(void)
{
	return &state.frame;
}
