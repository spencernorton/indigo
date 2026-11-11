#ifndef UI_GAMEFLOW_H
#define UI_GAMEFLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_motion.h"

/* Keeps carousel arithmetic exactly representable by a float while remaining
 * far above Swiss's practical library size. Oversized snapshots are clamped. */
#define UI_GAMEFLOW_MAX_ITEMS 65535u

typedef enum {
	UI_GAMEFLOW_MODE_LIBRARY = 0,
	UI_GAMEFLOW_MODE_DETAIL,
	UI_GAMEFLOW_MODE_LAUNCH
} uiGameflowMode_t;

typedef enum {
	UI_GAMEFLOW_DIRECTION_PREVIOUS = -1,
	UI_GAMEFLOW_DIRECTION_NONE = 0,
	UI_GAMEFLOW_DIRECTION_NEXT = 1
} uiGameflowDirection_t;

/* Setup > Library > Library Layout: how the Library lays out the games.
 * The default is 3D Boxes: the ring with projected retail cases and separate
 * plain edges. Horizontal is retained internally for Apps and the low-memory
 * Jewel Case fallback. Vertical is the same ring on end; Grid is rows of
 * posters that scroll up and down. Spotlight is
 * the ring again as a row of disc banners, under the selected game's
 * gameplay still and details. Jewel Case is the ring as one clear CD case
 * at a time, the game's disc spinning inside (gui/ui_jewel_case.c). */
typedef enum {
	UI_GAMEFLOW_LAYOUT_HORIZONTAL = 0,
	UI_GAMEFLOW_LAYOUT_VERTICAL,
	UI_GAMEFLOW_LAYOUT_GRID,
	UI_GAMEFLOW_LAYOUT_SPOTLIGHT,
	UI_GAMEFLOW_LAYOUT_JEWEL_CASE,
	UI_GAMEFLOW_LAYOUT_BOXES,
	UI_GAMEFLOW_LAYOUT_COUNT
} uiGameflowLayout_t;

/* Saved setting indices stay 0..4: the old Horizontal choice at 0 now names
 * 3D Boxes. gameflowLayout() resolves it to the BOXES renderer; Apps then
 * uses its original flat HORIZONTAL renderer. Do not expose both choices. */
enum { UI_GAMEFLOW_LIBRARY_LAYOUT_COUNT = UI_GAMEFLOW_LAYOUT_JEWEL_CASE + 1 };

/*
 * Menu-thread selection snapshot. generation is a wrapping uint32_t serial;
 * equal, stale, and exactly half-range-ambiguous snapshots are rejected.
 * A NONE directionHint asks the state machine to choose the shortest ring arc.
 */
typedef struct {
	uint32_t generation;
	uint32_t itemCount;
	uint32_t selectedIndex;
	uiGameflowDirection_t directionHint;
	/* Page jumps may place the old card outside the retained render window.
	 * Snap that explicit transition instead of animating an unrelated card. */
	bool snapTransition;
} uiGameflowSelectionSnapshot_t;

/*
 * Read-only render values copied into one persistent EV_GAMEFLOW event.
 * carouselPosition/Target are deliberately rebased local spring coordinates;
 * renderers should consume carouselTravel for the selected card's signed
 * one-slot transition.
 */
typedef struct {
	uiGameflowMode_t mode;
	uiGameflowDirection_t direction;
	uint32_t generation;
	uint32_t itemCount;
	uint32_t selectedIndex;
	uint32_t previousIndex;
	uint32_t focusIndex;
	float carouselPosition;
	float carouselTarget;
	float carouselTravel;
	float detailProgress;
	float launchProgress;
	/* Grid only: the highlight's column, sliding between whole columns.
	 * carouselTravel is then the rows' travel instead of the cards'. */
	float columnPosition;
	bool hasSnapshot;
	bool selectionPinned;
	bool transitioning;
} uiGameflowFrame_t;

/*
 * Fixed POD state: no heap ownership, I/O, locks, or external pointers. Embed
 * one instance in the retained event. The event wrapper must serialize every
 * state access (including Update, Frame, and IsGenerationCurrent) under the
 * same video mutex. A UIGameflow_Frame pointer must not be retained after that
 * mutex is released.
 */
typedef struct {
	uiGameflowMode_t mode;
	uiGameflowDirection_t direction;
	uint32_t generation;
	uint32_t itemCount;
	uint32_t selectedIndex;
	uint32_t previousIndex;
	uint32_t pinnedIndex;
	bool hasSnapshot;
	bool selectionPinned;
	uiMotionSpring_t carouselSpring;
	uiMotionSpring_t detailSpring;
	uiMotionSpring_t launchSpring;
	/* 0: the cards are one ring. Otherwise a grid of this many columns:
	 * the carousel spring travels in rows and columnSpring moves the
	 * highlight along its row. */
	uint32_t columns;
	uiMotionSpring_t columnSpring;
	uiGameflowFrame_t frame;
} uiGameflowState_t;

void UIGameflow_Init(uiGameflowState_t *state);

/* Lay the cards out as a grid of columns (0 keeps the ring), before the
 * first snapshot. The retained event keeps one layout for its lifetime. */
void UIGameflow_SetColumns(uiGameflowState_t *state, uint32_t columns);

/* Returns false without mutation when snapshot is null, equal, or stale. */
bool UIGameflow_ApplySnapshot(uiGameflowState_t *state,
	const uiGameflowSelectionSnapshot_t *snapshot, uiMotionMode_t motionMode);

/* Invalid modes safely select Library. Detail/Launch require a non-empty set. */
void UIGameflow_SetMode(uiGameflowState_t *state, uiGameflowMode_t mode,
	uiMotionMode_t motionMode);

/* Advance all retained motion on the video thread. Invalid motion snaps Off. */
void UIGameflow_Update(uiGameflowState_t *state, float deltaSeconds,
	uiMotionMode_t motionMode);

const uiGameflowFrame_t *UIGameflow_Frame(const uiGameflowState_t *state);
bool UIGameflow_IsGenerationCurrent(const uiGameflowState_t *state,
	uint32_t generation);

#endif
