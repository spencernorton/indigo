#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ui_lang.h"

const char *uiLangKeys[UI_LANG_MAX] = {"English", "Ukrainian"};
const char *uiLangNames[UI_LANG_MAX] = {"English", "Українська"};
const char *uiLangFiles[UI_LANG_MAX] = {NULL, "uk.po"};

/* A language's table, allocated as it loads and never written again: its
 * entries with their strings packed behind them, and an index of open
 * addressing over the English, a power of two at least twice the entries so
 * every probe stays short. A slot holds an entry's index plus one; 0 is
 * empty. */
typedef char uiLangSlotsFit[UI_LANG_MAX_ENTRIES * 2u <= 65535u ? 1 : -1];

typedef struct {
	const uiLangEntry_t *entries;
	size_t bytes;
	uint32_t mask;
	uint16_t slots[];
} uiLangTable_t;

/* Each language's table once it is whole; read by the video thread. */
static uiLangTable_t *published[UI_LANG_MAX];
static volatile int selected = UI_LANG_ENGLISH;

static uint32_t hashText(const char *text)
{
	uint32_t hash = 2166136261u;

	while(*text) {
		hash = (hash ^ (uint8_t)*text++) * 16777619u;
	}
	return hash;
}

/* One quoted string at *at, before end, decoded to *out: C's escapes \n,
 * \t, \", \\, \ooo and \xhh, none of them 0. *at moves past the closing
 * quote. False for anything else. */
static bool decodeQuoted(char **at, char *end, char **out)
{
	char *c = *at;
	char *o = *out;

	if(c >= end || *c != '"') {
		return false;
	}
	for(c++; c < end && *c != '"'; c++) {
		unsigned value;

		if(*c == '\n' || *c == '\0') {
			return false;
		}
		if(*c != '\\') {
			*o++ = *c;
			continue;
		}
		if(++c >= end) {
			return false;
		}
		switch(*c) {
			case 'n': *o++ = '\n'; continue;
			case 't': *o++ = '\t'; continue;
			case '"': *o++ = '"'; continue;
			case '\\': *o++ = '\\'; continue;
			case 'x':
				value = 0;
				for(int digits = 0; digits < 2 && c + 1 < end; digits++) {
					char h = c[1];
					int nibble = h >= '0' && h <= '9' ? h - '0' :
						h >= 'a' && h <= 'f' ? h - 'a' + 10 :
						h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;

					if(nibble < 0) {
						if(digits == 0) {
							return false;
						}
						break;
					}
					value = value * 16u + (unsigned)nibble;
					c++;
				}
				break;
			default:
				if(*c < '0' || *c > '7') {
					return false;
				}
				value = (unsigned)(*c - '0');
				for(int digits = 1; digits < 3 && c + 1 < end && c[1] >= '0' && c[1] <= '7'; digits++) {
					value = value * 8u + (unsigned)(*++c - '0');
				}
				if(value > 255u) {
					return false;
				}
				break;
		}
		if(value == 0u) {
			return false;
		}
		*o++ = (char)value;
	}
	if(c >= end) {
		return false;
	}
	*at = c + 1;
	*out = o;
	return true;
}

/* The printf conversion starting at text (a '%'): its length. */
static size_t conversionLength(const char *text)
{
	size_t length = 1;

	while(text[length] != '\0' && strchr("-+ #0123456789.*hlLqjzt", text[length])) {
		length++;
	}
	return text[length] != '\0' ? length + 1 : length;
}

/* Whether text has exactly english's conversions, in the same order. */
static bool sameConversions(const char *english, const char *text)
{
	for(;;) {
		size_t a, b;

		english = strchr(english, '%');
		text = strchr(text, '%');
		if(english == NULL || text == NULL) {
			return english == text;
		}
		a = conversionLength(english);
		b = conversionLength(text);
		if(a != b || memcmp(english, text, a) != 0) {
			return false;
		}
		english += a;
		text += b;
	}
}

static bool plainText(const char *text)
{
	for(; *text; text++) {
		if((uint8_t)*text < 0x20u && *text != '\n' && *text != '\t') {
			return false;
		}
	}
	return true;
}

static bool validEntry(const char *english, const char *text)
{
	size_t englishLength = strlen(english);
	size_t length = strlen(text);

	return englishLength > 0u && length > 0u && length <= UI_LANG_TEXT_MAX &&
		length <= englishLength * 4u + 64u && plainText(english) &&
		plainText(text) && sameConversions(english, text);
}

/* Whether line (up to end) starts with keyword and a space. */
static bool keyword(const char *line, const char *end, const char *word)
{
	size_t length = strlen(word);

	return (size_t)(end - line) > length && !memcmp(line, word, length) &&
		(line[length] == ' ' || line[length] == '\t');
}


static void skipBlanks(char **c, const char *end)
{
	while(*c < end && (**c == ' ' || **c == '\t' || **c == '\r')) {
		(*c)++;
	}
}

size_t UILang_Parse(char *data, size_t size, uiLangEntry_t *entries,
	size_t max)
{
	enum { NONE, ID, TEXT } state = NONE;
	char *english = NULL, *text = NULL, *out = NULL;
	char *line = data, *end;
	size_t count = 0;

	if(data == NULL || entries == NULL) {
		return 0;
	}
	end = data + size;
	/* Decoding writes behind what it reads (each line's quotes and newline
	 * are dropped), so every string and its 0 fit where they lie. */
	for(;;) {
		char *next = line < end ? memchr(line, '\n', (size_t)(end - line)) : NULL;
		char *lineEnd = next != NULL ? next : end;
		char *c = line;

		skipBlanks(&c, lineEnd);
		if(state != NONE && c < lineEnd && *c == '"') {
			/* The string goes on. */
		}
		else if(state == ID && keyword(c, lineEnd, "msgstr")) {
			c += 6;
			skipBlanks(&c, lineEnd);
			*out = '\0';
			text = out = c;
			state = TEXT;
		}
		else {
			/* Anything else ends the entry before it. */
			if(state == TEXT) {
				*out = '\0';
				if(count < max && validEntry(english, text)) {
					entries[count].english = english;
					entries[count].text = text;
					count++;
				}
			}
			state = NONE;
			if(keyword(c, lineEnd, "msgid")) {
				c += 5;
				skipBlanks(&c, lineEnd);
				english = out = c;
				state = ID;
			}
		}
		if(state != NONE) {
			if(!decodeQuoted(&c, lineEnd, &out)) {
				state = NONE;
			}
			else {
				skipBlanks(&c, lineEnd);
				if(c < lineEnd) {
					state = NONE;
				}
			}
		}
		if(next == NULL) {
			if(state == TEXT) {
				*out = '\0';
				if(count < max && validEntry(english, text)) {
					entries[count].english = english;
					entries[count].text = text;
					count++;
				}
			}
			return count;
		}
		line = next + 1;
	}
}

bool UILang_Load(int language, char *data, size_t size)
{
	uiLangTable_t *table;
	uiLangEntry_t *parsed, *entries;
	char *pool;
	size_t count, strings = 0u, slotCount = 16u;

	if(language <= UI_LANG_ENGLISH || language >= UI_LANG_MAX || data == NULL ||
		size > UI_LANG_FILE_MAX || UILang_Loaded(language)) {
		free(data);
		return UILang_Loaded(language);
	}
	parsed = malloc(UI_LANG_MAX_ENTRIES * sizeof(*parsed));
	count = parsed != NULL ? UILang_Parse(data, size, parsed, UI_LANG_MAX_ENTRIES) : 0u;
	for(size_t entry = 0; entry < count; entry++) {
		strings += strlen(parsed[entry].english) + strlen(parsed[entry].text) + 2u;
	}
	while(slotCount < count * 2u) {
		slotCount <<= 1;
	}
	entries = count > 0u ? malloc(count * sizeof(*entries) + strings) : NULL;
	table = entries != NULL ?
		calloc(1, sizeof(*table) + slotCount * sizeof(table->slots[0])) : NULL;
	if(table == NULL) {
		free(entries);
		free(parsed);
		free(data);
		return false;
	}
	/* Only the strings are kept, packed behind the entries: the file, with
	 * its comments, quotes and escapes, goes. */
	pool = (char *)(entries + count);
	for(size_t entry = 0; entry < count; entry++) {
		size_t english = strlen(parsed[entry].english) + 1u;
		size_t text = strlen(parsed[entry].text) + 1u;

		entries[entry].english = memcpy(pool, parsed[entry].english, english);
		entries[entry].text = memcpy(pool + english, parsed[entry].text, text);
		pool += english + text;
	}
	free(parsed);
	free(data);
	table->entries = entries;
	table->mask = (uint32_t)slotCount - 1u;
	table->bytes = sizeof(*table) + slotCount * sizeof(table->slots[0]) +
		count * sizeof(*entries) + strings;
	for(size_t entry = 0; entry < count; entry++) {
		uint32_t slot = hashText(entries[entry].english) & table->mask;

		/* The first of two equal strings wins. */
		while(table->slots[slot] &&
			strcmp(entries[table->slots[slot] - 1u].english, entries[entry].english)) {
			slot = (slot + 1u) & table->mask;
		}
		if(!table->slots[slot]) {
			table->slots[slot] = (uint16_t)(entry + 1u);
		}
	}
	/* The table and its entries stay for good. */
	__atomic_store_n(&published[language], table, __ATOMIC_RELEASE);
	return true;
}

size_t UILang_MemoryFootprint(void)
{
	size_t bytes = 0u;

	for(int language = 0; language < UI_LANG_MAX; language++) {
		const uiLangTable_t *table = __atomic_load_n(&published[language], __ATOMIC_ACQUIRE);

		bytes += table != NULL ? table->bytes : 0u;
	}
	return bytes;
}

bool UILang_Loaded(int language)
{
	return language > UI_LANG_ENGLISH && language < UI_LANG_MAX &&
		__atomic_load_n(&published[language], __ATOMIC_ACQUIRE) != NULL;
}

void UILang_Select(int language)
{
	selected = language > UI_LANG_ENGLISH && language < UI_LANG_MAX ?
		language : UI_LANG_ENGLISH;
}

int UILang_Selected(void)
{
	return selected;
}

const char *UILang_Text(const char *english)
{
	int language = selected;
	const uiLangTable_t *table;
	uint32_t slot;

	if(english == NULL || language == UI_LANG_ENGLISH || !*english) {
		return english;
	}
	table = __atomic_load_n(&published[language], __ATOMIC_ACQUIRE);
	if(table == NULL) {
		return english;
	}
	slot = hashText(english) & table->mask;
	while(table->slots[slot]) {
		const uiLangEntry_t *entry = &table->entries[table->slots[slot] - 1u];

		if(!strcmp(entry->english, english)) {
			return entry->text;
		}
		slot = (slot + 1u) & table->mask;
	}
	return english;
}
