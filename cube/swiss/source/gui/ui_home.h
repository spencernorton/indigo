#ifndef UI_HOME_H
#define UI_HOME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	UI_HOME_FACE_LIBRARY = 0,
	UI_HOME_FACE_SOURCE,
	UI_HOME_FACE_SETTINGS,
	UI_HOME_FACE_SYSTEM,
	/* Only while the source has apps (hasApps below): the ring is then five
	 * faces, Apps one turn left of Library (below it in Classic). */
	UI_HOME_FACE_APPS,
	/* On a side only when Settings puts them there: Memory Cards, also a
	 * row on System, and Emulators, while the source has a program in
	 * /emulators (hasEmulators below). */
	UI_HOME_FACE_SAVES,
	UI_HOME_FACE_EMULATORS,
	/* File Browser, also a row on System, the same way: Swiss's own file
	 * list at the source's root. */
	UI_HOME_FACE_FILES,
	UI_HOME_FACE_COUNT
} uiHomeFace_t;

/* The pictures a Home face can show, four per face and in face order: a
 * face's icon is face * UI_HOME_ICON_CHOICES + its choice (libraryIcon…
 * systemIcon in swiss.h), so no face can show another face's icon. Choice 0
 * is the face's own default. The cube draws every icon in one shade. */
#define UI_HOME_ICON_CHOICES 4
typedef enum {
	UI_HOME_ICON_CONTROLLER = 0,
	UI_HOME_ICON_BOOKS,
	UI_HOME_ICON_COVERS,
	UI_HOME_ICON_PLAY,
	UI_HOME_ICON_HUB,
	UI_HOME_ICON_DISC,
	UI_HOME_ICON_SD_CARD,
	UI_HOME_ICON_FOLDER,
	UI_HOME_ICON_SLIDERS,
	UI_HOME_ICON_GEAR,
	UI_HOME_ICON_TOGGLES,
	UI_HOME_ICON_DIAL,
	UI_HOME_ICON_CLOCK,
	UI_HOME_ICON_INFO,
	UI_HOME_ICON_POWER,
	UI_HOME_ICON_CHIP,
	/* Apps has one picture; its other choices draw nothing. So have Memory
	 * Cards, Emulators and File Browser (Source's folder). */
	UI_HOME_ICON_APPS,
	UI_HOME_ICON_SAVES = UI_HOME_FACE_SAVES * UI_HOME_ICON_CHOICES,
	UI_HOME_ICON_EMULATORS = UI_HOME_FACE_EMULATORS * UI_HOME_ICON_CHOICES,
	UI_HOME_ICON_FILES = UI_HOME_FACE_FILES * UI_HOME_ICON_CHOICES,
	UI_HOME_ICON_COUNT
} uiHomeIcon_t;

typedef enum {
	UI_HOME_SURFACE_RING = 0,
	UI_HOME_SURFACE_SOURCE,
	UI_HOME_SURFACE_SYSTEM,
	UI_HOME_SURFACE_RESTART_CONFIRM,
	UI_HOME_SURFACE_COUNT
} uiHomeSurface_t;

typedef enum {
	UI_HOME_INPUT_NONE = 0,
	UI_HOME_INPUT_LEFT,
	UI_HOME_INPUT_RIGHT,
	UI_HOME_INPUT_UP,
	UI_HOME_INPUT_DOWN,
	UI_HOME_INPUT_ACTIVATE,
	UI_HOME_INPUT_BACK,
	UI_HOME_INPUT_RECENT,
	/* Y: Settings from the ring, wherever its face is, or with none. */
	UI_HOME_INPUT_SETTINGS
} uiHomeInput_t;

typedef enum {
	UI_HOME_EFFECT_NONE = 0,
	UI_HOME_EFFECT_OPEN_LIBRARY,
	UI_HOME_EFFECT_CHANGE_SOURCE,
	UI_HOME_EFFECT_REFRESH,
	UI_HOME_EFFECT_OPEN_SETTINGS,
	UI_HOME_EFFECT_OPEN_INFO,
	UI_HOME_EFFECT_RESTART,
	UI_HOME_EFFECT_OPEN_RECENT,
	UI_HOME_EFFECT_OPEN_SAVES,
	UI_HOME_EFFECT_OPEN_APPS,
	UI_HOME_EFFECT_OPEN_EMULATORS,
	/* Swiss's own file list at the source's root, even where the Library
	 * would show. */
	UI_HOME_EFFECT_OPEN_FILES
} uiHomeEffect_t;

/* How the faces sit on the cube (Setup > Console > Cube). Infinite turns
 * round a ring of them in every direction. Classic keeps each face on a side
 * of its own, as the GameCube's own menu does: Library in front, Settings on
 * the left, System on the right, Source on top and Apps underneath, and
 * Library the way between them. */
typedef enum {
	UI_HOME_CUBE_INFINITE = 0,
	UI_HOME_CUBE_CLASSIC,
	UI_HOME_CUBE_COUNT
} uiHomeCubeStyle_t;

/* The four sides round Library, in the order the Infinite ring turns through
 * them after Library. Classic turns each to its own direction. */
typedef enum {
	UI_HOME_SIDE_UP = 0,
	UI_HOME_SIDE_LEFT,
	UI_HOME_SIDE_RIGHT,
	UI_HOME_SIDE_DOWN,
	UI_HOME_SIDE_COUNT
} uiHomeSide_t;

/* Library and a face on each side. */
#define UI_HOME_RING_MAX (1 + UI_HOME_SIDE_COUNT)

typedef struct {
	bool hasSource;
	bool hasRecent;
	/* The source's /apps folder holds a program: the Apps face shows. */
	bool hasApps;
	uiHomeCubeStyle_t style;
	/* The face on each side, UI_HOME_FACE_LIBRARY for none. Without
	 * customSides the sides are Source, Settings, System and Apps: a zeroed
	 * set of capabilities is the default cube. A face named twice keeps its
	 * first side; Library and anything that isn't a face mean none. */
	bool customSides;
	uint8_t sides[UI_HOME_SIDE_COUNT];
	/* The source's /emulators folder holds a program. */
	bool hasEmulators;
	/* Setup > Console's Face Labels and On-screen Controls off: no face
	 * name, or no button hints, under the cube. Zeroed, Home draws both. */
	bool hideFaceLabel;
	bool hideCommands;
} uiHomeCapabilities_t;

typedef enum {
	UI_HOME_TURN_NONE = 0,
	UI_HOME_TURN_HORIZONTAL,
	UI_HOME_TURN_VERTICAL
} uiHomeTurnAxis_t;

/* Exact proper signed-permutation matrix, mapping body coordinates to screen
 * coordinates. Screen-axis quarter turns pre-multiply this orientation. */
typedef struct {
	int8_t m[3][3];
} uiHomeOrientation_t;

/* Menu-thread-owned state. The ordinal selects the menu independently of the
 * cube's physical orientation, so coalesced mixed-axis turns lose no state. */
typedef struct {
	uiHomeFace_t face;
	uiHomeSurface_t surface;
	int selection;
	int32_t turnOrdinal;
	/* The faces the ring has now: Library, then each face on a side that is
	 * there now, Up, Left, Right and Down. face is always one of them, the
	 * one at turnOrdinal modulo faceCount. */
	int faceCount;
	uint32_t revision;
	uiHomeOrientation_t orientation;
	uiHomeTurnAxis_t turnAxis;
	int turnDirection;
	/* The capabilities' style, for the scene to place the glyphs and time
	 * the turns by. In Classic turnOrdinal is the face's place in the ring,
	 * and the orientation always the one that has the face's own side in
	 * front. */
	uiHomeCubeStyle_t style;
	/* The face on each side (UI_HOME_FACE_LIBRARY for none), and the faces
	 * on a side that aren't there now (bit 1 << face): Apps while the
	 * source has none. Such a face keeps its side but leaves the ring. */
	uint8_t sides[UI_HOME_SIDE_COUNT];
	uint16_t absent;
} uiHomeState_t;

void UIHome_OrientationInit(uiHomeOrientation_t *orientation);
bool UIHome_OrientationValid(const uiHomeOrientation_t *orientation);
void UIHome_OrientationTurn(uiHomeOrientation_t *orientation,
	uiHomeTurnAxis_t axis, int direction);
void UIHome_OrientationMatrix(const uiHomeOrientation_t *orientation,
	float out[3][3]);

#define UI_HOME_QUARTER_TURN_RADIANS 1.57079632679f

bool UIHome_IsFace(int face);
bool UIHome_IsSurface(int surface);
/* The faces in the capabilities' ring: Library and each face on a side that
 * is there; 5 for the default cube with apps, else 4. */
int UIHome_FaceCount(uiHomeCapabilities_t capabilities);
/* The state's sides name faces once each, its absent faces are on a side,
 * and faceCount counts its ring. Everything below needs that. */
bool UIHome_LayoutValid(const uiHomeState_t *state);
/* The face turnOrdinal lands on in the state's ring. */
uiHomeFace_t UIHome_RingFace(const uiHomeState_t *state, int32_t turnOrdinal);
/* A face's place in the ring, 0 for Library; -1 when it isn't in it. */
int UIHome_RingIndex(const uiHomeState_t *state, uiHomeFace_t face);
/* The side a face is on, there now or not; -1 for Library, or a face on
 * none. */
int UIHome_FaceSide(const uiHomeState_t *state, uiHomeFace_t face);
/* The sides are Source, Settings, System and Apps, as they are unless
 * Settings names others. */
bool UIHome_DefaultSides(const uiHomeState_t *state);
/* The face Settings' choice for a side puts there: choice 0 is the side's
 * default, and each next choice the next face round, none (Library)
 * included, so a zeroed choice is the default cube. */
uiHomeFace_t UIHome_SideFace(int side, int choice);
void UIHome_Init(uiHomeState_t *state, uiHomeCapabilities_t capabilities);
uiHomeEffect_t UIHome_Apply(uiHomeState_t *state, uiHomeInput_t input,
	uiHomeCapabilities_t capabilities);

int UIHome_RowCount(uiHomeSurface_t surface,
	uiHomeCapabilities_t capabilities);
bool UIHome_RowEnabled(uiHomeSurface_t surface, int row,
	uiHomeCapabilities_t capabilities);
const char *UIHome_FaceLabel(uiHomeFace_t face);
const char *UIHome_PrimaryHint(uiHomeFace_t face,
	uiHomeCapabilities_t capabilities);
const char *UIHome_SurfaceTitle(uiHomeSurface_t surface);
const char *UIHome_RowLabel(uiHomeSurface_t surface, int row);

#endif
