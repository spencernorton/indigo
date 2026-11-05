/* Fuzz Menu Language's file (swiss/indigo/uk.po) as Indigo reads it: any
 * file parses without a sanitizer finding, and every entry it keeps lies
 * inside the file, ends there, and is one the code can format and draw
 * with: not empty, no control character but a newline or tab, no longer
 * than UI_LANG_TEXT_MAX, and with its English's printf conversions. The
 * bytes are copied to exactly their size, so a read past the file is a
 * finding. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_lang.h"

static uiLangEntry_t entries[UI_LANG_MAX_ENTRIES];

static int inside(const char *text, const char *data, size_t size)
{
	return text >= data && text < data + size && memchr(text, '\0', (size_t)(data + size - text)) != NULL;
}

/* The printf conversions of text, each its '%' and up to its letter. */
static size_t conversions(const char *text, char out[][16], size_t max)
{
	size_t count = 0;

	while((text = strchr(text, '%')) != NULL && count < max) {
		size_t length = 1;

		while(text[length] && strchr("-+ #0123456789.*hlLqjzt", text[length]) && length < 14)
			length++;
		if(text[length])
			length++;
		memcpy(out[count], text, length);
		out[count][length] = '\0';
		count++;
		text += length;
	}
	return count;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *copy = malloc(size ? size : 1);
	size_t count;

	if(!copy)
		return 0;
	memcpy(copy, data, size);
	count = UILang_Parse(copy, size, entries, UI_LANG_MAX_ENTRIES);
	if(count > UI_LANG_MAX_ENTRIES)
		abort();
	for(size_t i = 0; i < count; i++) {
		const char *english = entries[i].english;
		const char *text = entries[i].text;
		static char a[64][16], b[64][16];
		size_t length, ca, cb;

		if(!inside(english, copy, size) || !inside(text, copy, size))
			abort();
		length = strlen(text);
		if(!*english || !length || length > UI_LANG_TEXT_MAX)
			abort();
		for(const char *c = text; *c; c++) {
			if((uint8_t)*c < 0x20u && *c != '\n' && *c != '\t')
				abort();
		}
		ca = conversions(english, a, 64);
		cb = conversions(text, b, 64);
		if(ca != cb)
			abort();
		for(size_t k = 0; k < ca && ca < 64; k++) {
			if(strcmp(a[k], b[k]))
				abort();
		}
	}
	free(copy);
	return 0;
}
