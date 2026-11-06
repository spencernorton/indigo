/*
 * Direct host commands from the repository root:
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion \
 *   -pedantic -Icube/swiss/source/gui buildtools/ui/tests/test_ui_home_layout.c \
 *   cube/swiss/source/gui/ui_home_layout.c cube/swiss/source/gui/ui_home.c \
 *   cube/swiss/source/gui/ui_home_text.c \
 *   -o /tmp/test_ui_home_layout && /tmp/test_ui_home_layout
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion \
 *   -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
 *   -Icube/swiss/source/gui buildtools/ui/tests/test_ui_home_layout.c \
 *   cube/swiss/source/gui/ui_home_layout.c cube/swiss/source/gui/ui_home.c \
 *   cube/swiss/source/gui/ui_home_text.c \
 *   -o /tmp/test_ui_home_layout_san && /tmp/test_ui_home_layout_san
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_home_layout.h"
#include "ui_home_text.h"
#include "ui_lang.h"

static unsigned int checks;

#define CHECK(condition) do { \
	++checks; \
	if(!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, \
			__LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

static uiHomeCapabilities_t capabilities(bool hasSource, bool hasRecent)
{
	uiHomeCapabilities_t value;

	value.hasSource = hasSource;
	value.hasRecent = hasRecent;
	value.hasApps = false;
	value.style = UI_HOME_CUBE_INFINITE;
	return value;
}

static uiHomeState_t stateAt(uiHomeFace_t face, uiHomeSurface_t surface,
	int selection)
{
	uiHomeState_t state;

	state.face = face;
	state.surface = surface;
	state.selection = selection;
	state.turnOrdinal = (int32_t)face;
	state.revision = 9u;
	return state;
}

static bool allZero(const void *value, size_t size)
{
	const unsigned char *bytes = value;
	size_t index;

	for(index = 0u; index < size; ++index) {
		if(bytes[index] != 0u) {
			return false;
		}
	}
	return true;
}

static uiHomeLayoutRect_t cubeKeepOut(void)
{
	uiHomeLayoutRect_t value = {
		UI_HOME_LAYOUT_CUBE_KEEP_OUT_LEFT,
		UI_HOME_LAYOUT_CUBE_KEEP_OUT_TOP,
		UI_HOME_LAYOUT_CUBE_KEEP_OUT_RIGHT,
		UI_HOME_LAYOUT_CUBE_KEEP_OUT_BOTTOM
	};

	return value;
}

static uiHomeLayoutRect_t selectedTravelBounds(
	uiHomeLayoutRect_t selectedBounds)
{
	selectedBounds.left -= UI_HOME_LAYOUT_SELECTED_TRAVEL;
	selectedBounds.right += UI_HOME_LAYOUT_SELECTED_TRAVEL;
	selectedBounds.top -= UI_HOME_LAYOUT_SELECTED_VERTICAL_TRAVEL;
	selectedBounds.bottom += UI_HOME_LAYOUT_SELECTED_VERTICAL_TRAVEL;
	return selectedBounds;
}

static void checkPointInRect(uiHomeLayoutPoint_t point,
	uiHomeLayoutRect_t bounds)
{
	CHECK(point.x >= bounds.left);
	CHECK(point.x <= bounds.right);
	CHECK(point.y >= bounds.top);
	CHECK(point.y <= bounds.bottom);
}

static void checkItem(const uiHomeLayoutItem_t *item,
	uiHomeLayoutRect_t command)
{
	CHECK(item != NULL);
	CHECK(UIHomeLayout_RectIsSafe(item->panelBounds));
	CHECK(UIHomeLayout_RectIsSafe(item->glowBounds));
	CHECK(UIHomeLayout_RectIsSafe(item->labelBounds));
	CHECK(item->glowBounds.left ==
		item->panelBounds.left - UI_HOME_LAYOUT_GLOW_EXTENT);
	CHECK(item->glowBounds.top ==
		item->panelBounds.top - UI_HOME_LAYOUT_GLOW_EXTENT);
	CHECK(item->glowBounds.right ==
		item->panelBounds.right + UI_HOME_LAYOUT_GLOW_EXTENT);
	CHECK(item->glowBounds.bottom ==
		item->panelBounds.bottom + UI_HOME_LAYOUT_GLOW_EXTENT);
	CHECK(item->glowBounds.top > UI_HOME_LAYOUT_SYSTEM_RAIL_BOTTOM);
	checkPointInRect(item->labelCenter, item->labelBounds);
	CHECK(UIHomeLayout_RectsDisjoint(item->glowBounds, command));
}

static void checkLayout(const uiHomeLayout_t *layout)
{
	int first;
	int second;
	int selected;

	CHECK(layout != NULL);
	CHECK(UIHomeLayout_Validate(layout));
	CHECK(UIHomeLayout_RectIsSafe(layout->titleBounds));
	checkPointInRect(layout->titleCenter, layout->titleBounds);
	CHECK(layout->commandCenter.x == UI_HOME_LAYOUT_COMMAND_X);
	CHECK(layout->commandCenter.y == UI_HOME_LAYOUT_COMMAND_Y);
	CHECK(layout->commandGlyphBounds.top == 428);
	CHECK(layout->commandGlyphBounds.bottom == 438);
	CHECK(layout->commandCenter.y - layout->commandGlyphBounds.top ==
		UI_HOME_LAYOUT_COMMAND_HALF_HEIGHT);
	CHECK(layout->commandGlyphBounds.bottom - layout->commandCenter.y ==
		UI_HOME_LAYOUT_COMMAND_HALF_HEIGHT);
	CHECK(UIHomeLayout_RectIsSafe(layout->commandGlyphBounds));
	checkPointInRect(layout->commandCenter, layout->commandGlyphBounds);
	if(layout->surface == UI_HOME_SURFACE_RING) {
		uiHomeLayoutRect_t keepOut = cubeKeepOut();
		uiHomeLayoutRect_t travel = selectedTravelBounds(
			layout->selectedLabelBounds);

		CHECK(UIHomeLayout_RectIsSafe(keepOut));
		CHECK(UIHomeLayout_RectIsSafe(travel));
		CHECK(UIHomeLayout_RectsDisjoint(layout->titleBounds, keepOut));
		CHECK(UIHomeLayout_RectsDisjoint(travel, keepOut));
		CHECK(travel.left >= UI_HOME_LAYOUT_SAFE_LEFT);
		CHECK(travel.right <= UI_HOME_LAYOUT_SAFE_RIGHT);
	}

	selected = 0;
	for(first = 0; first < layout->rowCount; ++first) {
		checkItem(&layout->rows[first], layout->commandGlyphBounds);
		if(layout->rows[first].selected) {
			++selected;
		}
		for(second = first + 1; second < layout->rowCount; ++second) {
			CHECK(UIHomeLayout_RectsDisjoint(
				layout->rows[first].glowBounds,
				layout->rows[second].glowBounds));
		}
	}
	CHECK(selected == (layout->rowCount > 0 ? 1 : 0));

	selected = 0;
	for(first = 0; first < layout->optionCount; ++first) {
		checkItem(&layout->options[first], layout->commandGlyphBounds);
		if(layout->options[first].selected) {
			++selected;
		}
		for(second = first + 1; second < layout->optionCount; ++second) {
			CHECK(UIHomeLayout_RectsDisjoint(
				layout->options[first].glowBounds,
				layout->options[second].glowBounds));
		}
	}
	CHECK(selected == (layout->optionCount > 0 ? 1 : 0));
	if(layout->surface == UI_HOME_SURFACE_RESTART_CONFIRM) {
		CHECK(UIHomeLayout_RectIsSafe(layout->modalBounds));
		CHECK(UIHomeLayout_RectIsSafe(layout->consequenceBounds));
		checkPointInRect(layout->consequenceCenter,
			layout->consequenceBounds);
		CHECK(layout->modalBounds.left <=
			layout->consequenceBounds.left);
		CHECK(layout->modalBounds.right >=
			layout->consequenceBounds.right);
		CHECK(layout->modalBounds.top <=
			layout->consequenceBounds.top);
		CHECK(layout->modalBounds.bottom >=
			layout->consequenceBounds.bottom);
		CHECK(UIHomeLayout_RectsDisjoint(layout->consequenceBounds,
			layout->options[0].glowBounds));
		CHECK(UIHomeLayout_RectsDisjoint(layout->consequenceBounds,
			layout->options[1].glowBounds));
		CHECK(UIHomeLayout_RectsDisjoint(layout->modalBounds,
			layout->commandGlyphBounds));
	}
}

static void testRectRules(void)
{
	uiHomeLayoutRect_t safe = {24, 62, 616, 438};
	uiHomeLayoutRect_t left = {24, 62, 100, 100};
	uiHomeLayoutRect_t right = {101, 62, 200, 100};
	uiHomeLayoutRect_t touching = {100, 100, 150, 150};

	CHECK(UIHomeLayout_RectIsValid(safe));
	CHECK(UIHomeLayout_RectIsSafe(safe));
	CHECK(!UIHomeLayout_RectIsValid(
		(uiHomeLayoutRect_t) {1, 0, 0, 1}));
	CHECK(!UIHomeLayout_RectIsValid(
		(uiHomeLayoutRect_t) {0, 1, 1, 0}));
	CHECK(!UIHomeLayout_RectIsSafe(
		(uiHomeLayoutRect_t) {23, 62, 616, 438}));
	CHECK(!UIHomeLayout_RectIsSafe(
		(uiHomeLayoutRect_t) {24, 61, 616, 438}));
	CHECK(!UIHomeLayout_RectIsSafe(
		(uiHomeLayoutRect_t) {24, 62, 617, 438}));
	CHECK(!UIHomeLayout_RectIsSafe(
		(uiHomeLayoutRect_t) {24, 62, 616, 439}));
	CHECK(UIHomeLayout_RectsDisjoint(left, right));
	CHECK(!UIHomeLayout_RectsDisjoint(left, touching));
	CHECK(!UIHomeLayout_RectsDisjoint(
		(uiHomeLayoutRect_t) {2, 0, 1, 1}, right));
}

static void testEveryValidLayout(void)
{
	uiHomeLayout_t layout;
	uiHomeLayout_t again;
	uiHomeState_t state;
	uiHomeCapabilities_t caps;
	int face;
	int surface;
	int source;
	int recent;
	int selection;

	for(face = 0; face < (int)UI_HOME_FACE_COUNT; ++face) {
		for(surface = 0; surface < (int)UI_HOME_SURFACE_COUNT; ++surface) {
			for(source = 0; source <= 1; ++source) {
				for(recent = 0; recent <= 1; ++recent) {
					int count;

					caps = capabilities(source != 0, recent != 0);
					count = UIHome_RowCount((uiHomeSurface_t)surface,
						caps);
					if(surface == (int)UI_HOME_SURFACE_RING) {
						count = 1;
					}
					for(selection = 0; selection < count; ++selection) {
						state = stateAt((uiHomeFace_t)face,
							(uiHomeSurface_t)surface, selection);
						memset(&layout, 0xa5, sizeof(layout));
						CHECK(UIHomeLayout_Compute(&state, caps, &layout));
						checkLayout(&layout);
						CHECK(layout.surface == (uiHomeSurface_t)surface);
						CHECK(layout.face == (uiHomeFace_t)face);
						CHECK(layout.selection == selection);
						CHECK(layout.hasSource == caps.hasSource);
						CHECK(layout.hasRecent == caps.hasRecent);
						CHECK(UIHomeLayout_Compute(&state, caps, &again));
						CHECK(memcmp(&layout, &again, sizeof(layout)) == 0);

						if(surface == (int)UI_HOME_SURFACE_RING) {
							CHECK(layout.ringLabelsVisible);
							CHECK(layout.rowCount == 0);
							CHECK(layout.optionCount == 0);
							CHECK(layout.selectedLabelBounds.top >
								UI_HOME_LAYOUT_CUBE_RAIL_BOTTOM);
							CHECK(UIHomeLayout_RectIsSafe(
								layout.selectedLabelBounds));
							checkPointInRect(layout.selectedLabelCenter,
								layout.selectedLabelBounds);
						}
						else if(surface == (int)UI_HOME_SURFACE_SOURCE) {
							CHECK(!layout.ringLabelsVisible);
							CHECK(layout.rowCount == (source != 0 ? 2 : 1));
							CHECK(layout.optionCount == 0);
						}
						else if(surface == (int)UI_HOME_SURFACE_SYSTEM) {
							/* Information, Memory Cards, File Browser,
							 * Restart: all four between System's raised
							 * rail and the command. */
							CHECK(layout.rowCount == 4);
							CHECK(layout.optionCount == 0);
							CHECK(layout.rows[0].glowBounds.top >
								UI_HOME_LAYOUT_SYSTEM_RAIL_BOTTOM);
							CHECK(layout.rows[0].panelBounds.top == 353);
							CHECK(layout.rows[3].glowBounds.bottom <
								layout.commandGlyphBounds.top);
						}
						else {
							CHECK(layout.rowCount == 0);
							CHECK(layout.optionCount == 2);
							CHECK(layout.modalBounds.left == 112);
							CHECK(layout.modalBounds.top == 337);
							CHECK(layout.modalBounds.right == 528);
							CHECK(layout.modalBounds.bottom == 419);
							CHECK(layout.consequenceCenter.x == 320);
							CHECK(layout.consequenceCenter.y == 355);
						}
					}
				}
			}
		}
	}
}

static void expectInvalid(uiHomeState_t *state,
	uiHomeCapabilities_t caps)
{
	uiHomeLayout_t layout;

	memset(&layout, 0xa5, sizeof(layout));
	CHECK(!UIHomeLayout_Compute(state, caps, &layout));
	CHECK(allZero(&layout, sizeof(layout)));
}

static void testMalformedInputs(void)
{
	uiHomeCapabilities_t caps = capabilities(true, true);
	uiHomeLayout_t layout;
	uiHomeState_t state = stateAt(UI_HOME_FACE_LIBRARY,
		UI_HOME_SURFACE_RING, 0);

	memset(&layout, 0xa5, sizeof(layout));
	CHECK(!UIHomeLayout_Compute(NULL, caps, &layout));
	CHECK(allZero(&layout, sizeof(layout)));
	CHECK(!UIHomeLayout_Compute(&state, caps, NULL));

	state.face = (uiHomeFace_t)-1;
	expectInvalid(&state, caps);
	state.face = UI_HOME_FACE_COUNT;
	expectInvalid(&state, caps);
	state.face = UI_HOME_FACE_LIBRARY;
	state.surface = (uiHomeSurface_t)-1;
	expectInvalid(&state, caps);
	state.surface = UI_HOME_SURFACE_COUNT;
	expectInvalid(&state, caps);

	state.surface = UI_HOME_SURFACE_RING;
	state.selection = -1;
	expectInvalid(&state, caps);
	state.selection = 1;
	expectInvalid(&state, caps);
	state.selection = INT_MAX;
	expectInvalid(&state, caps);
	state.surface = UI_HOME_SURFACE_SOURCE;
	state.selection = 2;
	expectInvalid(&state, caps);
	state.selection = INT_MIN;
	expectInvalid(&state, caps);
	state.selection = 1;
	expectInvalid(&state, capabilities(false, true));
}

static void testValidationRejectsTampering(void)
{
	uiHomeCapabilities_t caps = capabilities(true, true);
	uiHomeState_t source = stateAt(UI_HOME_FACE_SOURCE,
		UI_HOME_SURFACE_SOURCE, 0);
	uiHomeState_t ring = stateAt(UI_HOME_FACE_LIBRARY,
		UI_HOME_SURFACE_RING, 0);
	uiHomeState_t confirm = stateAt(UI_HOME_FACE_SYSTEM,
		UI_HOME_SURFACE_RESTART_CONFIRM, 0);
	uiHomeState_t system = stateAt(UI_HOME_FACE_SYSTEM,
		UI_HOME_SURFACE_SYSTEM, 3);
	uiHomeLayout_t valid;
	uiHomeLayout_t broken;

	CHECK(!UIHomeLayout_Validate(NULL));
	CHECK(UIHomeLayout_Compute(&source, caps, &valid));
	broken = valid;
	broken.commandCenter.y = 432;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.commandGlyphBounds.bottom = 439;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rows[0].glowBounds.top = UI_HOME_LAYOUT_CUBE_RAIL_BOTTOM;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rows[0].glowBounds = broken.rows[1].glowBounds;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rows[0].panelBounds.left += 1;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rows[0].panelBounds.left = INT_MIN;
	broken.rows[0].panelBounds.right = INT_MAX;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rowCount = 1;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.rows[0].selected = true;
	broken.rows[1].selected = true;
	CHECK(!UIHomeLayout_Validate(&broken));

	CHECK(UIHomeLayout_Compute(&system, caps, &valid));
	broken = valid;
	broken.rows[0].panelBounds.top -= 1;
	broken.rows[0].glowBounds.top -= 1;
	CHECK(!UIHomeLayout_Validate(&broken));
	CHECK(UIHomeLayout_Compute(&source, caps, &valid));
	broken = valid;
	broken.rows[0].panelBounds.top = 353;
	broken.rows[0].glowBounds.top = 350;
	CHECK(!UIHomeLayout_Validate(&broken));

	CHECK(UIHomeLayout_Compute(&ring, caps, &valid));
	broken = valid;
	broken.selectedLabelBounds.top = UI_HOME_LAYOUT_CUBE_RAIL_BOTTOM;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.ringLabelsVisible = false;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.selectedLabelBounds.left = UI_HOME_LAYOUT_SAFE_LEFT;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.selectedLabelBounds.left = INT_MIN;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.selectedLabelBounds.right = INT_MAX;
	CHECK(!UIHomeLayout_Validate(&broken));

	CHECK(UIHomeLayout_Compute(&confirm, caps, &valid));
	broken = valid;
	broken.options[1].glowBounds = broken.options[0].glowBounds;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.optionCount = 1;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.modalBounds.bottom = 429;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.consequenceCenter.y = broken.consequenceBounds.bottom + 1;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.consequenceBounds.bottom = broken.options[0].glowBounds.top;
	CHECK(!UIHomeLayout_Validate(&broken));
	broken = valid;
	broken.modalBounds.left = broken.options[0].glowBounds.left + 1;
	CHECK(!UIHomeLayout_Validate(&broken));
}

static int fakeIplTextWidth(const char *text)
{
	int width = 0;

	if(text == NULL) {
		return 0;
	}
	while(*text != '\0') {
		unsigned char glyph = (unsigned char)*text;

		if(glyph == (unsigned char)UI_HOME_TEXT_ELLIPSIS_BYTE) {
			width += 9;
		}
		else if(glyph == (unsigned char)'W') {
			width += 18;
		}
		else if(glyph == (unsigned char)'I') {
			width += 4;
		}
		else if(glyph == (unsigned char)' ') {
			width += 5;
		}
		else {
			width += 10;
		}
		++text;
	}
	return width;
}

static void testHomeTextFloorAndMetricEllipsis(void)
{
	char wide[40];
	char narrow[40];
	char wideOut[64];
	char narrowOut[64];
	char candidate[64];
	char small[8];
	bool ellipsized;
	float scale;
	size_t outLength;

	memset(wide, 'W', sizeof(wide) - 1u);
	wide[sizeof(wide) - 1u] = '\0';
	memset(narrow, 'I', sizeof(narrow) - 1u);
	narrow[sizeof(narrow) - 1u] = '\0';

	/* Static essential text never shrinks below the native-grid floor. */
	scale = UIHomeText_FitScale(wide, 80, 0.58f, fakeIplTextWidth);
	CHECK(scale == UI_HOME_TEXT_SCALE_FLOOR);
	CHECK(UIHomeText_FitScale("GC LOADER", 200, 0.58f,
		fakeIplTextWidth) == 0.58f);

	/* Dynamic source/status copy tail-ellipsizes using the supplied, uneven
	 * IPL-like glyph widths, then fits exactly at the floor. */
	scale = UIHomeText_CopyFitted(wideOut, sizeof(wideOut), wide, 80,
		0.58f, fakeIplTextWidth, &ellipsized);
	CHECK(ellipsized);
	CHECK(scale == UI_HOME_TEXT_SCALE_FLOOR);
	CHECK((float)fakeIplTextWidth(wideOut) * scale <= 80.0f);
	outLength = strlen(wideOut);
	CHECK(outLength > 1u);
	CHECK((unsigned char)wideOut[outLength - 1u] ==
		(unsigned char)UI_HOME_TEXT_ELLIPSIS_BYTE);

	/* The retained prefix is maximal under the actual metric: restoring one
	 * more omitted wide glyph would exceed the reserved band at the floor. */
	CHECK(outLength + 1u < sizeof(candidate));
	memcpy(candidate, wideOut, outLength + 1u);
	candidate[outLength - 1u] = 'W';
	candidate[outLength] = (char)UI_HOME_TEXT_ELLIPSIS_BYTE;
	candidate[outLength + 1u] = '\0';
	CHECK((float)fakeIplTextWidth(candidate) *
		UI_HOME_TEXT_SCALE_FLOOR > 80.0f);

	/* Equal byte counts do not imply equal fit: narrow glyphs preserve much
	 * more source copy than wide glyphs. */
	(void)UIHomeText_CopyFitted(narrowOut, sizeof(narrowOut), narrow, 80,
		0.58f, fakeIplTextWidth, &ellipsized);
	CHECK(strlen(narrowOut) > strlen(wideOut));
	CHECK((float)fakeIplTextWidth(narrowOut) *
		UI_HOME_TEXT_SCALE_FLOOR <= 80.0f);

	/* Buffer truncation remains bounded and visibly truthful even when width
	 * alone would permit the complete source name. */
	scale = UIHomeText_CopyFitted(small, sizeof(small),
		"GC Loader HW2", 1000, 0.58f, fakeIplTextWidth, &ellipsized);
	CHECK(ellipsized);
	CHECK(scale == 0.58f);
	CHECK(strlen(small) == sizeof(small) - 1u);
	CHECK((unsigned char)small[sizeof(small) - 2u] ==
		(unsigned char)UI_HOME_TEXT_ELLIPSIS_BYTE);

	/* Cyrillic text is cut between letters, never inside one: half of
	 * "я" (0xd1 0x8f) before the ellipsis would read as "х" (0xd1 0x85),
	 * and so would an IPL title's Ñ (0xd1). */
	{
		static const char cyrillic[] = "яяяяяяяяяяяяяяяяяяяя";
		static const char ipl[] = "a\321a\321a\321a\321a\321a\321a\321a\321a\321a\321";
		char cut[7];
		char fitted[sizeof(cyrillic)];

		(void)UIHomeText_CopyFitted(cut, sizeof(cut), cyrillic, 1000,
			0.58f, fakeIplTextWidth, &ellipsized);
		CHECK(ellipsized && strlen(cut) == 5u);
		CHECK((unsigned char)cut[4] == (unsigned char)UI_HOME_TEXT_ELLIPSIS_BYTE);
		CHECK(UILang_Head(cut, 4u) == 4u);
		for(int text = 0; text < 2; text++) {
			int cuts = 0;

			for(int width = 20; width <= 200; width += 3) {
				size_t at;

				(void)UIHomeText_CopyFitted(fitted, sizeof(fitted),
					text == 0 ? cyrillic : ipl, width, 0.58f, fakeIplTextWidth,
					&ellipsized);
				at = strlen(fitted);
				if(!ellipsized || at == 0u) {
					continue;
				}
				CHECK((unsigned char)fitted[at - 1u] ==
					(unsigned char)UI_HOME_TEXT_ELLIPSIS_BYTE);
				CHECK(UILang_Head(fitted, at - 1u) == at - 1u);
				cuts++;
			}
			CHECK(cuts > 10);
		}
	}
}

int main(void)
{
	testRectRules();
	testEveryValidLayout();
	testMalformedInputs();
	testValidationRejectsTampering();
	testHomeTextFloorAndMetricEllipsis();
	printf("ui_home_layout: %u checks passed\n", checks);
	return EXIT_SUCCESS;
}
