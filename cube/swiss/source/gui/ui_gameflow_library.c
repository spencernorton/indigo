#include <string.h>

#include "ui_gameflow_library.h"
#include "ui_lang.h"

static unsigned char asciiLower(unsigned char value)
{
	return value >= 'A' && value <= 'Z' ?
		(unsigned char)(value + ('a' - 'A')) : value;
}

static size_t trimmedPathLength(const char *path)
{
	size_t length = path != NULL ? strlen(path) : 0u;
	while(length > 0u && path[length - 1u] == '/') {
		length--;
	}
	return length;
}

static bool asciiPrefixEquals(const char *left, const char *right,
	size_t length)
{
	size_t i;

	if(left == NULL || right == NULL) {
		return false;
	}
	for(i = 0u; i < length; ++i) {
		if(asciiLower((unsigned char)left[i]) !=
			asciiLower((unsigned char)right[i])) {
			return false;
		}
	}
	return true;
}

static bool suffixEquals(const char *name, const char *suffix)
{
	size_t nameLength;
	size_t suffixLength;
	size_t i;

	if(name == NULL || suffix == NULL) {
		return false;
	}
	nameLength = strlen(name);
	suffixLength = strlen(suffix);
	if(nameLength <= suffixLength) {
		return false;
	}
	for(i = 0u; i < suffixLength; ++i) {
		if(asciiLower((unsigned char)name[nameLength - suffixLength + i]) !=
			asciiLower((unsigned char)suffix[i])) {
			return false;
		}
	}
	return true;
}

bool UIGameflowLibrary_IsGameImageName(const char *name)
{
	const char *leafName = name != NULL ? strrchr(name, '/') : NULL;

	leafName = leafName != NULL ? leafName + 1 : name;
	if(leafName == NULL || strncmp(leafName, "._", 2u) == 0) {
		return false;
	}
	return suffixEquals(name, ".fdi") || suffixEquals(name, ".gcm") ||
		suffixEquals(name, ".iso") || suffixEquals(name, ".tgc");
}

/* ParseGameFolderName over the first length bytes of name. */
static bool parseGameFolderName(const char *name, size_t length,
	char gameId[7], char *title, size_t titleSize)
{
	size_t titleLength;
	size_t i;

	if(name == NULL) {
		return false;
	}
	if(length < 10u || name[length - 8u] != '[' || name[length - 1u] != ']' ||
		name[length - 9u] != ' ') {
		return false;
	}
	titleLength = length - 9u;
	if(titleLength == 0u || name[titleLength - 1u] == ' ' ||
		memchr(name, '/', length) != NULL || memchr(name, '\\', length) != NULL) {
		return false;
	}
	for(i = 0u; i < 6u; ++i) {
		unsigned char value = (unsigned char)name[length - 7u + i];
		if(!((value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9'))) {
			return false;
		}
	}

	if(gameId != NULL) {
		memcpy(gameId, &name[length - 7u], 6u);
		gameId[6] = '\0';
	}
	if(title != NULL && titleSize > 0u) {
		size_t copyLength = titleLength < titleSize - 1u ?
			titleLength : titleSize - 1u;
		memcpy(title, name, copyLength);
		title[copyLength] = '\0';
	}
	return true;
}

bool UIGameflowLibrary_ParseGameFolderName(const char *name, char gameId[7],
	char *title, size_t titleSize)
{
	return name != NULL &&
		parseGameFolderName(name, strlen(name), gameId, title, titleSize);
}

bool UIGameflowLibrary_EntryEligible(uiGameflowLibraryMode_t mode,
	uint32_t index, uiGameflowLibraryEntryType_t type, const char *name)
{
	if(type == UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL) {
		return index == 0u;
	}
	if(mode == UI_GAMEFLOW_LIBRARY_IMAGE_FILES) {
		return type == UI_GAMEFLOW_LIBRARY_ENTRY_FILE &&
			UIGameflowLibrary_IsGameImageName(name);
	}
	if(mode == UI_GAMEFLOW_LIBRARY_GAME_FOLDERS) {
		return type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY &&
			UIGameflowLibrary_ParseGameFolderName(name, NULL, NULL, 0u);
	}
	if(mode == UI_GAMEFLOW_LIBRARY_FOLDERS) {
		return type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY ||
			UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
				index, type, name);
	}
	return false;
}

uiGameflowLibraryLocation_t UIGameflowLibrary_Locate(
	const char *gamesRoot, const char *currentPath)
{
	size_t rootLength = trimmedPathLength(gamesRoot);
	size_t pathLength = trimmedPathLength(currentPath);
	const char *leaf;

	if(rootLength == 0u || pathLength < rootLength ||
		!asciiPrefixEquals(gamesRoot, currentPath, rootLength)) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
	}
	if(pathLength == rootLength) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_ROOT;
	}
	if(pathLength <= rootLength + 1u || currentPath[rootLength] != '/' ||
		currentPath[pathLength] != '\0') {
		return UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
	}
	leaf = &currentPath[rootLength + 1u];
	if(memchr(leaf, '/', pathLength - rootLength - 1u) != NULL ||
		!UIGameflowLibrary_ParseGameFolderName(leaf, NULL, NULL, 0u)) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
	}
	return UI_GAMEFLOW_LIBRARY_LOCATION_STRICT_LEAF;
}

uiGameflowLibraryLocation_t UIGameflowLibrary_LocateFolders(
	const char *gamesRoot, const char *currentPath)
{
	uiGameflowLibraryLocation_t location =
		UIGameflowLibrary_Locate(gamesRoot, currentPath);
	size_t rootLength = trimmedPathLength(gamesRoot);
	size_t pathLength = trimmedPathLength(currentPath);
	const char *leaf;
	const char *slash;

	if(location == UI_GAMEFLOW_LIBRARY_LOCATION_ROOT) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT;
	}
	if(location == UI_GAMEFLOW_LIBRARY_LOCATION_STRICT_LEAF) {
		return location;
	}
	if(rootLength == 0u || pathLength <= rootLength + 1u ||
		!asciiPrefixEquals(gamesRoot, currentPath, rootLength) ||
		currentPath[rootLength] != '/' || currentPath[pathLength] != '\0') {
		return UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
	}
	leaf = &currentPath[rootLength + 1u];
	slash = memchr(leaf, '/', pathLength - rootLength - 1u);
	if(slash == NULL) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER;
	}
	/* Only a folder of games holds a second level: one in a game folder
	 * is that game's, and anything deeper is listed in the second level. */
	if(slash == leaf || slash[1] == '\0' ||
		memchr(slash + 1, '/', (size_t)(&currentPath[pathLength] - slash - 1)) != NULL ||
		parseGameFolderName(leaf, (size_t)(slash - leaf), NULL, NULL, 0u)) {
		return UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
	}
	return UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER;
}

bool UIGameflowLibrary_IsInsideFolder(uiGameflowLibraryLocation_t location)
{
	return location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER ||
		location == UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER;
}

bool UIGameflowLibrary_IsFolderPicture(const char *folder, const char *file)
{
	size_t length = trimmedPathLength(folder);

	return length > 0u && file != NULL && strlen(file) == length + 4u &&
		asciiPrefixEquals(folder, file, length) &&
		asciiPrefixEquals(".png", &file[length], 4u);
}

/* Copies count bytes of a path into heading at length, in capitals and with
 * each '/' as " / ", stopping while one byte is left for the NUL. */
static size_t headingPut(char *heading, size_t size, size_t length,
	const char *text, size_t count)
{
	size_t i;

	for(i = 0u; i < count && length + 1u < size; ++i) {
		unsigned char c = (unsigned char)text[i];

		if(c == '/') {
			const char *separator = " / ";
			size_t k;

			for(k = 0u; k < 3u && length + 1u < size; ++k) {
				heading[length++] = separator[k];
			}
			continue;
		}
		heading[length++] = (char)(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
	}
	return length;
}

bool UIGameflowLibrary_FolderHeading(const char *gamesRoot,
	const char *currentPath, char *heading, size_t size)
{
	size_t rootLength = trimmedPathLength(gamesRoot);
	size_t pathLength = trimmedPathLength(currentPath);
	const char *folder;
	const char *last;
	size_t folderLength;
	size_t full;
	size_t length = 0u;
	size_t i;

	if(heading == NULL || size == 0u) {
		return false;
	}
	heading[0] = '\0';
	if(size < 8u || rootLength == 0u || pathLength <= rootLength + 1u ||
		!asciiPrefixEquals(gamesRoot, currentPath, rootLength) ||
		currentPath[rootLength] != '/') {
		return false;
	}
	folder = &currentPath[rootLength + 1u];
	folderLength = pathLength - rootLength - 1u;
	last = folder;
	full = folderLength;
	for(i = 0u; i < folderLength; ++i) {
		if(folder[i] == '/') {
			last = &folder[i + 1u];
			full += 2u;
		}
	}
	if(full < size) {
		length = headingPut(heading, size, 0u, folder, folderLength);
	}
	else {
		size_t lastLength = (size_t)(&folder[folderLength] - last);

		if(last != folder) {
			heading[length++] = '\205';
			length = headingPut(heading, size, length, "/", 1u);
		}
		if(length + lastLength < size) {
			length = headingPut(heading, size, length, last, lastLength);
		}
		else {
			size_t start = length;

			length = headingPut(heading, size - 1u, length, last, lastLength);
			/* The ellipsis never follows half a Cyrillic letter. */
			length = start + UILang_Head(last, length - start);
			heading[length++] = '\205';
		}
	}
	heading[length] = '\0';
	return true;
}

bool UIGameflowLibrary_IsGamesRootEntry(const char *gamesRoot,
	uiGameflowLibraryEntryType_t type, const char *entryPath)
{
	return type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY &&
		UIGameflowLibrary_Locate(gamesRoot, entryPath) ==
			UI_GAMEFLOW_LIBRARY_LOCATION_ROOT;
}

bool UIGameflowLibrary_ShouldStartHome(bool hasAutoload, bool hasRecent,
	bool recentAutoEnabled)
{
	return !hasAutoload && !(hasRecent && recentAutoEnabled);
}

void UIGameflowLibrary_ClassifierInit(uiGameflowLibraryClassifier_t *state,
	uiGameflowLibraryLocation_t location)
{
	if(state == NULL) {
		return;
	}
	memset(state, 0, sizeof(*state));
	state->location = location;
	state->valid = location != UI_GAMEFLOW_LIBRARY_LOCATION_NONE;
}

bool UIGameflowLibrary_ClassifierAdd(uiGameflowLibraryClassifier_t *state,
	uiGameflowLibraryEntryType_t type, const char *name)
{
	uint32_t index;

	if(state == NULL || !state->valid) {
		return false;
	}
	index = state->entryCount++;
	if(type == UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL) {
		state->valid = index == 0u;
		return state->valid;
	}
	if(UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_IMAGE_FILES,
		index, type, name)) {
		state->imageCount++;
	}
	else if((state->location == UI_GAMEFLOW_LIBRARY_LOCATION_ROOT ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER) &&
		UIGameflowLibrary_EntryEligible(UI_GAMEFLOW_LIBRARY_GAME_FOLDERS,
			index, type, name)) {
		state->folderCount++;
	}
	else if((state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER) &&
		type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY) {
		state->subfolderCount++;
	}
	/* Anything else isn't a game: the Library skips it. */
	return true;
}

uiGameflowLibraryMode_t UIGameflowLibrary_ClassifierFinish(
	const uiGameflowLibraryClassifier_t *state)
{
	if(state == NULL || !state->valid) {
		return UI_GAMEFLOW_LIBRARY_NONE;
	}
	if(state->folderCount + state->subfolderCount > 0u &&
		(state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER)) {
		return UI_GAMEFLOW_LIBRARY_FOLDERS;
	}
	if(state->imageCount > 0u && state->folderCount == 0u) {
		return UI_GAMEFLOW_LIBRARY_IMAGE_FILES;
	}
	if(state->folderCount > 0u && state->imageCount == 0u) {
		return UI_GAMEFLOW_LIBRARY_GAME_FOLDERS;
	}
	/* With Library Folders on, /games and its folders stay in the Library
	 * even when empty or holding only pictures, with just their way back. */
	if(state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDERS_ROOT ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_FOLDER ||
		state->location == UI_GAMEFLOW_LIBRARY_LOCATION_SUBFOLDER) {
		return UI_GAMEFLOW_LIBRARY_IMAGE_FILES;
	}
	return UI_GAMEFLOW_LIBRARY_NONE;
}

uiGameflowLibraryMode_t UIGameflowLibrary_EntryMode(
	uiGameflowLibraryMode_t mode, uiGameflowLibraryEntryType_t type,
	const char *name)
{
	if(mode != UI_GAMEFLOW_LIBRARY_FOLDERS) {
		return mode;
	}
	if(type == UI_GAMEFLOW_LIBRARY_ENTRY_FILE &&
		UIGameflowLibrary_IsGameImageName(name)) {
		return UI_GAMEFLOW_LIBRARY_IMAGE_FILES;
	}
	if(type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY &&
		UIGameflowLibrary_ParseGameFolderName(name, NULL, NULL, 0u)) {
		return UI_GAMEFLOW_LIBRARY_GAME_FOLDERS;
	}
	return UI_GAMEFLOW_LIBRARY_NONE;
}

bool UIGameflowLibrary_UsesRetainedDetail(uiGameflowLibraryMode_t mode,
	uiGameflowLibraryEntryType_t type)
{
	return (mode == UI_GAMEFLOW_LIBRARY_GAME_FOLDERS &&
			type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY) ||
		(mode == UI_GAMEFLOW_LIBRARY_IMAGE_FILES &&
			type == UI_GAMEFLOW_LIBRARY_ENTRY_FILE);
}

uiGameflowLibraryArtwork_t UIGameflowLibrary_ChooseArtwork(
	bool posterReady, bool bannerReady)
{
	if(posterReady) {
		return UI_GAMEFLOW_LIBRARY_ART_POSTER;
	}
	if(bannerReady) {
		return UI_GAMEFLOW_LIBRARY_ART_BANNER;
	}
	return UI_GAMEFLOW_LIBRARY_ART_EMBLEM;
}

bool UIGameflowLibrary_BuildFallbackLayout(float visualSlot,
	uiGameflowLibraryFallbackLayout_t *layout)
{
	float distance;

	if(layout == NULL || visualSlot != visualSlot) {
		return false;
	}
	distance = visualSlot < 0.0f ? -visualSlot : visualSlot;
	if(distance >= 3.0f) {
		return false;
	}

	layout->contentInset = 0.055f;
	layout->bannerLeft = 0.12f;
	layout->bannerTop = 0.145f;
	layout->bannerRight = 0.88f;
	layout->bannerBottom = 0.335f;
	layout->motifCenterX = 0.50f;
	layout->motifCenterY = 0.36f;
	layout->motifRadius = 0.16f;
	layout->identityTop = 0.655f;
	layout->idBaseline = 0.765f;
	layout->regionBaseline = 0.855f;
	/* Identity belongs only to the centered card. During travel it fades
	 * before the neighboring card can claim the same visual hierarchy. */
	layout->identityAlpha = 1.0f - distance * 2.5f;
	if(layout->identityAlpha < 0.0f) {
		layout->identityAlpha = 0.0f;
	}
	return true;
}

const char *UIGameflowLibrary_RegionLabel(const char *gameId)
{
	size_t i;
	char region;

	if(gameId == NULL) {
		return "GAMECUBE";
	}
	for(i = 0u; i < 4u; ++i) {
		if(gameId[i] == '\0') {
			return "GAMECUBE";
		}
	}
	region = gameId[3];
	switch(region) {
		case 'E':
			return "NTSC-U";
		case 'J':
			return "NTSC-J";
		case 'K':
		case 'T':
			return "NTSC-K";
		case 'P':
		case 'D':
		case 'F':
		case 'H':
		case 'I':
		case 'L':
		case 'M':
		case 'R':
		case 'S':
		case 'U':
		case 'X':
		case 'Y':
			return "PAL";
		case 'A':
			return "REGION FREE";
		case 'C':
		case 'W':
			return "NTSC";
		default:
			return "GAMECUBE";
	}
}

size_t UIGameflowLibrary_BuildWindow(uint32_t itemCount,
	uint32_t selectedIndex, uiGameflowDirection_t directionHint,
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_WINDOW])
{
	static const int8_t relativeOrder[UI_GAMEFLOW_LIBRARY_WINDOW] = {
		0, -1, 1, -2, 2, -3, 3, -4, 4
	};
	size_t count = 0u;
	size_t i;

	if(slots == NULL || itemCount == 0u) {
		return 0u;
	}
	if(selectedIndex >= itemCount) {
		selectedIndex = itemCount - 1u;
	}

	for(i = 0u; i < UI_GAMEFLOW_LIBRARY_WINDOW; ++i) {
		int8_t relativeSlot = relativeOrder[i];
		int64_t candidate;
		uint32_t index;
		size_t j;

		if(itemCount == 2u && i == 1u &&
			directionHint == UI_GAMEFLOW_DIRECTION_PREVIOUS) {
			relativeSlot = 1;
		}
		candidate = (int64_t)selectedIndex + relativeSlot;

		while(candidate < 0) {
			candidate += itemCount;
		}
		index = (uint32_t)(candidate % itemCount);
		for(j = 0u; j < count; ++j) {
			if(slots[j].index == index) {
				break;
			}
		}
		if(j != count) {
			continue;
		}
		slots[count].index = index;
		slots[count].relativeSlot = relativeSlot;
		slots[count].column = 0u;
		if(++count == itemCount) {
			break;
		}
	}
	return count;
}

static uint32_t gridRows(uint32_t itemCount, uint32_t columns)
{
	return itemCount / columns + (itemCount % columns != 0u ? 1u : 0u);
}

bool UIGameflowLibrary_Move(uint32_t itemCount, uint32_t columns,
	uint32_t selectedIndex, uiGameflowLibraryMove_t move, uint32_t page,
	uiGameflowLibraryStep_t *step)
{
	uint32_t last;
	uint32_t rows;
	uint32_t row;
	uint32_t target;

	if(step == NULL || itemCount == 0u) {
		return false;
	}
	last = itemCount - 1u;
	if(selectedIndex > last) {
		selectedIndex = last;
	}
	if(move == UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS) {
		target = selectedIndex == 0u ? last : selectedIndex - 1u;
	}
	else if(move == UI_GAMEFLOW_LIBRARY_MOVE_NEXT) {
		target = selectedIndex == last ? 0u : selectedIndex + 1u;
	}
	else if(columns == 0u) {
		/* A ring pages by cards. */
		if(move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK) {
			target = selectedIndex == 0u ? last :
				(selectedIndex > page ? selectedIndex - page : 0u);
		}
		else if(move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON) {
			target = selectedIndex == last ? 0u :
				(page > last - selectedIndex ? last : selectedIndex + page);
		}
		else {
			return false;
		}
	}
	else {
		/* A grid moves by rows and keeps the column. */
		rows = gridRows(itemCount, columns);
		row = selectedIndex / columns;
		switch(move) {
			case UI_GAMEFLOW_LIBRARY_MOVE_UP:
				row = row == 0u ? rows - 1u : row - 1u;
				break;
			case UI_GAMEFLOW_LIBRARY_MOVE_DOWN:
				row = row == rows - 1u ? 0u : row + 1u;
				break;
			case UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK:
				row = row == 0u ? rows - 1u : (row > page ? row - page : 0u);
				break;
			case UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON:
				row = row == rows - 1u ? 0u :
					(page > rows - 1u - row ? rows - 1u : row + page);
				break;
			default:
				return false;
		}
		target = row * columns + selectedIndex % columns;
		if(target > last) {
			target = last;
		}
	}
	step->index = target;
	step->direction = move == UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_UP ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK ?
		UI_GAMEFLOW_DIRECTION_PREVIOUS : UI_GAMEFLOW_DIRECTION_NEXT;
	step->snap = move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK ||
		move == UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
	return true;
}

size_t UIGameflowLibrary_BuildGridWindow(uint32_t itemCount,
	uint32_t selectedIndex, uint32_t columns,
	uiGameflowDirection_t rowDirection,
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_GRID_WINDOW])
{
	static const int8_t rowOrder[5] = {0, -1, 1, -2, 2};
	uint32_t placed[5];
	uint32_t rows;
	uint32_t focusRow;
	uint32_t focusColumn;
	size_t placedCount = 0u;
	size_t count = 0u;
	size_t i;

	if(slots == NULL || itemCount == 0u || columns == 0u ||
		columns > UI_GAMEFLOW_LIBRARY_GRID_COLUMNS) {
		return 0u;
	}
	if(selectedIndex >= itemCount) {
		selectedIndex = itemCount - 1u;
	}
	rows = gridRows(itemCount, columns);
	focusRow = selectedIndex / columns;
	focusColumn = selectedIndex % columns;
	for(i = 0u; i < sizeof(rowOrder); ++i) {
		int8_t relativeRow = rowOrder[i];
		uint32_t row = (uint32_t)(((int64_t)focusRow + relativeRow +
			2 * (int64_t)rows) % rows);
		uint32_t step;
		size_t j;

		if(rows == 2u && (relativeRow == -1 || relativeRow == 1)) {
			int8_t side = rowDirection == UI_GAMEFLOW_DIRECTION_NEXT ? -1 :
				rowDirection == UI_GAMEFLOW_DIRECTION_PREVIOUS ? 1 :
				(focusRow == 0u ? 1 : -1);
			if(relativeRow != side) {
				continue;
			}
		}
		for(j = 0u; j < placedCount && placed[j] != row; ++j) {
		}
		if(j != placedCount) {
			continue;
		}
		placed[placedCount++] = row;
		/* The selected column, then outwards: left before right. */
		for(step = 0u; step < 2u * columns; ++step) {
			uint32_t offset = (step + 1u) / 2u;
			uint32_t column;
			uint32_t index;

			if(step % 2u == 1u) {
				if(offset > focusColumn) {
					continue;
				}
				column = focusColumn - offset;
			}
			else {
				column = focusColumn + offset;
				if(column >= columns) {
					continue;
				}
			}
			index = row * columns + column;
			if(index >= itemCount) {
				continue;
			}
			slots[count].index = index;
			slots[count].relativeSlot = relativeRow;
			slots[count].column = (uint8_t)column;
			count++;
		}
	}
	return count;
}
