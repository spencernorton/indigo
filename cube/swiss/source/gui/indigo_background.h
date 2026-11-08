#ifndef INDIGO_BACKGROUND_H
#define INDIGO_BACKGROUND_H

#include <gctypes.h>
#include <stdbool.h>

#include "ui_clock.h"
#include "ui_color.h"
#include "ui_scene.h"

/* One controller sample per video frame, taken beside the clock so the
 * renderer never reads hardware. The Controller icon mirrors it. */
typedef struct indigoPadFrame {
	bool available;
	s8 stickX;
	s8 stickY;
	s8 substickX;
	s8 substickY;
	u32 buttons;
} indigoPadFrame_t;

/* The EFB the frame is drawn in, for the glass's screen copies; the
 * default is 640 x 480. */
void IndigoBackground_SetFramebuffer(u16 width, u16 height);
/* This frame's UI_COLOR_ value for each UI_COLOR_LAYER_: the backdrop and
 * its rings, the waves, and the menus' for the cube. Set once a frame,
 * before either draw below. */
void IndigoBackground_SetColors(const int colors[UI_COLOR_LAYERS]);
/* Wave Speed: how fast the waves drift, as a multiple of their normal pace.
 * Set once a frame, before the draw below; a change speeds the waves up or
 * slows them down from where they are, without a jump. */
void IndigoBackground_SetWaveSpeed(float speed);
/* Waves: whether the waves drift behind the cube at all (the default). */
void IndigoBackground_SetWaves(bool shown);
/* Idle Animation: Sway turns the cube left and right while Home rests, so
 * the faces beside the front one peek round its edges; otherwise it drifts
 * a little, as it always has. Set once a frame, before the draw below. */
void IndigoBackground_SetIdleSway(bool sway);
/* Drawn after the configured backdrop and before every foreground widget.
 * pad may be NULL: the Controller icon then plays only its idle motion.
 * icons holds a uiHomeIcon_t for each uiHomeFace_t. */
#if UI_EXPERIMENTAL_540P
void IndigoBackground_Prepare540p(float seconds, bool backdropAnimated,
	bool cubeAnimated, const uiSceneFrame_t *scene,
	const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
	const int icons[UI_HOME_FACE_COUNT]);
#endif
void IndigoBackground_Draw(float seconds, bool backdropAnimated,
	bool cubeAnimated, const uiSceneFrame_t *scene,
	const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
	const int icons[UI_HOME_FACE_COUNT]);
/* Final pass used only during the short boot reveal, after foreground widgets. */
/* For a frame the background is not drawn in (a full-screen page covers it):
 * the Library emblem still follows the pad, so its idle play keeps time. */
void IndigoBackground_TrackPad(float seconds, bool animated,
	const indigoPadFrame_t *pad);

/* Memory Cards' cube screen covers the frame and draws this first: the
 * backdrop's wash and as much of its graph paper as paper says, in the
 * Backdrop Color; then, while handover is above 0, the Home cube where
 * scene has it, that far back from the distance it goes into. */
void IndigoBackground_DrawSavesBackdrop(float paper, float handover,
	float seconds, bool animated, const uiSceneFrame_t *scene,
	const uiClockFrame_t *clock, const int icons[UI_HOME_FACE_COUNT]);

void IndigoBackground_DrawBootOverlay(float seconds, bool animated,
	const uiSceneFrame_t *scene, const uiClockFrame_t *clock,
	const int icons[UI_HOME_FACE_COUNT]);

#endif
