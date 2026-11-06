/* Menu Language's reader and lookups (gui/ui_lang.c): what a language file
 * may hold, what it may not (each kind of entry the parser drops), the
 * lookup before and after a table loads, and the characters drawString
 * reads as UTF-8. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_lang.h"

static unsigned int checks;

#define CHECK(condition) do { \
	checks++; \
	if(!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		exit(1); \
	} \
} while(0)

static uiLangEntry_t entries[16];

static char *dupText(const char *text)
{
	char *copy = malloc(strlen(text) + 1u);

	return copy != NULL ? strcpy(copy, text) : NULL;
}

/* text parsed from an exact-size copy, so a read past it is a finding. */
static size_t parse(const char *text, char **copy)
{
	size_t size = strlen(text);

	*copy = malloc(size ? size : 1u);
	memcpy(*copy, text, size);
	return UILang_Parse(*copy, size, entries, 16u);
}

static void test_entries(void)
{
	char *copy;
	size_t count = parse(
		"# a comment\n"
		"msgid \"\"\n"
		"msgstr \"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		"\n"
		"msgid \"Settings\"\n"
		"msgstr \"Налаштування\"\n"
		"\n"
		"#: gui/settings.c:1\n"
		"msgid \"Help:\\n\"\n"
		"\"\\n\"\n"
		"\"Two lines\"\n"
		"msgstr \"\"\n"
		"\"Довідка:\\n\\nДва рядки\"\n"
		"msgid \"Loading\\205\"\r\n"
		"msgstr \"Завантаження\\205\"\r\n"
		"msgid \"%d enabled\"\n"
		"msgstr \"Увімкнено: %d\"\n"
		"msgid \"Tab\\there \\\"q\\\" \\\\ \\x41\"\n"
		"msgstr \"T\\tq \\\"q\\\" \\\\ A\"", &copy);

	CHECK(count == 5u);
	CHECK(!strcmp(entries[0].english, "Settings"));
	CHECK(!strcmp(entries[0].text, "Налаштування"));
	CHECK(!strcmp(entries[1].english, "Help:\n\nTwo lines"));
	CHECK(!strcmp(entries[1].text, "Довідка:\n\nДва рядки"));
	CHECK(!strcmp(entries[2].english, "Loading\205"));
	CHECK(!strcmp(entries[2].text, "Завантаження\205"));
	CHECK(!strcmp(entries[3].text, "Увімкнено: %d"));
	CHECK(!strcmp(entries[4].english, "Tab\there \"q\" \\ A"));
	CHECK(!strcmp(entries[4].text, "T\tq \"q\" \\ A"));
	free(copy);
}

/* Each entry the console can't trust is left out, and the file read on. */
static void test_dropped(void)
{
	static const char *bad[] = {
		"msgid \"%d of %s\"\nmsgstr \"%s з %d\"\n",       /* conversions reordered */
		"msgid \"%d games\"\nmsgstr \"Ігри\"\n",          /* a conversion dropped */
		"msgid \"Games\"\nmsgstr \"Ігри: %s\"\n",         /* a conversion added */
		"msgid \"50%% off\"\nmsgstr \"50% знижки\"\n",    /* %% became % з */
		"msgid \"%d\"\nmsgstr \"%n\"\n",
		"msgid \"Games\"\nmsgstr \"\"\n",                 /* untranslated */
		"msgid \"Games\"\nmsgstr \"a\\001b\"\n",          /* a control character */
		"msgid \"Games\"\nmsgstr \"a\\0b\"\n",            /* an escaped 0 */
		"msgid \"Games\"\nmsgstr \"a\\x00\"\n",
		"msgid \"Games\"\nmsgstr \"a\\q\"\n",             /* not an escape */
		"msgid \"Games\"\nmsgstr \"open\n",               /* unterminated */
		"msgid \"Games\"\nmsgstr \"Ігри\" junk\n",
		"msgid \"Games\"\nmsgctxt \"x\"\nmsgstr \"Ігри\"\n",
		"msgid \"Games\"\nmsgstr[0] \"Ігри\"\n",
		"msgstr \"Ігри\"\n",                              /* no msgid */
		"msgid \"Ok\"\nmsgstr \"01234567890123456789012345678901234567890123456789012345678901234567890123456789\"\n",
		"msgid\"Games\"\nmsgstr \"Ігри\"\n",
	};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char text[512];
		char *copy;

		snprintf(text, sizeof(text), "%smsgid \"Kept\"\nmsgstr \"Лишилось\"\n", bad[i]);
		CHECK(parse(text, &copy) == 1u);
		CHECK(!strcmp(entries[0].english, "Kept"));
		CHECK(!strcmp(entries[0].text, "Лишилось"));
		free(copy);
	}
}

static void test_max(void)
{
	char *copy = dupText("msgid \"A\"\nmsgstr \"Б\"\nmsgid \"B\"\nmsgstr \"В\"\n");

	CHECK(UILang_Parse(copy, strlen(copy), entries, 1u) == 1u);
	CHECK(UILang_Parse(NULL, 4u, entries, 1u) == 0u);
	free(copy);
}

static void test_lookup(void)
{
	static const char file[] =
		"msgid \"Settings\"\nmsgstr \"Налаштування\"\n"
		"msgid \"Settings\"\nmsgstr \"Друге\"\n"
		"msgid \"%d enabled\"\nmsgstr \"Увімкнено: %d\"\n";
	/* A comment far longer than the entries, which the table doesn't keep. */
	const size_t comment = 8192u;
	char *data = malloc(comment + sizeof(file));
	char line[32];

	CHECK(!UILang_Loaded(UI_LANG_UKRAINIAN));
	CHECK(UILang_MemoryFootprint() == 0u);
	UILang_Select(UI_LANG_UKRAINIAN);
	/* No table yet: English. */
	CHECK(!strcmp(UILang_Text("Settings"), "Settings"));
	CHECK(UILang_Text(NULL) == NULL);
	memset(data, '#', comment - 1u);
	data[comment - 1u] = '\n';
	memcpy(data + comment, file, sizeof(file));
	CHECK(UILang_Load(UI_LANG_UKRAINIAN, data, comment + sizeof(file) - 1u));
	CHECK(UILang_Loaded(UI_LANG_UKRAINIAN));
	/* The strings, three entries and a 16-slot index; data is freed. */
	CHECK(UILang_MemoryFootprint() > 0u && UILang_MemoryFootprint() < 512u);
	CHECK(!strcmp(UILang_Text("Settings"), "Налаштування")); /* the first wins */
	CHECK(!strcmp(UILang_Text("Library"), "Library"));
	snprintf(line, sizeof(line), UI_T("%d enabled"), 3);
	CHECK(!strcmp(line, "Увімкнено: 3"));
	/* A second file for a language that has one is let go. */
	CHECK(UILang_Load(UI_LANG_UKRAINIAN, dupText("msgid \"A\"\nmsgstr \"Б\"\n"), 22u));
	CHECK(!strcmp(UILang_Text("A"), "A"));
	UILang_Select(UI_LANG_ENGLISH);
	CHECK(!strcmp(UILang_Text("Settings"), "Settings"));
	UILang_Select(99);
	CHECK(UILang_Selected() == UI_LANG_ENGLISH);
	CHECK(!UILang_Load(UI_LANG_ENGLISH, dupText("x"), 1u));
	CHECK(!UILang_Load(UI_LANG_MAX, dupText("x"), 1u));
}

static void test_characters(void)
{
	int bytes;

	CHECK(UILang_Character("Я", &bytes) == 0x42fu && bytes == 2);
	CHECK(UILang_Character("ї", &bytes) == 0x457u && bytes == 2);
	CHECK(UILang_Character("Ґ", &bytes) == 0x490u && bytes == 2);
	/* Cyrillic the sheet hasn't (Ё), and IPL bytes, are one byte each. */
	CHECK(UILang_Character("Ё", &bytes) == 0xd0u && bytes == 1);
	CHECK(UILang_Character("\321a", &bytes) == 0xd1u && bytes == 1); /* Ñ */
	CHECK(UILang_Character("\205", &bytes) == 0x85u && bytes == 1);
	CHECK(UILang_Character("", &bytes) == 0u && bytes == 0);
	/* A cut never splits a letter. */
	CHECK(UILang_Head("Ая", 1u) == 0u);
	CHECK(UILang_Head("Ая", 2u) == 2u);
	CHECK(UILang_Head("Ая", 3u) == 2u);
	CHECK(UILang_Head("abc", 2u) == 2u);
	/* An IPL Ñ (0xd1) and the ellipsis after it would read as "х". */
	CHECK(UILang_Head("a\321b", 2u) == 1u);
	CHECK(UILang_Head("a\321\321b", 3u) == 1u);
	CHECK(UILang_Head("\321\321\217", 2u) == 0u); /* Ñ, then half of я */
	CHECK(UILang_Head("a\320b", 2u) == 2u); /* Ð and 0x85 are no sheet letter */
}

int main(void)
{
	test_entries();
	test_dropped();
	test_max();
	test_lookup();
	test_characters();
	printf("ui_lang: language files, dropped entries, lookups and UTF-8 letters: %u checks PASS\n", checks);
	return 0;
}
