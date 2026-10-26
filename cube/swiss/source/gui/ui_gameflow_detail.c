#include <stdio.h>
#include <string.h>

#include "ui_gameflow_detail.h"

#define UI_GAMEFLOW_DETAIL_WRAP_COLUMNS 48u

static bool validGameId(const char *gameId)
{
	size_t i;

	if(gameId == NULL) {
		return false;
	}
	for(i = 0u; i < UI_GAMEFLOW_DETAIL_ID_LENGTH; ++i) {
		unsigned char value = (unsigned char)gameId[i];
		if(!((value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9'))) {
			return false;
		}
	}
	return gameId[UI_GAMEFLOW_DETAIL_ID_LENGTH] == '\0';
}

static size_t boundedLength(const char *text, size_t limit)
{
	size_t length = 0u;

	while(text != NULL && length < limit && text[length] != '\0') {
		++length;
	}
	return length;
}

static void copyText(char *destination, size_t capacity, const char *source)
{
	size_t length;

	if(destination == NULL || capacity == 0u) {
		return;
	}
	if(source == NULL) {
		destination[0] = '\0';
		return;
	}
	length = boundedLength(source, capacity - 1u);
	memcpy(destination, source, length);
	destination[length] = '\0';
}

static void appendText(char *destination, size_t capacity, const char *text)
{
	size_t length;
	size_t textLength;

	if(destination == NULL || capacity == 0u || text == NULL) {
		return;
	}
	length = boundedLength(destination, capacity);
	if(length >= capacity - 1u) {
		return;
	}
	if(length != 0u) {
		const char separator[] = "   ";
		size_t available = capacity - length - 1u;
		size_t copyLength = sizeof(separator) - 1u;

		if(copyLength > available) {
			copyLength = available;
		}
		memcpy(&destination[length], separator, copyLength);
		length += copyLength;
		destination[length] = '\0';
	}
	textLength = boundedLength(text, capacity);
	if(textLength > capacity - length - 1u) {
		textLength = capacity - length - 1u;
	}
	memcpy(&destination[length], text, textLength);
	destination[length + textLength] = '\0';
}

static const char *skipWhitespace(const char *text)
{
	while(text != NULL && (*text == ' ' || *text == '\t' ||
		*text == '\r' || *text == '\n')) {
		++text;
	}
	return text;
}

static void markDescriptionTruncated(char *line, size_t capacity)
{
	size_t length;

	if(line == NULL || capacity < 4u) {
		return;
	}
	length = boundedLength(line, capacity - 1u);
	if(length + 3u < capacity &&
		length + 3u <= UI_GAMEFLOW_DETAIL_WRAP_COLUMNS) {
		memcpy(&line[length], "...", 4u);
		return;
	}
	if(length >= 3u) {
		memcpy(&line[length - 3u], "...", 4u);
	}
}

static void wrapDescription(
	char lines[UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES]
		[UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY], const char *description)
{
	const char *cursor = skipWhitespace(description);
	size_t line;

	for(line = 0u; line < UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES &&
		cursor != NULL && *cursor != '\0'; ++line) {
		const char *end = cursor;
		const char *lastSpace = NULL;
		size_t length = 0u;

		while(*end != '\0' && *end != '\r' && *end != '\n' &&
			length < UI_GAMEFLOW_DETAIL_WRAP_COLUMNS) {
			if(*end == ' ' || *end == '\t') {
				lastSpace = end;
			}
			++end;
			++length;
		}
		if(*end != '\0' && *end != '\r' && *end != '\n' &&
			lastSpace != NULL && lastSpace > cursor) {
			end = lastSpace;
			length = (size_t)(end - cursor);
		}
		while(length > 0u && (cursor[length - 1u] == ' ' ||
			cursor[length - 1u] == '\t')) {
			--length;
		}
		if(length >= UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY) {
			length = UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY - 1u;
		}
		memcpy(lines[line], cursor, length);
		lines[line][length] = '\0';
		cursor = skipWhitespace(end);
	}
	if(line == UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES && cursor != NULL &&
		*cursor != '\0') {
		markDescriptionTruncated(
			lines[UI_GAMEFLOW_DETAIL_DESCRIPTION_LINES - 1u],
			UI_GAMEFLOW_DETAIL_DESCRIPTION_CAPACITY);
	}
}

static void buildPresentation(uiGameflowDetailSnapshot_t *snapshot)
{
	uint32_t flags = snapshot->flags;

	if((flags & UI_GAMEFLOW_DETAIL_AUDIO_STREAMING) != 0u) {
		appendText(snapshot->statusText, sizeof(snapshot->statusText),
			"AUDIO STREAMING");
	}
	if((flags & UI_GAMEFLOW_DETAIL_HAS_DISC_TWO) != 0u) {
		appendText(snapshot->statusText, sizeof(snapshot->statusText),
			"DISC 2 READY");
	}
	if((flags & UI_GAMEFLOW_DETAIL_IS_AUTOLOAD) != 0u) {
		appendText(snapshot->statusText, sizeof(snapshot->statusText),
			"AUTOLOAD");
	}

	if((flags & UI_GAMEFLOW_DETAIL_CHEATS_KNOWN) == 0u) {
		copyText(snapshot->cheatSummary, sizeof(snapshot->cheatSummary),
			"STATUS UNAVAILABLE");
	}
	else if(snapshot->cheatCount == 0u) {
		copyText(snapshot->cheatSummary, sizeof(snapshot->cheatSummary),
			"NO CHEATS FOUND");
	}
	else {
		(void)snprintf(snapshot->cheatSummary,
			sizeof(snapshot->cheatSummary),
			"%lu of %lu enabled",
			(unsigned long)snapshot->enabledCheatCount,
			(unsigned long)snapshot->cheatCount);
		if(snapshot->enabledCheatCount == 0u) {
			copyText(snapshot->cheatPreview,
				sizeof(snapshot->cheatPreview), "Y  Choose cheats");
		}
		else if(snapshot->enabledCheatCount == 1u) {
			copyText(snapshot->cheatPreview,
				sizeof(snapshot->cheatPreview),
				snapshot->enabledCheatNames[0]);
		}
		else {
			(void)snprintf(snapshot->cheatPreview,
				sizeof(snapshot->cheatPreview), "%s  +%lu MORE",
				snapshot->enabledCheatNames[0],
				(unsigned long)(snapshot->enabledCheatCount - 1u));
		}
	}

	copyText(snapshot->launchLabel, sizeof(snapshot->launchLabel),
		(flags & UI_GAMEFLOW_DETAIL_CLEAN_BOOT_DEFAULT) != 0u ?
		"CLEAN BOOT" : "LAUNCH GAME");
	appendText(snapshot->primaryActions, sizeof(snapshot->primaryActions),
		"D-PAD  MOVE");
	appendText(snapshot->primaryActions, sizeof(snapshot->primaryActions),
		"A  SELECT");
	if((flags & UI_GAMEFLOW_DETAIL_CAN_LIBRARY) != 0u) {
		appendText(snapshot->primaryActions,
			sizeof(snapshot->primaryActions),
			(flags & UI_GAMEFLOW_DETAIL_BACK_FILES) != 0u ?
			"B  BACK" : "B  LIBRARY");
	}
	if((flags & UI_GAMEFLOW_DETAIL_CAN_SETTINGS) != 0u) {
		appendText(snapshot->primaryActions,
			sizeof(snapshot->primaryActions), "X  SETTINGS");
	}
	if((flags & UI_GAMEFLOW_DETAIL_CAN_CHEATS) != 0u) {
		appendText(snapshot->primaryActions,
			sizeof(snapshot->primaryActions), "Y  CHEATS");
	}

	if((flags & UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD) != 0u) {
		appendText(snapshot->advancedLineOne,
			sizeof(snapshot->advancedLineOne),
			(flags & UI_GAMEFLOW_DETAIL_IS_AUTOLOAD) != 0u ?
			"Z  AUTOLOAD ON" : "Z  AUTOLOAD");
	}
	if((flags & UI_GAMEFLOW_DETAIL_CAN_VERIFY) != 0u) {
		appendText(snapshot->advancedLineOne,
			sizeof(snapshot->advancedLineOne), "R  VERIFY");
	}
	if((flags & UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT) != 0u) {
		copyText(snapshot->advancedLineTwo,
			sizeof(snapshot->advancedLineTwo), "L+A  CLEAN BOOT");
	}
}

/* The SETTINGS inset, like the CHEATS one: how many of the game's rows are
 * its own and the first of them, or how to set some. The command rail keeps
 * a plain "X  SETTINGS", so the count isn't said twice. */
static void buildSettingsPresentation(uiGameflowDetailSnapshot_t *snapshot,
	const char *first)
{
	if((snapshot->flags & UI_GAMEFLOW_DETAIL_CAN_SETTINGS) == 0u) {
		return;
	}
	if(first == NULL || first[0] == '\0') {
		first = "Custom settings";
	}
	if(snapshot->customSettings == 0u) {
		copyText(snapshot->settingsSummary, sizeof(snapshot->settingsSummary),
			"Game Defaults");
		copyText(snapshot->settingsPreview, sizeof(snapshot->settingsPreview),
			"X  Change for this game");
		return;
	}
	(void)snprintf(snapshot->settingsSummary, sizeof(snapshot->settingsSummary),
		"%lu custom", (unsigned long)snapshot->customSettings);
	if(snapshot->customSettings == 1u) {
		copyText(snapshot->settingsPreview, sizeof(snapshot->settingsPreview),
			first);
	}
	else {
		(void)snprintf(snapshot->settingsPreview,
			sizeof(snapshot->settingsPreview), "%s  +%lu MORE", first,
			(unsigned long)(snapshot->customSettings - 1u));
	}
}

/* The game's own line after what the box says, when there is one. */
static const char *savesLineAfter(const char *line)
{
	return line != NULL && line[0] != '\0' ? " | " : "";
}

/* SAVES normally shows for two or more save copies. Always also shows a
 * settled scan with one copy or none. A memory card changing to this game's
 * card, or given up on, still says just that with fewer than two copies.
 * Copies of one file are copies; different files of the game are files,
 * each file's blocks counted once. line: the newest copy's own line. */
static void buildSavesPresentation(uiGameflowDetailSnapshot_t *snapshot,
	const uiSavesGameStats_t *stats, bool always, bool waiting, bool cardFailed,
	const char *line)
{
	char updated[17];
	const char *blocks;
	uint32_t files;
	bool incomplete;

	if(stats == NULL || (stats->saves < 2u && !always && !waiting && !cardFailed)) {
		return;
	}
	snapshot->flags |= UI_GAMEFLOW_DETAIL_HAS_SAVES;
	snapshot->saveStats = *stats;
	if(stats->saves < 2u && (waiting || cardFailed)) {
		copyText(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			waiting ? "Memory card loading" : "Memory card didn't load");
		copyText(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			waiting ? "Reading it when it's ready" : "Its saves weren't read");
		return;
	}
	incomplete = stats->partial || stats->checkedSources == 0u;
	if(stats->saves == 0u) {
		copyText(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			incomplete ? "Save scan incomplete" : "No save copies found");
		return;
	}
	files = stats->files != 0u && stats->files <= stats->saves ?
		stats->files : stats->saves;
	blocks = stats->blocks == 1u ? "block" : "blocks";
	if(stats->saves == 1u) {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"1 save | %lu %s", (unsigned long)stats->blocks, blocks);
	}
	else if(files == 1u) {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"%lu copies | %lu %s", (unsigned long)stats->saves,
			(unsigned long)stats->blocks, blocks);
	}
	else if(files == stats->saves) {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"%lu files | %lu %s", (unsigned long)files,
			(unsigned long)stats->blocks, blocks);
	}
	else {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"%lu files, %lu copies | %lu %s", (unsigned long)files,
			(unsigned long)stats->saves, (unsigned long)stats->blocks, blocks);
	}
	/* A card still changing to this game's card: the count grows when it
	 * is done; nothing about it is a partial scan. */
	if(waiting) {
		copyText(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			"Memory card loading");
		return;
	}
	if(stats->updatedKnown &&
		UISaves_FormatUpdated(stats->latestUpdated, updated, sizeof(updated))) {
		(void)snprintf(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			"%sUpdated %s%s%s", incomplete ? "Partial scan | " : "", updated,
			savesLineAfter(line), line != NULL ? line : "");
	}
	else {
		(void)snprintf(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			"%s%s%s", incomplete ? "Partial scan | Update date unavailable" :
			"Update date unavailable", savesLineAfter(line), line != NULL ? line : "");
	}
}

/* With two or more copies Left and Right step through, the one chosen: its
 * file when there are several, where it is, whether the game reads it
 * already or it goes on at launch, and its own line. */
static void buildSaveChoicePresentation(uiGameflowDetailSnapshot_t *snapshot,
	const uiGameflowDetailSource_t *source)
{
	char updated[17];
	const char *when;
	const char *where;
	const char *line;

	if((snapshot->flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) == 0u ||
		source->saveCopies < 2u || source->savesWaiting) {
		return;
	}
	snapshot->flags |= UI_GAMEFLOW_DETAIL_SAVE_CHOICE;
	if(source->saveChoice == 0u || source->saveChoice > source->saveCopies ||
		source->saveChoiceEntry == NULL) {
		return;
	}
	where = source->saveChoiceWhere != NULL ? source->saveChoiceWhere : "";
	if(source->saveChoiceFile == NULL || source->saveChoiceFile[0] == '\0') {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"Copy %lu of %lu | %s", (unsigned long)source->saveChoice,
			(unsigned long)source->saveCopies, where);
	}
	else if(source->saveChoiceFileCopies >= 2u) {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"%s %lu of %lu | %s", source->saveChoiceFile,
			(unsigned long)source->saveChoiceFileCopy,
			(unsigned long)source->saveChoiceFileCopies, where);
	}
	else {
		(void)snprintf(snapshot->savesSummary, sizeof(snapshot->savesSummary),
			"%s | %s", source->saveChoiceFile, where);
	}
	when = source->saveChoiceInUse ? "In use" : "Loads at launch";
	line = source->saveChoiceLine != NULL ? source->saveChoiceLine : "";
	if(UISaves_FormatUpdated(UISaves_UpdatedSeconds(source->saveChoiceEntry),
		updated, sizeof(updated))) {
		(void)snprintf(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			"%s | %s%s%s", when, updated, savesLineAfter(line), line);
	}
	else {
		(void)snprintf(snapshot->savesUpdated, sizeof(snapshot->savesUpdated),
			"%s | Date unavailable%s%s", when, savesLineAfter(line), line);
	}
}

/* MANUAL, for a game with a manual: its page count, or that the file can't
 * be read (a damaged or wrong file still shows, so it can be replaced). */
static void buildManualPresentation(uiGameflowDetailSnapshot_t *snapshot,
	int32_t pages)
{
	if(pages == 0) {
		return;
	}
	snapshot->flags |= UI_GAMEFLOW_DETAIL_HAS_MANUAL;
	if(pages < 0) {
		copyText(snapshot->manualSummary, sizeof(snapshot->manualSummary),
			"CAN'T BE READ");
		return;
	}
	(void)snprintf(snapshot->manualSummary, sizeof(snapshot->manualSummary),
		"%ld %s", (long)pages, pages == 1 ? "PAGE" : "PAGES");
}

bool UIGameflowDetail_Build(uiGameflowDetailSnapshot_t *snapshot,
	const uiGameflowDetailSource_t *source)
{
	size_t i;
	size_t enabledNameIndex = 0u;
	char description[UI_GAMEFLOW_DETAIL_DESCRIPTION_SOURCE + 1u];

	if(snapshot == NULL) {
		return false;
	}
	memset(snapshot, 0, sizeof(*snapshot));
	if(source == NULL || !validGameId(source->gameId) ||
		source->title == NULL || source->title[0] == '\0' ||
		(source->cheatCount != 0u && source->cheats == NULL)) {
		return false;
	}

	snapshot->generation = source->generation;
	snapshot->focusIndex = source->focusIndex;
	snapshot->customSettings = source->customSettings;
	snapshot->flags =
		(source->flags & ~(uint32_t)(UI_GAMEFLOW_DETAIL_HAS_BANNER |
			UI_GAMEFLOW_DETAIL_HAS_MANUAL |
			UI_GAMEFLOW_DETAIL_HAS_SAVES | UI_GAMEFLOW_DETAIL_SAVE_CHOICE)) |
		UI_GAMEFLOW_DETAIL_VALID;
	memcpy(snapshot->gameId, source->gameId,
		UI_GAMEFLOW_DETAIL_ID_LENGTH + 1u);
	copyText(snapshot->title, sizeof(snapshot->title), source->title);
	copyText(snapshot->company, sizeof(snapshot->company), source->company);
	copyText(snapshot->facts, sizeof(snapshot->facts), source->facts);
	copyText(description, sizeof(description), source->description);
	wrapDescription(snapshot->description, description);

	if(source->banner != NULL &&
		source->bannerSize == UI_GAMEFLOW_DETAIL_BANNER_BYTES) {
		memcpy(snapshot->banner, source->banner,
			UI_GAMEFLOW_DETAIL_BANNER_BYTES);
		snapshot->flags |= UI_GAMEFLOW_DETAIL_HAS_BANNER;
	}

	if(source->cheatCount > UINT32_MAX) {
		snapshot->cheatCount = UINT32_MAX;
	}
	else {
		snapshot->cheatCount = (uint32_t)source->cheatCount;
	}
	for(i = 0u; i < source->cheatCount; ++i) {
		if(!source->cheats[i].enabled) {
			continue;
		}
		if(snapshot->enabledCheatCount != UINT32_MAX) {
			++snapshot->enabledCheatCount;
		}
		if(enabledNameIndex < UI_GAMEFLOW_DETAIL_ENABLED_NAMES) {
			copyText(snapshot->enabledCheatNames[enabledNameIndex],
				sizeof(snapshot->enabledCheatNames[enabledNameIndex]),
				source->cheats[i].name != NULL ?
				source->cheats[i].name : "Unnamed cheat");
			++enabledNameIndex;
		}
	}
	snapshot->enabledCheatBytes = source->enabledCheatBytes;
	snapshot->cheatCapacityBytes = source->cheatCapacityBytes;
	if(snapshot->cheatCount == 0u) {
		snapshot->flags &= ~(uint32_t)UI_GAMEFLOW_DETAIL_CAN_CHEATS;
	}
	UIGameHistory_Format(snapshot->lastPlayedText, sizeof(snapshot->lastPlayedText),
		source->playHistoryAvailable, source->lastPlayedUnixSeconds);
	copyText(snapshot->saveStatusText, sizeof(snapshot->saveStatusText),
		UIGameHistory_SaveStatus(source->saveStatus));
	buildPresentation(snapshot);
	buildSettingsPresentation(snapshot, source->firstCustomSetting);
	buildSavesPresentation(snapshot, source->saveStats, source->savesAlways,
		source->savesWaiting, source->savesCardFailed, source->saveLine);
	buildSaveChoicePresentation(snapshot, source);
	buildManualPresentation(snapshot, source->manualPages);
	return true;
}

bool UIGameflowDetail_Matches(const uiGameflowDetailSnapshot_t *snapshot,
	uint32_t generation, uint32_t focusIndex, const char *gameId,
	size_t gameIdLength)
{
	return snapshot != NULL &&
		(snapshot->flags & UI_GAMEFLOW_DETAIL_VALID) != 0u &&
		snapshot->generation == generation &&
		snapshot->focusIndex == focusIndex &&
		gameId != NULL && gameIdLength == UI_GAMEFLOW_DETAIL_ID_LENGTH &&
		memcmp(snapshot->gameId, gameId,
			UI_GAMEFLOW_DETAIL_ID_LENGTH) == 0;
}

static bool focusable(uint32_t flags, int row)
{
	return row == UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH ||
		(row == UI_GAMEFLOW_DETAIL_FOCUS_CHEATS &&
			(flags & UI_GAMEFLOW_DETAIL_CAN_CHEATS) != 0u) ||
		(row == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS &&
			(flags & UI_GAMEFLOW_DETAIL_CAN_SETTINGS) != 0u) ||
		(row == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL &&
			(flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u);
}

uiGameflowDetailFocus_t UIGameflowDetail_MoveFocus(
	const uiGameflowDetailSnapshot_t *snapshot, uiGameflowDetailFocus_t focus,
	uint32_t input)
{
	int step = (input & UI_GAMEFLOW_DETAIL_INPUT_UP) != 0u ? 1 :
		(input & UI_GAMEFLOW_DETAIL_INPUT_DOWN) != 0u ? -1 : 0;
	int row;

	if(snapshot == NULL ||
		(snapshot->flags & UI_GAMEFLOW_DETAIL_VALID) == 0u || step == 0) {
		return focus;
	}
	for(row = (int)focus + step; row >= UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH &&
		row <= UI_GAMEFLOW_DETAIL_FOCUS_MANUAL; row += step) {
		if(focusable(snapshot->flags, row)) {
			return (uiGameflowDetailFocus_t)row;
		}
	}
	return focus;
}

uiGameflowDetailAction_t UIGameflowDetail_ResolveAction(
	const uiGameflowDetailSnapshot_t *snapshot, uiGameflowDetailFocus_t focus,
	uint32_t input)
{
	uint32_t flags;

	if(snapshot == NULL ||
		(snapshot->flags & UI_GAMEFLOW_DETAIL_VALID) == 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_NONE;
	}
	flags = snapshot->flags;
	if((input & UI_GAMEFLOW_DETAIL_INPUT_A) != 0u) {
		/* L is Clean Boot's modifier only where Clean Boot is offered. */
		if((input & UI_GAMEFLOW_DETAIL_INPUT_L) != 0u &&
			(flags & UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT) != 0u) {
			return UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT;
		}
		if(focus == UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH) {
			return UI_GAMEFLOW_DETAIL_ACTION_BOOT;
		}
		if(focus == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS &&
			(flags & UI_GAMEFLOW_DETAIL_CAN_SETTINGS) != 0u) {
			return UI_GAMEFLOW_DETAIL_ACTION_SETTINGS;
		}
		if(focus == UI_GAMEFLOW_DETAIL_FOCUS_CHEATS &&
			(flags & UI_GAMEFLOW_DETAIL_CAN_CHEATS) != 0u) {
			return UI_GAMEFLOW_DETAIL_ACTION_CHEATS;
		}
		if(focus == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL &&
			(flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u) {
			return UI_GAMEFLOW_DETAIL_ACTION_MANUAL;
		}
		return UI_GAMEFLOW_DETAIL_ACTION_NONE;
	}
	if((input & UI_GAMEFLOW_DETAIL_INPUT_B) != 0u &&
		(flags & UI_GAMEFLOW_DETAIL_CAN_LIBRARY) != 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_LIBRARY;
	}
	if((input & UI_GAMEFLOW_DETAIL_INPUT_R) != 0u &&
		(flags & UI_GAMEFLOW_DETAIL_CAN_VERIFY) != 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_VERIFY;
	}
	if((input & UI_GAMEFLOW_DETAIL_INPUT_X) != 0u &&
		(flags & UI_GAMEFLOW_DETAIL_CAN_SETTINGS) != 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_SETTINGS;
	}
	if((input & UI_GAMEFLOW_DETAIL_INPUT_Z) != 0u &&
		(flags & UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD) != 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD;
	}
	if((input & UI_GAMEFLOW_DETAIL_INPUT_Y) != 0u &&
		(flags & UI_GAMEFLOW_DETAIL_CAN_CHEATS) != 0u) {
		return UI_GAMEFLOW_DETAIL_ACTION_CHEATS;
	}
	return UI_GAMEFLOW_DETAIL_ACTION_NONE;
}
