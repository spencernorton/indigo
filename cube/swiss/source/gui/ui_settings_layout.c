/*
 * ui_settings_layout.c -- pure presentation geometry and text for the
 * Settings pages. Contract and coordinate rules: ui_settings_layout.h.
 * No Swiss headers, no drawing, no I/O, no allocation: everything is
 * computed into the caller's uiSetLayout_t, so the module is host-
 * testable under strict C99 and free of MEM1 cost beyond the caller's
 * single stack/struct instance.
 */

#include <string.h>

#include "ui_lang.h"
#include "ui_settings_layout.h"

/* The cheat browser's page: one column from x=40 to 600. */
#define PAGE_X0 40
#define PAGE_X1 600
#define ACCENT_Y 30
#define ACCENT_W 40
#define ACCENT_H 3

/* drawString anchors text on its vertical CENTER. */
#define TITLE_Y 63
#define TITLE_REGION_Y 49
#define TITLE_REGION_H 28
#define TITLE_GAP 16
#define SUBTITLE_Y 98
#define SUBTITLE_REGION_Y 90
#define SUBTITLE_REGION_H 16
#define TOP_DIVIDER_Y 115

/* Tabs: three equal cells, the L and R buttons outside them. */
#define TAB_CELL_W 84
#define TAB_CELL_H 24
#define TAB_GLYPH_W 15
#define TAB_GLYPH_GAP 8
/* A game's badge ("2 custom") takes the tabs' place. */
#define BADGE_REGION_X 400

#define SECTION_Y 132
#define POSITION_W 80

#define ROWS_Y0 148
#define ROW_PITCH 40
#define ROW_H 34
#define ROW_LABEL_X 52
#define ROW_VALUE_X0 300
#define ROW_VALUE_X 588
#define ROW_LABEL_GAP 12

#define SCROLL_X 606
#define SCROLL_W 2
#define SCROLL_MIN_THUMB 12

#define DESCRIPTION_Y 400
#define BOTTOM_DIVIDER_Y 413

#define FOOTER_Y 435
#define ACTION_H 24
#define ACTION_W_SAVE 80
#define ACTION_W_DISCARD 96
#define ACTION_GAP 8
#define HINT_ACTION_GAP 16

static const uiSetLayoutPage_t PAGES[UI_SETLAYOUT_PAGE_COUNT] = {
	{ "Quick", "Quick Settings",
	  "What you change between games.",
	  UI_SETLAYOUT_ROWS_QUICK, 0, 0 },
	/* "Game Defaults" is too wide for its tab in the console's font. */
	{ "Defaults", "Game Defaults",
	  "What every game starts with, unless it has its own.",
	  UI_SETLAYOUT_ROWS_GAME_DEFAULTS, 1, 0 },
	{ "Setup", "Setup",
	  "Set once. Press A to open a section.",
	  UI_SETLAYOUT_ROWS_SETUP, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Display",
	  "Video mode, cable, and TV.",
	  UI_SETLAYOUT_ROWS_DISPLAY, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Console",
	  "Colors, cube icons, sound, language, and system.",
	  UI_SETLAYOUT_ROWS_CONSOLE, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Storage",
	  "Settings device, SD cards, and the disc drive.",
	  UI_SETLAYOUT_ROWS_STORAGE, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Network",
	  "Adapter, file servers, and RetroTINK-4K.",
	  UI_SETLAYOUT_ROWS_NETWORK, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Library",
	  "Layout and folders, the recent list, and files.",
	  UI_SETLAYOUT_ROWS_LIBRARY, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Setup", "Developer",
	  "USB Gecko, memory, and debugging.",
	  UI_SETLAYOUT_ROWS_DEVELOPER, UI_SETLAYOUT_TAB_SETUP, 0 },
	{ "Game", "Game Settings",
	  "Saved for this game only.",
	  UI_SETLAYOUT_ROWS_GAME, UI_SETLAYOUT_NO_TAB, 1 },
};

const uiSetLayoutPage_t *UISetLayout_PageDesc(int page) {
	if (page < 0 || page >= UI_SETLAYOUT_PAGE_COUNT)
		return &PAGES[0];
	return &PAGES[page];
}

const char *UISetLayout_SettingsFileText(int state) {
	static const char *const text[UI_SETLAYOUT_SETTINGS_FILE_STATES] = {
		"Settings are saved in swiss/settings/global.ini.",
		"No swiss/settings/global.ini yet: using defaults.",
		"No device to save settings to.",
	};
	if (state < 0 || state >= UI_SETLAYOUT_SETTINGS_FILE_STATES)
		state = UI_SETLAYOUT_SETTINGS_FILE_NO_DEVICE;
	return text[state];
}

int UISetLayout_DiscardIndex(int page) {
	return UISetLayout_RowCount(page) + 1;
}

static int clampInt(int value, int lo, int hi) {
	if (value < lo)
		return lo;
	if (value > hi)
		return hi;
	return value;
}

static int hiddenTail[UI_SETLAYOUT_PAGE_COUNT];

void UISetLayout_HideTail(int page, int rows) {
	if (page >= 0 && page < UI_SETLAYOUT_PAGE_COUNT)
		hiddenTail[page] = clampInt(rows, 0, PAGES[page].rowCount - 1);
}

int UISetLayout_RowCount(int page) {
	if (page < 0 || page >= UI_SETLAYOUT_PAGE_COUNT)
		page = 0;
	return PAGES[page].rowCount - hiddenTail[page];
}

size_t UISetLayout_EllipsizeText(const char *source, size_t sourceLength,
	size_t maxText, uiSetLayoutEllipsizeMode_t mode, char *out,
	size_t outCapacity) {
	size_t left;
	size_t right;

	if (out == NULL || outCapacity == 0u)
		return 0u;
	out[0] = '\0';
	if (source == NULL)
		return 0u;
	if (sourceLength > UI_SETLAYOUT_VALUE_SOURCE_LIMIT)
		sourceLength = UI_SETLAYOUT_VALUE_SOURCE_LIMIT;
	if (maxText > outCapacity - 1u)
		maxText = outCapacity - 1u;
	if (sourceLength <= maxText) {
		memcpy(out, source, sourceLength);
		out[sourceLength] = '\0';
		return sourceLength;
	}
	if (maxText == 0u)
		return 0u;
	if (maxText == 1u) {
		out[0] = (char)UI_SETLAYOUT_ELLIPSIS_BYTE;
		out[1] = '\0';
		return 1u;
	}
	/* Neither cut splits a Cyrillic letter: the text comes out a byte
	 * shorter instead. */
	if (mode == UI_SETLAYOUT_ELLIPSIZE_MIDDLE) {
		left = UILang_Head(source, (maxText - 1u) / 2u);
		right = maxText - 1u - (maxText - 1u) / 2u;
		if (UILang_Head(source, sourceLength - right) != sourceLength - right)
			right--;
		memcpy(out, source, left);
		out[left] = (char)UI_SETLAYOUT_ELLIPSIS_BYTE;
		memcpy(out + left + 1u, source + sourceLength - right, right);
		maxText = left + 1u + right;
	}
	else {
		left = UILang_Head(source, maxText - 1u);
		memcpy(out, source, left);
		out[left] = (char)UI_SETLAYOUT_ELLIPSIS_BYTE;
		maxText = left + 1u;
	}
	out[maxText] = '\0';
	return maxText;
}

size_t UISetLayout_EllipsizeValue(const char *source, size_t sourceLength,
	uiSetLayoutEllipsizeMode_t mode, char *out, size_t outCapacity) {
	size_t maxText = outCapacity > 0u ? outCapacity - 1u : 0u;

	if (maxText > UI_SETLAYOUT_VALUE_TEXT_MAX)
		maxText = UI_SETLAYOUT_VALUE_TEXT_MAX;
	return UISetLayout_EllipsizeText(source, sourceLength, maxText, mode,
		out, outCapacity);
}

static int composeMeasuredText(const char *prefix, const char *body,
	const char *suffix, char *out, size_t outCapacity) {
	size_t prefixLength = strlen(prefix);
	size_t bodyLength = strlen(body);
	size_t suffixLength = strlen(suffix);
	size_t total = prefixLength + bodyLength + suffixLength;

	if (out == NULL || outCapacity == 0u || total >= outCapacity)
		return 0;
	memcpy(out, prefix, prefixLength);
	memcpy(out + prefixLength, body, bodyLength);
	memcpy(out + prefixLength + bodyLength, suffix, suffixLength);
	out[total] = '\0';
	return 1;
}

int UISetLayout_PrepareText(const char *source, size_t sourceLength,
	size_t maxSourceBytes, uiSetLayoutEllipsizeMode_t mode,
	uiSetLayoutTextKind_t kind, int selected, int enabled, int maxWidth,
	float preferredScale, float floorScale, uiSetLayoutTextMeasureFn measure,
	char *out, size_t outCapacity, uiSetLayoutTextFit_t *fit) {
	char body[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	const char *prefix = "";
	const char *suffix = "";
	size_t prefixLength;
	size_t suffixLength;
	size_t bodyCapacity;
	size_t bodyLimit;
	int width;
	float scale;
	int shortened = 0;

	if (out != NULL && outCapacity > 0u)
		out[0] = '\0';
	if (fit != NULL)
		memset(fit, 0, sizeof(*fit));
	if (source == NULL || out == NULL || outCapacity == 0u || fit == NULL ||
		measure == NULL || maxWidth <= 0 || preferredScale <= 0.0f ||
		floorScale <= 0.0f || preferredScale < floorScale ||
		kind < UI_SETLAYOUT_TEXT_PLAIN || kind > UI_SETLAYOUT_TEXT_EDITABLE)
		return 0;
	/* A whole string in Menu Language; a slice of one stays as it is. */
	if (source[sourceLength] == '\0' && UILang_Text(source) != source) {
		size_t translated;

		source = UILang_Text(source);
		translated = strlen(source);
		/* The caller's byte budget was set for the English text. */
		if (maxSourceBytes >= sourceLength)
			maxSourceBytes += translated > sourceLength ? translated - sourceLength : 0u;
		sourceLength = translated;
	}
	if (sourceLength > UI_SETLAYOUT_VALUE_SOURCE_LIMIT)
		sourceLength = UI_SETLAYOUT_VALUE_SOURCE_LIMIT;
	if (maxSourceBytes > UI_SETLAYOUT_VALUE_SOURCE_LIMIT)
		maxSourceBytes = UI_SETLAYOUT_VALUE_SOURCE_LIMIT;
	if (selected && enabled && kind == UI_SETLAYOUT_TEXT_CYCLE) {
		prefix = "< ";
		suffix = " >";
	}
	else if (selected && enabled && kind == UI_SETLAYOUT_TEXT_EDITABLE) {
		suffix = "\205";
	}
	prefixLength = strlen(prefix);
	suffixLength = strlen(suffix);
	if (prefixLength + suffixLength >= outCapacity)
		return 0;
	bodyCapacity = outCapacity - prefixLength - suffixLength;
	if (bodyCapacity > sizeof(body))
		bodyCapacity = sizeof(body);
	if (bodyCapacity == 0u)
		return 0;
	bodyLimit = sourceLength;
	if (bodyLimit > maxSourceBytes)
		bodyLimit = maxSourceBytes;
	if (bodyLimit >= bodyCapacity)
		bodyLimit = bodyCapacity - 1u;
	if (bodyLimit < sourceLength)
		shortened = 1;

	for (;;) {
		UISetLayout_EllipsizeText(source, sourceLength, bodyLimit, mode,
			body, bodyCapacity);
		if (!composeMeasuredText(prefix, body, suffix, out, outCapacity)) {
			out[0] = '\0';
			return 0;
		}
		width = measure(out);
		if (width < 0) {
			out[0] = '\0';
			return 0;
		}
		scale = width > 0 ? (float)maxWidth / (float)width : preferredScale;
		if (scale >= floorScale)
			break;
		if (bodyLimit == 0u) {
			out[0] = '\0';
			return 0;
		}
		bodyLimit--;
		shortened = 1;
	}
	if (scale > preferredScale)
		scale = preferredScale;
	fit->scale = scale;
	fit->unscaledWidth = width;
	fit->renderedWidth = (int)((float)width * scale + 0.999f);
	if (fit->renderedWidth > maxWidth)
		fit->renderedWidth = maxWidth;
	fit->ellipsized = shortened || bodyLimit < sourceLength;
	return 1;
}

static uiSetLayoutRect_t makeRect(int x, int y, int w, int h) {
	uiSetLayoutRect_t r;

	r.x = (short)x;
	r.y = (short)y;
	r.w = (short)w;
	r.h = (short)h;
	return r;
}

/* The tabs: one segmented control ending at the page's right edge, with
 * the L button before it and R after it. */
static void computeTabs(uiSetLayout_t *out) {
	int right = PAGE_X1 - TAB_GLYPH_W - TAB_GLYPH_GAP;
	int left = right - TAB_CELL_W * UI_SETLAYOUT_TAB_COUNT;
	int i;

	out->tabCount = UI_SETLAYOUT_TAB_COUNT;
	out->tabTrack = makeRect(left, TITLE_Y - TAB_CELL_H / 2,
		right - left, TAB_CELL_H);
	for (i = 0; i < UI_SETLAYOUT_TAB_COUNT; i++) {
		out->tabCell[i] = makeRect(left + i * TAB_CELL_W, out->tabTrack.y,
			TAB_CELL_W, TAB_CELL_H);
		out->tabLabelCenterX[i] = left + i * TAB_CELL_W + TAB_CELL_W / 2;
	}
	out->tabLabelY = TITLE_Y;
	out->tabLeftGlyphX = left - TAB_GLYPH_GAP - TAB_GLYPH_W;
	out->tabRightGlyphX = PAGE_X1;
}

static void computeHeader(const uiSetLayoutPage_t *desc, uiSetLayout_t *out) {
	int titleRight;

	out->accentBar = makeRect(PAGE_X0, ACCENT_Y, ACCENT_W, ACCENT_H);
	if (desc->tab != UI_SETLAYOUT_NO_TAB) {
		computeTabs(out);
		titleRight = out->tabLeftGlyphX;
	}
	else {
		out->badgeRegion = makeRect(BADGE_REGION_X, TITLE_REGION_Y,
			PAGE_X1 - BADGE_REGION_X, TITLE_REGION_H);
		out->badgeX = PAGE_X1;
		out->badgeY = TITLE_Y;
		out->badgeMaxWidth = out->badgeRegion.w;
		titleRight = BADGE_REGION_X;
	}
	out->titleRegion = makeRect(PAGE_X0, TITLE_REGION_Y,
		titleRight - TITLE_GAP - PAGE_X0, TITLE_REGION_H);
	out->titleX = PAGE_X0;
	out->titleY = TITLE_Y;
	out->titleMaxWidth = out->titleRegion.w;
	out->subtitleRegion = makeRect(PAGE_X0, SUBTITLE_REGION_Y,
		PAGE_X1 - PAGE_X0, SUBTITLE_REGION_H);
	out->subtitleX = PAGE_X0;
	out->subtitleY = SUBTITLE_Y;
	out->subtitleMaxWidth = out->subtitleRegion.w;
	out->topDivider = makeRect(PAGE_X0, TOP_DIVIDER_Y, PAGE_X1 - PAGE_X0, 1);

	out->sectionX = PAGE_X0;
	out->sectionY = SECTION_Y;
	out->sectionMaxWidth = PAGE_X1 - PAGE_X0 - POSITION_W - TITLE_GAP;
	out->positionX = PAGE_X1;
	out->positionY = SECTION_Y;
	out->positionMaxWidth = POSITION_W;
}

static void computeRows(const uiSetLayoutPage_t *desc, uiSetLayout_t *out) {
	int effRow, maxFirst, i;

	out->rowCount = desc->rowCount;
	out->visibleRowCount = desc->rowCount < UI_SETLAYOUT_VISIBLE_ROWS ?
	                       desc->rowCount : UI_SETLAYOUT_VISIBLE_ROWS;
	out->selectedRow = out->option < desc->rowCount ? out->option : -1;

	/* The cheat browser's window: the focus sits in its middle where the
	 * list allows. On an exit the window keeps showing the last rows, so it
	 * doesn't jump when the focus leaves them. */
	effRow = out->selectedRow >= 0 ? out->selectedRow : desc->rowCount - 1;
	maxFirst = desc->rowCount - out->visibleRowCount;
	if (maxFirst < 0)
		maxFirst = 0;
	out->firstVisibleRow =
		clampInt(effRow - UI_SETLAYOUT_VISIBLE_ROWS / 2, 0, maxFirst);

	for (i = 0; i < out->visibleRowCount; i++) {
		out->rowRect[i] = makeRect(PAGE_X0, ROWS_Y0 + i * ROW_PITCH,
			PAGE_X1 - PAGE_X0, ROW_H);
		out->rowTextY[i] = out->rowRect[i].y + ROW_H / 2;
	}
	out->rowLabelX = ROW_LABEL_X;
	out->rowLabelMaxWidth = ROW_VALUE_X0 - ROW_LABEL_GAP - ROW_LABEL_X;
	out->rowValueX0 = ROW_VALUE_X0;
	out->rowValueX = ROW_VALUE_X;
	/* A game's rows keep room for the CUSTOM chip before the value. */
	out->rowValueWidth = ROW_VALUE_X - ROW_VALUE_X0 - (desc->hasTags ?
		UI_SETLAYOUT_CHIP_W + UI_SETLAYOUT_CHIP_GAP : 0);
}

static void computeScroll(uiSetLayout_t *out) {
	int trackH = UI_SETLAYOUT_VISIBLE_ROWS * ROW_PITCH - (ROW_PITCH - ROW_H);
	int thumbH, travel;

	out->scrollVisible = out->rowCount > UI_SETLAYOUT_VISIBLE_ROWS;
	out->scrollTrack = makeRect(SCROLL_X, ROWS_Y0, SCROLL_W, trackH);
	if (!out->scrollVisible) {
		out->scrollThumb = out->scrollTrack;
		return;
	}
	thumbH = trackH * UI_SETLAYOUT_VISIBLE_ROWS / out->rowCount;
	if (thumbH < SCROLL_MIN_THUMB)
		thumbH = SCROLL_MIN_THUMB;
	travel = trackH - thumbH;
	out->scrollThumb = makeRect(SCROLL_X, ROWS_Y0 + travel *
		out->firstVisibleRow / (out->rowCount - UI_SETLAYOUT_VISIBLE_ROWS),
		SCROLL_W, thumbH);
}

/*
 * Footer: the button hints from the left edge, and the two exits at the
 * right, Save & Exit (option rowCount) then Discard & Exit (option
 * rowCount + 1), so Left/Right walk them in reading order.
 */
static void computeFooter(const uiSetLayoutPage_t *desc, uiSetLayout_t *out) {
	static const short widths[UI_SETLAYOUT_MAX_ACTIONS] = {
		ACTION_W_SAVE, ACTION_W_DISCARD
	};
	int x = PAGE_X1;
	int i;

	out->descriptionX = PAGE_X0;
	out->descriptionY = DESCRIPTION_Y;
	out->descriptionMaxWidth = PAGE_X1 - PAGE_X0;
	out->bottomDivider = makeRect(PAGE_X0, BOTTOM_DIVIDER_Y,
		PAGE_X1 - PAGE_X0, 1);

	out->actionKind[0] = UI_SETLAYOUT_ACTION_SAVE;
	out->actionKind[1] = UI_SETLAYOUT_ACTION_DISCARD;
	out->actionCount = UI_SETLAYOUT_MAX_ACTIONS;
	for (i = UI_SETLAYOUT_MAX_ACTIONS - 1; i >= 0; i--) {
		x -= widths[i];
		out->actionRect[i] = makeRect(x, FOOTER_Y - ACTION_H / 2,
			widths[i], ACTION_H);
		x -= ACTION_GAP;
	}
	out->selectedAction = out->option >= desc->rowCount ?
		out->option - desc->rowCount : -1;
	out->hintX = PAGE_X0;
	out->hintY = FOOTER_Y;
	out->hintMaxWidth = out->actionRect[0].x - HINT_ACTION_GAP - PAGE_X0;
}

void UISetLayout_Compute(int page, int option, int motionMode,
                         uiSetLayout_t *out) {
	const uiSetLayoutPage_t *desc;
	uiSetLayoutPage_t shown;

	memset(out, 0, sizeof(*out));
	page = clampInt(page, 0, UI_SETLAYOUT_PAGE_COUNT - 1);
	shown = *UISetLayout_PageDesc(page);
	shown.rowCount = UISetLayout_RowCount(page);
	desc = &shown;
	out->page = page;
	out->option = clampInt(option, 0, UISetLayout_DiscardIndex(page));
	out->currentTab = desc->tab;

	computeHeader(desc, out);
	computeRows(desc, out);
	computeScroll(out);
	computeFooter(desc, out);

	/* Motion: the focus card goes to the selected row's card or exit; Off
	 * snaps (focusAnimate 0) and must resolve on the next drawn frame.
	 * Reduced keeps eased focus travel but consumers drop secondary
	 * effects; Full may animate freely. */
	if (out->selectedRow >= 0)
		out->focusRect = out->rowRect[out->selectedRow - out->firstVisibleRow];
	else
		out->focusRect = out->actionRect[out->selectedAction];
	out->focusAnimate = motionMode != UI_SETLAYOUT_MOTION_OFF;
}

/*
 * The value list and the help card: one card width each, centred, with the
 * page's header (accent bar, title, divider) and footer (divider, hints).
 */
#define CARD_PAD 20
#define CARD_ACCENT_Y 18
#define CARD_TITLE_Y 42
#define CARD_DIVIDER_Y 60
#define CARD_BODY_Y 70
#define CARD_FOOT_H 44
#define CARD_HINT_FROM_BOTTOM 20
#define LIST_W 336
#define LIST_ROW_H 30
#define LIST_ROW_PITCH 34
#define LIST_ROW_INSET 14
#define LIST_SCROLL_GAP 10
#define HELP_W 560
#define HELP_PITCH 19

static void computeCard(int width, int bodyHeight, uiSetLayoutRect_t *card,
	uiSetLayoutRect_t *accentBar, int *titleX, int *titleY, int *titleMaxWidth,
	uiSetLayoutRect_t *topDivider, uiSetLayoutRect_t *bottomDivider,
	int *hintX, int *hintY) {
	int height = CARD_BODY_Y + bodyHeight + CARD_FOOT_H;
	int x = (640 - width) / 2;
	int y = (480 - height) / 2;

	*card = makeRect(x, y, width, height);
	*accentBar = makeRect(x + CARD_PAD, y + CARD_ACCENT_Y, ACCENT_W, ACCENT_H);
	*titleX = x + CARD_PAD;
	*titleY = y + CARD_TITLE_Y;
	*titleMaxWidth = width - CARD_PAD * 2;
	*topDivider = makeRect(x + CARD_PAD, y + CARD_DIVIDER_Y,
		width - CARD_PAD * 2, 1);
	*bottomDivider = makeRect(x + CARD_PAD, y + height - CARD_FOOT_H + 6,
		width - CARD_PAD * 2, 1);
	*hintX = x + CARD_PAD;
	*hintY = y + height - CARD_HINT_FROM_BOTTOM;
}

void UISetLayout_ComputeList(int count, int focus, uiSetListLayout_t *out) {
	int rowsH, rowW, i;

	memset(out, 0, sizeof(*out));
	out->count = clampInt(count, 1, UI_SETLAYOUT_LIST_MAX);
	out->focus = clampInt(focus, 0, out->count - 1);
	out->visibleCount = out->count < UI_SETLAYOUT_LIST_ROWS ?
		out->count : UI_SETLAYOUT_LIST_ROWS;
	out->first = clampInt(out->focus - out->visibleCount / 2, 0,
		out->count - out->visibleCount);
	out->scrollVisible = out->count > out->visibleCount;
	rowsH = out->visibleCount * LIST_ROW_PITCH - (LIST_ROW_PITCH - LIST_ROW_H);
	computeCard(LIST_W, rowsH, &out->card, &out->accentBar, &out->titleX,
		&out->titleY, &out->titleMaxWidth, &out->topDivider,
		&out->bottomDivider, &out->hintX, &out->hintY);
	rowW = LIST_W - LIST_ROW_INSET * 2 -
		(out->scrollVisible ? LIST_SCROLL_GAP : 0);
	for (i = 0; i < out->visibleCount; i++) {
		out->rowRect[i] = makeRect(out->card.x + LIST_ROW_INSET,
			out->card.y + CARD_BODY_Y + i * LIST_ROW_PITCH, rowW, LIST_ROW_H);
		out->rowTextY[i] = out->rowRect[i].y + LIST_ROW_H / 2;
	}
	out->rowTextX = out->card.x + LIST_ROW_INSET + 14;
	out->chipRight = out->card.x + LIST_ROW_INSET + rowW - 10;
	out->rowTextMaxWidth = out->chipRight - UI_SETLAYOUT_CHIP_W -
		UI_SETLAYOUT_CHIP_GAP - out->rowTextX;
	out->scrollTrack = makeRect(out->card.x + LIST_W - LIST_ROW_INSET - 2,
		out->card.y + CARD_BODY_Y, 2, rowsH);
	out->scrollThumb = out->scrollTrack;
	if (out->scrollVisible) {
		int thumbH = rowsH * out->visibleCount / out->count;
		if (thumbH < SCROLL_MIN_THUMB)
			thumbH = SCROLL_MIN_THUMB;
		out->scrollThumb.h = (short)thumbH;
		out->scrollThumb.y = (short)(out->scrollTrack.y + (rowsH - thumbH) *
			out->first / (out->count - out->visibleCount));
	}
	out->hintRightX = out->card.x + LIST_W - CARD_PAD;
	out->focusRect = out->rowRect[out->focus - out->first];
}

void UISetLayout_ComputeHelp(int lineCount, uiSetHelpLayout_t *out) {
	memset(out, 0, sizeof(*out));
	out->lineCount = clampInt(lineCount, 0, UI_SETLAYOUT_HELP_LINES);
	computeCard(HELP_W, out->lineCount * HELP_PITCH, &out->card,
		&out->accentBar, &out->titleX, &out->titleY, &out->titleMaxWidth,
		&out->topDivider, &out->bottomDivider, &out->hintX, &out->hintY);
	out->lineX = out->card.x + CARD_PAD;
	out->lineY0 = out->card.y + CARD_BODY_Y + HELP_PITCH / 2;
	out->linePitch = HELP_PITCH;
	out->lineMaxWidth = HELP_W - CARD_PAD * 2;
}

size_t UISetLayout_Label(const char *label, char *out, size_t capacity) {
	size_t length = 0u;

	if (out == NULL || capacity == 0u)
		return 0u;
	while (label != NULL && length + 1u < capacity && label[length] != '\0')
		length++;
	if (label != NULL)
		memmove(out, label, length);
	while (length > 0u && out[length - 1u] == ' ')
		length--;
	if (length > 0u && out[length - 1u] == ':')
		length--;
	while (length > 0u && out[length - 1u] == ' ')
		length--;
	out[length] = '\0';
	return length;
}

/* ------------------------------------------------------------------------
 * The one-line description of the focused row, from its help text.
 * --------------------------------------------------------------------- */
typedef struct {
	const char *text;
	size_t length;
} uiSetLayoutSpan_t;

static int isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r';
}

static char lowerAscii(char c) {
	return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/* The next line at *cursor, trimmed; 0 at the end of the text. */
static int nextLine(const char **cursor, uiSetLayoutSpan_t *line) {
	const char *start = *cursor;
	const char *end;

	if (*start == '\0')
		return 0;
	end = start;
	while (*end != '\0' && *end != '\n')
		end++;
	*cursor = *end == '\n' ? end + 1 : end;
	while (start < end && isBlank(*start))
		start++;
	while (end > start && isBlank(end[-1]))
		end--;
	line->text = start;
	line->length = (size_t)(end - start);
	return 1;
}

/* "Off - Rumble is turned off in games" names Off and describes it, and so
 * does "(Off) - ...". Names are short, so an ordinary sentence with a dash
 * in it isn't taken for one. */
static int optionLine(const uiSetLayoutSpan_t *line, uiSetLayoutSpan_t *name,
	uiSetLayoutSpan_t *description) {
	size_t i;

	for (i = 1u; i + 3u < line->length && i <= 24u; i++) {
		if (line->text[i] == ' ' && line->text[i + 1u] == '-' &&
			line->text[i + 2u] == ' ')
			break;
	}
	if (i + 3u >= line->length || i > 24u)
		return 0;
	name->text = line->text;
	name->length = i;
	if (name->length >= 2u && name->text[0] == '(' &&
		name->text[name->length - 1u] == ')') {
		name->text++;
		name->length -= 2u;
	}
	if (name->length == 0u || name->text[name->length - 1u] == '.')
		return 0;
	description->text = line->text + i + 3u;
	description->length = line->length - i - 3u;
	return 1;
}

static int sameWord(const char *a, size_t aLength, const char *b) {
	size_t i;

	for (i = 0u; i < aLength; i++) {
		if (b[i] == '\0' || lowerAscii(a[i]) != lowerAscii(b[i]))
			return 0;
	}
	return b[aLength] == '\0';
}

/* Which of the two-state families a word belongs to: 1 on, 2 off. */
static int stateOf(const char *text, size_t length) {
	static const char *const on[] = { "yes", "on", "enabled" };
	static const char *const off[] = { "no", "off", "disabled" };
	size_t i;

	for (i = 0u; i < sizeof(on) / sizeof(on[0]); i++) {
		if (sameWord(text, length, on[i]))
			return 1;
		if (sameWord(text, length, off[i]))
			return 2;
	}
	return 0;
}

/* Does a name ("Auto", or "11:10, 9:8") name value? */
static int namesValue(const uiSetLayoutSpan_t *name, const char *value,
	size_t valueLength) {
	size_t start = 0u;
	int valueState = stateOf(value, valueLength);

	while (start < name->length) {
		size_t end = start;
		size_t length;

		while (end < name->length && name->text[end] != ',')
			end++;
		length = end - start;
		if (length == valueLength) {
			size_t i;
			for (i = 0u; i < length &&
				lowerAscii(name->text[start + i]) == lowerAscii(value[i]); i++)
				;
			if (i == length)
				return 1;
		}
		if (valueState != 0 && stateOf(name->text + start, length) == valueState)
			return 1;
		start = end + 1u;
		while (start < name->length && name->text[start] == ' ')
			start++;
	}
	return 0;
}

static size_t appendSpan(char *out, size_t capacity, size_t used,
	const uiSetLayoutSpan_t *span) {
	size_t copy = span->length;

	if (used > 0u && used + 1u < capacity)
		out[used++] = ' ';
	if (copy > capacity - 1u - used)
		copy = capacity - 1u - used;
	memcpy(out + used, span->text, copy);
	used += copy;
	out[used] = '\0';
	return used;
}

size_t UISetLayout_HelpSummary(const char *help, const char *value,
	char *out, size_t capacity) {
	const char *cursor;
	uiSetLayoutSpan_t line, name, description;
	size_t valueLength = 0u;
	size_t used = 0u;
	int matches = 0;
	int continuing = 0;
	size_t i;

	if (out == NULL || capacity == 0u)
		return 0u;
	out[0] = '\0';
	if (help == NULL)
		return 0u;
	if (value != NULL) {
		while (value[valueLength] != '\0')
			valueLength++;
		while (valueLength > 0u && value[valueLength - 1u] == ' ')
			valueLength--;
	}

	/* The line that describes the current value, with the lines that
	 * continue it: they don't start with a capital. A value listed twice
	 * (per video mode, say) is too ambiguous to pick one. */
	cursor = help;
	if (!nextLine(&cursor, &line))
		return 0u;
	while (valueLength > 0u && nextLine(&cursor, &line)) {
		if (line.length == 0u) {
			continuing = 0;
		}
		else if (optionLine(&line, &name, &description)) {
			continuing = 0;
			if (namesValue(&name, value, valueLength)) {
				if (++matches > 1)
					break;
				used = appendSpan(out, capacity, 0u, &description);
				continuing = 1;
			}
		}
		else if (continuing && !(line.text[0] >= 'A' && line.text[0] <= 'Z')) {
			used = appendSpan(out, capacity, used, &line);
		}
		else {
			continuing = 0;
		}
	}
	if (matches == 1)
		return used;

	/* Otherwise the first sentence of the first paragraph, up to any list
	 * in it. Nothing when the paragraph opens with the list, or with a line
	 * that introduces one ("For 480i & 576i:"). */
	used = 0u;
	out[0] = '\0';
	cursor = help;
	(void)nextLine(&cursor, &line);
	do {
		if (!nextLine(&cursor, &line))
			return 0u;
	} while (line.length == 0u);
	while (line.length != 0u && !optionLine(&line, &name, &description)) {
		used = appendSpan(out, capacity, used, &line);
		if (!nextLine(&cursor, &line))
			break;
	}
	if (used == 0u || out[used - 1u] == ':') {
		out[0] = '\0';
		return 0u;
	}
	/* A sentence ends at a stop before a capital or a digit, so "e.g. /swiss"
	 * and "3.9.46" carry on. */
	for (i = 0u; i < used; i++) {
		if ((out[i] == '.' || out[i] == '!' || out[i] == '?') &&
			(i + 1u == used || (out[i + 1u] == ' ' && i + 2u < used &&
			((out[i + 2u] >= 'A' && out[i + 2u] <= 'Z') ||
			(out[i + 2u] >= '0' && out[i + 2u] <= '9'))))) {
			out[i + 1u] = '\0';
			return i + 1u;
		}
	}
	return used;
}
