/*
 * Host test command (run from the repository root):
 * cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion \
 *   -pedantic -Icube/swiss/source/gui \
 *   buildtools/ui/tests/test_gameflow_library.c \
 *   cube/swiss/source/gui/ui_gameflow_library.c \
 *   -o /tmp/test_gameflow_library && /tmp/test_gameflow_library
 */

/* FNM_CASEFOLD and FNM_LEADING_DIR, as newlib has them, for the flatten
 * pattern's checks. */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fnmatch.h>

#include "ui_gameflow_library.h"
#include "ui_lang.h"

#define CHECK(condition) do { \
	if(!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#condition); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

static void testImageEligibility(void)
{
	CHECK(UIGameflowLibrary_IsGameImageName("game.iso"));
	CHECK(UIGameflowLibrary_IsGameImageName("Zelda.NKIT.ISO"));
	CHECK(UIGameflowLibrary_IsGameImageName("disc.GCM"));
	CHECK(UIGameflowLibrary_IsGameImageName("disc.fdi"));
	CHECK(UIGameflowLibrary_IsGameImageName("disc.tgc"));
	CHECK(!UIGameflowLibrary_IsGameImageName("iso"));
	CHECK(!UIGameflowLibrary_IsGameImageName("game.iso.zip"));
	CHECK(!UIGameflowLibrary_IsGameImageName("game.ciso"));
	CHECK(!UIGameflowLibrary_IsGameImageName("folder"));
	CHECK(!UIGameflowLibrary_IsGameImageName(NULL));

	CHECK(UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		0u, UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(!UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(!UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "game.iso"));
}

static void testStrictFolderEligibility(void)
{
	char gameId[7];
	char title[64];

	CHECK(UIGameflowLibrary_ParseGameFolderName(
		"Super Mario Sunshine [GMSE01]", gameId, title, sizeof(title)));
	CHECK(strcmp(gameId, "GMSE01") == 0);
	CHECK(strcmp(title, "Super Mario Sunshine") == 0);
	CHECK(UIGameflowLibrary_ParseGameFolderName(
		"Super Smash Bros. Melee [GALE01]", gameId, title, sizeof(title)));
	CHECK(strcmp(gameId, "GALE01") == 0);

	CHECK(!UIGameflowLibrary_ParseGameFolderName(
		"Super Mario Sunshine [gmse01]", NULL, NULL, 0u));
	CHECK(!UIGameflowLibrary_ParseGameFolderName(
		"Super Mario Sunshine [GMSE0-]", NULL, NULL, 0u));
	CHECK(!UIGameflowLibrary_ParseGameFolderName(
		"Super Mario Sunshine GMSE01", NULL, NULL, 0u));
	CHECK(!UIGameflowLibrary_ParseGameFolderName(
		"Super Mario Sunshine  [GMSE01]", NULL, NULL, 0u));
	CHECK(!UIGameflowLibrary_ParseGameFolderName(
		"games/Sunshine [GMSE01]", NULL, NULL, 0u));
	CHECK(!UIGameflowLibrary_ParseGameFolderName("[GMSE01]", NULL, NULL, 0u));

	CHECK(UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY,
		"Super Mario Sunshine [GMSE01]"));
	CHECK(!UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_FILE,
		"Super Mario Sunshine [GMSE01]"));
	CHECK(!UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
		1u, UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Random Folder"));
}

static void testRetainedDetailDispatch(void)
{
	CHECK(UIGameflowLibrary_UsesRetainedDetail(
		UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY));
	CHECK(!UIGameflowLibrary_UsesRetainedDetail(
		UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE));
	CHECK(UIGameflowLibrary_UsesRetainedDetail(
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE));
	CHECK(!UIGameflowLibrary_UsesRetainedDetail(
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY));
	CHECK(!UIGameflowLibrary_UsesRetainedDetail(
		UI_GAMEFLOW_LIBRARY_NONE,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE));
}

static void testArtworkFallbackPolicy(void)
{
	uiGameflowLibraryFallbackLayout_t layout;
	float bannerWidth;
	float bannerHeight;
	float aspectError;

	/* A valid real cover is never replaced by fallback decoration. A missing,
	 * corrupt, or still-loading cover follows the same deterministic ladder. */
	CHECK(UIGameflowLibrary_ChooseArtwork(true, true) ==
		UI_GAMEFLOW_LIBRARY_ART_POSTER);
	CHECK(UIGameflowLibrary_ChooseArtwork(true, false) ==
		UI_GAMEFLOW_LIBRARY_ART_POSTER);
	CHECK(UIGameflowLibrary_ChooseArtwork(false, true) ==
		UI_GAMEFLOW_LIBRARY_ART_BANNER);
	CHECK(UIGameflowLibrary_ChooseArtwork(false, false) ==
		UI_GAMEFLOW_LIBRARY_ART_EMBLEM);

	CHECK(UIGameflowLibrary_BuildFallbackLayout(0.0f, &layout));
	CHECK(layout.contentInset > 0.0f && layout.contentInset < 0.10f);
	CHECK(layout.bannerLeft > layout.contentInset);
	CHECK(layout.bannerLeft < layout.bannerRight);
	CHECK(layout.bannerRight < 1.0f - layout.contentInset);
	CHECK(layout.bannerTop > layout.contentInset);
	CHECK(layout.bannerTop < layout.bannerBottom);
	CHECK(layout.bannerBottom < layout.identityTop);
	CHECK(layout.identityTop < layout.idBaseline);
	CHECK(layout.idBaseline < layout.regionBaseline);
	CHECK(layout.regionBaseline < 1.0f - layout.contentInset);
	CHECK(layout.motifCenterX - layout.motifRadius > layout.contentInset);
	CHECK(layout.motifCenterX + layout.motifRadius <
		1.0f - layout.contentInset);
	CHECK(layout.identityAlpha == 1.0f);
	/* The authored 640x480 center pose is 180x240. Preserve the native 3:1
	 * BNR aspect instead of stretching it to fill the poster silhouette. */
	bannerWidth = (layout.bannerRight - layout.bannerLeft) * 180.0f;
	bannerHeight = (layout.bannerBottom - layout.bannerTop) * 240.0f;
	aspectError = bannerWidth / bannerHeight - 3.0f;
	if(aspectError < 0.0f) {
		aspectError = -aspectError;
	}
	CHECK(aspectError < 0.01f);

	CHECK(UIGameflowLibrary_BuildFallbackLayout(0.2f, &layout));
	CHECK(layout.identityAlpha > 0.0f && layout.identityAlpha < 1.0f);
	CHECK(UIGameflowLibrary_BuildFallbackLayout(-1.0f, &layout));
	CHECK(layout.identityAlpha == 0.0f);
	/* The row's second card either side shows its banner too. */
	CHECK(UIGameflowLibrary_BuildFallbackLayout(-2.0f, &layout));
	CHECK(layout.identityAlpha == 0.0f);
	CHECK(!UIGameflowLibrary_BuildFallbackLayout(3.0f, &layout));
	CHECK(!UIGameflowLibrary_BuildFallbackLayout(-3.0f, &layout));
	CHECK(!UIGameflowLibrary_BuildFallbackLayout(NAN, &layout));
	CHECK(!UIGameflowLibrary_BuildFallbackLayout(0.0f, NULL));

	CHECK(strcmp(UIGameflowLibrary_RegionLabel("GMSE01"), "NTSC-U") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel("GALE01"), "NTSC-U") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel("GZLJ01"), "NTSC-J") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel("GZLP01"), "PAL") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel("ABCA01"),
		"REGION FREE") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel("BAD"), "GAMECUBE") == 0);
	CHECK(strcmp(UIGameflowLibrary_RegionLabel(NULL), "GAMECUBE") == 0);
}

static void testProductionFlattenedUpgrade(void)
{
	/* The production migration fixture retains Swiss's default
	 * games-directory flattening. scanFiles consequently presents each
	 * folder's game.iso as IMAGE_FILES; that must select the Library
	 * (menu_loop's dispatch) and the retained Detail/dashboard. */
	uiGameflowLibraryClassifier_t classifier;
	uiGameflowLibraryLocation_t location = UIGameflowLibrary_Locate(
		"gcldr:/games", "gcldr:/games");
	uiGameflowLibraryMode_t scannedMode;

	CHECK(location == UI_GAMEFLOW_LIBRARY_LOCATION_ROOT);
	UIGameflowLibrary_ClassifierInit(&classifier, location);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	scannedMode = UIGameflowLibrary_ClassifierFinish(&classifier);

	CHECK(scannedMode == UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
	CHECK(UIGameflowLibrary_UsesRetainedDetail(scannedMode,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE));
}

static void testClassifierFallbackMatrix(void)
{
	uiGameflowLibraryClassifier_t classifier;
	uiGameflowLibraryLocation_t location;

	location = UIGameflowLibrary_Locate("gcldr:/games", "gcldr:/other");
	CHECK(location == UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	UIGameflowLibrary_ClassifierInit(&classifier, location);
	CHECK(!UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_NONE);

	location = UIGameflowLibrary_Locate("gcldr:/games/",
		"GCLDR:/GAMES/");
	CHECK(location == UI_GAMEFLOW_LIBRARY_LOCATION_ROOT);
	UIGameflowLibrary_ClassifierInit(&classifier, location);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Other [GALE01]"));
	/* Loose images beside game folders: one Library can't show both. */
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_NONE);

	location = UIGameflowLibrary_Locate("gcldr:/games",
		"gcldr:/games/Super Mario Sunshine [GMSE01]");
	CHECK(location == UI_GAMEFLOW_LIBRARY_LOCATION_STRICT_LEAF);
	UIGameflowLibrary_ClassifierInit(&classifier, location);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	CHECK(UIGameflowLibrary_Locate("gcldr:/games",
		"gcldr:/games/Bad Folder") == UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	CHECK(UIGameflowLibrary_Locate("gcldr:/games",
		"gcldr:/games/Good [GALE01]/child") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	CHECK(UIGameflowLibrary_Locate(NULL, "gcldr:/games") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
}

/* Anything in /games that isn't a game is skipped, not a reason to give the
 * whole folder to Swiss's list; only a folder with no games falls back. */
static void testClassifierSkipsStrays(void)
{
	uiGameflowLibraryClassifier_t classifier;
	uiGameflowLibraryLocation_t root = UIGameflowLibrary_Locate(
		"sd:/games", "sd:/games");
	uiGameflowLibraryLocation_t leaf = UIGameflowLibrary_Locate(
		"sd:/games", "sd:/games/Super Mario Sunshine [GMSE01]");

	UIGameflowLibrary_ClassifierInit(&classifier, root);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Old saves"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "About these games.txt"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Pikmin.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "cover.png"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.rvz"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	UIGameflowLibrary_ClassifierInit(&classifier, root);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Pikmin [GPIE01]"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Old saves"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "readme.txt"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_GAME_FOLDERS);

	UIGameflowLibrary_ClassifierInit(&classifier, leaf);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Other [GALE01]"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "cover.jpg"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	/* No games: Swiss's list, which is where "decompress this" and the
	 * other messages about a file that isn't a disc image come from. */
	UIGameflowLibrary_ClassifierInit(&classifier, root);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.rvz"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Old saves"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_NONE);

	/* ".." anywhere but first is not a directory Swiss listed. */
	UIGameflowLibrary_ClassifierInit(&classifier, root);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "game.iso"));
	CHECK(!UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_NONE);
}

static void testHomeLibraryStartupRoute(void)
{
	CHECK(UIGameflowLibrary_ShouldStartHome(false, false, false));
	CHECK(UIGameflowLibrary_ShouldStartHome(false, false, true));
	CHECK(UIGameflowLibrary_ShouldStartHome(false, true, false));
	CHECK(!UIGameflowLibrary_ShouldStartHome(false, true, true));
	CHECK(!UIGameflowLibrary_ShouldStartHome(true, false, false));
	CHECK(!UIGameflowLibrary_ShouldStartHome(true, true, true));

	CHECK(UIGameflowLibrary_IsGamesRootEntry("gcldr:/games",
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "GCLDR:/GAMES/"));
	CHECK(!UIGameflowLibrary_IsGamesRootEntry("gcldr:/games",
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "gcldr:/games"));
	CHECK(!UIGameflowLibrary_IsGamesRootEntry("gcldr:/games",
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "gcldr:/games-old"));
	CHECK(!UIGameflowLibrary_IsGamesRootEntry("gcldr:/games",
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY,
		"gcldr:/games/Super Mario Sunshine [GMSE01]"));
	CHECK(!UIGameflowLibrary_IsGamesRootEntry(NULL,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "gcldr:/games"));
}

static void testWindow(void)
{
	static const uint32_t expectedIndices[5] = {0u, 4u, 1u, 3u, 2u};
	static const int8_t expectedRelative[5] = {0, -1, 1, -2, 2};
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_WINDOW];
	size_t count;
	size_t i;
	size_t j;

	CHECK(UIGameflowLibrary_BuildWindow(0u, 0u,
		UI_GAMEFLOW_DIRECTION_NONE, slots) == 0u);
	CHECK(UIGameflowLibrary_BuildWindow(5u, 0u,
		UI_GAMEFLOW_DIRECTION_NONE, NULL) == 0u);
	count = UIGameflowLibrary_BuildWindow(5u, 0u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count == 5u);
	for(i = 0u; i < count; ++i) {
		CHECK(slots[i].index == expectedIndices[i]);
		CHECK(slots[i].relativeSlot == expectedRelative[i]);
		for(j = i + 1u; j < count; ++j) {
			CHECK(slots[i].index != slots[j].index);
		}
	}

	/* Four either side: a held stick's strip runs up to two cards behind
	 * the selection, and the third card out must still be there. */
	count = UIGameflowLibrary_BuildWindow(12u, 8u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count == 9u);
	CHECK(slots[0].index == 8u && slots[0].relativeSlot == 0);
	CHECK(slots[1].index == 7u && slots[1].relativeSlot == -1);
	CHECK(slots[2].index == 9u && slots[2].relativeSlot == 1);
	CHECK(slots[7].index == 4u && slots[7].relativeSlot == -4);
	CHECK(slots[8].index == 0u && slots[8].relativeSlot == 4);

	count = UIGameflowLibrary_BuildWindow(1u, 99u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count == 1u);
	CHECK(slots[0].index == 0u && slots[0].relativeSlot == 0);

	count = UIGameflowLibrary_BuildWindow(2u, 1u,
		UI_GAMEFLOW_DIRECTION_NEXT, slots);
	CHECK(count == 2u);
	CHECK(slots[1].index == 0u && slots[1].relativeSlot == -1);
	CHECK(slots[1].relativeSlot + 1 == 0);

	count = UIGameflowLibrary_BuildWindow(2u, 1u,
		UI_GAMEFLOW_DIRECTION_PREVIOUS, slots);
	CHECK(count == 2u);
	CHECK(slots[1].index == 0u && slots[1].relativeSlot == 1);
	CHECK(slots[1].relativeSlot - 1 == 0);
}

static void checkWindowScale(uint32_t itemCount)
{
	static const uiGameflowDirection_t directions[3] = {
		UI_GAMEFLOW_DIRECTION_PREVIOUS,
		UI_GAMEFLOW_DIRECTION_NONE,
		UI_GAMEFLOW_DIRECTION_NEXT
	};
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_WINDOW];
	uint32_t selections[3];
	size_t expectedCount = itemCount < UI_GAMEFLOW_LIBRARY_WINDOW ?
		(size_t)itemCount : (size_t)UI_GAMEFLOW_LIBRARY_WINDOW;
	size_t directionIndex;
	size_t selectionIndex;

	if(itemCount == 0u) {
		CHECK(UIGameflowLibrary_BuildWindow(0u, 0u,
			UI_GAMEFLOW_DIRECTION_NONE, slots) == 0u);
		return;
	}
	selections[0] = 0u;
	selections[1] = itemCount / 2u;
	selections[2] = itemCount - 1u;
	for(directionIndex = 0u; directionIndex < 3u; ++directionIndex) {
		for(selectionIndex = 0u; selectionIndex < 3u; ++selectionIndex) {
			size_t count = UIGameflowLibrary_BuildWindow(itemCount,
				selections[selectionIndex], directions[directionIndex], slots);
			size_t i;
			size_t j;
			CHECK(count == expectedCount);
			CHECK(slots[0].index == selections[selectionIndex]);
			CHECK(slots[0].relativeSlot == 0);
			for(i = 0u; i < count; ++i) {
				CHECK(slots[i].index < itemCount);
				CHECK(slots[i].relativeSlot >= -4);
				CHECK(slots[i].relativeSlot <= 4);
				for(j = i + 1u; j < count; ++j) {
					CHECK(slots[i].index != slots[j].index);
				}
			}
		}
	}
}

static void testWindowScaleMatrix(void)
{
	static const uint32_t titleCounts[] = {0u, 1u, 3u, 27u, 100u, 250u};
	size_t i;

	for(i = 0u; i < sizeof(titleCounts) / sizeof(titleCounts[0]); ++i) {
		checkWindowScale(titleCounts[i]);
	}
}


/* renderFileCarousel's navigation before the layouts, line for line: the
 * Horizontal layout must keep it exactly. */
static uint32_t legacyCarouselMove(uint32_t count, uint32_t selected,
	uiGameflowLibraryMove_t move)
{
	int numFiles = (int)count;
	int curSelection = (int)selected;

	switch(move) {
		case UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS:
			curSelection = (curSelection - 1 < 0) ? numFiles - 1 :
				curSelection - 1;
			break;
		case UI_GAMEFLOW_LIBRARY_MOVE_NEXT:
			curSelection = (curSelection + 1) % numFiles;
			break;
		case UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK:
			if(curSelection == 0) {
				curSelection = numFiles - 1;
			}
			else {
				curSelection = (curSelection - 9 < 0) ? 0 : curSelection - 9;
			}
			break;
		case UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON:
			if(curSelection == numFiles - 1) {
				curSelection = 0;
			}
			else {
				curSelection = (curSelection + 9 > numFiles - 1) ?
					numFiles - 1 : (curSelection + 9) % numFiles;
			}
			break;
		default:
			break;
	}
	return (uint32_t)curSelection;
}

static void testRingMovesKeepTheCarousel(void)
{
	static const uiGameflowLibraryMove_t moves[4] = {
		UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS, UI_GAMEFLOW_LIBRARY_MOVE_NEXT,
		UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON
	};
	uiGameflowLibraryStep_t step;
	uint32_t count;
	uint32_t selected;
	size_t i;

	for(count = 1u; count <= 40u; ++count) {
		for(selected = 0u; selected < count; ++selected) {
			for(i = 0u; i < 4u; ++i) {
				CHECK(UIGameflowLibrary_Move(count, 0u, selected, moves[i],
					9u, &step));
				CHECK(step.index == legacyCarouselMove(count, selected,
					moves[i]));
				CHECK(step.direction == (i % 2u == 0u ?
					UI_GAMEFLOW_DIRECTION_PREVIOUS :
					UI_GAMEFLOW_DIRECTION_NEXT));
				CHECK(step.snap == (i >= 2u));
			}
		}
	}
	/* A ring has no rows; a failed move leaves the step alone. */
	step.index = 77u;
	step.direction = UI_GAMEFLOW_DIRECTION_NONE;
	step.snap = true;
	CHECK(!UIGameflowLibrary_Move(20u, 0u, 3u, UI_GAMEFLOW_LIBRARY_MOVE_UP,
		9u, &step));
	CHECK(!UIGameflowLibrary_Move(20u, 0u, 3u,
		UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 9u, &step));
	CHECK(!UIGameflowLibrary_Move(0u, 0u, 0u,
		UI_GAMEFLOW_LIBRARY_MOVE_NEXT, 9u, &step));
	CHECK(!UIGameflowLibrary_Move(0u, 5u, 0u,
		UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u, &step));
	CHECK(!UIGameflowLibrary_Move(5u, 5u, 0u,
		(uiGameflowLibraryMove_t)99, 3u, &step));
	CHECK(!UIGameflowLibrary_Move(5u, 0u, 0u, UI_GAMEFLOW_LIBRARY_MOVE_NEXT,
		9u, NULL));
	CHECK(step.index == 77u && step.snap &&
		step.direction == UI_GAMEFLOW_DIRECTION_NONE);
	/* An out-of-range selection is the last card. */
	CHECK(UIGameflowLibrary_Move(5u, 0u, 99u, UI_GAMEFLOW_LIBRARY_MOVE_NEXT,
		9u, &step) && step.index == 0u);
}

static uint32_t gridMove(uint32_t count, uint32_t selected,
	uiGameflowLibraryMove_t move, uint32_t page)
{
	uiGameflowLibraryStep_t step;

	CHECK(UIGameflowLibrary_Move(count, 5u, selected, move, page, &step));
	CHECK(step.index < count);
	CHECK(step.snap == (move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON));
	CHECK(step.direction == ((move == UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_UP ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK) ?
		UI_GAMEFLOW_DIRECTION_PREVIOUS : UI_GAMEFLOW_DIRECTION_NEXT));
	return step.index;
}

static void testGridMoves(void)
{
	uint32_t count;
	uint32_t selected;

	/* Twelve games in rows of five: the last row holds 10 and 11. */
	CHECK(gridMove(12u, 0u, UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS, 3u) == 11u);
	CHECK(gridMove(12u, 11u, UI_GAMEFLOW_LIBRARY_MOVE_NEXT, 3u) == 0u);
	CHECK(gridMove(12u, 4u, UI_GAMEFLOW_LIBRARY_MOVE_NEXT, 3u) == 5u);
	CHECK(gridMove(12u, 5u, UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS, 3u) == 4u);
	CHECK(gridMove(12u, 3u, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) == 8u);
	CHECK(gridMove(12u, 6u, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) == 11u);
	/* The short row lacks column 3: its last card instead. */
	CHECK(gridMove(12u, 8u, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) == 11u);
	/* From the last row to the first, keeping the column. */
	CHECK(gridMove(12u, 11u, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) == 1u);
	CHECK(gridMove(12u, 10u, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) == 0u);
	/* And back from the first row to the last. */
	CHECK(gridMove(12u, 1u, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) == 11u);
	CHECK(gridMove(12u, 0u, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) == 10u);
	CHECK(gridMove(12u, 3u, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) == 11u);
	CHECK(gridMove(12u, 11u, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) == 6u);
	CHECK(gridMove(12u, 9u, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) == 4u);
	/* One row: up and down stay; left and right still wrap. */
	for(selected = 0u; selected < 4u; ++selected) {
		CHECK(gridMove(4u, selected, UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u) ==
			selected);
		CHECK(gridMove(4u, selected, UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u) ==
			selected);
		CHECK(gridMove(4u, selected, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON, 3u) ==
			selected);
		CHECK(gridMove(4u, selected, UI_GAMEFLOW_LIBRARY_MOVE_NEXT, 3u) ==
			(selected + 1u) % 4u);
	}
	CHECK(gridMove(1u, 0u, UI_GAMEFLOW_LIBRARY_MOVE_NEXT, 3u) == 0u);
	/* Pages: three rows, stopping at the first and last rows, then round. */
	CHECK(gridMove(40u, 2u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON, 3u) == 17u);
	CHECK(gridMove(40u, 32u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON, 3u) == 37u);
	CHECK(gridMove(40u, 37u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON, 3u) == 2u);
	CHECK(gridMove(40u, 2u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, 3u) == 37u);
	CHECK(gridMove(40u, 17u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, 3u) == 2u);
	CHECK(gridMove(40u, 12u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, 3u) == 2u);
	CHECK(gridMove(38u, 28u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON, 3u) == 37u);
	CHECK(gridMove(38u, 4u, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, 3u) == 37u);

	for(count = 1u; count <= 60u; ++count) {
		uint32_t rows = (count + 4u) / 5u;
		for(selected = 0u; selected < count; ++selected) {
			uint32_t row = selected / 5u;
			uint32_t down = gridMove(count, selected,
				UI_GAMEFLOW_LIBRARY_MOVE_DOWN, 3u);
			uint32_t up = gridMove(count, selected,
				UI_GAMEFLOW_LIBRARY_MOVE_UP, 3u);

			/* Left and right step one card round the ring. */
			CHECK(gridMove(count, selected, UI_GAMEFLOW_LIBRARY_MOVE_NEXT,
				3u) == (selected + 1u) % count);
			CHECK(gridMove(count, selected,
				UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS, 3u) ==
				(selected + count - 1u) % count);
			/* Up and down move exactly one row round the rows. */
			CHECK(down / 5u == (row + 1u) % rows);
			CHECK(up / 5u == (row + rows - 1u) % rows);
			/* The same column when the row has it, else its last card. */
			CHECK(down % 5u == selected % 5u || down == count - 1u);
			CHECK(up % 5u == selected % 5u || up == count - 1u);
			/* Where the column was kept, the opposite move comes back. */
			if(down % 5u == selected % 5u) {
				CHECK(gridMove(count, down, UI_GAMEFLOW_LIBRARY_MOVE_UP,
					3u) == selected);
			}
			if(up % 5u == selected % 5u) {
				CHECK(gridMove(count, up, UI_GAMEFLOW_LIBRARY_MOVE_DOWN,
					3u) == selected);
			}
			(void)gridMove(count, selected, UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON,
				3u);
			(void)gridMove(count, selected,
				UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK, 3u);
		}
	}
}

/* Which ring rows a grid window should hold, nearest first. */
static size_t expectedGridRows(uint32_t rows, uint32_t focusRow,
	uiGameflowDirection_t rowDirection, int relative[5])
{
	static const int order[5] = {0, -1, 1, -2, 2};
	uint32_t seen[5];
	size_t count = 0u;
	size_t i;

	for(i = 0u; i < 5u; ++i) {
		uint32_t row = (uint32_t)((int)(focusRow + rows * 3u) + order[i]) %
			rows;
		size_t j;

		if(rows == 2u && (order[i] == -1 || order[i] == 1)) {
			int side = rowDirection == UI_GAMEFLOW_DIRECTION_NEXT ? -1 :
				rowDirection == UI_GAMEFLOW_DIRECTION_PREVIOUS ? 1 :
				(focusRow == 0u ? 1 : -1);
			if(order[i] != side) {
				continue;
			}
		}
		for(j = 0u; j < count && seen[j] != row; ++j) {
		}
		if(j != count) {
			continue;
		}
		seen[count] = row;
		relative[count++] = order[i];
	}
	return count;
}

static void testGridWindow(void)
{
	static const uiGameflowDirection_t directions[3] = {
		UI_GAMEFLOW_DIRECTION_PREVIOUS, UI_GAMEFLOW_DIRECTION_NONE,
		UI_GAMEFLOW_DIRECTION_NEXT
	};
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_GRID_WINDOW];
	uint32_t count;
	size_t count2;
	size_t d;

	CHECK(UIGameflowLibrary_BuildGridWindow(0u, 0u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, slots) == 0u);
	CHECK(UIGameflowLibrary_BuildGridWindow(9u, 0u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, NULL) == 0u);
	CHECK(UIGameflowLibrary_BuildGridWindow(9u, 0u, 0u,
		UI_GAMEFLOW_DIRECTION_NONE, slots) == 0u);
	CHECK(UIGameflowLibrary_BuildGridWindow(9u, 0u, 6u,
		UI_GAMEFLOW_DIRECTION_NONE, slots) == 0u);

	/* Twelve games, the first card focused: its row, then the short last
	 * row above it and the middle row below. */
	count2 = UIGameflowLibrary_BuildGridWindow(12u, 0u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count2 == 12u);
	CHECK(slots[0].index == 0u && slots[0].relativeSlot == 0 &&
		slots[0].column == 0u);
	CHECK(slots[1].index == 1u && slots[4].index == 4u);
	CHECK(slots[5].index == 10u && slots[5].relativeSlot == -1);
	CHECK(slots[6].index == 11u && slots[6].column == 1u);
	CHECK(slots[7].index == 5u && slots[7].relativeSlot == 1);
	/* Centered on column 2, outwards and left first. */
	count2 = UIGameflowLibrary_BuildGridWindow(40u, 17u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count2 == 25u);
	CHECK(slots[0].index == 17u && slots[1].index == 16u &&
		slots[2].index == 18u && slots[3].index == 15u &&
		slots[4].index == 19u);
	CHECK(slots[5].index == 12u && slots[5].relativeSlot == -1);
	CHECK(slots[20].relativeSlot == 2 && slots[20].index == 27u);

	/* Two rows: the other row sits where the last move between rows left
	 * it, so the row that had the focus stays put. */
	count2 = UIGameflowLibrary_BuildGridWindow(8u, 6u, 5u,
		UI_GAMEFLOW_DIRECTION_NEXT, slots);
	CHECK(count2 == 8u && slots[3].relativeSlot == -1);
	count2 = UIGameflowLibrary_BuildGridWindow(8u, 6u, 5u,
		UI_GAMEFLOW_DIRECTION_PREVIOUS, slots);
	CHECK(count2 == 8u && slots[3].relativeSlot == 1);
	count2 = UIGameflowLibrary_BuildGridWindow(8u, 1u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count2 == 8u && slots[5].relativeSlot == 1);
	count2 = UIGameflowLibrary_BuildGridWindow(8u, 6u, 5u,
		UI_GAMEFLOW_DIRECTION_NONE, slots);
	CHECK(count2 == 8u && slots[3].relativeSlot == -1);

	for(count = 1u; count <= 60u; ++count) {
		uint32_t rows = (count + 4u) / 5u;
		uint32_t selected;

		for(selected = 0u; selected < count; ++selected) {
			for(d = 0u; d < 3u; ++d) {
				int relative[5];
				size_t rowCount = expectedGridRows(rows, selected / 5u,
					directions[d], relative);
				size_t expected = 0u;
				size_t i;
				size_t j;

				for(i = 0u; i < rowCount; ++i) {
					uint32_t row = (selected / 5u + rows * 3u +
						(uint32_t)relative[i]) % rows;
					expected += row + 1u < rows ? 5u : count - row * 5u;
				}
				count2 = UIGameflowLibrary_BuildGridWindow(count, selected,
					5u, directions[d], slots);
				CHECK(count2 == expected);
				CHECK(slots[0].index == selected);
				CHECK(slots[0].relativeSlot == 0);
				CHECK(slots[0].column == selected % 5u);
				/* Tiny libraries are one row of distinct cards. */
				if(count <= 5u) {
					CHECK(count2 == count);
				}
				for(i = 0u; i < count2; ++i) {
					/* Through an int: GCC's -Wsign-conversion flags an s8
					 * cast straight into the unsigned sum. */
					int relativeRow = slots[i].relativeSlot;
					uint32_t row = (selected / 5u + rows * 3u +
						(uint32_t)relativeRow) % rows;
					CHECK(slots[i].index < count);
					CHECK(slots[i].relativeSlot >= -2 &&
						slots[i].relativeSlot <= 2);
					CHECK(slots[i].index / 5u == row);
					CHECK(slots[i].index % 5u == slots[i].column);
					for(j = i + 1u; j < count2; ++j) {
						CHECK(slots[i].index != slots[j].index);
					}
					/* Rows in order, nearest columns first within one. */
					if(i > 0u && slots[i].relativeSlot ==
						slots[i - 1u].relativeSlot) {
						int before = (int)slots[i - 1u].column -
							(int)(selected % 5u);
						int after = (int)slots[i].column - (int)(selected % 5u);
						CHECK(abs(before) <= abs(after));
					}
				}
			}
		}
	}
}

/* Library Folders: /games and two levels of folders below it. */
static void testLibraryFoldersLocations(void)
{
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games", "sd:/games") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games", "SD:/Games/") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games", "sd:/games/RPGs") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games",
		"sd:/games/RPGs/JRPG") == UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER);
	/* Never deeper: the second level lists everything below it. */
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games",
		"sd:/games/RPGs/JRPG/Old") == UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	/* A game folder is a game, not a folder of games. */
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games",
		"sd:/games/Pikmin [GPIE01]") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_STRICT_LEAF);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games",
		"sd:/games/Pikmin [GPIE01]/extras") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games", "sd:/gamesx") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	CHECK(UIGameflowLibrary_LocateFolders("sd:/games", "sd:/apps/RPGs") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);
	CHECK(UIGameflowLibrary_LocateFolders(NULL, "sd:/games") ==
		UI_GAMEFLOW_LIBRARY_LOCATION_NONE);

	CHECK(!UIGameflowLibrary_IsInsideFolder(
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT));
	CHECK(UIGameflowLibrary_IsInsideFolder(UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER));
	CHECK(UIGameflowLibrary_IsInsideFolder(
		UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER));
	CHECK(!UIGameflowLibrary_IsInsideFolder(
		UI_GAMEFLOW_LIBRARY_LOCATION_STRICT_LEAF));

	/* The flatten pattern, as scanFiles and the recent list use it: only a
	 * second-level folder is listed flat, and a game below one is found
	 * from that folder. */
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN, "sd:/games/RPGs/JRPG",
		FNM_PATHNAME | FNM_CASEFOLD) == 0);
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN, "sd:/games/RPGs",
		FNM_PATHNAME | FNM_CASEFOLD) == FNM_NOMATCH);
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN, "sd:/games",
		FNM_PATHNAME | FNM_CASEFOLD) == FNM_NOMATCH);
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN, "sd:/apps/a/b",
		FNM_PATHNAME | FNM_CASEFOLD) == FNM_NOMATCH);
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN,
		"sd:/games/RPGs/JRPG/Old/Deeper", FNM_PATHNAME | FNM_CASEFOLD) ==
		FNM_NOMATCH);
	CHECK(fnmatch(UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN,
		"sd:/games/RPGs/JRPG/Old/Deeper",
		FNM_PATHNAME | FNM_CASEFOLD | FNM_LEADING_DIR) == 0);
}

/* A folder's picture: the folder's own path and ".png", case aside. */
static void testLibraryFoldersPicture(void)
{
	CHECK(UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo.png"));
	CHECK(UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"SD:/GAMES/nintendo.PNG"));
	CHECK(UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo/",
		"sd:/games/Nintendo.png"));
	CHECK(UIGameflowLibrary_IsFolderPicture("sd:/games/RPGs/Old saves",
		"sd:/games/RPGs/Old saves.png"));
	/* Only the folder's own name, beside it. */
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo.jpg"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo 2.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintend.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo.png.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo/Nintendo.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Other/Nintendo.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/RPGs/Nintendo",
		"sd:/games/Nintendo.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/Nintendo",
		"sd:/games/Nintendo"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("", ".png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("/", "/.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture(NULL, "sd:/games/x.png"));
	CHECK(!UIGameflowLibrary_IsFolderPicture("sd:/games/x", NULL));
}

static void testLibraryFoldersHeading(void)
{
	char heading[64];
	char tiny[12];

	CHECK(UIGameflowLibrary_FolderHeading("sd:/games", "sd:/games/RPGs/JRPG",
		heading, sizeof(heading)));
	CHECK(strcmp(heading, "RPGS / JRPG") == 0);
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games", "SD:/Games/rpgs/",
		heading, sizeof(heading)));
	CHECK(strcmp(heading, "RPGS") == 0);
	/* A cut in a Cyrillic name keeps whole letters before its ellipsis. */
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games", "sd:/games/aяяяяяяяяяя",
		tiny, sizeof(tiny)));
	CHECK(strlen(tiny) == 10u && (unsigned char)tiny[9] == 0x85u);
	CHECK(UILang_Head(tiny, 9u) == 9u);
	/* /games itself and anywhere else have no heading. */
	CHECK(!UIGameflowLibrary_FolderHeading("sd:/games", "sd:/games",
		heading, sizeof(heading)));
	CHECK(heading[0] == '\0');
	CHECK(!UIGameflowLibrary_FolderHeading("sd:/games", "sd:/apps/RPGs",
		heading, sizeof(heading)));
	CHECK(!UIGameflowLibrary_FolderHeading("sd:/games", "sd:/gamesx/RPGs",
		heading, sizeof(heading)));
	/* Too long: the start gives way, so the folder shown still ends it. */
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games",
		"sd:/games/Flight and Space Games From Every Region of the World/"
		"The Very Long Second Level Folder", heading, sizeof(heading)));
	CHECK(strcmp(heading, "\205 / THE VERY LONG SECOND LEVEL FOLDER") == 0);
	/* A name too long even alone is cut at its end. */
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games",
		"sd:/games/A/Abcdefghijklmnop", tiny, sizeof(tiny)));
	CHECK(strcmp(tiny, "\205 / ABCDEF\205") == 0);
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games",
		"sd:/games/Abcdefghijklmnop", tiny, sizeof(tiny)));
	CHECK(strcmp(tiny, "ABCDEFGHIJ\205") == 0);
	/* Exactly full: no ellipsis. */
	CHECK(UIGameflowLibrary_FolderHeading("sd:/games", "sd:/games/Abcdefghijk",
		tiny, sizeof(tiny)));
	CHECK(strcmp(tiny, "ABCDEFGHIJK") == 0);
	CHECK(!UIGameflowLibrary_FolderHeading("sd:/games", "sd:/games/RPGs",
		heading, 0u));
}

static void testLibraryFoldersClassifier(void)
{
	uiGameflowLibraryClassifier_t classifier;

	/* Folders, game folders and images together. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "RPGs"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Pikmin [GPIE01]"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "readme.txt"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_FOLDERS);

	/* Images alone: the same Library as with Library Folders off. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	/* A folder that holds only folders is still a Library. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "JRPG"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_FOLDERS);

	/* The deepest folder takes images only: what's left of its folders
	 * after Swiss lists their games is empty. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Empty"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Tales disc 1.iso"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	/* With Library Folders on, /games and both folder levels stay in the
	 * Library with only their way back when they hold no games. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "notes.txt"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
	CHECK(UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES, 0u,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(!UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "notes.txt"));
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Empty"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "notes.txt"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	/* With Library Folders off, /games keeps today's rule. */
	UIGameflowLibrary_ClassifierInit(&classifier,
		UI_GAMEFLOW_LIBRARY_LOCATION_ROOT);
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "RPGs"));
	CHECK(UIGameflowLibrary_ClassifierAdd(&classifier,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso"));
	CHECK(UIGameflowLibrary_ClassifierFinish(&classifier) ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
}

static void testLibraryFoldersEntries(void)
{
	const uiGameflowLibraryMode_t folders = UI_GAMEFLOW_LIBRARY_FOLDERS;

	CHECK(UIGameflowLibrary_EntryEligible(folders, 0u,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(!UIGameflowLibrary_EntryEligible(folders, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."));
	CHECK(UIGameflowLibrary_EntryEligible(folders, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "RPGs"));
	CHECK(UIGameflowLibrary_EntryEligible(folders, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Pikmin [GPIE01]"));
	CHECK(UIGameflowLibrary_EntryEligible(folders, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso"));
	CHECK(!UIGameflowLibrary_EntryEligible(folders, 1u,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "readme.txt"));

	CHECK(UIGameflowLibrary_EntryMode(folders,
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso") ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
	CHECK(UIGameflowLibrary_EntryMode(folders,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Pikmin [GPIE01]") ==
		UI_GAMEFLOW_LIBRARY_GAME_FOLDERS);
	CHECK(UIGameflowLibrary_EntryMode(folders,
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "RPGs") ==
		UI_GAMEFLOW_LIBRARY_NONE);
	CHECK(UIGameflowLibrary_EntryMode(folders,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, "..") ==
		UI_GAMEFLOW_LIBRARY_NONE);
	CHECK(UIGameflowLibrary_EntryMode(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, "..") ==
		UI_GAMEFLOW_LIBRARY_IMAGE_FILES);

	/* A game in a folder opens its Detail; a folder of games and the
	 * parent card never do. */
	CHECK(UIGameflowLibrary_UsesRetainedDetail(UIGameflowLibrary_EntryMode(
		folders, UI_GAMEFLOW_LIBRARY_ENTRY_FILE, "Zelda.iso"),
		UI_GAMEFLOW_LIBRARY_ENTRY_FILE));
	CHECK(UIGameflowLibrary_UsesRetainedDetail(UIGameflowLibrary_EntryMode(
		folders, UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "Pikmin [GPIE01]"),
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY));
	CHECK(!UIGameflowLibrary_UsesRetainedDetail(UIGameflowLibrary_EntryMode(
		folders, UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY, "RPGs"),
		UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY));
	CHECK(!UIGameflowLibrary_UsesRetainedDetail(UIGameflowLibrary_EntryMode(
		folders, UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL, ".."),
		UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL));
}

int main(void)
{
	testImageEligibility();
	testStrictFolderEligibility();
	testRetainedDetailDispatch();
	testArtworkFallbackPolicy();
	testProductionFlattenedUpgrade();
	testClassifierFallbackMatrix();
	testClassifierSkipsStrays();
	testLibraryFoldersLocations();
	testLibraryFoldersPicture();
	testLibraryFoldersHeading();
	testLibraryFoldersClassifier();
	testLibraryFoldersEntries();
	testHomeLibraryStartupRoute();
	testWindow();
	testWindowScaleMatrix();
	testRingMovesKeepTheCarousel();
	testGridMoves();
	testGridWindow();
	puts("ui_gameflow library tests passed");
	return EXIT_SUCCESS;
}
