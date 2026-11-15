/* apps.c - Apps and Emulators (Home > Apps, Home > Emulators)

   The programs in /apps on the source, and in the folders in it, shown the
   way the Library shows games: its layouts, cards, controls and launch
   screen, with each app's own picture as its poster, or its name when it
   has none. ui_apps decides what is an app, card_art makes the posters
   (with ui_png), and Swiss's boot_dol starts the app,
   reading its .cli arguments and offering its .dcp choices as it does from
   the file list. Emulators is the same screen over /emulators. This file
   reads the card and runs the screen. */

#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <gccore.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "files.h"
#include "util.h"
#include "input.h"
#include "ui_apps.h"
#include "ui_gameflow_library.h"
#include "ui_menu_input.h"
#include "ui_png.h"
#include "card_art.h"
#include "ui_scene.h"
#include "apps.h"

/* A screen of programs: the folder in the source's root it lists, its
 * heading over the cards (empty: APPS), what it says when the folder has
 * none, and the program last selected, by path, so it opens where it was
 * left. */
typedef struct {
	const char *folder;
	const char *heading;
	const char *empty;
	char lastProgram[UI_APPS_PATH_LENGTH];
} appsScreen_t;

static appsScreen_t appsScreen = {"apps", "", "No apps in /apps on this device", ""};
static appsScreen_t emulatorsScreen = {"emulators", "EMULATORS",
	"No emulators in /emulators on this device", ""};
/* The screen showing, or last shown. */
static appsScreen_t *screen = &appsScreen;

/* The list shown and its device, and each app's program and picture as
 * the device listed them (two a app, the picture's empty without one). */
static uiApp_t *apps;
static u32 appCount;
static DEVICEHANDLER_INTERFACE *appsDevice;
static file_handle *appsFiles;
static u32 snapshotGeneration;

/* A copy of a listed entry, nothing of it open: every device reads a file or
 * a folder from its own entry (the disc drive by position, not by path). */
static void takeEntry(file_handle *out, const file_handle *entry)
{
	memcpy(out, entry, sizeof(*out));
	out->offset = 0;
	out->fp = NULL;
	out->ffsFp = NULL;
	out->meta = NULL;
	out->uiObj = NULL;
	out->lockCount = 0;
	out->thread = LWP_THREAD_NULL;
}

/* A folder's listing, and the same as ui_apps takes it: each entry borrows
 * its name, and points at, its file_handle in files. */
typedef struct {
	file_handle *files;
	uiAppsEntry_t *entries;
	size_t count;
} appsFolder_t;

static void freeFolder(appsFolder_t *folder)
{
	free(folder->entries);
	free(folder->files);
	memset(folder, 0, sizeof(*folder));
}

/* Lists the folder dir names on device; false for one that can't be read.
 * Every entry names device, whether or not the device's listing does. */
static bool listFolder(DEVICEHANDLER_INTERFACE *device, file_handle *dir,
	appsFolder_t *out)
{
	s32 count;
	s32 i;

	memset(out, 0, sizeof(*out));
	dir->device = device;
	count = device->readDir(dir, &out->files, -1);
	if(count <= 0 || out->files == NULL ||
		(out->entries = calloc((size_t)count, sizeof(uiAppsEntry_t))) == NULL) {
		freeFolder(out);
		return false;
	}
	for(i = 0; i < count; ++i) {
		file_handle *file = &out->files[i];
		uiAppsEntry_t *entry = &out->entries[out->count];

		if(file->fileType != IS_FILE && file->fileType != IS_DIR) {
			continue;	/* ".." */
		}
		file->device = device;
		entry->name = getRelativeName(file->name);
		entry->folder = file->fileType == IS_DIR;
		entry->hidden = (file->fileAttrib & ATTRIB_HIDDEN) != 0;
		entry->size = (uint32_t)file->size;
		entry->handle = file;
		out->count++;
	}
	return true;
}

/* device's folder name, found in its root as its own file list finds it. */
static bool findAppsFolder(DEVICEHANDLER_INTERFACE *device, const char *name,
	file_handle *out)
{
	file_handle root;
	appsFolder_t top;
	bool found = false;
	size_t i;

	takeEntry(&root, device->initial);
	if(!listFolder(device, &root, &top)) {
		return false;
	}
	for(i = 0; i < top.count && !found; ++i) {
		if(top.entries[i].folder && strcasecmp(top.entries[i].name, name) == 0) {
			takeEntry(out, top.entries[i].handle);
			found = true;
		}
	}
	freeFolder(&top);
	return found;
}

/* The apps in device's folder name, sorted, at most max of them; NULL for
 * none. Reads the folder, then each folder in it until the list is full.
 * With files, also each app's program and picture entries, two an app. */
static uiApp_t *scanApps(DEVICEHANDLER_INTERFACE *device, const char *name,
	size_t max, u32 *count, file_handle **files)
{
	file_handle dir;
	appsFolder_t top;
	appsFolder_t *folders = NULL;
	size_t folderCount = 0u;
	uiApp_t *list = NULL;
	size_t found = 0u;
	size_t i;
	bool room;

	*count = 0u;
	if(files != NULL) *files = NULL;
	if(device == NULL || device->initial == NULL || device->readDir == NULL ||
		!findAppsFolder(device, name, &dir) || !listFolder(device, &dir, &top)) {
		return NULL;
	}
	if((list = malloc(max * sizeof(uiApp_t))) == NULL ||
		(folders = calloc(top.count ? top.count : 1u, sizeof(appsFolder_t))) == NULL) {
		free(list);
		freeFolder(&top);
		return NULL;
	}
	room = UIApps_AddFolder(list, &found, max, "", top.entries, top.count);
	for(i = 0u; room && i < top.count; ++i) {
		const uiAppsEntry_t *entry = &top.entries[i];

		if(!entry->folder || entry->hidden || !UIApps_IsFolder(entry->name)) {
			continue;
		}
		takeEntry(&dir, entry->handle);
		if(listFolder(device, &dir, &folders[folderCount])) {
			room = UIApps_AddFolder(list, &found, max, entry->name,
				folders[folderCount].entries, folders[folderCount].count);
			folderCount++;
		}
	}
	UIApps_Sort(list, found);
	/* Keep what the listings say of each app before they go. */
	if(files != NULL && found > 0u &&
		(*files = calloc(found * 2u, sizeof(file_handle))) != NULL) {
		for(i = 0u; i < found; ++i) {
			takeEntry(&(*files)[i * 2u], list[i].programHandle);
			if(list[i].pictureHandle != NULL) {
				takeEntry(&(*files)[i * 2u + 1u], list[i].pictureHandle);
			}
		}
	}
	for(i = 0u; i < found; ++i) {
		list[i].programHandle = list[i].pictureHandle = NULL;
	}
	for(i = 0u; i < folderCount; ++i) {
		freeFolder(&folders[i]);
	}
	free(folders);
	freeFolder(&top);
	if(found == 0u || (files != NULL && *files == NULL)) {
		free(list);
		return NULL;
	}
	*count = (u32)found;
	return list;
}

static bool programsAvailable(DEVICEHANDLER_INTERFACE *device,
	const appsScreen_t *which)
{
	u32 count;
	uiApp_t *one = scanApps(device, which->folder, 1u, &count, NULL);

	free(one);
	return count > 0u;
}

bool apps_available(DEVICEHANDLER_INTERFACE *device)
{
	return programsAvailable(device, &appsScreen);
}

bool emulators_available(DEVICEHANDLER_INTERFACE *device)
{
	return programsAvailable(device, &emulatorsScreen);
}

/* Apps' cards for card_art: card i is apps[i], and its picture the file
 * the device listed beside its program. */
static uint32_t appPictureSize(int32_t card)
{
	const uiApp_t *app = &apps[card];

	return app->picture[0] == '\0' || app->pictureFailed ? 0u : app->pictureSize;
}

static bool appReadPicture(int32_t card, uint32_t offset, uint8_t *data, uint32_t size)
{
	file_handle file;
	s32 read;

	takeEntry(&file, &appsFiles[(u32)card * 2u + 1u]);
	file.offset = offset;
	read = appsDevice->readFile(&file, data, size);
	appsDevice->closeFile(&file);
	return read == (s32)size;
}

static const char *appName(int32_t card)
{
	return apps[card].name;
}

/* A picture that made no poster isn't tried again. */
static void appVerdict(int32_t card, bool ok)
{
	apps[card].pictureFailed = !ok;
}

/* The Library's snapshot of the cards around selected, and their apps,
 * nearest first, for posters: each app's picture, or its name. */
static bool buildSnapshot(uiGameflowRenderSnapshot_t *snapshot,
	u32 selected, uiGameflowLayout_t layout, uiGameflowDirection_t direction,
	uiGameflowDirection_t rowDirection, bool snap,
	int32_t want[UI_APPS_ART_SLOTS], u32 *wanted)
{
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_GRID_WINDOW];
	size_t count, i;

	memset(snapshot, 0, offsetof(uiGameflowRenderSnapshot_t, records));
	snprintf(snapshot->folder, sizeof(snapshot->folder), "%s", screen->heading);
	snapshot->selection.generation = ++snapshotGeneration;
	snapshot->selection.itemCount = appCount;
	snapshot->selection.selectedIndex = selected;
	snapshot->selection.directionHint = direction;
	snapshot->selection.snapTransition = snap;
	snprintf(snapshot->deviceName, sizeof(snapshot->deviceName), "%s",
		DeviceDisplayName(appsDevice));
	snapshot->layout = (u8)layout;
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		snapshot->columns = UI_GAMEFLOW_LIBRARY_GRID_COLUMNS;
		count = UIGameflowLibrary_BuildGridWindow(appCount, selected,
			UI_GAMEFLOW_LIBRARY_GRID_COLUMNS, rowDirection, slots);
	}
	else {
		count = UIGameflowLibrary_BuildWindow(appCount, selected, direction,
			slots);
	}
	snapshot->recordCount = (u32)count;
	*wanted = 0u;
	for(i = 0u; i < count; ++i) {
		uiGameflowCardSnapshot_t *record = &snapshot->records[i];
		uiApp_t *app = &apps[slots[i].index];
		char size[32] = "";

		memset(record, 0, sizeof(*record));
		record->libraryIndex = slots[i].index;
		record->relativeSlot = slots[i].relativeSlot;
		record->column = slots[i].column;
		record->flags = UI_GAMEFLOW_CARD_VALID | UI_GAMEFLOW_CARD_APP;
		record->size = app->size;
		snprintf(record->title, sizeof(record->title), "%s", app->name);
		snprintf(record->company, sizeof(record->company), "%s", app->program);
		formatBytes(size, app->size, 0,
			!(appsDevice->location & LOC_SYSTEM));
		snprintf(record->facts, sizeof(record->facts), "%s  |  %s",
			UIApps_TypeLabel((uiAppsType_t)app->type), size);
		if(*wanted < UI_APPS_ART_SLOTS) {
			want[(*wanted)++] = (int32_t)slots[i].index;
		}
	}
	return count > 0u;
}

/* A launch from the card: the launch screen, then boot_dol, which returns
 * only when the app could not start. */
static void launchApp(uiDrawObj_t *panel, u32 index)
{
	uiDrawObj_t *box;

	/* The DOL needs the device and the memory: no poster is made meanwhile. */
	CardArt_Pause();
	takeEntry(&curFile, &appsFiles[index * 2u]);
	DrawSetGameflowMode(panel, UI_GAMEFLOW_MODE_LAUNCH);
	while(padsButtonsHeld() & BUTTON_A) {
		VIDEO_WaitVSync();
	}
	/* The Recent list keeps curFile, as a start from the file list does. */
	boot_dol(&curFile, 0, NULL);
	box = DrawPublish(DrawMessageBox(D_WARN, "This app couldn't start"));
	sleep(2);
	DrawDispose(box);
	DrawSetGameflowMode(panel, UI_GAMEFLOW_MODE_LIBRARY);
	CardArt_Resume();
}

/* One sample's presses in layout, as the Library takes them (its browser in
 * swiss.c), moving *selected among the apps. */
static void moveSelection(uiGameflowLayout_t layout, u32 buttons,
	uiMenuInputDirection_t analog, u32 *selected,
	uiGameflowDirection_t *direction, uiGameflowDirection_t *rowDirection,
	bool *snap)
{
	bool left = (buttons & BUTTON_LEFT) || analog == UI_MENU_INPUT_LEFT;
	bool right = (buttons & BUTTON_RIGHT) || analog == UI_MENU_INPUT_RIGHT;
	bool up = (buttons & BUTTON_UP) || analog == UI_MENU_INPUT_UP;
	bool down = (buttons & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN;
	bool pageBack = (buttons & BUTTON_L) != 0u;
	bool pageOn = (buttons & BUTTON_R) != 0u;
	u32 columns = layout == UI_GAMEFLOW_LAYOUT_GRID ?
		UI_GAMEFLOW_LIBRARY_GRID_COLUMNS : 0u;
	uiGameflowLibraryMove_t moves[6];
	int moveCount = 0;
	int move;

	if(layout == UI_GAMEFLOW_LAYOUT_VERTICAL) {
		/* The column: up and down step, left and right page. */
		if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
		if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
		if(left || pageBack) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
		if(right || pageOn) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
	}
	else if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		/* The grid: every direction steps, L and R page. */
		if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
		if(right) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
		if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_UP;
		if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_DOWN;
		if(pageBack) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
		if(pageOn) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
	}
	else {
		/* The carousel: left and right step, up and down page. */
		if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
		if(right) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
		if(up || pageBack) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
		if(down || pageOn) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
	}
	for(move = 0; move < moveCount; ++move) {
		uiGameflowLibraryStep_t step;

		if(!UIGameflowLibrary_Move(appCount, columns, *selected, moves[move],
			columns ? UI_GAMEFLOW_LIBRARY_GRID_ROWS : FILES_PER_PAGE_CAROUSEL,
			&step)) {
			continue;
		}
		if(columns && step.index / columns != *selected / columns) {
			*rowDirection = step.direction;
		}
		*selected = step.index;
		*direction = step.direction;
		*snap = step.snap;
	}
}

static void showPrograms(appsScreen_t *which)
{
	const u32 waitButtons = BUTTON_A | BUTTON_B | BUTTON_UP | BUTTON_DOWN |
		BUTTON_LEFT | BUTTON_RIGHT | BUTTON_L | BUTTON_R;
	uiGameflowRenderSnapshot_t *snapshot = NULL;
	uiDrawObj_t *panel = NULL;
	uiMenuInputState_t menuInput;
	uiGameflowLayout_t layout = gameflowLayout();
	uiGameflowDirection_t direction = UI_GAMEFLOW_DIRECTION_NONE;
	uiGameflowDirection_t rowDirection = UI_GAMEFLOW_DIRECTION_NONE;
	bool snap = false;
	u32 menuInputRetrace;
	u32 selected = 0u;
	u32 i;

	/* Game stills, discs and retail cases do not apply to Apps: keep their
	 * existing flat carousel when one of those layouts is selected. */
	if(layout > UI_GAMEFLOW_LAYOUT_GRID) {
		layout = UI_GAMEFLOW_LAYOUT_HORIZONTAL;
	}
	/* The cube makes way as soon as A is pressed, while the card is read:
	 * a disc or a network share takes a moment for each folder. */
	UIScene_RequestLibraryLayout(layout);
	UIScene_Request(UI_SCENE_LIBRARY);
	screen = which;
	appsDevice = devices[DEVICE_CUR];
	apps = scanApps(appsDevice, which->folder, UI_APPS_MAX, &appCount, &appsFiles);
	if(apps == NULL) {
		/* The card changed since Home looked. */
		uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_INFO,
			which->empty));
		sleep(2);
		DrawDispose(box);
		return;
	}
	for(i = 0u; i < appCount; ++i) {
		if(strcmp(apps[i].program, which->lastProgram) == 0) {
			selected = i;
		}
	}
	snapshot = memalign(32, sizeof(*snapshot));
	{
		cardArtSource_t cards = {appPictureSize, appReadPicture, appName,
			appVerdict, (appsDevice->features & FEAT_THREAD_SAFE) != 0u};

		CardArt_Open(&cards);
	}
	UIMenuInput_Init(&menuInput);
	menuInputRetrace = VIDEO_GetRetraceCount();
	/* Home opens Apps on A's press and calls this before that A is let go,
	 * and a card is read in a moment: the loop below would take the held A
	 * as a start. Wait for it to go up, as Home does before the Library. */
	while(padsButtonsHeld() & waitButtons) {
		(void)padsMenuInputPoll(&menuInput,
			menuInputElapsedMicroseconds(&menuInputRetrace),
			gameflowMenuInputPolicy(layout), true);
		VIDEO_WaitVSync();
	}
	while(snapshot != NULL) {
		int32_t want[UI_APPS_ART_SLOTS];
		u32 wanted = 0u;
		u32 policy = gameflowMenuInputPolicy(layout);
		uiMenuInputDirection_t analog;
		u32 buttons;

		if(!buildSnapshot(snapshot, selected, layout, direction, rowDirection,
			snap, want, &wanted)) {
			break;
		}
		direction = UI_GAMEFLOW_DIRECTION_NONE;
		snap = false;
		if(panel == NULL || !DrawUpdateGameflow(panel, snapshot)) {
			uiDrawObj_t *fresh = DrawGameflow(snapshot);

			if(fresh == NULL) {
				break;
			}
			panel = panel == NULL ? DrawPublish(fresh) :
				DrawRepublish(panel, fresh);
		}
		CardArt_Want(want, wanted);
		/* Presses from before (the A that launched an app) aren't for here. */
		(void)padsButtonsTaken(waitButtons);
		for(;;) {
			/* Taken from the scans as well as held: a press made and let go
			 * while a card was read is still seen. */
			buttons = padsButtonsHeld() | padsButtonsTaken(waitButtons);
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace), policy,
				(buttons & waitButtons) != 0u);
			if((buttons & waitButtons) != 0u || analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
			CardArt_Poll();
		}
		if(buttons & BUTTON_B) {
			break;
		}
		if(buttons & BUTTON_A) {
			launchApp(panel, selected);
			UIMenuInput_Init(&menuInput);
			menuInputRetrace = VIDEO_GetRetraceCount();
		}
		else {
			moveSelection(layout, buttons, analog, &selected, &direction,
				&rowDirection, &snap);
		}
		while(padsButtonsHeld() & waitButtons) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace), policy, true);
			VIDEO_WaitVSync();
		}
	}
	snprintf(which->lastProgram, sizeof(which->lastProgram), "%s",
		apps[selected].program);
	CardArt_Pause();
	DrawDispose(panel);
	CardArt_Close();
	free(snapshot);
	free(apps);
	free(appsFiles);
	apps = NULL;
	appsFiles = NULL;
	appCount = 0u;
}

void show_apps(void)
{
	showPrograms(&appsScreen);
}

void show_emulators(void)
{
	showPrograms(&emulatorsScreen);
}
