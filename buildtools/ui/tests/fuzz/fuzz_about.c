/* Fuzz the game descriptions file (swiss/ui/descriptions.txt) as Spotlight
 * reads it, and the feature badges file (swiss/ui/features.txt, the same
 * format) as Game Details reads it: any file indexes without a sanitizer
 * finding, the index is sorted with each game once, every indexed game is
 * found, a description never outgrows the room it is given, and a line's
 * badges are at most four, each trimmed, non-empty and inside its room. The
 * bytes are copied to exactly their size, so a read past the file is a
 * finding. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_about.h"
#include "ui_gameflow_detail.h"

static uiAboutEntry_t entries[UI_ABOUT_MAX_GAMES];
static uiGameflowDetailSnapshot_t snapshot;

static void checkBadges(const char *id, const uiAbout_t *about)
{
	uiGameflowDetailSource_t source;
	char line[UI_GAMEFLOW_DETAIL_BADGE_LINE];
	bool ended = false;

	memset(&source, 0, sizeof(source));
	UIAbout_Find(about, id, line, sizeof(line));
	source.gameId = id;
	source.title = "Game";
	source.badges = line;
	if(!UIGameflowDetail_Build(&snapshot, &source))
		abort();
	for(size_t i = 0; i < UI_GAMEFLOW_DETAIL_BADGES; i++) {
		const char *badge = snapshot.badges[i];
		size_t length = strnlen(badge, UI_GAMEFLOW_DETAIL_BADGE_CAPACITY);

		if(length >= UI_GAMEFLOW_DETAIL_BADGE_CAPACITY)
			abort(); /* not NUL-ended in its room */
		if(length == 0) {
			ended = true;
			continue;
		}
		if(ended || strchr(badge, '|') || badge[0] == ' ' || badge[length - 1] == ' ')
			abort(); /* after the end, not split or not trimmed */
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uiAbout_t about;
	char out[321];
	char *copy = malloc(size ? size : 1);
	size_t count;

	if(!copy)
		return 0;
	memcpy(copy, data, size);
	count = UIAbout_Index(&about, copy, size, entries, UI_ABOUT_MAX_GAMES);
	if(count > UI_ABOUT_MAX_GAMES || count != about.count)
		abort();
	for(size_t i = 0; i < count; i++) {
		char id[7];
		if(i > 0 && memcmp(about.entries[i - 1].id, about.entries[i].id, 6) >= 0)
			abort(); /* unsorted, or a game twice */
		if(i >= 16)
			continue;
		memcpy(id, about.entries[i].id, 6);
		id[6] = '\0';
		if(!UIAbout_Find(&about, id, out, sizeof(out)) || strlen(out) >= sizeof(out))
			abort();
		checkBadges(id, &about);
		/* Another publisher's disc of the same game takes the first one's. */
		id[5] = id[5] == 'Z' ? 'A' : 'Z';
		if(!UIAbout_Find(&about, id, out, 8) || strlen(out) >= 8)
			abort();
	}
	free(copy);
	return 0;
}
