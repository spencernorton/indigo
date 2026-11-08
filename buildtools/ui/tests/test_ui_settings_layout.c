/*
 * test_ui_settings_layout.c -- strict-C99 host tests for the Settings
 * layout module: the cheat browser's page (header, section line, six row
 * cards, description, footer with the two exits), the value list and help
 * cards, the measured text copies, and the one-line help summary. Exercises
 * every page and every option index (rows, Save/Discard boundaries),
 * windowing at the first/middle/last row, scroll treatment, safe-area
 * geometry for the entire input space, and Full/Reduced/Off motion targets.
 *
 * The option-index model these tests encode is bound to the real
 * settings.h enums by _Static_asserts in settings.c, so a drift breaks
 * the target build, not just this suite.
 */

#include <stdio.h>
#include <string.h>

#include "ui_cheats.h"
#include "ui_hint.h"
#include "ui_settings_layout.h"

static int failures;
static int checks;
static const char *currentTest = "";

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, \
		        currentTest, #cond); \
	} \
} while (0)

#define RUN(fn) do { \
	currentTest = #fn; \
	fn(); \
	printf("  %-46s %s\n", #fn, failures == failuresBefore ? "ok" : "FAILED"); \
	failuresBefore = failures; \
} while (0)

/* Mirror of settings.c's view tables (bound there by _Static_asserts). */
static const struct {
	int rowCount, tab, discardIndex;
} EXPECT[UI_SETLAYOUT_PAGE_COUNT] = {
	{  9,  0, 10 }, /* Quick */
	{ 21,  1, 22 }, /* Game Defaults: no Vertical Offset, which can't reach a game */
	{  6,  2,  7 }, /* Setup: one row per section */
	{ 11,  2, 12 }, /* Display */
	{ 25,  2, 26 }, /* Console: the four sides, Cube, Backdrop and Wave Color, Wave Speed, Clock, Temperature, Waves, Idle Animation, Face Labels, On-screen Controls and Menu Language too */
	{  8,  2,  9 }, /* Storage: its Library Path and Save Folder too */
	{ 23,  2, 24 }, /* Network: Sync Clock, NTP Server IP and Time Zone too */
	{ 11,  2, 12 }, /* Library: Load at startup, Library Folders and Saves on Details too; no File Browser Types */
	{  4,  2,  5 }, /* Developer */
	{ 22, -1, 23 }, /* one game's own settings: no tabs */
};

static int rectInSafeArea(uiSetLayoutRect_t r) {
	return r.x >= UI_SETLAYOUT_SAFE_X0 && r.y >= UI_SETLAYOUT_SAFE_Y0 &&
	       r.x + r.w <= UI_SETLAYOUT_SAFE_X1 &&
	       r.y + r.h <= UI_SETLAYOUT_SAFE_Y1;
}

static int rectsDisjoint(uiSetLayoutRect_t a, uiSetLayoutRect_t b) {
	return a.x + a.w <= b.x || b.x + b.w <= a.x ||
	       a.y + a.h <= b.y || b.y + b.h <= a.y;
}

static int rectInside(uiSetLayoutRect_t inner, uiSetLayoutRect_t outer) {
	return inner.x >= outer.x && inner.y >= outer.y &&
	       inner.x + inner.w <= outer.x + outer.w &&
	       inner.y + inner.h <= outer.y + outer.h;
}

static int sameRect(uiSetLayoutRect_t a, uiSetLayoutRect_t b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* Deterministic IPL-like widths: deliberately pessimistic for wide glyphs.
 * Production passes GetTextSizeInPixels; the policy only needs an unscaled
 * width callback and is independently testable here. */
static int testMeasure(const char *text) {
	int width = 0;

	if (text == NULL)
		return -1;
	while (*text != '\0') {
		unsigned char ch = (unsigned char)*text++;
		if (ch == 'W' || ch == 'M')
			width += 13;
		else if (ch == 'I' || ch == 'l' || ch == 'i')
			width += 5;
		else if (ch == ' ')
			width += 4;
		else if (ch == UI_SETLAYOUT_ELLIPSIS_BYTE)
			width += 9;
		else
			width += 9;
	}
	return width;
}

/* A button hint's width at scale 1, icons included, as settings.c fits it. */
static int testHintMeasure(const char *text) {
	float width = UIHint_LineWidth(text, 24, 1.0f, testMeasure);
	int whole = (int)width;

	return (float)whole < width ? whole + 1 : whole;
}

static int failingMeasure(const char *text) {
	(void)text;
	return -1;
}

static void test_page_descriptors_match_settings_enums(void) {
	int p;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *d = UISetLayout_PageDesc(p);
		CHECK(d->rowCount == EXPECT[p].rowCount);
		CHECK(d->tab == EXPECT[p].tab);
		CHECK(UISetLayout_DiscardIndex(p) == EXPECT[p].discardIndex);
		CHECK(d->tabLabel != NULL && d->title != NULL && d->subtitle != NULL);
		CHECK(strlen(d->tabLabel) > 0 && strlen(d->subtitle) > 0);
	}
	/* The tabs are the first pages, each owning itself. */
	for (p = 0; p < UI_SETLAYOUT_TAB_COUNT; p++)
		CHECK(UISetLayout_PageDesc(p)->tab == p);
	/* The cheat browser's window: six cards. */
	CHECK(UI_SETLAYOUT_VISIBLE_ROWS == UI_CHEATS_VISIBLE_ROWS);
}

static void test_action_mapping_every_boundary(void) {
	int p;
	uiSetLayout_t l;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *d = UISetLayout_PageDesc(p);
		int discard = UISetLayout_DiscardIndex(p);
		int opt;

		/* Last row still selects a row, not an action. */
		UISetLayout_Compute(p, d->rowCount - 1, 0, &l);
		CHECK(l.selectedRow == d->rowCount - 1);
		CHECK(l.selectedAction == -1);
		CHECK(l.actionCount == UI_SETLAYOUT_MAX_ACTIONS);

		/* Every action option maps to an exit in kind order, and the kinds
		 * mirror show_settings' index arithmetic exactly:
		 * Save = rowCount = discard-1, Discard = discard. */
		for (opt = d->rowCount; opt <= discard; opt++) {
			UISetLayout_Compute(p, opt, 0, &l);
			CHECK(l.selectedRow == -1);
			CHECK(l.selectedAction == opt - d->rowCount);
		}
		/* Exits sit left-to-right in kind order and never overlap:
		 * Left/Right walks option indices in kind order, so a placement
		 * swap would make the focus hop spatially. */
		UISetLayout_Compute(p, discard, 0, &l);
		{
			int a;
			for (a = 1; a < l.actionCount; a++)
				CHECK(l.actionRect[a - 1].x + l.actionRect[a - 1].w <
				      l.actionRect[a].x);
		}
		CHECK(l.actionKind[l.selectedAction] == UI_SETLAYOUT_ACTION_DISCARD);
		UISetLayout_Compute(p, discard - 1, 0, &l);
		CHECK(l.actionKind[l.selectedAction] == UI_SETLAYOUT_ACTION_SAVE);
		CHECK(discard - 1 == d->rowCount);
	}
}

static void test_windowing_first_middle_last(void) {
	uiSetLayout_t l;
	int p, opt;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *d = UISetLayout_PageDesc(p);
		int fits = d->rowCount <= UI_SETLAYOUT_VISIBLE_ROWS;

		/* First row: window starts at the top. */
		UISetLayout_Compute(p, 0, 0, &l);
		CHECK(l.firstVisibleRow == 0);
		CHECK(l.selectedRow == 0);
		CHECK(l.visibleRowCount ==
		      (fits ? d->rowCount : UI_SETLAYOUT_VISIBLE_ROWS));

		/* Every row: the focus is in the window, in its middle where the
		 * list allows (the cheat browser's UICheats_WindowStart). */
		for (opt = 0; opt < d->rowCount; opt++) {
			UISetLayout_Compute(p, opt, 0, &l);
			CHECK(l.selectedRow >= l.firstVisibleRow);
			CHECK(l.selectedRow < l.firstVisibleRow + l.visibleRowCount);
			CHECK(l.firstVisibleRow == UICheats_WindowStart(opt, d->rowCount));
		}

		/* Last row: window ends exactly at the last row. */
		UISetLayout_Compute(p, d->rowCount - 1, 0, &l);
		CHECK(l.firstVisibleRow + l.visibleRowCount == d->rowCount);
		CHECK(l.selectedRow == d->rowCount - 1);

		/* On an exit the window must not jump. */
		UISetLayout_Compute(p, UISetLayout_DiscardIndex(p), 0, &l);
		CHECK(l.selectedRow == -1);
		CHECK(l.firstVisibleRow + l.visibleRowCount == d->rowCount);
	}
}

static void test_scroll_treatment(void) {
	uiSetLayout_t l;
	int p;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *d = UISetLayout_PageDesc(p);
		int opt;
		int prevY = -1;

		UISetLayout_Compute(p, 0, 0, &l);
		/* The track runs beside the six cards, right of the page column. */
		CHECK(l.scrollTrack.x >= l.rowRect[0].x + l.rowRect[0].w);
		CHECK(l.scrollTrack.y == l.rowRect[0].y);
		CHECK(l.scrollTrack.h == UI_SETLAYOUT_VISIBLE_ROWS *
		      (l.rowRect[1].y - l.rowRect[0].y) -
		      (l.rowRect[1].y - l.rowRect[0].y - l.rowRect[0].h));
		if (d->rowCount <= UI_SETLAYOUT_VISIBLE_ROWS) {
			CHECK(!l.scrollVisible);
			continue;
		}
		/* Thumb tracks the window monotonically and stays in-track. */
		for (opt = 0; opt < d->rowCount; opt++) {
			UISetLayout_Compute(p, opt, 0, &l);
			CHECK(l.scrollVisible);
			CHECK(rectInside(l.scrollThumb, l.scrollTrack));
			CHECK(l.scrollThumb.h >= 12);
			CHECK(l.scrollThumb.y >= prevY);
			prevY = l.scrollThumb.y;
		}
		/* Top window -> thumb at top; bottom window -> thumb at end. */
		UISetLayout_Compute(p, 0, 0, &l);
		CHECK(l.scrollThumb.y == l.scrollTrack.y);
		UISetLayout_Compute(p, d->rowCount - 1, 0, &l);
		CHECK(l.scrollThumb.y + l.scrollThumb.h ==
		      l.scrollTrack.y + l.scrollTrack.h);
	}
}

/* Like the cheat browser, the page covers the whole screen, title bar and
 * cube included, and is opaque: its clock can't show through the tabs. */
static void test_page_covers_the_screen(void) {
	CHECK(UI_SETLAYOUT_PAGE_X < 0 && UI_SETLAYOUT_PAGE_Y < 0);
	CHECK(UI_SETLAYOUT_PAGE_X + UI_SETLAYOUT_PAGE_W > 640);
	CHECK(UI_SETLAYOUT_PAGE_Y + UI_SETLAYOUT_PAGE_H > 480);
	CHECK(UI_SETLAYOUT_PAGE_ALPHA >= 250);
}

static void test_value_column_and_ellipsize_bounds(void) {
	uiSetLayout_t l;
	char source[UI_SETLAYOUT_VALUE_SOURCE_LIMIT + 1u];
	char guarded[UI_SETLAYOUT_VALUE_BUFFER_SIZE + 2u];
	char *out = guarded + 1;
	static const size_t lengths[] = {0u, 64u, 128u, 1023u};
	size_t i, j, result;
	int p;

	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		UISetLayout_Compute(p, 0, UI_SETLAYOUT_MOTION_FULL, &l);
		/* Labels end before the value column; values end inside the card,
		 * the ON/OFF pill where the cheat browser puts it. */
		CHECK(l.rowLabelX > l.rowRect[0].x);
		CHECK(l.rowLabelX + l.rowLabelMaxWidth < l.rowValueX0);
		CHECK(l.rowValueX0 + l.rowValueWidth <= l.rowValueX);
		CHECK(l.rowValueX < l.rowRect[0].x + l.rowRect[0].w);
		CHECK(l.rowValueX - UI_SETLAYOUT_TOGGLE_W == 526);
		CHECK(l.rowValueWidth >= UI_SETLAYOUT_FIELD_W);
		CHECK(l.rowValueX - UI_SETLAYOUT_FIELD_W >= l.rowValueX0);
	}
	CHECK(UI_SETLAYOUT_ROW_TEXT_FLOOR >= 0.60f);

	for (j = 0u; j < sizeof(source) - 1u; j++)
		source[j] = (char)('A' + (int)(j % 26u));
	source[sizeof(source) - 1u] = '\0';
	for (i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		size_t expected = lengths[i] < UI_SETLAYOUT_VALUE_TEXT_MAX ?
			lengths[i] : UI_SETLAYOUT_VALUE_TEXT_MAX;
		memset(guarded, 0x5a, sizeof(guarded));
		result = UISetLayout_EllipsizeValue(source, lengths[i],
			UI_SETLAYOUT_ELLIPSIZE_TAIL, out,
			UI_SETLAYOUT_VALUE_BUFFER_SIZE);
		CHECK(result == expected);
		CHECK(out[result] == '\0');
		CHECK((unsigned char)guarded[0] == 0x5au);
		CHECK((unsigned char)guarded[sizeof(guarded) - 1u] == 0x5au);
		if (lengths[i] > UI_SETLAYOUT_VALUE_TEXT_MAX)
			CHECK((unsigned char)out[result - 1u] ==
			      UI_SETLAYOUT_ELLIPSIS_BYTE);

		memset(guarded, 0x5a, sizeof(guarded));
		result = UISetLayout_EllipsizeValue(source, lengths[i],
			UI_SETLAYOUT_ELLIPSIZE_MIDDLE, out,
			UI_SETLAYOUT_VALUE_BUFFER_SIZE);
		CHECK(result == expected);
		CHECK(out[result] == '\0');
		CHECK((unsigned char)guarded[0] == 0x5au);
		CHECK((unsigned char)guarded[sizeof(guarded) - 1u] == 0x5au);
		if (lengths[i] > UI_SETLAYOUT_VALUE_TEXT_MAX) {
			size_t left = (UI_SETLAYOUT_VALUE_TEXT_MAX - 1u) / 2u;
			size_t right = UI_SETLAYOUT_VALUE_TEXT_MAX - 1u - left;
			CHECK((unsigned char)out[left] ==
			      UI_SETLAYOUT_ELLIPSIS_BYTE);
			CHECK(memcmp(out + left + 1u,
			      source + lengths[i] - right, right) == 0);
		}
	}
	CHECK(UISetLayout_EllipsizeValue(source, sizeof(source),
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, out, 1u) == 0u);
	CHECK(out[0] == '\0');
	CHECK(UISetLayout_EllipsizeValue(source, sizeof(source),
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, out, 2u) == 1u);
	CHECK((unsigned char)out[0] == UI_SETLAYOUT_ELLIPSIS_BYTE);
}

static void test_measured_floor_ellipsize_budgets(void) {
	char source[65];
	char guarded[UI_SETLAYOUT_LABEL_BUFFER_SIZE + 2u];
	char *out = guarded + 1;
	size_t maxText;

	memset(source, 'W', sizeof(source) - 1u);
	source[sizeof(source) - 1u] = '\0';
	for (maxText = 1u; maxText < UI_SETLAYOUT_LABEL_BUFFER_SIZE;
	     maxText++) {
		size_t result;
		memset(guarded, 0x5a, sizeof(guarded));
		result = UISetLayout_EllipsizeText(source,
			sizeof(source) - 1u, maxText,
			UI_SETLAYOUT_ELLIPSIZE_TAIL, out,
			UI_SETLAYOUT_LABEL_BUFFER_SIZE);
		CHECK(result == (maxText < sizeof(source) - 1u ?
		      maxText : sizeof(source) - 1u));
		CHECK(out[result] == '\0');
		CHECK((unsigned char)guarded[0] == 0x5au);
		CHECK((unsigned char)guarded[sizeof(guarded) - 1u] == 0x5au);
		if (maxText < sizeof(source) - 1u)
			CHECK((unsigned char)out[result - 1u] ==
			      UI_SETLAYOUT_ELLIPSIS_BYTE);

		memset(guarded, 0x5a, sizeof(guarded));
		result = UISetLayout_EllipsizeText(source,
			sizeof(source) - 1u, maxText,
			UI_SETLAYOUT_ELLIPSIZE_MIDDLE, out,
			UI_SETLAYOUT_LABEL_BUFFER_SIZE);
		CHECK(out[result] == '\0');
		CHECK((unsigned char)guarded[0] == 0x5au);
		CHECK((unsigned char)guarded[sizeof(guarded) - 1u] == 0x5au);
		if (maxText < sizeof(source) - 1u) {
			size_t left = (maxText - 1u) / 2u;
			CHECK((unsigned char)out[left] ==
			      UI_SETLAYOUT_ELLIPSIS_BYTE);
		}
	}

	CHECK(UISetLayout_EllipsizeText(source, sizeof(source) - 1u,
	      999u, UI_SETLAYOUT_ELLIPSIZE_TAIL, out, 2u) == 1u);
	CHECK((unsigned char)out[0] == UI_SETLAYOUT_ELLIPSIS_BYTE);
	CHECK(UISetLayout_EllipsizeText(NULL, 4u, 4u,
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, out,
	      UI_SETLAYOUT_LABEL_BUFFER_SIZE) == 0u);
}

static void test_geometry_inside_safe_area_everywhere(void) {
	uiSetLayout_t l;
	int p, opt, i;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		int discard = UISetLayout_DiscardIndex(p);
		for (opt = 0; opt <= discard; opt++) {
			UISetLayout_Compute(p, opt, 0, &l);
			CHECK(rectInSafeArea(l.accentBar));
			CHECK(rectInSafeArea(l.titleRegion));
			CHECK(rectInSafeArea(l.subtitleRegion));
			CHECK(rectInSafeArea(l.topDivider));
			CHECK(rectInSafeArea(l.bottomDivider));
			if (l.tabCount > 0)
				CHECK(rectInSafeArea(l.tabTrack));
			else
				CHECK(rectInSafeArea(l.badgeRegion));
			for (i = 0; i < l.tabCount; i++)
				CHECK(rectInSafeArea(l.tabCell[i]));
			for (i = 0; i < l.visibleRowCount; i++)
				CHECK(rectInSafeArea(l.rowRect[i]));
			for (i = 0; i < l.actionCount; i++)
				CHECK(rectInSafeArea(l.actionRect[i]));
			CHECK(rectInSafeArea(l.scrollTrack));
			CHECK(rectInSafeArea(l.scrollThumb));
			CHECK(rectInSafeArea(l.focusRect));
			CHECK(l.titleX >= UI_SETLAYOUT_SAFE_X0 &&
			      l.titleY >= UI_SETLAYOUT_SAFE_Y0);
			CHECK(l.positionX <= UI_SETLAYOUT_SAFE_X1);
			CHECK(l.hintX >= UI_SETLAYOUT_SAFE_X0 &&
			      l.hintY + 12 <= UI_SETLAYOUT_SAFE_Y1);
		}
	}
}

/* The cheat browser's bands, top to bottom, never touching: accent bar,
 * title line (tabs or badge on it), subtitle, divider, section line, row
 * cards, description, divider, footer. */
static void test_explicit_text_and_vertical_ownership(void) {
	uiSetLayout_t l;
	int p, opt, mode, i;

	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		int discard = UISetLayout_DiscardIndex(p);
		for (opt = 0; opt <= discard; opt++) {
			for (mode = UI_SETLAYOUT_MOTION_FULL;
			     mode <= UI_SETLAYOUT_MOTION_OFF; mode++) {
				UISetLayout_Compute(p, opt, mode, &l);
				/* The cheat browser's own metrics. */
				CHECK(l.accentBar.x == 40 && l.accentBar.y == 30 &&
				      l.accentBar.w == 40 && l.accentBar.h == 3);
				CHECK(l.titleX == 40 && l.titleY == 63);
				CHECK(l.subtitleX == 40 && l.subtitleY == 98);
				CHECK(l.topDivider.y == 115 && l.topDivider.w == 560);
				CHECK(l.sectionY == 132 && l.positionX == 600);
				CHECK(l.rowRect[0].x == 40 && l.rowRect[0].y == 148 &&
				      l.rowRect[0].w == 560 && l.rowRect[0].h == 34);
				CHECK(l.descriptionY == 400);
				CHECK(l.bottomDivider.y == 413);
				CHECK(l.hintY == 435);

				CHECK(l.accentBar.y + l.accentBar.h < l.titleRegion.y);
				CHECK(l.titleY > l.titleRegion.y &&
				      l.titleY < l.titleRegion.y + l.titleRegion.h);
				CHECK(l.titleRegion.y + l.titleRegion.h <
				      l.subtitleRegion.y);
				CHECK(l.subtitleRegion.y + l.subtitleRegion.h <
				      l.topDivider.y);
				CHECK(l.topDivider.y + 8 < l.sectionY);
				CHECK(l.sectionY + 8 < l.rowRect[0].y);
				CHECK(l.titleMaxWidth == l.titleRegion.w);
				CHECK(l.subtitleMaxWidth == l.subtitleRegion.w);
				CHECK(l.sectionX + l.sectionMaxWidth + 16 <=
				      l.positionX - l.positionMaxWidth);
				/* Tabs or a game's badge share the title line, right of
				 * the title and clear of it. */
				if (l.tabCount > 0) {
					CHECK(rectsDisjoint(l.titleRegion, l.tabTrack));
					CHECK(l.titleRegion.x + l.titleRegion.w + 16 <=
					      l.tabLeftGlyphX);
					CHECK(l.tabTrack.y < l.titleY &&
					      l.tabTrack.y + l.tabTrack.h > l.titleY);
					CHECK(l.badgeMaxWidth == 0);
				}
				else {
					CHECK(rectsDisjoint(l.titleRegion, l.badgeRegion));
					CHECK(l.titleRegion.x + l.titleRegion.w + 16 <=
					      l.badgeRegion.x);
					CHECK(l.badgeX == l.badgeRegion.x + l.badgeRegion.w);
					CHECK(l.badgeMaxWidth == l.badgeRegion.w);
					CHECK(l.badgeY == l.titleY);
				}
				for (i = 0; i < l.visibleRowCount; i++) {
					CHECK(l.rowTextY[i] == l.rowRect[i].y +
					      l.rowRect[i].h / 2);
					CHECK(l.rowRect[i].y == 148 + i * 40);
					if (i > 0)
						CHECK(l.rowRect[i - 1].y + l.rowRect[i - 1].h <
						      l.rowRect[i].y);
				}
				/* Six cards always leave room for the description, and
				 * the footer sits under the divider. */
				CHECK(148 + UI_SETLAYOUT_VISIBLE_ROWS * 40 <
				      l.descriptionY - 8);
				CHECK(l.descriptionY + 8 < l.bottomDivider.y);
				CHECK(l.bottomDivider.y < l.actionRect[0].y);
				CHECK(l.descriptionMaxWidth == 560);
				for (i = 0; i < l.actionCount; i++) {
					CHECK(l.actionRect[i].y < l.hintY &&
					      l.actionRect[i].y + l.actionRect[i].h > l.hintY);
					CHECK(l.hintX + l.hintMaxWidth + 16 <=
					      l.actionRect[i].x);
				}
				CHECK(l.actionRect[l.actionCount - 1].x +
				      l.actionRect[l.actionCount - 1].w == 600);
			}
		}
	}
}

static void test_measured_copies_cover_real_extremes(void) {
	static const struct {
		const char *label;
		const char *value;
	} samples[UI_SETLAYOUT_PAGE_COUNT] = {
		{ "Load GameCube Main Menu", "Apploader" },
		{ "Emulate Broadband Adapter:", "Auto (Progressive)" },
		{ "Developer", "USB Gecko, memory, debug" },
		{ "RetroTINK-4K HDMI Input:", "PAL 480p (Auto) " },
		{ "CPU Temperature Calibration:", "English (US)" },
		{ "Disable MemCard PRO GameID:", "Slot A&B" },
		{ "RetroTINK-4K Host IP:", "255.255.255.255" },
		{ "Hide unknown file types:", "*/games/*/*" },
		{ "Simulated MRAM Size:", "Serial Port 2" },
		{ "Emulate Broadband Adapter:", "English (US)" },
	};
	/* Every footer the pages draw, button icons included; the game page's
	 * holds four. */
	static const char *const footer[] = {
		"A  Choose", "Y  Help", "X  Default", "B  Done"
	};
	uiSetLayout_t l;
	uiSetLayoutTextFit_t fit;
	uiSetHelpLayout_t help;
	char out[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char label[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	char longest[UI_SETLAYOUT_VALUE_SOURCE_LIMIT + 1u];
	char tooltipLine[69];
	int p, mode, i, used;

	memset(longest, 'W', sizeof(longest) - 1u);
	longest[sizeof(longest) - 1u] = '\0';
	memset(tooltipLine, 'W', sizeof(tooltipLine) - 1u);
	tooltipLine[sizeof(tooltipLine) - 1u] = '\0';
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *desc = UISetLayout_PageDesc(p);
		for (mode = UI_SETLAYOUT_MOTION_FULL;
		     mode <= UI_SETLAYOUT_MOTION_OFF; mode++) {
			UISetLayout_Compute(p, 0, mode, &l);
			CHECK(UISetLayout_PrepareText(desc->title,
			      strlen(desc->title), UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
			      0, 1, l.titleMaxWidth, UI_SETLAYOUT_TITLE_SCALE,
			      UI_SETLAYOUT_TITLE_FLOOR, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(fit.renderedWidth <= l.titleMaxWidth);
			CHECK(fit.scale >= UI_SETLAYOUT_TITLE_FLOOR);
			CHECK(!fit.ellipsized);
			CHECK(UISetLayout_PrepareText(desc->subtitle,
			      strlen(desc->subtitle), UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
			      0, 1, l.subtitleMaxWidth, UI_SETLAYOUT_SUBTITLE_SCALE,
			      UI_SETLAYOUT_SUBTITLE_SCALE, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(fit.renderedWidth <= l.subtitleMaxWidth);
			CHECK(!fit.ellipsized);
			for (i = 0; i < l.tabCount; i++) {
				const char *tab = UISetLayout_PageDesc(i)->tabLabel;
				CHECK(UISetLayout_PrepareText(tab, strlen(tab),
				      UI_SETLAYOUT_SHORT_CAPACITY - 1u,
				      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
				      0, 1, l.tabCell[i].w - 12, UI_SETLAYOUT_TAB_SCALE,
				      UI_SETLAYOUT_TAB_SCALE, testMeasure, out,
				      sizeof(out), &fit));
				CHECK(!fit.ellipsized);
			}
			if (l.tabCount == 0) {
				CHECK(UISetLayout_PrepareText("No custom settings", 18u,
				      18u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
				      UI_SETLAYOUT_TEXT_PLAIN, 0, 1, l.badgeMaxWidth,
				      UI_SETLAYOUT_BADGE_SCALE, UI_SETLAYOUT_BADGE_SCALE,
				      testMeasure, out, sizeof(out), &fit));
				CHECK(!fit.ellipsized);
			}
			CHECK(UISetLayout_PrepareText("SETUP  \273  DEVELOPER", 19u,
			      19u, UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
			      0, 1, l.sectionMaxWidth, UI_SETLAYOUT_SECTION_SCALE,
			      UI_SETLAYOUT_SECTION_SCALE, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(!fit.ellipsized);
			CHECK(UISetLayout_PrepareText("22 / 22", 7u, 7u,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
			      l.positionMaxWidth, UI_SETLAYOUT_POSITION_SCALE,
			      UI_SETLAYOUT_POSITION_SCALE, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(!fit.ellipsized);
			/* The footer's hints, spaced 22 px apart, and both exits. */
			used = 0;
			for (i = 0; i < 4; i++) {
				CHECK(UISetLayout_PrepareText(footer[i],
				      strlen(footer[i]), strlen(footer[i]),
				      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
				      0, 1, l.hintMaxWidth - used, UI_SETLAYOUT_HINT_SCALE,
				      UI_SETLAYOUT_HINT_SCALE, testHintMeasure, out,
				      sizeof(out), &fit));
				CHECK(!fit.ellipsized);
				used += fit.renderedWidth + 22;
			}
			CHECK(used - 22 <= l.hintMaxWidth);
			CHECK(UISetLayout_PrepareText("Discard & Exit", 14u, 14u,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
			      l.actionRect[1].w - 12, UI_SETLAYOUT_ACTION_SCALE,
			      UI_SETLAYOUT_ACTION_SCALE, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(!fit.ellipsized);

			/* Labels at their size, never below the floor, and values in
			 * a pill with room for its arrows, or a text field. */
			(void)UISetLayout_Label(samples[p].label, label, sizeof(label));
			CHECK(UISetLayout_PrepareText(label, strlen(label),
			      UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
			      l.rowLabelMaxWidth, UI_SETLAYOUT_LABEL_SCALE,
			      UI_SETLAYOUT_ROW_TEXT_FLOOR, testMeasure, out,
			      sizeof(out), &fit));
			CHECK(fit.renderedWidth <= l.rowLabelMaxWidth);
			CHECK(fit.scale >= UI_SETLAYOUT_ROW_TEXT_FLOOR);
			CHECK(!fit.ellipsized);
			CHECK(UISetLayout_PrepareText(samples[p].value,
			      strlen(samples[p].value), UI_SETLAYOUT_VALUE_TEXT_MAX,
			      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
			      l.rowValueWidth - UI_SETLAYOUT_PILL_PAD * 2 -
			      UI_SETLAYOUT_ARROW_W * 2, UI_SETLAYOUT_VALUE_SCALE,
			      UI_SETLAYOUT_VALUE_SCALE, testMeasure, out, sizeof(out),
			      &fit));
			CHECK(!fit.ellipsized);
			CHECK(fit.renderedWidth + UI_SETLAYOUT_PILL_PAD * 2 +
			      UI_SETLAYOUT_ARROW_W * 2 <= l.rowValueWidth);
		}
	}

	UISetLayout_Compute(6, 1, UI_SETLAYOUT_MOTION_FULL, &l);
	CHECK(UISetLayout_PrepareText(longest, sizeof(longest) - 1u,
	      UI_SETLAYOUT_VALUE_TEXT_MAX, UI_SETLAYOUT_ELLIPSIZE_MIDDLE,
	      UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
	      UI_SETLAYOUT_FIELD_W - UI_SETLAYOUT_PILL_PAD * 2,
	      UI_SETLAYOUT_VALUE_SCALE, UI_SETLAYOUT_VALUE_SCALE, testMeasure,
	      out, sizeof(out), &fit));
	CHECK(fit.ellipsized);
	CHECK(fit.renderedWidth <= UI_SETLAYOUT_FIELD_W - UI_SETLAYOUT_PILL_PAD * 2);
	/* The longest tooltip line fits the help card at its size. */
	UISetLayout_ComputeHelp(UI_SETLAYOUT_HELP_LINES, &help);
	CHECK(UISetLayout_PrepareText(tooltipLine, sizeof(tooltipLine) - 1u,
	      sizeof(tooltipLine) - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
	      UI_SETLAYOUT_TEXT_PLAIN, 0, 1, help.lineMaxWidth,
	      UI_SETLAYOUT_HELP_SCALE, UI_SETLAYOUT_HELP_SCALE, testMeasure, out,
	      sizeof(out), &fit));
	CHECK(!fit.ellipsized);
}

static void test_measured_copy_fail_closed(void) {
	uiSetLayoutTextFit_t fit;
	char guarded[UI_SETLAYOUT_TEXT_BUFFER_SIZE + 2u];
	char *out = guarded + 1;

	memset(guarded, 0x5a, sizeof(guarded));
	CHECK(!UISetLayout_PrepareText("value", 5u, 5u,
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
	      100, 0.72f, 0.60f, failingMeasure, out,
	      UI_SETLAYOUT_TEXT_BUFFER_SIZE, &fit));
	CHECK(out[0] == '\0');
	CHECK((unsigned char)guarded[0] == 0x5au);
	CHECK((unsigned char)guarded[sizeof(guarded) - 1u] == 0x5au);
	CHECK(!UISetLayout_PrepareText("value", 5u, 5u,
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
	      0, 0.72f, 0.60f, testMeasure, out,
	      UI_SETLAYOUT_TEXT_BUFFER_SIZE, &fit));
	CHECK(!UISetLayout_PrepareText("value", 5u, 5u,
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
	      100, 0.50f, 0.60f, testMeasure, out,
	      UI_SETLAYOUT_TEXT_BUFFER_SIZE, &fit));
	CHECK(!UISetLayout_PrepareText(NULL, 5u, 5u,
	      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
	      100, 0.72f, 0.60f, testMeasure, out,
	      UI_SETLAYOUT_TEXT_BUFFER_SIZE, &fit));
}

/* One segmented control: three equal cells side by side in one track, the
 * L button before it and R after it, ending at the page's right edge. */
static void test_tab_cells_ordered_and_disjoint(void) {
	uiSetLayout_t l;
	int p, i;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		UISetLayout_Compute(p, 0, 0, &l);
		CHECK(l.currentTab == EXPECT[p].tab);
		CHECK(l.tabCount == (EXPECT[p].tab < 0 ? 0 : UI_SETLAYOUT_TAB_COUNT));
		if (l.tabCount == 0)
			continue;
		CHECK(l.tabCell[0].x == l.tabTrack.x);
		CHECK(l.tabCell[l.tabCount - 1].x + l.tabCell[l.tabCount - 1].w ==
		      l.tabTrack.x + l.tabTrack.w);
		for (i = 0; i < l.tabCount; i++) {
			CHECK(rectInside(l.tabCell[i], l.tabTrack));
			CHECK(l.tabCell[i].w == l.tabCell[0].w);
			CHECK(l.tabLabelCenterX[i] == l.tabCell[i].x + l.tabCell[i].w / 2);
			if (i > 0)
				CHECK(l.tabCell[i].x == l.tabCell[i - 1].x + l.tabCell[i - 1].w);
		}
		CHECK(l.tabLabelY == l.titleY);
		CHECK(l.tabLeftGlyphX + 15 < l.tabTrack.x);
		CHECK(l.tabTrack.x + l.tabTrack.w + 15 < l.tabRightGlyphX);
		CHECK(l.tabRightGlyphX == 600);
	}
}

/* The console's IPL font is wider than testMeasure, and than the font
 * Dolphin draws with: "Game Defaults" passed both yet read "Game Def..." on
 * a GameCube. Tab labels never shrink, so each must fit its cell in a font
 * 1.75 times testMeasure's width. */
static int consoleMeasure(const char *text) {
	return (testMeasure(text) * 7 + 3) / 4;
}

static void test_tab_labels_fit_the_console_font(void) {
	uiSetLayout_t l;
	uiSetLayoutTextFit_t fit;
	char out[UI_SETLAYOUT_SHORT_CAPACITY];
	int i;

	UISetLayout_Compute(0, 0, UI_SETLAYOUT_MOTION_FULL, &l);
	for (i = 0; i < l.tabCount; i++) {
		const char *tab = UISetLayout_PageDesc(i)->tabLabel;
		CHECK(UISetLayout_PrepareText(tab, strlen(tab),
		      UI_SETLAYOUT_SHORT_CAPACITY - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
		      UI_SETLAYOUT_TEXT_PLAIN, 0, 1, l.tabCell[i].w - 12,
		      UI_SETLAYOUT_TAB_SCALE, UI_SETLAYOUT_TAB_SCALE, consoleMeasure,
		      out, sizeof(out), &fit));
		CHECK(!fit.ellipsized);
	}
}

/* A game's own rows keep room for the CUSTOM chip before the value. */
static void test_custom_chip_room(void) {
	uiSetLayout_t l;
	int p;
	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		const uiSetLayoutPage_t *d = UISetLayout_PageDesc(p);
		UISetLayout_Compute(p, 0, 0, &l);
		CHECK(d->hasTags == (EXPECT[p].tab < 0));
		CHECK(l.rowValueX0 + l.rowValueWidth + (d->hasTags ?
		      UI_SETLAYOUT_CHIP_W + UI_SETLAYOUT_CHIP_GAP : 0) == l.rowValueX);
		CHECK(l.rowValueWidth >= 200);
	}
}

/* Storage (page 5) shows the settings file's state instead of its fixed
 * subtitle. Each state must fit without ellipsizing. */
static void test_settings_file_subtitles(void) {
	uiSetLayout_t l;
	uiSetLayoutTextFit_t fit;
	char out[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	int state, other;

	CHECK(strcmp(UISetLayout_PageDesc(5)->title, "Storage") == 0);
	UISetLayout_Compute(5, 0, UI_SETLAYOUT_MOTION_FULL, &l);
	for (state = 0; state < UI_SETLAYOUT_SETTINGS_FILE_STATES; state++) {
		const char *text = UISetLayout_SettingsFileText(state);
		CHECK(text != NULL && strlen(text) > 0);
		CHECK(UISetLayout_PrepareText(text, strlen(text),
		      UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
		      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN,
		      0, 1, l.subtitleMaxWidth, UI_SETLAYOUT_SUBTITLE_SCALE,
		      UI_SETLAYOUT_SUBTITLE_SCALE, testMeasure, out,
		      sizeof(out), &fit));
		CHECK(fit.renderedWidth <= l.subtitleMaxWidth);
		CHECK(!fit.ellipsized);
		for (other = 0; other < state; other++)
			CHECK(strcmp(text, UISetLayout_SettingsFileText(other)) != 0);
	}
	CHECK(UISetLayout_SettingsFileText(-1) ==
	      UISetLayout_SettingsFileText(UI_SETLAYOUT_SETTINGS_FILE_NO_DEVICE));
	CHECK(UISetLayout_SettingsFileText(UI_SETLAYOUT_SETTINGS_FILE_STATES) ==
	      UISetLayout_SettingsFileText(UI_SETLAYOUT_SETTINGS_FILE_NO_DEVICE));
}

/* The focus card goes to the selected row's card, or to the exit chosen. */
static void test_motion_targets(void) {
	uiSetLayout_t l;
	uiSetLayoutRect_t rowTarget;
	int p, opt;

	UISetLayout_Compute(2, 0, UI_SETLAYOUT_MOTION_FULL, &l);
	CHECK(l.focusAnimate == 1);
	rowTarget = l.focusRect;
	UISetLayout_Compute(2, 0, UI_SETLAYOUT_MOTION_REDUCED, &l);
	CHECK(l.focusAnimate == 1);
	CHECK(sameRect(l.focusRect, rowTarget)); /* target identical across modes */
	UISetLayout_Compute(2, 0, UI_SETLAYOUT_MOTION_OFF, &l);
	CHECK(l.focusAnimate == 0);    /* Off resolves on the next frame */
	CHECK(sameRect(l.focusRect, rowTarget));

	for (p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		for (opt = 0; opt <= UISetLayout_DiscardIndex(p); opt++) {
			UISetLayout_Compute(p, opt, 0, &l);
			if (l.selectedRow >= 0)
				CHECK(sameRect(l.focusRect,
				      l.rowRect[l.selectedRow - l.firstVisibleRow]));
			else
				CHECK(sameRect(l.focusRect,
				      l.actionRect[l.selectedAction]));
		}
	}
}

static void test_degenerate_inputs_clamped(void) {
	uiSetLayout_t l;
	UISetLayout_Compute(-5, -7, 0, &l);
	CHECK(l.page == 0 && l.option == 0);
	UISetLayout_Compute(99, 9999, 99, &l);
	CHECK(l.page == UI_SETLAYOUT_PAGE_COUNT - 1);
	CHECK(l.option == UISetLayout_DiscardIndex(l.page));
	CHECK(l.selectedAction >= 0);
	CHECK(l.focusAnimate == 1); /* unknown mode is not Off */
}

static void test_labels_lose_their_colon(void) {
	char out[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	char tiny[4];

	CHECK(UISetLayout_Label("Menu Music:", out, sizeof(out)) == 10u);
	CHECK(strcmp(out, "Menu Music") == 0);
	CHECK(UISetLayout_Label("Reset to defaults", out, sizeof(out)) == 17u);
	CHECK(strcmp(out, "Reset to defaults") == 0);
	CHECK(UISetLayout_Label("In-Game Reset: ", out, sizeof(out)) == 13u);
	CHECK(strcmp(out, "In-Game Reset") == 0);
	CHECK(UISetLayout_Label(":", out, sizeof(out)) == 0u && out[0] == '\0');
	CHECK(UISetLayout_Label(NULL, out, sizeof(out)) == 0u && out[0] == '\0');
	CHECK(UISetLayout_Label("Display", tiny, sizeof(tiny)) == 3u);
	CHECK(strcmp(tiny, "Dis") == 0);
	/* In place. */
	strcpy(out, "System Video:");
	CHECK(UISetLayout_Label(out, out, sizeof(out)) == 12u);
	CHECK(strcmp(out, "System Video") == 0);
}

static void expectSummary(const char *help, const char *value,
	const char *expected) {
	char out[256];
	size_t length = UISetLayout_HelpSummary(help, value, out, sizeof(out));

	CHECK(length == strlen(out));
	if (strcmp(out, expected) != 0) {
		failures++;
		fprintf(stderr, "FAIL [%s] summary of %.30s for %s: \"%s\", "
		        "expected \"%s\"\n", currentTest, help,
		        value != NULL ? value : "(none)", out, expected);
	}
	checks++;
}

/* Real help texts from settings.c: the line for the current value, else
 * the first sentence, and nothing rather than another value's line. */
static void test_help_summary_reads_the_current_value(void) {
	static const char rumble[] = "Controller Rumble:\n\nOn - Controllers can "
		"rumble in games (default)\nOff - Rumble is turned off in games";
	static const char motor[] = "Stop DVD Drive motor:\n\nDisabled - Leave "
		"it as-is (default)\nEnabled - Stop the disc from spinning when Swiss "
		"starts\n\nThis option is mostly for users booting from game save "
		"exploits\nwhere the disc will already be spinning.";
	static const char panels[] = "Panel Transparency:\n\nEnabled - Menu "
		"panels are translucent so the backdrop\nshows through, GameCube-menu "
		"style (default)\nDisabled - Panels are solid.";
	static const char recent[] = "Recent List:\n\n(On) - Press Start while "
		"browsing to show a recent list.\n(Lazy) - Same as On but list "
		"updates only for new entries.\n(Off) - Recent list is completely "
		"disabled.\n\nThe lazy/off options exist to minimise SD card writes.";
	static const char scale[] = "Force Horizontal Scale:\n\nHow the video "
		"output scales the picture across.\nAuto - Keep the game's own "
		"scaling (default)\n1:1 - No horizontal scaling\n11:10, 9:8 - Fixed "
		"ratios\n640px to 720px - A fixed output width";
	static const char widescreen[] = "Force Widescreen:\n\nStretches games "
		"made for 4:3 to fill a 16:9 screen.\n3D - Widen the 3D view only\n"
		"2D+3D - Also widen 2D menus and on-screen displays\nSet your TV to "
		"16:9. Edges can look wrong in some games.";
	static const char filter[] = "Force Vertical Filter:\n\nFor 480i & 576i:\n"
		" Auto - Do nothing (default)\n\nFor 240p & 288p:\n Auto - Equivalent "
		"to 0 (default)\n 0 - 50%/50% blend with lower lines\n 1 - 50%/50% "
		"blend with upper lines\n\nFor other video modes:\n Auto - Equivalent "
		"to 0 (default)\n 0 - 3\327MSAA resolve only\n 1 - 18.75%/62.5%/"
		"18.75% blend";
	static const char browser[] = "File Browser Type:\n\nStandard - Displays "
		"files with minimal detail (default)\n\nCarousel - Suited towards "
		"Game/DOL only use";
	static const char igr[] = "In-Game Reset: (A + Z + Start)\n\nReboot - "
		"Perform hot reset with a compatible device\nApploader - Requires "
		"/swiss/patches/apploader.img";
	static const char exi[] = "SD/IDE-EXI Speed:\n\nThe clock speed to try "
		"using on the EXI bus for SD cards and\nIDE-EXI devices. 27 MHz may "
		"not work with some SD cards or\nSD card adapters.";
	static const char cheats[] = "Auto-load cheats:\n\nIf enabled, and a "
		"cheats file for a particular game is found\ne.g. /swiss/cheats/"
		"GPOP8D.txt (on a compatible device)";
	static const char sound[] = "System Sound:\n\nSets the default audio "
		"output type used by most games";
	static const char offset[] = "Force Vertical Offset:\n\n+0 - Standard "
		"value (default)\n-2 - GCVideo-DVI compatible (480i)\n-3 - GCVideo-DVI "
		"compatible";
	static const char rt4k[] = "RetroTINK-4K HDMI Input:\n\nFor GCDigital "
		"compatibility mode:\n Requires FX-Framework firmware version "
		"3.9.46.178 or later.";
	char tiny[8];

	/* The line for the value; Yes/No/On/Off read Enabled and Disabled. */
	expectSummary(rumble, "On", "Controllers can rumble in games (default)");
	expectSummary(rumble, "Off", "Rumble is turned off in games");
	expectSummary(motor, "No", "Leave it as-is (default)");
	expectSummary(motor, "Yes", "Stop the disc from spinning when Swiss starts");
	/* A line that runs on is read whole; a new sentence is not. */
	expectSummary(panels, "Yes", "Menu panels are translucent so the backdrop "
		"shows through, GameCube-menu style (default)");
	expectSummary(panels, "No", "Panels are solid.");
	expectSummary(widescreen, "2D+3D",
		"Also widen 2D menus and on-screen displays");
	/* "(On) -" and "11:10, 9:8 -" name values too. */
	expectSummary(recent, "Lazy", "Same as On but list updates only for new "
		"entries.");
	expectSummary(scale, "9:8", "Fixed ratios");
	expectSummary(scale, "Auto", "Keep the game's own scaling (default)");
	expectSummary(offset, "-3", "GCVideo-DVI compatible");
	expectSummary(offset, "+0", "Standard value (default)");
	/* An unlisted value: the lead sentence, or nothing. */
	expectSummary(scale, "704px", "How the video output scales the picture "
		"across.");
	expectSummary(widescreen, "No", "Stretches games made for 4:3 to fill a "
		"16:9 screen.");
	expectSummary(browser, "Fullwidth", "");
	expectSummary(igr, "Disabled", "");
	expectSummary(offset, "+5", "");
	/* A value listed twice, for two video modes, is left to Y. */
	expectSummary(filter, "Auto", "");
	expectSummary(filter, "0", "");
	expectSummary(filter, "1", "");
	/* First sentences: "e.g." and "3.9.46" carry on; a digit starts one. */
	expectSummary(exi, "27 MHz", "The clock speed to try using on the EXI bus "
		"for SD cards and IDE-EXI devices.");
	expectSummary(cheats, "Yes", "If enabled, and a cheats file for a "
		"particular game is found e.g. /swiss/cheats/GPOP8D.txt (on a "
		"compatible device)");
	expectSummary(sound, "Mono", "Sets the default audio output type used by "
		"most games");
	expectSummary(sound, NULL, "Sets the default audio output type used by "
		"most games");
	expectSummary(rt4k, "Yes", "For GCDigital compatibility mode: Requires "
		"FX-Framework firmware version 3.9.46.178 or later.");
	/* Nothing to say. */
	expectSummary(NULL, "Yes", "");
	expectSummary("Name:", "Yes", "");
	expectSummary("Name:\n\n", "Yes", "");
	expectSummary("Name:\n\nFor these modes:\n Auto - x", "On", "");
	/* Bounded: cut to the buffer, always terminated. */
	CHECK(UISetLayout_HelpSummary(rumble, "Off", tiny, sizeof(tiny)) == 7u);
	CHECK(strcmp(tiny, "Rumble ") == 0);
	CHECK(UISetLayout_HelpSummary(rumble, "Off", tiny, 0u) == 0u);
}

/* The value list: a centred card whose window follows the focus like the
 * page's, with the focus drawn where it is. */
static void test_value_list_geometry(void) {
	uiSetListLayout_t l;
	int count, focus, i;

	for (count = 1; count <= UI_SETLAYOUT_LIST_MAX; count++) {
		int prevThumb = -1;
		for (focus = 0; focus < count; focus++) {
			UISetLayout_ComputeList(count, focus, &l);
			CHECK(rectInSafeArea(l.card));
			CHECK(l.card.x + l.card.w / 2 == 320);
			CHECK(l.visibleCount == (count < UI_SETLAYOUT_LIST_ROWS ?
			      count : UI_SETLAYOUT_LIST_ROWS));
			CHECK(l.focus == focus);
			CHECK(focus >= l.first && focus < l.first + l.visibleCount);
			CHECK(l.first >= 0 && l.first + l.visibleCount <= count);
			CHECK(sameRect(l.focusRect, l.rowRect[focus - l.first]));
			CHECK(rectInside(l.accentBar, l.card));
			CHECK(l.accentBar.y + l.accentBar.h < l.titleY - 8);
			CHECK(l.titleY + 8 < l.topDivider.y);
			for (i = 0; i < l.visibleCount; i++) {
				CHECK(rectInside(l.rowRect[i], l.card));
				CHECK(l.rowRect[i].y > l.topDivider.y);
				CHECK(l.rowRect[i].y + l.rowRect[i].h < l.bottomDivider.y);
				CHECK(l.rowTextY[i] == l.rowRect[i].y + l.rowRect[i].h / 2);
				if (i > 0)
					CHECK(l.rowRect[i - 1].y + l.rowRect[i - 1].h <
					      l.rowRect[i].y);
			}
			CHECK(l.rowTextX + l.rowTextMaxWidth + UI_SETLAYOUT_CHIP_GAP +
			      UI_SETLAYOUT_CHIP_W <= l.chipRight);
			/* Both hints fit their half of the footer at their size. */
			for (i = 0; i < 2; i++) {
				static const char *const hints[2] = {"A  Choose", "B  Cancel"};
				uiSetLayoutTextFit_t fit;
				char out[UI_SETLAYOUT_HINT_CAPACITY];
				CHECK(UISetLayout_PrepareText(hints[i], 9u, 9u,
				      UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1,
				      (l.hintRightX - l.hintX) / 2, UI_SETLAYOUT_HINT_SCALE,
				      UI_SETLAYOUT_HINT_SCALE, testHintMeasure, out,
				      sizeof(out), &fit));
				CHECK(!fit.ellipsized);
			}
			CHECK(l.chipRight <= l.rowRect[0].x + l.rowRect[0].w);
			CHECK(l.bottomDivider.y < l.hintY - 8);
			CHECK(l.hintY + 8 < l.card.y + l.card.h);
			CHECK(l.scrollVisible == (count > UI_SETLAYOUT_LIST_ROWS));
			if (l.scrollVisible) {
				CHECK(rectInside(l.scrollThumb, l.scrollTrack));
				CHECK(rectInside(l.scrollTrack, l.card));
				CHECK(l.scrollTrack.x > l.rowRect[0].x + l.rowRect[0].w);
				CHECK(l.scrollThumb.y >= prevThumb);
				prevThumb = l.scrollThumb.y;
			}
		}
	}
	/* Out-of-range input is clamped, never a table overrun. */
	UISetLayout_ComputeList(99, 99, &l);
	CHECK(l.count == UI_SETLAYOUT_LIST_MAX && l.focus == l.count - 1);
	UISetLayout_ComputeList(0, -3, &l);
	CHECK(l.count == 1 && l.focus == 0);
}

/* The help card holds the longest tooltip (the audit caps them at 16 lines
 * of 68 characters) inside the safe area. */
/* Settings › Network hides Sync Clock's rows, its last three, without a
 * network adapter: the page then lays out, scrolls and exits as if it had
 * only the rows it shows, and every other page keeps all of its own. */
static void test_hidden_tail_rows(void) {
	const int network = 6;
	int full = UISetLayout_PageDesc(network)->rowCount;
	uiSetLayout_t l;

	CHECK(UISetLayout_RowCount(network) == full);
	UISetLayout_HideTail(network, 3);
	CHECK(UISetLayout_RowCount(network) == full - 3);
	CHECK(UISetLayout_DiscardIndex(network) == full - 2);
	UISetLayout_Compute(network, full - 4, 0, &l);
	CHECK(l.rowCount == full - 3 && l.selectedRow == full - 4);
	UISetLayout_Compute(network, full - 3, 0, &l);
	CHECK(l.selectedRow == -1 && l.selectedAction == 0);
	UISetLayout_Compute(network, 999, 0, &l);
	CHECK(l.option == full - 2 && l.selectedAction == 1);
	for (int p = 0; p < UI_SETLAYOUT_PAGE_COUNT; p++) {
		if (p != network)
			CHECK(UISetLayout_RowCount(p) == UISetLayout_PageDesc(p)->rowCount);
	}
	/* A page keeps one row however many it is told to hide; a page out of
	 * range changes nothing. */
	UISetLayout_HideTail(network, 999);
	CHECK(UISetLayout_RowCount(network) == 1);
	UISetLayout_HideTail(-1, 3);
	UISetLayout_HideTail(UI_SETLAYOUT_PAGE_COUNT, 3);
	UISetLayout_HideTail(network, 0);
	CHECK(UISetLayout_RowCount(network) == full);
}

static void test_help_card_holds_sixteen_lines(void) {
	uiSetHelpLayout_t l;
	int lines;

	for (lines = 0; lines <= UI_SETLAYOUT_HELP_LINES; lines++) {
		UISetLayout_ComputeHelp(lines, &l);
		CHECK(l.lineCount == lines);
		CHECK(rectInSafeArea(l.card));
		CHECK(l.card.y + l.card.h / 2 >= 239 && l.card.y + l.card.h / 2 <= 241);
		CHECK(l.titleY + 8 < l.topDivider.y);
		CHECK(l.lineY0 - 8 > l.topDivider.y);
		CHECK(lines == 0 || l.lineY0 + (lines - 1) * l.linePitch + 8 <
		      l.bottomDivider.y);
		CHECK(l.bottomDivider.y < l.hintY - 8);
		CHECK(l.lineX + l.lineMaxWidth <= l.card.x + l.card.w);
		CHECK(l.linePitch >= 17);
	}
	UISetLayout_ComputeHelp(99, &l);
	CHECK(l.lineCount == UI_SETLAYOUT_HELP_LINES);
}

int main(void) {
	int failuresBefore = 0;
	RUN(test_page_descriptors_match_settings_enums);
	RUN(test_action_mapping_every_boundary);
	RUN(test_windowing_first_middle_last);
	RUN(test_scroll_treatment);
	RUN(test_page_covers_the_screen);
	RUN(test_geometry_inside_safe_area_everywhere);
	RUN(test_explicit_text_and_vertical_ownership);
	RUN(test_tab_cells_ordered_and_disjoint);
	RUN(test_tab_labels_fit_the_console_font);
	RUN(test_value_column_and_ellipsize_bounds);
	RUN(test_measured_floor_ellipsize_budgets);
	RUN(test_custom_chip_room);
	RUN(test_settings_file_subtitles);
	RUN(test_motion_targets);
	RUN(test_measured_copies_cover_real_extremes);
	RUN(test_measured_copy_fail_closed);
	RUN(test_degenerate_inputs_clamped);
	RUN(test_labels_lose_their_colon);
	RUN(test_help_summary_reads_the_current_value);
	RUN(test_value_list_geometry);
	RUN(test_help_card_holds_sixteen_lines);
	RUN(test_hidden_tail_rows);
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
