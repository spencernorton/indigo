#include <stddef.h>
#include <limits.h>
#include <string.h>

#include "ui_home.h"

static const char *const faceLabels[UI_HOME_FACE_COUNT] = {
	"LIBRARY", "SOURCE", "SETTINGS", "SYSTEM", "APPS", "MEMORY CARDS",
	"EMULATORS", "FILE BROWSER"
};

/* Absent faces are bits of a mask. */
_Static_assert(UI_HOME_FACE_COUNT <= 16, "a face's absent bit fits a 16-bit mask");

/* The cube unless Settings says otherwise: Source on top, Settings on the
 * left, System on the right and Apps underneath, as the GameCube's own menu
 * has its four. Infinite turns through them in that order after Library. */
static const uint8_t defaultSides[UI_HOME_SIDE_COUNT] = {
	UI_HOME_FACE_SOURCE, UI_HOME_FACE_SETTINGS, UI_HOME_FACE_SYSTEM,
	UI_HOME_FACE_APPS
};

/* Classic: the direction that turns Library's side to each side. */
static const uiHomeInput_t sideInputs[UI_HOME_SIDE_COUNT] = {
	UI_HOME_INPUT_UP, UI_HOME_INPUT_LEFT, UI_HOME_INPUT_RIGHT,
	UI_HOME_INPUT_DOWN
};

static int positiveModulo(int value, int modulus)
{
	int result = value % modulus;
	return result < 0 ? result + modulus : result;
}

bool UIHome_IsFace(int face)
{
	return face >= UI_HOME_FACE_LIBRARY && face < UI_HOME_FACE_COUNT;
}

bool UIHome_IsSurface(int surface)
{
	return surface >= UI_HOME_SURFACE_RING &&
		surface < UI_HOME_SURFACE_COUNT;
}

/* The capabilities' sides, each face once, and those of their faces that
 * aren't there now: Apps without apps. */
static void resolveLayout(uiHomeCapabilities_t capabilities,
	uint8_t sides[UI_HOME_SIDE_COUNT], uint16_t *absent)
{
	const uint8_t *wanted = capabilities.customSides ? capabilities.sides :
		defaultSides;
	unsigned named = 0u;
	int side;

	*absent = 0u;
	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		int face = wanted[side];

		if(face <= (int)UI_HOME_FACE_LIBRARY || face >= (int)UI_HOME_FACE_COUNT ||
			(named & (1u << face)) != 0u) {
			sides[side] = UI_HOME_FACE_LIBRARY;
			continue;
		}
		named |= 1u << face;
		sides[side] = (uint8_t)face;
		if((face == (int)UI_HOME_FACE_APPS && !capabilities.hasApps) ||
			(face == (int)UI_HOME_FACE_EMULATORS && !capabilities.hasEmulators)) {
			*absent = (uint16_t)(*absent | (1u << face));
		}
	}
}

/* The face on a side when it is there now, else Library. */
static int presentFace(const uint8_t sides[UI_HOME_SIDE_COUNT],
	uint16_t absent, int side)
{
	int face = sides[side];

	if(face <= (int)UI_HOME_FACE_LIBRARY || face >= (int)UI_HOME_FACE_COUNT ||
		((unsigned)absent & (1u << face)) != 0u) {
		return UI_HOME_FACE_LIBRARY;
	}
	return face;
}

static int ringCount(const uint8_t sides[UI_HOME_SIDE_COUNT], uint16_t absent)
{
	int count = 1;
	int side;

	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		if(presentFace(sides, absent, side) != (int)UI_HOME_FACE_LIBRARY) {
			++count;
		}
	}
	return count;
}

int UIHome_FaceCount(uiHomeCapabilities_t capabilities)
{
	uint8_t sides[UI_HOME_SIDE_COUNT];
	uint16_t absent;

	resolveLayout(capabilities, sides, &absent);
	return ringCount(sides, absent);
}

bool UIHome_LayoutValid(const uiHomeState_t *state)
{
	unsigned named = 0u;
	int side;

	if(state == NULL) {
		return false;
	}
	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		int face = state->sides[side];

		if(face == (int)UI_HOME_FACE_LIBRARY) {
			continue;
		}
		if(face >= (int)UI_HOME_FACE_COUNT || (named & (1u << face)) != 0u) {
			return false;
		}
		named |= 1u << face;
	}
	return ((unsigned)state->absent & ~named) == 0u &&
		state->faceCount == ringCount(state->sides, state->absent);
}

uiHomeFace_t UIHome_RingFace(const uiHomeState_t *state, int32_t turnOrdinal)
{
	int place;
	int index = 1;
	int side;

	if(state == NULL || state->faceCount < 1) {
		return UI_HOME_FACE_LIBRARY;
	}
	place = positiveModulo((int)(turnOrdinal % (int32_t)state->faceCount),
		state->faceCount);
	for(side = 0; side < UI_HOME_SIDE_COUNT && place > 0; ++side) {
		int face = presentFace(state->sides, state->absent, side);

		if(face == (int)UI_HOME_FACE_LIBRARY) {
			continue;
		}
		if(index == place) {
			return (uiHomeFace_t)face;
		}
		++index;
	}
	return UI_HOME_FACE_LIBRARY;
}

int UIHome_RingIndex(const uiHomeState_t *state, uiHomeFace_t face)
{
	int index = 1;
	int side;

	if(state == NULL) {
		return -1;
	}
	if(face == UI_HOME_FACE_LIBRARY) {
		return 0;
	}
	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		int onSide = presentFace(state->sides, state->absent, side);

		if(onSide == (int)UI_HOME_FACE_LIBRARY) {
			continue;
		}
		if(onSide == (int)face) {
			return index;
		}
		++index;
	}
	return -1;
}

bool UIHome_DefaultSides(const uiHomeState_t *state)
{
	return state != NULL &&
		memcmp(state->sides, defaultSides, sizeof(defaultSides)) == 0;
}

uiHomeFace_t UIHome_SideFace(int side, int choice)
{
	if(side < 0 || side >= UI_HOME_SIDE_COUNT) {
		return UI_HOME_FACE_LIBRARY;
	}
	return (uiHomeFace_t)positiveModulo(defaultSides[side] + choice,
		UI_HOME_FACE_COUNT);
}

int UIHome_FaceSide(const uiHomeState_t *state, uiHomeFace_t face)
{
	int side;

	if(state == NULL || face == UI_HOME_FACE_LIBRARY) {
		return -1;
	}
	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		if(state->sides[side] == (int)face) {
			return side;
		}
	}
	return -1;
}

void UIHome_OrientationInit(uiHomeOrientation_t *orientation)
{
	if(orientation != NULL) {
		*orientation = (uiHomeOrientation_t) {{{1,0,0},{0,1,0},{0,0,1}}};
	}
}

bool UIHome_OrientationValid(const uiHomeOrientation_t *orientation)
{
	int row, col;
	int determinant;
	if(orientation == NULL) return false;
	for(row = 0; row < 3; ++row) {
		int rowCount = 0, colCount = 0;
		for(col = 0; col < 3; ++col) {
			int value = orientation->m[row][col];
			if(value < -1 || value > 1) return false;
			rowCount += value != 0;
			colCount += orientation->m[col][row] != 0;
		}
		if(rowCount != 1 || colCount != 1) return false;
	}
	determinant = orientation->m[0][0] * (orientation->m[1][1] *
		orientation->m[2][2] - orientation->m[1][2] * orientation->m[2][1]) -
		orientation->m[0][1] * (orientation->m[1][0] * orientation->m[2][2] -
		orientation->m[1][2] * orientation->m[2][0]) +
		orientation->m[0][2] * (orientation->m[1][0] * orientation->m[2][1] -
		orientation->m[1][1] * orientation->m[2][0]);
	return determinant == 1;
}

void UIHome_OrientationTurn(uiHomeOrientation_t *orientation,
	uiHomeTurnAxis_t axis, int direction)
{
	uiHomeOrientation_t before;
	int col, step = direction < 0 ? -1 : 1;
	if(!UIHome_OrientationValid(orientation) || direction == 0 ||
		(axis != UI_HOME_TURN_HORIZONTAL && axis != UI_HOME_TURN_VERTICAL)) return;
	before = *orientation;
	for(col = 0; col < 3; ++col) {
		/* Ry(-step*pi/2) or Rx(-step*pi/2), in screen coordinates. */
		if(axis == UI_HOME_TURN_HORIZONTAL) {
			orientation->m[0][col] = (int8_t)(-step * before.m[2][col]);
			orientation->m[2][col] = (int8_t)(step * before.m[0][col]);
		}
		else {
			orientation->m[1][col] = (int8_t)(step * before.m[2][col]);
			orientation->m[2][col] = (int8_t)(-step * before.m[1][col]);
		}
	}
}

void UIHome_OrientationMatrix(const uiHomeOrientation_t *orientation,
	float out[3][3])
{
	int row, col;
	bool valid = UIHome_OrientationValid(orientation);
	if(out == NULL) return;
	for(row = 0; row < 3; ++row) {
		for(col = 0; col < 3; ++col) {
			out[row][col] = valid ? (float)orientation->m[row][col] :
				(row == col ? 1.0f : 0.0f);
		}
	}
}

static void normalizeSelection(uiHomeState_t *state,
	uiHomeCapabilities_t capabilities)
{
	int rowCount = UIHome_RowCount(state->surface, capabilities);

	if(rowCount <= 0) {
		state->selection = 0;
	}
	else if(state->selection < 0 || state->selection >= rowCount) {
		state->selection = 0;
	}
}

static uiHomeCubeStyle_t cubeStyle(uiHomeCapabilities_t capabilities)
{
	return capabilities.style == UI_HOME_CUBE_CLASSIC ?
		UI_HOME_CUBE_CLASSIC : UI_HOME_CUBE_INFINITE;
}

/* A direction turns the cube as the ring does: Left and Right about the
 * vertical axis, Up and Down about the horizontal one; Left and Up count
 * down, Right and Down up. */
static uiHomeTurnAxis_t inputAxis(uiHomeInput_t input)
{
	return input == UI_HOME_INPUT_UP || input == UI_HOME_INPUT_DOWN ?
		UI_HOME_TURN_VERTICAL : UI_HOME_TURN_HORIZONTAL;
}

static int inputStep(uiHomeInput_t input)
{
	return input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP ? -1 : 1;
}

/* Classic's orientation for a face: the quarter turn from Library that
 * brings the face's side to the front. */
static void classicOrientation(const uiHomeState_t *state, uiHomeFace_t face,
	uiHomeOrientation_t *orientation)
{
	int side = UIHome_FaceSide(state, face);

	UIHome_OrientationInit(orientation);
	if(side >= 0) {
		UIHome_OrientationTurn(orientation, inputAxis(sideInputs[side]),
			inputStep(sideInputs[side]));
	}
}

void UIHome_Init(uiHomeState_t *state, uiHomeCapabilities_t capabilities)
{
	int sourcePlace;

	if(state == NULL) {
		return;
	}
	state->style = cubeStyle(capabilities);
	resolveLayout(capabilities, state->sides, &state->absent);
	state->faceCount = ringCount(state->sides, state->absent);
	/* Classic always starts on Library, the way to every other face. Its
	 * side is the front, so the orientation below is already Library's.
	 * Infinite with no source starts on Source, where the ring has it. */
	state->face = UI_HOME_FACE_LIBRARY;
	state->turnOrdinal = 0;
	sourcePlace = UIHome_RingIndex(state, UI_HOME_FACE_SOURCE);
	if(!capabilities.hasSource && state->style != UI_HOME_CUBE_CLASSIC &&
		sourcePlace > 0) {
		state->face = UI_HOME_FACE_SOURCE;
		state->turnOrdinal = (int32_t)sourcePlace;
	}
	state->surface = UI_HOME_SURFACE_RING;
	state->selection = 0;
	state->revision = 1u;
	UIHome_OrientationInit(&state->orientation);
	state->turnAxis = UI_HOME_TURN_NONE;
	state->turnDirection = 0;
}

static void moveFace(uiHomeState_t *state, uiHomeTurnAxis_t axis, int direction)
{
	int step = direction < 0 ? -1 : 1;
	/* Keep the semantic counter bounded without signed overflow. Orientation
	 * is absolute and does not depend on the counter's accumulated magnitude. */
	if((state->turnOrdinal == INT32_MAX && step > 0) ||
		(state->turnOrdinal == INT32_MIN && step < 0)) {
		state->turnOrdinal %= (int32_t)state->faceCount;
	}
	state->turnOrdinal += (int32_t)step;
	UIHome_OrientationTurn(&state->orientation, axis, step);
	state->turnAxis = axis;
	state->turnDirection = step;
	state->face = UIHome_RingFace(state, state->turnOrdinal);
	state->surface = UI_HOME_SURFACE_RING;
	state->selection = 0;
	state->revision++;
}

static void moveRow(uiHomeState_t *state, int direction,
	uiHomeCapabilities_t capabilities)
{
	int rowCount = UIHome_RowCount(state->surface, capabilities);
	int next;

	if(rowCount <= 1) {
		state->selection = 0;
		return;
	}
	next = positiveModulo(state->selection + (direction < 0 ? -1 : 1),
		rowCount);
	if(next != state->selection) {
		state->selection = next;
		state->revision++;
	}
}

static void enterSurface(uiHomeState_t *state, uiHomeSurface_t surface,
	int selection)
{
	state->surface = surface;
	state->selection = selection;
	state->revision++;
}

/* File Browser, System's row or its own face: Swiss's list, or with no
 * source the source picker, as the Library does. */
static uiHomeEffect_t openFiles(uiHomeCapabilities_t capabilities)
{
	return capabilities.hasSource ? UI_HOME_EFFECT_OPEN_FILES :
		UI_HOME_EFFECT_CHANGE_SOURCE;
}

static uiHomeEffect_t applyRing(uiHomeState_t *state, uiHomeInput_t input,
	uiHomeCapabilities_t capabilities)
{
	int sourcePlace;

	/* Library alone has nowhere to turn. */
	if(state->faceCount < 2 && input >= UI_HOME_INPUT_LEFT &&
		input <= UI_HOME_INPUT_DOWN) {
		return UI_HOME_EFFECT_NONE;
	}
	if(input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP) {
		moveFace(state, input == UI_HOME_INPUT_UP ?
			UI_HOME_TURN_VERTICAL : UI_HOME_TURN_HORIZONTAL, -1);
	}
	else if(input == UI_HOME_INPUT_RIGHT || input == UI_HOME_INPUT_DOWN) {
		moveFace(state, input == UI_HOME_INPUT_DOWN ?
			UI_HOME_TURN_VERTICAL : UI_HOME_TURN_HORIZONTAL, 1);
	}
	else if(input == UI_HOME_INPUT_RECENT) {
		return capabilities.hasRecent ?
			UI_HOME_EFFECT_OPEN_RECENT : UI_HOME_EFFECT_NONE;
	}
	else if(input == UI_HOME_INPUT_SETTINGS) {
		return UI_HOME_EFFECT_OPEN_SETTINGS;
	}
	else if(input == UI_HOME_INPUT_ACTIVATE) {
		switch(state->face) {
			case UI_HOME_FACE_LIBRARY:
				if(capabilities.hasSource) {
					return UI_HOME_EFFECT_OPEN_LIBRARY;
				}
				/* Turn to Source with its list open when it is a turn
				 * away, else go straight to the source picker. */
				sourcePlace = UIHome_RingIndex(state, UI_HOME_FACE_SOURCE);
				if(sourcePlace != 1 && (sourcePlace < 1 ||
					sourcePlace != state->faceCount - 1)) {
					return UI_HOME_EFFECT_CHANGE_SOURCE;
				}
				moveFace(state, UI_HOME_TURN_HORIZONTAL,
					sourcePlace == 1 ? 1 : -1);
				/* Keep one visible state revision per accepted input. */
				state->surface = UI_HOME_SURFACE_SOURCE;
				break;
			case UI_HOME_FACE_SOURCE:
				enterSurface(state, UI_HOME_SURFACE_SOURCE, 0);
				break;
			case UI_HOME_FACE_SETTINGS:
				return UI_HOME_EFFECT_OPEN_SETTINGS;
			case UI_HOME_FACE_SYSTEM:
				enterSurface(state, UI_HOME_SURFACE_SYSTEM, 0);
				break;
			case UI_HOME_FACE_APPS:
				return UI_HOME_EFFECT_OPEN_APPS;
			case UI_HOME_FACE_SAVES:
				return UI_HOME_EFFECT_OPEN_SAVES;
			case UI_HOME_FACE_EMULATORS:
				return UI_HOME_EFFECT_OPEN_EMULATORS;
			case UI_HOME_FACE_FILES:
				return openFiles(capabilities);
			default:
				break;
		}
	}
	return UI_HOME_EFFECT_NONE;
}

/* One quarter turn the way a direction points, onto face, in Classic. */
static void classicTurn(uiHomeState_t *state, uiHomeInput_t input,
	uiHomeFace_t face)
{
	UIHome_OrientationTurn(&state->orientation, inputAxis(input),
		inputStep(input));
	state->turnAxis = inputAxis(input);
	state->turnDirection = inputStep(input);
	state->face = face;
	state->turnOrdinal = (int32_t)UIHome_RingIndex(state, face);
	state->surface = UI_HOME_SURFACE_RING;
	state->selection = 0;
	state->revision++;
}

/* Classic: Library is the way between the faces, as the GameCube's main menu
 * is. From Library a direction turns to the face on that side, when there is
 * one; from any other face only the way back, or B, turns back to Library.
 * Any other turn is refused and changes nothing: there is no way round, so
 * Settings to System is Right, Right. A and Start are the ring's, but for A
 * with no source, which turns to Source's side as the ring's turns to it,
 * or opens the source picker when Source has no side. */
static uiHomeEffect_t applyClassic(uiHomeState_t *state,
	uiHomeInput_t input, uiHomeCapabilities_t capabilities)
{
	uiHomeInput_t back;
	int side;

	if(input == UI_HOME_INPUT_ACTIVATE &&
		state->face == UI_HOME_FACE_LIBRARY && !capabilities.hasSource) {
		side = UIHome_FaceSide(state, UI_HOME_FACE_SOURCE);
		if(side < 0) {
			return UI_HOME_EFFECT_CHANGE_SOURCE;
		}
		classicTurn(state, sideInputs[side], UI_HOME_FACE_SOURCE);
		/* Keep one visible state revision per accepted input. */
		state->surface = UI_HOME_SURFACE_SOURCE;
		return UI_HOME_EFFECT_NONE;
	}
	if(input != UI_HOME_INPUT_LEFT && input != UI_HOME_INPUT_RIGHT &&
		input != UI_HOME_INPUT_UP && input != UI_HOME_INPUT_DOWN &&
		input != UI_HOME_INPUT_BACK) {
		return applyRing(state, input, capabilities);
	}
	if(state->face != UI_HOME_FACE_LIBRARY) {
		switch(UIHome_FaceSide(state, state->face)) {
			case UI_HOME_SIDE_LEFT: back = UI_HOME_INPUT_RIGHT; break;
			case UI_HOME_SIDE_RIGHT: back = UI_HOME_INPUT_LEFT; break;
			case UI_HOME_SIDE_UP: back = UI_HOME_INPUT_DOWN; break;
			default: back = UI_HOME_INPUT_UP; break;
		}
		if(input == back || input == UI_HOME_INPUT_BACK) {
			classicTurn(state, back, UI_HOME_FACE_LIBRARY);
		}
		return UI_HOME_EFFECT_NONE;
	}
	for(side = 0; side < UI_HOME_SIDE_COUNT; ++side) {
		int face = presentFace(state->sides, state->absent, side);

		if(sideInputs[side] == input && face != (int)UI_HOME_FACE_LIBRARY) {
			classicTurn(state, input, (uiHomeFace_t)face);
			break;
		}
	}
	return UI_HOME_EFFECT_NONE;
}

static uiHomeEffect_t applySource(uiHomeState_t *state,
	uiHomeInput_t input, uiHomeCapabilities_t capabilities)
{
	if(input == UI_HOME_INPUT_UP) {
		moveRow(state, -1, capabilities);
	}
	else if(input == UI_HOME_INPUT_DOWN) {
		moveRow(state, 1, capabilities);
	}
	else if(input == UI_HOME_INPUT_BACK) {
		enterSurface(state, UI_HOME_SURFACE_RING, 0);
	}
	else if(input == UI_HOME_INPUT_ACTIVATE) {
		if(state->selection == 0) {
			return UI_HOME_EFFECT_CHANGE_SOURCE;
		}
		if(state->selection == 1 && capabilities.hasSource) {
			return UI_HOME_EFFECT_REFRESH;
		}
	}
	return UI_HOME_EFFECT_NONE;
}

static uiHomeEffect_t applySystem(uiHomeState_t *state,
	uiHomeInput_t input, uiHomeCapabilities_t capabilities)
{
	if(input == UI_HOME_INPUT_UP) {
		moveRow(state, -1, capabilities);
	}
	else if(input == UI_HOME_INPUT_DOWN) {
		moveRow(state, 1, capabilities);
	}
	else if(input == UI_HOME_INPUT_BACK) {
		enterSurface(state, UI_HOME_SURFACE_RING, 0);
	}
	else if(input == UI_HOME_INPUT_ACTIVATE) {
		if(state->selection == 0) {
			return UI_HOME_EFFECT_OPEN_INFO;
		}
		if(state->selection == 1) {
			return UI_HOME_EFFECT_OPEN_SAVES;
		}
		if(state->selection == 2) {
			return openFiles(capabilities);
		}
		if(state->selection == 3) {
			/* Cancellation is always the initially selected confirmation. */
			enterSurface(state, UI_HOME_SURFACE_RESTART_CONFIRM, 0);
		}
	}
	return UI_HOME_EFFECT_NONE;
}

static uiHomeEffect_t applyRestartConfirm(uiHomeState_t *state,
	uiHomeInput_t input, uiHomeCapabilities_t capabilities)
{
	if(input == UI_HOME_INPUT_LEFT || input == UI_HOME_INPUT_UP) {
		moveRow(state, -1, capabilities);
	}
	else if(input == UI_HOME_INPUT_RIGHT || input == UI_HOME_INPUT_DOWN) {
		moveRow(state, 1, capabilities);
	}
	else if(input == UI_HOME_INPUT_BACK) {
		enterSurface(state, UI_HOME_SURFACE_SYSTEM, 3);
	}
	else if(input == UI_HOME_INPUT_ACTIVATE) {
		if(state->selection == 0) {
			enterSurface(state, UI_HOME_SURFACE_SYSTEM, 3);
		}
		else if(state->selection == 1) {
			return UI_HOME_EFFECT_RESTART;
		}
	}
	return UI_HOME_EFFECT_NONE;
}

/* The ring follows the capabilities: Apps comes when the source has apps and
 * goes when it has none, and the sides and style are Settings'. The cube
 * stays where it is; standing on a face as it goes lands on Library. Classic's
 * glyphs keep their sides, so there the cube turns to the face in front's
 * own: Library's as Apps goes, or the side of the face Settings was opened
 * from once Settings closes. Like the selection's repair, this is published,
 * not counted as an input: the revision stays. */
static void reconcileFaces(uiHomeState_t *state,
	uiHomeCapabilities_t capabilities)
{
	uint8_t sides[UI_HOME_SIDE_COUNT];
	uint16_t absent;
	uiHomeCubeStyle_t style = cubeStyle(capabilities);
	int count;
	int place;

	resolveLayout(capabilities, sides, &absent);
	count = ringCount(sides, absent);
	if(state->faceCount == count && state->style == style &&
		state->absent == absent &&
		memcmp(state->sides, sides, sizeof(sides)) == 0 &&
		UIHome_RingIndex(state, state->face) >= 0) {
		return;
	}
	memcpy(state->sides, sides, sizeof(sides));
	state->absent = absent;
	state->faceCount = count;
	state->style = style;
	place = UIHome_RingIndex(state, state->face);
	if(place < 0) {
		state->face = UI_HOME_FACE_LIBRARY;
		state->surface = UI_HOME_SURFACE_RING;
		state->selection = 0;
		place = 0;
	}
	state->turnOrdinal = (int32_t)place;
	if(style == UI_HOME_CUBE_CLASSIC) {
		classicOrientation(state, state->face, &state->orientation);
	}
}

uiHomeEffect_t UIHome_Apply(uiHomeState_t *state, uiHomeInput_t input,
	uiHomeCapabilities_t capabilities)
{
	if(state == NULL || !UIHome_IsFace((int)state->face) ||
		!UIHome_IsSurface((int)state->surface)) {
		return UI_HOME_EFFECT_NONE;
	}
	reconcileFaces(state, capabilities);
	normalizeSelection(state, capabilities);
	switch(state->surface) {
		case UI_HOME_SURFACE_RING:
			return state->style == UI_HOME_CUBE_CLASSIC ?
				applyClassic(state, input, capabilities) :
				applyRing(state, input, capabilities);
		case UI_HOME_SURFACE_SOURCE:
			return applySource(state, input, capabilities);
		case UI_HOME_SURFACE_SYSTEM:
			return applySystem(state, input, capabilities);
		case UI_HOME_SURFACE_RESTART_CONFIRM:
			return applyRestartConfirm(state, input, capabilities);
		default:
			return UI_HOME_EFFECT_NONE;
	}
}

int UIHome_RowCount(uiHomeSurface_t surface,
	uiHomeCapabilities_t capabilities)
{
	switch(surface) {
		case UI_HOME_SURFACE_SOURCE:
			return capabilities.hasSource ? 2 : 1;
		case UI_HOME_SURFACE_SYSTEM:
			return 4;
		case UI_HOME_SURFACE_RESTART_CONFIRM:
			return 2;
		default:
			return 0;
	}
}

bool UIHome_RowEnabled(uiHomeSurface_t surface, int row,
	uiHomeCapabilities_t capabilities)
{
	return row >= 0 && row < UIHome_RowCount(surface, capabilities);
}

const char *UIHome_FaceLabel(uiHomeFace_t face)
{
	if(!UIHome_IsFace((int)face)) {
		return "";
	}
	return faceLabels[(int)face];
}

const char *UIHome_PrimaryHint(uiHomeFace_t face,
	uiHomeCapabilities_t capabilities)
{
	switch(face) {
		case UI_HOME_FACE_LIBRARY:
		case UI_HOME_FACE_FILES:
			return capabilities.hasSource ? "A  OPEN" : "A  SELECT SOURCE";
		case UI_HOME_FACE_SOURCE:
		case UI_HOME_FACE_SYSTEM:
			return "A  ENTER";
		case UI_HOME_FACE_SETTINGS:
		case UI_HOME_FACE_APPS:
		case UI_HOME_FACE_SAVES:
		case UI_HOME_FACE_EMULATORS:
			return "A  OPEN";
		default:
			return "";
	}
}

const char *UIHome_SurfaceTitle(uiHomeSurface_t surface)
{
	switch(surface) {
		case UI_HOME_SURFACE_SOURCE:
			return "SOURCE";
		case UI_HOME_SURFACE_SYSTEM:
			return "SYSTEM";
		case UI_HOME_SURFACE_RESTART_CONFIRM:
			return "RESTART INDIGO?";
		default:
			return "HOME";
	}
}

const char *UIHome_RowLabel(uiHomeSurface_t surface, int row)
{
	static const char *const systemRows[4] = {
		"SYSTEM INFORMATION", "MEMORY CARDS", "FILE BROWSER", "RESTART INDIGO"
	};

	if(surface == UI_HOME_SURFACE_SYSTEM) {
		return row >= 0 && row < 4 ? systemRows[row] : "";
	}
	if(row < 0 || row >= 2) {
		return "";
	}
	switch(surface) {
		case UI_HOME_SURFACE_SOURCE:
			return row == 0 ? "CHANGE SOURCE" : "REFRESH LIBRARY";
		case UI_HOME_SURFACE_RESTART_CONFIRM:
			return row == 0 ? "CANCEL" : "RESTART";
		default:
			return "";
	}
}
