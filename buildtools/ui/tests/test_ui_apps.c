/* Apps' pure half: which files in /apps are programs, their names and
 * pictures, the order they show in, and the poster slots. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_apps.h"

static unsigned checks;

#define CHECK(condition) do { \
	++checks; \
	if(!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#condition); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

#define FILE_ENTRY(n) {(n), false, false, 1000u, NULL}
#define FOLDER_ENTRY(n) {(n), true, false, 0u, NULL}

static void testProgramTypes(void)
{
	static const struct {
		const char *name;
		uiAppsType_t type;
	} cases[] = {
		{"gbi.dol", UI_APPS_TYPE_DOL}, {"GBI.DOL", UI_APPS_TYPE_DOL},
		{"swiss.Dol+Cli", UI_APPS_TYPE_DOL_CLI}, {"a.elf", UI_APPS_TYPE_ELF},
		{"boots.dol", UI_APPS_TYPE_DOL}, {"reboot.dol", UI_APPS_TYPE_DOL},
		{"boot2.elf", UI_APPS_TYPE_ELF},
		/* The Homebrew Channel's, in any case and kind. */
		{"boot.dol", UI_APPS_TYPE_NONE}, {"BOOT.ELF", UI_APPS_TYPE_NONE},
		{"Boot.dol+cli", UI_APPS_TYPE_NONE},
		/* macOS's copies and other dot names. */
		{"._gbi.dol", UI_APPS_TYPE_NONE}, {".dol", UI_APPS_TYPE_NONE},
		{".hidden.dol", UI_APPS_TYPE_NONE},
		{"gbi.dol.txt", UI_APPS_TYPE_NONE}, {"gbi.png", UI_APPS_TYPE_NONE},
		{"gbi.cli", UI_APPS_TYPE_NONE}, {"gbi.dcp", UI_APPS_TYPE_NONE},
		{"dol", UI_APPS_TYPE_NONE}, {"", UI_APPS_TYPE_NONE},
		{"game.iso", UI_APPS_TYPE_NONE}, {"loader.bin", UI_APPS_TYPE_NONE}
	};
	size_t i;

	for(i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		if(UIApps_ProgramType(cases[i].name) != cases[i].type) {
			fprintf(stderr, "type of %s\n", cases[i].name);
		}
		CHECK(UIApps_ProgramType(cases[i].name) == cases[i].type);
	}
	CHECK(UIApps_ProgramType(NULL) == UI_APPS_TYPE_NONE);
	CHECK(UIApps_IsFolder("Game Boy Interface"));
	CHECK(!UIApps_IsFolder(".Trashes"));
	CHECK(!UIApps_IsFolder(""));
	CHECK(!UIApps_IsFolder(NULL));
	CHECK(strcmp(UIApps_TypeLabel(UI_APPS_TYPE_DOL), "DOL") == 0);
	CHECK(strcmp(UIApps_TypeLabel(UI_APPS_TYPE_DOL_CLI), "DOL+CLI") == 0);
	CHECK(strcmp(UIApps_TypeLabel(UI_APPS_TYPE_ELF), "ELF") == 0);
	CHECK(strcmp(UIApps_TypeLabel(UI_APPS_TYPE_NONE), "") == 0);
}

static const uiApp_t *find(const uiApp_t *list, size_t count, const char *name)
{
	size_t i;

	for(i = 0u; i < count; ++i) {
		if(strcmp(list[i].name, name) == 0) return &list[i];
	}
	return NULL;
}

static void testTopFolder(void)
{
	/* /apps itself: loose programs, each with its own picture or none. */
	uiAppsEntry_t entries[] = {
		FILE_ENTRY("gbi.dol"), FILE_ENTRY("gbi.png"),
		FILE_ENTRY("GBIHF.DOL"), FILE_ENTRY("gbihf.PNG"),
		FILE_ENTRY("igr.dol"), FILE_ENTRY("igr.cli"), FILE_ENTRY("readme.txt"),
		FILE_ENTRY("._gbi.dol"), {"secret.dol", false, true, 5u, NULL},
		FILE_ENTRY("icon.png"), FOLDER_ENTRY("emulators"),
		FOLDER_ENTRY("tools.dol")
	};
	uiApp_t list[8];
	size_t count = 0u;
	const uiApp_t *app;

	entries[0].size = 123456u;
	entries[1].size = 2048u;
	/* The device's own entries travel with the app. */
	entries[0].handle = &entries[0];
	entries[1].handle = &entries[1];
	CHECK(UIApps_AddFolder(list, &count, 8u, "", entries,
		sizeof(entries) / sizeof(entries[0])));
	CHECK(count == 3u);
	app = find(list, count, "gbi");
	CHECK(app != NULL);
	CHECK(strcmp(app->program, "gbi.dol") == 0);
	CHECK(strcmp(app->picture, "gbi.png") == 0);
	CHECK(app->size == 123456u && app->pictureSize == 2048u);
	CHECK(app->type == UI_APPS_TYPE_DOL && !app->pictureFailed);
	CHECK(app->programHandle == &entries[0] && app->pictureHandle == &entries[1]);
	app = find(list, count, "GBIHF");
	CHECK(app != NULL && strcmp(app->picture, "gbihf.PNG") == 0);
	/* No picture of its own, and /apps' icon.png is nobody's. */
	app = find(list, count, "igr");
	CHECK(app != NULL && app->picture[0] == '\0' && app->pictureSize == 0u);
	CHECK(app->pictureHandle == NULL);
	CHECK(find(list, count, "secret") == NULL);
	CHECK(find(list, count, "tools") == NULL);
}

static void testAppFolder(void)
{
	/* A folder of /apps, the Homebrew Channel's way: boot.dol is the Wii's
	 * program, icon.png the folder's picture. */
	uiAppsEntry_t entries[] = {
		FILE_ENTRY("boot.dol"), FILE_ENTRY("meta.xml"), FILE_ENTRY("icon.png"),
		FILE_ENTRY("gbi.dol"), FILE_ENTRY("gbisr.dol"), FILE_ENTRY("GBISR.png"),
		FILE_ENTRY("gbihf.elf")
	};
	uiApp_t list[8];
	size_t count = 1u;
	const uiApp_t *app;

	memset(list, 0, sizeof(list));
	strcpy(list[0].name, "already here");
	CHECK(UIApps_AddFolder(list, &count, 8u, "Game Boy Interface", entries,
		sizeof(entries) / sizeof(entries[0])));
	CHECK(count == 4u);
	CHECK(strcmp(list[0].name, "already here") == 0);
	CHECK(find(list, count, "boot") == NULL);
	app = find(list, count, "gbi");
	CHECK(app != NULL);
	CHECK(strcmp(app->program, "Game Boy Interface/gbi.dol") == 0);
	CHECK(strcmp(app->picture, "Game Boy Interface/icon.png") == 0);
	app = find(list, count, "gbisr");
	CHECK(app != NULL && strcmp(app->picture, "Game Boy Interface/GBISR.png") == 0);
	app = find(list, count, "gbihf");
	CHECK(app != NULL && app->type == UI_APPS_TYPE_ELF);
	CHECK(strcmp(app->picture, "Game Boy Interface/icon.png") == 0);

	/* A Wii-only folder adds nothing. */
	{
		uiAppsEntry_t wii[] = {
			FILE_ENTRY("boot.dol"), FILE_ENTRY("meta.xml"), FILE_ENTRY("icon.png")
		};
		size_t before = count;

		CHECK(UIApps_AddFolder(list, &count, 8u, "wiiapp", wii, 3u));
		CHECK(count == before);
	}
}

static void testDolphinDepotLayout(void)
{
	/* Dolphin Depot standard: /apps/<slug>/default.dol + opening.bnr, plus
	 * optional variant .dol files and PNG override precedence. */
	uiAppsEntry_t cleanrip[] = {
		FILE_ENTRY("default.dol"), FILE_ENTRY("opening.bnr")
	};
	uiAppsEntry_t customPng[] = {
		FILE_ENTRY("default.dol"), FILE_ENTRY("opening.bnr"),
		FILE_ENTRY("icon.png"), FILE_ENTRY("gbihf.dol")
	};
	uiAppsEntry_t hiddenBnr[] = {
		FILE_ENTRY("default.dol"), {"opening.bnr", false, true, 6496u, NULL}
	};
	uiApp_t list[8];
	size_t count = 0u;
	const uiApp_t *app;

	cleanrip[0].handle = &cleanrip[0];
	cleanrip[1].handle = &cleanrip[1];
	cleanrip[1].size = 6496u;
	CHECK(UIApps_AddFolder(list, &count, 8u, "cleanrip", cleanrip, 2u));
	CHECK(count == 1u);
	app = &list[0];
	CHECK(app->isDefaultDol);
	/* Before opening.bnr is parsed, default.dol takes its folder slug, never "default". */
	CHECK(strcmp(app->name, "cleanrip") == 0);
	CHECK(strcmp(app->program, "cleanrip/default.dol") == 0);
	CHECK(strcmp(app->banner, "cleanrip/opening.bnr") == 0);
	CHECK(app->bannerSize == 6496u && app->bannerHandle == &cleanrip[1]);
	CHECK(strcmp(app->picture, "cleanrip/opening.bnr") == 0);
	CHECK(app->pictureIsBnr && app->pictureHandle == &cleanrip[1]);
	UIApps_ApplyBanner(&list[0], "CleanRip", "emu_kidid");
	CHECK(strcmp(list[0].name, "CleanRip") == 0);
	CHECK(strcmp(list[0].author, "emu_kidid") == 0);
	/* Empty title preserves the folder slug fallback. */
	UIApps_ApplyBanner(&list[0], "", NULL);
	CHECK(strcmp(list[0].name, "CleanRip") == 0);

	/* icon.png beats opening.bnr for poster art, while opening.bnr stays for metadata. */
	customPng[1].handle = &customPng[1];
	customPng[1].size = 6496u;
	customPng[2].handle = &customPng[2];
	customPng[2].size = 1024u;
	CHECK(UIApps_AddFolder(list, &count, 8u, "gbi", customPng, 4u));
	CHECK(count == 3u);
	app = find(list, count, "gbi");
	CHECK(app != NULL && app->isDefaultDol);
	CHECK(!app->pictureIsBnr && strcmp(app->picture, "gbi/icon.png") == 0);
	CHECK(strcmp(app->banner, "gbi/opening.bnr") == 0);
	/* Variant .dol keeps its own descriptive name. */
	app = find(list, count, "gbihf");
	CHECK(app != NULL && !app->isDefaultDol);
	CHECK(strcmp(app->program, "gbi/gbihf.dol") == 0);
	CHECK(strcmp(app->picture, "gbi/icon.png") == 0);
	CHECK(app->banner[0] == '\0');

	/* Hidden opening.bnr is ignored; default.dol still falls back to folder slug. */
	CHECK(UIApps_AddFolder(list, &count, 8u, "hidden-app", hiddenBnr, 2u));
	app = find(list, count, "hidden-app");
	CHECK(app != NULL && app->isDefaultDol);
	CHECK(app->banner[0] == '\0' && app->picture[0] == '\0');
}

static void testLimits(void)
{
	uiAppsEntry_t entries[] = {
		FILE_ENTRY("a.dol"), FILE_ENTRY("b.dol"), FILE_ENTRY("c.dol")
	};
	char folder[300];
	char longName[120];
	uiAppsEntry_t named[1];
	uiApp_t list[4];
	size_t count = 0u;

	/* Full: the rest are left out, and it says so. */
	CHECK(!UIApps_AddFolder(list, &count, 2u, "", entries, 3u));
	CHECK(count == 2u);
	/* A path that won't fit is left out; a picture path that won't fit is
	 * dropped, not cut short. */
	memset(folder, 'f', sizeof(folder));
	folder[sizeof(folder) - 1u] = '\0';
	count = 0u;
	CHECK(UIApps_AddFolder(list, &count, 4u, folder, entries, 1u));
	CHECK(count == 0u);
	memset(folder, 'f', 245u);
	folder[245] = '\0';
	{
		uiAppsEntry_t tight[] = {FILE_ENTRY("a.dol"), FILE_ENTRY("a.png"),
			FILE_ENTRY("abcdefgh.dol"), FILE_ENTRY("abcdefgh.png")};

		CHECK(UIApps_AddFolder(list, &count, 4u, folder, tight, 4u));
		CHECK(count == 1u);
		CHECK(strcmp(list[0].name, "a") == 0);
		CHECK(strlen(list[0].program) == 245u + 6u);
		CHECK(strlen(list[0].picture) == 245u + 6u);
	}
	/* A long name is cut to fit what the card can show. */
	memset(longName, 'n', sizeof(longName));
	strcpy(longName + 100, ".dol");
	named[0] = (uiAppsEntry_t)FILE_ENTRY(longName);
	count = 0u;
	CHECK(UIApps_AddFolder(list, &count, 4u, "", named, 1u));
	CHECK(count == 1u);
	CHECK(strlen(list[0].name) == UI_APPS_NAME_LENGTH - 1u);
	CHECK(strlen(list[0].program) == 104u);
	/* Nothing to add is fine, and bad arguments add nothing. */
	CHECK(UIApps_AddFolder(list, &count, 4u, "", NULL, 0u));
	CHECK(UIApps_AddFolder(NULL, &count, 4u, "", entries, 3u));
	CHECK(count == 1u);
}

static void testSort(void)
{
	uiApp_t list[4];

	memset(list, 0, sizeof(list));
	strcpy(list[0].name, "snes9x");
	strcpy(list[0].program, "snes9x.dol");
	strcpy(list[1].name, "GBI");
	strcpy(list[1].program, "z/GBI.dol");
	strcpy(list[2].name, "gbi");
	strcpy(list[2].program, "gbi.dol");
	strcpy(list[3].name, "Genesis");
	strcpy(list[3].program, "genesis.dol");
	UIApps_Sort(list, 4u);
	CHECK(strcmp(list[0].program, "gbi.dol") == 0);
	CHECK(strcmp(list[1].program, "z/GBI.dol") == 0);
	CHECK(strcmp(list[2].name, "Genesis") == 0);
	CHECK(strcmp(list[3].name, "snes9x") == 0);
	UIApps_Sort(NULL, 4u);
}

static int countState(const uiAppsArt_t *art, uiAppsArtState_t state)
{
	int n = 0;
	uint32_t i;

	for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) {
		n += art->slots[i].state == state;
	}
	return n;
}

/* The poster thread makes one poster at a time while the menu thread moves
 * the window: a poster finished after its app scrolled away, its slot now
 * waiting for another app, is dropped, and the other app gets its own. */
static void testArtLatePoster(void)
{
	uiAppsArt_t art;
	int32_t window[1];
	uint32_t now = 1000u;
	int slot, again;

	UIAppsArt_Init(&art, now);
	window[0] = 1;
	UIAppsArt_Want(&art, window, 1u, now);
	slot = UIAppsArt_Next(&art, now);
	CHECK(slot >= 0 && art.slots[slot].app == 1);
	window[0] = 2;
	UIAppsArt_Want(&art, window, 1u, now);
	again = UIAppsArt_Next(&art, now);
	CHECK(again >= 0 && art.slots[again].app == 2);
	CHECK(!UIAppsArt_Done(&art, slot, 1, true));
	CHECK(UIAppsArt_Find(&art, 1) == -1);
	CHECK(UIAppsArt_Find(&art, 2) == -1);
	CHECK(art.slots[again].state == UI_APPS_ART_WANTED);
	CHECK(UIAppsArt_Done(&art, again, 2, true));
	CHECK(UIAppsArt_Find(&art, 2) == again);
}

static void testArtSlots(void)
{
	uiAppsArt_t art;
	int32_t window[UI_APPS_ART_SLOTS + 3u];
	uint32_t now = 0xFFFFFF00u;	/* the clock wraps during the test */
	int slot, first;
	uint32_t i;

	UIAppsArt_Init(&art, now);
	CHECK(UIAppsArt_Next(&art, now) == -1);
	CHECK(UIAppsArt_Find(&art, 0) == -1);
	/* Nearest first, repeats and negatives ignored. */
	window[0] = 7; window[1] = 3; window[2] = 7; window[3] = -1; window[4] = 9;
	UIAppsArt_Want(&art, window, 5u, now);
	CHECK(art.windowCount == 3u);
	CHECK(countState(&art, UI_APPS_ART_WANTED) == 3);
	/* A slot never drawn needs no wait. */
	first = UIAppsArt_Next(&art, now);
	CHECK(first >= 0 && art.slots[first].app == 7);
	CHECK(UIAppsArt_Find(&art, 7) == -1);	/* not drawn until filled */
	CHECK(UIAppsArt_Done(&art, first, 7, true));
	CHECK(UIAppsArt_Find(&art, 7) == first);
	slot = UIAppsArt_Next(&art, now);
	CHECK(slot >= 0 && art.slots[slot].app == 3);
	CHECK(UIAppsArt_Done(&art, slot, 3, false));
	CHECK(UIAppsArt_Find(&art, 3) == -1);
	CHECK(art.slots[slot].state == UI_APPS_ART_NONE);
	/* Done twice, or on a slot that isn't waiting, changes nothing. */
	CHECK(!UIAppsArt_Done(&art, slot, 3, true));
	CHECK(art.slots[slot].state == UI_APPS_ART_NONE);
	CHECK(!UIAppsArt_Done(&art, -1, 3, true));
	CHECK(!UIAppsArt_Done(&art, (int)UI_APPS_ART_SLOTS, 3, true));
	slot = UIAppsArt_Next(&art, now);
	CHECK(slot >= 0 && art.slots[slot].app == 9);
	CHECK(!UIAppsArt_Done(&art, slot, 7, true));	/* another app's */
	CHECK(UIAppsArt_Done(&art, slot, 9, true));
	CHECK(UIAppsArt_Next(&art, now) == -1);

	/* The window moves: 7 is let go; its texels may be in a frame being
	 * drawn, so its slot waits before holding another app's poster. 9 keeps
	 * its slot and its poster. */
	now += 5u;
	window[0] = 9; window[1] = 11;
	UIAppsArt_Want(&art, window, 2u, now);
	CHECK(UIAppsArt_Find(&art, 7) == -1);
	CHECK(art.slots[first].state == UI_APPS_ART_EMPTY);
	CHECK(art.slots[first].freedMs == now);
	CHECK(UIAppsArt_Find(&art, 9) >= 0);
	slot = UIAppsArt_Next(&art, now);
	/* 11 takes a slot never drawn rather than 7's. */
	CHECK(slot >= 0 && slot != first && art.slots[slot].app == 11);

	/* Every slot drawn, then a whole new window: nothing is filled until
	 * the quarantine is over, across the clock's wrap. */
	UIAppsArt_Init(&art, now);
	for(i = 0u; i < UI_APPS_ART_SLOTS; ++i) window[i] = (int32_t)i;
	UIAppsArt_Want(&art, window, UI_APPS_ART_SLOTS, now);
	while((slot = UIAppsArt_Next(&art, now)) >= 0) {
		CHECK(UIAppsArt_Done(&art, slot, art.slots[slot].app, true));
	}
	CHECK(countState(&art, UI_APPS_ART_READY) == (int)UI_APPS_ART_SLOTS);
	for(i = 0u; i < UI_APPS_ART_SLOTS + 3u; ++i) window[i] = (int32_t)(100u + i);
	UIAppsArt_Want(&art, window, UI_APPS_ART_SLOTS + 3u, now);
	CHECK(art.windowCount == UI_APPS_ART_SLOTS);
	CHECK(countState(&art, UI_APPS_ART_WANTED) == (int)UI_APPS_ART_SLOTS);
	CHECK(UIAppsArt_Next(&art, now) == -1);
	CHECK(UIAppsArt_Next(&art, now + UI_APPS_ART_QUARANTINE_MS - 1u) == -1);
	slot = UIAppsArt_Next(&art, now + UI_APPS_ART_QUARANTINE_MS);
	CHECK(slot >= 0 && art.slots[slot].app == 100);
	/* Wanting nothing lets everything go. */
	UIAppsArt_Want(&art, NULL, 0u, now);
	CHECK(countState(&art, UI_APPS_ART_EMPTY) == (int)UI_APPS_ART_SLOTS);
	UIAppsArt_Init(NULL, now);
	UIAppsArt_Want(NULL, window, 1u, now);
	CHECK(UIAppsArt_Next(NULL, now) == -1);
	CHECK(UIAppsArt_Find(NULL, 1) == -1);
}

int main(void)
{
	testProgramTypes();
	testTopFolder();
	testAppFolder();
	testDolphinDepotLayout();
	testLimits();
	testSort();
	testArtSlots();
	testArtLatePoster();
	printf("test_ui_apps: %u checks passed\n", checks);
	return 0;
}
