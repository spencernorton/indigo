#ifndef UI_SCENE_H
#define UI_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_gameflow.h"
#include "ui_home.h"
#include "ui_motion.h"

typedef enum {
	UI_SCENE_BOOT = 0,
	UI_SCENE_HOME,
	UI_SCENE_SOURCE,
	UI_SCENE_LIBRARY,
	UI_SCENE_GAME_DETAIL,
	UI_SCENE_SYSTEM,
	UI_SCENE_SETTINGS,
	UI_SCENE_COUNT
} uiSceneId_t;

/*
 * A read-only render snapshot. Requests originate on the menu thread, while
 * every animated value is owned and advanced by the video thread.
 */
typedef struct {
	uiSceneId_t scene;
	float cubeX;
	float cubeY;
	float cubeScale;
	float cubePitch;
	float cubeYaw;
	float orbitStrength;
	/* 1 on Home and Source, 0 elsewhere, moving with the cube: how far the
	 * waves have eased to Home's quieter strength. */
	float homeDecorativeBlend;
	float introProgress;
	float chromeProgress;
	/* The boot fly-in: how much farther off than its pose the cube still is
	 * (cube units) and how far it has left to spin (radians). Both reach 0 as
	 * it arrives on its Home face. */
	float introDistance;
	float introSpin;
	/* Poster opacity follows the cube retreat, separate from the boot chrome. */
	float libraryReveal;
	uiHomeFace_t homeFace;
	int homeTurnDirection;
	uiHomeTurnAxis_t homeTurnAxis;
	/* Row-major body-to-screen navigation transform, separate from authored
	 * cubePitch/cubeYaw. Non-Home destinations animate toward identity. */
	float homeOrientation[3][3];
	float homeTargetOrientation[3][3];
	float homeMotifBasis[UI_HOME_FACE_COUNT][3][3];
	/* Each face's glyph fades on its own (gui/ui_cube_motif.h); 0 for a
	 * face with no side, such as Apps outside a ring of four. */
	float homeMotifAlpha[UI_HOME_FACE_COUNT];
	float homeFocusProgress;
	float homeIdleBlend;
	/* Idle Animation › Sway's strength: like homeIdleBlend, but once Home
	 * has rested it stays 1 through Home's turns, until the cube leaves Home. */
	float homeSwayBlend;
	uint32_t homeRevision;
	uiHomeSurface_t homeSurface;
	int homeSelection;
	bool visible;
	bool transitioning;
} uiSceneFrame_t;

void UIScene_Reset(void);
void UIScene_Activate(void);
void UIScene_Request(uiSceneId_t scene);
/* Publish one complete reducer snapshot. Absolute orientation preserves mixed
 * axis commands even when several menu updates coalesce before one frame. */
void UIScene_RequestHome(const uiHomeState_t *home);
/* The Library's cube pose follows its layout: Horizontal keeps the
 * carousel's, Vertical tucks the cube behind the selected cover, and Grid
 * and Spotlight move it out of their way. Unknown layouts are Horizontal. */
void UIScene_RequestLibraryLayout(uiGameflowLayout_t layout);
void UIScene_Update(float deltaSeconds, uiMotionMode_t motionMode);
const uiSceneFrame_t *UIScene_Frame(void);

#endif
