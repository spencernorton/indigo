#ifndef UI_GAMEFLOW_DETAIL_H
#define UI_GAMEFLOW_DETAIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ui_game_history.h"
#include "ui_saves_metadata.h"

#define UI_GAMEFLOW_DETAIL_ID_LENGTH 6u
#define UI_GAMEFLOW_DETAIL_TITLE_CAPACITY 96u
#define UI_GAMEFLOW_DETAIL_COMPANY_CAPACITY 64u
#define UI_GAMEFLOW_DETAIL_FACTS_CAPACITY 96u
#define UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES 3u
#define UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY 80u
/* The description comes from a disc banner: 128 characters, which need
 * not end in a NUL. No more than that is read. */
#define UI_GAMEFLOW_DETAIL_DESCRIPTION_SOURCE 128u
#define UI_GAMEFLOW_DETAIL_ENABLED_NAMES 3u
#define UI_GAMEFLOW_DETAIL_CHEAT_NAME_CAPACITY 64u
#define UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY 96u
#define UI_GAMEFLOW_DETAIL_LAUNCH_LABEL_CAPACITY 48u
#define UI_GAMEFLOW_DETAIL_ADVANCED_CAPACITY 64u
#define UI_GAMEFLOW_DETAIL_BANNER_BYTES 6144u

typedef enum {
	UI_GAMEFLOW_DETAIL_VALID = 1u << 0,
	UI_GAMEFLOW_DETAIL_HAS_BANNER = 1u << 1,
	UI_GAMEFLOW_DETAIL_CHEATS_KNOWN = 1u << 2,
	UI_GAMEFLOW_DETAIL_CAN_SETTINGS = 1u << 3,
	UI_GAMEFLOW_DETAIL_CAN_CHEATS = 1u << 4,
	UI_GAMEFLOW_DETAIL_CAN_LIBRARY = 1u << 5,
	UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD = 1u << 6,
	UI_GAMEFLOW_DETAIL_IS_AUTOLOAD = 1u << 7,
	UI_GAMEFLOW_DETAIL_CAN_VERIFY = 1u << 8,
	UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT = 1u << 9,
	UI_GAMEFLOW_DETAIL_CLEAN_BOOT_DEFAULT = 1u << 10,
	UI_GAMEFLOW_DETAIL_AUDIO_STREAMING = 1u << 11,
	UI_GAMEFLOW_DETAIL_HAS_DISC_TWO = 1u << 12,
	/* Set by Build: the SAVES inset shows (any count with Always selected). */
	UI_GAMEFLOW_DETAIL_HAS_SAVES = 1u << 13,
	/* Set by Build: Left and Right choose the copy to start the game with. */
	UI_GAMEFLOW_DETAIL_SAVE_CHOICE = 1u << 14,
	/* Opened from the File Browser: B goes back there, not to the Library. */
	UI_GAMEFLOW_DETAIL_BACK_FILES = 1u << 15,
	/* Set by Build: the game has a manual (swiss/ui/manuals/): the Manual
	 * row shows, above Settings, and A on it opens the manual. */
	UI_GAMEFLOW_DETAIL_HAS_MANUAL = 1u << 16
} uiGameflowDetailFlags_t;

typedef struct {
	const char *name;
	bool enabled;
} uiGameflowDetailCheatSource_t;

/* Ephemeral menu-thread input. Build copies every byte that survives into the
 * retained event; none of these pointers may be stored or used by drawing. */
typedef struct {
	uint32_t generation;
	uint32_t focusIndex;
	const char *gameId;
	const char *title;
	const char *company;
	const char *facts;
	const char *description;
	uint64_t lastPlayedUnixSeconds;
	bool playHistoryAvailable;
	uiGameSaveStatus_t saveStatus;
	const uiSavesGameStats_t *saveStats;
	/* Show a settled scan even with one copy or none. No saveStats still
	 * means no scan (Saves on Details off), so nothing shows. */
	bool savesAlways;
	/* A memory card is still changing to this game's card: its saves aren't
	 * in saveStats yet, and no copy is chosen. */
	bool savesWaiting;
	/* One was given up on: its saves are missing from saveStats. */
	bool savesCardFailed;
	/* Left and Right's choice among saveCopies copies (1-based; 0: none, the
	 * totals show): its entry, where it is, and whether it is on the card the
	 * game reads. */
	uint32_t saveCopies;
	uint32_t saveChoice;
	const uint8_t *saveChoiceEntry;
	const char *saveChoiceWhere;
	bool saveChoiceInUse;
	/* With copies of more than one file: the chosen copy's file, and its
	 * place among that file's copies (1-based). NULL: one file. */
	const char *saveChoiceFile;
	uint32_t saveChoiceFileCopy;
	uint32_t saveChoiceFileCopies;
	/* The game's own second comment line ("Last saved on 1/6") of the copy
	 * the box describes: the newest for the totals, the chosen one while
	 * choosing. NULL or empty: none. */
	const char *saveLine;
	const char *saveChoiceLine;
	const uint8_t *banner;
	size_t bannerSize;
	const uiGameflowDetailCheatSource_t *cheats;
	size_t cheatCount;
	uint32_t enabledCheatBytes;
	uint32_t cheatCapacityBytes;
	uint32_t flags;
	uint32_t customSettings;    /* this game's rows that differ from Game Defaults */
	const char *firstCustomSetting; /* the first of them, "Name: value" */
	/* The game's manual: its pages, 0 for none, -1 for one that can't be
	 * read (the row still shows, so the reason can). */
	int32_t manualPages;
} uiGameflowDetailSource_t;

/* Fixed, pointer-free video-thread payload. The banner begins on a cache-line
 * boundary when this object itself is 32-byte aligned (as EV_GAMEFLOW is). */
typedef struct {
	uint32_t generation;
	uint32_t focusIndex;
	uint32_t flags;
	uint32_t cheatCount;
	uint32_t enabledCheatCount;
	uint32_t enabledCheatBytes;
	uint32_t cheatCapacityBytes;
	uint32_t customSettings;
	char gameId[UI_GAMEFLOW_DETAIL_ID_LENGTH + 1u];
	char title[UI_GAMEFLOW_DETAIL_TITLE_CAPACITY];
	char company[UI_GAMEFLOW_DETAIL_COMPANY_CAPACITY];
	char lastPlayedText[64];
	char saveStatusText[48];
	uiSavesGameStats_t saveStats;
	/* Read-only save copies in the checked slots, the configured Save Folder
	 * and the Source's swiss/saves, formatted only when
	 * UI_GAMEFLOW_DETAIL_HAS_SAVES is set. */
	char savesSummary[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	char savesUpdated[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	char facts[UI_GAMEFLOW_DETAIL_FACTS_CAPACITY];
	char description[UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES]
		[UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY];
	char enabledCheatNames[UI_GAMEFLOW_DETAIL_ENABLED_NAMES]
		[UI_GAMEFLOW_DETAIL_CHEAT_NAME_CAPACITY];
	/* Menu-thread presentation copy. Dynamic strings are formatted once by
	 * Build so the vsync-locked renderer never assembles labels per frame. */
	char statusText[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	char cheatSummary[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	char cheatPreview[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	/* The SETTINGS inset: "3 custom" or "Game Defaults", then the first
	 * custom row, or how to set some. */
	char settingsSummary[UI_GAMEFLOW_DETAIL_ADVANCED_CAPACITY];
	char settingsPreview[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	/* The MANUAL row: "24 PAGES", or why it can't open. */
	char manualSummary[UI_GAMEFLOW_DETAIL_ADVANCED_CAPACITY];
	char launchLabel[UI_GAMEFLOW_DETAIL_LAUNCH_LABEL_CAPACITY];
	char primaryActions[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	char advancedLineOne[UI_GAMEFLOW_DETAIL_ADVANCED_CAPACITY];
	char advancedLineTwo[UI_GAMEFLOW_DETAIL_ADVANCED_CAPACITY];
	uint8_t banner[UI_GAMEFLOW_DETAIL_BANNER_BYTES]
		__attribute__((aligned(32)));
} uiGameflowDetailSnapshot_t;

typedef enum {
	UI_GAMEFLOW_DETAIL_INPUT_A = 1u << 0,
	UI_GAMEFLOW_DETAIL_INPUT_B = 1u << 1,
	UI_GAMEFLOW_DETAIL_INPUT_X = 1u << 2,
	UI_GAMEFLOW_DETAIL_INPUT_Y = 1u << 3,
	UI_GAMEFLOW_DETAIL_INPUT_Z = 1u << 4,
	UI_GAMEFLOW_DETAIL_INPUT_R = 1u << 5,
	UI_GAMEFLOW_DETAIL_INPUT_L = 1u << 6,
	UI_GAMEFLOW_DETAIL_INPUT_UP = 1u << 7,
	UI_GAMEFLOW_DETAIL_INPUT_DOWN = 1u << 8
} uiGameflowDetailInput_t;

/* The rows up and down move between, counted from the bottom as they are
 * drawn: Launch, Cheats above it, Settings above that, and Manual at the
 * top when the game has one. The SAVES inset, when shown, is read-only and
 * takes no focus. A zeroed event rests on Launch. */
typedef enum {
	UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH = 0,
	UI_GAMEFLOW_DETAIL_FOCUS_CHEATS,
	UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS,
	UI_GAMEFLOW_DETAIL_FOCUS_MANUAL
} uiGameflowDetailFocus_t;

typedef enum {
	UI_GAMEFLOW_DETAIL_ACTION_NONE = 0,
	UI_GAMEFLOW_DETAIL_ACTION_BOOT,
	UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT,
	UI_GAMEFLOW_DETAIL_ACTION_LIBRARY,
	UI_GAMEFLOW_DETAIL_ACTION_SETTINGS,
	UI_GAMEFLOW_DETAIL_ACTION_CHEATS,
	UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD,
	UI_GAMEFLOW_DETAIL_ACTION_VERIFY,
	UI_GAMEFLOW_DETAIL_ACTION_MANUAL
} uiGameflowDetailAction_t;

bool UIGameflowDetail_Build(uiGameflowDetailSnapshot_t *snapshot,
	const uiGameflowDetailSource_t *source);
bool UIGameflowDetail_Matches(const uiGameflowDetailSnapshot_t *snapshot,
	uint32_t generation, uint32_t focusIndex, const char *gameId,
	size_t gameIdLength);
/* UP or DOWN moves one row, past a row the game can't use (Cheats when it
 * has none, Manual when it has no manual), and stops at the ends. */
uiGameflowDetailFocus_t UIGameflowDetail_MoveFocus(
	const uiGameflowDetailSnapshot_t *snapshot, uiGameflowDetailFocus_t focus,
	uint32_t input);
/* Plain A runs the focused row. Clean Boot's L+A and every other shortcut
 * do what they always did, whatever is focused. */
uiGameflowDetailAction_t UIGameflowDetail_ResolveAction(
	const uiGameflowDetailSnapshot_t *snapshot, uiGameflowDetailFocus_t focus,
	uint32_t input);

#endif
