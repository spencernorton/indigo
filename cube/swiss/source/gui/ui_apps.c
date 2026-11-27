/* ui_apps.c - Apps (Home > Apps): which files are programs, their names
   and pictures, and the poster slots. See ui_apps.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_apps.h"

#ifdef GEKKO
/* GameCube newlib runs in the C locale. Its comparisons already fold
 * precisely ASCII A-Z, including for high-byte filenames. */
#include <strings.h>
#define compareFolded strcasecmp
#define equalPrefixFolded(a, b, count) (strncasecmp((a), (b), (count)) == 0)
#else
static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* strcasecmp for ASCII, and in the C library of every build. */
static int compareFolded(const char *a, const char *b)
{
	for(;; ++a, ++b) {
		int x = lower((unsigned char)*a);
		int y = lower((unsigned char)*b);

		if(x != y || x == 0) return x - y;
	}
}

/* Compare up to count bytes using ASCII case folding, stopping at NUL. */
static bool equalPrefixFolded(const char *a, const char *b, size_t count)
{
	for(size_t i = 0; i < count; ++i) {
		if(lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
			return false;
		if(!a[i]) return true;
	}
	return true;
}

#endif

/* The program's name without its extension, and the extension's length. */
static size_t extensionLength(uiAppsType_t type)
{
	return type == UI_APPS_TYPE_DOL_CLI ? 8u : 4u;
}

uiAppsType_t UIApps_ProgramType(const char *name)
{
	const char *extension;
	uiAppsType_t type;

	if(name == NULL || name[0] == '.') {
		return UI_APPS_TYPE_NONE;
	}
	extension = strrchr(name, '.');
	if(extension == NULL) return UI_APPS_TYPE_NONE;
	if(compareFolded(extension, ".dol") == 0) type = UI_APPS_TYPE_DOL;
	else if(compareFolded(extension, ".dol+cli") == 0) type = UI_APPS_TYPE_DOL_CLI;
	else if(compareFolded(extension, ".elf") == 0) type = UI_APPS_TYPE_ELF;
	else return UI_APPS_TYPE_NONE;
	/* boot.* is the Homebrew Channel's: a Wii program. */
	if(extension - name == 4 &&
		equalPrefixFolded(name, "boot", 4u)) {
		return UI_APPS_TYPE_NONE;
	}
	return type;
}

bool UIApps_IsFolder(const char *name)
{
	return name != NULL && name[0] != '\0' && name[0] != '.';
}

const char *UIApps_TypeLabel(uiAppsType_t type)
{
	switch(type) {
		case UI_APPS_TYPE_DOL: return "DOL";
		case UI_APPS_TYPE_DOL_CLI: return "DOL+CLI";
		case UI_APPS_TYPE_ELF: return "ELF";
		default: return "";
	}
}

/* entries' picture called base (length bytes) + ".png", or NULL. */
static const uiAppsEntry_t *findPicture(const uiAppsEntry_t *entries,
	size_t entryCount, const char *base, size_t length)
{
	size_t i;

	for(i = 0u; i < entryCount; ++i) {
		const char *name = entries[i].name;

		/* base contains length non-NUL bytes. A shorter name fails the
		 * prefix match before its suffix is considered. */
		if(!entries[i].folder && !entries[i].hidden && name != NULL &&
			equalPrefixFolded(name, base, length) &&
			compareFolded(name + length, ".png") == 0) {
			return &entries[i];
		}
	}
	return NULL;
}

/* entries' file matching exactName ignoring ASCII case, or NULL. */
static const uiAppsEntry_t *findNamedFile(const uiAppsEntry_t *entries,
	size_t entryCount, const char *exactName)
{
	size_t i;

	for(i = 0u; i < entryCount; ++i) {
		if(entries[i].folder || entries[i].hidden || entries[i].name == NULL) {
			continue;
		}
		if(compareFolded(entries[i].name, exactName) == 0) {
			return &entries[i];
		}
	}
	return NULL;
}

static bool isDefaultBase(const char *name, size_t base)
{
	return base == sizeof("default") - 1u && equalPrefixFolded(name, "default", base);
}

/* folder/name into out; false when it doesn't fit. */
static bool joinPath(char out[UI_APPS_PATH_LENGTH], const char *folder,
	const char *name)
{
	const char *separator = folder[0] ? "/" : "";
	int written = snprintf(out, UI_APPS_PATH_LENGTH, "%s%s%s", folder, separator, name);

	return written > 0 && (size_t)written < UI_APPS_PATH_LENGTH;
}

bool UIApps_AddFolder(uiApp_t *list, size_t *count, size_t max,
	const char *folder, const uiAppsEntry_t *entries, size_t entryCount)
{
	const uiAppsEntry_t *icon = NULL;
	const uiAppsEntry_t *banner = NULL;
	size_t i;

	if(list == NULL || count == NULL || folder == NULL ||
		(entries == NULL && entryCount > 0u)) {
		return true;
	}
	if(folder[0] != '\0') {
		icon = findNamedFile(entries, entryCount, "icon.png");
		banner = findNamedFile(entries, entryCount, "opening.bnr");
	}
	for(i = 0u; i < entryCount; ++i) {
		const uiAppsEntry_t *entry = &entries[i];
		const uiAppsEntry_t *picture;
		uiAppsType_t type;
		uiApp_t *app;
		size_t base;
		bool isDefault;
		bool pictureIsBnr = false;

		if(entry->folder || entry->hidden ||
			(type = UIApps_ProgramType(entry->name)) == UI_APPS_TYPE_NONE) {
			continue;
		}
		if(*count >= max) {
			return false;
		}
		app = &list[*count];
		memset(app, 0, sizeof(*app));
		if(!joinPath(app->program, folder, entry->name)) {
			continue;
		}
		base = strlen(entry->name) - extensionLength(type);
		isDefault = folder[0] != '\0' && isDefaultBase(entry->name, base);
		picture = findPicture(entries, entryCount, entry->name, base);
		if(picture == NULL) {
			picture = icon;
		}
		if(picture == NULL && banner != NULL) {
			picture = banner;
			pictureIsBnr = true;
		}
		if(picture != NULL && !joinPath(app->picture, folder, picture->name)) {
			app->picture[0] = '\0';
			picture = NULL;
			pictureIsBnr = false;
		}
		if(isDefault && banner != NULL &&
			joinPath(app->banner, folder, banner->name)) {
			app->bannerSize = banner->size;
			app->bannerHandle = banner->handle;
		}
		if(isDefault) {
			snprintf(app->name, sizeof(app->name), "%s", folder);
		}
		else {
			snprintf(app->name, sizeof(app->name), "%.*s",
				(int)(base < sizeof(app->name) ? base : sizeof(app->name) - 1u),
				entry->name);
		}
		app->size = entry->size;
		app->pictureSize = picture != NULL ? picture->size : 0u;
		app->programHandle = entry->handle;
		app->pictureHandle = picture != NULL ? picture->handle : NULL;
		app->type = (uint8_t)type;
		app->isDefaultDol = isDefault;
		app->pictureIsBnr = pictureIsBnr;
		(*count)++;
	}
	return true;
}

void UIApps_ApplyBanner(uiApp_t *app, const char *title, const char *author)
{
	if(app == NULL) {
		return;
	}
	if(title != NULL && title[0] != '\0') {
		snprintf(app->name, sizeof(app->name), "%s", title);
	}
	if(author != NULL && author[0] != '\0') {
		snprintf(app->author, sizeof(app->author), "%s", author);
	}
}

static int appOrder(const void *a, const void *b)
{
	const uiApp_t *x = a;
	const uiApp_t *y = b;
	int byName = compareFolded(x->name, y->name);

	return byName != 0 ? byName : strcmp(x->program, y->program);
}

void UIApps_Sort(uiApp_t *list, size_t count)
{
	if(list != NULL && count > 1u) {
		qsort(list, count, sizeof(list[0]), appOrder);
	}
}

void UIAppsArt_Init(uiAppsArt_t *art, uint32_t nowMs)
{
	uint32_t i;

	if(art == NULL) return;
	memset(art, 0, sizeof(*art));
	for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) {
		art->slots[i].app = -1;
		/* Never drawn: free to fill at once. */
		art->slots[i].freedMs = nowMs - UI_APPS_ART_QUARANTINE_MS;
	}
}

static int slotOf(const uiAppsArt_t *art, int32_t app)
{
	uint32_t i;

	for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) {
		if(art->slots[i].state != UI_APPS_ART_EMPTY &&
			art->slots[i].app == app) {
			return (int)i;
		}
	}
	return -1;
}

void UIAppsArt_Want(uiAppsArt_t *art, const int32_t *apps, uint32_t count,
	uint32_t nowMs)
{
	uint32_t i, j;

	if(art == NULL || (apps == NULL && count > 0u)) return;
	if(count > UI_APPS_ART_SLOTS) count = UI_APPS_ART_SLOTS;
	art->windowCount = 0u;
	for(i = 0u; i < count; ++i) {
		bool repeated = false;

		for(j = 0u; j < art->windowCount; ++j) {
			repeated = repeated || art->window[j] == apps[i];
		}
		if(apps[i] >= 0 && !repeated) {
			art->window[art->windowCount++] = apps[i];
		}
	}
	/* Let go of what is no longer wanted. */
	for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) {
		uiAppsArtSlot_t *slot = &art->slots[i];
		bool wanted = false;

		for(j = 0u; j < art->windowCount; ++j) {
			wanted = wanted || art->window[j] == slot->app;
		}
		if(slot->state == UI_APPS_ART_EMPTY || wanted) continue;
		if(slot->state == UI_APPS_ART_READY) {
			slot->freedMs = nowMs;
		}
		slot->state = UI_APPS_ART_EMPTY;
		slot->app = -1;
	}
	/* Give each wanted app a slot, the longest free first. */
	for(j = 0u; j < art->windowCount; ++j) {
		int best = -1;

		if(slotOf(art, art->window[j]) >= 0) continue;
		for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) {
			if(art->slots[i].state == UI_APPS_ART_EMPTY && (best < 0 ||
				nowMs - art->slots[i].freedMs >
					nowMs - art->slots[best].freedMs)) {
				best = (int)i;
			}
		}
		if(best < 0) break;
		art->slots[best].app = art->window[j];
		art->slots[best].state = UI_APPS_ART_WANTED;
	}
}

int UIAppsArt_Next(const uiAppsArt_t *art, uint32_t nowMs)
{
	uint32_t j;

	if(art == NULL) return -1;
	for(j = 0u; j < art->windowCount; ++j) {
		int slot = slotOf(art, art->window[j]);

		if(slot >= 0 && art->slots[slot].state == UI_APPS_ART_WANTED &&
			nowMs - art->slots[slot].freedMs >= UI_APPS_ART_QUARANTINE_MS) {
			return slot;
		}
	}
	return -1;
}

bool UIAppsArt_Done(uiAppsArt_t *art, int slot, int32_t app, bool ok)
{
	if(art == NULL || slot < 0 || slot >= (int)UI_APPS_ART_SLOTS ||
		art->slots[slot].state != UI_APPS_ART_WANTED ||
		art->slots[slot].app != app) {
		return false;
	}
	art->slots[slot].state = ok ? UI_APPS_ART_READY : UI_APPS_ART_NONE;
	return true;
}

int UIAppsArt_Find(const uiAppsArt_t *art, int32_t app)
{
	int slot;

	if(art == NULL || app < 0) return -1;
	slot = slotOf(art, app);
	return slot >= 0 && art->slots[slot].state == UI_APPS_ART_READY ? slot : -1;
}
