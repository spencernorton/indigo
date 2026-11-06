#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_hint.h"

static unsigned long checks;

#define CHECK(condition) do { \
	checks++; \
	if(!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(1); \
	} \
} while(0)

typedef struct {
	uiHintGlyph_t first;
	uiHintGlyph_t second;
	const char *label;
} expectedItem_t;

static void expect(const char *text, const expectedItem_t *expected, int count)
{
	uiHintItem_t items[UI_HINT_MAX_ITEMS];
	int parsed = UIHint_Parse(text, items, UI_HINT_MAX_ITEMS);
	int i;

	CHECK(parsed == count);
	for(i = 0; i < count; i++) {
		int glyphs = (expected[i].first != UI_HINT_GLYPH_NONE) +
			(expected[i].second != UI_HINT_GLYPH_NONE);

		CHECK(items[i].glyphCount == glyphs);
		CHECK(items[i].glyph[0] == expected[i].first);
		CHECK(items[i].glyph[1] == expected[i].second);
		CHECK(items[i].labelLength == strlen(expected[i].label));
		CHECK(memcmp(items[i].label, expected[i].label, items[i].labelLength) == 0);
	}
}

#define EXPECT(text, ...) do { \
	static const expectedItem_t items[] = {__VA_ARGS__}; \
	expect(text, items, (int)(sizeof(items) / sizeof(items[0]))); \
} while(0)

/* Every hint line Indigo draws today, as the renderer will see it. */
static void test_every_hint_in_the_ui(void)
{
	EXPECT("STICK / D-PAD  TURN    A  OPEN    START  RECENT",
		{UI_HINT_GLYPH_STICK, UI_HINT_GLYPH_DPAD, "TURN"},
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "OPEN"},
		{UI_HINT_GLYPH_START, UI_HINT_GLYPH_NONE, "RECENT"});
	EXPECT("D-PAD  SELECT    A  OPEN    B  BACK",
		{UI_HINT_GLYPH_DPAD, UI_HINT_GLYPH_NONE, "SELECT"},
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "OPEN"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "BACK"});
	EXPECT("\213  \233  CHOOSE    A  SELECT    B  CANCEL",
		{UI_HINT_GLYPH_DPAD, UI_HINT_GLYPH_NONE, "CHOOSE"},
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "SELECT"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "CANCEL"});
	EXPECT("X  EXI  \267  Y  INFO",
		{UI_HINT_GLYPH_X, UI_HINT_GLYPH_NONE, "EXI"},
		{UI_HINT_GLYPH_Y, UI_HINT_GLYPH_NONE, "INFO"});
	EXPECT("L/R  PAGE 1 OF 6    B  BACK",
		{UI_HINT_GLYPH_L, UI_HINT_GLYPH_R, "PAGE 1 OF 6"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "BACK"});
	EXPECT("A  LAUNCH GAME    B  LIBRARY    X  SETTINGS (2 CUSTOM)    Y  CHEATS",
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "LAUNCH GAME"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "LIBRARY"},
		{UI_HINT_GLYPH_X, UI_HINT_GLYPH_NONE, "SETTINGS (2 CUSTOM)"},
		{UI_HINT_GLYPH_Y, UI_HINT_GLYPH_NONE, "CHEATS"});
	EXPECT("Z  AUTOLOAD ON", {UI_HINT_GLYPH_Z, UI_HINT_GLYPH_NONE, "AUTOLOAD ON"});
	EXPECT("A  Toggle debug", {UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "Toggle debug"});
	EXPECT("X  DEFAULT    B  DONE",
		{UI_HINT_GLYPH_X, UI_HINT_GLYPH_NONE, "DEFAULT"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "DONE"});
	EXPECT("L/R  1 OF 3", {UI_HINT_GLYPH_L, UI_HINT_GLYPH_R, "1 OF 3"});
	EXPECT("Y  HELP", {UI_HINT_GLYPH_Y, UI_HINT_GLYPH_NONE, "HELP"});
	EXPECT("A  LAUNCH GAME", {UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "LAUNCH GAME"});
	EXPECT("Y  Choose cheats", {UI_HINT_GLYPH_Y, UI_HINT_GLYPH_NONE, "Choose cheats"});
	EXPECT("Z  AUTOLOAD ON   R  VERIFY",
		{UI_HINT_GLYPH_Z, UI_HINT_GLYPH_NONE, "AUTOLOAD ON"},
		{UI_HINT_GLYPH_R, UI_HINT_GLYPH_NONE, "VERIFY"});
	EXPECT("L+A  CLEAN BOOT", {UI_HINT_GLYPH_L, UI_HINT_GLYPH_A, "CLEAN BOOT"});
	/* The Library's line: Y opens a game's settings. */
	EXPECT("D-PAD  BROWSE   A  OPEN   Y  SETTINGS   X  BACK   B  HOME",
		{UI_HINT_GLYPH_DPAD, UI_HINT_GLYPH_NONE, "BROWSE"},
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "OPEN"},
		{UI_HINT_GLYPH_Y, UI_HINT_GLYPH_NONE, "SETTINGS"},
		{UI_HINT_GLYPH_X, UI_HINT_GLYPH_NONE, "BACK"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "HOME"});
}

static void test_text_stays_text(void)
{
	/* A name must be followed by exactly two spaces to become an icon. */
	EXPECT("RELOADS INDIGO AND ENDS THIS SESSION",
		{UI_HINT_GLYPH_NONE, UI_HINT_GLYPH_NONE, "RELOADS INDIGO AND ENDS THIS SESSION"});
	EXPECT("A RETURN", {UI_HINT_GLYPH_NONE, UI_HINT_GLYPH_NONE, "A RETURN"});
	EXPECT("STARTED", {UI_HINT_GLYPH_NONE, UI_HINT_GLYPH_NONE, "STARTED"});
	/* The launch button's text while a game starts. */
	EXPECT("STARTING GAME...",
		{UI_HINT_GLYPH_NONE, UI_HINT_GLYPH_NONE, "STARTING GAME..."});
	{
		uiHintItem_t items[UI_HINT_MAX_ITEMS];

		/* L+A is held together; L/R and STICK / D-PAD are either. */
		CHECK(UIHint_Parse("L+A  CLEAN BOOT", items, UI_HINT_MAX_ITEMS) == 1);
		CHECK(items[0].chord);
		CHECK(UIHint_Parse("L/R  PAGE", items, UI_HINT_MAX_ITEMS) == 1);
		CHECK(!items[0].chord);
		CHECK(UIHint_Parse("STICK / D-PAD  TURN", items, UI_HINT_MAX_ITEMS) == 1);
		CHECK(!items[0].chord);
	}
	EXPECT("LR  PAGE", {UI_HINT_GLYPH_NONE, UI_HINT_GLYPH_NONE, "LR  PAGE"});
	/* A Cyrillic letter's second byte is no "\267" separator. */
	EXPECT("B  Назад", {UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "Назад"});
	EXPECT("A  Вибір \267 B  Назад",
		{UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "Вибір"},
		{UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, "Назад"});
	/* A bare name is an icon with no label. */
	EXPECT("B", {UI_HINT_GLYPH_B, UI_HINT_GLYPH_NONE, ""});
	/* Separators and edges trim; blanks make no items. */
	EXPECT("   A  OPEN   ", {UI_HINT_GLYPH_A, UI_HINT_GLYPH_NONE, "OPEN"});
	CHECK(UIHint_Parse("", NULL, 0) == 0);
	{
		uiHintItem_t items[UI_HINT_MAX_ITEMS];

		CHECK(UIHint_Parse("", items, UI_HINT_MAX_ITEMS) == 0);
		CHECK(UIHint_Parse("     ", items, UI_HINT_MAX_ITEMS) == 0);
		CHECK(UIHint_Parse(NULL, items, UI_HINT_MAX_ITEMS) == 0);
		/* Never more than max items. */
		CHECK(UIHint_Parse("A  1    B  2    X  3", items, 2) == 2);
		CHECK(items[1].glyph[0] == UI_HINT_GLYPH_B);
	}
}

/* Nine pixels a character, four a space: a stand-in for the console font. */
static int measure(const char *text)
{
	int width = 0;

	for(; *text != '\0'; text++) {
		width += *text == ' ' ? 4 : 9;
	}
	return width;
}

static bool near(float a, float b)
{
	return a - b < 0.001f && b - a < 0.001f;
}

static void test_widths(void)
{
	float size = UIHint_GlyphSize(24, 0.5f);
	char label[UI_HINT_LABEL_CAPACITY];
	uiHintItem_t items[UI_HINT_MAX_ITEMS];

	CHECK(near(size, 13.2f));
	/* Icon, gap, label. */
	CHECK(near(UIHint_LineWidth("A  OPEN", 24, 0.5f, measure),
		size + size * UI_HINT_LABEL_GAP + 36.0f * 0.5f));
	/* Alternatives sit a small gap apart. */
	CHECK(near(UIHint_LineWidth("L/R", 24, 0.5f, measure),
		size * 1.12f * 2.0f + size * UI_HINT_ALTERNATIVE_GAP));
	/* A chord puts a "+" between its buttons, a small gap each side. */
	CHECK(near(UIHint_LineWidth("L+A", 24, 0.5f, measure),
		size * 1.12f + size + size * UI_HINT_ALTERNATIVE_GAP * 2.0f + 9.0f * 0.5f));
	/* Items are an item gap apart, whatever the separator. */
	CHECK(near(UIHint_LineWidth("B  BACK   Y  INFO", 24, 0.5f, measure),
		UIHint_LineWidth("B  BACK  \267  Y  INFO", 24, 0.5f, measure)));
	CHECK(near(UIHint_LineWidth("B  BACK    Y  INFO", 24, 0.5f, measure),
		UIHint_LineWidth("B  BACK", 24, 0.5f, measure) + size * UI_HINT_ITEM_GAP +
		UIHint_LineWidth("Y  INFO", 24, 0.5f, measure)));
	/* Plain text is just its text. */
	CHECK(near(UIHint_LineWidth("A RETURN", 24, 0.5f, measure), 67.0f * 0.5f));
	CHECK(near(UIHint_LineWidth("", 24, 0.5f, measure), 0.0f));
	/* START's pill holds its name. */
	CHECK(near(UIHint_GlyphWidth(UI_HINT_GLYPH_START, size, 0.5f, measure),
		45.0f * 0.5f * UI_HINT_START_TEXT_SCALE + size * 0.6f));
	/* Linear in scale, so a fitter can scale a hint like any string. */
	CHECK(near(UIHint_LineWidth("STICK / D-PAD  TURN    A  OPEN    START  RECENT",
		24, 0.46f, measure), 0.46f * UIHint_LineWidth(
		"STICK / D-PAD  TURN    A  OPEN    START  RECENT", 24, 1.0f, measure)));
	/* A label longer than the buffer is cut, never overrun. */
	CHECK(UIHint_Parse("A  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
		items, UI_HINT_MAX_ITEMS) == 1);
	UIHint_ItemWidth(&items[0], size, 0.5f, measure, label, 8);
	CHECK(strcmp(label, "WWWWWWW") == 0);
}

static void expectSplit(const char *message, const char *text, const char *hint)
{
	char buffer[256];
	char out[UI_HINT_LABEL_CAPACITY];

	strcpy(buffer, message);
	CHECK(UIHint_SplitPrompt(buffer, out, sizeof(out)) == (hint != NULL));
	CHECK(strcmp(buffer, text) == 0);
	CHECK(strcmp(out, hint != NULL ? hint : "") == 0);
}

/* Message boxes: the prompt comes off the text and becomes icons. */
static void test_message_prompts(void)
{
	char buffer[64];
	char out[8];

	/* Swiss's endings, on their own line or after a sentence. */
	expectSplit("No cheats file found.\nPress A to continue.",
		"No cheats file found.", "A  CONTINUE");
	expectSplit("Copy Complete.\nPress A to continue",
		"Copy Complete.", "A  CONTINUE");
	expectSplit("Move Failed! Press A to continue",
		"Move Failed!", "A  CONTINUE");
	expectSplit("DOL is too big. Press A.", "DOL is too big.", "A  CONTINUE");
	expectSplit("No WODE found! Press A", "No WODE found!", "A  CONTINUE");
	expectSplit("Delete confirmation required.\n \nPress L + A to continue, or B to cancel.",
		"Delete confirmation required.", "L+A  CONTINUE    B  CANCEL");
	/* Indigo's own prompts end on a line of buttons. */
	expectSplit("Reset everything on this screen to its default?\nA  RESET    B  KEEP",
		"Reset everything on this screen to its default?", "A  RESET    B  KEEP");
	expectSplit("Keep Swiss Video Mode PAL 576p?\nIt changes back by itself in 9 s.\n"
		"A  KEEP    B  CHANGE BACK",
		"Keep Swiss Video Mode PAL 576p?\nIt changes back by itself in 9 s.",
		"A  KEEP    B  CHANGE BACK");
	/* No prompt: the text is left alone. */
	expectSplit("Copying...", "Copying...", NULL);
	expectSplit("Reading A RETURN", "Reading A RETURN", NULL);
	expectSplit("Passed verification!", "Passed verification!", NULL);
	/* "Press A" must be its own words, not the end of another. */
	expectSplit("DePress A", "DePress A", NULL);
	/* A line that only mentions a button stays text. */
	expectSplit("Hold B while choosing a game", "Hold B while choosing a game", NULL);
	/* A hint longer than the buffer is cut, never overrun. */
	strcpy(buffer, "Done.\nA  CONTINUE    B  CANCEL");
	CHECK(UIHint_SplitPrompt(buffer, out, sizeof(out)) == 1);
	CHECK(strcmp(out, "A  CONT") == 0);
	CHECK(UIHint_SplitPrompt(NULL, out, sizeof(out)) == 0);
}

int main(void)
{
	test_every_hint_in_the_ui();
	test_text_stays_text();
	test_widths();
	test_message_prompts();
	printf("hint parser: %lu checks, 0 failures\n", checks);
	return 0;
}
