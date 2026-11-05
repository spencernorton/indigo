#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_saves_details.h"
#include "ui_lang.h"

int main(void)
{
	uiSaveDetailsSnapshot_t snapshot;
	char name[] = "Copper Archive";
	char source[] = "Read-only card image";
	char updated[] = "2024-02-29 12:34";
	char icon[] = "Animated";
	char longName[160], unterminated[1024];
	const char *invalid[] = {NULL, "", "a\nb", "\177"};
	size_t i;

	assert(UISaveDetails_Build(&snapshot, name, 2u, false, source, updated, icon));
	memset(name, 'x', sizeof(name) - 1u);
	memset(source, 'x', sizeof(source) - 1u);
	memset(updated, 'x', sizeof(updated) - 1u);
	memset(icon, 'x', sizeof(icon) - 1u);
	assert(strcmp(snapshot.name, "Copper Archive") == 0);
	assert(strcmp(snapshot.source, "Read-only card image") == 0);
	assert(strcmp(snapshot.updated, "2024-02-29 12:34") == 0);
	assert(!strcmp(snapshot.icon, "Animated"));
	assert(snapshot.blocks == 2u && !snapshot.estimated);
	assert(sizeof(snapshot) < 192u);

	memset(longName, 'W', sizeof(longName));
	longName[sizeof(longName) - 1u] = '\0';
	assert(UISaveDetails_Build(&snapshot, longName, UINT32_MAX / 8u,
		true, "SD save", "Unable to read metadata", "Unavailable"));
	assert(strlen(snapshot.name) == sizeof(snapshot.name) - 1u);
	assert((unsigned char)snapshot.name[sizeof(snapshot.name) - 2u] == 0x85u);
	assert(snapshot.estimated && UISaveDetails_Valid(&snapshot));
	/* A Cyrillic name is cut between letters before its ellipsis. */
	longName[0] = 'a';
	for(size_t i = 1u; i + 2u < sizeof(longName); i += 2u) {
		longName[i] = (char)0xd1;
		longName[i + 1u] = (char)0x8f; /* я */
	}
	longName[sizeof(longName) - 1u] = '\0';
	assert(UISaveDetails_Build(&snapshot, longName, 1u, false, "SD save", "Unknown", "None stored"));
	{
		size_t at = strlen(snapshot.name) - 1u;

		assert((unsigned char)snapshot.name[at] == 0x85u);
		assert(UILang_Head(snapshot.name, at) == at);
	}
	assert(!UISaveDetails_Build(&snapshot, "Save", UINT32_MAX / 8u + 1u,
		false, "SD save", "Unknown", "None stored"));
	assert(!UISaveDetails_Valid(&snapshot));
	assert(UISaveDetails_Build(&snapshot, "Save", 0u, true, "SD save", "Unknown", "None stored"));
	for(i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		assert(!UISaveDetails_Build(&snapshot, invalid[i], 1u, false, "Slot A", "Unknown", "None stored"));
		assert(!UISaveDetails_Build(&snapshot, "Save", 1u, false, invalid[i], "Unknown", "None stored"));
		assert(!UISaveDetails_Build(&snapshot, "Save", 1u, false, "Slot A", invalid[i], "Static"));
		assert(!UISaveDetails_Build(&snapshot, "Save", 1u, false, "Slot A", "Unknown", invalid[i]));
		assert(!UISaveDetails_Valid(&snapshot));
	}
	memset(unterminated, 'a', sizeof(unterminated));
	assert(!UISaveDetails_Build(&snapshot, unterminated, 1u, false, "Slot A", "Unknown", "None stored"));
	assert(!UISaveDetails_Build(NULL, "Save", 1u, false, "Slot A", "Unknown", "None stored"));
	assert(!UISaveDetails_Valid(NULL));
	assert(UISaveDetails_Build(&snapshot, "Save", 1u, false, "Slot A", "Unknown", "None stored"));
	memset(snapshot.updated, 'x', sizeof(snapshot.updated));
	assert(!UISaveDetails_Valid(&snapshot));
	puts("Save details: bounded immutable copy, metadata fallback and size bounds PASS");
	return 0;
}
