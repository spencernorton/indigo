#include <stddef.h>
#include <string.h>

#include "ui_lang.h"
#include "ui_saves_details.h"

static bool textValid(const char *text, size_t capacity)
{
	size_t i;

	if(text == NULL) {
		return false;
	}
	for(i = 0u; i < capacity; ++i) {
		unsigned char value = (unsigned char)text[i];

		if(value == 0u) {
			return i > 0u;
		}
		if(value < 0x20u || value == 0x7fu) {
			return false;
		}
	}
	return false;
}

static bool copyText(char *out, size_t capacity, const char *source)
{
	size_t length;

	/* The caller supplies bounded menu copy, never a pointer into a file.
	 * A bounded scan also refuses an unterminated caller buffer. */
	if(!textValid(source, 1024u)) {
		return false;
	}
	length = strlen(source);
	if(length < capacity) {
		memcpy(out, source, length + 1u);
	}
	else {
		/* The ellipsis never follows half a Cyrillic letter. */
		length = UILang_Head(source, capacity - 2u);
		memcpy(out, source, length);
		out[length] = (char)0x85; /* IPL ellipsis */
		out[length + 1u] = '\0';
	}
	return true;
}

bool UISaveDetails_Build(uiSaveDetailsSnapshot_t *snapshot, const char *name,
	uint32_t blocks, bool estimated, const char *source, const char *updated,
	const char *icon)
{
	uiSaveDetailsSnapshot_t candidate;

	if(snapshot == NULL) {
		return false;
	}
	memset(snapshot, 0, sizeof(*snapshot));
	memset(&candidate, 0, sizeof(candidate));
	candidate.blocks = blocks;
	candidate.estimated = estimated;
	if(!copyText(candidate.name, sizeof(candidate.name), name) ||
		!copyText(candidate.source, sizeof(candidate.source), source) ||
		!copyText(candidate.updated, sizeof(candidate.updated), updated) ||
		!copyText(candidate.icon, sizeof(candidate.icon), icon) ||
		!UISaveDetails_Valid(&candidate)) {
		return false;
	}
	*snapshot = candidate;
	return true;
}

bool UISaveDetails_Valid(const uiSaveDetailsSnapshot_t *snapshot)
{
	return snapshot != NULL && snapshot->blocks <= UINT32_MAX / 8u &&
		textValid(snapshot->name, sizeof(snapshot->name)) &&
		textValid(snapshot->source, sizeof(snapshot->source)) &&
		textValid(snapshot->updated, sizeof(snapshot->updated)) &&
		textValid(snapshot->icon, sizeof(snapshot->icon));
}
