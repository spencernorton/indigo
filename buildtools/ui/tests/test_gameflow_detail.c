/*
 * Host test command (run from repository root):
 * cc -std=c11 -Wall -Wextra -Werror -Wconversion \
 *   -Wsign-conversion -pedantic -Icube/swiss/source/gui \
 *   buildtools/ui/tests/test_gameflow_detail.c \
 *   cube/swiss/source/gui/ui_gameflow_detail.c \
 *   cube/swiss/source/gui/ui_game_history.c \
 *   cube/swiss/source/gui/ui_saves_metadata.c \
 *   cube/swiss/source/gui/ui_saves.c \
 *   -o /tmp/test_gameflow_detail && /tmp/test_gameflow_detail
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_gameflow_detail.h"

#define CHECK(condition) do { \
	if(!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#condition); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

static char *copyString(const char *source)
{
	size_t length = strlen(source) + 1u;
	char *copy = malloc(length);

	CHECK(copy != NULL);
	memcpy(copy, source, length);
	return copy;
}

static uiGameflowDetailSnapshot_t buildSnapshot(void)
{
	uint8_t *banner = malloc(UI_GAMEFLOW_DETAIL_BANNER_BYTES);
	char *title = copyString("Super Mario Sunshine");
	char *company = copyString("Nintendo");
	char *description = copyString("Clean the island and recover the Shine Sprites "
		"in a bright platform adventure with FLUDD.\nSecond paragraph.");
	char *firstName = copyString("Infinite Water");
	char *secondName = copyString("Moon Jump");
	uiGameflowDetailCheatSource_t cheats[] = {
		{firstName, true}, {"All Shines", false}, {secondName, true},
		{"No Damage", true}
	};
	uiGameflowDetailSource_t source = {
		.generation = 77u,
		.focusIndex = 4u,
		.gameId = "GMSE01",
		.title = title,
		.company = company,
		.facts = "GMSE01  |  1.35 GB  |  DISC 1",
		.description = description,
		.playHistoryAvailable = true,
		.lastPlayedUnixSeconds = UI_GAME_HISTORY_MIN_TIME + 45000u,
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.banner = banner,
		.bannerSize = UI_GAMEFLOW_DETAIL_BANNER_BYTES,
		.cheats = cheats,
		.cheatCount = sizeof(cheats) / sizeof(cheats[0]),
		.enabledCheatBytes = 96u,
		.cheatCapacityBytes = 2048u,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
			UI_GAMEFLOW_DETAIL_CAN_SETTINGS |
			UI_GAMEFLOW_DETAIL_CAN_CHEATS |
			UI_GAMEFLOW_DETAIL_CAN_LIBRARY |
			UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD |
			UI_GAMEFLOW_DETAIL_IS_AUTOLOAD |
			UI_GAMEFLOW_DETAIL_CAN_VERIFY |
			UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT |
			UI_GAMEFLOW_DETAIL_AUDIO_STREAMING |
			UI_GAMEFLOW_DETAIL_HAS_DISC_TWO
	};
	uiGameflowDetailSnapshot_t snapshot;

	CHECK(banner != NULL && title != NULL && company != NULL &&
		description != NULL && firstName != NULL && secondName != NULL);
	memset(banner, 0x5a, UI_GAMEFLOW_DETAIL_BANNER_BYTES);
	CHECK(UIGameflowDetail_Build(&snapshot, &source));

	/* Destroy every borrowed source before examining the retained copy. */
	memset(banner, 0, UI_GAMEFLOW_DETAIL_BANNER_BYTES);
	free(banner);
	free(title);
	free(company);
	free(description);
	free(firstName);
	free(secondName);
	return snapshot;
}

static void testPointerFreeCopyAndMatch(void)
{
	uiGameflowDetailSnapshot_t snapshot = buildSnapshot();
	size_t i;

	CHECK(strcmp(snapshot.title, "Super Mario Sunshine") == 0);
	CHECK(strcmp(snapshot.company, "Nintendo") == 0);
	CHECK(strstr(snapshot.lastPlayedText, "2001") != NULL);
	CHECK(strcmp(snapshot.saveStatusText, "Check in game") == 0);
	CHECK(strcmp(snapshot.enabledCheatNames[0], "Infinite Water") == 0);
	CHECK(strcmp(snapshot.enabledCheatNames[1], "Moon Jump") == 0);
	CHECK(strcmp(snapshot.enabledCheatNames[2], "No Damage") == 0);
	CHECK(snapshot.cheatCount == 4u);
	CHECK(snapshot.enabledCheatCount == 3u);
	CHECK(snapshot.enabledCheatBytes == 96u);
	CHECK(snapshot.cheatCapacityBytes == 2048u);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_BANNER) != 0u);
	CHECK(((uintptr_t)snapshot.banner & 31u) == 0u);
	for(i = 0u; i < UI_GAMEFLOW_DETAIL_BANNER_BYTES; ++i) {
		CHECK(snapshot.banner[i] == 0x5au);
	}
	CHECK(snapshot.description[0][0] != '\0');
	CHECK(strcmp(snapshot.statusText,
		"AUDIO STREAMING   DISC 2 READY   AUTOLOAD") == 0);
	CHECK(strcmp(snapshot.cheatSummary,
		"3 of 4 enabled") == 0);
	CHECK(strcmp(snapshot.cheatPreview, "Infinite Water  +2 MORE") == 0);
	CHECK(strcmp(snapshot.launchLabel, "LAUNCH GAME") == 0);
	CHECK(strcmp(snapshot.primaryActions,
		"D-PAD  MOVE   A  SELECT   B  LIBRARY   X  SETTINGS   Y  CHEATS") == 0);
	CHECK(strcmp(snapshot.advancedLineOne,
		"Z  AUTOLOAD ON   R  VERIFY") == 0);
	CHECK(strcmp(snapshot.advancedLineTwo, "L+A  CLEAN BOOT") == 0);
	CHECK(strstr(snapshot.primaryActions, "AUTOLOAD") == NULL);
	CHECK(strstr(snapshot.primaryActions, "VERIFY") == NULL);
	CHECK(strstr(snapshot.primaryActions, "CLEAN") == NULL);
	CHECK(sizeof(snapshot) < 8192u);
	CHECK(UIGameflowDetail_Matches(&snapshot, 77u, 4u, "GMSE01", 6u));
	CHECK(!UIGameflowDetail_Matches(&snapshot, 78u, 4u, "GMSE01", 6u));
	CHECK(!UIGameflowDetail_Matches(&snapshot, 77u, 5u, "GMSE01", 6u));
	CHECK(!UIGameflowDetail_Matches(&snapshot, 77u, 4u, "GALE01", 6u));
	CHECK(!UIGameflowDetail_Matches(&snapshot, 77u, 4u, "GMSE0", 5u));
}

static void testNoCheatsClearsCapability(void)
{
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.flags = UI_GAMEFLOW_DETAIL_HAS_BANNER |
			UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
			UI_GAMEFLOW_DETAIL_CAN_CHEATS |
			UI_GAMEFLOW_DETAIL_CAN_SETTINGS
	};
	uiGameflowDetailSnapshot_t snapshot;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.lastPlayedText, "History unavailable") == 0);
	CHECK(strcmp(snapshot.saveStatusText, "Check in game") == 0);
	CHECK(snapshot.cheatCount == 0u);
	CHECK(snapshot.enabledCheatCount == 0u);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_CHEATS_KNOWN) != 0u);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_BANNER) == 0u);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_CAN_CHEATS) == 0u);
	CHECK(strcmp(snapshot.cheatSummary, "NO CHEATS FOUND") == 0);
	CHECK(snapshot.cheatPreview[0] == '\0');
	CHECK(strstr(snapshot.primaryActions, "Y  CHEATS") == NULL);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_Y) == UI_GAMEFLOW_DETAIL_ACTION_NONE);
}

static void testPresentationVariants(void)
{
	uiGameflowDetailCheatSource_t cheat = {"Invincibility", false};
	uiGameflowDetailSource_t source = {
		.gameId = "GM4E01",
		.title = "Mario Kart: Double Dash!!",
		.cheats = &cheat,
		.cheatCount = 1u,
		.enabledCheatBytes = UINT32_MAX,
		.cheatCapacityBytes = UINT32_MAX,
		.flags = UI_GAMEFLOW_DETAIL_CAN_SETTINGS |
			UI_GAMEFLOW_DETAIL_CAN_CHEATS |
			UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT |
			UI_GAMEFLOW_DETAIL_CLEAN_BOOT_DEFAULT
	};
	uiGameflowDetailSnapshot_t snapshot;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.cheatSummary, "STATUS UNAVAILABLE") == 0);
	CHECK(snapshot.cheatPreview[0] == '\0');
	CHECK(strcmp(snapshot.launchLabel, "CLEAN BOOT") == 0);
	CHECK(strcmp(snapshot.primaryActions,
		"D-PAD  MOVE   A  SELECT   X  SETTINGS   Y  CHEATS") == 0);
	CHECK(snapshot.advancedLineOne[0] == '\0');
	CHECK(strcmp(snapshot.advancedLineTwo, "L+A  CLEAN BOOT") == 0);

	source.flags |= UI_GAMEFLOW_DETAIL_CHEATS_KNOWN;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.cheatSummary,
		"0 of 1 enabled") == 0);
	CHECK(strcmp(snapshot.cheatPreview, "Y  Choose cheats") == 0);
}

static void testDescriptionTruncationIsVisible(void)
{
	/* A banner holds 128 characters at most; four long words are enough to
	 * need a fourth line. */
	const char *description =
		"Thirtycharacterswordnumberone. Thirtycharacterswordnumbertwo. "
		"Thirtycharacterswordnumberthre. Thirtycharacterswordnumberfour.";
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.description = description,
		.playHistoryAvailable = true,
		.lastPlayedUnixSeconds = UI_GAME_HISTORY_MIN_TIME + 45000u,
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN
	};
	uiGameflowDetailSnapshot_t snapshot;
	size_t length;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(snapshot.description[0][0] != '\0');
	CHECK(snapshot.description[1][0] != '\0');
	CHECK(snapshot.description[2][0] != '\0');
	length = strlen(snapshot.description[2]);
	CHECK(length >= 3u);
	CHECK(strcmp(&snapshot.description[2][length - 3u], "...") == 0);
}

/* A disc banner's description need not end in a NUL: the wrap stops at its
 * 128 characters instead of reading on into whatever follows it. */
static void testDescriptionReadsNoFurtherThanTheBanner(void)
{
	char banner[160];
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.description = banner,
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN
	};
	uiGameflowDetailSnapshot_t snapshot;
	size_t line;

	memset(banner, 'a', UI_GAMEFLOW_DETAIL_DESCRIPTION_SOURCE);
	memset(&banner[UI_GAMEFLOW_DETAIL_DESCRIPTION_SOURCE], 'Z',
		sizeof(banner) - UI_GAMEFLOW_DETAIL_DESCRIPTION_SOURCE - 1u);
	banner[sizeof(banner) - 1u] = '\0';
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(snapshot.description[0][0] == 'a');
	for(line = 0u; line < UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES; ++line) {
		CHECK(strchr(snapshot.description[line], 'Z') == NULL);
	}
}

static void testControllerPrecedence(void)
{
	uiGameflowDetailSnapshot_t snapshot = buildSnapshot();

	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_A) == UI_GAMEFLOW_DETAIL_ACTION_BOOT);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_A | UI_GAMEFLOW_DETAIL_INPUT_L) ==
		UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_A | UI_GAMEFLOW_DETAIL_INPUT_B |
		UI_GAMEFLOW_DETAIL_INPUT_Y) == UI_GAMEFLOW_DETAIL_ACTION_BOOT);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_B | UI_GAMEFLOW_DETAIL_INPUT_X) ==
		UI_GAMEFLOW_DETAIL_ACTION_LIBRARY);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_R | UI_GAMEFLOW_DETAIL_INPUT_X) ==
		UI_GAMEFLOW_DETAIL_ACTION_VERIFY);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_X | UI_GAMEFLOW_DETAIL_INPUT_Y) ==
		UI_GAMEFLOW_DETAIL_ACTION_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_Z | UI_GAMEFLOW_DETAIL_INPUT_Y) ==
		UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_Y) == UI_GAMEFLOW_DETAIL_ACTION_CHEATS);
	snapshot.flags &= ~(uint32_t)(UI_GAMEFLOW_DETAIL_CAN_LIBRARY |
		UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_B) == UI_GAMEFLOW_DETAIL_ACTION_NONE);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_A | UI_GAMEFLOW_DETAIL_INPUT_L) ==
		UI_GAMEFLOW_DETAIL_ACTION_BOOT);
}

/* Up and down walk the rows as they are drawn and stop at the ends, past
 * Cheats when the game has none. Plain A runs the focused row; L+A and every
 * shortcut do what they always did, whatever is focused. */
static void testFocus(void)
{
	const uiGameflowDetailFocus_t launch = UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH;
	const uiGameflowDetailFocus_t cheats = UI_GAMEFLOW_DETAIL_FOCUS_CHEATS;
	const uiGameflowDetailFocus_t settings = UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS;
	const uiGameflowDetailFocus_t rows[] = {launch, cheats, settings};
	const uint32_t up = UI_GAMEFLOW_DETAIL_INPUT_UP;
	const uint32_t down = UI_GAMEFLOW_DETAIL_INPUT_DOWN;
	const uint32_t a = UI_GAMEFLOW_DETAIL_INPUT_A;
	const struct {
		uint32_t input;
		uiGameflowDetailAction_t action;
	} shortcuts[] = {
		{a | UI_GAMEFLOW_DETAIL_INPUT_L, UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT},
		{UI_GAMEFLOW_DETAIL_INPUT_B, UI_GAMEFLOW_DETAIL_ACTION_LIBRARY},
		{UI_GAMEFLOW_DETAIL_INPUT_R, UI_GAMEFLOW_DETAIL_ACTION_VERIFY},
		{UI_GAMEFLOW_DETAIL_INPUT_X, UI_GAMEFLOW_DETAIL_ACTION_SETTINGS},
		{UI_GAMEFLOW_DETAIL_INPUT_Z, UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD},
		{UI_GAMEFLOW_DETAIL_INPUT_Y, UI_GAMEFLOW_DETAIL_ACTION_CHEATS}
	};
	uiGameflowDetailSnapshot_t snapshot = buildSnapshot();
	size_t row;
	size_t i;

	CHECK(UIGameflowDetail_MoveFocus(&snapshot, launch, up) == cheats);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, cheats, up) == settings);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, settings, up) == settings);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, settings, down) == cheats);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, cheats, down) == launch);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, launch, down) == launch);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, cheats, a |
		UI_GAMEFLOW_DETAIL_INPUT_X | UI_GAMEFLOW_DETAIL_INPUT_Y) == cheats);

	CHECK(UIGameflowDetail_ResolveAction(&snapshot, launch, a) ==
		UI_GAMEFLOW_DETAIL_ACTION_BOOT);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, cheats, a) ==
		UI_GAMEFLOW_DETAIL_ACTION_CHEATS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, settings, a) ==
		UI_GAMEFLOW_DETAIL_ACTION_SETTINGS);
	for(row = 0u; row < sizeof(rows) / sizeof(rows[0]); ++row) {
		CHECK(UIGameflowDetail_ResolveAction(&snapshot, rows[row], up) ==
			UI_GAMEFLOW_DETAIL_ACTION_NONE);
		CHECK(UIGameflowDetail_ResolveAction(&snapshot, rows[row], down) ==
			UI_GAMEFLOW_DETAIL_ACTION_NONE);
		for(i = 0u; i < sizeof(shortcuts) / sizeof(shortcuts[0]); ++i) {
			CHECK(UIGameflowDetail_ResolveAction(&snapshot, rows[row],
				shortcuts[i].input) == shortcuts[i].action);
		}
	}

	/* L is Clean Boot's modifier only where Clean Boot is offered. */
	snapshot.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT;
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, settings,
		a | UI_GAMEFLOW_DETAIL_INPUT_L) == UI_GAMEFLOW_DETAIL_ACTION_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, launch,
		a | UI_GAMEFLOW_DETAIL_INPUT_L) == UI_GAMEFLOW_DETAIL_ACTION_BOOT);

	/* No cheats: Cheats is passed both ways, and A never acts on it. */
	snapshot.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_CHEATS;
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, launch, up) == settings);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, settings, down) == launch);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, cheats, a) ==
		UI_GAMEFLOW_DETAIL_ACTION_NONE);
	/* Launch alone: nowhere to go. */
	snapshot.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_SETTINGS;
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, launch, up) == launch);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, settings, a) ==
		UI_GAMEFLOW_DETAIL_ACTION_NONE);

	memset(&snapshot, 0, sizeof(snapshot));
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, launch, up) == launch);
	CHECK(UIGameflowDetail_MoveFocus(NULL, cheats, down) == cheats);
}

static void testInvalidInputResetsSnapshot(void)
{
	uiGameflowDetailSnapshot_t snapshot;
	uiGameflowDetailSource_t source = {
		.gameId = "bad-id",
		.title = "Invalid"
	};

	memset(&snapshot, 0xff, sizeof(snapshot));
	CHECK(!UIGameflowDetail_Build(&snapshot, &source));
	CHECK(snapshot.flags == 0u);
	CHECK(snapshot.title[0] == '\0');
}

/* A game's own settings get their own line, like its cheats: how many and
 * the first, or how to set some. The X action stays plain, so the count
 * isn't said twice. */
static void testCustomSettingsLine(void)
{
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.facts = "GALE01",
		.description = "",
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
			UI_GAMEFLOW_DETAIL_CAN_SETTINGS | UI_GAMEFLOW_DETAIL_CAN_LIBRARY,
		.customSettings = 2u,
		.firstCustomSetting = "Force Video Mode: 480p"
	};
	uiGameflowDetailSnapshot_t snapshot;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.primaryActions,
		"D-PAD  MOVE   A  SELECT   B  LIBRARY   X  SETTINGS") == 0);
	CHECK(strcmp(snapshot.settingsSummary, "2 custom") == 0);
	CHECK(strcmp(snapshot.settingsPreview,
		"Force Video Mode: 480p  +1 MORE") == 0);
	source.customSettings = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.settingsSummary, "1 custom") == 0);
	CHECK(strcmp(snapshot.settingsPreview, "Force Video Mode: 480p") == 0);
	/* The name comes from settings.c; without one the line still reads. */
	source.firstCustomSetting = NULL;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.settingsPreview, "Custom settings") == 0);
	source.customSettings = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.primaryActions,
		"D-PAD  MOVE   A  SELECT   B  LIBRARY   X  SETTINGS") == 0);
	CHECK(strcmp(snapshot.settingsSummary, "Game Defaults") == 0);
	CHECK(strcmp(snapshot.settingsPreview, "X  Change for this game") == 0);
	/* Without the X action there is no settings line to offer. */
	source.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_SETTINGS;
	source.customSettings = 3u;
	source.firstCustomSetting = "Force Video Mode: 480p";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strstr(snapshot.primaryActions, "SETTINGS") == NULL);
	CHECK(snapshot.settingsSummary[0] == '\0');
	CHECK(snapshot.settingsPreview[0] == '\0');
}

/* Opened from the File Browser, B says it goes back there; nothing else
 * on the screen changes, and B still does what it did. */
static void testBackToFiles(void)
{
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.facts = "GALE01",
		.description = "",
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
			UI_GAMEFLOW_DETAIL_CAN_SETTINGS | UI_GAMEFLOW_DETAIL_CAN_LIBRARY
	};
	uiGameflowDetailSnapshot_t library, files;

	CHECK(UIGameflowDetail_Build(&library, &source));
	source.flags |= UI_GAMEFLOW_DETAIL_BACK_FILES;
	CHECK(UIGameflowDetail_Build(&files, &source));
	CHECK(strcmp(library.primaryActions,
		"D-PAD  MOVE   A  SELECT   B  LIBRARY   X  SETTINGS") == 0);
	CHECK(strcmp(files.primaryActions,
		"D-PAD  MOVE   A  SELECT   B  BACK   X  SETTINGS") == 0);
	CHECK(UIGameflowDetail_ResolveAction(&files, UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_B) == UI_GAMEFLOW_DETAIL_ACTION_LIBRARY);
	/* Every other byte is the Library's. */
	memcpy(files.primaryActions, library.primaryActions, sizeof(files.primaryActions));
	files.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_BACK_FILES;
	CHECK(memcmp(&files, &library, sizeof(files)) == 0);
	/* Without a way back, no B either way. */
	source.flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_LIBRARY;
	CHECK(UIGameflowDetail_Build(&files, &source));
	CHECK(strstr(files.primaryActions, "B  ") == NULL);
}

static void testReadOnlySaveCopies(void)
{
	uiSavesGameStats_t stats = {
		.saves = 1u, .files = 1u, .blocks = 2u, .latestUpdated = 762525240u,
		.sourceSaves = {0u, 0u, 1u}, .checkedSources = 4u,
		.updatedKnown = true
	};
	uiGameflowDetailSource_t source = {
		.gameId = "GACZ01", .title = "Astral Circuit", .saveStats = &stats,
		.flags = UI_GAMEFLOW_DETAIL_CAN_SETTINGS | UI_GAMEFLOW_DETAIL_HAS_SAVES
	};
	uiGameflowDetailSnapshot_t snapshot;

	/* One copy shows no SAVES inset, whatever the caller's flags say. */
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	CHECK(snapshot.savesSummary[0] == '\0');
	CHECK(snapshot.savesUpdated[0] == '\0');
	CHECK(snapshot.saveStats.saves == 0u);
	/* Two copies of one file do: copies, the file's blocks once. */
	stats.saves = 2u;
	stats.sourceSaves[2] = 2u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u);
	CHECK(strcmp(snapshot.savesSummary, "2 copies | 2 blocks") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Updated 2024-02-29 12:34") == 0);
	CHECK(snapshot.saveStats.sourceSaves[2] == 2u);
	/* Retained statistics and labels survive mutation of the menu source.
	 * Different files of the game are files, not copies. */
	stats.saves = 3u;
	stats.files = 3u;
	stats.blocks = 6u;
	stats.sourceSaves[2] = 3u;
	CHECK(snapshot.saveStats.saves == 2u);
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "3 files | 6 blocks") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Updated 2024-02-29 12:34") == 0);
	/* Files and copies both, when some file is in more than one place. */
	stats.saves = 5u;
	stats.files = 2u;
	stats.blocks = 58u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "2 files, 5 copies | 58 blocks") == 0);
	/* A count of files the caller left out reads as files. */
	stats.files = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "5 files | 58 blocks") == 0);
	stats.saves = 3u;
	stats.files = 3u;
	stats.blocks = 6u;
	/* The newest copy's own line follows the date. */
	source.saveLine = "Last saved on 1/6";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated, "Updated 2024-02-29 12:34 | Last saved on 1/6") == 0);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS, UI_GAMEFLOW_DETAIL_INPUT_UP) ==
		UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot,
		UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS, UI_GAMEFLOW_DETAIL_INPUT_A) ==
		UI_GAMEFLOW_DETAIL_ACTION_SETTINGS);

	stats.partial = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated,
		"Partial scan | Updated 2024-02-29 12:34 | Last saved on 1/6") == 0);
	stats.updatedKnown = false;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated,
		"Partial scan | Update date unavailable | Last saved on 1/6") == 0);
	source.saveLine = "";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated, "Partial scan | Update date unavailable") == 0);
	stats.partial = false;
	stats.checkedSources = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated, "Partial scan | Update date unavailable") == 0);
	/* No copies, partial or complete, and no scan at all: no inset. */
	stats.saves = 0u;
	stats.blocks = 0u;
	stats.partial = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	stats.partial = false;
	stats.checkedSources = 7u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	source.saveStats = NULL;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	CHECK(snapshot.savesSummary[0] == '\0');
	CHECK(snapshot.saveStats.saves == 0u);
	CHECK(snapshot.saveStats.checkedSources == 0u);

	source.saveStats = &stats;
	stats.saves = UINT32_MAX;
	stats.files = UINT32_MAX;
	stats.blocks = UINT32_MAX;
	stats.updatedKnown = true;
	stats.latestUpdated = UINT32_MAX;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary,
		"4294967295 files | 4294967295 blocks") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Update date unavailable") == 0);
	stats.latestUpdated = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated, "Update date unavailable") == 0);
}

/* A memory card changing to this game's card, or given up on: SAVES says
 * so whatever the count, and offers no choice while it changes. */
static void testSavesCardStates(void)
{
	uiSavesGameStats_t stats = {
		.saves = 0u, .files = 1u, .checkedSources = 4u
	};
	uiGameflowDetailSource_t source = {
		.gameId = "GACZ01", .title = "Astral Circuit", .saveStats = &stats,
		.flags = UI_GAMEFLOW_DETAIL_CAN_SETTINGS, .savesWaiting = true
	};
	uiGameflowDetailSnapshot_t snapshot;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u);
	CHECK(strcmp(snapshot.savesSummary, "Memory card loading") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Reading it when it's ready") == 0);
	stats.saves = 1u;
	stats.blocks = 2u;
	source.saveCopies = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Memory card loading") == 0);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
	/* Two or more: the totals over the loading line, still no choice. */
	stats.saves = 2u;
	stats.blocks = 4u;
	source.saveCopies = 2u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "2 copies | 4 blocks") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Memory card loading") == 0);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
	/* Given up on: said with fewer than two; with more, a partial scan. */
	source.savesWaiting = false;
	source.savesCardFailed = true;
	stats.partial = true;
	stats.saves = 0u;
	stats.blocks = 0u;
	source.saveCopies = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u);
	CHECK(strcmp(snapshot.savesSummary, "Memory card didn't load") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Its saves weren't read") == 0);
	stats.saves = 2u;
	stats.blocks = 4u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "2 copies | 4 blocks") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Partial scan | Update date unavailable") == 0);
	/* Neither, with fewer than two (a partial Save Folder scan too): none. */
	source.savesCardFailed = false;
	stats.saves = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	/* No scan at all (Saves on Details off): none, waiting or not. */
	source.saveStats = NULL;
	source.savesWaiting = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
}

/* Always shows settled zero/one-copy scans without changing the scan or
 * offering a save choice; card loading/failure and Off keep their meaning. */
static void testAlwaysShowSaves(void)
{
	uiSavesGameStats_t stats = {
		.saves = 0u, .checkedSources = 4u
	};
	uiGameflowDetailSource_t source = {
		.gameId = "GACZ01", .title = "Astral Circuit", .saveStats = &stats,
		.savesAlways = true, .flags = UI_GAMEFLOW_DETAIL_CAN_SETTINGS
	};
	uiGameflowDetailSnapshot_t snapshot, normal;

	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u);
	CHECK(strcmp(snapshot.savesSummary, "No save copies found") == 0);
	CHECK(snapshot.savesUpdated[0] == '\0');
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
	/* An incomplete scan must not claim there are no saves. */
	stats.partial = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Save scan incomplete") == 0);
	stats.partial = false;
	stats.checkedSources = 0u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Save scan incomplete") == 0);
	/* One save includes its blocks, date and the game's own line. */
	stats.checkedSources = 4u;
	stats.saves = 1u;
	stats.files = 1u;
	stats.blocks = 1u;
	stats.updatedKnown = true;
	stats.latestUpdated = 762525240u;
	source.saveLine = "Last saved on 2/29";
	source.saveCopies = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "1 save | 1 block") == 0);
	CHECK(strcmp(snapshot.savesUpdated,
		"Updated 2024-02-29 12:34 | Last saved on 2/29") == 0);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
	stats.blocks = 2u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "1 save | 2 blocks") == 0);
	stats.partial = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strncmp(snapshot.savesUpdated, "Partial scan | ", 15u) == 0);
	stats.partial = false;
	/* Multiple copies are byte-identical to On, including copy selection. */
	stats.saves = 2u;
	source.saveCopies = 2u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	source.savesAlways = false;
	CHECK(UIGameflowDetail_Build(&normal, &source));
	CHECK(memcmp(&snapshot, &normal, sizeof(snapshot)) == 0);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) != 0u);
	/* Keep explicit card states ahead of any zero/one-copy result. */
	source.savesAlways = true;
	stats.saves = 0u;
	source.savesWaiting = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Memory card loading") == 0);
	source.savesWaiting = false;
	source.savesCardFailed = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Memory card didn't load") == 0);
	/* With no scan (Off), Always cannot make a box or a choice appear. */
	source.saveStats = NULL;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u);
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
}

static void testSaveCopyChoice(void)
{
	uiSavesGameStats_t stats = {
		.saves = 3u, .files = 1u, .blocks = 2u, .latestUpdated = 762525240u,
		.sourceSaves = {1u, 0u, 2u}, .checkedSources = 5u,
		.updatedKnown = true
	};
	uint8_t entry[UI_SAVES_ENTRY_SIZE] = {0};
	uiGameflowDetailSource_t source = {
		.gameId = "GACZ01", .title = "Astral Circuit", .saveStats = &stats,
		.saveCopies = 3u, .flags = UI_GAMEFLOW_DETAIL_SAVE_CHOICE
	};
	uiGameflowDetailSnapshot_t snapshot;

	/* 2024-02-29 12:34 in the entry's clock (seconds since 2000). */
	entry[0x28] = 0x2d; entry[0x29] = 0x73; entry[0x2a] = 0x36; entry[0x2b] = 0x38;
	/* Nothing chosen: the totals, and Left and Right can choose. */
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) != 0u);
	CHECK(strcmp(snapshot.savesSummary, "3 copies | 2 blocks") == 0);
	/* A copy chosen elsewhere goes on at launch; the card's own is in use. */
	source.saveChoice = 2u;
	source.saveChoiceEntry = entry;
	source.saveChoiceWhere = "Save Folder";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Copy 2 of 3 | Save Folder") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "Loads at launch | 2024-02-29 12:34") == 0);
	/* Its own line follows. */
	source.saveChoiceLine = "Last saved on 1/6";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated,
		"Loads at launch | 2024-02-29 12:34 | Last saved on 1/6") == 0);
	/* Copies of several files: the file, and its place among that file's
	 * copies, never a count across files. */
	source.saveChoiceFile = "DobutsunomoriP_MURA";
	source.saveChoiceFileCopy = 2u;
	source.saveChoiceFileCopies = 2u;
	source.saveChoiceWhere = "MemoryCardA.USA.raw";
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary,
		"DobutsunomoriP_MURA 2 of 2 | MemoryCardA.USA.raw") == 0);
	source.saveChoiceFile = "DobutsunomoriP_LT0";
	source.saveChoiceFileCopy = 1u;
	source.saveChoiceFileCopies = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "DobutsunomoriP_LT0 | MemoryCardA.USA.raw") == 0);
	source.saveChoiceFile = NULL;
	source.saveChoiceLine = NULL;
	source.saveChoiceWhere = "Save Folder";
	source.saveChoice = 1u;
	source.saveChoiceWhere = "Slot A";
	source.saveChoiceInUse = true;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "Copy 1 of 3 | Slot A") == 0);
	CHECK(strcmp(snapshot.savesUpdated, "In use | 2024-02-29 12:34") == 0);
	memset(entry + 0x28, 0, 4u);
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesUpdated, "In use | Date unavailable") == 0);
	/* A choice out of range shows the totals. */
	source.saveChoice = 4u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.savesSummary, "3 copies | 2 blocks") == 0);
	/* One copy, or no SAVES box: no choice, whatever the caller's flags. */
	source.saveCopies = 1u;
	source.saveChoice = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) == 0u);
	CHECK(strcmp(snapshot.savesSummary, "3 copies | 2 blocks") == 0);
	source.saveCopies = 3u;
	stats.saves = 1u;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & (UI_GAMEFLOW_DETAIL_SAVE_CHOICE |
		UI_GAMEFLOW_DETAIL_HAS_SAVES)) == 0u);
}

/* A game with a manual has a Manual row at the top: UP from Settings
 * reaches it and A opens the manual. Without one, or before Build says so,
 * the row isn't there. */
static void testManualRow(void)
{
	uiGameflowDetailSource_t source = {
		.gameId = "GALE01",
		.title = "Super Smash Bros. Melee",
		.facts = "GALE01",
		.description = "",
		.saveStatus = UI_GAME_SAVE_NOT_CHECKED,
		.flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
			UI_GAMEFLOW_DETAIL_CAN_SETTINGS | UI_GAMEFLOW_DETAIL_CAN_LIBRARY |
			UI_GAMEFLOW_DETAIL_HAS_MANUAL
	};
	uiGameflowDetailSnapshot_t snapshot;

	/* The flag is Build's alone: a source without pages has no row. */
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) == 0u);
	CHECK(snapshot.manualSummary[0] == '\0');
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS,
		UI_GAMEFLOW_DETAIL_INPUT_UP) == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_A) == UI_GAMEFLOW_DETAIL_ACTION_NONE);

	source.manualPages = 24;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u);
	CHECK(strcmp(snapshot.manualSummary, "24 PAGES") == 0);
	/* Launch, Settings (no cheats), Manual, and no further. */
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH,
		UI_GAMEFLOW_DETAIL_INPUT_UP) == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS,
		UI_GAMEFLOW_DETAIL_INPUT_UP) == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_UP) == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL);
	CHECK(UIGameflowDetail_MoveFocus(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_DOWN) == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_A) == UI_GAMEFLOW_DETAIL_ACTION_MANUAL);
	/* Its shortcuts stay theirs: X is Settings, wherever the focus is. */
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_X) == UI_GAMEFLOW_DETAIL_ACTION_SETTINGS);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_B) == UI_GAMEFLOW_DETAIL_ACTION_LIBRARY);
	source.manualPages = 1;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK(strcmp(snapshot.manualSummary, "1 PAGE") == 0);
	/* A file that can't be read keeps its row, and says so. */
	source.manualPages = -1;
	CHECK(UIGameflowDetail_Build(&snapshot, &source));
	CHECK((snapshot.flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u);
	CHECK(strcmp(snapshot.manualSummary, "CAN'T BE READ") == 0);
	CHECK(UIGameflowDetail_ResolveAction(&snapshot, UI_GAMEFLOW_DETAIL_FOCUS_MANUAL,
		UI_GAMEFLOW_DETAIL_INPUT_A) == UI_GAMEFLOW_DETAIL_ACTION_MANUAL);
}

int main(void)
{
	testReadOnlySaveCopies();
	testSavesCardStates();
	testAlwaysShowSaves();
	testSaveCopyChoice();
	testPointerFreeCopyAndMatch();
	testCustomSettingsLine();
	testNoCheatsClearsCapability();
	testPresentationVariants();
	testDescriptionTruncationIsVisible();
	testDescriptionReadsNoFurtherThanTheBanner();
	testControllerPrecedence();
	testFocus();
	testInvalidInputResetsSnapshot();
	testBackToFiles();
	testManualRow();
	puts("gameflow detail tests passed");
	return EXIT_SUCCESS;
}
