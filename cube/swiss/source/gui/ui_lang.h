#ifndef UI_LANG_H
#define UI_LANG_H

#include <stdbool.h>
#include <stddef.h>

/*
 * ui_lang -- Menu Language: the words the interface draws in. English is
 * the source: every string in the code is English, and a language is a
 * file on the card (swiss/indigo/<code>.po, gettext's format) that maps
 * those strings, exactly as the code spells them, to its own in UTF-8.
 * drawString and the text measures look up whatever they are handed, so a
 * label drawn from a literal needs nothing more; text built from a format,
 * or cut into lines before it is drawn, looks its format or whole text up
 * with UI_T first. A string with no entry, or a language whose file isn't
 * there, stays English.
 *
 * Only the interface changes: what Indigo writes to the card (the settings
 * files' keys and values), System Language and the games' own text stay as
 * they are. The IPL font has no Cyrillic, so drawString draws Ukrainian
 * letters from a sheet of its own (gui/ui_font_cyrillic.inc).
 *
 * A lookup allocates nothing and never blocks, so a draw function may make
 * one a frame.
 */

enum {
	UI_LANG_ENGLISH,
	UI_LANG_UKRAINIAN,
	UI_LANG_MAX
};

/* A language file holds at most this many entries and this many bytes, and
 * a translation is at most UI_LANG_TEXT_MAX bytes: message boxes format
 * into Swiss's 2 KB txtbuffer. */
#define UI_LANG_MAX_ENTRIES 4000u
#define UI_LANG_FILE_MAX (1024u * 1024u)
#define UI_LANG_TEXT_MAX 1600u

typedef struct {
	const char *english;
	const char *text;
} uiLangEntry_t;

/* What the settings file writes for each language, what Settings shows for
 * it (in that language), and its file's name in swiss/indigo/ (NULL for
 * English, which needs none). */
extern const char *uiLangKeys[UI_LANG_MAX];
extern const char *uiLangNames[UI_LANG_MAX];
extern const char *uiLangFiles[UI_LANG_MAX];

/* A language file's entries, decoded where they lie in data (size bytes):
 * msgid and msgstr pairs, each a quoted C string continued by quoted lines,
 * with '#' comments between them. Writes at most max entries; returns how
 * many. An entry is left out, and the file read on, when it isn't one of
 * these or its translation is empty, has a control character other than a
 * newline or tab, is longer than UI_LANG_TEXT_MAX (or four times its
 * English and 64 bytes more), or doesn't have exactly its English's printf
 * conversions in the same order: the code formats with a translation as it
 * would with its English. Pure; never reads or writes past size. */
size_t UILang_Parse(char *data, size_t size, uiLangEntry_t *entries,
	size_t max);

/* Takes data (size bytes of a language file, allocated with malloc) as
 * language's table for good: it is parsed where it lies and kept. Frees
 * data instead when the language already has its table or the file has no
 * entry. Call from the menu thread; the video thread sees the table whole
 * or not at all. True once language has a table. */
bool UILang_Load(int language, char *data, size_t size);
bool UILang_Loaded(int language);

/* Select the language UILang_Text translates into, a UI_LANG_ value.
 * Anything out of range is English. The video thread selects the setting's
 * once a frame, as it sets Menu Widescreen. */
void UILang_Select(int language);
int UILang_Selected(void);

/* english in the selected language: its entry, or english itself without
 * one. NULL stays NULL. A format looked up keeps the compiler's check of
 * its arguments against the English. */
const char *UILang_Text(const char *english) __attribute__((format_arg(1)));
#define UI_T(english) UILang_Text(english)

/* The first character of text: its Unicode code point, and how many bytes
 * of text it takes. UTF-8 is read only for the letters the Cyrillic sheet
 * draws, which also shows a file name's Cyrillic (FatFs hands names over in
 * UTF-8); every other byte is one character of the IPL font's own encoding,
 * as before. 0 at the end of text. */
unsigned UILang_Character(const char *text, int *bytes);

/* The most of text's first length bytes that ends on a whole character:
 * length, or one less where a cut there would split a Cyrillic letter.
 * text must go on past length (a cut, not the end). */
size_t UILang_Head(const char *text, size_t length);

#endif
