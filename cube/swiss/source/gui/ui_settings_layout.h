#ifndef UI_SETTINGS_LAYOUT_H
#define UI_SETTINGS_LAYOUT_H

#include <stddef.h>

/*
 * ui_settings_layout -- pure presentation geometry and text for the Settings
 * pages, drawn in the cheat browser's language: one full-screen page with a
 * header, a section line, six row cards in a window that follows the focus,
 * a one-line description of the focused row and a footer holding the button
 * hints and the two exits. It owns NO Swiss settings, mutates NO option
 * values, and performs NO drawing or I/O, so it compiles and tests on the
 * host under strict C99 (buildtools/ui/tests/test_ui_settings_layout).
 *
 * Pages are the Settings views: three tabs (Quick, Game
 * Defaults, Setup), the six Setup sections that open from Setup, and one
 * game's own settings, which has no tabs. The option-index model mirrors
 * show_settings() and is bound to settings.c's row tables by
 * _Static_asserts:
 *   options 0..rowCount-1          rows
 *   then Save & Exit
 *   then Discard & Exit            (index == UISetLayout_DiscardIndex(page))
 *
 * The page covers the whole screen, title bar included, like the cheat
 * browser. Its content stays inside the 5% action-safe area
 * x=32..608, y=24..456.
 */

#define UI_SETLAYOUT_PAGE_COUNT 10
#define UI_SETLAYOUT_TAB_COUNT 3
#define UI_SETLAYOUT_VISIBLE_ROWS 6
#define UI_SETLAYOUT_MAX_ACTIONS 2

/* Per-view row counts (bound to settings.c's row tables). */
#define UI_SETLAYOUT_ROWS_QUICK 9
#define UI_SETLAYOUT_ROWS_GAME_DEFAULTS 21
#define UI_SETLAYOUT_ROWS_SETUP 6
#define UI_SETLAYOUT_ROWS_DISPLAY 11
#define UI_SETLAYOUT_ROWS_CONSOLE 25
#define UI_SETLAYOUT_ROWS_STORAGE 8
#define UI_SETLAYOUT_ROWS_NETWORK 23
#define UI_SETLAYOUT_ROWS_LIBRARY 11
#define UI_SETLAYOUT_ROWS_DEVELOPER 4
#define UI_SETLAYOUT_ROWS_GAME 22

/* Tab owning each view: 0..2 are the tabs, the sections belong to Setup,
 * and a game's own settings belong to none. */
#define UI_SETLAYOUT_TAB_SETUP 2
#define UI_SETLAYOUT_NO_TAB (-1)

/* Safe-area bounds every computed rect must respect. */
#define UI_SETLAYOUT_SAFE_X0 32
#define UI_SETLAYOUT_SAFE_Y0 24
#define UI_SETLAYOUT_SAFE_X1 608
#define UI_SETLAYOUT_SAFE_Y1 456

/* The page: drawn 6 px past every screen edge and opaque, so it covers the
 * title bar's clock and the cube like the cheat browser. (Any translucency
 * let the clock show through the tabs.) */
#define UI_SETLAYOUT_PAGE_X (-6)
#define UI_SETLAYOUT_PAGE_Y (-6)
#define UI_SETLAYOUT_PAGE_W 652
#define UI_SETLAYOUT_PAGE_H 492
#define UI_SETLAYOUT_PAGE_ALPHA 255

/* Display values are bounded before font measurement or Draw* allocation.
 * The setting itself is never modified; only its one-frame presentation is
 * ellipsized. */
#define UI_SETLAYOUT_VALUE_SOURCE_LIMIT 1023u
#define UI_SETLAYOUT_VALUE_TEXT_MAX 24u
#define UI_SETLAYOUT_VALUE_BUFFER_SIZE 32u
#define UI_SETLAYOUT_LABEL_BUFFER_SIZE 96u
#define UI_SETLAYOUT_TEXT_BUFFER_SIZE 104u
#define UI_SETLAYOUT_ELLIPSIS_BYTE 0x85u

/* Settings text never shrinks below this native-grid scale. Wider strings
 * are ellipsized to the measured column before drawing. */
#define UI_SETLAYOUT_ROW_TEXT_FLOOR 0.60f

/* Type scale, the cheat browser's. */
#define UI_SETLAYOUT_TITLE_SCALE 1.05f
#define UI_SETLAYOUT_TITLE_FLOOR 0.80f
#define UI_SETLAYOUT_SUBTITLE_SCALE 0.64f
#define UI_SETLAYOUT_TAB_SCALE 0.50f
#define UI_SETLAYOUT_BADGE_SCALE 0.54f
#define UI_SETLAYOUT_SECTION_SCALE 0.42f
#define UI_SETLAYOUT_POSITION_SCALE 0.46f
#define UI_SETLAYOUT_LABEL_SCALE 0.66f
#define UI_SETLAYOUT_VALUE_SCALE 0.60f
#define UI_SETLAYOUT_CHIP_SCALE 0.36f
#define UI_SETLAYOUT_DESCRIPTION_SCALE 0.48f
#define UI_SETLAYOUT_HINT_SCALE 0.48f
#define UI_SETLAYOUT_ACTION_SCALE 0.50f

/* Inside a row card: an ON/OFF pill, a value pill with the < > it cycles
 * with, a text field, and the CUSTOM chip of a game's own rows. */
#define UI_SETLAYOUT_TOGGLE_W 62
#define UI_SETLAYOUT_TOGGLE_H 24
#define UI_SETLAYOUT_PILL_H 24
#define UI_SETLAYOUT_PILL_PAD 12
#define UI_SETLAYOUT_ARROW_W 14
#define UI_SETLAYOUT_FIELD_W 200
#define UI_SETLAYOUT_CHIP_W 50
#define UI_SETLAYOUT_CHIP_H 16
#define UI_SETLAYOUT_CHIP_GAP 8
#define UI_SETLAYOUT_SWATCH 14
#define UI_SETLAYOUT_SWATCH_GAP 6

typedef struct {
	short x, y, w, h;
} uiSetLayoutRect_t;

typedef enum {
	UI_SETLAYOUT_ACTION_SAVE = 0,
	UI_SETLAYOUT_ACTION_DISCARD
} uiSetLayoutActionKind_t;

/* Mirrors uiMotionMode_t values without dragging target headers in. */
typedef enum {
	UI_SETLAYOUT_MOTION_FULL = 0,
	UI_SETLAYOUT_MOTION_REDUCED,
	UI_SETLAYOUT_MOTION_OFF
} uiSetLayoutMotionMode_t;

typedef enum {
	UI_SETLAYOUT_ELLIPSIZE_TAIL = 0,
	UI_SETLAYOUT_ELLIPSIZE_MIDDLE
} uiSetLayoutEllipsizeMode_t;

typedef enum {
	UI_SETLAYOUT_TEXT_PLAIN = 0,
	UI_SETLAYOUT_TEXT_CYCLE,
	UI_SETLAYOUT_TEXT_EDITABLE
} uiSetLayoutTextKind_t;

typedef int (*uiSetLayoutTextMeasureFn)(const char *text);

typedef struct {
	float scale;
	int unscaledWidth;
	int renderedWidth;
	int ellipsized;
} uiSetLayoutTextFit_t;

typedef struct {
	const char *tabLabel;
	const char *title;
	const char *subtitle;
	int rowCount;
	int tab;                    /* owning tab, or UI_SETLAYOUT_NO_TAB */
	int hasTags;                /* rows can carry the CUSTOM chip */
} uiSetLayoutPage_t;

typedef struct {
	int page;
	int option;

	/* Header: accent bar, title, then the tabs (or a game's badge) on the
	 * title's line, the subtitle and a divider. The tabs are one segmented
	 * control with the L and R buttons at its ends. A game's own settings
	 * have no tabs (tabCount 0, currentTab -1) and show how many rows are
	 * its own instead. */
	uiSetLayoutRect_t accentBar;
	int titleX, titleY;
	int titleMaxWidth;
	uiSetLayoutRect_t titleRegion;
	int tabCount;
	int currentTab;
	uiSetLayoutRect_t tabTrack;
	uiSetLayoutRect_t tabCell[UI_SETLAYOUT_TAB_COUNT];
	int tabLabelCenterX[UI_SETLAYOUT_TAB_COUNT];
	int tabLabelY;
	int tabLeftGlyphX;          /* L: its left edge */
	int tabRightGlyphX;         /* R: its right edge */
	int badgeX, badgeY;         /* right-aligned */
	int badgeMaxWidth;
	uiSetLayoutRect_t badgeRegion;
	int subtitleX, subtitleY;
	int subtitleMaxWidth;
	uiSetLayoutRect_t subtitleRegion;
	uiSetLayoutRect_t topDivider;

	/* Section line: where this page sits (left) and "3 / 22" (right). */
	int sectionX, sectionY;
	int sectionMaxWidth;
	int positionX, positionY;   /* right-aligned */
	int positionMaxWidth;

	/* Rows: a window of at most UI_SETLAYOUT_VISIBLE_ROWS around the
	 * selection; row i of the window shows settable row
	 * firstVisibleRow + i. */
	int rowCount;
	int firstVisibleRow;
	int visibleRowCount;
	int selectedRow;            /* absolute row index, -1 on an exit */
	uiSetLayoutRect_t rowRect[UI_SETLAYOUT_VISIBLE_ROWS];
	int rowTextY[UI_SETLAYOUT_VISIBLE_ROWS];
	int rowLabelX;              /* left-aligned label anchor */
	int rowLabelMaxWidth;       /* fit boundary before the value column */
	int rowValueX0;             /* left edge of the value column */
	int rowValueX;              /* right edge of every value */
	int rowValueWidth;

	/* Slim scroll treatment (hidden when everything fits). */
	int scrollVisible;
	uiSetLayoutRect_t scrollTrack;
	uiSetLayoutRect_t scrollThumb;

	/* The focused row in one line, above the footer. */
	int descriptionX, descriptionY;
	int descriptionMaxWidth;
	uiSetLayoutRect_t bottomDivider;

	/* Footer: button hints from the left, the two exits at the right. */
	int hintX, hintY;
	int hintMaxWidth;
	int actionCount;
	int actionKind[UI_SETLAYOUT_MAX_ACTIONS];
	uiSetLayoutRect_t actionRect[UI_SETLAYOUT_MAX_ACTIONS];
	int selectedAction;         /* -1 or index into actionKind/actionRect */

	/* Motion: where the focus card goes (the selected row's card or exit)
	 * and whether the consumer may animate toward it. Off must resolve on
	 * the next drawn frame. */
	uiSetLayoutRect_t focusRect;
	int focusAnimate;           /* 0 under UI_SETLAYOUT_MOTION_OFF */
} uiSetLayout_t;

/* ------------------------------------------------------------------------
 * The page as the video thread draws it. settings.c fills one on the menu
 * thread, every string already fitted to its place, and FrameBufferMagic
 * copies it under the video mutex into the one page event Settings keeps
 * for its whole session. Pointer-free, so the copy is all the renderer
 * ever reads.
 * --------------------------------------------------------------------- */
typedef enum {
	UI_SETLAYOUT_ROW_TOGGLE = 0, /* two values: an ON/OFF pill */
	UI_SETLAYOUT_ROW_CHOICE,     /* a value pill, < > while focused */
	UI_SETLAYOUT_ROW_TEXT,       /* A opens the text editor */
	UI_SETLAYOUT_ROW_LINK,       /* A opens a Setup section */
	UI_SETLAYOUT_ROW_ACTION      /* A runs it (Reset to defaults) */
} uiSetLayoutRowKind_t;

#define UI_SETLAYOUT_HINT_ITEMS 4
#define UI_SETLAYOUT_HINT_CAPACITY 24u
#define UI_SETLAYOUT_SHORT_CAPACITY 32u

typedef struct {
	char label[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char value[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	float labelScale;
	float valueScale;
	short valueWidth;           /* value's drawn width, px */
	unsigned char kind;         /* uiSetLayoutRowKind_t */
	unsigned char on;           /* a toggle's state */
	unsigned char enabled;
	unsigned char custom;       /* a game's own value: the CUSTOM chip */
	unsigned char swatch;       /* 0, or 1 + the UI_COLOR_LAYER_ a dot shows */
	unsigned char placeholder;  /* an empty text value, drawn as "Not set" */
} uiSetPageRow_t;

typedef struct {
	uiSetLayout_t layout;
	char title[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char subtitle[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char tab[UI_SETLAYOUT_TAB_COUNT][UI_SETLAYOUT_SHORT_CAPACITY];
	char badge[UI_SETLAYOUT_SHORT_CAPACITY];
	char section[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	char position[UI_SETLAYOUT_SHORT_CAPACITY];
	char description[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char hint[UI_SETLAYOUT_HINT_ITEMS][UI_SETLAYOUT_HINT_CAPACITY];
	char action[UI_SETLAYOUT_MAX_ACTIONS][UI_SETLAYOUT_SHORT_CAPACITY];
	float titleScale;
	float subtitleScale;
	float tabScale[UI_SETLAYOUT_TAB_COUNT];
	float badgeScale;
	float descriptionScale;
	float actionScale[UI_SETLAYOUT_MAX_ACTIONS];
	short hintX[UI_SETLAYOUT_HINT_ITEMS];
	int hintCount;
	int view;                   /* a new view snaps the focus card */
	uiSetPageRow_t rows[UI_SETLAYOUT_VISIBLE_ROWS];
} uiSetPageSnapshot_t;

/* ------------------------------------------------------------------------
 * The value list A opens on a long choice, and the help card Y opens: cards
 * over a dimmed page. The list's focus is drawn where it is, never animated.
 * --------------------------------------------------------------------- */
#define UI_SETLAYOUT_LIST_ROWS 7
#define UI_SETLAYOUT_LIST_MAX 16
#define UI_SETLAYOUT_HELP_LINES 16
#define UI_SETLAYOUT_CARD_TITLE_SCALE 0.72f
#define UI_SETLAYOUT_HELP_SCALE 0.58f

typedef struct {
	uiSetLayoutRect_t card;
	uiSetLayoutRect_t accentBar;
	int titleX, titleY;
	int titleMaxWidth;
	uiSetLayoutRect_t topDivider;
	int count;
	int first;
	int visibleCount;
	int focus;                  /* absolute value index */
	uiSetLayoutRect_t rowRect[UI_SETLAYOUT_LIST_ROWS];
	int rowTextY[UI_SETLAYOUT_LIST_ROWS];
	int rowTextX;
	int rowTextMaxWidth;
	int chipRight;              /* the CURRENT chip's right edge */
	int scrollVisible;
	uiSetLayoutRect_t scrollTrack;
	uiSetLayoutRect_t scrollThumb;
	uiSetLayoutRect_t bottomDivider;
	int hintX, hintRightX, hintY;
	uiSetLayoutRect_t focusRect;
} uiSetListLayout_t;

typedef struct {
	uiSetListLayout_t layout;
	char title[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char value[UI_SETLAYOUT_LIST_ROWS][UI_SETLAYOUT_SHORT_CAPACITY];
	char hint[2][UI_SETLAYOUT_HINT_CAPACITY]; /* A Choose, B Cancel */
	float titleScale;
	float valueScale[UI_SETLAYOUT_LIST_ROWS];
	float hintScale[2];
	int current;                /* window slot of the value set, or -1 */
} uiSetListSnapshot_t;

typedef struct {
	uiSetLayoutRect_t card;
	uiSetLayoutRect_t accentBar;
	int titleX, titleY;
	int titleMaxWidth;
	uiSetLayoutRect_t topDivider;
	int lineCount;
	int lineX, lineY0, linePitch;
	int lineMaxWidth;
	uiSetLayoutRect_t bottomDivider;
	int hintX, hintY;
} uiSetHelpLayout_t;

/* The list of count values (at most UI_SETLAYOUT_LIST_MAX) with focus on
 * one: its window centres the focus, like the page's. */
void UISetLayout_ComputeList(int count, int focus, uiSetListLayout_t *out);

/* The help card for lineCount body lines (at most UI_SETLAYOUT_HELP_LINES),
 * centred on the screen. */
void UISetLayout_ComputeHelp(int lineCount, uiSetHelpLayout_t *out);

/* Static page descriptor (labels, row counts, nav availability). */
const uiSetLayoutPage_t *UISetLayout_PageDesc(int page);

/* Setup > Storage's subtitle says where the settings live and whether that
 * file holds them; settings.c picks the state. */
#define UI_SETLAYOUT_SETTINGS_FILE_SAVED 0
#define UI_SETLAYOUT_SETTINGS_FILE_MISSING 1
#define UI_SETLAYOUT_SETTINGS_FILE_NO_DEVICE 2
#define UI_SETLAYOUT_SETTINGS_FILE_STATES 3
const char *UISetLayout_SettingsFileText(int state);

/* Index of the Discard & Exit option: the rows shown + 1. */
int UISetLayout_DiscardIndex(int page);

/* The rows a page shows: its rowCount less its last `rows`, which it hides
 * (Settings › Network hides Sync Clock's without a network adapter). */
void UISetLayout_HideTail(int page, int rows);
int UISetLayout_RowCount(int page);

/* Full layout for one page. motionMode uses the mirror enum. */
void UISetLayout_Compute(int page, int option, int motionMode,
	uiSetLayout_t *out);

/* Copies at most UI_SETLAYOUT_VALUE_TEXT_MAX display bytes, replacing a
 * clipped tail or middle with the IPL ellipsis glyph. sourceLength is an
 * explicit available-byte count and is clamped to VALUE_SOURCE_LIMIT. */
size_t UISetLayout_EllipsizeValue(const char *source, size_t sourceLength,
	uiSetLayoutEllipsizeMode_t mode, char *out, size_t outCapacity);

/* General bounded ellipsizer used by the renderer's measured text-floor
 * loop. maxText is the requested display-byte budget and is clamped to the
 * destination capacity and source safety limit. */
size_t UISetLayout_EllipsizeText(const char *source, size_t sourceLength,
	size_t maxText, uiSetLayoutEllipsizeMode_t mode, char *out,
	size_t outCapacity);

/* Prepares one measured presentation copy. Decorations are retained while
 * only the source body is shortened, so selected cycle/text affordances never
 * disappear. A successful result is guaranteed to stay inside maxWidth at or
 * above floorScale. Invalid measurement or an impossible readable fit fails
 * closed with an empty output. */
int UISetLayout_PrepareText(const char *source, size_t sourceLength,
	size_t maxSourceBytes, uiSetLayoutEllipsizeMode_t mode,
	uiSetLayoutTextKind_t kind, int selected, int enabled, int maxWidth,
	float preferredScale, float floorScale, uiSetLayoutTextMeasureFn measure,
	char *out, size_t outCapacity, uiSetLayoutTextFit_t *fit);

/* A row's label as the page shows it: without the trailing colon the row
 * tables keep for prompts ("Menu Music:" is drawn "Menu Music"). out may be
 * label itself. */
size_t UISetLayout_Label(const char *label, char *out, size_t capacity);

/* The one line under the rows, from a row's help text (its tooltip), whose
 * "Name:" line is skipped:
 * - when the help lists the values ("Off - Rumble is turned off in
 *   games"), the line for value, with the lines that continue it (they
 *   don't start with a capital). Yes, On and Enabled name one state; No,
 *   Off and Disabled the other. A value listed twice, per video mode say,
 *   isn't picked;
 * - otherwise the first sentence of the first paragraph, up to any list in
 *   it. Nothing when that paragraph opens with the list or introduces one
 *   with a colon, rather than describe another value.
 * Returns the length written into out, cut to capacity (0: say nothing). */
size_t UISetLayout_HelpSummary(const char *help, const char *value,
	char *out, size_t capacity);

#endif
