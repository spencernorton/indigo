/*
*
*   Swiss - The Gamecube IPL replacement
*
*/

#include <argz.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdarg.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <ogc/color.h>
#include <ogc/exi.h>
#include <ogc/usbgecko.h>
#include <ogc/video_types.h>
#include <sdcard/card_cmn.h>
#include <ogc/lwp_threads.h>
#include <ogc/machine/processor.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <psoarchive/PRS.h>
#include <xxhash.h>
#include <zlib.h>

#include "swiss.h"
#include "main.h"
#include "util.h"
#include "info.h"
#include "saves.h"
#include "card_slots.h"
#include "httpd.h"
#include "exi.h"
#include "bba.h"
#include "patcher.h"
#include "dvd.h"
#include "elf.h"
#include "flippy.h"
#include "gameid.h"
#include "gcm.h"
#include "mp3.h"
#include "nkit.h"
#include "rt4k.h"
#include "wkf.h"
#include "cheats.h"
#include "settings.h"
#include "menuaudio.h"
#include "aram/sidestep.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "gui/apps.h"
#include "gui/card_art.h"
#include "gui/manual.h"
#include "gui/ui_apps.h"
#include "gui/ui_gameflow_detail.h"
#include "gui/saves_stats.h"
#include "gui/ui_gameflow_library.h"
#include "gui/ui_cheats.h"
#include "gui/ui_folder.h"
#include "gui/ui_gameflow_ownership.h"
#include "gui/ui_gameflow_resolver.h"
#include "gui/ui_home.h"
#include "gui/ui_home_safety.h"
#include "gui/ui_presentation.h"
#include "gui/ui_scene.h"
#include "gui/ui_motion.h"
#include "gui/ui_stage.h"
#include "gui/time_sync.h"
#include "devices/deviceHandler.h"
#include "devices/filelock.h"
#include "devices/filemeta.h"
#include "dolparameters.h"
#include "reservedarea.h"

DiskHeader GCMDisk;      //Gamecube Disc Header struct
TGCHeader tgcFile;
char IPLInfo[256] __attribute__((aligned(32)));
GXRModeObj *newmode = NULL;
char txtbuffer[2048];           //temporary text buffer
file_handle curFile;    //filedescriptor for current file
file_handle curDir;     //filedescriptor for current directory

// Menu related variables
int curMenuLocation = ON_FILLIST; //where are we on the screen?
int curMenuSelection = 0;	      //menu selection
int curSelection = 0;		      //entry selection
int needsDeviceChange = 0;
int needsRefresh = 0;
int current_view_start = 0;
int current_view_end = 0;

/* Home is a menu-thread state machine. The renderer receives only snapshots;
 * hardware availability is never probed from the video thread. */
static uiHomeState_t homeState;
static bool homeStateReady;
static bool homeLibraryEntryPending;
static bool homeFlippyUpdatePending;
static uiMenuInputState_t homeMenuInput;
static bool homeMenuInputVisible;
static u32 homeMenuInputRetrace;

/* Mount truth is bound to the exact handler pointer. A newly assigned source
 * therefore starts unmounted and can never inherit a predecessor's state. */
static uiHomeSourceLifecycle_t homeSourceLifecycle;

/* Whether the source has apps, and emulators, looked for once per mount
 * and refresh. */
static bool homeAppsKnown;
static bool homeAppsFound;
static bool homeEmulatorsKnown;
static bool homeEmulatorsFound;

/* System > File Browser: Swiss's own file list, even where the Library
 * would show, until Home or a Recent entry takes over again. */
static bool homeFileBrowser;

/* Setup > Library > File Management, which File Browser always has. Only
 * read here: the setting itself is never changed, so never saved changed. */
static bool fileManagementAllowed(void)
{
	return swissSettings.enableFileManagement || homeFileBrowser;
}

/* While the File Browser runs one of Swiss's file operations, Swiss's
 * questions and results are drawn as its own boxes beside the row (the
 * question's words unchanged); everywhere else they are Swiss's. */
static bool filesBoxes;
static int filesAsk(const char *text);

/* Asks before an action that is hard to undo. text ends with its hint line
 * ("A  MOVE    B  CANCEL"); true on A. */
static bool confirmAction(const char *text)
{
	int asked = filesBoxes ? filesAsk(text) : -1;
	if(asked >= 0) {
		return asked;
	}
	bool released = false;
	bool confirmed = false;
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_WARN, text));

	while(1) {
		u32 held = padsButtonsHeld();

		/* The press that chose the action must be let go first. */
		if(!released) {
			released = held == 0u;
		}
		else if(held & BUTTON_A) {
			confirmed = true;
			break;
		}
		else if(held & BUTTON_B) {
			break;
		}
		VIDEO_WaitVSync();
	}
	do {VIDEO_WaitVSync();} while(padsButtonsHeld() & (BUTTON_A | BUTTON_B));
	DrawDispose(box);
	return confirmed;
}

/* Autoload skips Home at every start, so turning it on asks first; turning
 * it off doesn't. */
static bool autoloadToggleConfirmed(const char *path, bool folder)
{
	if(!strcmp(&swissSettings.autoload[0], path) ||
		!fnmatch(&swissSettings.autoload[0], path, FNM_PATHNAME)) {
		return true;
	}
	return confirmAction(folder ?
		"Open this folder at every start?\nHome is skipped until you turn it off.\nA  AUTOLOAD    B  CANCEL" :
		"Open this game at every start?\nHome is skipped until you turn it off.\nA  AUTOLOAD    B  CANCEL");
}

static void homeSourceRecord(DEVICEHANDLER_INTERFACE *handler,
	uiHomeSourceMountState_t state)
{
	UIHomeSafety_RecordSource(&homeSourceLifecycle, handler, state);
	homeAppsKnown = false;
	homeEmulatorsKnown = false;
}

static bool homeSourceLifecycleMounted(void)
{
	return UIHomeSafety_SourceMounted(&homeSourceLifecycle,
		devices[DEVICE_CUR]);
}

static bool homeSourceIsReady(void)
{
	bool available = devices[DEVICE_CUR] != NULL &&
		deviceHandler_getDeviceAvailable(devices[DEVICE_CUR]);
	return UIHomeSafety_SourceReady(&homeSourceLifecycle,
		devices[DEVICE_CUR], available);
}

static DEVICEHANDLER_INTERFACE *homeSourceFromPathIncludingUnavailable(
	char *path)
{
	DEVICEHANDLER_INTERFACE *handler;
	char *devicePath;
	size_t prefixLength;
	int i;

	if(path == NULL) {
		return NULL;
	}
	handler = getDeviceFromPath(path);
	if(handler != NULL) {
		return handler;
	}
	devicePath = getDevicePath(path);
	if(devicePath == path) {
		return NULL;
	}
	prefixLength = (size_t)(devicePath - path);
	for(i = 0; i < MAX_DEVICES; ++i) {
		handler = allDevices[i];
		if(handler != NULL && handler->initial != NULL &&
				!strncmp(handler->initial->name, path, prefixLength)) {
			return handler;
		}
	}
	return NULL;
}

static void homeSourceObserveStartup(void)
{
	if(homeSourceLifecycle.state != UI_HOME_SOURCE_MOUNT_UNKNOWN) {
		return;
	}
	homeSourceRecord(devices[DEVICE_CUR],
		devices[DEVICE_CUR] != NULL && !needsDeviceChange ?
			UI_HOME_SOURCE_MOUNT_MOUNTED :
			UI_HOME_SOURCE_MOUNT_UNMOUNTED);
}

static uiHomeCapabilities_t homeCapabilities(void)
{
	uiHomeCapabilities_t capabilities = {
		.hasSource = homeSourceIsReady(),
		.hasRecent = swissSettings.recentListLevel > 0 &&
			swissSettings.recent[0][0] != '\0',
		.style = swissSettings.cubeStyle ?
			UI_HOME_CUBE_CLASSIC : UI_HOME_CUBE_INFINITE,
		.hideFaceLabel = swissSettings.hideFaceLabels != 0,
		.hideCommands = swissSettings.hideHomeControls != 0,
		.customSides = true,
		.sides = {
			UIHome_SideFace(UI_HOME_SIDE_UP, swissSettings.upFace),
			UIHome_SideFace(UI_HOME_SIDE_LEFT, swissSettings.leftFace),
			UIHome_SideFace(UI_HOME_SIDE_RIGHT, swissSettings.rightFace),
			UIHome_SideFace(UI_HOME_SIDE_DOWN, swissSettings.downFace)
		}
	};
	bool appsPlaced = false;
	bool emulatorsPlaced = false;

	for(int side = 0; side < UI_HOME_SIDE_COUNT; side++) {
		appsPlaced = appsPlaced || capabilities.sides[side] == UI_HOME_FACE_APPS;
		emulatorsPlaced = emulatorsPlaced ||
			capabilities.sides[side] == UI_HOME_FACE_EMULATORS;
	}
	/* The Apps face shows while the mounted source has an app, when Setup >
	 * Console puts it on a side; otherwise the card isn't read for it. */
	if(capabilities.hasSource && appsPlaced && !homeAppsKnown) {
		homeAppsFound = apps_available(devices[DEVICE_CUR]);
		homeAppsKnown = true;
	}
	capabilities.hasApps = capabilities.hasSource && appsPlaced &&
		homeAppsFound;
	/* Emulators the same way, for /emulators. */
	if(capabilities.hasSource && emulatorsPlaced && !homeEmulatorsKnown) {
		homeEmulatorsFound = emulators_available(devices[DEVICE_CUR]);
		homeEmulatorsKnown = true;
	}
	capabilities.hasEmulators = capabilities.hasSource && emulatorsPlaced &&
		homeEmulatorsFound;
	return capabilities;
}

static void homePublish(bool visible)
{
	uiHomeCapabilities_t capabilities = homeCapabilities();
	const char *sourceName = NULL;

	if(visible != homeMenuInputVisible) {
		UIMenuInput_Init(&homeMenuInput);
		homeMenuInputRetrace = VIDEO_GetRetraceCount();
		homeMenuInputVisible = visible;
	}

	if(visible && !homeStateReady) {
		UIHome_Init(&homeState, capabilities);
		homeStateReady = true;
	}
	if(homeStateReady) {
		/* Reconcile rows when a source disappears while a context is retained. */
		(void)UIHome_Apply(&homeState, UI_HOME_INPUT_NONE, capabilities);
		curMenuSelection = (int)homeState.face;
	}
	if(capabilities.hasSource) {
		sourceName = DeviceDisplayName(devices[DEVICE_CUR]);
	}
	DrawUpdateHome(visible && homeStateReady ? &homeState : NULL,
		capabilities, sourceName);
}

static void folderArtClose(void);

/* The File Browser's page, while it is up (defined with the File Browser):
 * every place that lets that page go clears it, or a later page at the same
 * address would look like the File Browser's own and skip its opening. */
static uiDrawObj_t *filesPage;

/* A browser panel belongs to the file-list surface. Retaining only the file
 * selection keeps Library re-entry stable without leaving an unconditional
 * legacy browser container over Home and every child menu opened from it. */
static void homePublishBrowserTransition(uiDrawObj_t **filePanel)
{
	bool visible = curMenuLocation == ON_OPTIONS;
	if(visible && filePanel != NULL && *filePanel != NULL) {
		if(*filePanel == filesPage) filesPage = NULL;
		DrawDispose(*filePanel);
		*filePanel = NULL;
	}
	/* Disposal precedes publication: the video thread removes the old panel
	 * before any frame can draw the newly visible Home composition. */
	homePublish(visible);
	if(visible) {
		folderArtClose();
	}
}

/* re-init video for a given game */
void ogc_video__reset()
{
	/* set TV mode for current game */
	switch(swissSettings.gameVMode) {
		case -2:
			newmode = &TVPal576ProgScale;
			break;
		case -1:
			newmode = &TVNtsc480Prog;
			break;
		case 0:
			switch(swissSettings.sramVideo) {
				case SYS_VIDEO_PAL:
					newmode = &TVPal576IntDfScale;
					break;
				case SYS_VIDEO_MPAL:
					newmode = &TVMpal480IntDf;
					break;
				default:
					newmode = &TVNtsc480IntDf;
					break;
			}
			break;
		case 1:
			newmode = &TVNtsc480IntDf;
			break;
		case 2:
			newmode = &TVNtsc480Int;
			break;
		case 3:
			newmode = &TVNtsc240DsVf;
			break;
		case 4 ... 7:
			newmode = &TVNtsc480Prog;
			break;
		case 8:
			newmode = &TVPal576IntDfScale;
			break;
		case 9:
			newmode = &TVPal576IntScale;
			break;
		case 10:
			newmode = &TVPal288DsVfScale;
			break;
		case 11 ... 14:
			newmode = &TVPal576ProgScale;
			break;
		default:
			newmode = NULL;
			break;
	}
	if((newmode != NULL) && (newmode != getVideoMode())) {
		if((newmode->viTVMode >> 2) == VI_PAL) {
			sprintf(txtbuffer, "Video Mode: %s\n%s Mode selected.", getVideoModeString(newmode), swissSettings.sram60Hz ? "60Hz":"50Hz");
		} else {
			sprintf(txtbuffer, "Video Mode: %s", getVideoModeString(newmode));
		}
		DrawVideoMode(newmode);
		/* The launch screen shows it as a step, without the wait. */
		if(!DrawLaunchStep(txtbuffer)) {
			uiDrawObj_t *msgBox = DrawMessageBox(D_INFO, txtbuffer);
			DrawPublish(msgBox);
			sleep(2);
			DrawDispose(msgBox);
		}
	}
}

void select_recent_entry() {
	if(swissSettings.recent[0][0] == 0) return;	// don't draw empty.
	int i = 0, idx = 0, max = RECENT_MAX;
	int rh = 22; // row height
	int fileListBase = 175;
	const u32 waitButtons = BUTTON_B | BUTTON_A | BUTTON_UP | BUTTON_DOWN;
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace = VIDEO_GetRetraceCount();
	uiDrawObj_t *container = NULL;
	UIMenuInput_Init(&menuInput);
	while(1) {
		u32 recentButtons;
		uiMenuInputDirection_t analog;
		uiDrawObj_t *newPanel = DrawEmptyBox(30,fileListBase-30, getVideoMode()->fbWidth-30, 380);
		DrawAddChild(newPanel, DrawLabel(45, fileListBase-18, "Recent:"));
		for(i = 0; i < RECENT_MAX; i++) {
			if(swissSettings.recent[i][0] == 0) {
				max = i;
				break;
			}
			DrawAddChild(newPanel, DrawSelectableButton(45,fileListBase+(i*(rh+2)), getVideoMode()->fbWidth-45, fileListBase+(i*(rh+2))+rh, getRelativeName(&swissSettings.recent[i][0]), (i == idx) ? B_SELECTED:B_NOSELECT));
		}
		container = DrawRepublish(container, newPanel);
		while(1) {
			recentButtons = padsButtonsHeld();
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(recentButtons & waitButtons) != 0u);
			if((recentButtons & waitButtons) != 0u ||
					analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
		}
		if((recentButtons & BUTTON_UP) || analog == UI_MENU_INPUT_UP) {
			idx = (--idx < 0) ? max - 1 : idx;
		}
		if((recentButtons & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {
			idx = (idx + 1) % max;
		}
		if(recentButtons & BUTTON_A) break;
		if(recentButtons & BUTTON_B) { idx = -1; break; }
		while((padsButtonsHeld() & waitButtons) != 0u) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT, true);
			VIDEO_WaitVSync();
		}
	}
	do {VIDEO_WaitVSync();} while (padsButtonsHeld() & BUTTON_B);
	DrawDispose(container);
	if(idx >= 0) {
		DEVICEHANDLER_INTERFACE *targetSource =
			homeSourceFromPathIncludingUnavailable(
				&swissSettings.recent[idx][0]);
		DEVICEHANDLER_INTERFACE *previousSource = devices[DEVICE_CUR];
		bool previousSourceMounted = homeSourceLifecycleMounted();
		bool previousSourceReady = homeSourceIsReady();
		bool targetWasAvailable = targetSource != NULL &&
			deviceHandler_getDeviceAvailable(targetSource);
		bool forceReinit = UIHomeSafety_ShouldForceRecentInit(
			&homeSourceLifecycle, previousSource, targetSource,
			previousSourceReady);

		/* util.c intentionally fast-paths pointer equality. Break that equality
		 * only for a stale same-handler source so find_existing_entry must init;
		 * a different ready source remains published if the target init fails. */
		if(forceReinit) {
			freeFiles();
			DrawGameflowCancelPosters();
			if(previousSourceMounted) {
				previousSource->deinit(previousSource->initial);
				homeSourceRecord(previousSource,
					UI_HOME_SOURCE_MOUNT_UNMOUNTED);
			}
			devices[DEVICE_CUR] = NULL;
			homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
		}
		/* getDeviceFromPath intentionally hides unavailable handlers. Expose the
		 * resolved identity only for this synchronous init attempt, then restore
		 * failure truth below. */
		if(targetSource != NULL && !targetWasAvailable) {
			deviceHandler_setDeviceAvailable(targetSource, true);
		}
		/* find_existing_entry owns generic discovery and may deinitialize a
		 * different mounted source after the replacement init succeeds. Retire
		 * UI-owned poster handles here, at the menu boundary, so util.c remains
		 * independent of the renderer. A failed replacement simply reloads the
		 * prior source's pack on the next Library request. */
		if(devices[DEVICE_CUR] != NULL &&
			devices[DEVICE_CUR] != targetSource) {
			DrawGameflowCancelPosters();
		}
		int res = find_existing_entry(&swissSettings.recent[idx][0], true);
		if(res != RECENT_ERR_DEV_MISSING) {
			/* An entry opens as it does from Home: in the Library where
			 * that would show. */
			homeFileBrowser = false;
		}
		if(res == RECENT_ERR_DEV_MISSING && targetSource != NULL &&
				!targetWasAvailable) {
			deviceHandler_setDeviceAvailable(targetSource, false);
		}
		if(res != RECENT_ERR_DEV_MISSING && targetSource != NULL &&
				devices[DEVICE_CUR] == targetSource) {
			/* Zero and ENT_MISSING both prove target init/scan succeeded. */
			deviceHandler_setDeviceAvailable(targetSource, true);
			homeSourceRecord(targetSource, UI_HOME_SOURCE_MOUNT_MOUNTED);
		}
		else if(devices[DEVICE_CUR] == previousSource &&
				previousSourceMounted) {
			/* A different target failed before publication; preserve the exact
			 * mounted source and its browser state. */
			homeSourceRecord(previousSource, UI_HOME_SOURCE_MOUNT_MOUNTED);
		}
		else {
			devices[DEVICE_CUR] = NULL;
			homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
		}
		if(res) {
			uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,res == RECENT_ERR_ENT_MISSING ? "Recent entry not found.\nPress A to continue."
																					:	"Recent device not found.\nPress A to continue.");
			DrawPublish(msgBox);
			wait_press_A();
			DrawDispose(msgBox);
		}
	}
}

// Modify entry to go up a directory.
bool upToParent(file_handle* entry)
{
	// If we're a file, go up to the parent of the file
	if(entry->fileType == IS_FILE)
		getParentPath(entry->name, entry->name);

	// Go up a folder
	return getParentPath(entry->name, entry->name);
}

/* The list's actions, one copy for the File Browser and the Library. Each
 * acts on directory[curSelection], the sorted listing's focused entry, as the
 * loops always did. cardArt: the caller shows folder pictures (the
 * Library), which stop with the meta thread before a file operation. */

/* X, and ".." outside the Library: up a folder. The folder left is the one
 * scanFiles selects again (curFile); at the root the Source picker opens. */
static void filesUp(const file_handle *parent)
{
	memcpy(&curFile, &curDir, sizeof(file_handle));
	curDir.fileBase = parent->fileBase;
	needsDeviceChange = upToParent(&curDir);
	needsRefresh = 1;
}

static bool gameflowCustomRootForDevice(const DEVICEHANDLER_INTERFACE *device,
	const char *customPath, char *gamesRoot)
{
	size_t prefixLength;

	if(device == NULL || device->initial == NULL ||
		customPath == NULL || customPath[0] == '\0') {
		return false;
	}
	prefixLength = strlen(device->initial->name);
	if(prefixLength == 0u ||
		strncasecmp(device->initial->name, customPath, prefixLength) != 0 ||
		customPath[prefixLength] == '\0') {
		return false;
	}
	strlcpy(gamesRoot, customPath, PATHNAME_MAX);
	return true;
}

static void gameflowGamesRoot(const DEVICEHANDLER_INTERFACE *device,
	char *gamesRoot)
{
	if(device == NULL || device->initial == NULL) {
		gamesRoot[0] = '\0';
		return;
	}
	/* Settings › Library Path, when it is on this device; else /games. */
	if(gameflowCustomRootForDevice(device, swissSettings.libraryPath,
			gamesRoot)) {
		return;
	}
	concat_path(gamesRoot, device->initial->name, "games");
}

/* The retained Library ends at /games. Its parent card and X return Home,
 * keeping this listing ready for the next visit instead of opening Swiss's
 * device-root browser. Within a folder, keep scanFiles' child selection. */
static void gameflowNavigateParent(bool useGameflow, const file_handle *parent)
{
	char gamesRoot[PATHNAME_MAX];

	if(useGameflow && devices[DEVICE_CUR] != NULL &&
		devices[DEVICE_CUR]->initial != NULL) {
		gameflowGamesRoot(devices[DEVICE_CUR], gamesRoot);
		if(UIGameflowLibrary_Locate(gamesRoot, curDir.name) ==
			UI_GAMEFLOW_LIBRARY_LOCATION_ROOT) {
			curMenuLocation = ON_OPTIONS;
			return;
		}
	}
	filesUp(parent);
}

static bool filesManageFile(DEVICEHANDLER_INTERFACE *keep);

/* A: open a folder, go up from "..", start a file, or with File Management
 * manage one that can't start. */
static void filesActivate(file_handle **directory, bool useGameflow)
{
	lockFile(directory[curSelection]);
	//go into a folder or select a file
	if(directory[curSelection]->fileType==IS_DIR) {
		memcpy(&curDir, directory[curSelection], sizeof(file_handle));
		needsRefresh=1;
	}
	else if(directory[curSelection]->fileType==IS_SPECIAL) {
		gameflowNavigateParent(useGameflow, directory[curSelection]);
	}
	else if(directory[curSelection]->fileType==IS_FILE) {
		memcpy(&curFile, directory[curSelection], sizeof(file_handle));
		if(canLoadFileType(curFile.name, devices[DEVICE_CUR]->extraExtensions)) {
			meta_thread_stop();
			load_file();
		}
		else if(fileManagementAllowed()) {
			meta_thread_stop();
			needsRefresh = filesManageFile(NULL) ? 1:0;
		}
		memcpy(directory[curSelection], &curFile, sizeof(file_handle));
	}
	unlockFile(directory[curSelection]);
}

/* Z on "..": folder opens at every start, or no longer does. Turning it
 * on asks first. */
static void filesToggleAutoload(const char *folder)
{
	if(!autoloadToggleConfirmed(folder, true)) {
		return;
	}
	// Toggle autoload
	if(!strcmp(&swissSettings.autoload[0], folder)
	|| !fnmatch(&swissSettings.autoload[0], folder, FNM_PATHNAME)) {
		memset(&swissSettings.autoload[0], 0, PATHNAME_MAX);
	}
	else {
		strcpy(&swissSettings.autoload[0], folder);
	}
	// Save config
	uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Saving autoload\205"));
	config_update_autoload(true);
	DrawDispose(msgBox);
}

/* Z in the Library, with File Management: the Z menu on a file or a
 * folder, Autoload on "..". True when the listing must be read again. */
static bool filesManage(file_handle **directory)
{
	if(!fileManagementAllowed()) {
		return false;
	}
	lockFile(directory[curSelection]);
	if(directory[curSelection]->fileType == IS_FILE || directory[curSelection]->fileType == IS_DIR) {
		memcpy(&curFile, directory[curSelection], sizeof(file_handle));
		meta_thread_stop();
		CardArt_Pause();
		needsRefresh = filesManageFile(NULL) ? 1:0;
		memcpy(directory[curSelection], &curFile, sizeof(file_handle));
		while(padsButtonsHeld() & BUTTON_B) VIDEO_WaitVSync();
		if(needsRefresh) {
			// If we return from doing something with a file, refresh the device in the same dir we were at
			unlockFile(directory[curSelection]);
			return true;
		}
	}
	else if(directory[curSelection]->fileType == IS_SPECIAL) {
		filesToggleAutoload(&curDir.name[0]);
	}
	unlockFile(directory[curSelection]);
	return false;
}

/* START, unless Recent List is Off: the Recent list. True when it opened. */
static bool filesRecent(bool cardArt)
{
	if(swissSettings.recentListLevel > 0) {
		meta_thread_stop();
		if(cardArt) {
			CardArt_Pause();
		}
		select_recent_entry();
		return true;
	}
	return false;
}

/* B: back to Home. */
static void filesHome(void)
{
	curMenuLocation = ON_OPTIONS;
}

/* The DK Bongos' clap: on to the next Bongo game. */
static void filesBarrelGame(uiDrawObj_t *loadingBox)
{
	if(padsButtonsHeld() & BUTTON_CLAP) {
		DrawUpdateProgressLoading(loadingBox, +1);
		curSelection = meta_find_barrel_game(curSelection);
		DrawUpdateProgressLoading(loadingBox, -1);
	}
}

/* ------------------------------------------------------------------------
 * The File Browser: two panes side by side, as Memory Cards shows two
 * stacks, in place of Swiss's lists everywhere outside the Library. The
 * left pane is the Source and is the listing menu_loop scans: it starts
 * games, holds the second disc and the MP3 player's songs, and is the one
 * the meta thread reads banners for. The right pane reads a folder of its
 * own, on the Source or on a storage of its own that L and R choose.
 * --------------------------------------------------------------------- */

/* The right pane's listing: readDir and sortFiles, as select_dest_dir reads
 * a folder. Never scanFiles, populate_meta, the meta thread,
 * current_view_*, curFile, curDir or curSelection, so the shared listing
 * and everything that reads it never see this one. Its storage is kept out
 * of devices[]: Settings' Load at startup picks a destination device and
 * unmounts whatever DEVICE_DEST held. */
typedef struct {
	DEVICEHANDLER_INTERFACE *device;	/* its storage, for the session */
	file_handle dir;			/* the open folder */
	file_handle *entries;			/* readDir's array */
	file_handle **sorted;			/* sortFiles' view of it */
	int read, count;			/* entries read, and shown */
	bool listed;				/* entries hold dir's listing */
	bool readFailed;			/* the last read failed */
	u8 mount;				/* uiFilesMount_t */
	u16 listing;
	char status[64];			/* why init failed, the device's words */
	char free[24];				/* its free space, "read-only" or "" */
	char focusName[PATHNAME_MAX];		/* found again after a read */
} filesPane_t;

static filesPane_t filesOther;
static uiFilesState_t filesState;
static uiFilesSnapshot_t filesSnapshot __attribute__((aligned(32)));
/* filesPage (declared above): the page last published, so a return from a
 * folder change updates it in place rather than opening it again. */
static u16 filesLeftListing;
/* The Source's free space, read once a listing. */
static char filesFree[24];
/* A left folder change keeps the presses made while the folder is read. */
static bool filesKeepPresses;
/* Line 2 of the info bar until the next press: why a press did nothing. */
static const char *filesNote;
static char filesNoteText[UI_FILES_TEXT_CAPACITY];
/* The visible left rows read without a meta thread, from filesMetaFirst. */
static int filesMetaFirst = -1;
static u16 filesMetaListing;
static u32 filesMetaTried;
/* The scene the File Browser opened over: Home's from a face, else the
 * Library's. */
static uiSceneId_t filesScene = UI_SCENE_HOME;
/* renderFileCarousel couldn't draw the Library here: the File Browser shows
 * this folder, read again, until the next listing. */
static bool gameflowListFallback;
/* A device the File Browser holds mounted while Swiss's box runs on the
 * right pane's storage: manage_file neither mounts nor unmounts it as a
 * destination. */
static DEVICEHANDLER_INTERFACE *manageKeep;

/* Swiss's box, keeping keep mounted: manage_file neither mounts nor
 * unmounts it as a destination, and a destination slot holding it is
 * empty meanwhile, so the picker can't unmount it. keep NULL: the right
 * pane's own storage, when it holds one (outside the File Browser it has
 * let it go). */
static bool filesManageFile(DEVICEHANDLER_INTERFACE *keep)
{
	bool changed;

	if(keep == NULL && filesOther.mount == UI_FILES_OWN) {
		keep = filesOther.device;
	}
	if(keep != NULL && devices[DEVICE_DEST] == keep) {
		devices[DEVICE_DEST] = NULL;
	}
	manageKeep = keep;
	changed = manage_file();
	manageKeep = NULL;
	return changed;
}
static void sourceCommit(DEVICEHANDLER_INTERFACE *device);
static bool sourceMount(void);
/* The box open (a storage menu, Actions, a question): its words, the
 * devices a storage menu lists, and the hints under it. */
static uiFilesStorageMenu_t filesMenu;
static DEVICEHANDLER_INTERFACE *filesMenuDevices[UI_FILES_STORAGE_DEVICES];
static uiFilesHintMode_t filesMenuHints = UI_FILES_HINTS_BOX;
static void manageDestName(char *out, const char *dir, const char *src,
	DEVICEHANDLER_INTERFACE *srcDev, DEVICEHANDLER_INTERFACE *destDev);
/* Where the last copy or move landed, after Keep both chose its number. */
static char manageLanded[PATHNAME_MAX];

static int filesMeasure(const char *text)
{
	return GetTextSizeInPixels(text);
}

/* text's width at scale, as the draw takes it. */
static int filesWidth(const char *text, float scale)
{
	return (int)ceilf((float)GetTextSizeInPixels(text) * scale);
}

/* A device as the File Browser's model sees it. */
static void filesDevice(DEVICEHANDLER_INTERFACE *device, uiFilesDevice_t *out)
{
	memset(out, 0, sizeof(*out));
	if(device == NULL) {
		return;
	}
	out->handler = device;
	out->name = DeviceDisplayName(device);
	out->location = device->location;
	out->network = device == &__device_smb || device == &__device_ftp ||
		device == &__device_fsp;
	out->metric = !(device->location & LOC_SYSTEM);
	out->card = device == &__device_card_a || device == &__device_card_b;
	out->canWrite = (device->features & FEAT_WRITE) != 0;
	/* A memory card's rename is a stub that changes nothing. */
	out->canRename = device->renameFile != NULL && !out->card;
	out->canHide = device->hideFile != NULL;
	out->canDelete = device->deleteFile != NULL;
}

/* a and b can't be open together: two storages on one connector, or two on
 * the network adapter. reason, when given, says why. */
static bool filesClash(DEVICEHANDLER_INTERFACE *a, DEVICEHANDLER_INTERFACE *b,
	char *reason, size_t capacity)
{
	uiFilesDevice_t x, y;

	filesDevice(a, &x);
	filesDevice(b, &y);
	return UIFiles_StorageClash(&x, &y, reason, capacity);
}

/* A memory card's and a Qoob's sizes are in their blocks, 0 elsewhere. */
static u32 filesBlockSize(DEVICEHANDLER_INTERFACE *device)
{
	return device == &__device_card_a || device == &__device_card_b ? 8192u :
		device == &__device_qoob ? 65536u : 0u;
}

/* A free-space box's words: never a guess (a network share's, or none). */
static void filesFreeText(DEVICEHANDLER_INTERFACE *device, char *out, size_t capacity)
{
	device_info *info;

	out[0] = '\0';
	if(!(device->features & FEAT_WRITE)) {
		strlcpy(out, "read-only", capacity);
		return;
	}
	info = device->info != NULL ? device->info(device->initial) : NULL;
	if(UIFiles_FreeKnown(info != NULL, info != NULL ? info->totalSpace : 0u,
			device == &__device_smb || device == &__device_ftp ||
			device == &__device_fsp)) {
		UIFiles_SizeText(out, capacity, info->freeSpace, filesBlockSize(device),
			info->metric);
	}
}

/* The right pane's listing goes; its storage, folder and focus stay. */
static void filesOtherFree(void)
{
	for(int i = 0; i < filesOther.read; i++) {
		filesOther.device->closeFile(&filesOther.entries[i]);
	}
	free(filesOther.sorted);
	free(filesOther.entries);
	filesOther.sorted = NULL;
	filesOther.entries = NULL;
	filesOther.read = filesOther.count = 0;
	filesOther.listed = false;
}

/* The right pane lets its storage go: its listing, and the mount it made
 * itself (the Source's stays). Its storage, folder and focus are kept for
 * filesOtherAcquire to bring back. Called before control leaves the
 * screen, before anything that mounts or unmounts storage on its own (a
 * Source change, a settings save, Recent, starting a file) and on B. */
static void filesOtherRelease(void)
{
	filesOtherFree();
	if(filesOther.mount == UI_FILES_OWN) {
		filesOther.device->deinit(filesOther.device->initial);
	}
	filesOther.mount = UI_FILES_UNMOUNTED;
}

/* Mounts the right pane's storage as manage_file mounts a destination,
 * unless it is the Source, whose mount it shares. False when it won't. */
static bool filesOtherMount(void)
{
	DEVICEHANDLER_INTERFACE *device = filesOther.device;
	s32 ret;

	if(device == devices[DEVICE_CUR]) {
		filesOther.mount = UI_FILES_SHARED;
		return true;
	}
	device->deinit(device->initial);
	deviceHandler_setStatEnabled(0);
	ret = device->init(device->initial);
	deviceHandler_setStatEnabled(1);
	filesOther.mount = ret ? UI_FILES_FAILED : UI_FILES_OWN;
	filesOther.status[0] = '\0';
	if(ret) {
		char *status = device->status != NULL ? device->status(device->initial) : NULL;

		strlcpy(filesOther.status, status != NULL ? status : strerror(ret),
			sizeof(filesOther.status));
	}
	return !ret;
}

/* path is folder or inside it. */
static bool filesWithin(const char *path, const char *folder)
{
	size_t length = strlen(folder);

	return !strncmp(path, folder, length) && (path[length] == '\0' || path[length] == '/');
}

static bool filesAtRoot(const file_handle *dir)
{
	char parent[PATHNAME_MAX];

	strlcpy(parent, dir->name, sizeof(parent));
	return getParentPath(parent, parent);
}

/* Reads the right pane's folder, focusing focusName again. The read is the
 * mount check: a storage of its own that someone else unmounted is mounted
 * again once. A folder that is gone starts it again at its storage's top. */
static void filesOtherRead(void)
{
	DEVICEHANDLER_INTERFACE *device = filesOther.device;
	int focus = 0;

	filesOtherFree();
	filesOther.read = device->readDir(&filesOther.dir, &filesOther.entries, -1);
	if(filesOther.read < 0 && filesOther.mount == UI_FILES_OWN) {
		free(filesOther.entries);
		filesOther.entries = NULL;
		if(filesOtherMount()) {
			filesOther.read = device->readDir(&filesOther.dir, &filesOther.entries, -1);
		}
	}
	if(filesOther.read <= 0 && filesOther.mount != UI_FILES_FAILED &&
			strcmp(filesOther.dir.name, device->initial->name)) {
		free(filesOther.entries);
		filesOther.entries = NULL;
		memcpy(&filesOther.dir, device->initial, sizeof(file_handle));
		filesOther.read = device->readDir(&filesOther.dir, &filesOther.entries, -1);
	}
	filesOther.readFailed = filesOther.read < 0;
	if(filesOther.read < 0) {
		filesOther.read = 0;
	}
	filesOther.count = sortFiles(filesOther.entries, filesOther.read, &filesOther.sorted);
	for(int i = 0; i < filesOther.count; i++) {
		if(!strcmp(filesOther.sorted[i]->name, filesOther.focusName)) {
			focus = i;
		}
	}
	if(focus == 0 && filesOther.count > 1 &&
			filesOther.sorted[0]->fileType == IS_SPECIAL) {
		focus = 1;
	}
	filesFreeText(device, filesOther.free, sizeof(filesOther.free));
	filesOther.listed = true;
	filesOther.listing++;
	UIFiles_SetPane(&filesState, UI_FILES_RIGHT, filesOther.count, focus);
}

/* Where the right pane opens the first time in a session. */
static DEVICEHANDLER_INTERFACE *filesOtherDefault(void)
{
	DEVICEHANDLER_INTERFACE *source = devices[DEVICE_CUR], *config = devices[DEVICE_CONFIG];

	return UIFiles_RightOnConfig(fileManagementAllowed(), config != NULL,
		config != NULL && deviceHandler_getDeviceAvailable(config), config == source) &&
		!filesClash(config, source, NULL, 0) ? config : source;
}

/* The right pane, mounted and read if it isn't: every time the screen
 * starts or comes back. A pane that failed to mount stays so until it is
 * released or its storage chosen again. */
static void filesOtherAcquire(void)
{
	DEVICEHANDLER_INTERFACE *source = devices[DEVICE_CUR];

	if(filesOther.device == NULL || filesClash(filesOther.device, source, NULL, 0)) {
		filesOtherRelease();
		filesOther.device = filesOtherDefault();
		memcpy(&filesOther.dir, filesOther.device->initial, sizeof(file_handle));
		filesOther.focusName[0] = '\0';
	}
	/* The Source changed under a shared pane: the pane mounts its own. Or
	 * its storage became the Source: the Source's mount is the one. */
	if(filesOther.mount == UI_FILES_SHARED && filesOther.device != source) {
		filesOtherFree();
		filesOther.mount = UI_FILES_UNMOUNTED;
	}
	if(filesOther.mount == UI_FILES_OWN && filesOther.device == source) {
		filesOther.mount = UI_FILES_SHARED;
	}
	if(filesOther.mount == UI_FILES_UNMOUNTED) {
		filesOtherFree();
		if(!filesOtherMount()) {
			filesOther.listed = true;
			filesOther.readFailed = false;
			filesOther.free[0] = '\0';
			filesOther.listing++;
			UIFiles_SetPane(&filesState, UI_FILES_RIGHT, 0, 0);
			return;
		}
	}
	if(filesOther.mount != UI_FILES_FAILED && !filesOther.listed) {
		filesOtherRead();
	}
}

/* The right pane on device, at its top, mounted and read. */
static void filesOtherChoose(DEVICEHANDLER_INTERFACE *device)
{
	filesOtherRelease();
	filesOther.device = device;
	memcpy(&filesOther.dir, device->initial, sizeof(file_handle));
	filesOther.focusName[0] = '\0';
	filesOtherAcquire();
}

/* R, Other devices...: Swiss's destination picker, kept from mounting or
 * unmounting anything of the screen's. DEVICE_DEST is empty while it runs
 * (so it lets nothing go), what it chose becomes the pane's storage, and
 * the slot and the scene come back as they were. The picker greys nothing:
 * a choice that can't be open beside the Source keeps the pane where it
 * was, and the info bar says why. */
static void filesOtherPick(void)
{
	DEVICEHANDLER_INTERFACE *dest = devices[DEVICE_DEST];
	DEVICEHANDLER_INTERFACE *chosen;

	filesOtherRelease();
	devices[DEVICE_DEST] = NULL;
	/* The picker reads held buttons: the A that chose it isn't its own. */
	while(padsButtonsHeld() & PAD_BUTTON_A) VIDEO_WaitVSync();
	select_device(DEVICE_DEST);
	chosen = devices[DEVICE_DEST];
	devices[DEVICE_DEST] = dest;
	UIScene_Request(filesScene);
	if(chosen != NULL && filesClash(chosen, devices[DEVICE_CUR], filesNoteText,
			sizeof(filesNoteText))) {
		filesNote = filesNoteText;
		chosen = NULL;
	}
	if(chosen != NULL) {
		filesOtherChoose(chosen);
	}
	else {
		filesOtherAcquire();
	}
}

/* The right pane opens folder (or, NULL, its parent), focusing focus (NULL:
 * the folder it came from). A meta thread may be reading the Source
 * meanwhile: it runs only on a device that is thread safe. */
static void filesOtherOpen(const file_handle *folder, const char *focus)
{
	char from[PATHNAME_MAX];

	strlcpy(from, filesOther.dir.name, sizeof(from));
	if(folder != NULL) {
		memcpy(&filesOther.dir, folder, sizeof(file_handle));
	}
	else {
		/* A disc finds a folder by its fileBase: the ".." entry has the
		 * parent's, as filesUp takes it. */
		if(filesOther.count > 0 && filesOther.sorted[0]->fileType == IS_SPECIAL) {
			filesOther.dir.fileBase = filesOther.sorted[0]->fileBase;
		}
		getParentPath(filesOther.dir.name, filesOther.dir.name);
		filesOther.dir.fileType = IS_DIR;
		if(filesAtRoot(&filesOther.dir)) {
			memcpy(&filesOther.dir, filesOther.device->initial, sizeof(file_handle));
		}
	}
	strlcpy(filesOther.focusName, focus != NULL ? focus : from,
		sizeof(filesOther.focusName));
	filesOtherRead();
}

/* On reset or power-off, deviceHandler.c shuts down the devices[] slots,
 * after the interface stops reading (priority 1). The right pane's storage
 * is in none of them, so it is shut down here, at the same point. */
static s32 filesOtherOnReset(s32 final)
{
	if(!final && filesOther.mount == UI_FILES_OWN && filesOther.device != NULL &&
			!(filesOther.device->quirks & QUIRK_NO_DEINIT)) {
		filesOther.device->deinit(filesOther.device->initial);
		filesOther.mount = UI_FILES_UNMOUNTED;
	}
	return TRUE;
}

static sys_resetinfo filesOtherResetInfo = {
	{NULL, NULL}, filesOtherOnReset, 1
};

__attribute__((constructor))
static void filesOtherRegisterReset(void)
{
	SYS_RegisterResetFunc(&filesOtherResetInfo);
}

static bool filesFlattened(void)
{
	return !fnmatch(swissSettings.flattenDir, curDir.name,
		FNM_PATHNAME | FNM_CASEFOLD);
}

/* What an entry is. Swiss's meta reading may have made a left folder the
 * program inside it: it shows, and is focused again, as that folder. */
static uiFilesKind_t filesKind(const file_handle *entry, int pane,
	const char *dirName)
{
	/* A memory card's and the Qoob's names are flat: a slash in one is part
	 * of the name, never a folder Swiss made into its program (and acting
	 * on that "folder" would list, and act on, every save there). */
	bool flat = devices[DEVICE_CUR] == &__device_card_a ||
		devices[DEVICE_CUR] == &__device_card_b ||
		devices[DEVICE_CUR] == &__device_qoob;

	if(pane == UI_FILES_LEFT && !flat && UIFiles_IsProgramFolder(entry->name,
			entry->fileType, dirName, filesFlattened())) {
		return UI_FILES_KIND_PROGRAM_FOLDER;
	}
	return UIFiles_Kind(entry->name, entry->fileType);
}

/* A starts it: a file Swiss loads, but a FlippyDrive update only on the
 * FlippyDrive (elsewhere it is a file to copy, and A opens its Actions). */
static bool filesLoads(const file_handle *entry)
{
	return entry->fileType == IS_FILE &&
		canLoadFileType((char *)entry->name, devices[DEVICE_CUR]->extraExtensions) &&
		(!endsWith((char *)entry->name, ".fpkg") || devices[DEVICE_CUR] == &__device_flippy ||
		devices[DEVICE_CUR] == &__device_flippyflash);
}

static bool filesAutoloadFolder(const char *folder)
{
	return swissSettings.autoload[0] != '\0' &&
		(!strcmp(&swissSettings.autoload[0], folder) ||
		!fnmatch(&swissSettings.autoload[0], folder, FNM_PATHNAME));
}

/* The size column's words for a file: blocks on a memory card or a Qoob,
 * where it is on a WODE. */
static void filesSizeText(char *out, size_t capacity, const file_handle *entry)
{
	DEVICEHANDLER_INTERFACE *device = entry->device;

	if(device == &__device_wode) {
		const ISOInfo_t *iso = (const ISOInfo_t *)&entry->other;

		UIFiles_PartitionText(out, capacity, iso->iso_partition, iso->iso_number);
		return;
	}
	UIFiles_SizeText(out, capacity, entry->size, filesBlockSize(device),
		device != NULL && !(device->location & LOC_SYSTEM));
}

/* What each kind is, in the info bar. */
static const char *const filesKindWords[UI_FILES_KINDS] = {
	"", "Folder", "Program folder", "GameCube disc", "Compressed GameCube disc",
	"Program", "Firmware update", "Music", "Picture", "Text", "File"
};

/* The name scanFiles finds the left pane's focus by: a program folder's own
 * path, else the entry's. */
static void filesLeftFocusName(file_handle **directory, char *out, size_t capacity)
{
	file_handle *entry = directory[curSelection];

	lockFile(entry);
	if(filesKind(entry, UI_FILES_LEFT, curDir.name) == UI_FILES_KIND_PROGRAM_FOLDER) {
		UIFiles_ProgramFolderPath(out, capacity, entry->name, curDir.name);
	}
	else {
		strlcpy(out, entry->name, capacity);
	}
	unlockFile(entry);
}

/* One pane's header and rows, from its listing's window. wait: lock the
 * left rows the meta thread may be filling, else give up (false) when one
 * is busy. */
static bool filesPaneSnapshot(uiFilesPaneSnapshot_t *out, file_handle **entries,
	int pane, const char *dirName, const uiFilesLayout_t *layout, bool wait)
{
	const uiFilesPaneState_t *state = &filesState.pane[pane];
	const uiFilesRect_t *box = &layout->pane[pane];
	const char *deviceName = DeviceDisplayName(pane == UI_FILES_LEFT ? devices[DEVICE_CUR] :
		filesOther.device);
	const char *free = pane == UI_FILES_LEFT ? filesFree : filesOther.free;
	bool readOnly = !strcmp(free, "read-only");
	bool track = state->count > UI_FILES_ROWS;
	char name[PATHNAME_MAX];
	int i, room;

	out->rows = 0;
	out->focusRow = -1;
	out->count = (s16)state->count;
	out->first = (s16)state->first;
	for(i = state->first; i < state->count && out->rows < UI_FILES_ROWS; i++) {
		uiFilesRowSnapshot_t *row = &out->row[out->rows];
		file_handle *entry = entries[i];
		char size[24] = "";
		int metaWidth;

		if(pane == UI_FILES_LEFT) {
			if(!wait && !trylockFile(entry)) {
				return false;
			}
			if(wait) {
				lockFile(entry);
			}
		}
		row->kind = (u8)filesKind(entry, pane, dirName);
		UIFiles_RowName(name, sizeof(name), entry->name, row->kind, dirName, deviceName);
		if(entry->fileType == IS_FILE && row->kind != UI_FILES_KIND_PROGRAM_FOLDER) {
			filesSizeText(size, sizeof(size), entry);
		}
		row->flags = entry->fileType != IS_SPECIAL &&
			((entry->fileAttrib & ATTRIB_HIDDEN) || *getRelativeName(entry->name) == '.') ?
			UI_FILES_ROW_HIDDEN : 0u;
		if(pane == UI_FILES_LEFT) {
			unlockFile(entry);
		}
		UIFiles_RowMeta(row->meta, sizeof(row->meta), row->kind, size);
		metaWidth = row->meta[0] != '\0' ? filesWidth(row->meta, 0.44f) : 0;
		row->scale = UIFiles_FitName(row->name, sizeof(row->name), name,
			UIFiles_NameWidth(layout, pane, metaWidth, track), filesMeasure);
		if(i == state->focus) {
			row->flags |= UI_FILES_ROW_FOCUS;
			out->focusRow = out->rows;
		}
		out->rows++;
	}
	snprintf(out->button, sizeof(out->button), pane == UI_FILES_LEFT ?
		"L  Choose storage" : "R  Choose storage");
	out->source = pane == UI_FILES_LEFT;
	strlcpy(out->free, free, sizeof(out->free));
	out->readOnly = readOnly;
	out->freeWidth = (s16)(free[0] != '\0' ?
		MAX(UI_FILES_FREE_MIN_WIDTH, filesWidth(free, 0.50f) + 16) : 0);
	out->deviceScale = UIFiles_FitDevice(out->device, sizeof(out->device), deviceName,
		layout, pane, out->source, out->freeWidth,
		out->freeWidth > 0 && !readOnly ? filesWidth("free", 0.42f) : 0, filesMeasure);
	out->deviceWidth = (s16)filesWidth(out->device, out->deviceScale);
	snprintf(out->counter, sizeof(out->counter), "%d / %d",
		state->count > 0 ? state->focus + 1 : 0, state->count);
	out->autoload = filesAutoloadFolder(dirName);
	room = box->x1 - box->x0 - 4 - filesWidth(out->counter, 0.46f) - 12 -
		(out->autoload ? 74 : 0);
	UIFiles_FitPath(out->path, sizeof(out->path), getDevicePath((char *)dirName),
		room, 0.46f, filesMeasure);
	out->pathWidth = (s16)filesWidth(out->path, 0.46f);
	out->reading = 0;
	memset(out->message, 0, sizeof(out->message));
	/* The right pane's storage can't be used: why, and what to do. */
	if(pane == UI_FILES_RIGHT && (filesOther.mount == UI_FILES_FAILED ||
			filesOther.readFailed)) {
		UIFiles_NotReady(out->message, deviceName, filesOther.status,
			filesOther.mount == UI_FILES_FAILED ? NULL :
			getRelativeName(filesOther.dir.name));
	}
	else if(state->count == 0 ||
			(state->count == 1 && entries[0]->fileType == IS_SPECIAL)) {
		strlcpy(out->message[0], "This folder is empty.", sizeof(out->message[0]));
		if((pane == UI_FILES_LEFT ? getCurrentDirEntryCount() : filesOther.read) >
				state->count) {
			strlcpy(out->message[1], "What it holds isn't shown here.",
				sizeof(out->message[1]));
		}
	}
	return true;
}

static bool filesOpensDetail(const file_handle *entry);

/* The info bar and the hints, for the focused entry of the focused pane. */
static bool filesInfoSnapshot(uiFilesSnapshot_t *s, file_handle **entries,
	const char *dirName, const uiFilesLayout_t *layout, bool wait)
{
	int pane = filesState.active;
	const uiFilesPaneState_t *state = &filesState.pane[pane];
	const char *deviceName = DeviceDisplayName(pane == UI_FILES_LEFT ? devices[DEVICE_CUR] :
		filesOther.device);
	char name[PATHNAME_MAX], title[PATHNAME_MAX];
	uiFilesKind_t kind = UI_FILES_KIND_FOLDER;
	file_handle *entry = state->count > 0 ? entries[state->focus] : NULL;
	bool loads = false, autoload = false;
	int room, scaled;

	s->active = (u8)pane;
	s->hasBanner = 0;
	s->warn = 0;
	s->chip[0] = s->size[0] = '\0';
	s->line[0][0] = s->line[1][0] = '\0';
	/* No entry: the folder's name, or at a storage's top its own. */
	strlcpy(title, getRelativeName((char *)dirName), sizeof(title));
	if(title[0] == '\0') {
		strlcpy(title, deviceName, sizeof(title));
	}
	if(entry != NULL) {
		if(pane == UI_FILES_LEFT) {
			if(!wait && !trylockFile(entry)) {
				return false;
			}
			if(wait) {
				lockFile(entry);
			}
		}
		kind = filesKind(entry, pane, dirName);
		loads = filesLoads(entry);
		UIFiles_RowName(name, sizeof(name), entry->name, kind, dirName, deviceName);
		strlcpy(title, name, sizeof(title));
		if(entry->fileType == IS_FILE && kind != UI_FILES_KIND_PROGRAM_FOLDER) {
			filesSizeText(s->size, sizeof(s->size), entry);
		}
		if(pane == UI_FILES_LEFT && entry->meta != NULL) {
			file_meta *meta = entry->meta;

			if(meta->banner != NULL && meta->bannerSize == BNR_PIXELDATA_LEN &&
					meta->bannerSum != 0xFFFF) {
				memcpy(s->banner, meta->banner, BNR_PIXELDATA_LEN);
				s->hasBanner = 1;
			}
			if(s->hasBanner && meta->displayName != NULL && meta->displayName[0] != '\0') {
				strlcpy(title, meta->displayName, sizeof(title));
				strlcpy(s->line[0], name, sizeof(s->line[0]));
			}
			if(kind == UI_FILES_KIND_DISC && meta->diskId.gamename[0] != '\0') {
				/* A banner's short company fills its field without a terminator. */
				snprintf(s->line[1], sizeof(s->line[1]), "GameCube disc  \267  %.4s%.2s%s%.*s",
					meta->diskId.gamename, meta->diskId.company,
					meta->bannerDesc.company[0] != '\0' ? "  \267  " : "",
					(int)sizeof(meta->bannerDesc.company), meta->bannerDesc.company);
			}
		}
		if(pane == UI_FILES_LEFT && s->line[1][0] == '\0') {
			strlcpy(s->line[1], filesKindWords[kind], sizeof(s->line[1]));
		}
		/* Without a banner's title above it, the file's facts go first. */
		if(s->line[0][0] == '\0') {
			strlcpy(s->line[0], s->line[1], sizeof(s->line[0]));
			s->line[1][0] = '\0';
		}
		if((entry->fileAttrib & ATTRIB_HIDDEN) && entry->fileType != IS_SPECIAL) {
			strlcpy(s->chip, "HIDDEN", sizeof(s->chip));
		}
		else if(pane == UI_FILES_LEFT && entry->fileType == IS_FILE &&
				filesAutoloadFolder(entry->name)) {
			strlcpy(s->chip, "AUTOLOAD", sizeof(s->chip));
		}
		if(pane == UI_FILES_LEFT) {
			unlockFile(entry);
		}
		if(pane == UI_FILES_RIGHT && kind != UI_FILES_KIND_PARENT) {
			snprintf(s->line[0], sizeof(s->line[0]), "On %s  \233  %s", deviceName,
				getDevicePath(filesOther.dir.name));
		}
		if(kind == UI_FILES_KIND_DISC_COMPRESSED) {
			strlcpy(s->line[1], "Swiss can't start a compressed disc.", sizeof(s->line[1]));
			s->warn = 1;
		}
		else if(pane == UI_FILES_RIGHT && loads) {
			strlcpy(s->line[1], "Games start on the left. Y swaps the two sides.",
				sizeof(s->line[1]));
		}
	}
	if(filesNote != NULL) {
		strlcpy(s->line[1], filesNote, sizeof(s->line[1]));
		s->warn = 1;
	}
	s->infoKind = (u8)kind;
	/* The name at 0.62, smaller to fit, cut in the middle past 0.46. */
	room = layout->info.x1 - 16 - layout->infoTextX - (s->chip[0] != '\0' ? 64 : 0);
	scaled = GetTextSizeInPixels(title);
	if((float)scaled * 0.62f <= (float)room) {
		strlcpy(s->title, title, sizeof(s->title));
		s->titleScale = 0.62f;
	}
	else if(scaled > 0 && (float)room / (float)scaled >= UI_FILES_NAME_MIN_SCALE) {
		strlcpy(s->title, title, sizeof(s->title));
		s->titleScale = (float)room / (float)scaled;
	}
	else {
		s->titleScale = UIFiles_FitName(s->title, sizeof(s->title), title, room,
			filesMeasure);
	}
	s->titleWidth = (s16)filesWidth(s->title, s->titleScale);
	s->sizeWidth = (s16)(s->size[0] != '\0' ? MAX(48, filesWidth(s->size, 0.50f) + 16) : 0);
	/* The lines keep clear of the loading wheel's word in the corner. */
	room = layout->info.x1 - 112 - layout->infoTextX - (s->sizeWidth ? s->sizeWidth + 12 : 0);
	for(int line = 0; line < 2; line++) {
		strlcpy(name, s->line[line], sizeof(name));
		(void)UIFiles_FitName(s->line[line], sizeof(s->line[line]), name, room,
			filesMeasure);
	}
	if(entry != NULL && entry->fileType == IS_SPECIAL) {
		autoload = filesAutoloadFolder(dirName);
	}
	/* A on a disc opens its Detail only where filesOpensDetail says so;
	 * elsewhere it is Swiss's load_file, as for any file that starts. */
	if(kind == UI_FILES_KIND_DISC && pane == UI_FILES_LEFT &&
		!filesOpensDetail(entry)) {
		kind = UI_FILES_KIND_OTHER;
	}
	/* A right pane with no entry, at its top or not ready: A, X and R
	 * choose its storage, and Z has nothing to act on. */
	UIFiles_Hints(entry == NULL && pane == UI_FILES_RIGHT &&
		(filesOther.mount == UI_FILES_FAILED || filesOther.readFailed ||
		filesAtRoot(&filesOther.dir)) ?
		UI_FILES_HINTS_STORAGE : UI_FILES_HINTS_LIST, pane, kind, loads,
		fileManagementAllowed(), autoload, s->hint[0], s->hint[1]);
	return true;
}

/* While a storage menu is open, the info bar is the focused device's: its
 * name, its free space when it is open on either side, what choosing it
 * does, and (amber) why it can't be chosen. */
static void filesMenuInfo(uiFilesSnapshot_t *s, const uiFilesLayout_t *layout)
{
	int item = filesMenu.box.focus;
	DEVICEHANDLER_INTERFACE *device = item < filesMenu.devices ? filesMenuDevices[item] : NULL;
	const char *free = device == NULL ? "" : device == devices[DEVICE_CUR] ? filesFree :
		device == filesOther.device && filesOther.mount != UI_FILES_FAILED ? filesOther.free : "";
	char line[UI_FILES_TEXT_CAPACITY], *second;
	int room;

	if(filesMenu.devices >= 0) {
		s->hasBanner = 0;
		s->infoKind = UI_FILES_KIND_FOLDER;
		s->chip[0] = '\0';
		strlcpy(s->size, free, sizeof(s->size));
		s->sizeWidth = (s16)(s->size[0] != '\0' ? MAX(48, filesWidth(s->size, 0.50f) + 16) : 0);
		s->titleScale = UIFiles_FitName(s->title, sizeof(s->title), filesMenu.box.item[item],
			layout->info.x1 - 16 - layout->infoTextX, filesMeasure);
		s->titleWidth = (s16)filesWidth(s->title, s->titleScale);
	}
	room = layout->info.x1 - 112 - layout->infoTextX - (s->sizeWidth ? s->sizeWidth + 12 : 0);
	/* Nothing greyed: line 2 is free, so the last sentence goes there (the
	 * last: a folder's name in the first may hold a ". "). */
	strlcpy(line, filesMenu.line[item], sizeof(line));
	second = NULL;
	for(char *at = line; filesMenu.devices >= 0 && filesMenu.reason[item][0] == '\0' &&
			(at = strstr(at, ". ")) != NULL; at++) {
		second = at;
	}
	if(second != NULL) {
		second[1] = '\0';
		second += 2;
	}
	(void)UIFiles_FitName(s->line[0], sizeof(s->line[0]), line, room, filesMeasure);
	(void)UIFiles_FitName(s->line[1], sizeof(s->line[1]),
		second != NULL ? second : filesMenu.reason[item], room, filesMeasure);
	s->warn = (filesMenu.warn >> item) & 1u;
	UIFiles_Hints(filesMenuHints, s->active, UI_FILES_KIND_FOLDER, false, false, false,
		s->hint[0], s->hint[1]);
}

/* The page for this frame, published or updated. wait as filesPaneSnapshot:
 * false (nothing changed) when a row was busy. */
static bool filesPublish(file_handle **directory, uiDrawObj_t **filePanel, bool wait)
{
	uiFilesLayout_t layout;
	file_handle **entries = filesState.active == UI_FILES_LEFT ? directory : filesOther.sorted;
	const char *dirName = filesState.active == UI_FILES_LEFT ? curDir.name : filesOther.dir.name;

	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	if(!filesPaneSnapshot(&filesSnapshot.pane[UI_FILES_LEFT], directory, UI_FILES_LEFT,
			curDir.name, &layout, wait) ||
		!filesPaneSnapshot(&filesSnapshot.pane[UI_FILES_RIGHT], filesOther.sorted,
			UI_FILES_RIGHT, filesOther.dir.name, &layout, wait) ||
		!filesInfoSnapshot(&filesSnapshot, entries, dirName, &layout, wait)) {
		return false;
	}
	if(filesSnapshot.menu.open) {
		filesMenuInfo(&filesSnapshot, &layout);
	}
	filesSnapshot.pane[UI_FILES_LEFT].listing = filesLeftListing;
	filesSnapshot.pane[UI_FILES_RIGHT].listing = filesOther.listing;
	if(*filePanel == NULL || *filePanel != filesPage ||
			!DrawUpdateFiles(*filePanel, &filesSnapshot)) {
		uiDrawObj_t *page = DrawFiles(&filesSnapshot);

		if(page == NULL) {
			return false;
		}
		*filePanel = filesPage = DrawRepublish(*filePanel, page);
	}
	return true;
}

/* The menu thread reads one visible left row's banner a frame, never
 * while drawing, as Swiss's list read its visible rows: without a meta
 * thread (a disc, a memory card, WODE...) it is the only reader, and with
 * one it reads the rows in view first and lets the meta cache drop rows
 * out of view once it is full. True when it read one. */
static bool filesMetaStep(file_handle **directory)
{
	if(filesMetaFirst != current_view_start || filesMetaListing != filesLeftListing) {
		filesMetaFirst = current_view_start;
		filesMetaListing = filesLeftListing;
		filesMetaTried = 0u;
	}
	for(int i = current_view_start; i < current_view_end && i - current_view_start < 32; i++) {
		u32 bit = 1u << (i - current_view_start);

		if(!(filesMetaTried & bit)) {
			filesMetaTried |= bit;
			if(directory[i]->meta != NULL) {
				continue;
			}
			lockFile(directory[i]);
			populate_meta(directory[i]);
			unlockFile(directory[i]);
			return true;
		}
	}
	return false;
}

/* Waits until seconds of video have gone, at the screen's own rate. */
static void filesWait(float seconds)
{
	u32 since = VIDEO_GetRetraceCount();
	float rate = VIDEO_GetRetraceRate();

	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	while((float)(VIDEO_GetRetraceCount() - since) < seconds * rate) {
		VIDEO_WaitVSync();
	}
}

/* The info bar and the hints as the focused entry has them. A box or the
 * message takes them over while it is up; when it goes they come back, so
 * nothing it said stays under what comes next (a question, the progress
 * card). */
typedef struct {
	char title[UI_FILES_ROW_TEXT], chip[12], size[24];
	char line[2][UI_FILES_TEXT_CAPACITY], hint[2][UI_FILES_HINT_CAPACITY];
	float titleScale;
	s16 titleWidth, sizeWidth;
	u8 hasBanner, infoKind, warn;
} filesInfo_t;

static void filesInfoKeep(filesInfo_t *out)
{
	const uiFilesSnapshot_t *s = &filesSnapshot;

	memcpy(out->title, s->title, sizeof(out->title));
	memcpy(out->chip, s->chip, sizeof(out->chip));
	memcpy(out->size, s->size, sizeof(out->size));
	memcpy(out->line, s->line, sizeof(out->line));
	memcpy(out->hint, s->hint, sizeof(out->hint));
	out->titleScale = s->titleScale;
	out->titleWidth = s->titleWidth;
	out->sizeWidth = s->sizeWidth;
	out->hasBanner = s->hasBanner;
	out->infoKind = s->infoKind;
	out->warn = s->warn;
}

static void filesInfoPut(const filesInfo_t *in)
{
	uiFilesSnapshot_t *s = &filesSnapshot;

	memcpy(s->title, in->title, sizeof(s->title));
	memcpy(s->chip, in->chip, sizeof(s->chip));
	memcpy(s->size, in->size, sizeof(s->size));
	memcpy(s->line, in->line, sizeof(s->line));
	memcpy(s->hint, in->hint, sizeof(s->hint));
	s->titleScale = in->titleScale;
	s->titleWidth = in->titleWidth;
	s->sizeWidth = in->sizeWidth;
	s->hasBanner = in->hasBanner;
	s->infoKind = in->infoKind;
	s->warn = in->warn;
}

/* Line 1 of the item a box chose, for a question that follows it on the
 * same press (Swiss's Move question after Actions' Move); "" otherwise. */
static char filesChosenLine[UI_FILES_TEXT_CAPACITY];

/* The box in filesMenu as the frame shows it, with the focused item's two
 * lines in the info bar. */
static void filesMenuShow(void)
{
	uiFilesLayout_t layout;

	filesSnapshot.menu = filesMenu.box;
	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	if(filesSnapshot.menu.open) {
		filesMenuInfo(&filesSnapshot, &layout);
	}
	(void)DrawUpdateFiles(filesPage, &filesSnapshot);
}

/* The box in filesMenu, open beside its row until a choice: Up and Down
 * move and wrap, A chooses an item that isn't greyed (a greyed one keeps
 * its reason showing), a letter chip's button does the same for its item,
 * and B or a close button shuts it (-1). chord: Delete's question, which
 * only L held with A answers, as Swiss's does; A alone on Delete does
 * nothing. Once it is shut the buttons that answered are let go. */
static int filesBox(uiFilesHintMode_t hints, u32 close, bool chord)
{
	static const char letters[] = "XYRLZ";
	static const u32 letterButtons[] = {PAD_BUTTON_X, PAD_BUTTON_Y, BUTTON_R, BUTTON_L, BUTTON_Z};
	uiFilesMenu_t *box = &filesMenu.box;
	u32 buttons = BUTTON_UP | BUTTON_DOWN | BUTTON_A | BUTTON_B | close;
	uiMenuInputState_t stick;
	u32 stickRetrace = VIDEO_GetRetraceCount();
	int choice = -1;
	filesInfo_t entry;

	filesInfoKeep(&entry);
	for(int i = 0; i < box->count; i++) {
		const char *at = box->letter[i] != '\0' ? strchr(letters, box->letter[i]) : NULL;

		if(at != NULL) {
			buttons |= letterButtons[at - letters];
		}
	}
	/* L+A: L is let go too before Swiss goes on, as its own prompt waits. */
	if(chord) {
		buttons |= BUTTON_L;
	}
	filesMenuHints = hints;
	box->open = 1;
	box->serial = (u16)(filesSnapshot.menu.serial + 1u);
	UIMenuInput_Init(&stick);
	(void)padsButtonsTaken(buttons);
	while(choice < 0) {
		uiMenuInputDirection_t analog;
		u32 pressed;
		int focus = box->focus, pick = -1;
		bool chorded = false;

		filesMenuShow();
		while(1) {
			pressed = padsButtonsTaken(buttons);
			analog = padsMenuInputPoll(&stick, menuInputElapsedMicroseconds(&stickRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(padsButtonsHeld() & buttons) != 0u);
			chorded = chord && (padsButtonsHeld() & (BUTTON_A | BUTTON_L)) == (BUTTON_A | BUTTON_L);
			if(chorded || pressed != 0u || analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
		}
		if(chorded) {
			choice = 0;
			menuaudio_select();
			break;
		}
		if((pressed & BUTTON_UP) || analog == UI_MENU_INPUT_UP) {
			focus = (focus + box->count - 1) % box->count;
		}
		else if((pressed & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {
			focus = (focus + 1) % box->count;
		}
		if(focus != box->focus) {
			box->focus = (u8)focus;
			menuaudio_blip();
			continue;
		}
		if(pressed & (BUTTON_B | close)) {
			break;
		}
		for(int i = 0; i < box->count; i++) {
			const char *at = box->letter[i] != '\0' ? strchr(letters, box->letter[i]) : NULL;

			if(at != NULL && (pressed & letterButtons[at - letters])) {
				pick = i;
			}
		}
		if(pick < 0 && (pressed & BUTTON_A) && !(chord && focus == 0)) {
			pick = focus;
		}
		if(pick >= 0 && ((box->dim >> pick) & 1u)) {
			box->focus = (u8)pick;
		}
		else if(pick >= 0) {
			box->focus = (u8)pick;
			choice = pick;
			menuaudio_select();
		}
	}
	/* A question that follows says where the chosen item goes; the info
	 * bar is the focused entry's again. */
	if(choice >= 0) {
		strlcpy(filesChosenLine, filesMenu.line[choice], sizeof(filesChosenLine));
	}
	box->open = 0;
	filesInfoPut(&entry);
	filesMenuShow();
	do {VIDEO_WaitVSync();} while(padsButtonsHeld() & (buttons & ~(BUTTON_UP | BUTTON_DOWN)));
	return choice;
}

/* text into out, cut in the middle as rows are, so that drawn at scale it
 * is no wider than room. */
static void filesFitAt(char *out, size_t capacity, const char *text, int room, float scale)
{
	/* What fits at the least scale in room * least / scale fits at scale. */
	(void)UIFiles_FitName(out, capacity, text, (int)((float)room * UI_FILES_NAME_MIN_SCALE / scale),
		filesMeasure);
}

/* filesMenu as a box beside the focused row of the focused pane: the title
 * (none for Actions), its items, and the width they need. */
static void filesMenuPlace(const char *title, int count)
{
	const uiFilesPaneSnapshot_t *pane = &filesSnapshot.pane[filesState.active];
	uiFilesMenu_t *box = &filesMenu.box;
	uiFilesLayout_t layout;
	const uiFilesRect_t *side;
	int widest;

	/* A title's measure, with room to spare: the box must hold it, and the
	 * box no more than its pane (UIFiles_MenuBox), so a long one is cut. */
	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	side = &layout.pane[filesState.active == UI_FILES_RIGHT];
	filesFitAt(box->title, sizeof(box->title), title, (side->x1 - side->x0 - 12 - 24) * 10 / 11,
		0.56f);
	widest = box->title[0] != '\0' ? filesWidth(box->title, 0.56f) * 11 / 10 + 24 : 0;
	box->count = (u8)count;
	box->pane = (u8)filesState.active;
	box->row = (u8)(pane->focusRow > 0 ? pane->focusRow : 0);
	for(int i = 0; i < count; i++) {
		widest = MAX(widest, filesWidth(box->item[i], 0.56f) + 32 + (box->letter[i] ? 28 : 0));
	}
	box->width = (s16)widest;
}

/* A question beside the row, Yes first: its title, its two answers, and the
 * info bar's lines under it (line NULL keeps line 1 as it is; detail is
 * amber when warn). */
static void filesQuestion(const char *title, const char *yes, const char *no,
	const char *line, const char *detail, bool warn)
{
	memset(&filesMenu, 0, sizeof(filesMenu));
	filesMenu.devices = -1;
	strlcpy(filesMenu.box.item[0], yes, sizeof(filesMenu.box.item[0]));
	strlcpy(filesMenu.box.item[1], no, sizeof(filesMenu.box.item[1]));
	for(int i = 0; i < 2; i++) {
		strlcpy(filesMenu.line[i], line != NULL ? line : filesChosenLine[0] != '\0' ?
			filesChosenLine : filesSnapshot.line[0], sizeof(filesMenu.line[i]));
		strlcpy(filesMenu.reason[i], detail, sizeof(filesMenu.reason[i]));
	}
	filesMenu.warn = warn ? 3u : 0u;
	filesMenuPlace(title, 2);
}

/* entries[index]'s name and type, for UIFiles_LandingIndex. */
static void filesEntryAt(const void *context, int index, const char **name, int *fileType)
{
	file_handle *const *entries = context;

	*name = entries[index]->name;
	*fileType = entries[index]->fileType;
}

/* The Copy question's ghost row: the other pane shows where the copy will
 * land until the question is answered. */
static int filesGhostPane = -1;
static uiFilesPaneSnapshot_t filesGhostSaved;

static void filesGhostOn(int pane, int landing, const char *name, const char *size)
{
	uiFilesLayout_t layout;
	uiFilesRowSnapshot_t row;
	uiFilesPaneSnapshot_t *shown = &filesSnapshot.pane[pane];

	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	memset(&row, 0, sizeof(row));
	row.kind = (u8)UIFiles_Kind(name, IS_FILE);
	UIFiles_RowMeta(row.meta, sizeof(row.meta), row.kind, size);
	row.scale = UIFiles_FitName(row.name, sizeof(row.name), getRelativeName((char *)name),
		UIFiles_NameWidth(&layout, pane, row.meta[0] != '\0' ? filesWidth(row.meta, 0.44f) : 0,
		shown->count + 1 > UI_FILES_ROWS), filesMeasure);
	filesGhostSaved = *shown;
	filesGhostPane = pane;
	UIFiles_InsertGhost(shown, landing, &row);
}

static void filesGhostOff(void)
{
	if(filesGhostPane >= 0) {
		filesSnapshot.pane[filesGhostPane] = filesGhostSaved;
		filesGhostPane = -1;
	}
}

/* confirmAction's question as a box beside the row: its title, its verb
 * focused and Cancel, its second line in the info bar's amber line; only
 * the verb says yes. -1 when text isn't such a question. */
static int filesAsk(const char *text)
{
	uiFilesQuestion_t question;
	int choice;

	if(!UIFiles_ParseQuestion(text, &question)) {
		return -1;
	}
	filesQuestion(question.title, question.verb, question.cancel, NULL, question.detail,
		question.detail[0] != '\0');
	choice = filesBox(UI_FILES_HINTS_QUESTION, 0u, false);
	filesGhostOff();
	return choice == 0;
}

/* Delete's question, with Swiss's words (text's first line) and Swiss's
 * chord: L held and A deletes; B, or A on Cancel, doesn't. */
static bool filesAskDelete(const char *text)
{
	char title[UI_FILES_TEXT_CAPACITY];
	size_t length = strcspn(text, "\n");

	strlcpy(title, text, MIN(sizeof(title), length + 1));
	filesQuestion(title, "Delete", "Cancel", NULL, "Hold L and press A to delete.", true);
	filesMenu.box.rose = 1;
	return filesBox(UI_FILES_HINTS_DELETE, 0u, true) == 0;
}

/* A copy's target is there already: Keep both (Swiss's Rename, the next
 * free _NN), Replace it or Cancel, as the buttons Swiss's box reads (A, Z,
 * B). Keep both is greyed when only replacing makes room. */
static bool filesFitsBoth = true;

static u32 filesAskExists(const char *destName)
{
	uiFilesChoices_t choices;
	char title[UI_FILES_TEXT_CAPACITY], folder[PATHNAME_MAX];
	int choice;

	UIFiles_ExistsChoices(filesFitsBoth, &choices);
	strlcpy(folder, destName, sizeof(folder));
	getParentPath(folder, folder);
	/* The file's name, however long, is the info bar's line 1. */
	snprintf(title, sizeof(title), "It's already in %s.", *getRelativeName(folder) != '\0' ?
		getRelativeName(folder) : DeviceDisplayName(devices[DEVICE_DEST]));
	memset(&filesMenu, 0, sizeof(filesMenu));
	filesMenu.devices = -1;
	for(int i = 0; i < 3; i++) {
		strlcpy(filesMenu.box.item[i], choices.item[i], sizeof(filesMenu.box.item[i]));
		strlcpy(filesMenu.line[i], getRelativeName((char *)destName), sizeof(filesMenu.line[i]));
	}
	strlcpy(filesMenu.reason[0], choices.reason, sizeof(filesMenu.reason[0]));
	filesMenu.warn = choices.dim;
	filesMenu.box.dim = (u16)choices.dim;
	filesMenu.box.focus = (u8)choices.focus;
	filesMenuPlace(title, 3);
	choice = filesBox(UI_FILES_HINTS_QUESTION, 0u, false);
	return choice == 0 ? BUTTON_A : choice == 1 ? BUTTON_Z : BUTTON_B;
}

/* How an operation the File Browser ran ended: said done, failed, stopped,
 * or (nothing said) not run. */
enum { FILES_SAID_NOTHING = 0, FILES_SAID_DONE, FILES_SAID_STOPPED, FILES_SAID_FAILED };
static int filesOutcome;
/* What is copied, as the info bar named it: its banner's title, else its
 * file name. */
static char filesCopyName[UI_FILES_ROW_TEXT];

/* A result as the File Browser says it: the maroon message over the page,
 * each line cut to the stage, gone after 2 s or on A or B; a failure stays
 * until A. It fades out over 0.15 s unless UI Motion is Off. */
static void filesSay(const char *title, const char *detail, bool failed)
{
	u32 shown = VIDEO_GetRetraceCount();
	float rate = VIDEO_GetRetraceRate();
	filesInfo_t entry;

	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	filesFitAt(filesSnapshot.message[0], sizeof(filesSnapshot.message[0]), title, 560, 0.56f);
	filesFitAt(filesSnapshot.message[1], sizeof(filesSnapshot.message[1]), detail, 560, 0.46f);
	filesSnapshot.messageWidth = (s16)MAX(filesWidth(filesSnapshot.message[0], 0.56f),
		filesWidth(filesSnapshot.message[1], 0.46f)) + 48;
	filesSnapshot.messageLeaving = 0;
	filesSnapshot.messageSerial++;
	filesSnapshot.menu.open = 0;
	filesInfoKeep(&entry);
	filesSnapshot.line[0][0] = filesSnapshot.line[1][0] = '\0';
	filesSnapshot.warn = 0;
	UIFiles_Hints(UI_FILES_HINTS_MESSAGE, filesState.active, UI_FILES_KIND_FOLDER, false,
		false, false, filesSnapshot.hint[0], filesSnapshot.hint[1]);
	(void)DrawUpdateFiles(filesPage, &filesSnapshot);
	(void)padsButtonsTaken(BUTTON_A | BUTTON_B);
	while(1) {
		u32 pressed = padsButtonsTaken(BUTTON_A | BUTTON_B);

		if((pressed & BUTTON_A) || (!failed && ((pressed & BUTTON_B) ||
				(float)(VIDEO_GetRetraceCount() - shown) >= 2.0f * rate))) {
			break;
		}
		VIDEO_WaitVSync();
	}
	if(UIMotion_ModeFromFlags(swissSettings.disableUIAnimations,
			swissSettings.reduceUIAnimations) != UI_MOTION_OFF) {
		filesSnapshot.messageLeaving = 1;
		filesSnapshot.messageSerial++;
		(void)DrawUpdateFiles(filesPage, &filesSnapshot);
		filesWait(0.15f);
	}
	filesSnapshot.messageLeaving = 0;
	filesSnapshot.message[0][0] = filesSnapshot.message[1][0] = '\0';
	filesInfoPut(&entry);
	(void)DrawUpdateFiles(filesPage, &filesSnapshot);
	while(padsButtonsHeld() & (BUTTON_A | BUTTON_B)) VIDEO_WaitVSync();
}

/* A result said while Swiss's operation runs is kept until both panes have
 * been read again, so the message sits over what the operation left:
 * filesSayLater keeps it, filesSayPending says it. flashPane: the pane
 * whose focused row is a copy that just landed, which flashes under it. */
static struct {
	char title[UI_FILES_TEXT_CAPACITY], detail[UI_FILES_TEXT_CAPACITY];
	bool failed, pending;
	int flashPane;
} filesLater = {.flashPane = -1};

static void filesSayLater(const char *title, const char *detail, bool failed)
{
	strlcpy(filesLater.title, title, sizeof(filesLater.title));
	strlcpy(filesLater.detail, detail, sizeof(filesLater.detail));
	filesLater.failed = failed;
	filesLater.pending = true;
	filesOutcome = failed ? FILES_SAID_FAILED : FILES_SAID_DONE;
}

static void filesSayPending(void)
{
	uiFilesPaneSnapshot_t *pane = filesLater.flashPane >= 0 ?
		&filesSnapshot.pane[filesLater.flashPane] : NULL;
	bool flash = pane != NULL && pane->focusRow >= 0 && pane->focusRow < pane->rows;

	filesLater.flashPane = -1;
	if(!filesLater.pending) {
		return;
	}
	filesLater.pending = false;
	if(flash) {
		pane->row[pane->focusRow].flags |= UI_FILES_ROW_FLASH;
	}
	filesSay(filesLater.title, filesLater.detail, filesLater.failed);
	if(flash) {
		pane->row[pane->focusRow].flags &= (u8)~UI_FILES_ROW_FLASH;
	}
}

/* How a copy or a move ended, as the File Browser says it once the panes
 * are read again. dest is where it was going. */
static void filesSayCopy(uiFilesResult_t result, bool move, const char *dest, int code,
	bool removed, bool replaced)
{
	char lines[2][UI_FILES_TEXT_CAPACITY], folder[PATHNAME_MAX];

	strlcpy(folder, dest, sizeof(folder));
	getParentPath(folder, folder);
	UIFiles_Result(result, move, filesCopyName, DeviceDisplayName(devices[DEVICE_CUR]),
		DeviceDisplayName(devices[DEVICE_DEST]), getRelativeName(folder), code, removed,
		replaced, lines);
	filesSayLater(lines[0], lines[1], result == UI_FILES_RESULT_WRITE_FAILED ||
		result == UI_FILES_RESULT_READ_FAILED || result == UI_FILES_RESULT_KEPT);
	filesOutcome = result == UI_FILES_RESULT_STOPPED ? FILES_SAID_STOPPED :
		result == UI_FILES_RESULT_DONE || result == UI_FILES_RESULT_KEPT ? FILES_SAID_DONE :
		FILES_SAID_FAILED;
}

/* A copy's progress card in the File Browser: "Copying <it> to <storage>",
 * the destination's path, and B Stop. */
static uiDrawObj_t *filesProgress(int option, const char *destName)
{
	char title[UI_FILES_TEXT_CAPACITY + 64], path[PATHNAME_MAX + 64];
	const char *there = DeviceDisplayName(devices[DEVICE_DEST]);

	snprintf(title, sizeof(title), "%s %s to %s", option == MOVE_OPTION ? "Moving" : "Copying",
		filesCopyName, there);
	snprintf(path, sizeof(path), "%s  \233  %s", there, getDevicePath((char *)destName));
	return DrawProgressBarFiles(title, path);
}

/* The folder a program folder stands for: its path, as a folder, on the
 * entry's storage. */
static void filesProgramFolder(const file_handle *entry, const char *dirName, file_handle *out)
{
	memcpy(out, entry, sizeof(file_handle));
	UIFiles_ProgramFolderPath(out->name, sizeof(out->name), entry->name, dirName);
	out->fileType = IS_DIR;
	out->size = 0;
	out->meta = NULL;
	out->fp = NULL;
	out->ffsFp = NULL;
	out->uiObj = NULL;
	out->lockCount = 0;
	out->thread = LWP_THREAD_NULL;
	/* The folder's own attributes, not its program's: Hide then says
	 * Unhide on a hidden one. */
	out->fileAttrib = 0;
	if(devices[DEVICE_CUR] != NULL && devices[DEVICE_CUR]->statFile != NULL) {
		devices[DEVICE_CUR]->statFile(out);
		out->fileType = IS_DIR;
	}
}

/* A side as UIFiles_Availability sees it, with its free space when it is
 * mounted and gives a real figure. */
static void filesSide(uiFilesSide_t *out, DEVICEHANDLER_INTERFACE *device, const char *folder,
	int mount, bool readOk)
{
	device_info *info = NULL;

	memset(out, 0, sizeof(*out));
	filesDevice(device, &out->device);
	out->folder = folder;
	out->mount = (u8)mount;
	out->readOk = readOk;
	if((mount == UI_FILES_SHARED || mount == UI_FILES_OWN) && device->info != NULL) {
		info = device->info(device->initial);
	}
	out->freeKnown = UIFiles_FreeKnown(info != NULL, info != NULL ? info->totalSpace : 0u,
		out->device.network);
	out->freeBytes = out->freeKnown ? info->freeSpace : 0u;
}

/* The name pane's row index is focused again by after a re-read: a left
 * program folder's own path, else the entry's; "" for no row. */
static void filesNameAt(int pane, file_handle **directory, int index, char *out, size_t capacity)
{
	file_handle **entries = pane == UI_FILES_LEFT ? directory : filesOther.sorted;
	int count = pane == UI_FILES_LEFT ? getSortedDirEntryCount() : filesOther.count;

	out[0] = '\0';
	if(index < 0 || index >= count) {
		return;
	}
	if(pane == UI_FILES_LEFT) {
		lockFile(entries[index]);
	}
	if(pane == UI_FILES_LEFT && filesKind(entries[index], pane, curDir.name) ==
			UI_FILES_KIND_PROGRAM_FOLDER) {
		UIFiles_ProgramFolderPath(out, capacity, entries[index]->name, curDir.name);
	}
	else {
		strlcpy(out, entries[index]->name, capacity);
	}
	if(pane == UI_FILES_LEFT) {
		unlockFile(entries[index]);
	}
}

/* filesManageFrom: one action on a pane's entry, to the other pane's
 * folder, through Swiss's own manage_file_ex. A right-pane entry runs with
 * the slots swapped (its storage as the Source, the Source as the
 * destination); both slots come back on the one way out. True when the
 * panes must be read again. */
/* The left pane keeps open every game and DOL whose banner it read, and a
 * file held open can't be replaced, deleted or renamed through another
 * handle (FatFs locks it). Before an operation they are closed: the banners
 * are kept, and a later read opens the file again. */
static void filesCloseLeft(void)
{
	file_handle *entries = getCurrentDirEntries();
	int i, count = getCurrentDirEntryCount();

	for(i = 0; devices[DEVICE_CUR] != NULL && entries != NULL && i < count; i++) {
		devices[DEVICE_CUR]->closeFile(&entries[i]);
	}
}

static bool filesManageFrom(file_handle *entry, int pane, int option, const char *destDir)
{
	DEVICEHANDLER_INTERFACE *cur = devices[DEVICE_CUR], *dest = devices[DEVICE_DEST];
	bool fromRight = pane == UI_FILES_RIGHT;
	bool changed;

	meta_thread_stop();
	filesCloseLeft();
	devices[DEVICE_CUR] = fromRight ? filesOther.device : cur;
	devices[DEVICE_DEST] = fromRight ? cur : filesOther.device;
	memcpy(&curFile, entry, sizeof(file_handle));
	filesBoxes = true;
	changed = manage_file_ex(option, destDir);
	filesBoxes = false;
	memcpy(entry, &curFile, sizeof(file_handle));
	devices[DEVICE_CUR] = cur;
	devices[DEVICE_DEST] = dest;
	return changed;
}

/* Z, or A on a file that doesn't start here: the Actions box beside the
 * focused row of pane, then what it chose on that entry, Copy and Move to
 * the other pane's folder. Copy asks first, with a ghost row where the copy
 * will land; Move, Hide and Delete ask Swiss's questions as boxes. Focus
 * afterwards goes by name: the left pane's into curFile, the right's into
 * its focusName. True when both panes must be read again. */
static bool filesActions(file_handle **directory, int pane)
{
	static const int options[UI_FILES_ACTIONS] = {
		COPY_OPTION, MOVE_OPTION, RENAME_OPTION, HIDE_OPTION, DELETE_OPTION
	};
	static file_handle folder;
	bool left = pane == UI_FILES_LEFT;
	DEVICEHANDLER_INTERFACE *here = left ? devices[DEVICE_CUR] : filesOther.device;
	DEVICEHANDLER_INTERFACE *there = left ? filesOther.device : devices[DEVICE_CUR];
	const char *hereDir = left ? curDir.name : filesOther.dir.name;
	const char *thereDir = left ? filesOther.dir.name : curDir.name;
	file_handle **thereList = left ? filesOther.sorted : directory;
	int thereCount = left ? filesOther.count : getSortedDirEntryCount();
	const uiFilesPaneState_t *state = &filesState.pane[pane];
	file_handle *entry = (left ? directory : filesOther.sorted)[state->focus];
	bool card = here == &__device_card_a || here == &__device_card_b;
	bool toCard = there == &__device_card_a || there == &__device_card_b;
	uiFilesSide_t from, to;
	uiFilesEntry_t what;
	uiFilesAvailability_t avail;
	uiFilesFocusAfter_t after;
	char landing[PATHNAME_MAX], was[PATHNAME_MAX], name[PATHNAME_MAX];
	int action;
	bool changed, done;

	meta_thread_stop();
	memset(&what, 0, sizeof(what));
	if(left) {
		lockFile(entry);
	}
	/* A program folder acts as the folder, never the program in it. */
	what.programFolder = filesKind(entry, pane, hereDir) == UI_FILES_KIND_PROGRAM_FOLDER;
	if(what.programFolder) {
		filesProgramFolder(entry, hereDir, &folder);
	}
	strlcpy(filesCopyName, left && entry->meta != NULL && entry->meta->displayName != NULL &&
		entry->meta->displayName[0] != '\0' ? entry->meta->displayName :
		getRelativeName(entry->name), sizeof(filesCopyName));
	if(left) {
		unlockFile(entry);
	}
	if(what.programFolder) {
		entry = &folder;
	}
	filesSide(&from, here, hereDir, left ? UI_FILES_SHARED : filesOther.mount, true);
	filesSide(&to, there, thereDir, left ? filesOther.mount : UI_FILES_SHARED,
		left ? !filesOther.readFailed : true);
	what.pane = pane;
	what.isFile = entry->fileType == IS_FILE;
	what.hidden = (entry->fileAttrib & ATTRIB_HIDDEN) != 0;
	what.needed = entry->size + (card && !toCard ? sizeof(GCI) : 0u);
	manageDestName(landing, thereDir, entry->name, here, there);
	for(int i = 0; i < thereCount; i++) {
		if(!strcasecmp(thereList[i]->name, landing)) {
			what.exists = true;
			what.existsFolder = thereList[i]->fileType == IS_DIR;
			what.existingSize = thereList[i]->size;
		}
	}
	UIFiles_Availability(&from, &to, &what, &avail);

	memset(&filesMenu, 0, sizeof(filesMenu));
	filesMenu.devices = -1;
	for(int i = 0; i < UI_FILES_ACTIONS; i++) {
		strlcpy(filesMenu.box.item[i], avail.label[i], sizeof(filesMenu.box.item[i]));
		filesMenu.box.letter[i] = avail.letter[i];
		filesMenu.box.dim |= (u16)(!avail.enabled[i] << i);
		filesMenu.warn |= (u16)(avail.warn[i] << i);
		UIFiles_ActionLine((uiFilesAction_t)i, what.hidden, DeviceDisplayName(here),
			DeviceDisplayName(there), getDevicePath((char *)thereDir), filesMenu.line[i],
			sizeof(filesMenu.line[i]));
		strlcpy(filesMenu.reason[i], avail.line[i], sizeof(filesMenu.reason[i]));
	}
	filesMenu.box.focus = (u8)UIFiles_FirstEnabled(avail.enabled, UI_FILES_ACTIONS);
	filesMenuPlace("", UI_FILES_ACTIONS);
	action = filesBox(UI_FILES_HINTS_BOX, 0u, false);
	/* Only what the box allowed: a Move it greys never runs as a copy. */
	if(action < 0 || !avail.enabled[action]) {
		return false;
	}
	if(action == UI_FILES_ACTION_COPY) {
		char title[64], line[UI_FILES_TEXT_CAPACITY], size[24], free[24] = "";
		file_handle **list = left ? filesOther.sorted : directory;

		snprintf(title, sizeof(title), "Copy to %s?", DeviceDisplayName(there));
		if(to.freeKnown) {
			UIFiles_SizeText(free, sizeof(free), to.freeBytes, filesBlockSize(there),
				to.device.metric);
		}
		snprintf(line, sizeof(line), free[0] != '\0' ? "To %s  \233  %s  \267  %s free" :
			"To %s  \233  %s", DeviceDisplayName(there), getDevicePath((char *)thereDir), free);
		filesSizeText(size, sizeof(size), entry);
		filesQuestion(title, "Yes", "No", line, avail.line[action], avail.warn[action]);
		filesGhostOn(left ? UI_FILES_RIGHT : UI_FILES_LEFT,
			UIFiles_LandingIndex(list, thereCount, filesEntryAt, landing, IS_FILE), landing, size);
		if(filesBox(UI_FILES_HINTS_QUESTION, 0u, false) != 0) {
			filesGhostOff();
			return false;
		}
		filesGhostOff();
	}
	else if(action == UI_FILES_ACTION_MOVE) {
		/* Swiss's Move question shows the ghost row too. */
		char size[24];

		filesSizeText(size, sizeof(size), entry);
		filesGhostOn(left ? UI_FILES_RIGHT : UI_FILES_LEFT,
			UIFiles_LandingIndex(left ? filesOther.sorted : directory, thereCount, filesEntryAt,
			landing, IS_FILE), landing, size);
	}

	filesFitsBoth = !avail.replaceOnly[action];
	filesOutcome = FILES_SAID_NOTHING;
	strlcpy(was, entry->name, sizeof(was));
	if(left && !what.programFolder) {
		lockFile(entry);
	}
	changed = filesManageFrom(entry, pane, options[action], thereDir);
	if(left && !what.programFolder) {
		unlockFile(entry);
	}
	filesGhostOff();
	done = action == UI_FILES_ACTION_HIDE ? changed : filesOutcome == FILES_SAID_DONE;

	/* Focus, by name, before the panes are read again: this pane's per
	 * UIFiles_FocusAfter, the other's on what landed there. */
	UIFiles_FocusAfter((uiFilesAction_t)action, done, state->focus, state->count,
		swissSettings.showHiddenFiles, what.hidden, &after);
	/* The message waits for the panes to be read again; with nothing
	 * changed they stay as they are, so it comes now. */
	filesLater.flashPane = after.flash ? (left ? UI_FILES_RIGHT : UI_FILES_LEFT) : -1;
	if(!changed) {
		filesSayPending();
	}
	if(after.sourceIndex == UI_FILES_FOCUS_NEW_NAME) {
		strlcpy(name, entry->name, sizeof(name));
	}
	else {
		filesNameAt(pane, directory, after.sourceIndex, name, sizeof(name));
	}
	if(left) {
		strlcpy(curFile.name, name, sizeof(curFile.name));
		if(after.otherToNew) {
			strlcpy(filesOther.focusName, manageLanded, sizeof(filesOther.focusName));
		}
	}
	else {
		strlcpy(filesOther.focusName, name, sizeof(filesOther.focusName));
		filesLeftFocusName(directory, curFile.name, sizeof(curFile.name));
		if(after.otherToNew) {
			strlcpy(curFile.name, manageLanded, sizeof(curFile.name));
		}
		/* The left pane's folder, or one above it, renamed or deleted on
		 * the right: the left goes to the folder that held it, focusing it
		 * by its new name, rather than reading a folder that is gone. */
		if(changed && filesOther.device == devices[DEVICE_CUR] && filesWithin(curDir.name, was)) {
			memcpy(&curDir, &filesOther.dir, sizeof(file_handle));
			strlcpy(curFile.name, entry->name, sizeof(curFile.name));
		}
	}
	return changed;
}

/* L or R: the storage menu beside that side's button, as Memory Cards'.
 * Up and Down move and wrap, A chooses (a greyed device keeps its reason
 * showing), B, L or R close it. The choice: an item, filesMenu.devices
 * being Other devices..., or -1. */
static int filesStorageMenu(int pane, file_handle **directory, uiDrawObj_t **filePanel)
{
	uiFilesDevice_t listed[UI_FILES_STORAGE_DEVICES], current, other;
	DEVICEHANDLER_INTERFACE *mine = pane == UI_FILES_LEFT ? devices[DEVICE_CUR] : filesOther.device;
	DEVICEHANDLER_INTERFACE *theirs = pane == UI_FILES_LEFT ? filesOther.device : devices[DEVICE_CUR];
	int count = 0, widest, choice;

	/* Every detected storage that can be read, in Swiss's order. */
	for(int i = 0; i < MAX_DEVICES && count < UI_FILES_STORAGE_DEVICES; i++) {
		DEVICEHANDLER_INTERFACE *device = allDevices[i];

		if(device != NULL && (device->features & FEAT_READ) &&
				deviceHandler_getDeviceAvailable(device)) {
			filesMenuDevices[count] = device;
			filesDevice(device, &listed[count++]);
		}
	}
	filesDevice(mine, &current);
	filesDevice(theirs, &other);
	UIFiles_StorageMenu(pane, listed, count, &current, &other,
		getDevicePath(pane == UI_FILES_LEFT ? filesOther.dir.name : curDir.name), &filesMenu);
	widest = filesWidth(filesMenu.box.title, 0.56f) + 24;
	for(int i = 0; i < filesMenu.box.count; i++) {
		widest = MAX(widest, filesWidth(filesMenu.box.item[i], 0.56f) + 32);
	}
	filesMenu.box.width = (s16)widest;
	choice = filesBox(UI_FILES_HINTS_BOX, BUTTON_L | BUTTON_R, false);
	(void)filesPublish(directory, filePanel, true);
	return choice;
}

/* L, a device: it becomes the Source in place, as the Source picker makes
 * it. The right pane lets go first: the old Source may be its storage too.
 * menu_loop reads the new Source's top folder next (the listing in hand is
 * gone), or, when it won't mount, opens the Source picker. */
static void filesSourceChange(DEVICEHANDLER_INTERFACE *device)
{
	filesOtherRelease();
	meta_thread_stop();
	sourceCommit(device);
	(void)sourceMount();
}

/* Y: the right pane's storage and folder become the Source, and the
 * Source's the right pane's, each side keeping its focus. Only onto a
 * right pane that is ready, else (false) the info bar says why. */
static bool filesSwapSides(file_handle **directory)
{
	static file_handle rightDir;
	DEVICEHANDLER_INTERFACE *left = devices[DEVICE_CUR], *right = filesOther.device;
	const uiFilesPaneState_t *pane = &filesState.pane[UI_FILES_RIGHT];
	char leftFocus[PATHNAME_MAX], rightFocus[PATHNAME_MAX];

	if(!UIFiles_CanSwap(filesOther.mount, !filesOther.readFailed)) {
		snprintf(filesNoteText, sizeof(filesNoteText), "%s isn't ready, so the sides can't swap.",
			DeviceDisplayName(right));
		filesNote = filesNoteText;
		return false;
	}
	/* What the panes hold goes before the sides change places, and comes
	 * back with the next page (UI Motion Off: it stays). */
	filesSnapshot.swapping = 1;
	(void)DrawUpdateFiles(filesPage, &filesSnapshot);
	filesWait(UIFiles_SwapHalfSeconds(UIMotion_ModeFromFlags(swissSettings.disableUIAnimations,
		swissSettings.reduceUIAnimations)));
	filesLeftFocusName(directory, leftFocus, sizeof(leftFocus));
	strlcpy(rightFocus, pane->count > 0 ? filesOther.sorted[pane->focus]->name : "",
		sizeof(rightFocus));
	memcpy(&rightDir, &filesOther.dir, sizeof(file_handle));
	memcpy(&filesOther.dir, &curDir, sizeof(file_handle));
	if(right == left) {
		/* One storage on both sides: only the folders change places. */
		filesOtherFree();
		memcpy(&curDir, &rightDir, sizeof(file_handle));
		needsRefresh = 1;
	}
	else {
		filesOtherRelease();
		meta_thread_stop();
		filesOther.device = left;
		sourceCommit(right);
		if(sourceMount()) {
			memcpy(&curDir, &rightDir, sizeof(file_handle));
		}
	}
	strlcpy(filesOther.focusName, leftFocus, sizeof(filesOther.focusName));
	strlcpy(curFile.name, rightFocus, sizeof(curFile.name));
	return true;
}

/* The File Browser on the shared listing, as Swiss's lists were: it returns
 * to menu_loop for a left folder change, a Source change, B, START, a
 * launch, and anything that needs the listing read again. */
static void filesOpenDetail(file_handle **directory, uiDrawObj_t **filePanel);

static uiDrawObj_t* renderFileList(file_handle** directory, int num_files, uiDrawObj_t* filePanel)
{
	const u32 waitButtons = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT |
		BUTTON_A | BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y | BUTTON_Z | BUTTON_L |
		BUTTON_R | BUTTON_START | BUTTON_CLAP;
	const u32 vertical = BUTTON_UP | BUTTON_DOWN;
	uiMenuInputState_t menuInput, pageInput;
	u32 menuInputRetrace, pageInputRetrace, repeatHeld = 0u, repeatAt = 0u;
	float rate = VIDEO_GetRetraceRate();
	uiDrawObj_t *loadingBox;
	int storage = -1, choice;

	gameflowListFallback = false;
	if(num_files<=0) {
		memcpy(&curDir, devices[DEVICE_CUR]->initial, sizeof(file_handle));
		needsRefresh=1;
		return filePanel;
	}
	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	if(filePanel == NULL || filePanel != filesPage) {
		/* Opening: the left pane has the focus, the right comes back where
		 * it was. It asks for no scene, so the cube stays as Home or the
		 * Library left it, and goes back there after anything that turns
		 * it (a game's info coming back to the list). */
		filesState.active = UI_FILES_LEFT;
		filesNote = NULL;
		filesSnapshot.menu.open = 0;
		filesScene = filePanel == NULL ? UI_SCENE_HOME : UI_SCENE_LIBRARY;
	}
	else {
		UIScene_Request(filesScene);
	}
	if(curSelection == 0 && num_files > 1 && directory[0]->fileType == IS_SPECIAL) {
		curSelection = 1; // skip the ".." by default
	}
	UIFiles_SetPane(&filesState, UI_FILES_LEFT, num_files, curSelection);
	curSelection = filesState.pane[UI_FILES_LEFT].focus;
	UIFiles_LeftView(&filesState, &current_view_start, &current_view_end);
	filesLeftListing++;
	filesFreeText(devices[DEVICE_CUR], filesFree, sizeof(filesFree));
	filesOtherAcquire();
	/* Presses from before the page shows this (the A that opened it, a B in
	 * a box, one made while a device was set up) aren't for here; those made
	 * while a folder was read are. */
	if(!filesKeepPresses) {
		(void)padsButtonsTaken(waitButtons);
	}
	filesKeepPresses = false;
	filesSnapshot.swapping = 0;
	if(!filesPublish(directory, &filePanel, true)) {
		/* No memory for the page: Home rather than a stale screen. */
		filesOtherRelease();
		curMenuLocation = ON_OPTIONS;
		return filePanel;
	}
	/* What an operation said, over both panes as it left them. */
	filesSayPending();
	loadingBox = DrawPublish(DrawProgressLoading(PROGRESS_BOX_FILES));
	meta_thread_start(loadingBox);
	UIMenuInput_Init(&menuInput);
	UIMenuInput_Init(&pageInput);
	menuInputRetrace = pageInputRetrace = VIDEO_GetRetraceCount();
	while(1) {
		uiFilesPaneState_t *active;
		uiMenuInputDirection_t analog, page;
		u32 buttons, held;
		int frames = 0;
		bool moved = false;

		while(1) {
			u32 now = VIDEO_GetRetraceCount();

			held = padsButtonsHeld();
			buttons = padsButtonsTaken(waitButtons);
			/* Up and Down held: again after 320 ms, then every 120 ms. */
			if((held & vertical) != repeatHeld) {
				repeatHeld = held & vertical;
				repeatAt = now + (u32)(0.32f * rate);
			}
			else if(repeatHeld != 0u && (s32)(now - repeatAt) >= 0) {
				buttons |= repeatHeld;
				repeatAt = now + (u32)(0.12f * rate);
			}
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(held & waitButtons) != 0u);
			page = padsSubMenuInputPoll(&pageInput,
				menuInputElapsedMicroseconds(&pageInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(held & waitButtons) != 0u);
			if(buttons != 0u || analog != UI_MENU_INPUT_NONE ||
					page != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
			/* Banners arrive: from this thread a row a frame, else from the
			 * meta thread, looked for four times a second. */
			if(filesMetaStep(directory) || ++frames % 15 == 0) {
				(void)filesPublish(directory, &filePanel, false);
			}
		}
		filesChosenLine[0] = '\0';
		filesNote = NULL;
		active = &filesState.pane[filesState.active];
		/* Moves first: a press of Down and A acts on the row moved to. */
		if((buttons & BUTTON_UP) || analog == UI_MENU_INPUT_UP) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_UP);
		}
		if((buttons & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_DOWN);
		}
		if(page == UI_MENU_INPUT_UP) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_PAGE_UP);
		}
		if(page == UI_MENU_INPUT_DOWN) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_PAGE_DOWN);
		}
		if(buttons & BUTTON_LEFT) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_LEFT);
		}
		if(buttons & BUTTON_RIGHT) {
			moved |= UIFiles_Input(&filesState, UI_FILES_INPUT_RIGHT);
		}
		if(buttons & BUTTON_CLAP) {
			filesBarrelGame(loadingBox);
			UIFiles_SetPane(&filesState, UI_FILES_LEFT, num_files, curSelection);
			moved = true;
		}
		active = &filesState.pane[filesState.active];
		curSelection = filesState.pane[UI_FILES_LEFT].focus;
		UIFiles_LeftView(&filesState, &current_view_start, &current_view_end);
		if(moved) {
			menuaudio_blip();
		}

		/* L and R: each side's storage; X and ".." at a side's top open
		 * its menu too. */
		storage = (buttons & BUTTON_L) ? UI_FILES_LEFT : (buttons & BUTTON_R) ?
			UI_FILES_RIGHT : -1;
		if(filesState.active == UI_FILES_LEFT && storage < 0) {
			if(buttons & BUTTON_A) {
				int type;
				bool loads, detail;

				/* What A does depends on the meta (a program folder, a
				 * second disc): read it now if no one has yet. */
				lockFile(directory[curSelection]);
				populate_meta(directory[curSelection]);
				type = directory[curSelection]->fileType;
				loads = filesLoads(directory[curSelection]);
				detail = loads && filesOpensDetail(directory[curSelection]);
				unlockFile(directory[curSelection]);
				if(type == IS_SPECIAL && filesAtRoot(&curDir)) {
					storage = UI_FILES_LEFT;
				}
				else if(type == IS_FILE && !loads) {
					/* A file that doesn't start here: its Actions. */
					if(fileManagementAllowed() && filesActions(directory, UI_FILES_LEFT)) {
						filesOther.listed = false;
						needsRefresh = 1;
						break;
					}
					meta_thread_stop();
					meta_thread_start(loadingBox);
				}
				else if(detail) {
					/* A game: its Detail, as the Library's. The page under it
					 * changes, and the wheel goes with the old one, once the
					 * banner thread that writes to it has stopped. */
					meta_thread_stop();
					DrawDispose(loadingBox);
					loadingBox = NULL;
					filesOpenDetail(directory, &filePanel);
					break;
				}
				else {
					/* Starting a file: nothing stays mounted that the game,
					 * its details or the loader don't know about. */
					if(type == IS_FILE && loads) {
						filesOtherRelease();
					}
					/* A firmware file asks first, as a box beside its row. */
					filesBoxes = type == IS_FILE && UIFiles_Kind(directory[curSelection]->name,
						type) == UI_FILES_KIND_FIRMWARE;
					filesActivate(directory, false);
					filesBoxes = false;
					filesKeepPresses = type == IS_DIR || type == IS_SPECIAL;
					/* Z's box on a file that can't start changed something:
					 * the right pane is read again too. */
					if(type == IS_FILE && needsRefresh) {
						filesOther.listed = false;
					}
					break;
				}
			}
			else if((buttons & PAD_BUTTON_X) && filesAtRoot(&curDir)) {
				storage = UI_FILES_LEFT;
			}
			else if(buttons & PAD_BUTTON_X) {
				filesUp(directory[0]);
				filesKeepPresses = true;
				/* At the top the Source picker opens: it must not see X
				 * still held, which is its own button. */
				if(needsDeviceChange) {
					while(padsButtonsHeld() & PAD_BUTTON_X) VIDEO_WaitVSync();
				}
				break;
			}
			else if((buttons & BUTTON_Z) && directory[curSelection]->fileType == IS_SPECIAL &&
					(fileManagementAllowed() || filesAutoloadFolder(curDir.name))) {
				/* Autoload: on with File Management, off always (the folder
				 * Indigo starts in is where you'd turn it off). A settings
				 * save mounts and unmounts the Configuration Device, which
				 * may be the right pane's. */
				filesBoxes = true;
				filesOtherRelease();
				filesToggleAutoload(&curDir.name[0]);
				filesOtherAcquire();
				filesBoxes = false;
			}
			else if((buttons & BUTTON_Z) && fileManagementAllowed()) {
				if(filesActions(directory, UI_FILES_LEFT)) {
					filesOther.listed = false;
					needsRefresh = 1;
					break;
				}
				/* Nothing changed: the banners go on arriving. */
				meta_thread_stop();
				meta_thread_start(loadingBox);
			}
		}
		else if(storage < 0 && (buttons & (BUTTON_A | PAD_BUTTON_X | BUTTON_Z))) {
			file_handle *entry = active->count > 0 ? filesOther.sorted[active->focus] : NULL;
			bool parent = (buttons & PAD_BUTTON_X) || (!(buttons & BUTTON_Z) &&
				(entry == NULL || entry->fileType == IS_SPECIAL));

			if(parent) {
				if(filesAtRoot(&filesOther.dir) || filesOther.mount == UI_FILES_FAILED) {
					storage = UI_FILES_RIGHT;
				}
				else {
					(void)DrawUpdateFilesReading(filePanel, UI_FILES_RIGHT);
					filesOtherOpen(NULL, NULL);
				}
			}
			else if(entry != NULL && entry->fileType == IS_SPECIAL) {
				if(fileManagementAllowed() || filesAutoloadFolder(filesOther.dir.name)) {
					filesBoxes = true;
					filesOtherRelease();
					filesToggleAutoload(filesOther.dir.name);
					filesOtherAcquire();
					filesBoxes = false;
				}
			}
			else if(entry != NULL && (buttons & BUTTON_A) && entry->fileType == IS_DIR) {
				(void)DrawUpdateFilesReading(filePanel, UI_FILES_RIGHT);
				filesOtherOpen(entry, "");
			}
			else if(entry != NULL && (buttons & BUTTON_A) && filesLoads(entry)) {
				filesNote = "Games start on the left. Y swaps the two sides.";
			}
			else if(entry != NULL && fileManagementAllowed()) {
				if(filesActions(directory, UI_FILES_RIGHT)) {
					filesOther.listed = false;
					needsRefresh = 1;
					break;
				}
				meta_thread_stop();
				meta_thread_start(loadingBox);
			}
		}
		if(storage >= 0) {
			meta_thread_stop();
			choice = filesStorageMenu(storage, directory, &filePanel);
			if(choice >= 0 && storage == UI_FILES_LEFT) {
				if(choice == filesMenu.devices) {
					/* Other devices...: Swiss's Source picker, from
					 * menu_loop, as X at the top always opened it. */
					filesOtherRelease();
					needsDeviceChange = 1;
					break;
				}
				if(filesMenuDevices[choice] != devices[DEVICE_CUR]) {
					filesSourceChange(filesMenuDevices[choice]);
					break;
				}
			}
			else if(choice >= 0) {
				(void)DrawUpdateFilesReading(filePanel, UI_FILES_RIGHT);
				if(choice == filesMenu.devices) {
					filesOtherPick();
				}
				else if(filesMenuDevices[choice] != filesOther.device ||
						filesOther.mount == UI_FILES_FAILED) {
					filesOtherChoose(filesMenuDevices[choice]);
				}
			}
			meta_thread_start(loadingBox);
		}
		/* Y: the sides swap, the Source with them. */
		if((buttons & PAD_BUTTON_Y) && storage < 0) {
			meta_thread_stop();
			if(filesSwapSides(directory)) {
				break;
			}
			meta_thread_start(loadingBox);
		}
		if((buttons & BUTTON_START) && swissSettings.recentListLevel > 0) {
			/* Recent may change the Source and saves the list. */
			filesOtherRelease();
			(void)filesRecent(false);
			break;
		}
		if(buttons & BUTTON_B) {
			/* The page goes and the Home cube comes back before Home does. */
			filesSnapshot.leaving = 1;
			(void)DrawUpdateFiles(filePanel, &filesSnapshot);
			filesWait(UIFiles_LeaveSeconds(UIMotion_ModeFromFlags(
				swissSettings.disableUIAnimations, swissSettings.reduceUIAnimations)));
			filesSnapshot.leaving = 0;
			filesOtherRelease();
			/* No Swiss row to dim on the way out. */
			filesHome();
			break;
		}
		/* A box (Z, Autoload, a storage menu) may have left presses of its
		 * own. */
		if(buttons & (BUTTON_A | PAD_BUTTON_X | PAD_BUTTON_Y | BUTTON_Z | BUTTON_L | BUTTON_R)) {
			(void)padsButtonsTaken(waitButtons);
		}
		(void)filesPublish(directory, &filePanel, true);
	}
	meta_thread_stop();
	DrawDispose(loadingBox);
	/* Leaving the screen, or its Source: the right listing goes. */
	if(curMenuLocation != ON_FILLIST || needsDeviceChange) {
		filesOtherRelease();
	}
	return filePanel;
}

static u32 gameflowSnapshotGeneration;
static bool gameflowStartupSurfacePending = true;
static bool gameflowStartupHomeReleasePending;

typedef struct {
	uiDrawObj_t *event;
	file_handle rootFolder;
	file_handle *children;
	int childCount;
	file_handle *primary;
	file_handle *oppositeDisc;
	u32 generation;
	u32 focusIndex;
	int knownCheatCount;
	bool cheatsScanned;
	/* Y in the Library: Detail opens this game's settings first, once. */
	bool openSettings;
	char gameId[UI_GAMEFLOW_DETAIL_ID_LENGTH + 1u];
	/* Detail reads the save copies once while it is open: their totals, the
	 * slot the game reads its save from (-1: no card), and the copy Left and
	 * Right chose (-1: none). Where the copies are: gameflowSaveCopies. */
	bool savesScanned;
	uiSavesGameStats_t saveStats;
	int saveSlot;
	int saveChoice;
	/* A memory card is still changing to this game's card (card_slots.c):
	 * its saves are read when it is done, and no copy is chosen till then.
	 * One given up on at the last read: its saves are missing. */
	bool savesWaiting;
	bool savesCardFailed;
	/* The game's manual (gui/manual.c): its pages, 0 for none, -1 for one
	 * that can't be read. */
	int32_t manualPages;
} gameflowLaunchContext_t;

/* The copies of the open Detail's save, one Detail at a time. */
static savesCopies_t gameflowSaveCopies;

static void load_file_with_context(gameflowLaunchContext_t *context);
static void load_game_with_context(gameflowLaunchContext_t *context);
static void gameflowWaitForCardIds(void);
static void gameflowWriteCardTrace(void);
static int gameflow_info_game(ConfigEntry *config,
	gameflowLaunchContext_t *context);
static bool gameflowReadResolverHeader(file_handle *file,
	uiGameflowResolverEntry_t *entry);

static uiGameflowLibraryEntryType_t gameflowEntryType(const file_handle *file)
{
	if(file == NULL) {
		return UI_GAMEFLOW_LIBRARY_ENTRY_OTHER;
	}
	switch(file->fileType) {
		case IS_SPECIAL:
			return UI_GAMEFLOW_LIBRARY_ENTRY_SPECIAL;
		case IS_FILE:
			return UI_GAMEFLOW_LIBRARY_ENTRY_FILE;
		case IS_DIR:
			return UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY;
		default:
			return UI_GAMEFLOW_LIBRARY_ENTRY_OTHER;
	}
}

static uiGameflowLibraryMode_t gameflowLibraryMode(file_handle **directory,
	int numFiles)
{
	char gamesRoot[PATHNAME_MAX];
	uiGameflowLibraryClassifier_t classifier;
	uiGameflowLibraryLocation_t location;
	bool flattened;
	int i;

	if(homeFileBrowser || directory == NULL || numFiles <= 0 ||
		devices[DEVICE_CUR] == NULL || devices[DEVICE_CUR]->initial == NULL ||
		!(devices[DEVICE_CUR]->features & FEAT_BOOT_GCM)) {
		return UI_GAMEFLOW_LIBRARY_NONE;
	}
	gameflowGamesRoot(devices[DEVICE_CUR], gamesRoot);
	location = swissSettings.libraryFolders ?
		UIGameflowLibrary_LocateFolders(gamesRoot, curDir.name) :
		UIGameflowLibrary_Locate(gamesRoot, curDir.name);
	if(location == UI_GAMEFLOW_LIBRARY_LOCATION_NONE) {
		return UI_GAMEFLOW_LIBRARY_NONE;
	}
	UIGameflowLibrary_ClassifierInit(&classifier, location);
	/* scanFiles lists what a flattened directory's folders hold, so the only
	 * folders left in it are empty ones: never a game. */
	flattened = !fnmatch(swissSettings.flattenDir, curDir.name,
		FNM_PATHNAME | FNM_CASEFOLD);

	for(i = 0; i < numFiles; ++i) {
		const char *name = directory[i] ?
			getRelativeName(directory[i]->name) : NULL;
		uiGameflowLibraryEntryType_t type = gameflowEntryType(directory[i]);
		if(flattened && type == UI_GAMEFLOW_LIBRARY_ENTRY_DIRECTORY) {
			continue;
		}
		if(!UIGameflowLibrary_ClassifierAdd(&classifier, type, name)) {
			return UI_GAMEFLOW_LIBRARY_NONE;
		}
	}
	return UIGameflowLibrary_ClassifierFinish(&classifier);
}

/* The Library's entries: a Library location's games, after "..", move to the
 * front of the scanned list in their order, and the Library shows only them,
 * skipping a stray file or an empty folder instead of giving the whole
 * folder to Swiss's list. Returns how many to show; curSelection keeps its
 * entry, so an index means the same game in both lists. Anywhere else the
 * list is left as it is. */
static int gameflowLibraryEntries(file_handle **directory, int numFiles)
{
	uiGameflowLibraryMode_t mode = gameflowLibraryMode(directory, numFiles);
	file_handle *selected;
	int count = 0;
	int i;

	if(mode == UI_GAMEFLOW_LIBRARY_NONE) {
		return numFiles;
	}
	selected = curSelection >= 0 && curSelection < numFiles ?
		directory[curSelection] : NULL;
	for(i = 0; i < numFiles; ++i) {
		file_handle *entry = directory[i];

		if(entry == NULL || !UIGameflowLibrary_EntryEligible(mode,
			(uint32_t)count, gameflowEntryType(entry),
			getRelativeName(entry->name))) {
			continue;
		}
		memmove(&directory[count + 1], &directory[count],
			(size_t)(i - count) * sizeof(*directory));
		directory[count++] = entry;
	}
	curSelection = 0;
	for(i = 0; i < count; ++i) {
		if(directory[i] == selected) {
			curSelection = i;
		}
	}
	return count;
}

/* How the Library treats one entry: in a Library Folders location, an
 * image, a game folder, or a folder of games (NONE: A opens it). */
static uiGameflowLibraryMode_t gameflowEntryMode(uiGameflowLibraryMode_t mode,
	file_handle *file)
{
	return UIGameflowLibrary_EntryMode(mode, gameflowEntryType(file),
		file != NULL ? getRelativeName(file->name) : NULL);
}

/* Library Folders: curDir is a folder below /games, where B goes up a
 * folder and the heading names it. */
static bool gameflowInsideFolder(void)
{
	char gamesRoot[PATHNAME_MAX];

	if(!swissSettings.libraryFolders || devices[DEVICE_CUR] == NULL ||
		devices[DEVICE_CUR]->initial == NULL) {
		return false;
	}
	gameflowGamesRoot(devices[DEVICE_CUR], gamesRoot);
	return UIGameflowLibrary_IsInsideFolder(
		UIGameflowLibrary_LocateFolders(gamesRoot, curDir.name));
}

#define LIBRARY_PATH_MODAL_BUTTONS (BUTTON_A | BUTTON_B | BUTTON_X | \
	BUTTON_Y | BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)

static bool gameflowPathPrefixMatches(const char *entryPath,
	const char *targetPath, bool *exactMatch)
{
	size_t entryLen;
	size_t targetLen;

	if(entryPath == NULL || targetPath == NULL) {
		return false;
	}
	entryLen = strlen(entryPath);
	while(entryLen > 0u && entryPath[entryLen - 1u] == '/') {
		entryLen--;
	}
	targetLen = strlen(targetPath);
	while(targetLen > 0u && targetPath[targetLen - 1u] == '/') {
		targetLen--;
	}
	if(entryLen == 0u || targetLen < entryLen ||
		strncasecmp(entryPath, targetPath, entryLen) != 0) {
		return false;
	}
	if(targetLen == entryLen) {
		*exactMatch = true;
		return true;
	}
	if(targetPath[entryLen] == '/') {
		*exactMatch = false;
		return true;
	}
	return false;
}

static bool gameflowResolveDirOnDevice(DEVICEHANDLER_INTERFACE *device,
	const char *targetPath, file_handle *outDir)
{
	file_handle current;
	bool exact = false;
	int depth;

	if(device == NULL || device->initial == NULL ||
		device->readDir == NULL || targetPath == NULL ||
		targetPath[0] == '\0' || outDir == NULL) {
		return false;
	}
	if(!gameflowPathPrefixMatches(device->initial->name, targetPath,
		&exact)) {
		return false;
	}
	memcpy(&current, device->initial, sizeof(file_handle));
	if(exact) {
		memcpy(outDir, &current, sizeof(file_handle));
		return true;
	}
	for(depth = 0; depth < 8; ++depth) {
		file_handle *entries = NULL;
		int count = device->readDir(&current, &entries, -1);
		bool stepped = false;
		int i;

		for(i = 0; entries != NULL && i < count; ++i) {
			if(entries[i].fileType != IS_DIR) {
				continue;
			}
			if(gameflowPathPrefixMatches(entries[i].name, targetPath,
				&exact)) {
				memcpy(&current, &entries[i], sizeof(file_handle));
				stepped = true;
				break;
			}
		}
		for(i = 0; entries != NULL && i < count; ++i) {
			if(device->closeFile != NULL) {
				device->closeFile(&entries[i]);
			}
		}
		free(entries);
		if(!stepped) {
			return false;
		}
		if(exact) {
			memcpy(outDir, &current, sizeof(file_handle));
			return true;
		}
	}
	return false;
}

static void homeSourceModalDrainButtons(uiMenuInputState_t *menuInput,
	u32 *lastRetrace)
{
	while((padsButtonsHeld() & LIBRARY_PATH_MODAL_BUTTONS) != 0u) {
		(void)padsMenuInputPoll(menuInput,
			menuInputElapsedMicroseconds(lastRetrace),
			UI_MENU_INPUT_AXIS_BOTH | UI_MENU_INPUT_REPEAT, true);
		VIDEO_WaitVSync();
	}
}

static u32 homeSourceModalWaitInput(uiMenuInputState_t *menuInput,
	u32 *lastRetrace)
{
	while(1) {
		u32 held = padsButtonsHeld() & LIBRARY_PATH_MODAL_BUTTONS;
		uiMenuInputDirection_t analog = padsMenuInputPoll(menuInput,
			menuInputElapsedMicroseconds(lastRetrace),
			UI_MENU_INPUT_AXIS_BOTH | UI_MENU_INPUT_REPEAT,
			held != 0u);

		if(held != 0u) {
			return held;
		}
		if(analog == UI_MENU_INPUT_UP) return BUTTON_UP;
		if(analog == UI_MENU_INPUT_DOWN) return BUTTON_DOWN;
		if(analog == UI_MENU_INPUT_LEFT) return BUTTON_LEFT;
		if(analog == UI_MENU_INPUT_RIGHT) return BUTTON_RIGHT;
		VIDEO_WaitVSync();
	}
}

static int homeFolderEntryOrder(const void *left, const void *right)
{
	const file_handle *a = *(file_handle *const *)left;
	const file_handle *b = *(file_handle *const *)right;

	return strcasecmp(getRelativeName((char *)a->name),
		getRelativeName((char *)b->name));
}

static void homeCloseDirListing(DEVICEHANDLER_INTERFACE *device,
	file_handle **rawEntries, int *rawCount, file_handle ***subDirs,
	int *subCount)
{
	int i;

	if(*rawEntries != NULL) {
		for(i = 0; i < *rawCount; ++i) {
			if(device != NULL && device->closeFile != NULL) {
				device->closeFile(&(*rawEntries)[i]);
			}
		}
		free(*rawEntries);
		*rawEntries = NULL;
	}
	*rawCount = 0;
	free(*subDirs);
	*subDirs = NULL;
	*subCount = 0;
}

/* Settings › Library Path's chooser: a folder on any storage connected,
 * or Auto. The device it browses is mounted only while it is open. */
static bool homeChooseLibraryFolder(char *outPath, size_t outSize)
{
	DEVICEHANDLER_INTERFACE *activeDevice = NULL;
	DEVICEHANDLER_INTERFACE *candidates[MAX_DEVICES];
	file_handle openDir;
	file_handle selectedDir;
	file_handle *rawEntries = NULL;
	file_handle **subDirs = NULL;
	uiSavesPageSnapshot_t shown;
	uiDrawObj_t *page = NULL;
	uiMenuInputState_t menuInput;
	u32 lastRetrace = VIDEO_GetRetraceCount();
	u32 listGen = 1u;
	int rawCount = 0;
	int subCount = 0;
	int candidateCount = 0;
	int selection = 0;
	int deviceSelection = 0;
	bool choosingDevice = false;
	bool needReload = true;
	bool chosen = false;
	bool cleared = false;
	int i;

	memset(&openDir, 0, sizeof(openDir));
	memset(&selectedDir, 0, sizeof(selectedDir));
	UIMenuInput_Init(&menuInput);

	for(i = 0; i < MAX_DEVICES; ++i) {
		DEVICEHANDLER_INTERFACE *dev = allDevices[i];

		if(dev == NULL || dev == &__device_dvd ||
			!(dev->features & FEAT_BOOT_GCM) ||
			dev->initial == NULL || dev->init == NULL ||
			dev->deinit == NULL || dev->readDir == NULL) {
			continue;
		}
		if(!deviceHandler_getDeviceAvailable(dev) ||
			filesClash(dev, devices[DEVICE_CUR], NULL, 0)) {
			continue;
		}
		candidates[candidateCount++] = dev;
	}

	if(outPath[0] != '\0') {
		activeDevice = homeSourceFromPathIncludingUnavailable(outPath);
		if(activeDevice != NULL && activeDevice != devices[DEVICE_CUR]) {
			activeDevice->deinit(activeDevice->initial);
			deviceHandler_setStatEnabled(0);
			if(activeDevice->init(activeDevice->initial) != 0) {
				activeDevice = NULL;
			}
			deviceHandler_setStatEnabled(1);
		}
		if(activeDevice != NULL &&
			!gameflowResolveDirOnDevice(activeDevice, outPath, &openDir)) {
			memcpy(&openDir, activeDevice->initial, sizeof(file_handle));
		}
	}
	if(activeDevice == NULL && devices[DEVICE_CUR] != NULL &&
		devices[DEVICE_CUR]->initial != NULL &&
		devices[DEVICE_CUR] != &__device_dvd &&
		(devices[DEVICE_CUR]->features & FEAT_BOOT_GCM)) {
		char gamesRoot[PATHNAME_MAX];

		activeDevice = devices[DEVICE_CUR];
		gameflowGamesRoot(activeDevice, gamesRoot);
		if(!gameflowResolveDirOnDevice(activeDevice, gamesRoot, &openDir)) {
			memcpy(&openDir, activeDevice->initial, sizeof(file_handle));
		}
	}
	if(activeDevice == NULL) {
		choosingDevice = true;
	}

	homeSourceModalDrainButtons(&menuInput, &lastRetrace);
	while(1) {
		u32 pressed;

		if(choosingDevice) {
			int totalDevices = candidateCount + 1;
			int first;

			if(totalDevices <= 0) {
				break;
			}
			if(deviceSelection >= totalDevices) {
				deviceSelection = totalDevices - 1;
			}
			first = UICheats_WindowStart(deviceSelection, totalDevices);
			memset(&shown, 0, sizeof(shown));
			snprintf(shown.title, sizeof(shown.title), "Choose a Device");
			snprintf(shown.status, sizeof(shown.status), "Storage Devices");
			snprintf(shown.section, sizeof(shown.section),
				"Select a storage device to browse its folders");
			snprintf(shown.position, sizeof(shown.position), "%d / %d",
				deviceSelection + 1, totalDevices);
			for(i = 0; i < UI_SAVES_PAGE_ROWS &&
				first + i < totalDevices; ++i) {
				int idx = first + i;
				uiSavesPageRow_t *row = &shown.rows[i];

				if(idx < candidateCount) {
					UICheats_Fit(row->title, sizeof(row->title),
						DeviceDisplayName(candidates[idx]), 330, 0.62f,
						GetTextSizeInPixels);
					snprintf(row->blocks, sizeof(row->blocks), "%s",
						candidates[idx]->initial->name);
				}
				else {
					snprintf(row->title, sizeof(row->title),
						"Auto (Find a Games Folder)");
					snprintf(row->blocks, sizeof(row->blocks), "Clear");
				}
				shown.rowCount++;
			}
			shown.first = first;
			shown.count = totalDevices;
			shown.list = listGen;
			shown.focusRow = (s8)(deviceSelection - first);
			snprintf(shown.hint[0], sizeof(shown.hint[0]),
				"A  Select Device");
			snprintf(shown.hint[1], sizeof(shown.hint[1]), "B  Back");
			if(page == NULL) {
				if((page = DrawSavesPage(&shown)) != NULL) {
					DrawPublish(page);
				}
			}
			else {
				DrawUpdateSavesPage(page, &shown);
			}
			pressed = homeSourceModalWaitInput(&menuInput, &lastRetrace);
			if(pressed & BUTTON_B) {
				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				if(activeDevice != NULL) {
					choosingDevice = false;
					listGen++;
					continue;
				}
				break;
			}
			if(pressed & (BUTTON_UP | BUTTON_LEFT)) {
				deviceSelection = UICheats_Move(deviceSelection,
					totalDevices, -1, (pressed & BUTTON_LEFT) != 0u);
			}
			else if(pressed & (BUTTON_DOWN | BUTTON_RIGHT)) {
				deviceSelection = UICheats_Move(deviceSelection,
					totalDevices, 1, (pressed & BUTTON_RIGHT) != 0u);
			}
			else if(pressed & BUTTON_A) {
				DEVICEHANDLER_INTERFACE *nextDev;
				char gamesRoot[PATHNAME_MAX];

				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				if(deviceSelection >= candidateCount) {
					cleared = true;
					chosen = true;
					break;
				}
				nextDev = candidates[deviceSelection];
				if(nextDev != activeDevice) {
					if(nextDev != devices[DEVICE_CUR]) {
						nextDev->deinit(nextDev->initial);
						deviceHandler_setStatEnabled(0);
						if(nextDev->init(nextDev->initial) != 0) {
							uiDrawObj_t *box;

							deviceHandler_setStatEnabled(1);
							box = DrawPublish(DrawMessageBox(D_WARN,
								"This device could not be read.\nA  OK"));
							wait_press_A();
							DrawDispose(box);
							homeSourceModalDrainButtons(&menuInput,
								&lastRetrace);
							continue;
						}
						deviceHandler_setStatEnabled(1);
						deviceHandler_setDeviceAvailable(nextDev, true);
					}
					homeCloseDirListing(activeDevice, &rawEntries,
						&rawCount, &subDirs, &subCount);
					if(activeDevice != NULL &&
						activeDevice != devices[DEVICE_CUR]) {
						activeDevice->deinit(activeDevice->initial);
					}
					activeDevice = nextDev;
				}
				concat_path(gamesRoot, activeDevice->initial->name, "games");
				if(!gameflowResolveDirOnDevice(activeDevice, gamesRoot,
					&openDir)) {
					memcpy(&openDir, activeDevice->initial,
						sizeof(file_handle));
				}
				choosingDevice = false;
				needReload = true;
				selection = 0;
				listGen++;
			}
			homeSourceModalDrainButtons(&menuInput, &lastRetrace);
			continue;
		}

		if(needReload) {
			homeCloseDirListing(activeDevice, &rawEntries, &rawCount,
				&subDirs, &subCount);
			rawCount = activeDevice->readDir(&openDir, &rawEntries, -1);
			if(rawCount > 0 && rawEntries != NULL) {
				subDirs = calloc((size_t)rawCount, sizeof(*subDirs));
				for(i = 0; subDirs != NULL && i < rawCount; ++i) {
					const char *leaf = getRelativeName(rawEntries[i].name);

					if(rawEntries[i].fileType == IS_DIR && leaf != NULL &&
						leaf[0] != '.' && strcmp(leaf, "..") != 0) {
						subDirs[subCount++] = &rawEntries[i];
					}
				}
				if(subCount > 1) {
					qsort(subDirs, (size_t)subCount, sizeof(subDirs[0]),
						homeFolderEntryOrder);
				}
			}
			else {
				rawCount = 0;
			}
			needReload = false;
		}

		{
			bool hasParent = strcmp(openDir.name,
				activeDevice->initial->name) != 0;
			int parentRow = hasParent ? 1 : -1;
			int firstSubRow = hasParent ? 2 : 1;
			int switchDeviceRow = firstSubRow + subCount;
			int clearRow = switchDeviceRow + 1;
			int totalRows = clearRow + 1;
			int first;

			if(selection >= totalRows) {
				selection = totalRows - 1;
			}
			first = UICheats_WindowStart(selection, totalRows);
			memset(&shown, 0, sizeof(shown));
			snprintf(shown.title, sizeof(shown.title), "Choose a Folder");
			snprintf(shown.status, sizeof(shown.status), "%s",
				DeviceDisplayName(activeDevice));
			UICheats_Fit(shown.section, sizeof(shown.section),
				openDir.name, 420, 0.42f, GetTextSizeInPixels);
			snprintf(shown.position, sizeof(shown.position), "%d / %d",
				selection + 1, totalRows);
			for(i = 0; i < UI_SAVES_PAGE_ROWS &&
				first + i < totalRows; ++i) {
				int idx = first + i;
				uiSavesPageRow_t *row = &shown.rows[i];
				char label[PATHNAME_MAX + 16];

				if(idx == 0) {
					snprintf(label, sizeof(label), "Use %s", openDir.name);
					UICheats_Fit(row->title, sizeof(row->title), label,
						330, 0.62f, GetTextSizeInPixels);
					snprintf(row->blocks, sizeof(row->blocks), "Current");
				}
				else if(idx == parentRow) {
					char parent[PATHNAME_MAX];

					getParentPath(openDir.name, parent);
					snprintf(label, sizeof(label), "Up to %s", parent);
					UICheats_Fit(row->title, sizeof(row->title), label,
						330, 0.62f, GetTextSizeInPixels);
					snprintf(row->blocks, sizeof(row->blocks), "Parent");
				}
				else if(idx >= firstSubRow && idx < firstSubRow + subCount) {
					file_handle *sub = subDirs[idx - firstSubRow];

					UICheats_Fit(row->title, sizeof(row->title),
						getRelativeName(sub->name), 330, 0.62f,
						GetTextSizeInPixels);
					snprintf(row->blocks, sizeof(row->blocks), "Folder");
				}
				else if(idx == switchDeviceRow) {
					snprintf(row->title, sizeof(row->title),
						"Switch Device...");
					snprintf(row->blocks, sizeof(row->blocks), "Device");
				}
				else if(idx == clearRow) {
					snprintf(row->title, sizeof(row->title),
						"Auto (Find a Games Folder)");
					snprintf(row->blocks, sizeof(row->blocks), "Clear");
				}
				shown.rowCount++;
			}
			shown.first = first;
			shown.count = totalRows;
			shown.list = listGen;
			shown.focusRow = (s8)(selection - first);
			snprintf(shown.hint[0], sizeof(shown.hint[0]),
				"A  Open / Select   X  Use Folder   Y  Device");
			snprintf(shown.hint[1], sizeof(shown.hint[1]), "B  Back");
			if(page == NULL) {
				if((page = DrawSavesPage(&shown)) != NULL) {
					DrawPublish(page);
				}
			}
			else {
				DrawUpdateSavesPage(page, &shown);
			}
			pressed = homeSourceModalWaitInput(&menuInput, &lastRetrace);
			if(pressed & BUTTON_B) {
				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				break;
			}
			if(pressed & BUTTON_Y) {
				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				choosingDevice = true;
				deviceSelection = 0;
				for(i = 0; i < candidateCount; ++i) {
					if(candidates[i] == activeDevice) {
						deviceSelection = i;
						break;
					}
				}
				listGen++;
				continue;
			}
			if(pressed & BUTTON_X) {
				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				if(selection >= firstSubRow &&
					selection < firstSubRow + subCount) {
					memcpy(&selectedDir, subDirs[selection - firstSubRow],
						sizeof(file_handle));
				}
				else {
					memcpy(&selectedDir, &openDir, sizeof(file_handle));
				}
				chosen = true;
				break;
			}
			if(pressed & (BUTTON_UP | BUTTON_LEFT)) {
				selection = UICheats_Move(selection, totalRows, -1,
					(pressed & BUTTON_LEFT) != 0u);
			}
			else if(pressed & (BUTTON_DOWN | BUTTON_RIGHT)) {
				selection = UICheats_Move(selection, totalRows, 1,
					(pressed & BUTTON_RIGHT) != 0u);
			}
			else if(pressed & BUTTON_A) {
				homeSourceModalDrainButtons(&menuInput, &lastRetrace);
				if(selection == 0) {
					memcpy(&selectedDir, &openDir, sizeof(file_handle));
					chosen = true;
					break;
				}
				else if(selection == parentRow) {
					char parent[PATHNAME_MAX];

					if(!getParentPath(openDir.name, parent) &&
						gameflowResolveDirOnDevice(activeDevice, parent,
							&openDir)) {
						needReload = true;
						selection = 0;
						listGen++;
					}
				}
				else if(selection >= firstSubRow &&
					selection < firstSubRow + subCount) {
					memcpy(&openDir, subDirs[selection - firstSubRow],
						sizeof(file_handle));
					needReload = true;
					selection = 0;
					listGen++;
				}
				else if(selection == switchDeviceRow) {
					choosingDevice = true;
					deviceSelection = 0;
					for(i = 0; i < candidateCount; ++i) {
						if(candidates[i] == activeDevice) {
							deviceSelection = i;
							break;
						}
					}
					listGen++;
				}
				else if(selection == clearRow) {
					cleared = true;
					chosen = true;
					break;
				}
			}
			homeSourceModalDrainButtons(&menuInput, &lastRetrace);
		}
	}

	homeCloseDirListing(activeDevice, &rawEntries, &rawCount, &subDirs,
		&subCount);
	if(chosen) {
		if(cleared) {
			outPath[0] = '\0';
		}
		else {
			strlcpy(outPath, selectedDir.name, outSize);
		}
	}
	if(activeDevice != NULL && activeDevice != devices[DEVICE_CUR]) {
		activeDevice->deinit(activeDevice->initial);
	}
	if(page != NULL) {
		DrawDispose(page);
	}
	return chosen;
}

bool choose_library_path(char *selection, size_t size)
{
	return homeChooseLibraryFolder(selection, size);
}

/* A storage device the Library can open: one Swiss has seen, that can
 * hold games and list folders, never the disc drive. */
static bool homeLibraryDevice(DEVICEHANDLER_INTERFACE *device)
{
	return device != NULL && device != &__device_dvd &&
		(device->features & FEAT_BOOT_GCM) && device->initial != NULL &&
		device->init != NULL && device->deinit != NULL &&
		device->readDir != NULL && deviceHandler_getDeviceAvailable(device);
}

/* The Library's folder on a mounted device, into dir: false without one. */
static bool homeLibraryOn(DEVICEHANDLER_INTERFACE *device, file_handle *dir)
{
	char gamesRoot[PATHNAME_MAX];

	gameflowGamesRoot(device, gamesRoot);
	return gameflowResolveDirOnDevice(device, gamesRoot, dir);
}

/* Mounts a device that isn't the Source; false, and nothing held, when it
 * won't. */
static bool homeLibraryMount(DEVICEHANDLER_INTERFACE *device)
{
	bool mounted;

	device->deinit(device->initial);
	deviceHandler_setStatEnabled(0);
	mounted = device->init(device->initial) == 0;
	deviceHandler_setStatEnabled(1);
	return mounted;
}

/* The first device after the Source, in the order Swiss looks for a boot
 * device, with a Library folder: left mounted, with dir its folder. Only
 * storage attached to the console: no network share, WODE or USB Gecko.
 * Each device it looked at without one is let go again. */
static DEVICEHANDLER_INTERFACE *homeFindLibrary(file_handle *dir)
{
	int i;

	for(i = 0; i < MAX_DEVICES; ++i) {
		DEVICEHANDLER_INTERFACE *candidate = allDevices[i];

		if(candidate == devices[DEVICE_CUR] || !homeLibraryDevice(candidate) ||
			candidate == &__device_smb || candidate == &__device_ftp ||
			candidate == &__device_fsp || candidate == &__device_wode ||
			candidate == &__device_usbgecko ||
			(devices[DEVICE_CUR] != NULL &&
				filesClash(candidate, devices[DEVICE_CUR], NULL, 0)) ||
			!homeLibraryMount(candidate)) {
			continue;
		}
		if(homeLibraryOn(candidate, dir)) {
			return candidate;
		}
		candidate->deinit(candidate->initial);
	}
	return NULL;
}

/* A mounted device becomes the Source; the old one's listing and mount go. */
static void homeSwitchSource(DEVICEHANDLER_INTERFACE *device)
{
	DEVICEHANDLER_INTERFACE *previous = devices[DEVICE_CUR];

	filesOtherRelease();
	DrawGameflowCancelPosters();
	freeFiles();
	if(previous != NULL) {
		devices[DEVICE_PREV] = previous;
		previous->deinit(previous->initial);
		homeSourceRecord(previous, UI_HOME_SOURCE_MOUNT_UNMOUNTED);
	}
	devices[DEVICE_CUR] = device;
	deviceHandler_setDeviceAvailable(device, true);
	homeSourceRecord(device, UI_HOME_SOURCE_MOUNT_MOUNTED);
}

/* Home starts on the Library: Settings › Library Path's device when it is
 * connected, else the boot device when it has /games, else the first
 * storage that has. */
static void homeApplyStartupSource(void)
{
	DEVICEHANDLER_INTERFACE *target = swissSettings.libraryPath[0] != '\0' ?
		homeSourceFromPathIncludingUnavailable(swissSettings.libraryPath) : NULL;
	file_handle dir;

	if(target != NULL && target == devices[DEVICE_CUR]) {
		return;
	}
	if(target != NULL && homeLibraryDevice(target) && homeLibraryMount(target)) {
		homeSwitchSource(target);
		needsDeviceChange = 0;
		return;
	}
	/* The boot device stays when it has the Library's folder, or when the
	 * Library never reads it (the disc drive: no spin-up at startup). */
	if(devices[DEVICE_CUR] != NULL && (!homeLibraryDevice(devices[DEVICE_CUR]) ||
		homeLibraryOn(devices[DEVICE_CUR], &dir))) {
		return;
	}
	target = homeFindLibrary(&dir);
	if(target != NULL) {
		homeSwitchSource(target);
		needsDeviceChange = 0;
	}
}

/* Library from Home on a Source without a Library folder: the first other
 * storage with one becomes the Source, opened at it. */
static bool gameflowTryOtherDevicesForLibrary(void)
{
	DEVICEHANDLER_INTERFACE *previous = devices[DEVICE_CUR];
	DEVICEHANDLER_INTERFACE *found;
	file_handle dir;

	if(previous == NULL || previous->initial == NULL || needsRefresh ||
		strcmp(curDir.name, previous->initial->name)) {
		return false;
	}
	found = homeFindLibrary(&dir);
	if(found == NULL) {
		return false;
	}
	homeSwitchSource(found);
	memcpy(&curDir, &dir, sizeof(file_handle));
	curSelection = 0;
	needsRefresh = 1;
	return true;
}

static bool gameflowEnterLibraryFromHome(void)
{
	char gamesRoot[PATHNAME_MAX];
	file_handle **directory;
	int numFiles;
	int i;

	if(devices[DEVICE_CUR] == NULL ||
		devices[DEVICE_CUR]->initial == NULL) {
		return false;
	}
	gameflowGamesRoot(devices[DEVICE_CUR], gamesRoot);
	/* Returning Home must not destroy the retained /games selection. */
	if(UIGameflowLibrary_Locate(gamesRoot, curDir.name) ==
			UI_GAMEFLOW_LIBRARY_LOCATION_ROOT || gameflowInsideFolder()) {
		return true;
	}
	directory = getSortedDirEntries();
	numFiles = getSortedDirEntryCount();
	if(directory == NULL || numFiles <= 0) {
		return false;
	}
	for(i = 0; i < numFiles; ++i) {
		if(directory[i] == NULL ||
			!UIGameflowLibrary_IsGamesRootEntry(gamesRoot,
				gameflowEntryType(directory[i]), directory[i]->name)) {
			continue;
		}
		lockFile(directory[i]);
		memcpy(&curDir, directory[i], sizeof(file_handle));
		unlockFile(directory[i]);
		curSelection = 0;
		needsRefresh = 1;
		return true;
	}
	if(strcmp(curDir.name, devices[DEVICE_CUR]->initial->name) == 0 &&
		gameflowResolveDirOnDevice(devices[DEVICE_CUR], gamesRoot, &curDir)) {
		curSelection = 0;
		needsRefresh = 1;
		return true;
	}
	return gameflowTryOtherDevicesForLibrary();
}

static void homeRefreshLibrary(void)
{
	DEVICEHANDLER_INTERFACE *source = devices[DEVICE_CUR];
	/* Refresh reads the card again, /apps and /emulators included. */
	homeAppsKnown = false;
	homeEmulatorsKnown = false;
	if(source == NULL) {
		needsRefresh = 0;
		return;
	}
	memcpy(&curDir, source->initial, sizeof(file_handle));
	if(source == &__device_wkf) {
		s32 ret;
		freeFiles();
		DrawGameflowCancelPosters();
		wkfReinit();
		source->deinit(source->initial);
		homeSourceRecord(source, UI_HOME_SOURCE_MOUNT_UNMOUNTED);
		ret = source->init(source->initial);
		if(ret) {
			if(ret == ENODEV) {
				deviceHandler_setDeviceAvailable(source, false);
			}
			/* Preserve the failed handler as the selector's starting identity.
			 * It is not deinitialized a second time. */
			devices[DEVICE_PREV] = source;
			devices[DEVICE_CUR] = NULL;
			homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
			needsRefresh = 0;
			needsDeviceChange = 1;
			return;
		}
		deviceHandler_setDeviceAvailable(source, true);
		homeSourceRecord(source, UI_HOME_SOURCE_MOUNT_MOUNTED);
	}
	needsRefresh = 1;
}

static void homeRunFlippyUpdate(void)
{
	DEVICEHANDLER_INTERFACE *source = devices[DEVICE_CUR];
	uiDrawObj_t *progBar;
	flippybootstatus *status;

	homeFlippyUpdatePending = false;
	if(source != &__device_flippy && source != &__device_flippyflash) {
		return;
	}
	/* The file browser has returned and released its selected entry. Retire
	 * every cached handle before resetting the backing FAT session. */
	freeFiles();
	DrawGameflowCancelPosters();
	progBar = DrawPublish(DrawProgressBar(true, 0, "Resetting RP2040"));
	flippy_closefrom(1);
	flippy_reset();
	homeSourceRecord(source, UI_HOME_SOURCE_MOUNT_UNMOUNTED);
	needsDeviceChange = 1;
	needsRefresh = 0;
	refreshDeviceCode(false);
	flippy_boot(FLIPPY_MODE_UPDATE);
	while((status = flippy_getbootstatus()) &&
			status->current_progress != 0xFFFF) {
		sprintf(txtbuffer, "%.64s\n%.64s", status->text, status->subtext);
		progBar = DrawRepublish(progBar, DrawProgressBar(
			!status->show_progress_bar,
			(int)(((float)status->current_progress / (float)1000) * 100),
			txtbuffer));
	}
	DrawDispose(progBar);
	deviceHandler_setDeviceAvailable(&__device_flippy,
		deviceHandler_Flippy_test());
	deviceHandler_setDeviceAvailable(&__device_flippyflash,
		deviceHandler_FlippyFlash_test());
	/* The updater reset invalidated the mounted session even when the device
	 * probes available. Selection must explicitly remount it. */
	needsDeviceChange = 1;
	needsRefresh = 0;
}

static void homeRestartSwiss(void) __attribute__((noreturn));
static void homeRestartSwiss(void)
{
	if(devices[DEVICE_CUR] != NULL) {
		DrawGameflowCancelPosters();
		devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);
	}
	if(swissSettings.hasFlippyDrive) {
		flippy_bypass(false);
		flippy_reset();
	}
	menuaudio_shutdown();
	DrawShutdown();
	SYS_ResetSystem(SYS_HOTRESET, 0, !swissSettings.hasFlippyDrive);
	__builtin_unreachable();
}

#define HOME_CONFIRMATION_BUTTONS (BUTTON_A | BUTTON_B | BUTTON_RIGHT | \
	BUTTON_LEFT | BUTTON_UP | BUTTON_DOWN | BUTTON_START)
#define SELECTOR_RELEASE_BUTTONS (HOME_CONFIRMATION_BUTTONS | BUTTON_X | \
	BUTTON_Y | BUTTON_L | BUTTON_R | BUTTON_Z)

u32 menuInputElapsedMicroseconds(u32 *lastRetrace)
{
	u32 currentRetrace;
	u32 elapsedRetraces;
	float retraceRate;
	float elapsed;

	if(lastRetrace == NULL) {
		return 0u;
	}
	currentRetrace = VIDEO_GetRetraceCount();
	elapsedRetraces = currentRetrace - *lastRetrace;
	*lastRetrace = currentRetrace;
	if(elapsedRetraces == 0u) {
		return 0u;
	}
	retraceRate = VIDEO_GetRetraceRate();
	if(!isfinite(retraceRate) || retraceRate < 1.0f) {
		retraceRate = 60.0f;
	}
	elapsed = (float)elapsedRetraces * 1000000.0f / retraceRate;
	/* The mapper intentionally consumes no more than 50 ms per poll. Clamp
	 * before lrintf so a long menu pause cannot overflow target 32-bit long. */
	if(elapsed >= (float)UI_MENU_INPUT_MAX_ELAPSED_US) {
		return UI_MENU_INPUT_MAX_ELAPSED_US;
	}
	return elapsed <= 0.0f ? 0u : (u32)lrintf(elapsed);
}

static bool homeDrainRestartInput(bool cancelRequested)
{
	u32 held;
	while(UIHomeSafety_RestartReleasePending(
			(held = padsButtonsHeld()), HOME_CONFIRMATION_BUTTONS)) {
		cancelRequested = UIHomeSafety_AccumulateRestartCancel(
			cancelRequested, held, BUTTON_B);
		VIDEO_WaitVSync();
	}
	return cancelRequested;
}

static void homeDrainSelectorInput(void)
{
	while((padsButtonsHeld() & SELECTOR_RELEASE_BUTTONS) != 0u) {
		VIDEO_WaitVSync();
	}
	/* Per-channel neutral arming replaces the old eight-frame aggregate-axis
	 * quarantine. A held or drifting secondary pad cannot inherit ownership. */
	UIMenuInput_Init(&homeMenuInput);
	homeMenuInputRetrace = VIDEO_GetRetraceCount();
}

static void homeConfirmRestartEffect(void)
{
	/* The reducer-owned Cancel/Restart modal remains published while the
	 * activating sample drains. Any B observed before that sample fully
	 * releases vetoes reset and returns one level to System. */
	if(homeDrainRestartInput(false)) {
		(void)UIHome_Apply(&homeState, UI_HOME_INPUT_BACK,
			homeCapabilities());
		homePublish(true);
		return;
	}
	homeRestartSwiss();
}

/* The bundled web browser DOL, under Indigo's own folder on the config device,
 * where sd_package.sh puts it. */
#define WEB_BROWSER_DIR "swiss/indigo"
#define WEB_BROWSER_DOL "cubebrowser.dol"

/* The bundled browser isn't a program someone chose: boot_dol keeps it out
 * of the Recent list, whose top entry Indigo would open at the next start. */
static bool bootDolSkipRecent;

/* Launch the bundled web browser the way the Apps face launches a program:
 * find its DOL on the config device and hand it to boot_dol, which shuts the
 * menu down and jumps. boot_dol returns only when the DOL won't start.
 * The config device is mounted only while something reads it, unless it is
 * the Source too (config_set_device): mount it to look, and keep it through
 * boot_dol, which reads the DOL from it. */
static void launchWebBrowser(void)
{
	DEVICEHANDLER_INTERFACE *device;
	file_handle dir;
	file_handle *entries = NULL;
	int count = 0, found = -1;
	bool mounted;
	uiDrawObj_t *box;

	mounted = config_set_device();
	device = devices[DEVICE_CONFIG];
	if(mounted && device->initial != NULL && device->readDir != NULL) {
		memset(&dir, 0, sizeof(dir));
		concat_path(dir.name, device->initial->name, WEB_BROWSER_DIR);
		dir.device = device;
		dir.fileType = IS_DIR;
		count = device->readDir(&dir, &entries, -1);
		for(int i = 0; i < count; i++) {
			if(entries[i].fileType != IS_DIR && strcasecmp(
				getRelativeName(entries[i].name), WEB_BROWSER_DOL) == 0) {
				found = i;
				break;
			}
		}
	}
	if(found < 0) {
		free(entries);
		if(mounted) {
			config_unset_device();
		}
		box = DrawPublish(DrawMessageBox(D_FAIL,
			"The web browser isn't installed. Press A."));
		wait_press_A();
		DrawDispose(box);
		return;
	}
	memcpy(&curFile, &entries[found], sizeof(file_handle));
	curFile.offset = 0;
	curFile.fp = NULL;
	curFile.ffsFp = NULL;
	curFile.meta = NULL;
	curFile.uiObj = NULL;
	curFile.lockCount = 0;
	curFile.thread = LWP_THREAD_NULL;
	free(entries);
	/* With the console lab on, lab=1 starts the browser's own, on the same
	 * port with the same requests: a test run goes on watching and driving,
	 * and its /reset comes back to Indigo.
	 * TODO: pass the current theme as argv once the browser reads it. */
	char *labArgs[] = {NULL, "lab=1"};
	bootDolSkipRecent = true;
	boot_dol(&curFile, swissSettings.labServer ? 2 : 0, labArgs);
	bootDolSkipRecent = false;
	/* It didn't start: close the DOL before its device goes. */
	device->closeFile(&curFile);
	config_unset_device();
	box = DrawPublish(DrawMessageBox(D_WARN, "The web browser couldn't start"));
	sleep(2);
	DrawDispose(box);
}

static void homeDispatchEffect(uiHomeEffect_t effect)
{
	if(effect != UI_HOME_EFFECT_NONE) {
		/* No analog owner or repeat deadline survives a blocking child surface. */
		UIMenuInput_Init(&homeMenuInput);
		homeMenuInputRetrace = VIDEO_GetRetraceCount();
	}
	switch(effect) {
		case UI_HOME_EFFECT_OPEN_LIBRARY:
			if(gameflowEnterLibraryFromHome()) {
				curMenuLocation = ON_FILLIST;
			}
			else if(devices[DEVICE_CUR] != NULL &&
					devices[DEVICE_CUR]->initial != NULL) {
				/* The current cache may belong to /apps, a Recent parent, or another
				 * directory. Scan the device root, then promote /games if present. */
				memcpy(&curDir, devices[DEVICE_CUR]->initial,
					sizeof(file_handle));
				curSelection = 0;
				needsRefresh = 1;
				homeLibraryEntryPending = true;
				curMenuLocation = ON_FILLIST;
			}
			break;
		case UI_HOME_EFFECT_CHANGE_SOURCE:
			needsDeviceChange = 1;
			break;
		case UI_HOME_EFFECT_REFRESH:
			homeRefreshLibrary();
			break;
		case UI_HOME_EFFECT_OPEN_SETTINGS:
			UIScene_Request(UI_SCENE_SETTINGS);
			needsRefresh = show_settings_view(VIEW_QUICK, 0, NULL);
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_OPEN_INFO:
			UIScene_Request(UI_SCENE_SYSTEM);
			show_info();
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_OPEN_SAVES:
			/* Memory Cards covers the screen and hands the Home cube over
			 * where it stands, its System side to the front, going back into
			 * the distance and coming back; the System scene would turn it to
			 * the Library's side on the way and back again after. */
			show_saves();
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_OPEN_APPS:
			show_apps();
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_OPEN_EMULATORS:
			show_emulators();
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_OPEN_FILES:
			if(devices[DEVICE_CUR] != NULL &&
					devices[DEVICE_CUR]->initial != NULL) {
				homeFileBrowser = true;
				memcpy(&curDir, devices[DEVICE_CUR]->initial,
					sizeof(file_handle));
				curSelection = 0;
				needsRefresh = 1;
				curMenuLocation = ON_FILLIST;
			}
			break;
		case UI_HOME_EFFECT_OPEN_WEB:
			launchWebBrowser();
			UIScene_Request(UI_SCENE_HOME);
			break;
		case UI_HOME_EFFECT_RESTART:
			homeConfirmRestartEffect();
			break;
		case UI_HOME_EFFECT_OPEN_RECENT:
			select_recent_entry();
			break;
		default:
			break;
	}
}

static void gameflowCopyText(char *destination, size_t destinationSize,
	const char *source, size_t sourceLimit)
{
	size_t length;

	if(destination == NULL || destinationSize == 0u) {
		return;
	}
	if(source == NULL) {
		destination[0] = '\0';
		return;
	}
	length = strnlen(source, sourceLimit);
	if(length >= destinationSize) {
		length = destinationSize - 1u;
	}
	memcpy(destination, source, length);
	destination[length] = '\0';
}

static void gameflowTrimImageExtension(char *title)
{
	char *extension;
	if(title == NULL || !UIGameflowLibrary_IsGameImageName(title)) {
		return;
	}
	extension = strrchr(title, '.');
	if(extension != NULL) {
		*extension = '\0';
	}
}

static bool gameflowCopyDiskId(char destination[7], const dvddiskid *diskId)
{
	const unsigned char *source = (const unsigned char*)diskId;
	int i;

	for(i = 0; i < 6; ++i) {
		if(!((source[i] >= 'A' && source[i] <= 'Z') ||
			(source[i] >= '0' && source[i] <= '9'))) {
			destination[0] = '\0';
			return false;
		}
	}
	memcpy(destination, source, 6u);
	destination[6] = '\0';
	return true;
}

/* The Library marks a game whose settings differ from Game Defaults. */
static void gameflowMarkCustom(uiGameflowCardSnapshot_t *record)
{
	char region = strcmp(UIGameflowLibrary_RegionLabel(record->gameId), "PAL") ?
		'E' : 'P';

	if(settings_game_has_custom(record->gameId, region)) {
		record->flags |= UI_GAMEFLOW_CARD_CUSTOM;
	}
}

static void gameflowSnapshotRecord(uiGameflowCardSnapshot_t *record,
	file_handle *file, uiGameflowLibraryMode_t mode)
{
	const char *relativeName = getRelativeName(file->name);
	char sizeText[32] = "";
	uiGameflowResolverEntry_t headerEntry;

	record->flags = UI_GAMEFLOW_CARD_VALID;
	record->size = file->size;
	if(file->fileAttrib & ATTRIB_HIDDEN) {
		record->flags |= UI_GAMEFLOW_CARD_HIDDEN;
	}
	if(file->fileType == IS_SPECIAL) {
		record->flags |= UI_GAMEFLOW_CARD_PARENT;
		gameflowCopyText(record->title, sizeof(record->title),
			"Return to parent", sizeof("Return to parent"));
		gameflowCopyText(record->company, sizeof(record->company),
			"GAME LIBRARY", sizeof("GAME LIBRARY"));
		gameflowCopyText(record->facts, sizeof(record->facts),
			"A  RETURN", sizeof("A  RETURN"));
		return;
	}
	if(mode == UI_GAMEFLOW_LIBRARY_NONE && file->fileType == IS_DIR) {
		/* Library Folders: a folder of games, which A opens. */
		record->subfolder = 1u;
		gameflowCopyText(record->title, sizeof(record->title), relativeName,
			PATHNAME_MAX);
		gameflowCopyText(record->company, sizeof(record->company),
			"FOLDER", sizeof("FOLDER"));
		gameflowCopyText(record->facts, sizeof(record->facts),
			"A  OPEN", sizeof("A  OPEN"));
		return;
	}
	if(mode == UI_GAMEFLOW_LIBRARY_GAME_FOLDERS) {
		record->flags |= UI_GAMEFLOW_CARD_FOLDER;
		UIGameflowLibrary_ParseGameFolderName(relativeName, record->gameId,
			record->title, sizeof(record->title));
		gameflowMarkCustom(record);
		gameflowCopyText(record->company, sizeof(record->company),
			"GAME FOLDER", sizeof("GAME FOLDER"));
		snprintf(record->facts, sizeof(record->facts), "%s  |  A OPEN",
			record->gameId);
		return;
	}

	populate_meta(file);
	if(file->meta != NULL) {
		const char *title = file->meta->displayName;
		const char *company = file->meta->bannerDesc.fullCompany[0] ?
			file->meta->bannerDesc.fullCompany :
			file->meta->bannerDesc.company;
		gameflowCopyText(record->title, sizeof(record->title), title,
			BNR_FULL_TEXT_LEN);
		gameflowCopyText(record->company, sizeof(record->company), company,
			BNR_FULL_TEXT_LEN);
		gameflowCopyDiskId(record->gameId, &file->meta->diskId);
		if(file->meta->banner != NULL &&
			file->meta->bannerSize == BNR_PIXELDATA_LEN &&
			file->meta->bannerSum != 0xFFFF) {
			memcpy(record->banner, file->meta->banner, BNR_PIXELDATA_LEN);
			record->flags |= UI_GAMEFLOW_CARD_HAS_BANNER;
		}
	}
	/* TGC metadata does not publish diskId, and a constrained metadata cache can
	 * also leave a valid image without meta. Read the validated disc header so
	 * every advertised image type carries the exact ID needed by posters and
	 * the retained Detail identity gate. */
	if(!record->gameId[0]) {
		bool headerValid;

		memset(&headerEntry, 0, sizeof(headerEntry));
		headerValid = gameflowReadResolverHeader(file, &headerEntry);
		if(file->device == &__device_flippy ||
			file->device == &__device_flippyflash) {
			file->device->closeFile(file);
		}
		if(headerValid) {
			memcpy(record->gameId, headerEntry.gameId,
				sizeof(headerEntry.gameId));
			record->gameId[sizeof(record->gameId) - 1u] = '\0';
			if(file->meta != NULL) {
				memcpy(file->meta->diskId.gamename, headerEntry.gameId, 4u);
				memcpy(file->meta->diskId.company, &headerEntry.gameId[4], 2u);
				file->meta->diskId.disknum = headerEntry.discNumber;
				file->meta->diskId.gamever = headerEntry.version;
			}
		}
	}
	if(!record->title[0]) {
		gameflowCopyText(record->title, sizeof(record->title), relativeName,
			PATHNAME_MAX);
	}
	gameflowTrimImageExtension(record->title);
	formatBytes(sizeText, file->size, 0,
		file->device == NULL || !(file->device->location & LOC_SYSTEM));
	if(record->gameId[0]) {
		snprintf(record->facts, sizeof(record->facts), "%s  |  %s",
			record->gameId, sizeText);
	}
	else {
		snprintf(record->facts, sizeof(record->facts), "GAME IMAGE  |  %s",
			sizeText);
	}
	if(!strcmp(swissSettings.autoload, file->name) ||
		!fnmatch(swissSettings.autoload, file->name,
			FNM_PATHNAME | FNM_PREFIX_DIRS)) {
		record->flags |= UI_GAMEFLOW_CARD_AUTOLOAD;
	}
	gameflowMarkCustom(record);
}

static void gameflowProtectMetaFile(const file_handle *file)
{
	file_handle **entries = getSortedDirEntries();
	int entryCount = getSortedDirEntryCount();
	int i;

	for(i = 0; file != NULL && entries != NULL && i < entryCount; ++i) {
		if(file == entries[i]) {
			current_view_start = i;
			current_view_end = i;
			return;
		}
	}
}

static bool gameflowBuildSnapshot(uiGameflowRenderSnapshot_t *snapshot,
	file_handle **directory, int numFiles, uiGameflowLibraryMode_t mode,
	uiGameflowLayout_t layout, uiGameflowDirection_t directionHint,
	uiGameflowDirection_t rowDirection, bool snapTransition)
{
	uiGameflowLibraryWindowSlot_t slots[UI_GAMEFLOW_LIBRARY_GRID_WINDOW];
	size_t count;
	size_t i;

	if(snapshot == NULL || directory == NULL || numFiles <= 0 ||
		curSelection < 0 || curSelection >= numFiles) {
		return false;
	}
	/* Only the records the window fills are cleared, then copied. */
	memset(snapshot, 0, offsetof(uiGameflowRenderSnapshot_t, records));
	/* filemeta's bounded cache protects this global inclusive sorted-entry range
	 * from eviction. Retained Gameflow wraps, so at minimum pin the selected
	 * entry whose metadata is borrowed by activation. */
	gameflowProtectMetaFile(directory[curSelection]);
	snapshot->selection.generation = ++gameflowSnapshotGeneration;
	snapshot->selection.itemCount = (u32)numFiles;
	snapshot->selection.selectedIndex = (u32)curSelection;
	snapshot->selection.directionHint = directionHint;
	snapshot->selection.snapTransition = snapTransition;
	gameflowCopyText(snapshot->deviceName, sizeof(snapshot->deviceName),
		DeviceDisplayName(devices[DEVICE_CUR]), sizeof(snapshot->deviceName));
	if(gameflowInsideFolder()) {
		char gamesRoot[PATHNAME_MAX];

		gameflowGamesRoot(devices[DEVICE_CUR], gamesRoot);
		UIGameflowLibrary_FolderHeading(gamesRoot, curDir.name,
			snapshot->folder, sizeof(snapshot->folder));
	}
	snapshot->layout = (u8)layout;
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		size_t kept = 0u;

		snapshot->columns = UI_GAMEFLOW_LIBRARY_GRID_COLUMNS;
		count = UIGameflowLibrary_BuildGridWindow((u32)numFiles,
			(u32)curSelection, UI_GAMEFLOW_LIBRARY_GRID_COLUMNS, rowDirection,
			slots);
		/* A grid's rows two away are off screen until a scroll brings them
		 * one row away. Take a game there only once its metadata is in, so
		 * no press waits on reading them. */
		for(i = 0u; i < count; ++i) {
			file_handle *file = directory[slots[i].index];
			bool unread;

			if(file == NULL) {
				return false;
			}
			lockFile(file);
			unread = file->fileType == IS_FILE && file->meta == NULL;
			unlockFile(file);
			if(!unread || (slots[i].relativeSlot >= -1 &&
				slots[i].relativeSlot <= 1)) {
				slots[kept++] = slots[i];
			}
		}
		count = kept;
	}
	else {
		count = UIGameflowLibrary_BuildWindow((u32)numFiles,
			(u32)curSelection, directionHint, slots);
	}
	snapshot->recordCount = (u32)count;
	/* Read the settings files once, outside the per-card file locks. */
	settings_game_files_load();

	for(i = 0u; i < count; ++i) {
		uiGameflowCardSnapshot_t *record = &snapshot->records[i];
		file_handle *file = directory[slots[i].index];
		if(file == NULL) {
			return false;
		}
		/* All but the banner, which is read only under its flag: 256 of
		 * the record's 6,400 bytes. */
		memset(record->title, 0, sizeof(*record) -
			offsetof(uiGameflowCardSnapshot_t, title));
		record->libraryIndex = slots[i].index;
		record->relativeSlot = slots[i].relativeSlot;
		record->column = slots[i].column;
		lockFile(file);
		gameflowSnapshotRecord(record, file, gameflowEntryMode(mode, file));
		unlockFile(file);
	}
	/* Spotlight shows the selected game's description: the card's
	 * descriptions file's, else its disc banner's. */
	if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
		file_handle *file = directory[curSelection];
		const char *gameId = NULL;
		for(i = 0u; i < count; ++i) {
			if(snapshot->records[i].libraryIndex == (u32)curSelection) {
				gameId = snapshot->records[i].gameId;
			}
		}
		if(gameId == NULL || !DrawGameflowDescription(devices[DEVICE_CUR],
			gameId, snapshot->description, sizeof(snapshot->description))) {
			lockFile(file);
			if(file->meta != NULL) {
				gameflowCopyText(snapshot->description,
					sizeof(snapshot->description),
					file->meta->bannerDesc.description, BNR_DESC_LEN);
			}
			unlockFile(file);
		}
	}
	return count > 0u;
}

static bool gameflowReadResolverHeader(file_handle *file,
	uiGameflowResolverEntry_t *entry)
{
	DiskHeader diskHeader;
	TGCHeader tgcHeader;
	u32 headerOffset = 0u;

	if(file == NULL || entry == NULL || file->device == NULL) {
		return false;
	}
	if(endsWith(file->name, ".tgc")) {
		memset(&tgcHeader, 0, sizeof(tgcHeader));
		file->device->seekFile(file, 0, DEVICE_HANDLER_SEEK_SET);
		if(file->device->readFile(file, &tgcHeader, sizeof(tgcHeader)) !=
			sizeof(tgcHeader) || tgcHeader.magic != TGC_MAGIC) {
			return false;
		}
		headerOffset = tgcHeader.headerStart;
	}
	memset(&diskHeader, 0, sizeof(diskHeader));
	file->device->seekFile(file, headerOffset, DEVICE_HANDLER_SEEK_SET);
	if(file->device->readFile(file, &diskHeader, sizeof(diskHeader)) !=
		sizeof(diskHeader) || !valid_gcm_magic(&diskHeader)) {
		return false;
	}
	if(!gameflowCopyDiskId(entry->gameId, (const dvddiskid*)&diskHeader)) {
		return false;
	}
	entry->discNumber = diskHeader.DiscID;
	entry->version = diskHeader.Version;
	entry->discCount = diskHeader.TotalDisc;
	entry->headerValid = true;
	return true;
}

static bool gameflowPopulateResolvedMeta(file_handle *file,
	const uiGameflowResolverEntry_t *header)
{
	if(file == NULL || header == NULL || !header->headerValid) {
		return false;
	}
	populate_meta(file);
	if(file->meta == NULL) {
		return false;
	}
	memcpy(file->meta->diskId.gamename, header->gameId, 4u);
	memcpy(file->meta->diskId.company, &header->gameId[4], 2u);
	file->meta->diskId.disknum = header->discNumber;
	file->meta->diskId.gamever = header->version;
	return true;
}

/* The Library gives up on two possible other discs. From the File Browser,
 * whose folders aren't laid out for the Library, Swiss's choice stands
 * (meta_find_disc2): the one named as the other disc, else the last. */
static file_handle *gameflowFindOppositeImage(file_handle *image,
	const uiGameflowResolverEntry_t *primaryHeader,
	uiGameflowResolverEntry_t *oppositeHeader, bool swissChoice)
{
	file_handle *entries;
	file_handle *match = NULL;
	int entryCount;
	int i;

	if(oppositeHeader != NULL) {
		memset(oppositeHeader, 0, sizeof(*oppositeHeader));
	}
	if(image == NULL || image->device == NULL || primaryHeader == NULL ||
		primaryHeader->discCount <= 1u ||
		(image->device->quirks & QUIRK_GCLOADER_NO_DISC_2)) {
		return NULL;
	}
	entries = getCurrentDirEntries();
	entryCount = getCurrentDirEntryCount();
	for(i = 0; entries != NULL && i < entryCount; ++i) {
		uiGameflowResolverEntry_t candidateHeader;
		file_handle *candidate = &entries[i];
		char knownId[UI_GAMEFLOW_RESOLVER_ID_LENGTH];
		bool known;
		bool headerValid;

		if(candidate == image || candidate->fileType != IS_FILE ||
			candidate->device == NULL ||
			!UIGameflowLibrary_IsGameImageName(candidate->name)) {
			continue;
		}
		/* An image whose metadata names another game is not read. The lock
		 * keeps the metadata cache from freeing it meanwhile. */
		lockFile(candidate);
		known = candidate->meta != NULL;
		if(known) {
			memcpy(knownId, &candidate->meta->diskId, sizeof(knownId));
		}
		unlockFile(candidate);
		if(!UIGameflowResolver_MayBeOppositeDisc(primaryHeader,
			known ? knownId : NULL)) {
			continue;
		}
		memset(&candidateHeader, 0, sizeof(candidateHeader));
		headerValid = gameflowReadResolverHeader(candidate,
			&candidateHeader);
		candidate->device->closeFile(candidate);
		if(!headerValid || !UIGameflowResolver_IsOppositeDisc(
			primaryHeader, &candidateHeader)) {
			continue;
		}
		if(match != NULL && !swissChoice) {
			if(oppositeHeader != NULL) {
				memset(oppositeHeader, 0, sizeof(*oppositeHeader));
			}
			return NULL;
		}
		match = candidate;
		if(oppositeHeader != NULL) {
			memcpy(oppositeHeader, &candidateHeader,
				sizeof(*oppositeHeader));
		}
		if(swissChoice && UIGameflowResolver_NamedAsOppositeDisc(image->name,
			primaryHeader->discNumber, candidate->name,
			candidateHeader.discNumber)) {
			break;
		}
	}
	return match;
}

static void gameflowReleasePrivateChild(size_t index, void *opaque)
{
	gameflowLaunchContext_t *context = opaque;
	file_handle *child = &context->children[index];

	if(child->meta != NULL) {
		meta_free(child->meta);
		child->meta = NULL;
	}
	if(child->device != NULL && child->device->closeFile != NULL) {
		child->device->closeFile(child);
	}
}

static void gameflowReleasePrivateChildren(gameflowLaunchContext_t *context)
{
	if(context == NULL || context->children == NULL) {
		return;
	}
	if(context->childCount > 0) {
		UIGameflowOwnership_ReleaseChildren((size_t)context->childCount,
			gameflowReleasePrivateChild, context);
	}
	free(context->children);
	context->children = NULL;
	context->childCount = 0;
	context->primary = NULL;
	context->oppositeDisc = NULL;
}

static const char *gameflowResolveFailureText(
	uiGameflowResolveStatus_t status)
{
	switch(status) {
		case UI_GAMEFLOW_RESOLVE_NO_IMAGE:
			return "No supported game image was found in this folder.";
		case UI_GAMEFLOW_RESOLVE_AMBIGUOUS:
			return "This folder contains more than one possible game image.";
		case UI_GAMEFLOW_RESOLVE_ID_MISMATCH:
			return "The game image ID does not match the folder ID.";
		case UI_GAMEFLOW_RESOLVE_INVALID_METADATA:
			return "A game image in this folder is invalid or unreadable.";
		default:
			return "This game folder could not be opened safely.";
	}
}

static uint32_t gameflowPresentationInput(void)
{
	u32 buttons = padsButtonsHeld();
	uint32_t input = UI_PRESENTATION_INPUT_NONE;

	if(buttons & BUTTON_A) {
		input |= UI_PRESENTATION_INPUT_A;
	}
	if(buttons & BUTTON_B) {
		input |= UI_PRESENTATION_INPUT_B;
	}
	return input;
}

static void gameflowWaitForPresentationDismiss(
	const uiPresentationSnapshot_t *snapshot)
{
	if(!UIPresentation_Dismissible(snapshot)) {
		return;
	}
	/* The A that opened the folder must never dismiss its error. A fresh A or
	 * B sample acknowledges the state, then its release returns to Library. */
	while(gameflowPresentationInput() != UI_PRESENTATION_INPUT_NONE) {
		VIDEO_WaitVSync();
	}
	while(!UIPresentation_AcceptsInput(snapshot,
		gameflowPresentationInput())) {
		VIDEO_WaitVSync();
	}
	while(gameflowPresentationInput() != UI_PRESENTATION_INPUT_NONE) {
		VIDEO_WaitVSync();
	}
}

static const uiGameflowCardSnapshot_t *gameflowSelectedRecord(
	const uiGameflowRenderSnapshot_t *snapshot)
{
	int i;

	if(snapshot == NULL) {
		return NULL;
	}
	for(i = 0; i < (int)snapshot->recordCount; ++i) {
		if(snapshot->records[i].libraryIndex ==
			snapshot->selection.selectedIndex) {
			return &snapshot->records[i];
		}
	}
	return NULL;
}

static bool gameflowResolveAndLoadFolder(file_handle *folder,
	uiDrawObj_t *event, const uiGameflowRenderSnapshot_t *snapshot,
	bool openSettings)
{
	gameflowLaunchContext_t context;
	uiPresentationSnapshot_t presentationSnapshot;
	uiDrawObj_t *presentation = NULL;
	uiGameflowResolverFolder_t resolverFolder;
	uiGameflowResolverEntry_t *resolverEntries = NULL;
	uiGameflowResolverResult_t result;
	uiGameflowResolveStatus_t status =
		UI_GAMEFLOW_RESOLVE_INVALID_ARGUMENT;
	const uiGameflowCardSnapshot_t *selectedRecord = NULL;
	const char *folderName;
	int i;

	if(folder == NULL || event == NULL || snapshot == NULL ||
		folder->fileType != IS_DIR || folder->device == NULL) {
		return false;
	}
	selectedRecord = gameflowSelectedRecord(snapshot);
	folderName = getRelativeName(folder->name);
	memset(&context, 0, sizeof(context));
	memset(&resolverFolder, 0, sizeof(resolverFolder));
	if(selectedRecord == NULL ||
		!UIGameflowLibrary_ParseGameFolderName(folderName,
			resolverFolder.gameId, NULL, 0u) ||
		strncmp(resolverFolder.gameId, selectedRecord->gameId,
			UI_GAMEFLOW_DETAIL_ID_LENGTH)) {
		return false;
	}
	if(UIPresentation_Build(&presentationSnapshot, UI_PRESENTATION_LOADING,
		"OPENING GAME", "Checking this folder for a safe game image.",
		"Your Library selection and source stay in place.", NULL)) {
		uiDrawObj_t *candidate = DrawPresentation(&presentationSnapshot);

		if(candidate != NULL) {
			presentation = DrawPublish(candidate);
		}
	}

	context.event = event;
	memcpy(&context.rootFolder, folder, sizeof(context.rootFolder));
	context.generation = snapshot->selection.generation;
	context.focusIndex = snapshot->selection.selectedIndex;
	context.openSettings = openSettings;
	memcpy(context.gameId, resolverFolder.gameId, sizeof(context.gameId));
	context.childCount = folder->device->readDir(&context.rootFolder,
		&context.children, -1);
	if(context.childCount > 0 && context.children != NULL) {
		resolverEntries = calloc((size_t)context.childCount,
			sizeof(*resolverEntries));
	}
	if(resolverEntries != NULL) {
		for(i = 0; i < context.childCount; ++i) {
			file_handle *child = &context.children[i];
			uiGameflowResolverEntry_t *entry = &resolverEntries[i];
			const char *leafName = getRelativeName(child->name);
			size_t leafLength = strnlen(leafName, PATHNAME_MAX);

			entry->type = gameflowEntryType(child);
			entry->sourceIndex = (u32)i;
			if(strncmp(leafName, "._", 2u) == 0 ||
				(!swissSettings.showHiddenFiles &&
				((child->fileAttrib & ATTRIB_HIDDEN) || leafName[0] == '.'))) {
				entry->type = UI_GAMEFLOW_LIBRARY_ENTRY_OTHER;
				continue;
			}
			if(leafLength >= sizeof(entry->name)) {
				strcpy(entry->name, "invalid.iso");
				continue;
			}
			memcpy(entry->name, leafName, leafLength + 1u);
			if(entry->type == UI_GAMEFLOW_LIBRARY_ENTRY_FILE &&
				UIGameflowLibrary_IsGameImageName(entry->name)) {
				gameflowReadResolverHeader(child, entry);
			}
		}
		status = UIGameflowResolver_Resolve(&resolverFolder,
			resolverEntries, (size_t)context.childCount, &result);
	}
	if(status != UI_GAMEFLOW_RESOLVE_OK ||
		result.primarySourceIndex >= (u32)context.childCount ||
		(result.hasOppositeDisc &&
		result.oppositeDiscSourceIndex >= (u32)context.childCount)) {
		uiDrawObj_t *candidate;

		free(resolverEntries);
		gameflowReleasePrivateChildren(&context);
		if(UIPresentation_Build(&presentationSnapshot,
			UI_PRESENTATION_RECOVERABLE_ERROR, "CAN'T OPEN THIS GAME",
			gameflowResolveFailureText(status),
			"Nothing changed. Choose another title or repair the folder.",
			"A / B  RETURN TO LIBRARY")) {
			if(presentation != NULL &&
				!DrawUpdatePresentation(presentation,
					&presentationSnapshot)) {
				DrawDispose(presentation);
				presentation = NULL;
			}
			if(presentation == NULL) {
				candidate = DrawPresentation(&presentationSnapshot);
				if(candidate != NULL) {
					presentation = DrawPublish(candidate);
				}
			}
			if(presentation == NULL) {
				presentation = DrawPublish(DrawMessageBox(D_WARN,
					gameflowResolveFailureText(status)));
			}
			gameflowWaitForPresentationDismiss(&presentationSnapshot);
		}
		if(presentation != NULL) {
			DrawDispose(presentation);
		}
		return true;
	}
	if(presentation != NULL) {
		DrawDispose(presentation);
		presentation = NULL;
	}

	context.primary = &context.children[result.primarySourceIndex];
	context.oppositeDisc = result.hasOppositeDisc &&
		!(folder->device->quirks & QUIRK_GCLOADER_NO_DISC_2) ?
		&context.children[result.oppositeDiscSourceIndex] : NULL;
	gameflowPopulateResolvedMeta(context.primary,
		&resolverEntries[result.primarySourceIndex]);
	if(context.oppositeDisc != NULL &&
		!gameflowPopulateResolvedMeta(context.oppositeDisc,
			&resolverEntries[result.oppositeDiscSourceIndex])) {
		context.oppositeDisc = NULL;
	}
	free(resolverEntries);
	memcpy(&curFile, context.primary, sizeof(curFile));
	load_file_with_context(&context);
	memcpy(context.primary, &curFile, sizeof(*context.primary));
	memcpy(&curFile, &context.rootFolder, sizeof(curFile));
	DrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);
	DrawClearGameflowDetail(context.event);
	gameflowReleasePrivateChildren(&context);
	return true;
}

/* Detail opened from the File Browser (filesOpenDetail): the event isn't on
 * screen until Detail is ready, and the File Browser's page comes back over
 * it before it turns back to the Library. gameflowFilesPage is that page
 * while it is up; gameflowFilesShown, once the event has been. */
static bool gameflowFromFiles;
static bool gameflowFilesShown;
static uiDrawObj_t *gameflowFilesPage;

/* Detail or the launch screen first shows: from the File Browser, the event
 * takes the page's place already in that mode, so no card flies in from a
 * Library slot and no Library frame is drawn. */
static void gameflowShowFromFiles(uiDrawObj_t *event, uiGameflowMode_t mode)
{
	if(!gameflowFromFiles || gameflowFilesShown) {
		DrawSetGameflowMode(event, mode);
		return;
	}
	/* Detail draws nothing until the cube is back behind it (libraryReveal):
	 * from a File Browser over Home, the page hides the cube going back. */
	for(int vsync = 0; vsync < 60 && UIScene_Frame()->libraryReveal < 1.0f;
		vsync++) {
		VIDEO_WaitVSync();
	}
	if(gameflowFilesPage == filesPage) filesPage = NULL;
	DrawSetGameflowModeNow(event, mode);
	DrawRepublish(gameflowFilesPage, event);
	gameflowFilesPage = NULL;
	gameflowFilesShown = true;
}

/* Before the event goes back to Library mode: from the File Browser, its
 * page again, settled, on top, so the slide back to a Library slot happens
 * under it. Once is enough. */
static void gameflowBackToFiles(void)
{
	if(gameflowFromFiles && gameflowFilesPage == NULL) {
		gameflowFilesPage = DrawFilesSettled(&filesSnapshot);
		if(gameflowFilesPage != NULL) {
			DrawPublish(gameflowFilesPage);
		}
	}
}

/* A launch starts from a fresh handle. A Library entry keeps the file
 * its banner was read through, and a read error on a slow SD card over EXI
 * leaves that file failing every later read. A launch that failed after setup
 * leaves the entry marked as mapped, and the next map of it would be wrong. */
static void gameflowFreshHandle(file_handle *file)
{
	file->device->closeFile(file);
	if(file->status == STATUS_HAS_MAPPING) {
		file->status = STATUS_NOT_MAPPED;
	}
}

static bool gameflowLoadImageWithContext(file_handle *image,
	uiDrawObj_t *event, const uiGameflowRenderSnapshot_t *snapshot,
	bool openSettings)
{
	gameflowLaunchContext_t context;
	uiGameflowResolverEntry_t headerEntry;
	uiGameflowResolverEntry_t oppositeHeader;
	const uiGameflowCardSnapshot_t *selectedRecord;

	if(image == NULL || event == NULL || snapshot == NULL ||
		image->fileType != IS_FILE || image->device == NULL ||
		!UIGameflowLibrary_IsGameImageName(image->name)) {
		return false;
	}
	selectedRecord = gameflowSelectedRecord(snapshot);
	if(selectedRecord == NULL) {
		return false;
	}
	memset(&headerEntry, 0, sizeof(headerEntry));
	gameflowFreshHandle(image);
	if(!gameflowReadResolverHeader(image, &headerEntry) ||
		(selectedRecord->gameId[0] &&
		strncmp(selectedRecord->gameId, headerEntry.gameId,
			UI_GAMEFLOW_DETAIL_ID_LENGTH))) {
		return false;
	}

	memset(&context, 0, sizeof(context));
	context.event = event;
	memcpy(&context.rootFolder, &curDir, sizeof(context.rootFolder));
	context.primary = image;
	context.generation = snapshot->selection.generation;
	context.focusIndex = snapshot->selection.selectedIndex;
	context.openSettings = openSettings;
	memcpy(context.gameId, headerEntry.gameId, sizeof(context.gameId));
	gameflowProtectMetaFile(image);
	gameflowPopulateResolvedMeta(image, &headerEntry);
	memcpy(&curFile, image, sizeof(curFile));
	context.oppositeDisc = gameflowFindOppositeImage(image, &headerEntry,
		&oppositeHeader, gameflowFromFiles);
	if(context.oppositeDisc != NULL) {
		gameflowFreshHandle(context.oppositeDisc);
	}
	if(context.oppositeDisc != NULL &&
		!gameflowPopulateResolvedMeta(context.oppositeDisc,
			&oppositeHeader)) {
		context.oppositeDisc = NULL;
	}
	load_file_with_context(&context);
	/* A non-NULL context suppresses load_game's legacy close because strict
	 * folders own private children. Flattened entries are borrowed from the
	 * public directory instead, so close them exactly as the legacy image path
	 * does before copying the selected handle state back to its owner. */
	devices[DEVICE_CUR]->closeFile(&curFile);
	devices[DEVICE_CUR]->closeFile(context.oppositeDisc);
	memcpy(image, &curFile, sizeof(*image));
	gameflowBackToFiles();
	DrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);
	DrawClearGameflowDetail(context.event);
	return true;
}

/* A one-game Library window for Detail: the File Browser's focused entry,
 * selected, with the ID its disc header gave (the Library's identity check
 * and the posters go by it). */
static void filesDetailSnapshot(uiGameflowRenderSnapshot_t *snapshot,
	file_handle *entry, const uiGameflowResolverEntry_t *header)
{
	uiGameflowCardSnapshot_t *record = &snapshot->records[0];

	memset(snapshot, 0, offsetof(uiGameflowRenderSnapshot_t, records) +
		sizeof(snapshot->records[0]));
	snapshot->selection.generation = ++gameflowSnapshotGeneration;
	snapshot->selection.itemCount = (u32)getSortedDirEntryCount();
	snapshot->selection.selectedIndex = (u32)curSelection;
	snapshot->recordCount = 1u;
	snapshot->layout = UI_GAMEFLOW_LAYOUT_HORIZONTAL;
	gameflowCopyText(snapshot->deviceName, sizeof(snapshot->deviceName),
		DeviceDisplayName(devices[DEVICE_CUR]), sizeof(snapshot->deviceName));
	record->libraryIndex = (u32)curSelection;
	record->relativeSlot = 0;
	gameflowSnapshotRecord(record, entry, UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
	gameflowCopyText(record->gameId, sizeof(record->gameId), header->gameId,
		sizeof(header->gameId));
}

/* A on a game in the File Browser's left pane opens Indigo's Game Detail,
 * as the Library does: its posters, its Saves box, settings and cheats,
 * Launch Game. Nothing else is mounted meanwhile. The page stays up until
 * Detail is ready and is back before Detail goes, so neither is seen
 * leaving; B comes back to the same row. A disc whose header won't read,
 * or a Detail that can't be built, goes to Swiss's own load_file. */
static bool filesOpensDetail(const file_handle *entry)
{
	return entry->fileType == IS_FILE &&
		UIGameflowLibrary_IsGameImageName(entry->name) &&
		(devices[DEVICE_CUR]->features & FEAT_BOOT_GCM);
}

static void filesOpenDetail(file_handle **directory, uiDrawObj_t **filePanel)
{
	file_handle *entry = directory[curSelection];
	uiGameflowRenderSnapshot_t *snapshot = NULL;
	uiGameflowResolverEntry_t header;
	uiDrawObj_t *event = NULL;
	bool handled = false;

	meta_thread_stop();
	filesOtherRelease();
	lockFile(entry);
	memset(&header, 0, sizeof(header));
	gameflowFreshHandle(entry);
	if(gameflowReadResolverHeader(entry, &header)) {
		snapshot = memalign(32, sizeof(*snapshot));
	}
	if(snapshot != NULL) {
		filesDetailSnapshot(snapshot, entry, &header);
		/* The Library's one bounded poster read before Detail. */
		DrawGameflowRequestPosters(devices[DEVICE_CUR], snapshot);
		DrawGameflowPrimePoster();
		event = DrawGameflow(snapshot);
	}
	if(event != NULL) {
		gameflowFilesPage = *filePanel;
		gameflowFilesShown = false;
		gameflowFromFiles = true;
		handled = gameflowLoadImageWithContext(entry, event, snapshot, false);
		gameflowFromFiles = false;
		if(gameflowFilesShown) {
			DrawDispose(event);
		}
		else {
			DrawDiscard(event);
		}
		*filePanel = filesPage = gameflowFilesPage;
		gameflowFilesPage = NULL;
	}
	free(snapshot);
	unlockFile(entry);
	if(!handled) {
		filesActivate(directory, false);
	}
	/* Detail turned the cube to its own scene and the Library's. */
	UIScene_Request(filesScene);
}

/* The Library's layout from Setup; anything unknown is the carousel. */
uiGameflowLayout_t gameflowLayout(void)
{
	return swissSettings.libraryLayout > UI_GAMEFLOW_LAYOUT_HORIZONTAL &&
		swissSettings.libraryLayout < UI_GAMEFLOW_LAYOUT_COUNT ?
		(uiGameflowLayout_t)swissSettings.libraryLayout :
		UI_GAMEFLOW_LAYOUT_HORIZONTAL;
}

/* The stick follows the layout: across the carousel, up and down the
 * column, both ways in the grid. */
u32 gameflowMenuInputPolicy(uiGameflowLayout_t layout)
{
	switch(layout) {
		case UI_GAMEFLOW_LAYOUT_VERTICAL:
			return UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT;
		case UI_GAMEFLOW_LAYOUT_GRID:
			return UI_MENU_INPUT_AXIS_BOTH | UI_MENU_INPUT_REPEAT;
		default:
			return UI_MENU_INPUT_AXIS_HORIZONTAL | UI_MENU_INPUT_REPEAT;
	}
}

/* Library Folders: a folder's poster is its picture, the PNG beside it
 * with its name (Racing.png beside Racing), or else its name (card_art).
 * Card i is the listing's entry i, and card_art holds the posters of the
 * listing folderArtListing names until Home takes over. Its poster thread
 * reads the listing, so it is paused whenever the browser below returns or
 * does anything that may read the listing again or start a game. */
static bool folderArtOpen;
static u64 folderArtListing;
static DEVICEHANDLER_INTERFACE *folderArtDevice;
static u8 *folderArtFailed;	/* by card: its picture made no poster */

/* The folder's picture, from the listing as the device read it: the sorted
 * listing leaves a PNG out when unknown file types are hidden. */
static file_handle *folderArtPicture(int32_t card)
{
	file_handle *entries = getCurrentDirEntries();
	const char *folder = getSortedDirEntries()[card]->name;
	int count = getCurrentDirEntryCount();

	for(int i = 0; i < count; ++i) {
		if(entries[i].fileType == IS_FILE &&
			UIGameflowLibrary_IsFolderPicture(folder, entries[i].name)) {
			return &entries[i];
		}
	}
	return NULL;
}

static uint32_t folderArtPictureSize(int32_t card)
{
	file_handle *picture;

	if((folderArtFailed != NULL && folderArtFailed[card]) ||
		(picture = folderArtPicture(card)) == NULL) {
		return 0u;
	}
	return picture->size;
}

static bool folderArtReadPicture(int32_t card, uint32_t offset, uint8_t *data, uint32_t size)
{
	file_handle *picture = folderArtPicture(card);
	file_handle file;
	s32 read;

	if(picture == NULL) {
		return false;
	}
	/* A copy, nothing of it open, as Apps reads its pictures. */
	memcpy(&file, picture, sizeof(file));
	file.offset = offset;
	file.fp = NULL;
	file.ffsFp = NULL;
	file.meta = NULL;
	file.uiObj = NULL;
	file.lockCount = 0;
	file.thread = LWP_THREAD_NULL;
	read = folderArtDevice->readFile(&file, data, size);
	folderArtDevice->closeFile(&file);
	return read == (s32)size;
}

static const char *folderArtName(int32_t card)
{
	return getRelativeName(getSortedDirEntries()[card]->name);
}

/* A picture that made no poster isn't tried again in this listing. */
static void folderArtVerdict(int32_t card, bool ok)
{
	if(folderArtFailed != NULL) {
		folderArtFailed[card] = !ok;
	}
}

/* The listing, as its posters are kept: its device, its folder and its
 * entries in order (FNV-1a). Read again unchanged, it keeps them. */
static u64 folderArtIdentity(file_handle **directory, int count)
{
	u64 hash = 14695981039346656037ull ^ (uintptr_t)devices[DEVICE_CUR];

	for(int i = -1; i < count; ++i) {
		const char *name = i < 0 ? curDir.name : directory[i]->name;

		do {
			hash = (hash ^ (u8)*name) * 1099511628211ull;
		} while(*name++ != '\0');
	}
	return hash;
}

/* Before a snapshot with a folder on it is shown: card_art goes on with
 * this listing's posters, or starts on them. */
static void folderArtShow(const uiGameflowRenderSnapshot_t *snapshot,
	u64 listing)
{
	cardArtSource_t source = {folderArtPictureSize, folderArtReadPicture,
		folderArtName, folderArtVerdict, false};
	u32 i;

	for(i = 0u; i < snapshot->recordCount && !snapshot->records[i].subfolder;
		++i) {
	}
	if(i == snapshot->recordCount) {
		return;
	}
	if(folderArtOpen && folderArtListing == listing) {
		CardArt_Resume();
		return;
	}
	CardArt_Pause();
	free(folderArtFailed);
	folderArtFailed = calloc(getSortedDirEntryCount(), 1);
	folderArtDevice = devices[DEVICE_CUR];
	folderArtListing = listing;
	folderArtOpen = true;
	source.threadSafe = (folderArtDevice->features & FEAT_THREAD_SAFE) != 0u;
	CardArt_Open(&source);
}

/* The folders on screen, nearest first: the posters to keep or make. */
static void folderArtWant(const uiGameflowRenderSnapshot_t *snapshot,
	u64 listing)
{
	int32_t want[UI_APPS_ART_SLOTS];
	u32 wanted = 0u;

	if(!folderArtOpen || folderArtListing != listing) {
		return;
	}
	for(u32 i = 0u; i < snapshot->recordCount && wanted < UI_APPS_ART_SLOTS;
		++i) {
		if(snapshot->records[i].subfolder) {
			want[wanted++] = (int32_t)snapshot->records[i].libraryIndex;
		}
	}
	CardArt_Want(want, wanted);
}

static void folderArtClose(void)
{
	if(!folderArtOpen) {
		return;
	}
	CardArt_Close();
	free(folderArtFailed);
	folderArtFailed = NULL;
	folderArtOpen = false;
}

/* The Library: a Library location's games (and, with Library Folders, its
 * folders), in the layout Setup chose. menu_loop calls it only where
 * gameflowLibraryMode finds a Library. */
uiDrawObj_t* renderFileCarousel(file_handle** directory, int num_files, uiDrawObj_t* filePanel)
{
	memset(txtbuffer,0,sizeof(txtbuffer));
	if(num_files<=0) {
		memcpy(&curDir, devices[DEVICE_CUR]->initial, sizeof(file_handle));
		needsRefresh=1;
		return filePanel;
	}
	if(curSelection == 0 && num_files > 1 && directory[0]->fileType==IS_SPECIAL) {
		curSelection = 1; // skip the ".." by default
	}
	uiGameflowLibraryMode_t gameflowMode = gameflowLibraryMode(directory, num_files);
	/* Only Library Folders shows folders, and their posters. */
	u64 folderListing = swissSettings.libraryFolders ?
		folderArtIdentity(directory, num_files) : 0u;
	uiGameflowDirection_t gameflowDirection = UI_GAMEFLOW_DIRECTION_NONE;
	/* The last move between grid rows: a two-row grid keeps its other row
	 * on the side that move left it. */
	uiGameflowDirection_t gameflowRowDirection = UI_GAMEFLOW_DIRECTION_NONE;
	bool gameflowSnapTransition = false;
	uiGameflowRenderSnapshot_t *gameflowSnapshot =
		memalign(32, sizeof(uiGameflowRenderSnapshot_t));
	uiDrawObj_t *loadingBox = DrawProgressLoading(PROGRESS_BOX_TOPLEFT);
	DrawPublish(loadingBox);
	meta_thread_start(loadingBox);
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace = VIDEO_GetRetraceCount();
	UIMenuInput_Init(&menuInput);
	while(1) {
		uiGameflowLayout_t layout = gameflowLayout();
		/* The Library is drawn: its snapshot was allocated and built and
		 * the frame took it. */
		bool drawn = gameflowSnapshot != NULL;
		DrawUpdateProgressLoading(loadingBox, +1);
		if(drawn && !gameflowBuildSnapshot(gameflowSnapshot, directory,
			num_files, gameflowMode, layout, gameflowDirection,
			gameflowRowDirection, gameflowSnapTransition)) {
			drawn = false;
		}
		if(drawn) {
			gameflowDirection = UI_GAMEFLOW_DIRECTION_NONE;
			gameflowSnapTransition = false;
			folderArtShow(gameflowSnapshot, folderListing);
			if(!DrawUpdateGameflow(filePanel, gameflowSnapshot)) {
				uiDrawObj_t *newPanel = DrawGameflow(gameflowSnapshot);
				if(newPanel != NULL) {
					if(filePanel == filesPage) filesPage = NULL;
					filePanel = DrawRepublish(filePanel, newPanel);
				}
				else {
					drawn = false;
				}
			}
			if(drawn) {
				/* Strict game-library eligibility and a retained snapshot are
				 * both proven before the private poster pack is touched. */
				DrawGameflowRequestPosters(devices[DEVICE_CUR],
					gameflowSnapshot);
				folderArtWant(gameflowSnapshot, folderListing);
			}
		}
		DrawUpdateProgressLoading(loadingBox, -1);
		if(!drawn) {
			/* No Library to draw here (no memory for it): the File Browser
			 * shows this folder instead, read again, since
			 * gameflowLibraryEntries has moved its games to the front. */
			lockFile(directory[curSelection]);
			memcpy(curFile.name, directory[curSelection]->name, sizeof(curFile.name));
			unlockFile(directory[curSelection]);
			gameflowListFallback = true;
			needsRefresh = 1;
			break;
		}
		
		u32 waitButtons = BUTTON_X|BUTTON_START|BUTTON_B|BUTTON_A|BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_L|BUTTON_R|BUTTON_Z|BUTTON_CLAP|PAD_BUTTON_Y;
		u32 menuInputPolicy = gameflowMenuInputPolicy(layout);
		u32 browserButtons;
		uiMenuInputDirection_t analog;
		/* Presses from before (the A that opened this, a B that left a
		 * Detail) aren't for here. */
		(void)padsButtonsTaken(waitButtons);
		while(1) {
			/* Taken from the scans as well as held: a press made and let go
			 * while a folder picture was read is still seen. */
			browserButtons = padsButtonsHeld() | padsButtonsTaken(waitButtons);
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				menuInputPolicy, (browserButtons & waitButtons) != 0u);
			if((browserButtons & waitButtons) != 0u ||
					analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
			/* Each pack and a non-thread-safe folder picture advance by
			 * at most 16 KiB, then input gets another turn. Thread-safe
			 * picture reads stay on their own worker. */
			DrawGameflowPollPosters();
			CardArt_Poll();
		}
		/* Y on a game opens its settings through its Detail: never on the
		 * parent card, and A wins a press of both. */
		bool openSettings = !(browserButtons & BUTTON_A) &&
			(browserButtons & PAD_BUTTON_Y) &&
			UIGameflowLibrary_UsesRetainedDetail(
				gameflowEntryMode(gameflowMode, directory[curSelection]),
				gameflowEntryType(directory[curSelection]));
		bool retainedActivation = (browserButtons & BUTTON_A) || openSettings;
		/* A and Y own a retained-library input frame. Moving curSelection
		 * first would pair the new directory entry with the previous
		 * immutable snapshot and bypass the strict folder resolver/dashboard. */
		if(!retainedActivation) {
			bool left = (browserButtons & BUTTON_LEFT) ||
				analog == UI_MENU_INPUT_LEFT;
			bool right = (browserButtons & BUTTON_RIGHT) ||
				analog == UI_MENU_INPUT_RIGHT;
			bool up = (browserButtons & BUTTON_UP) ||
				analog == UI_MENU_INPUT_UP;
			bool down = (browserButtons & BUTTON_DOWN) ||
				analog == UI_MENU_INPUT_DOWN;
			uiGameflowLibraryMove_t moves[6];
			u32 columns = layout == UI_GAMEFLOW_LAYOUT_GRID ?
				UI_GAMEFLOW_LIBRARY_GRID_COLUMNS : 0u;
			int moveCount = 0;

			/* Each layout's presses, applied in this order. */
			if(layout == UI_GAMEFLOW_LAYOUT_VERTICAL) {
				/* The column: up and down step, left and right page. */
				if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
				if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
				if(left || (browserButtons & BUTTON_L))
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
				if(right || (browserButtons & BUTTON_R))
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
			}
			else if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
				/* The grid: every direction steps, L and R page. */
				if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
				if(right) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
				if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_UP;
				if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_DOWN;
				if(browserButtons & BUTTON_L)
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
				if(browserButtons & BUTTON_R)
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
			}
			else {
				/* The carousel: left and right step, up and down page. */
				if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;
				if(right) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;
				if(up || (browserButtons & BUTTON_L))
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;
				if(down || (browserButtons & BUTTON_R))
					moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;
			}
			for(int move = 0; move < moveCount; ++move) {
				uiGameflowLibraryStep_t step;

				if(!UIGameflowLibrary_Move((u32)num_files, columns,
					(u32)curSelection, moves[move], columns ?
					UI_GAMEFLOW_LIBRARY_GRID_ROWS : FILES_PER_PAGE_CAROUSEL,
					&step)) {
					continue;
				}
				if(columns && step.index / columns != (u32)curSelection / columns) {
					gameflowRowDirection = step.direction;
				}
				curSelection = (int)step.index;
				gameflowDirection = step.direction;
				gameflowSnapTransition = step.snap;
			}
		}
		filesBarrelGame(loadingBox);
		
		if((browserButtons & BUTTON_A) || openSettings) {
			/* What follows may start a game or read the listing again:
			 * no folder's poster is made meanwhile. */
			CardArt_Pause();
			/* A folder of games (Library Folders) opens below, as
			 * Swiss opens any folder. */
			uiGameflowLibraryMode_t entryMode =
				gameflowEntryMode(gameflowMode, directory[curSelection]);
			if(UIGameflowLibrary_UsesRetainedDetail(
				entryMode,
				gameflowEntryType(directory[curSelection]))) {
				bool handled = false;

				meta_thread_stop();
				/* Finish the already-requested center cover before Detail
				 * freezes the request window, as the File Browser's Detail
				 * does. Detail draws its BNR until a cover still missing lands:
				 * its idle retraces poll for it. */
				DrawGameflowPrimePoster();
				if(entryMode == UI_GAMEFLOW_LIBRARY_GAME_FOLDERS) {
					file_handle folderSnapshot;

					lockFile(directory[curSelection]);
					memcpy(&folderSnapshot, directory[curSelection],
						sizeof(folderSnapshot));
					unlockFile(directory[curSelection]);
					folderSnapshot.lockCount = 0;
					folderSnapshot.thread = LWP_THREAD_NULL;
					folderSnapshot.fp = NULL;
					folderSnapshot.ffsFp = NULL;
					folderSnapshot.meta = NULL;
					folderSnapshot.uiObj = NULL;
					handled = gameflowResolveAndLoadFolder(&folderSnapshot,
						filePanel, gameflowSnapshot, openSettings);
				}
				else {
					lockFile(directory[curSelection]);
					handled = gameflowLoadImageWithContext(
						directory[curSelection], filePanel,
						gameflowSnapshot, openSettings);
					unlockFile(directory[curSelection]);
				}
				if(handled) {
					break;
				}
			}
			if(openSettings) {
				/* Y has no legacy meaning: it never opens or boots a file. */
				while(padsButtonsHeld() & PAD_BUTTON_Y) VIDEO_WaitVSync();
				break;
			}
			filesActivate(directory, true);
			break;
		}
		if(browserButtons & BUTTON_X) {
			gameflowNavigateParent(true, directory[0]);
			while(padsButtonsHeld() & BUTTON_X) VIDEO_WaitVSync();
			break;
		}
		if((browserButtons & BUTTON_Z) &&
				layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE) {
			/* Z stops the discs spinning or starts them, and the choice is
			 * kept: the Jewel Case has no file management. Only its key is
			 * saved, as the autoload's is, with the readers stopped while it
			 * writes; the folders' posters go on with the next frame. */
			swissSettings.stillDiscs ^= 1;
			menuaudio_select();
			meta_thread_stop();
			CardArt_Pause();
			config_update_spin_discs(true);
			meta_thread_start(loadingBox);
		}
		else if((browserButtons & BUTTON_Z) && filesManage(directory)) {
			break;
		}

		if((browserButtons & BUTTON_B) && gameflowInsideFolder()) {
			/* Library Folders: B goes up a folder, as X does; at /games
			 * it goes Home. */
			gameflowNavigateParent(true, directory[0]);
			while(padsButtonsHeld() & BUTTON_B) VIDEO_WaitVSync();
			break;
		}
		if(browserButtons & BUTTON_B) {
			filesHome();
			break;
		}
		if((browserButtons & BUTTON_START) && filesRecent(true)) {
			break;
		}
		while (padsButtonsHeld() & waitButtons) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				menuInputPolicy, true);
			VIDEO_WaitVSync();
		}
	}
	meta_thread_stop();
	/* The folders' posters stay on screen while the next listing is read. */
	CardArt_Pause();
	DrawDispose(loadingBox);
	free(gameflowSnapshot);
	return filePanel;
}

bool select_dest_dir(file_handle* initial, char* selection)
{
	file_handle **directory = NULL;
	file_handle *curDirEntries = NULL;
	file_handle curDir;
	memcpy(&curDir, initial, sizeof(file_handle));
	int i = 0, j = 0, max = 0, refresh = 1, num_files =0, idx = 0;
	const u32 waitButtons = BUTTON_X | BUTTON_A | BUTTON_B | BUTTON_UP | BUTTON_DOWN;
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace = VIDEO_GetRetraceCount();
	
	bool cancelled = false;
	int fileListBase = 90;
	int scrollBarHeight = (FILES_PER_PAGE*40);
	int scrollBarTabHeight = (int)((float)scrollBarHeight/(float)num_files);
	uiDrawObj_t* destDirBox = NULL;
	UIMenuInput_Init(&menuInput);
	while(1){
		u32 buttons;
		uiMenuInputDirection_t analog;
		// Read the directory
		if(refresh) {
			free(directory);
			free(curDirEntries);
			curDirEntries = NULL;
			num_files = devices[DEVICE_DEST]->readDir(&curDir, &curDirEntries, IS_DIR);
			num_files = sortFiles(curDirEntries, num_files, &directory);
			if(num_files <= 1 && destDirBox == NULL) {
				strcpy(selection, curDir.name);
				break;
			}
			refresh = idx = 0;
			scrollBarTabHeight = (int)((float)scrollBarHeight/(float)num_files);
		}
		uiDrawObj_t* tempBox = DrawEmptyBox(20,40, getVideoMode()->fbWidth-20, 450);
		DrawAddChild(tempBox, DrawHintLabel(50, 67, "X  Choose this folder", 1.0f, ALIGN_LEFT, defaultColor));
		i = MIN(MAX(0,idx-FILES_PER_PAGE/2),MAX(0,num_files-FILES_PER_PAGE));
		max = MIN(num_files, MAX(idx+FILES_PER_PAGE/2,FILES_PER_PAGE));
		if(num_files > FILES_PER_PAGE)
			DrawAddChild(tempBox, DrawVertScrollBar(getVideoMode()->fbWidth-30, fileListBase, 16, scrollBarHeight, (float)((float)idx/(float)(num_files-1)),scrollBarTabHeight));
		for(j = 0; i<max; ++i,++j) {
			DrawAddChild(tempBox, DrawSelectableButton(50,fileListBase+(j*40), getVideoMode()->fbWidth-35, fileListBase+(j*40)+40, getRelativeName(directory[i]->name), (i == idx) ? B_SELECTED:B_NOSELECT));
		}
		destDirBox = DrawRepublish(destDirBox, tempBox);
		/* The stick repeats on the shared list schedule, like every other list. */
		while(1) {
			buttons = padsButtonsHeld();
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(buttons & waitButtons) != 0u);
			if((buttons & waitButtons) != 0u || analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
		}
		if((buttons & BUTTON_UP) || analog == UI_MENU_INPUT_UP){	idx = (--idx < 0) ? num_files-1 : idx;}
		if((buttons & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {idx = (idx + 1) % num_files;	}
		if((buttons & PAD_BUTTON_A))	{
			//go into a folder or select a file
			if(directory[idx]->fileType==IS_DIR) {
				memcpy(&curDir, directory[idx], sizeof(file_handle));
				refresh=1;
			}
			else if(directory[idx]->fileType==IS_SPECIAL){
				curDir.fileBase = directory[idx]->fileBase;
				upToParent(&curDir);
				refresh=1;
			}
		}
		if(buttons & BUTTON_X)	{
			strcpy(selection, curDir.name);
			break;
		}
		if(buttons & BUTTON_B)	{
			cancelled = true;
			break;
		}
		while((padsButtonsHeld() & waitButtons) != 0u) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT, true);
			VIDEO_WaitVSync();
		}
	}
	if(destDirBox != NULL) {
		DrawDispose(destDirBox);
	}
	free(curDirEntries);
	free(directory);
	return cancelled;
}

// Alt DOL sorting/selecting code
int exccomp(const void *a1, const void *b1)
{
	const ExecutableFile* a = a1;
	const ExecutableFile* b = b1;
	
	if(!a && b) return 1;
	if(a && !b) return -1;
	if(!a && !b) return 0;
	
	if(in_range(a->type, PATCH_BIN, PATCH_ELF) && !in_range(b->type, PATCH_BIN, PATCH_ELF))
		return -1;
	if(!in_range(a->type, PATCH_BIN, PATCH_ELF) && in_range(b->type, PATCH_BIN, PATCH_ELF))
		return 1;
	
	if(a->offset == GCMDisk.DOLOffset && b->offset != GCMDisk.DOLOffset)
		return -1;
	if(a->offset != GCMDisk.DOLOffset && b->offset == GCMDisk.DOLOffset)
		return 1;
	
	return strcasecmp(a->name, b->name);
}

void sortDols(ExecutableFile *filesToPatch, int num_files)
{
	if(num_files > 0) {
		qsort(filesToPatch, num_files, sizeof(ExecutableFile), exccomp);
	}
}

// Allow the user to select an alternate DOL
ExecutableFile* select_alt_dol(ExecutableFile *filesToPatch, int num_files) {
	if(swissSettings.autoBoot) return NULL;
	int i = 0, j = 0, max = 0, idx = 0, page = 4;
	sortDols(filesToPatch, num_files);	// Sort DOL to the top
	for(i = 0; i < num_files; i++) {
		if(!in_range(filesToPatch[i].type, PATCH_BIN, PATCH_ELF)) {
			num_files = i;
			break;
		}
	}
	if(num_files < 2) return NULL;
	
	int fileListBase = 175;
	int scrollBarHeight = (page*40);
	int scrollBarTabHeight = (int)((float)scrollBarHeight/(float)num_files);
	const u32 waitButtons = BUTTON_A | BUTTON_B | BUTTON_UP | BUTTON_DOWN;
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace = VIDEO_GetRetraceCount();
	uiDrawObj_t *container = NULL;
	UIMenuInput_Init(&menuInput);
	while(1) {
		u32 buttons;
		uiMenuInputDirection_t analog;
		uiDrawObj_t *newPanel = DrawEmptyBox(20,fileListBase-30, getVideoMode()->fbWidth-20, 340);
		DrawAddChild(newPanel, DrawHintLabel(50, fileListBase-18, "Select a DOL    B  Boot normally", 1.0f, ALIGN_LEFT, defaultColor));
		i = MIN(MAX(0,idx-(page/2)),MAX(0,num_files-page));
		max = MIN(num_files, MAX(idx+(page/2),page));
		if(num_files > page)
			DrawAddChild(newPanel, DrawVertScrollBar(getVideoMode()->fbWidth-30, fileListBase, 16, scrollBarHeight, (float)((float)idx/(float)(num_files-1)),scrollBarTabHeight));
		for(j = 0; i<max; ++i,++j) {
			DrawAddChild(newPanel, DrawSelectableButton(50,fileListBase+(j*40), getVideoMode()->fbWidth-35, fileListBase+(j*40)+40, filesToPatch[i].name, (i == idx) ? B_SELECTED:B_NOSELECT));
		}
		container = DrawRepublish(container, newPanel);
		/* The stick repeats on the shared list schedule, like every other list. */
		while(1) {
			buttons = padsButtonsHeld();
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT,
				(buttons & waitButtons) != 0u);
			if((buttons & waitButtons) != 0u || analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
		}
		if((buttons & BUTTON_UP) || analog == UI_MENU_INPUT_UP){	idx = (--idx < 0) ? num_files-1 : idx;}
		if((buttons & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {idx = (idx + 1) % num_files;	}
		if((buttons & BUTTON_A))	break;
		if((buttons & BUTTON_B))	{ idx = -1; break; }
		while((padsButtonsHeld() & waitButtons) != 0u) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT, true);
			VIDEO_WaitVSync();
		}
	}
	DrawDispose(container);
	return idx >= 0 ? &filesToPatch[idx] : NULL;
	
}

void load_app(ExecutableFile *fileToPatch)
{
	uiDrawObj_t* progBox = NULL;
	const char* message = NULL;
	char* gameID = VAR_AREA;
	/* NULL until something is read: a BS2 that can't be read goes to fail
	 * and frees it without ever setting it. */
	void* buffer = NULL;
	u32 sizeToRead;
	int type;
	char* argz = NULL;
	size_t argz_len = 0;
	
	// Get top of memory
	u32 topAddr = getTopAddr();
	if (topAddr < 0x81700000) {
		topAddr = 0x81800000;
	}
	print_debug("Top of RAM simulated as: 0x%08X\n", topAddr);
	
	*(vu32*)(VAR_AREA+0x0028) = 0x01800000;
	*(vu32*)(VAR_AREA+0x002C) = swissSettings.enableUSBGecko ? SYS_CONSOLE_DEVELOPMENT_HW1 : SYS_CONSOLE_RETAIL_HW1;
	*(vu32*)(VAR_AREA+0x002C) += SYS_GetFlipperRevision();
	*(vu32*)(VAR_AREA+0x00CC) = swissSettings.sramVideo;
	*(vu32*)(VAR_AREA+0x00D0) = 0x01000000;
	*(vu32*)(VAR_AREA+0x00E8) = 0x81800000 - topAddr;
	*(vu32*)(VAR_AREA+0x00EC) = topAddr;
	*(vu32*)(VAR_AREA+0x00F0) = 0x01800000;
	*(vu32*)(VAR_AREA+0x00F8) = SYS_GetBusFrequency();
	*(vu32*)(VAR_AREA+0x00FC) = SYS_GetCoreFrequency();
	
	if(devices[DEVICE_CUR]->emulated() & EMU_MEMCARD) {
		*(vu32*)(VAR_AREA+0x30C0) = 
		*(vu32*)(VAR_AREA+0x30C4) = ticks_to_millisecs(gettime()) / 100 + 1;
	}
	else {
		initialize_card(CARD_SLOTA);
		initialize_card(CARD_SLOTB);
		
		*(vu32*)(VAR_AREA+0x30C0) = exi_probe(0);
		*(vu32*)(VAR_AREA+0x30C4) = exi_probe(1);
	}
	
	*(vu16*)(VAR_AREA+0x30E0) = 0x0006;
	*(vu8 *)(VAR_AREA+0x30E3) = (!!swissSettings.disableRecalibration << 6) | (!!swissSettings.disableRumble << 5);
	*(vu16*)(VAR_AREA+0x30E6) = *DVDDeviceCode;
	*(vu8 *)(VAR_AREA+0x30E9) = *PADSpec;
	
	// Copy the game header to 0x80000000
	memcpy(VAR_AREA,(void*)&GCMDisk,0x20);
	
	if(fileToPatch != NULL && fileToPatch->file != NULL) {
		argz = getExternalPath(fileToPatch->file->name);
		argz_len = strlen(argz) + 1;
		
		// For a DOL from a TGC, redirect the FST to the TGC FST.
		if(fileToPatch->tgcBase + fileToPatch->tgcFileStartArea != 0) {
			// Read FST to top of Main Memory (round to 32 byte boundary)
			u32 fstAddr = (topAddr-fileToPatch->fstSize)&~31;
			u32 fstSize = (fileToPatch->fstSize+31)&~31;
			devices[DEVICE_CUR]->seekFile(fileToPatch->file,fileToPatch->fstOffset,DEVICE_HANDLER_SEEK_SET);
			if(devices[DEVICE_CUR]->readFile(fileToPatch->file,(void*)fstAddr,fstSize) != fstSize) {
				message = "Failed to read FST!";
				goto fail_early;
			}
			if(fileToPatch->file->device == &__device_dvd) {
				adjust_tgc_fst((void*)fstAddr, fileToPatch->fstSize, fileToPatch->file->fileBase + fileToPatch->tgcBase, fileToPatch->tgcFileStartArea, fileToPatch->tgcFakeOffset);
				*(vu32*)VAR_TGC_OFFSET = fileToPatch->file->fileBase + fileToPatch->tgcBase;
			}
			else {
				adjust_tgc_fst((void*)fstAddr, fileToPatch->fstSize, fileToPatch->tgcBase, fileToPatch->tgcFileStartArea, fileToPatch->tgcFakeOffset);
				*(vu32*)VAR_TGC_OFFSET = fileToPatch->tgcBase;
			}
			
			// Copy bi2.bin (Disk Header Information) to just under the FST
			u32 bi2Addr = (fstAddr-0x2000)&~31;
			memcpy((void*)bi2Addr,(void*)&GCMDisk+0x440,0x2000);
			
			// Patch bi2.bin
			Patch_GameSpecificFile((void*)bi2Addr, 0x2000, gameID, "bi2.bin");
			
			*(vu32*)(VAR_AREA+0x0020) = 0x0D15EA5E;
			*(vu32*)(VAR_AREA+0x0024) = 1;
			*(vu32*)(VAR_AREA+0x0034) = fstAddr;								// Arena Hi
			*(vu32*)(VAR_AREA+0x0038) = fstAddr;								// FST Location in ram
			*(vu32*)(VAR_AREA+0x003C) = fileToPatch->fstSize;					// FST Max Length
			*(vu32*)(VAR_AREA+0x00F4) = bi2Addr;								// bi2.bin location
		}
		else {
			// Read FST to top of Main Memory (round to 32 byte boundary)
			u32 fstAddr = (topAddr-GCMDisk.MaxFSTSize)&~31;
			u32 fstSize = (fileToPatch->fstSize+31)&~31;
			devices[DEVICE_CUR]->seekFile(fileToPatch->file,fileToPatch->fstOffset,DEVICE_HANDLER_SEEK_SET);
			if(devices[DEVICE_CUR]->readFile(fileToPatch->file,(void*)fstAddr,fstSize) != fstSize) {
				message = "Failed to read FST!";
				goto fail_early;
			}
			
			// Copy bi2.bin (Disk Header Information) to just under the FST
			u32 bi2Addr = (fstAddr-0x2000)&~31;
			memcpy((void*)bi2Addr,(void*)&GCMDisk+0x440,0x2000);
			
			// Patch bi2.bin
			Patch_GameSpecificFile((void*)bi2Addr, 0x2000, gameID, "bi2.bin");
			
			*(vu32*)(VAR_AREA+0x0020) = 0x0D15EA5E;
			*(vu32*)(VAR_AREA+0x0024) = 1;
			*(vu32*)(VAR_AREA+0x0034) = fstAddr;								// Arena Hi
			*(vu32*)(VAR_AREA+0x0038) = fstAddr;								// FST Location in ram
			*(vu32*)(VAR_AREA+0x003C) = GCMDisk.MaxFSTSize;						// FST Max Length
			*(vu32*)(VAR_AREA+0x00F4) = bi2Addr;								// bi2.bin location
		}
		
		if(devices[DEVICE_PATCHES] && devices[DEVICE_PATCHES] != devices[DEVICE_CUR]) {
			sprintf(txtbuffer, "Loading DOL\nDo not remove %s", devices[DEVICE_PATCHES]->deviceName);
			progBox = DrawPublish(DrawProgressBar(true, 0, txtbuffer));
		}
		else {
			progBox = DrawPublish(DrawProgressBar(true, 0, "Loading DOL"));
		}
		
		print_debug("DOL Lives at %08X\n", fileToPatch->offset);
		sizeToRead = (fileToPatch->size + 31) & ~31;
		type = fileToPatch->type;
		print_debug("DOL size %i\n", sizeToRead);
		
		if(fileToPatch->patchFile != NULL) {
			buffer = readFileBlockAligned(fileToPatch->patchFile, 0, sizeToRead);
			if(!buffer) {
				message = "Failed to read patched file!";
				goto fail;
			}
			
			XXH128_hash_t old_hash, new_hash = XXH3_128bits(buffer, sizeToRead);
			devices[DEVICE_PATCHES]->seekFile(fileToPatch->patchFile, -sizeof(old_hash), DEVICE_HANDLER_SEEK_END);
			if(devices[DEVICE_PATCHES]->readFile(fileToPatch->patchFile, &old_hash, sizeof(old_hash)) != sizeof(old_hash) ||
				!XXH128_isEqual(old_hash, new_hash)) {
				devices[DEVICE_PATCHES]->deleteFile(fileToPatch->patchFile);
				sprintf(txtbuffer, "Failed integrity check in patched file!\nPlease test %s for defects.", devices[DEVICE_PATCHES]->deviceName);
				message = txtbuffer;
				goto fail;
			}
		}
		else {
			buffer = readFileBlockAligned(fileToPatch->file, fileToPatch->offset, sizeToRead);
			if(!buffer) {
				message = "Failed to read DOL!";
				goto fail;
			}
			
			fileToPatch->hash = XXH3_64bits(buffer, sizeToRead);
			if(!valid_file_xxh3(&GCMDisk, fileToPatch)) {
				message = "Failed integrity check!";
				goto fail;
			}
			gameID_set(&GCMDisk, fileToPatch->hash);
		}
		
		u8 *oldBuffer = buffer, *newBuffer = NULL;
		if(type == PATCH_DOL_PRS || type == PATCH_OTHER_PRS) {
			int ret = pso_prs_decompress_buf(buffer, &newBuffer, fileToPatch->size);
			if(ret < 0) {
				message = "Failed to decompress DOL!";
				goto fail;
			}
			sizeToRead = ret;
			buffer = newBuffer;
			newBuffer = NULL;
			free(oldBuffer);
			oldBuffer = NULL;
		}
	}
	else {
		if(devices[DEVICE_PATCHES] && devices[DEVICE_PATCHES] != devices[DEVICE_CUR]) {
			sprintf(txtbuffer, "Loading BS2\nDo not remove %s", devices[DEVICE_PATCHES]->deviceName);
			progBox = DrawPublish(DrawProgressBar(true, 0, txtbuffer));

			if(!load_rom_ipl(devices[DEVICE_PATCHES], &buffer, &sizeToRead) &&
				!load_rom_ipl(devices[DEVICE_CUR], &buffer, &sizeToRead) &&
				!load_rom_ipl(&__device_sys, &buffer, &sizeToRead)) {
				message = "Failed to read BS2!";
				goto fail;
			}
		}
		else {
			progBox = DrawPublish(DrawProgressBar(true, 0, "Loading BS2"));

			if(!load_rom_ipl(devices[DEVICE_CUR], &buffer, &sizeToRead) &&
				!load_rom_ipl(&__device_sys, &buffer, &sizeToRead)) {
				message = "Failed to read BS2!";
				goto fail;
			}
		}
		type = PATCH_BS2;

		if(fileToPatch != NULL) {
			fileToPatch->size = sizeToRead;
			fileToPatch->hash = XXH3_64bits(buffer, sizeToRead);
			if(!valid_file_xxh3(&GCMDisk, fileToPatch)) {
				message = "Unknown BS2";
				goto fail;
			}
		}
	}
	
	if(getTopAddr() == topAddr) {
		if(type == PATCH_BS2) setTopAddr(0);
		else setTopAddr(HI_RESERVE);
	}
	if(fileToPatch == NULL || fileToPatch->patchFile == NULL) {
		Patch_ExecutableFile(&buffer, &sizeToRead, gameID, type);
	}
	if(getTopAddr() == 0) {
		setTopAddr(HI_RESERVE);
		*(vu32*)(VAR_AREA+0x0034) = (u32)SYS_GetArenaHi() & ~31;
	}
	
	// See if the combination of our patches has exhausted our play area.
	switch(install_code(0)) {
		case 0:
			break;
		case ENOMEM:
			message = "Exhausted reserved memory.\nAn SD Card Adapter is necessary in order\nfor patches to reserve additional memory.";
			goto fail;
		default:
			message = "Something went wrong.\nPlease open an issue with your hardware\nconfiguration and Swiss settings.";
			goto fail;
	}
	setTopAddr(topAddr);
	
	if(swissSettings.wiirdEngine) {
		if(!kenobi_install_engine()) {
			message = "Cheat engine safety check failed.\nNo code list was installed.";
			goto fail;
		}
	}

	/* The poster pack may share DEVICE_CUR with patches. Unpublish it before
	 * either alias can tear the device down. */
	DrawGameflowCancelPosters();
	if(devices[DEVICE_PATCHES] && !(devices[DEVICE_PATCHES]->quirks & QUIRK_NO_DEINIT)) {
		devices[DEVICE_PATCHES]->deinit(devices[DEVICE_PATCHES]->initial);
	}
	// Don't spin down the drive when running something from it...
	if(!(devices[DEVICE_CUR]->quirks & QUIRK_NO_DEINIT)) {
		devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);
	}
	if(devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) {
		// Check DVD Status, make sure it's error code 0
		print_debug("DVD: %08X\n",dvd_get_error());
	}
	else if(swissSettings.hasFlippyDrive) {
		flippy_bypass(false);
		flippy_reset();
	}
	
	DrawDispose(progBox);
	menuaudio_shutdown();
	DrawShutdown();
	
	print_debug("libogc shutdown and boot game!\n");
	if(devices[DEVICE_CUR] == &__device_sd_a || devices[DEVICE_CUR] == &__device_sd_b || devices[DEVICE_CUR] == &__device_sd_c) {
		s32 exi_channel;
		if(getExiDeviceByLocation(devices[DEVICE_CUR]->location, &exi_channel, NULL)) {
			sdgecko_setPageSize(exi_channel, 512);
			sdgecko_enableCRC(exi_channel, false);
			print_debug("set size\n");
		}
	}
	else if(devices[DEVICE_PATCHES] == &__device_sd_a || devices[DEVICE_PATCHES] == &__device_sd_b || devices[DEVICE_PATCHES] == &__device_sd_c) {
		s32 exi_channel;
		if(getExiDeviceByLocation(devices[DEVICE_PATCHES]->location, &exi_channel, NULL)) {
			sdgecko_setPageSize(exi_channel, 512);
			sdgecko_enableCRC(exi_channel, false);
			print_debug("set size\n");
		}
	}
	if(type == PATCH_BS2) {
		BINtoARAM(buffer, sizeToRead, 0x81300000, 0x812FFFE0);
	}
	else if(type == PATCH_BIN) {
		BINtoARAM(buffer, sizeToRead, 0x80003100, 0x80003100);
	}
	else if(type == PATCH_DOL || type == PATCH_DOL_PRS) {
		DOLtoARAM(buffer, argz, argz_len);
	}
	else if(type == PATCH_ELF) {
		ELFtoARAM(buffer, argz, argz_len);
	}
	SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
	__builtin_unreachable();

fail:
	DrawDispose(progBox);
	free(buffer);
fail_early:
	free(argz);
	if(message) {
		uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_FAIL, message));
		wait_press_A();
		DrawDispose(msgBox);
	}
}

/* Whether a file boot_dol read whole may start: a DOL Swiss knows must match
 * its hash, and sidestep, which copies sections without a length, must find
 * each one the header names inside the file, so a copy cut short is refused.
 * SDLOADER.BIN and branch BINs load raw, as boot_dol loads them. */
static bool boot_image_valid(file_handle *file, void *buffer, XXH64_hash_t hash)
{
	bool raw = file->size >= 4 && memcmp(buffer, ELFMAG, SELFMAG) != 0 &&
		(endsWith(file->name, "/SDLOADER.BIN") || branchResolve(buffer, PATCH_BIN, 0));
	return valid_dol_xxh3(file, hash) && wiiload_image_valid(buffer, file->size, raw);
}

void boot_dol(file_handle* file, int argc, char *argv[])
{
	void *buffer = memalign(32, file->size);
	if(!buffer) {
		uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,"DOL is too big. Press A.");
		DrawPublish(msgBox);
		wait_press_A();
		DrawDispose(msgBox);
		return;
	}
		
	int i=0;
	void *ptr = buffer;
	uiDrawObj_t* progBar = DrawProgressBar(false, 0, "Loading DOL");
	DrawPublish(progBar);
	for(i = 0; i < file->size; i+= 131072) {
		DrawUpdateProgressBar(progBar, (int)((float)((float)i/(float)file->size)*100));
		
		file->device->seekFile(file,i,DEVICE_HANDLER_SEEK_SET);
		int size = i+131072 > file->size ? file->size-i : 131072; 
		if(file->device->readFile(file,ptr,size)!=size) {
			DrawDispose(progBar);
			free(buffer);
			file->device->closeFile(file);
			uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,"Failed to read DOL. Press A.");
			DrawPublish(msgBox);
			wait_press_A();
			DrawDispose(msgBox);
			return;
		}
  		ptr+=size;
	}
	
	XXH64_hash_t hash = XXH3_64bits(buffer, file->size);
	if(!boot_image_valid(file, buffer, hash)) {
		DrawDispose(progBar);
		free(buffer);
		file->device->closeFile(file);
		uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,"DOL is corrupted. Press A.");
		DrawPublish(msgBox);
		wait_press_A();
		DrawDispose(msgBox);
		return;
	}
	gameID_set(NULL, hash);
	DrawDispose(progBar);
	
	if(devices[DEVICE_CONFIG] != NULL && !bootDolSkipRecent) {
		// Update the recent list.
		if(update_recent()) {
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Saving recent list\205"));
			config_update_recent(true);
			DrawDispose(msgBox);
		}
	}

	char fileName[PATHNAME_MAX];
	memset(fileName, 0, PATHNAME_MAX);
	strncpy(fileName, file->name, strrchr(file->name, '.') - file->name);
	print_debug("DOL file name without extension [%s]\n", fileName);

	/* The Source's device, or with none mounted (the Web face starts the
	 * browser without one) the DOL's own. */
	DEVICEHANDLER_INTERFACE *cur = devices[DEVICE_CUR] != NULL ?
		devices[DEVICE_CUR] : file->device;

	// .iso disc image file
	file_handle *imageFile = calloc(1, sizeof(file_handle));
	snprintf(imageFile->name, PATHNAME_MAX, "%s.iso", fileName);

	if(!fnmatch("*/apps/*/*", file->name, FNM_PATHNAME | FNM_CASEFOLD | FNM_LEADING_DIR)) {
		getParentPath(imageFile->name, imageFile->name);
		concat_path(imageFile->name, imageFile->name, "data.iso");
	}
	if(cur->statFile) {
		cur->statFile(imageFile);
	}

	file_handle *bootFile = NULL;
	if(cur == &__device_gcloader) {
		bootFile = calloc(1, sizeof(file_handle));

		if(!gcloaderGetBootFile(bootFile)) {
			free(bootFile);
			bootFile = NULL;
		}
	}

	// Build a command line to pass to the DOL
	char *argz = getExternalPath(imageFile->fileType == IS_FILE ? imageFile->name : file->name);
	size_t argz_len = strlen(argz) + 1;

	// .cli argument file
	file_handle *cliArgFile = calloc(1, sizeof(file_handle));
	snprintf(cliArgFile->name, PATHNAME_MAX, "%s.cli", fileName);
	
	// we found something, use parameters (.cli)
	if(cur->readFile(cliArgFile, NULL, 0) == 0 && cliArgFile->size) {
		print_debug("Argument file found [%s]\n", cliArgFile->name);
		char *cli_buffer = calloc(1, cliArgFile->size + 1);
		if(cli_buffer) {
			cur->seekFile(cliArgFile, 0, DEVICE_HANDLER_SEEK_SET);
			cur->readFile(cliArgFile, cli_buffer, cliArgFile->size);

			// Parse CLI
			char *line, *linectx = NULL;
			line = strtok_r(cli_buffer, "\r\n", &linectx);
			while(line != NULL) {
				argz_add(&argz, &argz_len, line);
				line = strtok_r(NULL, "\r\n", &linectx);
			}

			free(cli_buffer);
		}
	}
	cur->closeFile(cliArgFile);
	free(cliArgFile);

	// .dcp parameter file
	file_handle *dcpArgFile = calloc(1, sizeof(file_handle));
	snprintf(dcpArgFile->name, PATHNAME_MAX, "%s.dcp", fileName);
	
	// we found something, parse and display parameters for selection (.dcp)
	if(cur->readFile(dcpArgFile, NULL, 0) == 0 && dcpArgFile->size) {
		print_debug("Argument file found [%s]\n", dcpArgFile->name);
		char *dcp_buffer = calloc(1, dcpArgFile->size + 1);
		if(dcp_buffer) {
			cur->seekFile(dcpArgFile, 0, DEVICE_HANDLER_SEEK_SET);
			cur->readFile(dcpArgFile, dcp_buffer, dcpArgFile->size);

			// Parse DCP
			parseParameters(dcp_buffer);
			free(dcp_buffer);

			Parameters *params = (Parameters*)getParameters();
			if(params->num_params > 0) {
				DrawArgsSelector(getRelativeName(file->name));
				// Get an argv back or none.
				populateArgz(&argz, &argz_len);
			}
		}
	}
	cur->closeFile(dcpArgFile);
	free(dcpArgFile);

	for(i = 1; i < argc; i++) {
		argz_add(&argz, &argz_len, argv[i]);
	}

	// Boot
	if(cur->location == LOC_DVD_CONNECTOR) {
		cur->setupFile(imageFile, bootFile, NULL, -1);
	}
	if(swissSettings.hasFlippyDrive && needs_flippy_bypass(file, hash)) {
		flippy_bypass(true);
	}
	int sram60Hz = swissSettings.sram60Hz;
	int sramProgressive = swissSettings.sramProgressive;
	if(swissSettings.appsVMode > 0) {
		GXRModeObj *forcedMode = getVideoModeFromSwissSetting(swissSettings.appsVMode);
		DrawVideoMode(forcedMode);
		swissSettings.sram60Hz = getTVFormat() == VI_EURGB60;
		swissSettings.sramProgressive = getScanMode() == VI_PROGRESSIVE;
	}

	// sidestep resets ARAM before handing off; stop AESND first because its
	// GameCube mixer owns ARAM stream buffers.
	bool resetOnLoadFailure = mp3_player_initialized();
	bool restartMenuAudio = menuaudio_shutdown();
	if(!memcmp(buffer, ELFMAG, SELFMAG)) {
		ELFtoARAM(buffer, argz, argz_len);
	}
	else if(endsWith(file->name, "/SDLOADER.BIN")) {
		BINtoARAM(buffer, file->size, 0x81700000, 0x81700000);
	}
	else if(branchResolve(buffer, PATCH_BIN, 0)) {
		BINtoARAM(buffer, file->size, 0x80003100, 0x80003100);
	}
	else {
		DOLtoARAM(buffer, argz, argz_len);
	}
	// libaesnd's MP3Player retains a private voice across calls. If sidestep
	// reset AESND and rejected the image, restart Swiss rather than reuse it.
	if(resetOnLoadFailure) {
		DrawShutdown();
		SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
		__builtin_unreachable();
	}
	free(argz);
	free(buffer);

	cur->closeFile(bootFile);
	cur->closeFile(imageFile);
	free(bootFile);
	free(imageFile);
	/* It didn't start: go back to the menu's mode, as a failed game launch
	 * does, and to the SRAM flags the next game's Auto video mode reads. */
	if(swissSettings.appsVMode > 0) {
		swissSettings.sram60Hz = sram60Hz;
		swissSettings.sramProgressive = sramProgressive;
		GXRModeObj *menuMode = getVideoModeFromSwissSetting(swissSettings.uiVMode);
		if(menuMode != getVideoMode())
			DrawVideoMode(menuMode);
	}
	// Successful boots never return. Restore audio only if it was already live.
	if(restartMenuAudio) menuaudio_init();
}

/* The name a copy of src lands under in dir on destDev: its own name less
 * what FAT can't hold, and a memory card's save as a .gci anywhere else.
 * The File Browser predicts the landing with it, so the name it shows is
 * the name the copy writes. */
static void manageDestName(char *out, const char *dir, const char *src,
	DEVICEHANDLER_INTERFACE *srcDev, DEVICEHANDLER_INTERFACE *destDev)
{
	bool isSrcCard = srcDev == &__device_card_a || srcDev == &__device_card_b;
	bool isDestCard = destDev == &__device_card_a || destDev == &__device_card_b;

	concat_path(out, dir, stripInvalidChars(getRelativeName((char *)src)));
	// Create a GCI if something is coming out from CARD to another device
	if(isSrcCard && !isDestCard) {
		strlcat(out, ".gci", PATHNAME_MAX);
	}
}

/* Whether a file of that name is on the destination: its statFile, or, for
 * a device without one (FlippyDrive Flash), opening it, as the copy's own
 * check does. */
static bool manageDestExists(file_handle *file)
{
	DEVICEHANDLER_INTERFACE *dest = devices[DEVICE_DEST];

	if(dest->statFile != NULL) {
		return dest->statFile(file) == 0;
	}
	if(dest->readFile(file, NULL, 0) != 0) {
		return false;
	}
	dest->closeFile(file);
	return true;
}

/* A copy that stopped or failed leaves nothing half written behind: the
 * destination is closed (closing twice is harmless) and deleted. False when
 * it couldn't be, so the message says part of it is left. Two things are
 * never deleted: a folder of that name, which the copy couldn't open, and
 * anything on a memory card, whose delete goes by the save's own name and
 * whose writes can report a short count for a save that is complete; a
 * card keeps what it has, as Swiss's copy did. */
static bool manageDropPartial(file_handle *destFile)
{
	static file_handle there;
	DEVICEHANDLER_INTERFACE *dest = devices[DEVICE_DEST];

	dest->closeFile(destFile);
	if(dest == &__device_card_a || dest == &__device_card_b) {
		return false;
	}
	memset(&there, 0, sizeof(there));
	strlcpy(there.name, destFile->name, sizeof(there.name));
	there.fileType = IS_FILE;
	if(dest->statFile != NULL && dest->statFile(&there) == 0 && there.fileType == IS_DIR) {
		return true;
	}
	return dest->deleteFile != NULL && dest->deleteFile(destFile) == 0;
}

/* One of manage_file's results: Swiss's box (type and text, then A), or
 * in the File Browser its message (title and detail; failed waits for A),
 * said once the panes are read again. */
static void manageTell(int type, const char *text, const char *title, const char *detail,
	bool failed)
{
	if(filesBoxes) {
		filesSayLater(title, detail, failed);
		return;
	}
	uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(type, text));
	wait_press_A();
	DrawDispose(msgBox);
}

/* How a copy or a move ended: Swiss's box (type and text, then A), or in
 * the File Browser its message, from result. */
static void manageCopied(uiFilesResult_t result, int option, const char *dest, int code,
	bool removed, bool replaced, int type, const char *text)
{
	if(filesBoxes) {
		filesSayCopy(result, option == MOVE_OPTION, dest, code, removed, replaced);
		return;
	}
	uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(type, text));
	wait_press_A();
	DrawDispose(msgBox);
}

/* Manage file  - The user will be asked what they want to do with the currently selected file - copy/move/delete*/
bool manage_file() {
	return manage_file_ex(MANAGE_ASK, NULL);
}

/* manage_file with the File Browser's choice already made: option is one of
 * enum fileOptions, or MANAGE_ASK for Swiss's box. For Copy and Move,
 * destDir is a folder on devices[DEVICE_DEST], which the caller has set and
 * mounted, so Swiss's destination picker, its mount and its folder chooser
 * are skipped. Everything after them is Swiss's. */
bool manage_file_ex(int preset, const char *destDir) {
	bool isFile = curFile.fileType == IS_FILE;
	bool isHidden = curFile.fileAttrib & ATTRIB_HIDDEN;
	bool canWrite = devices[DEVICE_CUR]->features & FEAT_WRITE;
	bool canMove = canWrite && isFile;
	bool canCopy = isFile;
	bool canDelete = canWrite && devices[DEVICE_CUR]->deleteFile;
	bool canRename = canWrite && devices[DEVICE_CUR]->renameFile;
	bool canHide = canWrite && devices[DEVICE_CUR]->hideFile;
	/* The Apps and Emulators faces look in /apps and /emulators again
	 * after any change here. */
	homeAppsKnown = false;
	homeEmulatorsKnown = false;
	
	int option = preset;
	if(preset == MANAGE_ASK) {
		// Ask the user what they want to do with the selected entry
		uiDrawObj_t* manageFileBox = DrawEmptyBox(10,150, getVideoMode()->fbWidth-10, 320);
		sprintf(txtbuffer, "Manage %s:", isFile ? "File" : "Directory");
		DrawAddChild(manageFileBox, DrawStyledLabel(640/2, 160, txtbuffer, 1.0f, ALIGN_CENTER, defaultColor));
		float scale = GetTextScaleToFitInWidth(getRelativeName(curFile.name), getVideoMode()->fbWidth-10-10);
		DrawAddChild(manageFileBox, DrawStyledLabel(640/2, 190, getRelativeName(curFile.name), scale, ALIGN_CENTER, defaultColor));
		sprintf(txtbuffer, "%s%s%s%s%s",
						canCopy ? "X  Copy    " : "",
						canMove ? "Y  Move    " : "",
						canDelete ? "Z  Delete    " : "",
						canRename ? "R  Rename    " : "",
						canHide ? isHidden ? "L  Unhide" : "L  Hide" : "");
		DrawAddChild(manageFileBox, DrawHintLabel(640/2, 250, txtbuffer, GetHintScaleToFitInWidthWithMax(txtbuffer, getVideoMode()->fbWidth-10-10, 1.0f), ALIGN_CENTER, defaultColor));
		DrawAddChild(manageFileBox, DrawHintLabel(640/2, 310, "B  Return", 1.0f, ALIGN_CENTER, defaultColor));
		DrawPublish(manageFileBox);
		u32 waitButtons = BUTTON_X|BUTTON_Y|BUTTON_B|BUTTON_Z|BUTTON_R|BUTTON_L;
		do {VIDEO_WaitVSync();} while (padsButtonsHeld() & waitButtons);
		option = 0;
		while(1) {
			u32 buttons = padsButtonsHeld();
			if(canCopy && (buttons & BUTTON_X)) {
				option = COPY_OPTION;
				while(padsButtonsHeld() & BUTTON_X){ VIDEO_WaitVSync (); }
				break;
			}
			if(canMove && (buttons & BUTTON_Y)) {
				option = MOVE_OPTION;
				while(padsButtonsHeld() & BUTTON_Y){ VIDEO_WaitVSync (); }
				break;
			}
			if(canDelete && (buttons & BUTTON_Z)) {
				option = DELETE_OPTION;
				while(padsButtonsHeld() & BUTTON_Z){ VIDEO_WaitVSync (); }
				break;
			}
			if(canRename && (buttons & BUTTON_R)) {
				option = RENAME_OPTION;
				while(padsButtonsHeld() & BUTTON_R){ VIDEO_WaitVSync (); }
				break;
			}
			if(canRename && (buttons & BUTTON_L)) {
				option = HIDE_OPTION;
				while(padsButtonsHeld() & BUTTON_L){ VIDEO_WaitVSync (); }
				break;
			}
			if(buttons & BUTTON_B) {
				DrawDispose(manageFileBox);
				return false;
			}
		}
		do {VIDEO_WaitVSync();} while (padsButtonsHeld() & waitButtons);
		DrawDispose(manageFileBox);
	}
	
	if(option == MOVE_OPTION && !confirmAction(
		"Move this file?\nIt is removed from here once copied.\nA  MOVE    B  CANCEL")) {
		return false;
	}
	if(option == HIDE_OPTION && canHide && !isHidden && !confirmAction(isFile ?
		"Hide this file?\nIt shows only with Show hidden files on.\nA  HIDE    B  CANCEL" :
		"Hide this folder?\nIt shows only with Show hidden files on.\nA  HIDE    B  CANCEL")) {
		return false;
	}
	if(option == DELETE_OPTION && filesBoxes && !filesAskDelete(isFile ?
			"Delete this file?\n \nPress L + A to continue, or B to cancel." :
			"Delete this folder and all it holds?\n \nPress L + A to continue, or B to cancel.")) {
		return false;
	}
	// "Are you sure option" for deletes.
	if(option == DELETE_OPTION && !filesBoxes) {
		uiDrawObj_t *msgBox = DrawMessageBox(D_WARN, isFile ?
			"Delete this file?\n \nPress L + A to continue, or B to cancel." :
			"Delete this folder and all it holds?\n \nPress L + A to continue, or B to cancel.");
		DrawPublish(msgBox);
		bool cancel = false;
		while(1) {
			u32 btns = padsButtonsHeld();
			if ((btns & (BUTTON_A|BUTTON_L)) == (BUTTON_A|BUTTON_L)) {
				break;
			}
			else if (btns & BUTTON_B) {
				cancel = true;
				break;
			}
			VIDEO_WaitVSync();
		}
		do {VIDEO_WaitVSync();} while (padsButtonsHeld() & (BUTTON_A|BUTTON_L|BUTTON_B));
		DrawDispose(msgBox);
		if(cancel) {
			return false;
		}
	}

	// Handles (un)hiding directories or files on FAT FS devices.
	if (canHide && option == HIDE_OPTION) {
		devices[DEVICE_CUR]->hideFile(&curFile, !isHidden);
	}
	// Handles renaming directories or files on FAT FS devices.
	else if(canRename && option == RENAME_OPTION) {
		char *nameBuffer = calloc(1, sizeof(curFile.name));
		char *parentPath = calloc(1, sizeof(curFile.name));
		getParentPath(&curFile.name[0], parentPath);
		strcpy(nameBuffer, getRelativeName(&curFile.name[0]));
		DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA|ENTRYMODE_FILE, "Rename", nameBuffer, sizeof(curFile.name)-(strlen(parentPath)+1));
		concat_path(txtbuffer, parentPath, nameBuffer);
		bool modified = (strcmp(&curFile.name[0], txtbuffer) != 0) && strlen(nameBuffer) > 0;
		if(modified) {
			print_debug("Renaming %s to %s\n", &curFile.name[0], txtbuffer);
			u32 ret = devices[DEVICE_CUR]->renameFile(&curFile, txtbuffer);
			sprintf(txtbuffer, "%s renamed!\nPress A to continue.", isFile ? "File" : "Directory");
			manageTell(D_INFO, ret ? "Move Failed! Press A to continue" : txtbuffer,
				ret ? "Couldn't rename it." : "Renamed.", "", ret != 0);
		}
		free(nameBuffer);
		free(parentPath);
		do {VIDEO_WaitVSync();} while (padsButtonsHeld() & (BUTTON_B|BUTTON_START));
		return modified;
	}
	// Handle deletes (dir or file)
	else if(canDelete && option == DELETE_OPTION) {
		uiDrawObj_t *progBar = DrawPublish(DrawProgressBar(true, 0, "Deleting\205"));
		bool deleted = deleteFileOrDir(&curFile);
		DrawDispose(progBar);
		sprintf(txtbuffer, "%s %s\nPress A to continue.", isFile ? "File" : "Directory", deleted ? "deleted successfully" : "failed to delete!");
		manageTell(deleted ? D_INFO : D_FAIL, txtbuffer, !deleted ? "Couldn't delete it." :
			isFile ? "The file was deleted." : "The folder was deleted.", "", !deleted);
		return deleted;
	}
	// If copy, ask which device is the destination device and copy
	else if((option == COPY_OPTION) || (option == MOVE_OPTION)) {
		u32 ret = 0;
		/* One handle, kept: every way out (there are many) leaves nothing to free. */
		static file_handle destStore;
		file_handle *destFile = NULL;
		bool replaced = false;	/* Replace it took the file there off */
		if(destDir == NULL) {
			// Show a list of destination devices (the same device is also a possibility)
			select_device(DEVICE_DEST);
			if(devices[DEVICE_DEST] == NULL) return false;

			// If the devices are not the same, init the destination, fail on non-existing device/etc
			if(devices[DEVICE_DEST] != devices[DEVICE_CUR] && devices[DEVICE_DEST] != manageKeep) {
				devices[DEVICE_DEST]->deinit( devices[DEVICE_DEST]->initial );
				deviceHandler_setStatEnabled(0);
				if(devices[DEVICE_DEST]->init( devices[DEVICE_DEST]->initial )) {
					deviceHandler_setStatEnabled(1);
					sprintf(txtbuffer, "Failed to init destination device! (%u)\nPress A to continue.",ret);
					uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,txtbuffer);
					DrawPublish(msgBox);
					wait_press_A();
					DrawDispose(msgBox);
					return false;
				}
				deviceHandler_setStatEnabled(1);
			}
			// Traverse this destination device and let the user select a directory to dump the file in
			destFile = memset(&destStore, 0, sizeof(destStore));

			// Show a directory only browser and get the destination file location
			ret = select_dest_dir(devices[DEVICE_DEST]->initial, destFile->name);
			if(ret) {
				if(devices[DEVICE_DEST] != devices[DEVICE_CUR] && devices[DEVICE_DEST] != manageKeep) {
					devices[DEVICE_DEST]->deinit( devices[DEVICE_DEST]->initial );
				}
				devices[DEVICE_DEST] = NULL;
				return false;
			}
		}
		else {
			destFile = memset(&destStore, 0, sizeof(destStore));
			strlcpy(destFile->name, destDir, PATHNAME_MAX);
		}
		
		u32 isDestCard = devices[DEVICE_DEST] == &__device_card_a || devices[DEVICE_DEST] == &__device_card_b;
		u32 isSrcCard = devices[DEVICE_CUR] == &__device_card_a || devices[DEVICE_CUR] == &__device_card_b;
		
		manageDestName(destFile->name, destFile->name, curFile.name, devices[DEVICE_CUR],
			devices[DEVICE_DEST]);

		// If the destination file already exists, ask the user what to do
		/* One there that can't be read is there all the same: without the
		 * question the copy would write over it, or delete it on a failed
		 * write. */
		if(manageDestExists(destFile)) {
			/* The File Browser asks with its own box: Keep both is A, Replace it
			 * Z and Cancel B, as if pressed in Swiss's. */
			u32 chosen = filesBoxes ? filesAskExists(destFile->name) : 0;
			uiDrawObj_t* dupeBox = NULL;
			if(!chosen) {
				dupeBox = DrawEmptyBox(10,150, getVideoMode()->fbWidth-10, 350);
				DrawAddChild(dupeBox, DrawStyledLabel(640/2, 160, "File exists:", 1.0f, ALIGN_CENTER, defaultColor));
				float scale = GetTextScaleToFitInWidth(getRelativeName(curFile.name), getVideoMode()->fbWidth-10-10);
				DrawAddChild(dupeBox, DrawStyledLabel(640/2, 200, getRelativeName(curFile.name), scale, ALIGN_CENTER, defaultColor));
				DrawAddChild(dupeBox, DrawHintLabel(640/2, 230, "A  Rename    Z  Overwrite", 1.0f, ALIGN_CENTER, defaultColor));
				DrawAddChild(dupeBox, DrawHintLabel(640/2, 300, "B  Return", 1.0f, ALIGN_CENTER, defaultColor));
				DrawPublish(dupeBox);
			}
			while(padsButtonsHeld() & (BUTTON_A | BUTTON_Z)) { VIDEO_WaitVSync (); }
			while(1) {
				u32 buttons = chosen ? chosen : padsButtonsHeld();
				if(buttons & BUTTON_Z) {
					/* FAT ignores case: one file may be spelled two ways. */
					if(!strcasecmp(curFile.name, destFile->name)) {
						DrawDispose(dupeBox);
						manageTell(D_INFO, "Can't overwrite a file with itself!", "That's the same file.",
							"", true);
						return false; 
					}
					/* Replacing: the file there goes first. One that
					 * won't go is kept, and nothing is copied. */
					else if(devices[DEVICE_DEST]->deleteFile) {
						if(devices[DEVICE_DEST]->deleteFile(destFile) != 0) {
							DrawDispose(dupeBox);
							manageTell(D_FAIL, "Failed to delete the existing file!\nPress A to continue.",
								"Couldn't replace it.", "The file there couldn't be removed.", true);
							return false;
						}
						replaced = true;
					}

					while(padsButtonsHeld() & BUTTON_Z){ VIDEO_WaitVSync (); }
					break;
				}
				if(buttons & BUTTON_A) {
					int cursor, extension_start = -1, copy_num = 0;
					char name_backup[1024];
					for(cursor = 0; destFile->name[cursor]; cursor++) {
						if(destFile->name[cursor] == '.' && destFile->name[cursor - 1] != '/'
							&& cursor > 0)
							extension_start = cursor;
						if(destFile->name[cursor] == '/')
							extension_start = -1;
						name_backup[cursor] = destFile->name[cursor];
					}
					name_backup[cursor] = 0;

					devices[DEVICE_DEST]->closeFile(destFile);

					if(extension_start >= 0) {
						destFile->name[extension_start] = 0;
						cursor = extension_start;
					}

					if(destFile->name[cursor - 3] == '_' && in_range(destFile->name[cursor - 2], '0', '9')
							&& in_range(destFile->name[cursor - 1], '0', '9')) {
						copy_num = (int) strtol(destFile->name + cursor - 2, 0, 10);
					}
					else {
						cursor += 3;
						if((strlen(name_backup) + 4) >= 1024) {
							DrawDispose(dupeBox);
							manageTell(D_INFO, "File name too long!", "Its name is too long for another copy.",
								"", true);
							return false;
						}
						destFile->name[cursor - 3] = '_';
						sprintf(destFile->name + cursor - 2, "%02i", copy_num);
					}

					if(extension_start >= 0) {
						strcpy(destFile->name + cursor, name_backup + extension_start);
					}

					while(manageDestExists(destFile)) {
						copy_num++;
						if(copy_num > 99) {
							DrawDispose(dupeBox);
							manageTell(D_INFO, "Too many copies!", "There are 99 copies of it there already.",
								"", true);
							return false;
						}
						sprintf(destFile->name + cursor - 2, "%02i", copy_num);
						if(extension_start >= 0) {
							strcpy(destFile->name + cursor, name_backup + extension_start);
						}
					}

					while(padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
					break;
				}
				if(buttons & BUTTON_B) {
					DrawDispose(dupeBox);
					return false;
				}
			}
			DrawDispose(dupeBox);
		}

		strlcpy(manageLanded, destFile->name, sizeof(manageLanded));
		// Seek back to 0 after all these reads
		devices[DEVICE_CUR]->seekFile(&curFile, 0, DEVICE_HANDLER_SEEK_SET);
		devices[DEVICE_DEST]->seekFile(destFile, 0, DEVICE_HANDLER_SEEK_SET);
		
		// Same (fat based) device and user wants to move the file, just rename ;)
		if(devices[DEVICE_CUR] == devices[DEVICE_DEST]
			&& canRename && option == MOVE_OPTION) {
			ret = devices[DEVICE_CUR]->renameFile(&curFile, destFile->name);
			needsRefresh=1;
			/* Replace it took the file there off first: say so when the
			 * move then fails. */
			manageTell(D_INFO, ret ? "Move Failed! Press A to continue":"File moved! Press A to continue",
				ret ? "Couldn't move it." : "Finished moving.",
				ret && replaced ? "The file that was there is gone already." : "", ret != 0);
		}
		else {
			// If we're copying out from memory card, make a .GCI
			if(isSrcCard) {
				setCopyGCIMode(TRUE);
				curFile.size += sizeof(GCI);
			}
			// If we're copying a .gci to a memory card, do it properly
			else if(isDestCard && (endsWith(curFile.name,".gci") || endsWith(curFile.name,".gcs") || endsWith(curFile.name,".sav"))) {
				GCI gci;
				devices[DEVICE_CUR]->seekFile(&curFile, 0, DEVICE_HANDLER_SEEK_SET);
				if(devices[DEVICE_CUR]->readFile(&curFile, &gci, sizeof(GCI)) == sizeof(GCI)) {
					if(!memcmp(&gci, "DATELGC_SAVE", 12)) {
						devices[DEVICE_CUR]->seekFile(&curFile, 0x80, DEVICE_HANDLER_SEEK_SET);
						devices[DEVICE_CUR]->readFile(&curFile, &gci, sizeof(GCI));
						#pragma GCC diagnostic push
						#pragma GCC diagnostic ignored "-Wrestrict"
						swab(&gci.reserved01, &gci.reserved01, 2);
						swab(&gci.icon_addr,  &gci.icon_addr, 20);
						#pragma GCC diagnostic pop
					}
					else if(!memcmp(&gci, "GCSAVE", 6)) {
						devices[DEVICE_CUR]->seekFile(&curFile, 0x110, DEVICE_HANDLER_SEEK_SET);
						devices[DEVICE_CUR]->readFile(&curFile, &gci, sizeof(GCI));
					}
					if(curFile.size - curFile.offset == gci.filesize8 * 8192) setGCIInfo(&gci);
					else devices[DEVICE_CUR]->seekFile(&curFile, -sizeof(GCI), DEVICE_HANDLER_SEEK_CUR);
				}
			}
			
			// Read from one file and write to the new directory
			u32 bulkWrite = isSrcCard || isDestCard || devices[DEVICE_DEST] == &__device_qoob;
			u32 curOffset = curFile.offset, cancelled = 0, written = 0, chunkSize = bulkWrite ? curFile.size - curOffset : (256*1024);
			char *readBuffer = (char*)memalign(32,chunkSize);
			sprintf(txtbuffer, "Copying to: %s",getRelativeName(destFile->name));
			uiDrawObj_t* progBar = filesBoxes ? filesProgress(option, destFile->name) :
				DrawProgressBar(false, 0, txtbuffer);
			DrawPublish(progBar);
			
			u64 startTime = SYS_GetSystemTime();
			u64 lastTime = SYS_GetSystemTime();
			u32 lastOffset = 0;
			int speed = 0;
			int timeremain = 0;
			print_debug("Copying %i byte file from %s to %s\n", curFile.size, &curFile.name[0], destFile->name);
			/* A B pressed before this (to leave another screen) isn't a cancel. */
			(void)padsButtonsTaken(BUTTON_B);
			while(curOffset < curFile.size) {
				/* Taken from the scans as well as held: a B tapped while a
				 * chunk was read or written still cancels. */
				u32 buttons = padsButtonsHeld() | padsButtonsTaken(BUTTON_B);
				if(buttons & BUTTON_B) {
					cancelled = 1;
					break;
				}
				u32 timeDiff = diff_msec(lastTime, SYS_GetSystemTime());
				u32 timeStart = diff_msec(startTime, SYS_GetSystemTime());
				if(timeDiff >= 1000) {
					speed = (int)((float)(curOffset-lastOffset) / (float)(timeDiff/1000.0f));
					timeremain = (curFile.size - curOffset) / speed;
					lastTime = SYS_GetSystemTime();
					lastOffset = curOffset;
				}
				DrawUpdateProgressBarDetail(progBar, (int)((float)((float)curOffset/(float)curFile.size)*100), speed, timeStart/1000, timeremain);
				u32 amountToCopy = curOffset + chunkSize > curFile.size ? curFile.size - curOffset : chunkSize;
				devices[DEVICE_CUR]->seekFile(&curFile, curOffset, DEVICE_HANDLER_SEEK_SET);
				ret = devices[DEVICE_CUR]->readFile(&curFile, readBuffer, amountToCopy);
				if(ret != amountToCopy) {	// Retry the read.
					devices[DEVICE_CUR]->seekFile(&curFile, curOffset, DEVICE_HANDLER_SEEK_SET);
					ret = devices[DEVICE_CUR]->readFile(&curFile, readBuffer, amountToCopy);
					if(ret != amountToCopy) {
						DrawDispose(progBar);
						free(readBuffer);
						devices[DEVICE_CUR]->closeFile(&curFile);
						devices[DEVICE_DEST]->closeFile(destFile);
						/* Before its first write the copy made no file, so none is left. */
						bool removed = !written || manageDropPartial(destFile);
						sprintf(txtbuffer, "Failed to Read! (%d %d)\n%s",amountToCopy,ret, &curFile.name[0]);
						manageCopied(UI_FILES_RESULT_READ_FAILED, option, destFile->name, ret, removed,
							replaced, D_FAIL, txtbuffer);
						setGCIInfo(NULL);
						setCopyGCIMode(FALSE);
						return true;
					}
				}
				written = 1;
				ret = devices[DEVICE_DEST]->writeFile(destFile, readBuffer, amountToCopy);
				if(ret != amountToCopy) {
					DrawDispose(progBar);
					free(readBuffer);
					devices[DEVICE_CUR]->closeFile(&curFile);
					devices[DEVICE_DEST]->closeFile(destFile);
					bool removed = manageDropPartial(destFile);
					sprintf(txtbuffer, "Failed to Write! (%d %d)\n%s",amountToCopy,ret,destFile->name);
					manageCopied(UI_FILES_RESULT_WRITE_FAILED, option, destFile->name, ret, removed,
						replaced, D_FAIL, txtbuffer);
					setGCIInfo(NULL);
					setCopyGCIMode(FALSE);
					return true;
				}
				curOffset+=amountToCopy;
			}
			DrawDispose(progBar);
			free(readBuffer);
			devices[DEVICE_CUR]->closeFile(&curFile);

			ret = devices[DEVICE_DEST]->writeFile(destFile, NULL, 0);
			if(ret == 0)
				ret = devices[DEVICE_DEST]->closeFile(destFile);
			if(ret != 0) {
				bool removed = manageDropPartial(destFile);
				sprintf(txtbuffer, "Failed to Write! (%d)\n%s",ret,destFile->name);
				manageCopied(UI_FILES_RESULT_WRITE_FAILED, option, destFile->name, ret, removed,
					replaced, D_FAIL, txtbuffer);
				setGCIInfo(NULL);
				setCopyGCIMode(FALSE);
				return true;
			}
			setGCIInfo(NULL);
			setCopyGCIMode(FALSE);
			/* B tapped while the last piece was read or written, or the file
			 * closed: the loop ended before it could see it. */
			if((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) {
				cancelled = 1;
			}
			bool removed = cancelled && manageDropPartial(destFile);
			bool kept = false;
			uiFilesResult_t result = UI_FILES_RESULT_DONE;
			const char *message;
			if(!cancelled) {
				// If cut, delete from source device
				if(canDelete && option == MOVE_OPTION) {
					kept = devices[DEVICE_CUR]->deleteFile(&curFile) != 0;
					needsRefresh=1;
					message = "Move Complete!";
					result = kept ? UI_FILES_RESULT_KEPT : UI_FILES_RESULT_DONE;
				}
				else {
					message = "Copy Complete.\nPress A to continue";
				}
			} 
			else {
				sprintf(txtbuffer, "%s cancelled.\nPress A to continue", (option == MOVE_OPTION) ? "Move" : "Copy");
				message = txtbuffer;
				result = UI_FILES_RESULT_STOPPED;
			}
			/* A Move that couldn't take the original off says it copied. */
			manageCopied(result, canDelete || cancelled ? option : COPY_OPTION, destFile->name, 0,
				removed, replaced, D_INFO, message);
		}
	}

	return true;
}

void verify_game()
{
	u32 crc = 0;
	u32 curOffset = 0, cancelled = 0, chunkSize = (512*1024);
	
	if(devices[DEVICE_CUR]->location == LOC_DVD_CONNECTOR) {
		devices[DEVICE_CUR]->setupFile(&curFile, NULL, NULL, -1);
		if(swissSettings.audioStreaming) {
			AUDIO_SetStreamVolLeft(0);
			AUDIO_SetStreamVolRight(0);
			AUDIO_SetStreamPlayState(AI_STREAM_START);
			DVD_PrepareStreamAbs(&DVDCommandBlock, curFile.size & ~(32*1024-1), 0);
			DVD_StopStreamAtEnd(&DVDCommandBlock);
		}
	}
	
	unsigned char *readBuffer = (unsigned char*)memalign(32,chunkSize);
	uiDrawObj_t* progBar = DrawProgressBar(false, 0, "Verifying\205");
	DrawPublish(progBar);
	
	u64 startTime = SYS_GetSystemTime();
	u64 lastTime = SYS_GetSystemTime();
	u32 lastOffset = 0;
	int speed = 0;
	int timeremain = 0;
	/* A B pressed before this (to leave another screen) isn't a cancel. */
	(void)padsButtonsTaken(BUTTON_B);
	while(curOffset < curFile.size) {
		/* Taken from the scans as well as held: a B tapped while a chunk
		 * was read still cancels. */
		u32 buttons = padsButtonsHeld() | padsButtonsTaken(BUTTON_B);
		if(buttons & BUTTON_B) {
			cancelled = 1;
			break;
		}
		u32 timeDiff = diff_msec(lastTime, SYS_GetSystemTime());
		u32 timeStart = diff_msec(startTime, SYS_GetSystemTime());
		if(timeDiff >= 1000) {
			speed = (int)((float)(curOffset-lastOffset) / (float)(timeDiff/1000.0f));
			timeremain = (curFile.size - curOffset) / speed;
			lastTime = SYS_GetSystemTime();
			lastOffset = curOffset;
		}
		DrawUpdateProgressBarDetail(progBar, (int)((float)((float)curOffset/(float)curFile.size)*100), speed, timeStart/1000, timeremain);
		u32 amountToRead = curOffset + chunkSize > curFile.size ? curFile.size - curOffset : chunkSize;
		devices[DEVICE_CUR]->seekFile(&curFile, curOffset, DEVICE_HANDLER_SEEK_SET);
		u32 ret = devices[DEVICE_CUR]->readFile(&curFile, readBuffer, amountToRead);
		if(ret != amountToRead) {
			DrawDispose(progBar);
			free(readBuffer);
			sprintf(txtbuffer, "Failed to Read! (%d %d)\n%s",amountToRead,ret, &curFile.name[0]);
			uiDrawObj_t *msgBox = DrawMessageBox(D_FAIL,txtbuffer);
			DrawPublish(msgBox);
			wait_press_A();
			DrawDispose(msgBox);
			goto fail;
		}
		crc = crc32(crc,readBuffer,amountToRead);
		curOffset+=amountToRead;
	}
	DrawDispose(progBar);
	free(readBuffer);
	if(!cancelled) {
		uiDrawObj_t *msgBox = NULL;
		if(valid_gcm_crc32(&GCMDisk, crc)) {
			msgBox = DrawMessageBox(D_PASS,"Passed verification!\nPress A to continue.");
		}
		else {
			msgBox = DrawMessageBox(D_FAIL,"Failed verification!\nPress A to continue.");
		}
		DrawPublish(msgBox);
		wait_press_A();
		DrawDispose(msgBox);
	}

fail:
	if(devices[DEVICE_CUR]->location == LOC_DVD_CONNECTOR) {
		if(swissSettings.audioStreaming) {
			DVD_CancelStream(&DVDCommandBlock);
			AUDIO_SetStreamPlayState(AI_STREAM_STOP);
		}
		devices[DEVICE_CUR]->closeFile(&curFile);
	}
}

static void load_game_with_context(gameflowLaunchContext_t *context) {
	file_handle *disc2File = context != NULL ?
		context->oppositeDisc : meta_find_disc2(&curFile);
	
	if(devices[DEVICE_CUR] == &__device_wode) {
		uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Setup base offset please Wait\205"));
		devices[DEVICE_CUR]->setupFile(&curFile, disc2File, NULL, -1);
		DrawDispose(msgBox);
		repopulate_meta(&curFile);
	}
	uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Reading\205"));
	
	// boot the GCM/ISO file, gamecube disc or multigame selected entry
	memset(&tgcFile, 0, sizeof(TGCHeader));
	if(endsWith(curFile.name,".tgc")) {
		devices[DEVICE_CUR]->seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
		if(devices[DEVICE_CUR]->readFile(&curFile,&tgcFile,sizeof(TGCHeader)) != sizeof(TGCHeader) || tgcFile.magic != TGC_MAGIC) {
			msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Invalid or Corrupt File!"));
			sleep(2);
			DrawDispose(msgBox);
			goto exit;
		}
		
		devices[DEVICE_CUR]->seekFile(&curFile,tgcFile.headerStart,DEVICE_HANDLER_SEEK_SET);
		if(devices[DEVICE_CUR]->readFile(&curFile,&GCMDisk,sizeof(DiskHeader)) != sizeof(DiskHeader) || !valid_gcm_magic(&GCMDisk)) {
			msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Invalid or Corrupt File!"));
			sleep(2);
			DrawDispose(msgBox);
			goto exit;
		}
		
		swissSettings.audioStreaming = is_streaming_disc(&GCMDisk);
	}
	else {
		devices[DEVICE_CUR]->seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
		if(devices[DEVICE_CUR]->readFile(&curFile,&GCMDisk,sizeof(DiskHeader)) != sizeof(DiskHeader) || !valid_gcm_magic(&GCMDisk)) {
			if(!GCMDisk.ConsoleID && !memcmp(&GCMDisk.ConsoleID, &GCMDisk.GamecodeA, sizeof(DiskHeader) - 1)) {
				devices[DEVICE_CUR]->seekFile(&curFile,0x8000,DEVICE_HANDLER_SEEK_SET);
				if(devices[DEVICE_CUR]->readFile(&curFile,&GCMDisk,sizeof(DiskHeader)) == sizeof(DiskHeader) &&
					!GCMDisk.ConsoleID && !memcmp(&GCMDisk.ConsoleID, &GCMDisk.GamecodeA, sizeof(DiskHeader) - 1)) {
					msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Invalid or Corrupt File! (Fake SD Card?)"));
					sleep(2);
					DrawDispose(msgBox);
					goto exit;
				}
			}
			msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Invalid or Corrupt File!"));
			sleep(2);
			DrawDispose(msgBox);
			goto exit;
		}
		
		swissSettings.audioStreaming = is_streaming_disc(&GCMDisk);
		
		if(needs_nkit_reencode(&GCMDisk, curFile.size)) {
			msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Please reconvert to NKit.iso using\nNKit bundled with this Swiss release."));
			sleep(5);
			DrawDispose(msgBox);
			goto exit;
		}
		else if((is_artx_disc(&GCMDisk) || !valid_gcm_boot(&GCMDisk)) && is_nkit_format(&GCMDisk)) {
			msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "File is not playable in NKit.iso format.\nPlease convert back to ISO using NKit."));
			sleep(5);
			DrawDispose(msgBox);
			goto exit;
		}
		else if(is_redump_disc(curFile.meta) && !valid_gcm_size(&GCMDisk, curFile.size)) {
			if(swissSettings.audioStreaming && !valid_gcm_size2(&GCMDisk, curFile.size)) {
				msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "File is a bad dump and is not playable.\nPlease attempt recovery using NKit."));
				sleep(5);
				DrawDispose(msgBox);
				goto exit;
			}
			else if(!is_recent_entry(curFile.name)) {
				msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "File is a bad dump, but may be playable.\nPlease attempt recovery using NKit."));
				sleep(5);
			}
		}
	}
	if(swissSettings.audioStreaming && !(devices[DEVICE_CUR]->features & FEAT_AUDIO_STREAMING)) {
		msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Device does not support audio streaming.\nThis may impact playability."));
		sleep(5);
	}
	if(swissSettings.exiSpeed && (devices[DEVICE_CUR]->quirks & QUIRK_EXI_SPEED)) {
		msgBox = DrawRepublish(msgBox, DrawMessageBox(D_WARN, "Device is operating in a degraded state.\nThis may impact playability."));
		sleep(5);
	}
	/* The game's ID to the memory card emulators, which follow it to the
	 * game's own card; card_slots.c knows when each is done. */
	CardSlots_RequestGame(&GCMDisk);
	DrawDispose(msgBox);
	
	// Find the config for this game, or default if we don't know about it
	ConfigEntry *config = calloc(1, sizeof(ConfigEntry));
	memcpy(config->game_id, &GCMDisk.ConsoleID, 4);
	memcpy(config->game_name, GCMDisk.GameName, 64);
	config->region = wodeRegionToChar(GCMDisk.RegionCode);
	config->forceCleanBoot = is_diag_disc(&GCMDisk);
	config_find(config);
	
	// Show game info or return to the menu
	if(!(context != NULL ? gameflow_info_game(config, context) :
		info_game(config))) {
		free(config);
		gameflowWriteCardTrace();
		goto exit;
	}
	/* From here the launch times its waits and hands the game the clock, so
	 * Sync Clock leaves the clock as it is (gui/time_sync.c). */
	time_sync_close();
	gameflowWaitForCardIds();
	gameflowWriteCardTrace();
	/* A Library launch shows the launch screen from here to the hand-off,
	 * Boot without prompts included. */
	if(context != NULL) {
		gameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_LAUNCH);
	}
	
	if(devices[DEVICE_CONFIG] != NULL) {
		// Update the recent list.
		if(update_recent()) {
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Saving recent list\205"));
			config_update_recent(true);
			DrawDispose(msgBox);
		}
	}
	/* Play history is written where the recent list is, before the fragment
	 * table, FST and patches exist: Indigo never writes the card once the
	 * game's sectors are mapped. */
	config_record_game_handoff((const char *)&GCMDisk, 6u);
	
	// Load config for this game into our current settings
	config_load_current(config);
	rt4k_load_profile(config->rt4kProfile);
	
	if(config->forceCleanBoot || ((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) && config->preferCleanBoot)) {
		gameID_set(&GCMDisk, get_gcm_boot_hash(&GCMDisk, curFile.meta));
		
		if(!(devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR)) {
			msgBox = DrawPublish(DrawMessageBox(D_WARN, "Device does not support clean boot."));
			sleep(2);
			DrawDispose(msgBox);
			goto fail;
		}
		if(!devices[DEVICE_CUR]->setupFile(&curFile, disc2File, NULL, -2)) {
			msgBox = DrawPublish(DrawMessageBox(D_FAIL, "Failed to setup the file (too fragmented?)"));
			wait_press_A();
			DrawDispose(msgBox);
			goto fail;
		}
		if(!(devices[DEVICE_CUR]->quirks & QUIRK_NO_DEINIT)) {
			DrawGameflowCancelPosters();
			devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);
		}
		
		menuaudio_shutdown();
		DrawShutdown();
		SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
		__builtin_unreachable();
	}
	
	// setup the video mode before we kill libOGC kernel
	ogc_video__reset();
	
	// Auto load cheats if the set to auto load and if any are found
	if(swissSettings.autoCheats) {
		/* Detail discovery is display-only and current-device-only. Resolve a
		 * fresh runtime session here, including Swiss's legacy fallback slots,
		 * so launch never trusts dashboard state or a stale prior game. */
		bool cheatsFound = findCheats(true) > 0;
		if(cheatsFound) {
			loadCheatsSelection();
		}
		if(cheatsFound) {
			int appliedCount = getRuntimeEnabledCheatsCount();
			sprintf(txtbuffer, "Applied %i cheats", appliedCount);
			if(!DrawLaunchStep(txtbuffer)) {
				msgBox = DrawPublish(DrawMessageBox(D_INFO, txtbuffer));
				sleep(1);
				DrawDispose(msgBox);
			}
		}
	}
	if(swissSettings.wiirdDebug && !usb_isgeckoalive(1)) {
		swissSettings.wiirdDebug = 0;
	}
	if(cheatsLaunchStatus() == CHEAT_DECISION_TOO_LARGE) {
		msgBox = DrawPublish(DrawMessageBox(D_WARN,
			"Selected cheats exceed WiiRD capacity.\nNo cheats will be applied."));
		sleep(2);
		DrawDispose(msgBox);
	}
	if(cheatsShouldInstallEngine()) {
		swissSettings.wiirdEngine = 1;
		setTopAddr(WIIRD_ENGINE);
	}
	else {
		swissSettings.wiirdEngine = 0;
		setTopAddr(0x81800000);
	}
	
	int numToPatch = 0;
	ExecutableFile *filesToPatch = memalign(32, sizeof(ExecutableFile)*512);
	memset(filesToPatch, 0, sizeof(ExecutableFile)*512);

	// Report to the user the patch status of this GCM/ISO file
	numToPatch = check_game(&curFile, disc2File, filesToPatch);
	
	// Prompt for DOL selection if multi-dol
	ExecutableFile *fileToPatch = NULL;
	if(devices[DEVICE_PATCHES] == NULL) {
		fileToPatch = select_alt_dol(filesToPatch, numToPatch);
	}
	if(fileToPatch != NULL) {
		print_debug("Alt DOL selected: %s\n", fileToPatch->name);
		gameID_set(&GCMDisk, fileToPatch->hash);
	}
	else if(tgcFile.magic == TGC_MAGIC) {
		for(int i = 0; i < numToPatch; i++) {
			if(filesToPatch[i].file == &curFile && filesToPatch[i].offset == tgcFile.dolStart) {
				fileToPatch = &filesToPatch[i];
				gameID_set(&GCMDisk, fileToPatch->hash);
				break;
			}
		}
	}
	else if(valid_gcm_boot(&GCMDisk)) {
		for(int i = 0; i < numToPatch; i++) {
			if(filesToPatch[i].file == &curFile && filesToPatch[i].offset == GCMDisk.DOLOffset) {
				fileToPatch = &filesToPatch[i];
				gameID_set(&GCMDisk, fileToPatch->hash);
				break;
			}
		}
		if(swissSettings.bs2Boot) {
			fileToPatch = &filesToPatch[numToPatch++];
			strcpy(fileToPatch->name, "BS2.img");
			fileToPatch->type = PATCH_BS2;
		}
	}
	else {
		gameID_set(&GCMDisk, get_gcm_boot_hash(&GCMDisk, curFile.meta));
		
		fileToPatch = &filesToPatch[numToPatch++];
		strcpy(fileToPatch->name, "BS2.img");
		fileToPatch->type = PATCH_BS2;
	}
	
	s32 exi_channel = EXI_CHANNEL_MAX;
	s32 exi_device = EXI_DEVICE_MAX;
	switch(swissSettings.disableMemoryCard) {
		case 1:
			if(EXI_Probe(EXI_CHANNEL_0)) {
				exi_channel = EXI_CHANNEL_0;
				exi_device = EXI_DEVICE_0;
			}
			break;
		case 2:
			if(EXI_Probe(EXI_CHANNEL_1)) {
				exi_channel = EXI_CHANNEL_1;
				exi_device = EXI_DEVICE_0;
			}
			break;
	}
	
	*(vu8*)VAR_CURRENT_DISC = disc2File && disc2File == fileToPatch->file;
	*(vu8*)VAR_DRIVE_FLAGS = (!!swissSettings.hasFlippyDrive << 3) | ((drive_status == DEBUG_MODE) << 2) | ((tgcFile.magic == TGC_MAGIC) << 1) | !!disc2File;
	*(vu8*)VAR_EMU_READ_SPEED = swissSettings.emulateReadSpeed;
	*(vu8*)VAR_IGR_TYPE = swissSettings.igrType;
	*(vu32**)VAR_FRAG_LIST = NULL;
	*(vu8*)VAR_SD_SHIFT = 0;
	*(vu8*)VAR_EXI_SLOT = (exi_device << 6) | (exi_channel << 4) | (exi_device << 2) | exi_channel;
	*(vu8*)VAR_EXI_CPR = (EXI_CHANNEL_MAX << 6) | EXI_SPEED1MHZ;
	*(vu8*)VAR_EXI2_CPR = (EXI_CHANNEL_MAX << 6) | EXI_SPEED1MHZ;
	*(vu32**)VAR_EXI_REGS = NULL;
	net_get_mac_address((u8*)VAR_CLIENT_MAC);
	*(vu32**)VAR_EXI2_REGS = NULL;
	*(vu8*)VAR_CURRENT_FIELD = VI_FRAME;
	*(vu8*)VAR_TRIGGER_LEVEL = swissSettings.triggerLevel;
	*(vu8*)VAR_CARD_A_ID = 0x00;
	*(vu8*)VAR_CARD_B_ID = 0x00;
	
	if(getTopAddr() == 0x81800000) {
		if(fileToPatch->type == PATCH_BS2) setTopAddr(0);
		else setTopAddr(HI_RESERVE);
	}
	// Call the special setup for each device (e.g. SD will set the sector(s))
	if(!devices[DEVICE_CUR]->setupFile(&curFile, disc2File, filesToPatch, numToPatch)) {
		msgBox = DrawPublish(DrawMessageBox(D_FAIL, "Failed to setup the file (too fragmented?)"));
		wait_press_A();
		DrawDispose(msgBox);
		goto fail_patched;
	}

	if(devices[DEVICE_CUR]->emulated() & EMU_ETHERNET) {
		s32 exi_channel, exi_device, exi_interrupt, exi_speed;
		if(getExiDeviceByLocation(bba_location, &exi_channel, &exi_device) &&
			getExiInterruptByLocation(bba_location, &exi_interrupt) &&
			getExiSpeedByLocation(bba_location, &exi_speed)) {
			*(vu8*)VAR_EXI_SLOT = (*(vu8*)VAR_EXI_SLOT & 0x0F) | (((exi_device << 6) | (exi_channel << 4)) & 0xF0);
			*(vu8*)VAR_EXI2_CPR = (exi_interrupt << 6) | ((1 << exi_device) << 3) | exi_speed;
			*(vu32**)VAR_EXI2_REGS = ((vu32(*)[5])0xCC006800)[exi_channel];
		}
	}

	load_app(fileToPatch);

fail_patched:
	if(devices[DEVICE_PATCHES] != NULL) {
		for(int i = 0; i < numToPatch; i++) {
			devices[DEVICE_PATCHES]->closeFile(filesToPatch[i].patchFile);
			free(filesToPatch[i].patchFile);
		}
		if(devices[DEVICE_PATCHES] != devices[DEVICE_CUR]) {
			devices[DEVICE_PATCHES]->deinit(devices[DEVICE_PATCHES]->initial);
		}
		devices[DEVICE_PATCHES] = NULL;
	}
	free(filesToPatch);
	setTopAddr(0x81800000);
	setTopAddr(0);
fail:
	gameID_unset();
	rt4k_load_profile(swissSettings.rt4kProfile);
	config_unload_current();
	/* The launch switched to the game's video mode before anything could
	 * fail: go back to the menu's, or a game from another region leaves the
	 * menu at its rate (PAL's 50 Hz on an NTSC TV) and a component cable's
	 * 480p at the game's 480i. */
	GXRModeObj *menuMode = getVideoModeFromSwissSetting(swissSettings.uiVMode);
	if(menuMode != getVideoMode())
		DrawVideoMode(menuMode);
	free(config);
exit:
	if(context == NULL) {
		devices[DEVICE_CUR]->closeFile(&curFile);
		devices[DEVICE_CUR]->closeFile(disc2File);
	}
}

void load_game() {
	load_game_with_context(NULL);
}

/* Execute/Load/Parse the currently selected file */
static void load_file_with_context(gameflowLaunchContext_t *context)
{
	char *fileName = &curFile.name[0];
		
	//if it's a DOL, boot it
	if(strlen(fileName)>4) {
		if(endsWith(fileName,".bin") || endsWith(fileName,".dol") || endsWith(fileName,".dol+cli") || endsWith(fileName,".elf")) {
			boot_dol(&curFile, 0, NULL);
			// if it was invalid (overlaps sections, too large, etc..) it'll return
			uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "Invalid DOL"));
			sleep(2);
			DrawDispose(msgBox);
			return;
		}
		else if(endsWith(fileName,".fpkg")) {
			if(devices[DEVICE_CUR] == &__device_flippy || devices[DEVICE_CUR] == &__device_flippyflash) {
				if(confirmAction("Update the FlippyDrive with this file?\nA  UPDATE    B  CANCEL")) {
					homeFlippyUpdatePending = true;
				}
				needsRefresh = 0;
				return;
			}
			needsRefresh = manage_file() ? 1:0;
			return;
		}
		else if(endsWith(fileName,".fzn")) {
			if(curFile.size != 0x1D0000) {
				uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "File Size must be 0x1D0000 bytes!"));
				sleep(2);
				DrawDispose(msgBox);
				return;
			}
			if(!confirmAction("Write this file to the WiiKey's flash?\nA  FLASH    B  CANCEL")) {
				return;
			}
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Reading Flash File\205"));
			u8 *flash = (u8*)memalign(32,0x1D0000);
			devices[DEVICE_CUR]->seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
			devices[DEVICE_CUR]->readFile(&curFile,flash,0x1D0000);
			// Try to read a .fw file too.
			file_handle fwFile;
			memset(&fwFile, 0, sizeof(file_handle));
			snprintf(&fwFile.name[0], PATHNAME_MAX, "%s.fw", &curFile.name[0]);
			u8 *firmware = (u8*)memalign(32, 0x3000);
			DrawDispose(msgBox);
			if(devices[DEVICE_CUR] == &__device_dvd || devices[DEVICE_CUR]->readFile(&fwFile,firmware,0x3000) != 0x3000) {
				free(firmware); firmware = NULL;
				msgBox = DrawPublish(DrawMessageBox(D_WARN, "Didn't find a firmware file, flashing menu only."));
			}
			else {
				msgBox = DrawPublish(DrawMessageBox(D_INFO, "Found firmware file, this will be flashed too."));
			}
			sleep(1);
			DrawDispose(msgBox);
			wkfWriteFlash(flash, firmware);
			msgBox = DrawPublish(DrawMessageBox(D_INFO, "Flashing Complete !!"));
			sleep(2);
			DrawDispose(msgBox);
			return;
		}
		else if(endsWith(fileName,".fdi") || endsWith(fileName,".gcm") || endsWith(fileName,".iso") || endsWith(fileName,".tgc")) {
			if(devices[DEVICE_CUR]->features & FEAT_BOOT_GCM) {
				UIScene_Request(UI_SCENE_GAME_DETAIL);
				load_game_with_context(context);
				UIScene_Request(UI_SCENE_LIBRARY);
				memset(&GCMDisk, 0, sizeof(DiskHeader));
			}
			else {
				uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "Device does not support disc images."));
				sleep(5);
				DrawDispose(msgBox);
			}
			return;
		}
		else if(endsWith(fileName,".gcz") || endsWith(fileName,".rvz")) {
			uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "Compressed disc images must be\ndecompressed using NKit or Dolphin."));
			sleep(5);
			DrawDispose(msgBox);
			return;
		}
		else if(endsWith(fileName,".mp3")) {
			// MP3Player shares AESND; pause menu voices without resetting its
			// persistent decoder voice so repeated playback remains valid.
			menuaudio_suspend();
			mp3_player(getSortedDirEntries(), getSortedDirEntryCount(), &curFile);
			menuaudio_resume();
			return;
		}
		// This should be unreachable now anyway.
		uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "Unknown File Type!"));
		sleep(1);
		DrawDispose(msgBox);
		return;			
	}

}

void load_file()
{
	load_file_with_context(NULL);
}

int check_game(file_handle *file, file_handle *file2, ExecutableFile *filesToPatch)
{ 	
	char* gameID = (char*)&GCMDisk;
	uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Checking Game\205"));
	
	int numToPatch;
	if(tgcFile.magic == TGC_MAGIC) {
		numToPatch = parse_tgc(file, filesToPatch, 0, getRelativeName(file->name));
	}
	else {
		numToPatch = parse_gcm(file, file2, filesToPatch);
		
		if(!strncmp(gameID, "GCCE01", 6) || !strncmp(gameID, "GCCJGC", 6) || !strncmp(gameID, "GCCP01", 6)) {
			parse_gcm_add(file, filesToPatch, &numToPatch, "ffcc_cli.bin");
		}
		else if(!strncmp(gameID, "GHAE08", 6) || !strncmp(gameID, "GHAJ08", 6)) {
			parse_gcm_add(file, filesToPatch, &numToPatch, "claire.rel");
			parse_gcm_add(file, filesToPatch, &numToPatch, "leon.rel");
		}
		else if(!strncmp(gameID, "GHAP08", 6)) {
			switch(swissSettings.sramLanguage) {
				default:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon.rel");
					break;
				case SYS_LANG_GERMAN:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_g.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_g.rel");
					break;
				case SYS_LANG_FRENCH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_f.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_f.rel");
					break;
				case SYS_LANG_SPANISH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_s.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_s.rel");
					break;
				case SYS_LANG_ITALIAN:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_i.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_i.rel");
					break;
				case SYS_LANG_DUTCH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_f.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_g.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_i.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "claire_s.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_f.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_g.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_i.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "leon_s.rel");
					break;
			}
		}
		else if(!strncmp(gameID, "GLEP08", 6)) {
			switch(swissSettings.sramLanguage) {
				default:
					parse_gcm_add(file, filesToPatch, &numToPatch, "eng.rel");
					break;
				case SYS_LANG_GERMAN:
					parse_gcm_add(file, filesToPatch, &numToPatch, "ger.rel");
					break;
				case SYS_LANG_FRENCH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "fra.rel");
					break;
				case SYS_LANG_SPANISH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "spa.rel");
					break;
				case SYS_LANG_ITALIAN:
					parse_gcm_add(file, filesToPatch, &numToPatch, "ita.rel");
					break;
				case SYS_LANG_DUTCH:
					parse_gcm_add(file, filesToPatch, &numToPatch, "eng.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "fra.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "ger.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "ita.rel");
					parse_gcm_add(file, filesToPatch, &numToPatch, "spa.rel");
					break;
			}
		}
		if(swissSettings.disableVideoPatches < 1) {
			if(!strncmp(gameID, "GS8P7D", 6)) {
				parse_gcm_add(file, filesToPatch, &numToPatch, "SPYROCFG_NGC.CFG");
			}
		}
	}
	DrawDispose(msgBox);
	
	if(devices[DEVICE_CUR]->emulated()) {
		patch_gcm(filesToPatch, numToPatch);
	}
	return numToPatch;
}

uiDrawObj_t* draw_game_info(ConfigEntry *config) {
	uiDrawObj_t *container = DrawEmptyBox(75,120, getVideoMode()->fbWidth-78, 400);

	sprintf(txtbuffer, "%s", curFile.meta && curFile.meta->displayName ? curFile.meta->displayName : getRelativeName(curFile.name));
	float scale = GetTextScaleToFitInWidth(txtbuffer,(getVideoMode()->fbWidth-78)-75);
	DrawAddChild(container, DrawStyledLabel(640/2, 130, txtbuffer, scale, ALIGN_CENTER, defaultColor));

	if(devices[DEVICE_CUR] == &__device_qoob) {
		formatBytes(stpcpy(txtbuffer, "Size: "), curFile.size, 65536, false);
		DrawAddChild(container, DrawStyledLabel(640/2, 160, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
		sprintf(txtbuffer,"Position on Flash: %08X",(u32)(curFile.fileBase&0xFFFFFFFF));
		DrawAddChild(container, DrawStyledLabel(640/2, 180, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
	}
	else if(devices[DEVICE_CUR] == &__device_wode) {
		ISOInfo_t* isoInfo = (ISOInfo_t*)&curFile.other;
		sprintf(txtbuffer,"Partition: %i, ISO: %i", isoInfo->iso_partition,isoInfo->iso_number);
		DrawAddChild(container, DrawStyledLabel(640/2, 160, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
	}
	else if(devices[DEVICE_CUR] == &__device_card_a || devices[DEVICE_CUR] == &__device_card_b) {
		formatBytes(stpcpy(txtbuffer, "Size: "), curFile.size, 8192, false);
		DrawAddChild(container, DrawStyledLabel(640/2, 160, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
		sprintf(txtbuffer,"Position on Card: %08X",curFile.offset);
		DrawAddChild(container, DrawStyledLabel(640/2, 180, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
	}
	else {
		formatBytes(stpcpy(txtbuffer, "Size: "), curFile.size, 0, true);
		DrawAddChild(container, DrawStyledLabel(640/2, 160, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));
		if(curFile.meta) {
			if(curFile.meta->banner)
				DrawAddChild(container, DrawTexObj(&curFile.meta->bannerTexObj, 215, 240, 192, 64, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
			if(curFile.meta->regionTexObj)
				DrawAddChild(container, DrawTexObj(curFile.meta->regionTexObj, 449, 262, 32, 20, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));

			sprintf(txtbuffer, "%s", curFile.meta->bannerDesc.description);
			char* rest = &txtbuffer[0]; 
			char* tok;
			int line = 0;
			float minScale = 1.0f;
			while ((tok = strtok_r (rest,"\r\n", &rest))) {
				minScale = MIN(GetTextScaleToFitInWidth(tok,(getVideoMode()->fbWidth-78)-75), minScale);
			}
			sprintf(txtbuffer, "%s", curFile.meta->bannerDesc.description);
			rest = &txtbuffer[0]; 
			while ((tok = strtok_r (rest,"\r\n", &rest))) {
				DrawAddChild(container, DrawStyledLabel(640/2, 315+(line*minScale*24), tok, minScale, ALIGN_CENTER, defaultColor));
				line++;
			}
		}
	}
	if(GCMDisk.DVDMagicWord == DVD_MAGIC) {
		if(is_verifiable_disc(&GCMDisk)) {
			DrawAddChild(container, DrawStyledLabel(640/2, 180, "(R) Verify data integrity", 0.6f, ALIGN_CENTER, defaultColor));
		}
		sprintf(txtbuffer, "Game ID: [%.6s] Audio Streaming: [%s]", (char*)&GCMDisk, GCMDisk.AudioStreaming ? swissSettings.audioStreaming ? "Enabled" : "Unused" : "Disabled");
		DrawAddChild(container, DrawStyledLabel(640/2, 200, txtbuffer, 0.8f, ALIGN_CENTER, defaultColor));

		if(GCMDisk.TotalDisc > 1) {
			if(devices[DEVICE_CUR]->quirks & QUIRK_GCLOADER_NO_DISC_2) {
				DrawAddChild(container, DrawStyledLabel(640/2, 220, "A firmware update is required.", 0.6f, ALIGN_CENTER, defaultColor));
			}
			else {
				sprintf(txtbuffer, "Disc %i/%i [Found: %s]", GCMDisk.DiscID+1, GCMDisk.TotalDisc, meta_find_disc2(&curFile) ? "Yes":"No");
				DrawAddChild(container, DrawStyledLabel(640/2, 220, txtbuffer, 0.6f, ALIGN_CENTER, defaultColor));
			}
		}
		else if(GCMDisk.CountryCode == 'E'
			&& GCMDisk.RegionCode == 0) {
			if(GCMDisk.Version > 0x30) {
				sprintf(txtbuffer, "Revision %X", GCMDisk.Version - 0x30);
				DrawAddChild(container, DrawStyledLabel(640/2, 220, txtbuffer, 0.6f, ALIGN_CENTER, defaultColor));
			}
		}
		else if(GCMDisk.Version) {
			sprintf(txtbuffer, "Revision %X", GCMDisk.Version);
			DrawAddChild(container, DrawStyledLabel(640/2, 220, txtbuffer, 0.6f, ALIGN_CENTER, defaultColor));
		}
	}

	char *textPtr = txtbuffer;
	if(devices[DEVICE_CONFIG] != NULL) {
		bool isAutoLoadEntry = !strcmp(swissSettings.autoload, curFile.name) || !fnmatch(swissSettings.autoload, curFile.name, FNM_PATHNAME);
		textPtr += sprintf(textPtr, "Z  Load at startup [Current: %s]", isAutoLoadEntry ? "Yes":"No");
	}
	if((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) && !config->preferCleanBoot) {
		textPtr = stpcpy(textPtr, textPtr == txtbuffer ? "L+A  Clean Boot":"    L+A  Clean Boot");
	}
	if(textPtr != txtbuffer) {
		DrawAddChild(container, DrawHintLabel(640/2, 370, txtbuffer, 0.6f, ALIGN_CENTER, defaultColor));
	}
	textPtr = stpcpy(txtbuffer, "X  Settings    Y  Cheats");

	if(devices[DEVICE_CUR] != &__device_wode) {
		textPtr = stpcpy(textPtr, "    B  Exit");
	}
	if((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) && config->preferCleanBoot) {
		textPtr = stpcpy(textPtr, "    A  Clean Boot");
	}
	else {
		textPtr = stpcpy(textPtr, "    A  Boot");
	}
	DrawAddChild(container, DrawHintLabel(640/2, 390, txtbuffer, 0.75f, ALIGN_CENTER, defaultColor));
	return container;
}

static u32 gameflowDetailInput(u32 buttons)
{
	u32 input = 0u;

	if(buttons & BUTTON_A) input |= UI_GAMEFLOW_DETAIL_INPUT_A;
	if(buttons & BUTTON_B) input |= UI_GAMEFLOW_DETAIL_INPUT_B;
	if(buttons & PAD_BUTTON_X) input |= UI_GAMEFLOW_DETAIL_INPUT_X;
	if(buttons & PAD_BUTTON_Y) input |= UI_GAMEFLOW_DETAIL_INPUT_Y;
	if(buttons & BUTTON_Z) input |= UI_GAMEFLOW_DETAIL_INPUT_Z;
	if(buttons & BUTTON_R) input |= UI_GAMEFLOW_DETAIL_INPUT_R;
	if(buttons & BUTTON_L) input |= UI_GAMEFLOW_DETAIL_INPUT_L;
	if(buttons & BUTTON_UP) input |= UI_GAMEFLOW_DETAIL_INPUT_UP;
	if(buttons & BUTTON_DOWN) input |= UI_GAMEFLOW_DETAIL_INPUT_DOWN;
	return input;
}

/* The slot whose card the game reads its save from: the first holding a copy
 * of it, else the first with a card in it. -1: no card. */
static int gameflowSaveSlot(const savesCopies_t *copies)
{
	unsigned slot, i;

	for(slot = 0u; slot < 2u; slot++) {
		for(i = 0u; i < copies->count; i++) {
			if(copies->copy[i].source == (savesCopySource_t)slot) return (int)slot;
		}
	}
	for(slot = 0u; slot < 2u; slot++) {
		if(copies->cards[slot]) return (int)slot;
	}
	return -1;
}

/* A save file in the Source's swiss/saves goes by the Source's name; a card
 * image anywhere by its own. */
static const char *gameflowSaveWhere(const savesCopy_t *copy)
{
	switch(copy->source) {
		case SAVES_COPY_SLOT_A: return "Slot A";
		case SAVES_COPY_SLOT_B: return "Slot B";
		case SAVES_COPY_FILE: return copy->device == devices[DEVICE_CONFIG] ?
			"Save Folder" : DeviceDisplayName(copy->device);
		default: return getRelativeName((char *)copy->path);
	}
}

/* Slots (bit 0 A, bit 1 B) a game's details found changing cards since Indigo
 * started: a memory card emulator that took the game's ID (card_slots.c).
 * Indigo only reads those: the MemCard PRO build tested on a real card
 * (indigo#102) never wrote to one. */
static unsigned gameflowSaveEmulators;

/* The newest copy's own line ("Last saved on 1/6"), for the totals: what the
 * game wrote last. Empty when none was read. */
static const char *gameflowSaveNewestLine(void)
{
	const savesCopy_t *newest = NULL;
	unsigned i;

	for(i = 0u; i < gameflowSaveCopies.count; i++) {
		if(newest == NULL || UISaves_UpdatedSeconds(gameflowSaveCopies.copy[i].entry) >
			UISaves_UpdatedSeconds(newest->entry)) {
			newest = &gameflowSaveCopies.copy[i];
		}
	}
	return newest != NULL ? newest->line : "";
}

/* Whether the copies listed are of more than one of the game's files. */
static bool gameflowSaveFiles(void)
{
	unsigned i;

	for(i = 1u; i < gameflowSaveCopies.count; i++) {
		if(!UISaves_SameFile(gameflowSaveCopies.copy[i].entry,
			gameflowSaveCopies.copy[0].entry)) return true;
	}
	return false;
}

/* Left and Right choose a copy only where a launch can put it on the card the
 * game reads: two or more copies, a card in a slot that isn't a memory card
 * emulator, and Emulate Memory Card off (the game would read its card image
 * instead). */
static bool gameflowSaveChoosable(const gameflowLaunchContext_t *context)
{
	return context->savesScanned && gameflowSaveCopies.count >= 2u &&
		context->saveSlot >= 0 && !swissSettings.emulateMemoryCard &&
		!(gameflowSaveEmulators & (1u << context->saveSlot));
}

/* The copy Left and Right show, while there is a choice. Without one the box
 * shows the totals, with no « SAVES ». With copies of more than one file, it
 * names the chosen copy's file and its place among that file's copies. */
static void gameflowSaveChoiceSource(const gameflowLaunchContext_t *context,
	uiGameflowDetailSource_t *source)
{
	static char file[UI_SAVES_NAME_LENGTH + 1u];
	const savesCopy_t *copy;
	unsigned i;

	if(!gameflowSaveChoosable(context)) {
		return;
	}
	source->saveCopies = gameflowSaveCopies.count;
	if(context->saveChoice < 0 ||
		(unsigned)context->saveChoice >= gameflowSaveCopies.count) {
		return;
	}
	copy = &gameflowSaveCopies.copy[context->saveChoice];
	source->saveChoice = (uint32_t)context->saveChoice + 1u;
	source->saveChoiceEntry = copy->entry;
	source->saveChoiceWhere = gameflowSaveWhere(copy);
	source->saveChoiceInUse = context->saveSlot >= 0 &&
		copy->source == (savesCopySource_t)context->saveSlot;
	source->saveChoiceLine = copy->line;
	if(gameflowSaveFiles() && UISaves_DisplayText(file, sizeof(file),
		copy->entry + 8, UI_SAVES_NAME_LENGTH)) {
		source->saveChoiceFile = file;
		for(i = 0u; i < gameflowSaveCopies.count; i++) {
			if(UISaves_SameFile(gameflowSaveCopies.copy[i].entry, copy->entry)) {
				source->saveChoiceFileCopies++;
				if(i <= (unsigned)context->saveChoice) source->saveChoiceFileCopy++;
			}
		}
	}
}

/* Set when a memory card slot changed (card_slots.c) while a box of
 * gameflowSaveAsk was up: the copies Detail showed may not be the cards'. */
static bool gameflowSlotsChanged;

/* A box until A or B, once the press that opened it is let go. The memory
 * card slots are followed meanwhile. */
static bool gameflowSaveAsk(const char *text)
{
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_INFO, text));
	bool released = false;
	bool yes = false;

	while(1) {
		u32 held = padsButtonsHeld();

		if(CardSlots_Poll()) gameflowSlotsChanged = true;
		if(!released) {
			released = (held & (BUTTON_A | BUTTON_B)) == 0u;
		}
		else if(held & BUTTON_A) {
			yes = true;
			break;
		}
		else if(held & BUTTON_B) {
			break;
		}
		VIDEO_WaitVSync();
	}
	DrawDispose(box);
	while(padsButtonsHeld() & (BUTTON_A | BUTTON_B)) {
		VIDEO_WaitVSync();
	}
	return yes;
}

/* Diagnostic branch only: keep the trace on the settings device after Detail
 * closes. Never write through a memory-card slot; the card being diagnosed
 * must see exactly the release's traffic. No save data is included. */
static void gameflowWriteCardTrace(void)
{
	static size_t written;
	const char *text = CardSlots_Trace();
	size_t length = strlen(text);

	if(length == 0u || length == written || devices[DEVICE_CONFIG] == NULL ||
		(devices[DEVICE_CONFIG]->location & (LOC_MEMCARD_SLOT_A | LOC_MEMCARD_SLOT_B)) != 0u ||
		!config_set_device()) {
		return;
	}
	if(config_file_write("swiss/settings/card-trace.txt", (char *)text)) written = length;
	config_unset_device();
}

/* A launch with a game ID still waiting behind a memory card's switch sends
 * it first, once that switch is done, so the game starts with its own card on
 * the way. Bounded by the switch's own deadline; the box says why. */
static void gameflowWaitForCardIds(void)
{
	uiDrawObj_t *box = NULL;
	u64 start = gettime();

	while(CardSlots_IdWaiting() && diff_msec(start, gettime()) < 65000u) {
		if(box == NULL && diff_msec(start, gettime()) > 500u) {
			box = DrawPublish(DrawProgressBar(true, 0,
				"Waiting for the memory card\205"));
		}
		(void)CardSlots_Poll();
		VIDEO_WaitVSync();
	}
	if(box != NULL) DrawDispose(box);
}

/* Before a launch: a copy chosen with Left and Right that isn't the one on
 * the card the game reads goes on that card first, once A says so. False:
 * Detail stays open (B, which goes back to the card's own copy, or it didn't
 * go on). Without a choice (gameflowSaveChoosable) the card stays as it is. */
static bool gameflowLoadChosenSave(gameflowLaunchContext_t *context)
{
	const savesCopy_t *copy;
	char why[256];
	char text[512];
	char file[UI_SAVES_NAME_LENGTH + 1u];
	uiDrawObj_t *box;
	bool ok, own = false;
	unsigned i;

	gameflowSlotsChanged = false;
	if(!gameflowSaveChoosable(context) || context->saveChoice < 0 ||
		(unsigned)context->saveChoice >= gameflowSaveCopies.count) {
		return true;
	}
	copy = &gameflowSaveCopies.copy[context->saveChoice];
	if(copy->source == (savesCopySource_t)context->saveSlot) {
		return true;
	}
	/* The card's own copies are never dropped from the list, so none of this
	 * file there means the card has none of it to keep (another file of the
	 * game it has stays as it is). */
	for(i = 0u; i < gameflowSaveCopies.count; i++) {
		own |= gameflowSaveCopies.copy[i].source == (savesCopySource_t)context->saveSlot &&
			UISaves_SameFile(gameflowSaveCopies.copy[i].entry, copy->entry);
	}
	/* Never onto the card a memory card emulator is still changing from. */
	if(!CardSlots_WritableFor(context->saveSlot, context->gameId)) {
		gameflowSaveAsk("The memory card is still changing to this game's\n"
			"card. Try again in a moment.\nA  OK");
		return false;
	}
	/* With more than one file of the game, the question names the file. */
	if(!gameflowSaveFiles() ||
		!UISaves_DisplayText(file, sizeof(file), copy->entry + 8, UI_SAVES_NAME_LENGTH)) {
		snprintf(file, sizeof(file), "the save");
	}
	snprintf(text, sizeof(text), own ? "Start with %s from %s?\n"
		"%s's own copy of it goes to the Save Folder first.\nA  LOAD    B  KEEP" :
		"Start with %s from %s?\n"
		"It goes on the memory card in %s.\nA  LOAD    B  KEEP", file,
		gameflowSaveWhere(copy), context->saveSlot == 0 ? "Slot A" : "Slot B");
	if(!gameflowSaveAsk(text)) {
		context->saveChoice = -1;
		return false;
	}
	/* Again after the question, which followed the cards while it was up: a
	 * card that changed meanwhile may not hold the copies Detail showed. */
	if(CardSlots_Poll() || gameflowSlotsChanged) {
		context->savesScanned = false;
		gameflowSaveAsk("A memory card changed while the question was up.\n"
			"Look at the saves again, then launch.\nA  OK");
		return false;
	}
	if(!CardSlots_WritableFor(context->saveSlot, context->gameId)) {
		gameflowSaveAsk("The memory card is still changing to this game's\n"
			"card. Try again in a moment.\nA  OK");
		return false;
	}
	box = DrawPublish(DrawProgressBar(true, 0, "Loading save\205"));
	ok = Saves_LoadCopy(context->saveSlot, copy, why, sizeof(why));
	DrawDispose(box);
	if(!ok) {
		snprintf(text, sizeof(text), "The save didn't go on the card.\n%s\nA  OK",
			why);
		gameflowSaveAsk(text);
		/* The cards may have changed: read them again. */
		context->savesScanned = false;
	}
	return ok;
}

static bool gameflowPublishDetail(ConfigEntry *config,
	gameflowLaunchContext_t *context)
{
	uiGameflowDetailSnapshot_t *snapshot;
	uiGameflowDetailSource_t source;
	uiGameflowDetailCheatSource_t *cheatSources = NULL;
	CheatEntries *cheats = getCheats();
	file_meta *meta = curFile.meta;
	char fallbackTitle[65];
	char facts[UI_GAMEFLOW_DETAIL_FACTS_CAPACITY];
	char sizeText[32];
	const char *company = "";
	const char *description = "";
	u32 flags = UI_GAMEFLOW_DETAIL_CHEATS_KNOWN |
		UI_GAMEFLOW_DETAIL_CAN_SETTINGS;
	int availableCheats;
	int i;
	bool published = false;

	if(config == NULL || context == NULL || context->event == NULL ||
		cheats == NULL) {
		return false;
	}
	memset(&source, 0, sizeof(source));
	availableCheats = MIN(MAX(0, context->knownCheatCount),
		cheats->num_cheats);
	memset(fallbackTitle, 0, sizeof(fallbackTitle));
	memcpy(fallbackTitle, GCMDisk.GameName, sizeof(GCMDisk.GameName));
	formatBytes(sizeText, curFile.size, 0,
		curFile.device == NULL || !(curFile.device->location & LOC_SYSTEM));
	snprintf(facts, sizeof(facts),
		"%.6s  |  %s  |  DISC %u/%u  |  REGION %c",
		context->gameId, sizeText, (unsigned int)GCMDisk.DiscID + 1u,
		MAX(1u, (unsigned int)GCMDisk.TotalDisc),
		config->region != '\0' ? config->region : '?');

	if(meta != NULL) {
		company = meta->bannerDesc.fullCompany[0] ?
			meta->bannerDesc.fullCompany : meta->bannerDesc.company;
		description = meta->bannerDesc.description;
	}
	if(availableCheats > 0) {
		cheatSources = calloc((size_t)availableCheats,
			sizeof(*cheatSources));
		if(cheatSources == NULL) {
			return false;
		}
		for(i = 0; i < availableCheats; ++i) {
			cheatSources[i].name = cheats->cheat[i].name;
			cheatSources[i].enabled = cheats->cheat[i].enabled != 0;
		}
		flags |= UI_GAMEFLOW_DETAIL_CAN_CHEATS;
	}
	if(devices[DEVICE_CUR] != &__device_wode) {
		flags |= UI_GAMEFLOW_DETAIL_CAN_LIBRARY;
	}
	if(gameflowFromFiles) {
		flags |= UI_GAMEFLOW_DETAIL_BACK_FILES;
	}
	if(devices[DEVICE_CONFIG] != NULL) {
		flags |= UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD;
		if(!strcmp(swissSettings.autoload, curFile.name) ||
			!fnmatch(swissSettings.autoload, curFile.name, FNM_PATHNAME)) {
			flags |= UI_GAMEFLOW_DETAIL_IS_AUTOLOAD;
		}
	}
	if(is_verifiable_disc(&GCMDisk)) {
		flags |= UI_GAMEFLOW_DETAIL_CAN_VERIFY;
	}
	if((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) &&
		!config->preferCleanBoot) {
		flags |= UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT;
	}
	if(config->forceCleanBoot ||
		((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) &&
		config->preferCleanBoot)) {
		flags |= UI_GAMEFLOW_DETAIL_CLEAN_BOOT_DEFAULT;
	}
	if(GCMDisk.AudioStreaming && swissSettings.audioStreaming) {
		flags |= UI_GAMEFLOW_DETAIL_AUDIO_STREAMING;
	}
	if(context->oppositeDisc != NULL) {
		flags |= UI_GAMEFLOW_DETAIL_HAS_DISC_TWO;
	}

	source.generation = context->generation;
	source.focusIndex = context->focusIndex;
	source.gameId = context->gameId;
	source.title = meta != NULL && meta->displayName != NULL &&
		meta->displayName[0] ? meta->displayName :
		(fallbackTitle[0] ? fallbackTitle : getRelativeName(curFile.name));
	source.company = company;
	source.facts = facts;
	source.description = description;
	source.lastPlayedUnixSeconds = config_last_played(context->gameId, 6u,
		&source.playHistoryAvailable);
	source.saveStatus = UI_GAME_SAVE_NOT_CHECKED;
	/* Only the verified disc launch context owns a save identity, and only
	 * Saves on Details (on by default) reads the memory cards for it: off, a
	 * game's details leave them alone. Apps use the same Library renderer
	 * but never take this snapshot path. Read once while Detail is open. */
	if(!swissSettings.hideDetailSaves &&
		context->primary != NULL && context->primary->fileType == IS_FILE &&
		valid_gcm_magic(&GCMDisk) &&
		memcmp(context->gameId, &GCMDisk, UI_GAMEFLOW_DETAIL_ID_LENGTH) == 0) {
		if(!context->savesScanned) {
			unsigned slots =
				(CardSlots_ReadableFor(0, context->gameId) ? 1u : 0u) |
				(CardSlots_ReadableFor(1, context->gameId) ? 2u : 0u);

			/* A slot this game can't read yet is an emulator changing cards. */
			gameflowSaveEmulators |= ~slots & 3u;
			Saves_CollectGameStats(context->gameId, &context->saveStats,
				&gameflowSaveCopies, slots);
			/* A card given up on while changing to this game's card: its
			 * saves are missing, so the scan is incomplete. */
			context->savesCardFailed = CardSlots_FailedFor(0, context->gameId) ||
				CardSlots_FailedFor(1, context->gameId);
			if(context->savesCardFailed) context->saveStats.partial = true;
			CardSlots_TraceScan(slots, context->saveStats.saves, context->saveStats.partial);
			context->savesScanned = true;
			context->saveSlot = gameflowSaveSlot(&gameflowSaveCopies);
			/* The totals first: the card as it is. */
			context->saveChoice = -1;
		}
		context->savesWaiting = CardSlots_WaitingFor(context->gameId);
		source.saveStats = &context->saveStats;
		source.saveLine = gameflowSaveNewestLine();
		source.savesWaiting = context->savesWaiting;
		source.savesCardFailed = context->savesCardFailed;
		gameflowSaveChoiceSource(context, &source);
	}
	source.customSettings = (uint32_t)settings_game_custom_count(config);
	source.firstCustomSetting = settings_game_custom_first(config);
	context->manualPages = Manual_Pages(devices[DEVICE_CUR], context->gameId);
	source.manualPages = context->manualPages;
	if(meta != NULL && meta->banner != NULL &&
		meta->bannerSize == UI_GAMEFLOW_DETAIL_BANNER_BYTES &&
		meta->bannerSum != 0xFFFF) {
		source.banner = meta->banner;
		source.bannerSize = meta->bannerSize;
	}
	source.cheats = cheatSources;
	source.cheatCount = availableCheats > 0 ?
		(size_t)availableCheats : 0u;
	source.enabledCheatBytes = (u32)MAX(0, getEnabledCheatsSize());
	source.cheatCapacityBytes = (u32)MAX(0, kenobi_get_maxsize());
	source.flags = flags;

	snapshot = memalign(32, sizeof(*snapshot));
	if(snapshot != NULL && UIGameflowDetail_Build(snapshot, &source)) {
		published = DrawUpdateGameflowDetail(context->event, snapshot);
	}
	free(snapshot);
	free(cheatSources);
	return published;
}

static int gameflow_info_game(ConfigEntry *config,
	gameflowLaunchContext_t *context)
{
	const u32 detailButtons = PAD_BUTTON_X | BUTTON_B | BUTTON_A |
		PAD_BUTTON_Y | BUTTON_Z | BUTTON_R | BUTTON_UP | BUTTON_DOWN |
		BUTTON_LEFT | BUTTON_RIGHT;
	uiMenuActionState_t detailInput;
	uiMenuInputState_t detailStick;
	uiGameflowDetailFocus_t focus = UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH;
	int numCheats;
	bool openSettings;

	/* Preserve all Cubeboot and auto-boot behavior in the untouched public
	 * implementation, including its cancellation semantics. Y's settings
	 * are never a boot, so they bypass Boot without prompts. */
	if(context == NULL || swissSettings.cubebootInvoked ||
		(swissSettings.autoBoot && !context->openSettings)) {
		return info_game(config);
	}
	openSettings = context->openSettings;
	context->openSettings = false;

	numCheats = findCheatsReadOnly();
	context->cheatsScanned = true;
	if(numCheats > 0) {
		loadCheatsSelectionReadOnly();
		context->knownCheatCount = numCheats;
	}
	else {
		/* Discovery owns the global reset and clears definitions, flags, and
		 * launch provenance before every allocation or device probe. */
		context->knownCheatCount = 0;
	}
	if(!gameflowPublishDetail(config, context)) {
		/* Without the retained Detail, Y goes back to the Library: the
		 * legacy screen would boot under Boot without prompts. */
		return openSettings ? 0 : info_game(config);
	}
	DrawSetGameflowDetailFocus(context->event, focus);
	gameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_DETAIL);
	/* The entry A press is consumed, but held L and the C-stick cannot
	 * block the detail screen. Only physical X/Y invoke those shortcuts.
	 * Presses made and let go before now aren't for here either. */
	UIMenuAction_Init(&detailInput, padsButtonsHeld());
	(void)padsButtonsTaken(detailButtons);
	UIMenuInput_Init(&detailStick);

	while(1) {
		u32 buttons;
		u32 input;
		uiMenuInputDirection_t analog;
		uiGameflowDetailSnapshot_t actionSnapshot;
		uiGameflowDetailAction_t action;
		uiGameflowDetailFocus_t moved;

		if(openSettings) {
			/* Entered with Y: the settings, then this Detail. */
			openSettings = false;
			action = UI_GAMEFLOW_DETAIL_ACTION_SETTINGS;
		}
		else {
			do {
				VIDEO_WaitVSync();
				/* Taken from the scans as well as held: a press made and
				 * let go while a poster was read is still seen. */
				buttons = UIMenuAction_Update(&detailInput,
					padsButtonsHeld() | padsButtonsTaken(detailButtons),
					detailButtons, BUTTON_L, BUTTON_B);
				/* The stick steps like the D-pad: once a push, back to
				 * centre first, never while a button is down. With no
				 * repeat it needs no clock. */
				analog = padsMenuInputPoll(&detailStick, 0u,
					UI_MENU_INPUT_AXIS_BOTH,
					(padsButtonsHeld() & detailButtons) != 0u);
				if(analog == UI_MENU_INPUT_UP) buttons |= BUTTON_UP;
				if(analog == UI_MENU_INPUT_DOWN) buttons |= BUTTON_DOWN;
				if(analog == UI_MENU_INPUT_LEFT) buttons |= BUTTON_LEFT;
				if(analog == UI_MENU_INPUT_RIGHT) buttons |= BUTTON_RIGHT;
				/* A poster still on its way (its slot was just let go, or
				 * the pack only now opened) is read on an idle retrace, one
				 * at most, as the Library does, and fades in when it lands. */
				if(buttons == 0u) {
					DrawGameflowPollPosters();
					/* A card finished changing to this game's card, gave
					 * up, or left the bus: read the saves again. */
					if(CardSlots_Poll() && context->savesScanned) {
						context->savesScanned = false;
						gameflowPublishDetail(config, context);
					}
				}
			} while(buttons == 0u);
			/* Left and Right choose the save copy to start with, while there
			 * is a choice: each copy, then the totals again (-1, the card as
			 * it is), so a choice can always be undone. */
			if(buttons & (BUTTON_LEFT | BUTTON_RIGHT)) {
				/* Not while a card changes to this game's card: its copy
				 * isn't on the list yet. */
				if(gameflowSaveChoosable(context) && !context->savesWaiting) {
					int span = (int)gameflowSaveCopies.count + 1;
					int step = (buttons & BUTTON_RIGHT) ? 1 : -1;

					context->saveChoice = (context->saveChoice + 1 + step + span) %
						span - 1;
					gameflowPublishDetail(config, context);
					menuaudio_blip();
				}
				continue;
			}
			memset(&actionSnapshot, 0, sizeof(actionSnapshot));
			actionSnapshot.flags = UI_GAMEFLOW_DETAIL_VALID |
				UI_GAMEFLOW_DETAIL_CAN_SETTINGS |
				(devices[DEVICE_CUR] != &__device_wode ?
					UI_GAMEFLOW_DETAIL_CAN_LIBRARY : 0u) |
				(context->knownCheatCount > 0 ?
					UI_GAMEFLOW_DETAIL_CAN_CHEATS : 0u) |
				(devices[DEVICE_CONFIG] != NULL ?
					UI_GAMEFLOW_DETAIL_CAN_AUTOLOAD : 0u) |
				(context->manualPages != 0 ?
					UI_GAMEFLOW_DETAIL_HAS_MANUAL : 0u) |
				(is_verifiable_disc(&GCMDisk) ?
					UI_GAMEFLOW_DETAIL_CAN_VERIFY : 0u) |
				(((devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR) &&
					!config->preferCleanBoot) ?
					UI_GAMEFLOW_DETAIL_CAN_CLEAN_BOOT : 0u);
			input = gameflowDetailInput(buttons);
			moved = UIGameflowDetail_MoveFocus(&actionSnapshot, focus, input);
			action = UI_GAMEFLOW_DETAIL_ACTION_NONE;
			/* Like Home: a sample that moves does nothing else. */
			if(moved != focus) {
				focus = moved;
				DrawSetGameflowDetailFocus(context->event, focus);
				menuaudio_blip();
			}
			else {
				action = UIGameflowDetail_ResolveAction(&actionSnapshot,
					focus, input);
				if(action != UI_GAMEFLOW_DETAIL_ACTION_NONE) {
					menuaudio_select();
				}
			}
		}

		if((action == UI_GAMEFLOW_DETAIL_ACTION_BOOT ||
			action == UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT) &&
			!gameflowLoadChosenSave(context)) {
			/* Not now: the save stays as it was, and so does Detail, but for
			 * a card that changed while one of its boxes was up. */
			if(gameflowSlotsChanged) context->savesScanned = false;
			gameflowPublishDetail(config, context);
		}
		else if(action == UI_GAMEFLOW_DETAIL_ACTION_BOOT ||
			action == UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT) {
			if(action == UI_GAMEFLOW_DETAIL_ACTION_CLEAN_BOOT) {
				config->forceCleanBoot = 1;
			}
			/* L+A from another row: the launch lights Launch. */
			DrawSetGameflowDetailFocus(context->event,
				UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH);
			DrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LAUNCH);
			while(padsButtonsHeld() & BUTTON_A) {
				VIDEO_WaitVSync();
			}
			return 1;
		}
		if(action == UI_GAMEFLOW_DETAIL_ACTION_LIBRARY) {
			gameflowBackToFiles();
			DrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LIBRARY);
			/* The browser owns its B release drain after restoring Library. */
			return 0;
		}
		if(action == UI_GAMEFLOW_DETAIL_ACTION_VERIFY) {
			verify_game();
			UIScene_Request(UI_SCENE_GAME_DETAIL);
			gameflowPublishDetail(config, context);
		}
		else if(action == UI_GAMEFLOW_DETAIL_ACTION_SETTINGS) {
			UIScene_Request(UI_SCENE_SETTINGS);
			needsRefresh = show_settings_view(VIEW_GAME, 0, config);
			UIScene_Request(UI_SCENE_GAME_DETAIL);
			gameflowPublishDetail(config, context);
		}
		/* From the File Browser, turning Autoload on asks first, as Swiss's
		 * game screen did there. */
		else if(action == UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD &&
			(!gameflowFromFiles ||
			autoloadToggleConfirmed(curFile.name, false))) {
			if(!strcmp(swissSettings.autoload, curFile.name) ||
				!fnmatch(swissSettings.autoload, curFile.name,
					FNM_PATHNAME)) {
				memset(swissSettings.autoload, 0,
					sizeof(swissSettings.autoload));
			}
			else if(!fnmatch("dvd:/*.gcm", curFile.name, FNM_PATHNAME)) {
				strcpy(swissSettings.autoload, "dvd:/*.gcm");
			}
			else {
				strcpy(swissSettings.autoload, curFile.name);
			}
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0,
				"Saving autoload\205"));
			config_update_autoload(true);
			DrawDispose(msgBox);
			gameflowPublishDetail(config, context);
		}
		else if(action == UI_GAMEFLOW_DETAIL_ACTION_CHEATS) {
			DrawCheatsSelector(curFile.name);
			saveCheatsSelection();
			gameflowPublishDetail(config, context);
		}
		else if(action == UI_GAMEFLOW_DETAIL_ACTION_MANUAL) {
			Manual_Show(devices[DEVICE_CUR], context->gameId);
			gameflowPublishDetail(config, context);
			/* Its file gone meanwhile: the row is too. */
			if(context->manualPages == 0) {
				focus = UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS;
				DrawSetGameflowDetailFocus(context->event, focus);
			}
		}
		/* A modal's dismissal is not a new Detail action. Do not require
		 * unrelated buttons or a held clean-boot modifier to be released. */
		UIMenuAction_Init(&detailInput, padsButtonsHeld());
		(void)padsButtonsTaken(detailButtons);
		UIMenuInput_Init(&detailStick);
	}
}

/* Show info about the game - and also load the config for it */
int info_game(ConfigEntry *config)
{
	if(swissSettings.cubebootInvoked) {
		u32 buttons = padsButtonsHeld();
		if(buttons & BUTTON_Y) {
			if(findCheats(false) > 0) {
				loadCheatsSelection();
				DrawCheatsSelector(curFile.name);
				saveCheatsSelection();
			}
		}
		return swissSettings.cubebootInvoked;
	}
	if(swissSettings.autoBoot) {
		if(padsButtonsHeld() & BUTTON_B) {
			swissSettings.autoBoot = 0;
		} else {
			return swissSettings.autoBoot;
		}
	}
	int ret = 0, num_cheats = -1;
	uiDrawObj_t *infoPanel = DrawPublish(draw_game_info(config));
	while(1) {
		while(padsButtonsHeld() & (BUTTON_X | BUTTON_B | BUTTON_A | BUTTON_Y | BUTTON_Z | BUTTON_R)){ VIDEO_WaitVSync (); }
		while(!(padsButtonsHeld() & (BUTTON_X | BUTTON_B | BUTTON_A | BUTTON_Y | BUTTON_Z | BUTTON_R))){ VIDEO_WaitVSync (); }
		u32 buttons = padsButtonsHeld();
		if(buttons & BUTTON_A) {
			if(buttons & BUTTON_L) {
				config->forceCleanBoot = 1;
			}
			ret = 1;
			break;
		}
		// WODE can't return from here.
		if((buttons & BUTTON_B) && devices[DEVICE_CUR] != &__device_wode) {
			ret = 0;
			break;
		}
		if((buttons & BUTTON_R) && is_verifiable_disc(&GCMDisk)) {
			verify_game();
		}
		if(buttons & BUTTON_X) {
			needsRefresh = show_settings_view(VIEW_GAME, 0, config);
			infoPanel = DrawRepublish(infoPanel, draw_game_info(config));
		}
		if((buttons & BUTTON_Z) && devices[DEVICE_CONFIG] != NULL &&
			autoloadToggleConfirmed(&curFile.name[0], false)) {
			// Toggle autoload
			if(!strcmp(&swissSettings.autoload[0], &curFile.name[0])
			|| !fnmatch(&swissSettings.autoload[0], &curFile.name[0], FNM_PATHNAME)) {
				memset(&swissSettings.autoload[0], 0, PATHNAME_MAX);
			}
			else if(!fnmatch("dvd:/*.gcm", &curFile.name[0], FNM_PATHNAME)) {
				strcpy(&swissSettings.autoload[0], "dvd:/*.gcm");
			}
			else {
				strcpy(&swissSettings.autoload[0], &curFile.name[0]);
			}
			// Save config
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Saving autoload\205"));
			config_update_autoload(true);
			DrawDispose(msgBox);
			infoPanel = DrawRepublish(infoPanel, draw_game_info(config));
		}
		// Look for a cheats file based on the GameID
		if(buttons & BUTTON_Y) {
			// don't find cheats again if we've just found some for this game since it'll wipe selections.
			if(num_cheats == -1) {
				num_cheats = findCheats(false);
			}
			if(num_cheats != 0) {
				loadCheatsSelection();
				DrawCheatsSelector(curFile.name);
				saveCheatsSelection();
			}
		}
		while(padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
	}
	while(padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
	DrawDispose(infoPanel);
	return ret;
}

/* The devices the picker lists, in allDevices order: those that can do the
 * job and were detected, or with Z every one. With none detected it lists
 * every one, as Z does. */
static int selectorDevices(DEVICEHANDLER_INTERFACE **listed,
	u32 requiredFeatures, int *showAllDevices)
{
	int count = 0;

	for(int i = 0; i < MAX_DEVICES; i++) {
		DEVICEHANDLER_INTERFACE *device = allDevices[i];
		if(device != NULL && (device->features & requiredFeatures) &&
				(*showAllDevices || deviceHandler_getDeviceAvailable(device))) {
			listed[count++] = device;
		}
	}
	if(count == 0 && !*showAllDevices) {
		*showAllDevices = 1;
		return selectorDevices(listed, requiredFeatures, showAllDevices);
	}
	return count;
}

/* Where the focus lands in a list: on device, or else on the next device
 * listed after it. */
static int selectorFocus(DEVICEHANDLER_INTERFACE *const *listed, int count,
	const DEVICEHANDLER_INTERFACE *device)
{
	int start = 0;

	for(int i = 0; i < MAX_DEVICES; i++) {
		if(allDevices[i] == device) {
			start = i;
			break;
		}
	}
	for(int step = 0; step < MAX_DEVICES; step++) {
		for(int j = 0; j < count; j++) {
			if(listed[j] == allDevices[(start + step) % MAX_DEVICES]) {
				return j;
			}
		}
	}
	return 0;
}

static bool select_device_internal(int type)
{
	u32 requiredFeatures = (type == DEVICE_DEST) ? FEAT_WRITE:FEAT_READ;
	const u32 selectorButtons = BUTTON_RIGHT|BUTTON_LEFT|BUTTON_B|BUTTON_A|
		BUTTON_X|BUTTON_Y|BUTTON_L|BUTTON_R|BUTTON_Z;

	if(is_httpd_in_use()) {
		uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_INFO,"Can't load device while HTTP is processing!"));
		sleep(5);
		DrawDispose(msgBox);
		return false;
	}
	UIScene_Request(type == DEVICE_CUR ? UI_SCENE_SOURCE : UI_SCENE_LIBRARY);

	DEVICEHANDLER_INTERFACE *listed[MAX_DEVICES];
	int inAdvanced = 0, showAllDevices = 0;
	int savedExiSpeed = swissSettings.exiSpeed;
	int count = selectorDevices(listed, requiredFeatures, &showAllDevices);
	/* The focus, unwrapped: each step moves it by one, round and round. */
	int travel = selectorFocus(listed, count, devices[DEVICE_PREV]);
	int focus = travel;
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace = VIDEO_GetRetraceCount();

	UIMenuInput_Init(&menuInput);
	uiDrawObj_t *deviceSelectBox = DrawPublish(DrawDeviceSelector(type == DEVICE_DEST));
	while(1) {
		/* The stick changes the device too, and repeats while it is held;
		 * on the EXI speed's two values it steps once per push. */
		u32 stickPolicy = UI_MENU_INPUT_AXIS_HORIZONTAL |
			(inAdvanced ? 0u : UI_MENU_INPUT_REPEAT);
		uiMenuInputDirection_t analog;
		u32 btns;

		focus = ((travel % count) + count) % count;
		DrawUpdateDeviceSelector(deviceSelectBox, listed, count, travel,
			homeSourceLifecycleMounted() ? devices[DEVICE_CUR] : NULL,
			devices[DEVICE_CONFIG], showAllDevices, inAdvanced);
		while(1) {
			btns = padsButtonsHeld();
			analog = padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				stickPolicy, (btns & selectorButtons) != 0u);
			if((btns & selectorButtons) || analog != UI_MENU_INPUT_NONE) {
				break;
			}
			VIDEO_WaitVSync();
		}
		/* Cancellation wins every simultaneous selector chord and leaves all
		 * source configuration exactly as it entered. */
		if(btns & BUTTON_B) {
			swissSettings.exiSpeed = savedExiSpeed;
			if(type == DEVICE_DEST) {
				devices[type] = NULL;
			}
			DrawDispose(deviceSelectBox);
			if(type == DEVICE_DEST) {
				UIScene_Request(UI_SCENE_LIBRARY);
			}
			menuaudio_select();
			homeDrainSelectorInput();
			return false;
		}
		if(analog == UI_MENU_INPUT_RIGHT) {
			btns |= BUTTON_RIGHT;
		}
		else if(analog == UI_MENU_INPUT_LEFT) {
			btns |= BUTTON_LEFT;
		}
		if((btns & BUTTON_Y) && listed[focus]->details) {
			char *deviceDetails = listed[focus]->details(listed[focus]->initial);
			if(deviceDetails) {
				menuaudio_select();
				uiDrawObj_t *deviceDetailBox = DrawPublish(DrawTooltip(deviceDetails));
				while (padsButtonsHeld() & BUTTON_Y){ VIDEO_WaitVSync (); }
				while (!((padsButtonsHeld() & BUTTON_Y) || (padsButtonsHeld() & BUTTON_B))){ VIDEO_WaitVSync (); }
				DrawDispose(deviceDetailBox);
				free(deviceDetails);
			}
		}
		if((btns & BUTTON_X) && (listed[focus]->features & FEAT_EXI_SPEED)) {
			inAdvanced ^= 1;
			menuaudio_select();
		}
		if(btns & BUTTON_Z) {
			DEVICEHANDLER_INTERFACE *shown = listed[focus];
			showAllDevices ^= 1;
			count = selectorDevices(listed, requiredFeatures, &showAllDevices);
			travel = focus = selectorFocus(listed, count, shown);
			if(listed[focus] != shown) {
				inAdvanced = 0;
			}
			menuaudio_select();
		}
		if(inAdvanced) {
			if(btns & (BUTTON_RIGHT|BUTTON_LEFT)) {
				swissSettings.exiSpeed ^= 1;
				menuaudio_blip();
			}
		}
		else if(count > 1 && (btns & (BUTTON_RIGHT|BUTTON_R|BUTTON_LEFT|BUTTON_L))) {
			travel += (btns & (BUTTON_RIGHT|BUTTON_R)) ? 1 : -1;
			menuaudio_blip();
		}
		if(btns & BUTTON_A) {
			if(!inAdvanced) {
				menuaudio_select();
				break;
			}
			inAdvanced = 0;
		}
		while((padsButtonsHeld() & selectorButtons) != 0u) {
			(void)padsMenuInputPoll(&menuInput,
				menuInputElapsedMicroseconds(&menuInputRetrace),
				stickPolicy, true);
			VIDEO_WaitVSync();
		}
	}
	while ((padsButtonsHeld() & BUTTON_A)){ VIDEO_WaitVSync (); }
	DEVICEHANDLER_INTERFACE *selectedDevice = listed[focus];
	if(type == DEVICE_CUR) {
		/* The live source survived the selector. Only now that A has confirmed
		 * do we invalidate files and remount, including same-source EXI changes. */
		sourceCommit(selectedDevice);
	}
	else {
		if(devices[type] != NULL) {
			// Don't deinit our current device when selecting a destination device
			if(!(type == DEVICE_DEST && devices[type] == devices[DEVICE_CUR])) {
				devices[type]->deinit(devices[type]->initial);
			}
		}
		devices[type] = selectedDevice;
	}
	if(showAllDevices && (selectedDevice->location & (LOC_MEMCARD_SLOT_A | LOC_MEMCARD_SLOT_B | LOC_SERIAL_PORT_2))) {
		EXI_ProbeReset();
	}
	DrawDispose(deviceSelectBox);
	if(type == DEVICE_DEST) {
		UIScene_Request(UI_SCENE_LIBRARY);
	}
	return true;
}

void select_device(int type)
{
	(void)select_device_internal(type);
}

/* The Source becomes device, unmounted: the old one's listing and posters
 * go, it is unmounted, and Home records both. sourceMount mounts the new
 * one. The device picker and the File Browser's L and Y change it here. */
static void sourceCommit(DEVICEHANDLER_INTERFACE *device)
{
	if(devices[DEVICE_CUR] != NULL) {
		freeFiles();
		DrawGameflowCancelPosters();
		devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);
		homeSourceRecord(devices[DEVICE_CUR], UI_HOME_SOURCE_MOUNT_UNMOUNTED);
	}
	else {
		homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
	}
	devices[DEVICE_CUR] = device;
	homeSourceRecord(device, UI_HOME_SOURCE_MOUNT_UNMOUNTED);
}

/* Mounts the Source just chosen and opens its top folder. A Source that
 * won't mount says why, is let go and the Source picker opens next
 * (needsDeviceChange): false then. */
static bool sourceMount(void)
{
	uiDrawObj_t *msgBox;
	s32 ret;

	needsRefresh = 1;
	memcpy(&curDir, devices[DEVICE_CUR]->initial, sizeof(file_handle));
	msgBox = DrawPublish(DrawProgressBar(true, 0, "Setting up device"));
	ret = devices[DEVICE_CUR]->init(devices[DEVICE_CUR]->initial);
	if(ret) {
		homeSourceRecord(devices[DEVICE_CUR],
			UI_HOME_SOURCE_MOUNT_UNMOUNTED);
		needsDeviceChange = 1;
		if(ret == ENODEV) {	// for completely removed devices vs something like the disc drive without a disc.
			deviceHandler_setDeviceAvailable(devices[DEVICE_CUR], false);
		}
		char* statusMsg = devices[DEVICE_CUR]->status(devices[DEVICE_CUR]->initial);
		msgBox = DrawRepublish(msgBox, DrawMessageBox(D_FAIL, statusMsg ? statusMsg : strerror(ret)));
		sleep(2);
		DrawDispose(msgBox);
		/* The confirmed replacement never mounted, so it cannot be
		 * treated as the preserved live source on the next selector. */
		devices[DEVICE_CUR] = NULL;
		homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
		return false;
	}
	DrawDispose(msgBox);
	deviceHandler_setDeviceAvailable(devices[DEVICE_CUR], true);
	homeSourceRecord(devices[DEVICE_CUR],
		UI_HOME_SOURCE_MOUNT_MOUNTED);
	return true;
}

void menu_loop()
{
	menuaudio_init();
	homeSourceObserveStartup();
	while(padsButtonsHeld() & BUTTON_A) { VIDEO_WaitVSync (); }
	/* Persisted settings are authoritative by this point, while the selector
	 * has not yet published. Starting here keeps the visible reveal aligned
	 * with the interactive menu and its audio lifecycle. */
	UIScene_Activate();
	if(gameflowStartupSurfacePending) {
		gameflowStartupSurfacePending = false;
		if(UIGameflowLibrary_ShouldStartHome(
			swissSettings.autoload[0] != '\0',
			swissSettings.recent[0][0] != '\0',
			swissSettings.recentListLevel > 1)) {
			homeApplyStartupSource();
			/* Normal and Lazy boots reveal Home, independent of any earlier
			 * autoboot/firmware probe. Keep explicit Autoload and Recent=On
			 * requests on Swiss's established direct-entry path. */
			if(devices[DEVICE_CUR] != NULL &&
				devices[DEVICE_CUR]->initial != NULL) {
				memcpy(&curDir, devices[DEVICE_CUR]->initial,
					sizeof(file_handle));
				needsRefresh = 1;
			}
			curMenuLocation = ON_OPTIONS;
			gameflowStartupHomeReleasePending = true;
		}
	}
	// We don't care if a subsequent device is "default"
	if(needsDeviceChange) {
		DEVICEHANDLER_INTERFACE *previousDevice = devices[DEVICE_CUR];
		int refreshBeforeSelection = needsRefresh;
		if(previousDevice) {
			devices[DEVICE_PREV] = previousDevice;
		}
		needsDeviceChange = 0;
		/* The picker reads held buttons: its A or B is not the list's. */
		filesKeepPresses = false;
		homePublish(curMenuLocation == ON_OPTIONS);
		bool deviceConfirmed = select_device_internal(DEVICE_CUR);
		UIScene_Request(UI_SCENE_HOME);
		/* A confirmed source is always remounted, including same-source
		 * EXI changes. Cancellation never reaches this lifecycle. */
		if(deviceConfirmed && devices[DEVICE_CUR] != NULL && !sourceMount()) {
			return;
		}
		else if(!deviceConfirmed) {
			if(homeSourceLifecycleMounted()) {
				needsRefresh = refreshBeforeSelection;
			}
			else {
				/* A detected handler whose init failed is not a live source.
				 * Refusal/cancel may discard that pointer, but must not run a
				 * destructor intended for a mounted source. */
				devices[DEVICE_CUR] = NULL;
				homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
				needsRefresh = 0;
			}
		}
		if(devices[DEVICE_CUR] == NULL) {
			curMenuLocation=ON_OPTIONS;
		}
		if(deviceConfirmed && homeStateReady &&
				homeState.surface == UI_HOME_SURFACE_SOURCE) {
			(void)UIHome_Apply(&homeState, UI_HOME_INPUT_BACK,
				homeCapabilities());
		}
	}
	if(gameflowStartupHomeReleasePending) {
		/* A held B may have skipped slow device detection. Drain it before the
		 * root Home surface becomes interactive. */
		while(padsButtonsHeld() & BUTTON_B) { VIDEO_WaitVSync (); }
		gameflowStartupHomeReleasePending = false;
	}

	uiDrawObj_t *filePanel = NULL;
	while(1) {
		homePublishBrowserTransition(&filePanel);
		if(devices[DEVICE_CUR] != NULL && needsRefresh) {
			int postScanLocation = curMenuLocation;

			curMenuLocation=ON_OPTIONS;
			curSelection=0;
			/* The File Browser says its folder is being read (a disc
			 * spinning up, a network share) while it is. */
			(void)DrawUpdateFilesReading(filePanel, UI_FILES_LEFT);
			scanFiles();
			if(getCurrentDirEntryCount()<=0) {
				homeLibraryEntryPending = false;
				filesOtherRelease();
				DrawGameflowCancelPosters();
				devices[DEVICE_PREV] = devices[DEVICE_CUR];
				devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);
				homeSourceRecord(devices[DEVICE_CUR],
					UI_HOME_SOURCE_MOUNT_UNMOUNTED);
				devices[DEVICE_CUR] = NULL;
				homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);
				needsDeviceChange=1;
				curMenuLocation = postScanLocation;
				break;
			}
			needsRefresh = 0;
			curMenuLocation = postScanLocation;
			homePublishBrowserTransition(&filePanel);
			if(homeLibraryEntryPending) {
				homeLibraryEntryPending = false;
				if(gameflowEnterLibraryFromHome()) {
					curMenuLocation = ON_FILLIST;
					continue;
				}
			}
		}
		if(devices[DEVICE_CUR] != NULL && curMenuLocation==ON_FILLIST) {
			/* A Library location with a game in it is the Library's; every
			 * other folder is the File Browser's. The File Browser asks for
			 * no scene: the Home cube stays where the face or the Library
			 * left it, as Memory Cards hands it over. */
			if(!gameflowListFallback && gameflowLibraryMode(getSortedDirEntries(),
					getSortedDirEntryCount()) != UI_GAMEFLOW_LIBRARY_NONE) {
				filesOtherRelease();
				filesKeepPresses = false;
				UIScene_RequestLibraryLayout(gameflowLayout());
				UIScene_Request(UI_SCENE_LIBRARY);
				filePanel = renderFileCarousel(getSortedDirEntries(),
					gameflowLibraryEntries(getSortedDirEntries(),
						getSortedDirEntryCount()), filePanel);
			}
			else {
				filePanel = renderFileList(getSortedDirEntries(),
					getSortedDirEntryCount(), filePanel);
			}
			while(padsButtonsHeld() & (BUTTON_B | BUTTON_A | BUTTON_RIGHT | BUTTON_LEFT | BUTTON_START)) {
				VIDEO_WaitVSync (); 
			}
		}
		else if (curMenuLocation==ON_OPTIONS) {
			const u32 homeButtons = HOME_CONFIRMATION_BUTTONS | BUTTON_Y;
			uiHomeCapabilities_t capabilities;
			uiHomeInput_t navigation = UI_HOME_INPUT_NONE;
			uiHomeInput_t command = UI_HOME_INPUT_NONE;
			uiHomeEffect_t effect = UI_HOME_EFFECT_NONE;
			uiMenuInputDirection_t analog = UI_MENU_INPUT_NONE;
			u32 allowedAxes;
			u32 btns;
			uint32_t revision;
			bool navigated = false;

			homeFileBrowser = false;
			UIScene_Request(UI_SCENE_HOME);
			homePublish(true);
			if(homeState.surface == UI_HOME_SURFACE_RING) {
				allowedAxes = UI_MENU_INPUT_AXIS_BOTH;
			}
			else if(homeState.surface == UI_HOME_SURFACE_RESTART_CONFIRM) {
				allowedAxes = UI_MENU_INPUT_AXIS_BOTH;
			}
			else {
				allowedAxes = UI_MENU_INPUT_AXIS_VERTICAL;
			}
			while(1) {
				btns = padsButtonsHeld();
				analog = padsMenuInputPoll(&homeMenuInput,
					menuInputElapsedMicroseconds(&homeMenuInputRetrace),
					allowedAxes, (btns & homeButtons) != 0u);
				if((btns & homeButtons) || analog != UI_MENU_INPUT_NONE) {
					break;
				}
				VIDEO_WaitVSync();
			}

			capabilities = homeCapabilities();
			/* Back owns the sample. It must not rotate or move a row first. */
			if(!(btns & BUTTON_B)) {
				if(homeState.surface == UI_HOME_SURFACE_RING) {
					if((btns & BUTTON_LEFT) || analog == UI_MENU_INPUT_LEFT) {
						navigation = UI_HOME_INPUT_LEFT;
					}
					else if((btns & BUTTON_RIGHT) || analog == UI_MENU_INPUT_RIGHT) {
						navigation = UI_HOME_INPUT_RIGHT;
					}
					else if((btns & BUTTON_UP) || analog == UI_MENU_INPUT_UP) {
						navigation = UI_HOME_INPUT_UP;
					}
					else if((btns & BUTTON_DOWN) || analog == UI_MENU_INPUT_DOWN) {
						navigation = UI_HOME_INPUT_DOWN;
					}
				}
				else if(homeState.surface == UI_HOME_SURFACE_RESTART_CONFIRM) {
					if((btns & BUTTON_LEFT) || (btns & BUTTON_UP) ||
							analog == UI_MENU_INPUT_LEFT ||
							analog == UI_MENU_INPUT_UP) {
						navigation = UI_HOME_INPUT_LEFT;
					}
					else if((btns & BUTTON_RIGHT) || (btns & BUTTON_DOWN) ||
							analog == UI_MENU_INPUT_RIGHT ||
							analog == UI_MENU_INPUT_DOWN) {
						navigation = UI_HOME_INPUT_RIGHT;
					}
				}
				else {
					if((btns & BUTTON_UP) || analog == UI_MENU_INPUT_UP) {
						navigation = UI_HOME_INPUT_UP;
					}
					else if((btns & BUTTON_DOWN) ||
							analog == UI_MENU_INPUT_DOWN) {
						navigation = UI_HOME_INPUT_DOWN;
					}
				}
				if(navigation != UI_HOME_INPUT_NONE) {
					revision = homeState.revision;
					(void)UIHome_Apply(&homeState, navigation, capabilities);
					navigated = revision != homeState.revision;
					if(navigated) {
						homePublish(true);
						menuaudio_blip();
					}
				}
			}

			/* One sample performs one semantic action. A/Start following an
			 * accepted navigation waits for the next fresh sample. */
			if(btns & BUTTON_B) {
				command = UI_HOME_INPUT_BACK;
			}
			else if(btns & BUTTON_A) {
				command = UI_HOME_INPUT_ACTIVATE;
			}
			else if(btns & BUTTON_START) {
				command = UI_HOME_INPUT_RECENT;
			}
			else if(btns & BUTTON_Y) {
				command = UI_HOME_INPUT_SETTINGS;
			}
			if(command != UI_HOME_INPUT_NONE && !navigated) {
				revision = homeState.revision;
				effect = UIHome_Apply(&homeState, command, capabilities);
				if(revision != homeState.revision ||
						effect != UI_HOME_EFFECT_NONE) {
					homePublish(true);
					menuaudio_select();
				}
				homeDispatchEffect(effect);
			}
			/* Digital actions retain their exact one-action-per-press contract.
			 * Home analog is also one-shot until neutral; repeat is reserved for
			 * longer browser lists where sustained travel is intentional. */
			while((padsButtonsHeld() & homeButtons) != 0u) {
				(void)padsMenuInputPoll(&homeMenuInput,
					menuInputElapsedMicroseconds(&homeMenuInputRetrace),
					allowedAxes, true);
				VIDEO_WaitVSync();
			}
		}
		/* load_file() can be reached from every browser and from Recent. Run
		 * the destructive updater only after those callers release snapshots. */
		if(homeFlippyUpdatePending) {
			if(filePanel != NULL) {
				if(filePanel == filesPage) filesPage = NULL;
				DrawDispose(filePanel);
				filePanel = NULL;
			}
			homeRunFlippyUpdate();
		}
		if(needsDeviceChange) {
			break;
		}
	}
	if(filePanel != NULL) {
		if(filePanel == filesPage) filesPage = NULL;
		DrawDispose(filePanel);
	}
}
