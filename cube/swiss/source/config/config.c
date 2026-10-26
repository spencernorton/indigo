#include <argz.h>
#include <langinfo.h>
#include <stdcountof.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <gccore.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "config.h"
#include "settings.h"
#include "bba.h"
#include "deviceHandler-FAT.h"
#include "ui_game_history.h"
#include "ui_gameflow_library.h"
#include "ui_folder.h"

// This is an example Swiss settings entry (sits at the top of global.ini)
//!!Swiss Settings Start!!
//SD/IDE Speed=32MHz
//Swiss Video Mode=Auto
//Enable Debug=No
//Force No DVD Drive Mode=No
//Hide Unknown file types=No
//Stop DVD Motor on startup=Yes
//Enable WiiRD debug=Yes
//Enable File Management=No
//SMBUserName=user
//SMBPassword=password
//SMBShareName=nas
//SMBHostIP=192.168.1.32
//AutoCheats=Yes
//InitNetwork=No
//IGRType=Disabled
//FTPUserName=user
//FTPPassword=password
//FTPHostIP=192.168.1.32
//FTPPort=21
//FTPUsePasv=No
//!!Swiss Settings End!!

// This is an example game entry
//ID=GAFE
//Name=Animal Crossing (NTSC)
//Comment=Playable without issues
//Status=Working
//Force Video Mode=Progressive
//Mute Audio Streaming=Yes
//No Disc Mode=Yes
//Force Widescreen=Yes

#define SWISS_SETTINGS_FILENAME_LEGACY "swiss.ini"
#define SWISS_SETTINGS_FILENAME "global.ini"
#define SWISS_RECENTLIST_FILENAME "recent.ini"
static const char *playHistoryFilenames[2] = {
	"swiss/settings/play-history-0.ini", "swiss/settings/play-history-1.ini"
};

static uiGameHistory_t playHistory;
static DEVICEHANDLER_INTERFACE *playHistoryDevice;
static int playHistorySlot = -1;
#define SWISS_BASE_DIR "swiss"
#define SWISS_SETTINGS_DIR "swiss/settings"
#define SWISS_GAME_SETTINGS_DIR "swiss/settings/game"

/* Whether global.ini holds the settings: it was read at boot or a save has
 * written it since. Setup > Storage says so. */
static bool globalFileLoaded;
static uiFolderColors_t folderColors;

uint8_t config_folder_color(const char *path) {
	return UIFolder_GetColor(&folderColors, path);
}

bool config_global_file_loaded(void) {
	return globalFileLoaded;
}

// Tries to init the current config device
bool config_set_device() {
	// Set the current config device to whatever the current configDeviceId is
	DEVICEHANDLER_INTERFACE *configDevice = getDeviceByUniqueId(swissSettings.configDeviceId);
	devices[DEVICE_CONFIG] = NULL;
	if(configDevice != NULL) {
		if(configDevice->test()) {
			deviceHandler_setDeviceAvailable(configDevice, true);
			devices[DEVICE_CONFIG] = configDevice;
		}
	}
	
	// Not available or not a writable device? That's too bad.
	if(devices[DEVICE_CONFIG] == NULL) {
		return false;
	}
	//print_debug("Save device is %s\n", devices[DEVICE_CONFIG]->deviceName);
	deviceHandler_setStatEnabled(0);
	// If we're not using this device already, init it.
	if(devices[DEVICE_CONFIG] != devices[DEVICE_CUR]) {
		print_debug("Save device is not current, current is (%s)\n", devices[DEVICE_CUR] == NULL ? "NULL":devices[DEVICE_CUR]->deviceName);
		if(devices[DEVICE_CONFIG]->init(devices[DEVICE_CONFIG]->initial)) {
			print_debug("Save device failed to init\n");
			deviceHandler_setStatEnabled(1);
			return false;
		}
	}
	deviceHandler_setStatEnabled(1);
	return true;
}

void config_unset_device() {
	if(devices[DEVICE_CONFIG] != devices[DEVICE_CUR]) {
		devices[DEVICE_CONFIG]->deinit(devices[DEVICE_CONFIG]->initial);
	}
}

/* A save writes NAME.new and closes it, then swaps it in for NAME, so power
 * lost at any point leaves one whole copy: NAME until the swap, NAME.new
 * alone during it. */
static void config_name_new(char name[PATHNAME_MAX]) {
	size_t used = strlen(name);
	snprintf(name + used, PATHNAME_MAX - used, ".new");
}

// Reads from a file and returns a populated buffer, NULL if anything goes wrong.
char* config_file_read(char* filename) {
	char* readBuffer = NULL;
	file_handle *configFile = (file_handle*)calloc(1, sizeof(file_handle));
	concat_path(configFile->name, devices[DEVICE_CONFIG]->initial->name, filename);
	print_debug("config_file_read: looking for %s\n", configFile->name);
	if(devices[DEVICE_CONFIG]->statFile(configFile)) {
		/* A save that lost power mid-swap left the settings in NAME.new. */
		config_name_new(configFile->name);
	}
	if(!devices[DEVICE_CONFIG]->statFile(configFile) && configFile->size) {
		readBuffer = (char*)calloc(1, configFile->size + 1);
		if (readBuffer) {
			print_debug("config_file_read: reading %i byte file\n", configFile->size);
			/* A short read is a failed read: a save must not mistake part
			 * of a file for all of it. */
			if(devices[DEVICE_CONFIG]->readFile(configFile, readBuffer, configFile->size) != (s32)configFile->size) {
				free(readBuffer);
				readBuffer = NULL;
			}
			devices[DEVICE_CONFIG]->closeFile(configFile);
		}
	}
	free(configFile);
	return readBuffer;
}

/* Hands every per-game settings file to visit: its four-character game ID
 * and its text, which visit may change and is freed afterwards. The
 * settings device is mounted once for all of them. Returns the files read,
 * or -1 when there is no settings device. */
int config_each_game_file(void (*visit)(const char *gameId, char *text, void *context),
	void *context) {
	file_handle dir;
	file_handle *entries = NULL;
	int count, i, read = 0;

	if(visit == NULL || !config_set_device()) {
		return -1;
	}
	memset(&dir, 0, sizeof(dir));
	concat_path(dir.name, devices[DEVICE_CONFIG]->initial->name, SWISS_GAME_SETTINGS_DIR);
	count = devices[DEVICE_CONFIG]->readDir(&dir, &entries, -1);
	for(i = 0; i < count && entries != NULL; i++) {
		const char *leaf = getRelativeName(entries[i].name);
		char path[PATHNAME_MAX];
		char *text;

		/* <ID4>.ini; nothing else in the folder is a game's settings. */
		if(entries[i].fileType != IS_FILE || strlen(leaf) != 8u ||
			strcasecmp(leaf + 4, ".ini") != 0) {
			continue;
		}
		concatf_path(path, SWISS_GAME_SETTINGS_DIR, "%.4s.ini", leaf);
		text = config_file_read(path);
		if(text != NULL) {
			visit(leaf, text, context);
			free(text);
			read++;
		}
	}
	free(entries);
	config_unset_device();
	return read;
}

int config_file_write(char* filename, char* contents) {
	DEVICEHANDLER_INTERFACE *device = devices[DEVICE_CONFIG];
	file_handle *configFile = (file_handle*)calloc(1, sizeof(file_handle));
	file_handle *newFile = (file_handle*)calloc(1, sizeof(file_handle));
	int written = 0;

	if(configFile == NULL || newFile == NULL) {
		free(configFile);
		free(newFile);
		return 0;
	}
	concat_path(configFile->name, device->initial->name, filename);
	strcpy(newFile->name, configFile->name);
	config_name_new(newFile->name);

	u32 len = strlen(contents);
	print_debug("config_file_write: writing %i bytes to %s\n", len, configFile->name);
	/* NAME.new alone is the settings: make it NAME before writing over it. */
	if(device->statFile(configFile) && !device->statFile(newFile)) {
		device->renameFile(newFile, configFile->name);
		strcpy(newFile->name, configFile->name);
		config_name_new(newFile->name);
	}
	device->deleteFile(newFile);
	if(device->writeFile(newFile, contents, len) == len &&
		!device->closeFile(newFile)) {
		/* FatFs will not rename onto a file that exists. */
		device->deleteFile(configFile);
		written = !device->renameFile(newFile, configFile->name);
	}
	else {
		device->closeFile(newFile);
	}
	free(newFile);
	free(configFile);
	return written;
}

/* Slots alternate so a short/failed write cannot destroy the last complete
 * record. Startup accepts only bounded, checksummed files and chooses the
 * newest valid generation. Unknown read failures never mean "never played". */
static int config_read_play_history_slot(DEVICEHANDLER_INTERFACE *device,
	int slot, uiGameHistory_t *history) {
	file_handle file = {0};
	char *buffer;
	s32 status;
	int result = -1;
	UIGameHistory_Init(history, false);
	concat_path(file.name, device->initial->name, playHistoryFilenames[slot]);
	file.device = device;
	status = device->statFile(&file);
	if(status != 0) {
		/* Missing-file codes are known only for FAT-backed config devices. */
		if(device->statFile == deviceHandler_FAT_statFile &&
			(status == FR_NO_FILE || status == FR_NO_PATH)) result = 0;
		device->closeFile(&file);
		return result;
	}
	if(file.fileType != IS_FILE || file.size == 0u ||
		file.size >= UI_GAME_HISTORY_FILE_CAPACITY) {
		device->closeFile(&file);
		return -1;
	}
	size_t size = file.size;
	buffer = malloc(size);
	if(buffer != NULL) {
		s32 read = device->readFile(&file, buffer, (u32)size);
		s32 closed = device->closeFile(&file);
		if(read == (s32)size && closed == 0 &&
			UIGameHistory_Parse(history, buffer, size)) result = 1;
		free(buffer);
	} else {
		device->closeFile(&file);
	}
	return result;
}

static void config_load_play_history(void) {
	DEVICEHANDLER_INTERFACE *device = devices[DEVICE_CONFIG];
	uiGameHistory_t first, second;
	int a, b;
	UIGameHistory_Init(&playHistory, false);
	playHistoryDevice = device;
	playHistorySlot = -1;
	if(device == NULL || device->initial == NULL || device->statFile == NULL ||
		device->readFile == NULL || device->closeFile == NULL) return;
	a = config_read_play_history_slot(device, 0, &first);
	b = config_read_play_history_slot(device, 1, &second);
	playHistorySlot = UIGameHistory_SelectSlot(&first, &second);
	if(playHistorySlot >= 0) playHistory = playHistorySlot == 0 ? first : second;
	else if(a == 0 && b == 0) UIGameHistory_Init(&playHistory, true);
}

uint64_t config_last_played(const char *gameId, size_t gameIdLength,
	bool *available) {
	bool ready = playHistory.available && playHistoryDevice != NULL &&
		playHistoryDevice == devices[DEVICE_CONFIG];
	if(available != NULL) *available = ready;
	return ready ? UIGameHistory_Find(&playHistory, gameId, gameIdLength) : 0u;
}

void config_record_game_handoff(const char *gameId, size_t gameIdLength) {
	DEVICEHANDLER_INTERFACE *device = devices[DEVICE_CONFIG];
	time_t now = time(NULL);
	uiGameHistory_t next;
	file_handle *file;
	char *serialized;
	size_t size;
	int slot;
	bool written = false;
	if(now < 0 || !UIGameHistory_ValidTime((uint64_t)now)) return;
	/* Called before the game's setup, like the recent list. Never probe, mount,
	 * change devices or reload history here: optional persistence uses the
	 * active FAT mount only. */
	if(device == NULL || device != devices[DEVICE_CUR] ||
		device != playHistoryDevice || !playHistory.available ||
		device->initial == NULL || device->context == NULL ||
		device->statFile != deviceHandler_FAT_statFile ||
		device->writeFile != deviceHandler_FAT_writeFile ||
		!(device->features & FEAT_WRITE) ||
		(device->quirks & QUIRK_GCLOADER_WRITE_CONFLICT)) return;
	next = playHistory;
	if(!UIGameHistory_Record(&next, gameId, gameIdLength, (uint64_t)now)) return;
	serialized = malloc(UI_GAME_HISTORY_FILE_CAPACITY);
	file = calloc(1, sizeof(*file));
	if(serialized == NULL || file == NULL) { free(serialized); free(file); return; }
	size = UIGameHistory_Serialize(&next, serialized, UI_GAME_HISTORY_FILE_CAPACITY);
	slot = playHistorySlot == 0 ? 1 : 0;
	if(size != 0u) {
		concat_path(file->name, device->initial->name, playHistoryFilenames[slot]);
		file->device = device;
		/* FAT write opens with CREATE_ALWAYS. Only the inactive slot is ever
		 * replaced; the selected valid slot is untouched until next launch. */
		s32 count = device->writeFile(file, serialized, (u32)size);
		/* The FAT writer created this unmapped handle; bypass wrappers that
		 * can otherwise remap a GC Loader disc while closing mapped files. */
		s32 closed = deviceHandler_FAT_closeFile(file);
		written = count == (s32)size && closed == 0;
	}
	free(file);
	free(serialized);
	if(written) { playHistory = next; playHistorySlot = slot; }
	else {
		/* Preserve the disk recovery slot but do not present stale cached data
		 * as a successful record if this optional write did not complete. */
		UIGameHistory_Init(&playHistory, false);
		print_debug("Play history write failed; history unavailable.\n");
	}
}

void config_file_delete(char* filename) {
	file_handle *configFile = (file_handle*)calloc(1, sizeof(file_handle));
	concat_path(configFile->name, devices[DEVICE_CONFIG]->initial->name, filename);
	print_debug("config_file_delete: deleting %s\n", configFile->name);
	devices[DEVICE_CONFIG]->deleteFile(configFile);
	/* And a NAME.new, which a read would take for it. */
	config_name_new(configFile->name);
	devices[DEVICE_CONFIG]->deleteFile(configFile);
	free(configFile);
}

/* Keys a global.ini may still carry from older Swiss versions. They are read
 * but never written, so a save drops them instead of keeping a stale copy
 * that could override the current key. */
static const char *const globalOldKeys[] = {
	"Enable Debug", "USB Gecko debug output", "Stop DVD Motor on startup", "Library Folder Colors",
	"Hide Apps Face", NULL
};

/* Every key a game's file can hold. A save drops the ones it no longer
 * writes: a setting back at its default. */
static const char *const gameFileKeys[] = {
	"ID", "Name", "Comment", "Status", "Game Language", "Force Video Mode",
	"Force Horizontal Scale", "Force Vertical Offset", "Force Vertical Filter",
	"Force Field Rendering", "Fix Pixel Center", "Disable Alpha Dithering",
	"Force Anisotropic Filter", "Force Widescreen", "Force Polling Rate",
	"Invert Camera Stick", "Swap Camera Stick", "Digital Trigger Level",
	"Emulate Audio Streaming", "Emulate Read Speed", "Emulate Broadband Adapter",
	"Disable Memory Card", "Disable Hypervisor", "Prefer Clean Boot",
	"RetroTINK-4K Profile", NULL
};

#define CONFIG_LOST_COMMENT "# Anything written in here will be lost!"
#define CONFIG_KEPT_COMMENT "# Indigo keeps comments and unknown keys when it saves."

typedef struct {
	const char *text;
	size_t length;
	size_t keyLength;       /* 0 when the line is not Key=Value */
} configLine_t;

/* Splits text into lines without their endings; a final line ending does
 * not start another line. Returns the count, or -1 when there are too many. */
static int config_split_lines(const char *text, configLine_t *lines, int max)
{
	int count = 0;

	while(*text != '\0') {
		const char *end = strchr(text, '\n');
		size_t length = end ? (size_t)(end - text) : strlen(text);
		const char *eq;

		if(count == max) return -1;
		lines[count].text = text;
		lines[count].length = length > 0 && text[length - 1] == '\r' ? length - 1 : length;
		eq = lines[count].length > 0 && text[0] != '#' ?
			memchr(text, '=', lines[count].length) : NULL;
		lines[count].keyLength = eq ? (size_t)(eq - text) : 0;
		count++;
		if(end == NULL) break;
		text = end + 1;
	}
	return count;
}

/* An upper bound on the lines config_split_lines finds in text. */
static int config_count_lines(const char *text)
{
	int count = 1;

	for(; *text != '\0'; text++) {
		count += *text == '\n';
	}
	return count;
}

static bool config_line_is(const configLine_t *line, const char *text)
{
	return line->length == strlen(text) && !memcmp(line->text, text, line->length);
}

static bool config_key_listed(const configLine_t *line, const char *const *keys)
{
	for(; keys != NULL && *keys != NULL; keys++) {
		if(strlen(*keys) == line->keyLength &&
			!memcmp(*keys, line->text, line->keyLength)) {
			return true;
		}
	}
	return false;
}

static char *config_append_line(char *out, const configLine_t *line)
{
	memcpy(out, line->text, line->length);
	out += line->length;
	*out++ = '\r';
	*out++ = '\n';
	return out;
}

/* Saving rewrites a settings file from memory. When one already exists, keep
 * what a person wrote in it: comments, blank lines and keys Swiss doesn't
 * know stay where they are, and known keys take their new value in place.
 * Known keys the new text no longer has (older names, or a game setting back
 * at its default) are dropped. Keys the file lacked go just before its End
 * marker, or at the end. Returns a malloc'd string, or NULL on failure so
 * the caller writes the generated text instead. */
char *config_merge_file(const char *existing, const char *generated,
	const char *const *alsoKnown)
{
	configLine_t *oldLines, *newLines;
	bool *written;
	int oldMax, newMax, oldCount, newCount, i, j;
	char *merged = NULL, *out;

	if(existing == NULL || generated == NULL) return NULL;
	/* Sized from the text, so a long, well-commented file keeps its notes. */
	oldMax = config_count_lines(existing);
	newMax = config_count_lines(generated);
	oldLines = malloc((size_t)oldMax * sizeof(*oldLines));
	newLines = malloc((size_t)newMax * sizeof(*newLines));
	written = calloc((size_t)newMax, sizeof(*written));
	if(oldLines == NULL || newLines == NULL || written == NULL) goto done;
	oldCount = config_split_lines(existing, oldLines, oldMax);
	newCount = config_split_lines(generated, newLines, newMax);
	if(oldCount <= 0 || newCount < 0) goto done;
	/* Each output line is an old line (any of which may become the kept
	 * comment) or a generated one, plus a CRLF. */
	merged = malloc(strlen(existing) + strlen(generated) +
		(size_t)(oldCount + newCount) * (2u + sizeof(CONFIG_KEPT_COMMENT)) + 1u);
	if(merged == NULL) goto done;
	out = merged;
	for(i = 0; i < oldCount; i++) {
		const configLine_t *line = &oldLines[i];

		if(line->keyLength == 0) {
			if(config_line_is(line, "#!!Swiss Settings End!!")) {
				for(j = 0; j < newCount; j++) {
					if(newLines[j].keyLength != 0 && !written[j]) {
						out = config_append_line(out, &newLines[j]);
						written[j] = true;
					}
				}
			}
			if(config_line_is(line, CONFIG_LOST_COMMENT)) {
				configLine_t kept = {CONFIG_KEPT_COMMENT, sizeof(CONFIG_KEPT_COMMENT) - 1, 0};
				out = config_append_line(out, &kept);
			}
			else {
				out = config_append_line(out, line);
			}
			continue;
		}
		for(j = 0; j < newCount; j++) {
			if(newLines[j].keyLength == line->keyLength &&
				!memcmp(newLines[j].text, line->text, line->keyLength)) {
				break;
			}
		}
		if(j < newCount) {
			/* A repeated key keeps only its first place. */
			if(!written[j]) {
				out = config_append_line(out, &newLines[j]);
				written[j] = true;
			}
		}
		else if(!config_key_listed(line, alsoKnown)) {
			out = config_append_line(out, line);
		}
	}
	for(j = 0; j < newCount; j++) {
		if(newLines[j].keyLength != 0 && !written[j]) {
			out = config_append_line(out, &newLines[j]);
		}
	}
	*out = '\0';
done:
	free(written);
	free(newLines);
	free(oldLines);
	return merged;
}

int config_update_global(bool checkConfigDevice) {
	/* Settings files change: the Library's custom marks read them again. */
	settings_game_files_forget();
	if(checkConfigDevice && !config_set_device()) return 0;

	char *configString = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&configString, &len);
	if(!fp) return 0;

	// Write out Swiss settings
	fprintf(fp, "# Swiss Configuration File!\r\n" CONFIG_KEPT_COMMENT "\r\n\r\n#!!Swiss Settings Start!!\r\n");
	fprintf(fp, "SD/IDE Speed=%s\r\n", swissSettings.exiSpeed ? "32MHz":"16MHz");
	fprintf(fp, "System Boot Mode=%s\r\n", swissSettings.sramBoot ? "Production":"Default");
	fprintf(fp, "System Sound=%s\r\n", swissSettings.sramStereo ? "Stereo":"Mono");
	fprintf(fp, "System Video=%s\r\n", sramVideoStr[swissSettings.sramVideo]);
	fprintf(fp, "Screen Position=%+hi\r\n", swissSettings.sramHOffset);
	fprintf(fp, "System Language=%s\r\n", sramLanguageStr[swissSettings.sramLanguage]);
	fprintf(fp, "Swiss Video Mode=%s\r\n", uiVModeStr[swissSettings.uiVMode]);
	fprintf(fp, "Apps Video Mode=%s\r\n", uiVModeStr[swissSettings.appsVMode]);
	fprintf(fp, "Disable PAD Recalibration=%s\r\n", swissSettings.disableRecalibration ? "Yes":"No");
	fprintf(fp, "Disable PAD Rumble=%s\r\n", swissSettings.disableRumble ? "Yes":"No");
	fprintf(fp, "Enable USB Gecko=%s\r\n", enableUSBGeckoStr[swissSettings.enableUSBGecko]);
	fprintf(fp, "Wait for USB Gecko=%s\r\n", swissSettings.waitForUSBGecko ? "Yes":"No");
	fprintf(fp, "Simulated MRAM Size=%s\r\n", simulatedMemSizeStr[swissSettings.simulatedMemSize]);
	fprintf(fp, "Hide Unknown file types=%s\r\n", swissSettings.hideUnknownFileTypes ? "Yes":"No");
	fprintf(fp, "Disable UI Animations=%s\r\n", swissSettings.disableUIAnimations ? "Yes":"No");
	fprintf(fp, "Reduce UI Animations=%s\r\n", !swissSettings.disableUIAnimations && swissSettings.reduceUIAnimations ? "Yes":"No");
	fprintf(fp, "Disable Panel Transparency=%s\r\n", swissSettings.disablePanelTransparency ? "Yes":"No");
	fprintf(fp, "Disable Animated Backdrop=%s\r\n", swissSettings.disableAnimatedBackdrop ? "Yes":"No");
	fprintf(fp, "Disable Menu Music=%s\r\n", swissSettings.disableMenuMusic ? "Yes":"No");
	fprintf(fp, "Disable Menu SFX=%s\r\n", swissSettings.disableMenuSFX ? "Yes":"No");
	fprintf(fp, "Menu Color=%s\r\n", uiColorStr[swissSettings.uiColor]);
	fprintf(fp, "Backdrop Color=%s\r\n", uiLayerColorStr[swissSettings.uiBackdropColor]);
	fprintf(fp, "Wave Color=%s\r\n", uiLayerColorStr[swissSettings.uiWaveColor]);
	fprintf(fp, "Menu Widescreen=%s\r\n", swissSettings.menuWidescreen ? "Yes":"No");
	fprintf(fp, "Library Icon=%s\r\n", libraryIconStr[swissSettings.libraryIcon]);
	fprintf(fp, "Source Icon=%s\r\n", sourceIconStr[swissSettings.sourceIcon]);
	fprintf(fp, "Settings Icon=%s\r\n", settingsIconStr[swissSettings.settingsIcon]);
	fprintf(fp, "System Icon=%s\r\n", systemIconStr[swissSettings.systemIcon]);
	fprintf(fp, "Up Face=%s\r\n", upFaceStr[swissSettings.upFace]);
	fprintf(fp, "Left Face=%s\r\n", leftFaceStr[swissSettings.leftFace]);
	fprintf(fp, "Right Face=%s\r\n", rightFaceStr[swissSettings.rightFace]);
	fprintf(fp, "Down Face=%s\r\n", downFaceStr[swissSettings.downFace]);
	fprintf(fp, "Cube=%s\r\n", swissSettings.cubeStyle ? "Classic":"Infinite");
	fprintf(fp, "Clock=%s\r\n", clockPositionStr[swissSettings.clockPosition]);
	fprintf(fp, "Temperature=%s\r\n", clockPositionStr[swissSettings.temperaturePosition]);
	fprintf(fp, "Wave Speed=%s\r\n", waveSpeedStr[swissSettings.waveSpeed]);
	fprintf(fp, "Library Layout=%s\r\n", libraryLayoutStr[swissSettings.libraryLayout]);
	fprintf(fp, "Spin Discs=%s\r\n", swissSettings.stillDiscs ? "No":"Yes");
	fprintf(fp, "Library Folders=%s\r\n", swissSettings.libraryFolders ? "Yes":"No");
	fprintf(fp, "Hide Saves on Details=%s\r\n", swissSettings.hideDetailSaves ? "Yes":"No");
	fprintf(fp, "Always Show Saves on Details=%s\r\n", swissSettings.alwaysShowDetailSaves ? "Yes":"No");
	fprintf(fp, "Idle Animation=%s\r\n", swissSettings.idleAnimation ? "Sway":"Calm");
	fprintf(fp, "Hide Waves=%s\r\n", swissSettings.hideWaves ? "Yes":"No");
	fprintf(fp, "Hide On-screen Controls=%s\r\n", swissSettings.hideHomeControls ? "Yes":"No");
	fprintf(fp, "Hide Face Labels=%s\r\n", swissSettings.hideFaceLabels ? "Yes":"No");
	fputs("Memory Card Folder Colors=", fp);
	UIFolder_WriteColors(&folderColors, fp);
	fputs("\r\n", fp);
	fprintf(fp, "Init DVD Drive at startup=%s\r\n", swissSettings.initDVDDriveAtStart ? "Yes":"No");
	fprintf(fp, "Stop DVD Drive motor=%s\r\n", swissSettings.stopMotor ? "Yes":"No");
	fprintf(fp, "Configure Audio Buffer=%s\r\n", configAudioBufferStr[swissSettings.configAudioBuffer]);
	fprintf(fp, "Enable WiiRD debug=%s\r\n", swissSettings.wiirdDebug ? "Yes":"No");
	fprintf(fp, "Enable File Management=%s\r\n", swissSettings.enableFileManagement ? "Yes":"No");
	fprintf(fp, "Disable MemCard PRO GameID=%s\r\n", disableMCPGameIDStr[swissSettings.disableMCPGameID]);
	fprintf(fp, "Disable Video Patches=%s\r\n", disableVideoPatchesStr[swissSettings.disableVideoPatches]);
	fprintf(fp, "Force Video Active=%s\r\n", swissSettings.forceVideoActive ? "Yes":"No");
	fprintf(fp, "Force DTV Status=%s\r\n", forceDTVStatusStr[swissSettings.forceDTVStatus]);
	fprintf(fp, "Last DTV Status=%s\r\n", getRawDTVStatus() ? "Yes":"No");
	fprintf(fp, "Pause for resolution change=%s\r\n", swissSettings.pauseAVOutput ? "Yes":"No");
	fprintf(fp, "AutoBoot=%s\r\n", swissSettings.autoBoot ? "Yes":"No");
	fprintf(fp, "AutoCheats=%s\r\n", swissSettings.autoCheats ? "Yes":"No");
	fprintf(fp, "InitNetwork=%s\r\n", swissSettings.initNetworkAtStart ? "Yes":"No");
	fprintf(fp, "IGRType=%s\r\n", igrTypeStr[swissSettings.igrType]);
	fprintf(fp, "AVECompat=%s\r\n", aveCompatStr[swissSettings.aveCompat]);
	fprintf(fp, "FileBrowserType=%s\r\n", fileBrowserTypeStr[swissSettings.fileBrowserType]);
	fprintf(fp, "AppsBrowserType=%s\r\n", fileBrowserTypeStr[swissSettings.appsBrowserType]);
	fprintf(fp, "GameBrowserType=%s\r\n", fileBrowserTypeStr[swissSettings.gameBrowserType]);
	fprintf(fp, "BS2Boot=%s\r\n", bs2BootStr[swissSettings.bs2Boot]);
	fprintf(fp, "RT4KHostIP=%s\r\n", swissSettings.rt4kHostIp);
	fprintf(fp, "RT4KPort=%hu\r\n", swissSettings.rt4kPort);
	fprintf(fp, "RT4KOptim=%s\r\n", swissSettings.rt4kOptim ? "Yes":"No");
	fprintf(fp, "SMBUserName=%s\r\n", swissSettings.smbUser);
	fprintf(fp, "SMBPassword=%s\r\n", swissSettings.smbPassword);
	fprintf(fp, "SMBShareName=%s\r\n", swissSettings.smbShare);
	fprintf(fp, "SMBHostIP=%s\r\n", swissSettings.smbServerIp);
	fprintf(fp, "FTPUserName=%s\r\n", swissSettings.ftpUserName);
	fprintf(fp, "FTPPassword=%s\r\n", swissSettings.ftpPassword);
	fprintf(fp, "FTPHostIP=%s\r\n", swissSettings.ftpHostIp);
	fprintf(fp, "FTPPort=%hu\r\n", swissSettings.ftpPort);
	fprintf(fp, "FTPUsePasv=%s\r\n", swissSettings.ftpUsePasv ? "Yes":"No");
	fprintf(fp, "FSPHostIP=%s\r\n", swissSettings.fspHostIp);
	fprintf(fp, "FSPPort=%hu\r\n", swissSettings.fspPort);
	fprintf(fp, "FSPPassword=%s\r\n", swissSettings.fspPassword);
	fprintf(fp, "FSPPathMTU=%hu\r\n", swissSettings.fspPathMtu);
	fprintf(fp, "BBALocalIP=%s\r\n", swissSettings.bbaLocalIp);
	fprintf(fp, "BBANetmask=%hu\r\n", swissSettings.bbaNetmask);
	fprintf(fp, "BBAGateway=%s\r\n", swissSettings.bbaGateway);
	fprintf(fp, "BBAUseDHCP=%s\r\n", swissSettings.bbaUseDhcp ? "Yes":"No");
	fprintf(fp, "NTPSync=%s\r\n", swissSettings.ntpSync ? "Yes":"No");
	fprintf(fp, "NTPHostIP=%s\r\n", swissSettings.ntpHostIp);
	fprintf(fp, "TimeZone=%s\r\n", swissSettings.timeZone);
	fprintf(fp, "LabServer=%s\r\n", swissSettings.labServer ? "Yes":"No");
	fprintf(fp, "LogHost=%s\r\n", swissSettings.logHost);
	fprintf(fp, "ShowHiddenFiles=%s\r\n", swissSettings.showHiddenFiles ? "Yes":"No");
	fprintf(fp, "RecentListLevel=%s\r\n", recentListLevelStr[swissSettings.recentListLevel]);
	fprintf(fp, "GCLoaderHWVersion=%i\r\n", swissSettings.gcloaderHwVersion);
	fprintf(fp, "GCLoaderTopVersion=%s\r\n", swissSettings.gcloaderTopVersion);
	fprintf(fp, "Autoload=%s\r\n", swissSettings.autoload);
	fprintf(fp, "FlattenDir=%s\r\n", swissSettings.libraryFolders ?
		swissSettings.libraryFoldersFlattenDir : swissSettings.flattenDir);
	fprintf(fp, "Save Folder=%s\r\n", swissSettings.saveFolder);
	fprintf(fp, "Library Path=%s\r\n", swissSettings.libraryPath);

	// Write out the default game config portion too
	fprintf(fp, "Force NTSC Video Mode=%s\r\n", gameVModeStr[swissSettings.gameVModeNtsc]);
	fprintf(fp, "Force PAL Video Mode=%s\r\n", gameVModeStr[swissSettings.gameVModePal]);
	fprintf(fp, "Force Horizontal Scale=%s\r\n", forceHScaleStr[swissSettings.forceHScale]);
	fprintf(fp, "Force Vertical Offset=%+hi\r\n", swissSettings.forceVOffset);
	fprintf(fp, "Force Vertical Filter=%s\r\n", forceVFilterStr[swissSettings.forceVFilter]);
	fprintf(fp, "Force Field Rendering=%s\r\n", forceVJitterStr[swissSettings.forceVJitter]);
	fprintf(fp, "Fix Pixel Center=%s\r\n", fixPixelCenterStr[swissSettings.fixPixelCenter]);
	fprintf(fp, "Disable Alpha Dithering=%s\r\n", swissSettings.disableDithering ? "Yes":"No");
	fprintf(fp, "Force Anisotropic Filter=%s\r\n", swissSettings.forceAnisotropy ? "Yes":"No");
	fprintf(fp, "Force Widescreen=%s\r\n", forceWidescreenStr[swissSettings.forceWidescreen]);
	fprintf(fp, "Force Polling Rate=%s\r\n", forcePollRateStr[swissSettings.forcePollRate]);
	fprintf(fp, "Invert Camera Stick=%s\r\n", invertCStickStr[swissSettings.invertCStick]);
	fprintf(fp, "Swap Camera Stick=%s\r\n", swapCStickStr[swissSettings.swapCStick]);
	fprintf(fp, "Digital Trigger Level=%hhu\r\n", swissSettings.triggerLevel);
	fprintf(fp, "Emulate Audio Streaming=%s\r\n", emulateAudioStreamStr[swissSettings.emulateAudioStream]);
	fprintf(fp, "Emulate Read Speed=%s\r\n", emulateReadSpeedStr[swissSettings.emulateReadSpeed]);
	fprintf(fp, "Emulate Broadband Adapter=%s\r\n", swissSettings.emulateEthernet ? "Yes":"No");
	fprintf(fp, "Emulate Memory Card=%s\r\n", swissSettings.emulateMemoryCard ? "Yes":"No");
	fprintf(fp, "Disable Memory Card=%s\r\n", disableMemoryCardStr[swissSettings.disableMemoryCard]);
	fprintf(fp, "Disable Hypervisor=%s\r\n", swissSettings.disableHypervisor ? "Yes":"No");
	fprintf(fp, "Prefer Clean Boot=%s\r\n", swissSettings.preferCleanBoot ? "Yes":"No");
	fprintf(fp, "RetroTINK-4K Profile=%i\r\n", swissSettings.rt4kProfile);
	fprintf(fp, "#!!Swiss Settings End!!\r\n\r\n");
	fclose(fp);

	ensure_path(DEVICE_CONFIG, SWISS_BASE_DIR, NULL, true);
	ensure_path(DEVICE_CONFIG, SWISS_SETTINGS_DIR, NULL, false);
	
	/* The path stays in its own buffer across the read and the write. */
	char path[PATHNAME_MAX];
	concat_path(path, SWISS_SETTINGS_DIR, SWISS_SETTINGS_FILENAME);
	char *existing = config_file_read(path);
	char *merged = config_merge_file(existing, configString, globalOldKeys);
	int res = config_file_write(path, merged != NULL ? merged : configString);
	if(res) {
		globalFileLoaded = true;
	}
	free(merged);
	free(existing);
	free(configString);
	if(checkConfigDevice) {
		config_unset_device();
	}
	return res;
}

/* Folder changes save only their key. Unsaved or temporary global values
 * in memory must not be written as a side effect of coloring a folder. */
static bool config_folder_settings_absent(char *path) {
	DEVICEHANDLER_INTERFACE *device = devices[DEVICE_CONFIG];
	file_handle file = {0};
	/* A failed read is never proof of absence. Only FAT's explicit missing
	 * codes for both the live file and recovery copy permit a new file. */
	if(device->statFile != deviceHandler_FAT_statFile) return false;
	concat_path(file.name, device->initial->name, path);
	s32 status = device->statFile(&file);
	device->closeFile(&file);
	if(status != FR_NO_FILE && status != FR_NO_PATH) return false;
	config_name_new(file.name);
	status = device->statFile(&file);
	device->closeFile(&file);
	return status == FR_NO_FILE || status == FR_NO_PATH;
}

/* checkConfigDevice false: the caller holds the settings device mounted (Memory
 * Cards does for its whole visit), so it is neither mounted nor let go here. */
bool config_set_folder_color(const char *path, uint8_t color, bool checkConfigDevice) {
	uiFolderColors_t *previous = malloc(sizeof(*previous));
	char *line = NULL;
	size_t length = 0u;
	FILE *fp;
	int result = 0;
	if(previous == NULL) return false;
	*previous = folderColors;
	if(!UIFolder_SetColor(&folderColors, path, color)) { free(previous); return false; }
	if(checkConfigDevice ? !config_set_device() : devices[DEVICE_CONFIG] == NULL) goto done;
	fp = open_memstream(&line, &length);
	if(fp != NULL) {
		char configPath[PATHNAME_MAX];
		fputs("Memory Card Folder Colors=", fp);
		UIFolder_WriteColors(&folderColors, fp);
		fputs("\r\n", fp);
		if(fclose(fp) == 0) {
			concat_path(configPath, SWISS_SETTINGS_DIR, SWISS_SETTINGS_FILENAME);
			char *existing = config_file_read(configPath);
			const char *const oldColorKey[] = {"Library Folder Colors", NULL};
			char *merged = config_merge_file(existing, line, oldColorKey);
			if(existing != NULL) {
				/* Do not replace a readable settings file if merging ran out
				 * of memory: that would discard unrelated settings. */
				if(merged != NULL) result = config_file_write(configPath, merged);
			}
			else if(config_folder_settings_absent(configPath)) {
				ensure_path(DEVICE_CONFIG, SWISS_BASE_DIR, NULL, true);
				ensure_path(DEVICE_CONFIG, SWISS_SETTINGS_DIR, NULL, false);
				result = config_file_write(configPath, line);
			}
			free(merged);
			free(existing);
		}
		free(line);
	}
	if(checkConfigDevice) config_unset_device();
done:
	if(!result) folderColors = *previous;
	else globalFileLoaded = true;
	free(previous);
	return result != 0;
}

/* What an autoload toggle saves over an existing global.ini: the same file
 * with only its Autoload key changed, or NULL when there is no file to keep. */
static char *config_merge_autoload(const char *existing) {
	char line[sizeof(swissSettings.autoload) + sizeof("Autoload=\r\n")];
	snprintf(line, sizeof(line), "Autoload=%s\r\n", swissSettings.autoload);
	return config_merge_file(existing, line, NULL);
}

/* What Z in the Jewel Case saves: the file with only its Spin Discs key
 * changed, or NULL when there is no file to keep. */
static char *config_merge_spin_discs(const char *existing) {
	return config_merge_file(existing, swissSettings.stillDiscs ?
		"Spin Discs=No\r\n" : "Spin Discs=Yes\r\n", NULL);
}

/* Toggling autoload, or the Jewel Case's spin, saves only that key. Other
 * settings in memory may hold values nobody chose to keep (holding B at
 * launch turns AutoBoot off until the next boot), so the file keeps its own. */
static int config_update_one_key(char *(*merge)(const char *existing),
	bool checkConfigDevice) {
	if(checkConfigDevice && !config_set_device()) return 0;

	char path[PATHNAME_MAX];
	concat_path(path, SWISS_SETTINGS_DIR, SWISS_SETTINGS_FILENAME);
	char *existing = config_file_read(path);
	char *merged = merge(existing);
	/* No readable file to keep: save every setting, as before. */
	int res = merged != NULL ? config_file_write(path, merged) : config_update_global(false);
	free(merged);
	free(existing);
	if(checkConfigDevice) {
		config_unset_device();
	}
	return res;
}

int config_update_autoload(bool checkConfigDevice) {
	return config_update_one_key(config_merge_autoload, checkConfigDevice);
}

int config_update_spin_discs(bool checkConfigDevice) {
	return config_update_one_key(config_merge_spin_discs, checkConfigDevice);
}

int config_update_recent(bool checkConfigDevice) {
	if(checkConfigDevice && !config_set_device()) return 0;

	char *configString = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&configString, &len);
	if(!fp) return 0;

	fprintf(fp, "# Recent list. Created by Swiss\r\n");
	int i;
	for(i = 0; i < RECENT_MAX; i++) {
		fprintf(fp, "Recent_%i=%s\r\n", i, swissSettings.recent[i]);
		//print_debug("added recent num %i [%s]\n", i, swissSettings.recent[i]);
	}
	fclose(fp);

	ensure_path(DEVICE_CONFIG, SWISS_BASE_DIR, NULL, true);
	ensure_path(DEVICE_CONFIG, SWISS_SETTINGS_DIR, NULL, false);
	
	concat_path(txtbuffer, SWISS_SETTINGS_DIR, SWISS_RECENTLIST_FILENAME);
	int res = config_file_write(txtbuffer, configString);
	free(configString);
	if(checkConfigDevice) {
		config_unset_device();
	}
	return res;
}


int config_update_game(ConfigEntry *entry, ConfigEntry *defaults, bool checkConfigDevice) {
	/* Settings files change: the Library's custom marks read them again. */
	settings_game_files_forget();
	if(checkConfigDevice && !config_set_device()) return 0;

	char *configString = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&configString, &len);
	if(!fp) return 0;

	fprintf(fp, "# Game specific configuration file. Created by Swiss\r\n");
	fprintf(fp, "ID=%.4s\r\n", entry->game_id);
	fprintf(fp, "Name=%.64s\r\n", entry->game_name);
	fprintf(fp, "Comment=%.128s\r\n", entry->comment);
	fprintf(fp, "Status=%.32s\r\n", entry->status);
	if(entry->gameLanguage != defaults->gameLanguage) fprintf(fp, "Game Language=%s\r\n", sramLanguageStr[entry->gameLanguage]);
	if(entry->gameVMode != defaults->gameVMode) fprintf(fp, "Force Video Mode=%s\r\n", gameVModeStr[entry->gameVMode]);
	if(entry->forceHScale != defaults->forceHScale) fprintf(fp, "Force Horizontal Scale=%s\r\n", forceHScaleStr[entry->forceHScale]);
	if(entry->forceVOffset != defaults->forceVOffset) fprintf(fp, "Force Vertical Offset=%+hi\r\n", entry->forceVOffset);
	if(entry->forceVFilter != defaults->forceVFilter) fprintf(fp, "Force Vertical Filter=%s\r\n", forceVFilterStr[entry->forceVFilter]);
	if(entry->forceVJitter != defaults->forceVJitter) fprintf(fp, "Force Field Rendering=%s\r\n", forceVJitterStr[entry->forceVJitter]);
	if(entry->fixPixelCenter != defaults->fixPixelCenter) fprintf(fp, "Fix Pixel Center=%s\r\n", fixPixelCenterStr[entry->fixPixelCenter]);
	if(entry->disableDithering != defaults->disableDithering) fprintf(fp, "Disable Alpha Dithering=%s\r\n", entry->disableDithering ? "Yes":"No");
	if(entry->forceAnisotropy != defaults->forceAnisotropy) fprintf(fp, "Force Anisotropic Filter=%s\r\n", entry->forceAnisotropy ? "Yes":"No");
	if(entry->forceWidescreen != defaults->forceWidescreen) fprintf(fp, "Force Widescreen=%s\r\n", forceWidescreenStr[entry->forceWidescreen]);
	if(entry->forcePollRate != defaults->forcePollRate) fprintf(fp, "Force Polling Rate=%s\r\n", forcePollRateStr[entry->forcePollRate]);
	if(entry->invertCStick != defaults->invertCStick) fprintf(fp, "Invert Camera Stick=%s\r\n", invertCStickStr[entry->invertCStick]);
	if(entry->swapCStick != defaults->swapCStick) fprintf(fp, "Swap Camera Stick=%s\r\n", swapCStickStr[entry->swapCStick]);
	if(entry->triggerLevel != defaults->triggerLevel) fprintf(fp, "Digital Trigger Level=%hhu\r\n", entry->triggerLevel);
	if(entry->emulateAudioStream != defaults->emulateAudioStream) fprintf(fp, "Emulate Audio Streaming=%s\r\n", emulateAudioStreamStr[entry->emulateAudioStream]);
	if(entry->emulateReadSpeed != defaults->emulateReadSpeed) fprintf(fp, "Emulate Read Speed=%s\r\n", emulateReadSpeedStr[entry->emulateReadSpeed]);
	if(entry->emulateEthernet != defaults->emulateEthernet) fprintf(fp, "Emulate Broadband Adapter=%s\r\n", entry->emulateEthernet ? "Yes":"No");
	if(entry->disableMemoryCard != defaults->disableMemoryCard) fprintf(fp, "Disable Memory Card=%s\r\n", disableMemoryCardStr[entry->disableMemoryCard]);
	if(entry->disableHypervisor != defaults->disableHypervisor) fprintf(fp, "Disable Hypervisor=%s\r\n", entry->disableHypervisor ? "Yes":"No");
	if(entry->preferCleanBoot != defaults->preferCleanBoot) fprintf(fp, "Prefer Clean Boot=%s\r\n", entry->preferCleanBoot ? "Yes":"No");
	if(entry->rt4kProfile != defaults->rt4kProfile) fprintf(fp, "RetroTINK-4K Profile=%i\r\n", entry->rt4kProfile);
	fclose(fp);

	ensure_path(DEVICE_CONFIG, SWISS_BASE_DIR, NULL, true);
	ensure_path(DEVICE_CONFIG, SWISS_SETTINGS_DIR, NULL, false);
	ensure_path(DEVICE_CONFIG, SWISS_GAME_SETTINGS_DIR, NULL, false);
	
	char path[PATHNAME_MAX];
	concatf_path(path, SWISS_GAME_SETTINGS_DIR, "%.4s.ini", entry->game_id);
	char *existing = config_file_read(path);
	char *merged = config_merge_file(existing, configString, gameFileKeys);
	int res = config_file_write(path, merged != NULL ? merged : configString);
	free(merged);
	free(existing);
	free(configString);
	if(checkConfigDevice) {
		config_unset_device();
	}
	return res;
}

static char fixPixelCenterEntries[][5] = {"00\0E\1", "DNDD\1", "G2BE\1", "G2BP\1", "GD7E\1", "GD7P\1", "GEME\1", "GEMJ\1", "GEMP\1", "GNBE\1", "GNBJ\1", "GNBP\1", "GZBJ\1"};
static char forceAnisotropyEntries[][5] = {"GADE\0", "GAEE\0", "GAEJ\0", "GAFE\0", "GAFJ\0", "GAFP\0", "GAFU\0", "GM8E\0", "GM8J\0", "GM8P\0", "PGSE\0", "PKBJ\0", "PZLE\0", "PZLJ\0", "PZLP\0"};
/* Animal Crossing Deluxe widens itself (its own Widescreen mode) and sizes
 * its rain and snow by that mode, not by the projection Swiss widens. */
static char forceWidescreenEntries[][5] = {"GADE\0"};
static char triggerLevelEntries[][5] = {"GKGE\0", "GKGJ\0", "GKGP\0", "GY2E\0", "GY2J\0", "GY2P\0", "GY3E\0", "GY3J\0", "GYBE\0", "GYBJ\0", "GYBP\0"};
static char emulateAudioStreamEntries[][5] = {"UFZE\0", "UFZJ\0", "UFZP\0"};
static char emulateReadSpeedEntries[][5] = {"DRSE\1", "GADE\0", "GQSD\1", "GQSE\1", "GQSF\1", "GQSI\1", "GQSP\1", "GQSS\1", "GRSE\1", "GRSJ\1", "GRSP\1", "GTOJ\1"};
static char emulateEthernetEntries[][5] = {"DPSJ\1", "GHEE\1", "GHEJ\1", "GKYE\1", "GKYJ\1", "GKYP\1", "GM4E\1", "GM4J\1", "GM4P\1", "GPJJ\1", "GPOE\1", "GPOJ\1", "GPOP\1", "GPSE\1", "GPSJ\1", "GPSP\1", "GTEE\1", "GTEJ\1", "GTEP\1", "GTEW\1", "PHEJ\1"};

/* Game defaults as a given settings snapshot defines them. Settings compares
 * a game against the snapshot it opened with, so changing a default there
 * does not pin the game's untouched values as overrides. */
void config_defaults_from(ConfigEntry *entry, const SwissSettings *settings) {
	strcpy(entry->comment, "No Comment");
	strcpy(entry->status, "Unknown");
	entry->gameLanguage = SRAM_LANGUAGE_MAX;
	entry->gameVMode = entry->region == 'P' ? settings->gameVModePal : settings->gameVModeNtsc;
	entry->forceHScale = settings->forceHScale;
	entry->forceVOffset = settings->forceVOffset;
	entry->forceVFilter = settings->forceVFilter;
	entry->forceVJitter = settings->forceVJitter;
	entry->fixPixelCenter = settings->fixPixelCenter;
	entry->disableDithering = settings->disableDithering;
	entry->forceAnisotropy = settings->forceAnisotropy;
	entry->forceWidescreen = settings->forceWidescreen;
	entry->forcePollRate = settings->forcePollRate;
	entry->invertCStick = settings->invertCStick;
	entry->swapCStick = settings->swapCStick;
	entry->triggerLevel = settings->triggerLevel;
	entry->emulateAudioStream = settings->emulateAudioStream;
	entry->emulateReadSpeed = settings->emulateReadSpeed;
	entry->emulateEthernet = settings->emulateEthernet;
	entry->disableMemoryCard = settings->disableMemoryCard;
	entry->disableHypervisor = settings->disableHypervisor;
	entry->preferCleanBoot = settings->preferCleanBoot;
	entry->rt4kProfile = settings->rt4kProfile;

	for(int i = 0; i < countof(fixPixelCenterEntries); i++) {
		if(!strncmp(entry->game_id, fixPixelCenterEntries[i], 4)) {
			entry->fixPixelCenter = fixPixelCenterEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(forceAnisotropyEntries); i++) {
		if(!strncmp(entry->game_id, forceAnisotropyEntries[i], 4)) {
			entry->forceAnisotropy = forceAnisotropyEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(forceWidescreenEntries); i++) {
		if(!strncmp(entry->game_id, forceWidescreenEntries[i], 4)) {
			entry->forceWidescreen = forceWidescreenEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(triggerLevelEntries); i++) {
		if(!strncmp(entry->game_id, triggerLevelEntries[i], 4)) {
			entry->triggerLevel = triggerLevelEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(emulateAudioStreamEntries); i++) {
		if(!strncmp(entry->game_id, emulateAudioStreamEntries[i], 4)) {
			entry->emulateAudioStream = emulateAudioStreamEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(emulateReadSpeedEntries); i++) {
		if(!strncmp(entry->game_id, emulateReadSpeedEntries[i], 4)) {
			entry->emulateReadSpeed = emulateReadSpeedEntries[i][4];
			break;
		}
	}
	for(int i = 0; i < countof(emulateEthernetEntries); i++) {
		if(!strncmp(entry->game_id, emulateEthernetEntries[i], 4)) {
			entry->emulateEthernet = emulateEthernetEntries[i][4];
			break;
		}
	}
}

void config_defaults(ConfigEntry *entry) {
	config_defaults_from(entry, &swissSettings);
}

// TODO kill this off in one major version from now. Don't add new settings to it.
/* Entries for the games a legacy swiss.ini carries over and for Game
 * Defaults: 2,047 games at most, the rest dropped. About 650 KB, so on the
 * heap: the menu thread's stack is 128 KB. */
#define LEGACY_ENTRIES 2048

void config_parse_legacy(char *configData, void (*progress_indicator)(char*, int, int)) {
	ConfigEntry *configEntries = calloc(LEGACY_ENTRIES, sizeof(ConfigEntry));
	int configEntriesCount = 0;
	if(configEntries == NULL) {
		// No room to migrate: swiss.ini stays for another try.
		return;
	}
	// Parse each entry and put it into our array
	char *line, *linectx = NULL;
	int first = 1;
	bool defaultPassed = false;
	line = strtok_r( configData, "\r\n", &linectx );
	while( line != NULL ) {
		//print_debug("Line [%s]\n", line);
		if(line[0] != '#') {
			// Is this line a new game entry?
			char *name, *value = NULL;
			name = strtok_r(line, "=", &value);
			
			if(value != NULL) {
				//print_debug("Name [%s] Value [%s]\n", name, value);

				if(!strcmp("ID", name)) {
					defaultPassed = true;
					// Games past the last slot are read into it, then dropped.
					if(!first && configEntriesCount < LEGACY_ENTRIES - 1) {
						configEntriesCount++;
					}
					strncpy(configEntries[configEntriesCount].game_id, value, 4);
					first = 0;
					// Fill this entry with defaults incase some values are missing..
					config_defaults(&configEntries[configEntriesCount]);
				}
				else if(!strcmp("Name", name)) {
					strncpy(configEntries[configEntriesCount].game_name, value, 64);
				}
				else if(!strcmp("Comment", name)) {
					strncpy(configEntries[configEntriesCount].comment, value, 128);
				}
				else if(!strcmp("Status", name)) {
					strncpy(configEntries[configEntriesCount].status, value, 32);
				}
				else if(!strcmp("Force Video Mode", name)) {
					int *ptr = !defaultPassed ? &swissSettings.gameVMode : &configEntries[configEntriesCount].gameVMode;
					for(int i = 0; i < 15; i++) {
						if(!strcmp(gameVModeStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Horizontal Scale", name)) {
					int *ptr = !defaultPassed ? &swissSettings.forceHScale : &configEntries[configEntriesCount].forceHScale;
					for(int i = 0; i < 9; i++) {
						if(!strcmp(forceHScaleStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Vertical Filter", name)) {
					int *ptr = !defaultPassed ? &swissSettings.forceVFilter : &configEntries[configEntriesCount].forceVFilter;
					for(int i = 0; i < 4; i++) {
						if(!strcmp(forceVFilterStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Alpha Dithering", name)) {
					if(defaultPassed)
						configEntries[configEntriesCount].disableDithering = !strcmp("Yes", value);
					else
						swissSettings.disableDithering = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Anisotropic Filter", name)) {
					if(defaultPassed)
						configEntries[configEntriesCount].forceAnisotropy = !strcmp("Yes", value);
					else
						swissSettings.forceAnisotropy = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Widescreen", name)) {
					int *ptr = !defaultPassed ? &swissSettings.forceWidescreen : &configEntries[configEntriesCount].forceWidescreen;
					for(int i = 0; i < 3; i++) {
						if(!strcmp(forceWidescreenStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Invert Camera Stick", name)) {
					int *ptr = !defaultPassed ? &swissSettings.invertCStick : &configEntries[configEntriesCount].invertCStick;
					for(int i = 0; i < 4; i++) {
						if(!strcmp(invertCStickStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Read Speed", name)) {
					int *ptr = !defaultPassed ? &swissSettings.emulateReadSpeed : &configEntries[configEntriesCount].emulateReadSpeed;
					for(int i = 0; i < 3; i++) {
						if(!strcmp(emulateReadSpeedStr[i], value)) {
							*ptr = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Memory Card", name)) {
					swissSettings.emulateMemoryCard = !strcmp("Yes", value);
				}
				
				// Swiss settings
				else if(!strcmp("SD/IDE Speed", name)) {
					swissSettings.exiSpeed = !strcmp("32MHz", value);
				}
				else if(!strcmp("Swiss Video Mode", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(uiVModeStr[i], value)) {
							swissSettings.uiVMode = i;
							break;
						}
					}
				}
				else if(!strcmp("Enable Debug", name)) {
					swissSettings.enableUSBGecko = !strcmp("Yes", value) ? USBGECKO_MEMCARD_SLOT_B : USBGECKO_OFF;
				}
				else if(!strcmp("Hide Unknown file types", name)) {
					swissSettings.hideUnknownFileTypes = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable UI Animations", name)) {
					swissSettings.disableUIAnimations = !strcmp("Yes", value);
				}
				else if(!strcmp("Reduce UI Animations", name)) {
					swissSettings.reduceUIAnimations = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Panel Transparency", name)) {
					swissSettings.disablePanelTransparency = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Animated Backdrop", name)) {
					swissSettings.disableAnimatedBackdrop = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Menu Music", name)) {
					swissSettings.disableMenuMusic = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Menu SFX", name)) {
					swissSettings.disableMenuSFX = !strcmp("Yes", value);
				}
				else if(!strcmp("Stop DVD Motor on startup", name)) {
					swissSettings.stopMotor = !strcmp("Yes", value);
				}
				else if(!strcmp("Enable WiiRD debug", name)) {
					swissSettings.wiirdDebug = !strcmp("Yes", value);
				}
				else if(!strcmp("Enable File Management", name)) {
					swissSettings.enableFileManagement = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Video Patches", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(disableVideoPatchesStr[i], value)) {
							swissSettings.disableVideoPatches = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Video Active", name)) {
					swissSettings.forceVideoActive = !strcmp("Yes", value);
				}
				else if(!strcmp("Force DTV Status", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(forceDTVStatusStr[i], value)) {
							swissSettings.forceDTVStatus = i;
							break;
						}
					}
				}
				else if(!strcmp("SMBUserName", name)) {
					strlcpy(swissSettings.smbUser, value, sizeof(swissSettings.smbUser));
				}
				else if(!strcmp("SMBPassword", name)) {
					strlcpy(swissSettings.smbPassword, value, sizeof(swissSettings.smbPassword));
				}
				else if(!strcmp("SMBShareName", name)) {
					strlcpy(swissSettings.smbShare, value, sizeof(swissSettings.smbShare));
				}
				else if(!strcmp("SMBHostIP", name)) {
					strlcpy(swissSettings.smbServerIp, value, sizeof(swissSettings.smbServerIp));
				}
				else if(!strcmp("AutoCheats", name)) {
					swissSettings.autoCheats = !strcmp("Yes", value);
				}
				else if(!strcmp("InitNetwork", name)) {
					swissSettings.initNetworkAtStart = !strcmp("Yes", value);
				}
				else if(!strcmp("IGRType", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(igrTypeStr[i], value)) {
							swissSettings.igrType = i;
							break;
						}
					}
				}
				else if(!strcmp("AVECompat", name)) {
					for(int i = 0; i < AVE_COMPAT_MAX; i++) {
						if(!strcmp(aveCompatStr[i], value)) {
							swissSettings.aveCompat = i;
							break;
						}
					}
				}
				else if(!strcmp("FileBrowserType", name)) {
					for(int i = 0; i < BROWSER_MAX; i++) {
						if(!strcmp(fileBrowserTypeStr[i], value)) {
							swissSettings.fileBrowserType = i;
							break;
						}
					}
				}
				else if(!strcmp("BS2Boot", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(bs2BootStr[i], value)) {
							swissSettings.bs2Boot = i;
							break;
						}
					}
				}
				else if(!strcmp("FTPUserName", name)) {
					strlcpy(swissSettings.ftpUserName, value, sizeof(swissSettings.ftpUserName));
				}
				else if(!strcmp("FTPPassword", name)) {
					strlcpy(swissSettings.ftpPassword, value, sizeof(swissSettings.ftpPassword));
				}
				else if(!strcmp("FTPHostIP", name)) {
					strlcpy(swissSettings.ftpHostIp, value, sizeof(swissSettings.ftpHostIp));
				}
				else if(!strcmp("FTPPort", name)) {
					swissSettings.ftpPort = atoi(value);
				}
				else if(!strcmp("FTPUsePasv", name)) {
					swissSettings.ftpUsePasv = !strcmp("Yes", value);
				}
				else if(!strcmp("FSPHostIP", name)) {
					strlcpy(swissSettings.fspHostIp, value, sizeof(swissSettings.fspHostIp));
				}
				else if(!strcmp("FSPPort", name)) {
					swissSettings.fspPort = atoi(value);
				}
				else if(!strcmp("FSPPassword", name)) {
					strlcpy(swissSettings.fspPassword, value, sizeof(swissSettings.fspPassword));
				}
				else if(!strcmp("ShowHiddenFiles", name)) {
					swissSettings.showHiddenFiles = !strcmp("Yes", value);
				}
				else if(!strcmp("RecentListLevel", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(recentListLevelStr[i], value)) {
							swissSettings.recentListLevel = i;
							break;
						}
					}
				}
				else if(!strcmp("GCLoaderTopVersion", name)) {
					strlcpy(swissSettings.gcloaderTopVersion, value, sizeof(swissSettings.gcloaderTopVersion));
				}
				else if(!strcmp("Autoload", name)) {
					strlcpy(swissSettings.autoload, value, sizeof(swissSettings.autoload));
				}
				else if(!strncmp("Recent_", name, strlen("Recent_"))) {
					int recent_slot = atoi(name+strlen("Recent_"));
					if(recent_slot >= 0 && recent_slot < RECENT_MAX) {
						//print_debug("found recent num %i [%s]\n", recent_slot, value);
						strlcpy(swissSettings.recent[recent_slot], value, PATHNAME_MAX);
					}
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}

	if((configEntriesCount > 0 || !first) && configEntriesCount < LEGACY_ENTRIES - 1) {
		configEntriesCount++;
	}
	if(configEntriesCount > 0 || !first) {
		config_defaults(&configEntries[configEntriesCount]);
	}
	 print_debug("Found %i entries in the (legacy) config file\n",configEntriesCount);
	 
	 // Write out to individual files.
	 int i;
	 for(i = 0; i < configEntriesCount; i++) {
		 progress_indicator("Migrating settings to new format.", 1, (int)(((float)i / (float)configEntriesCount) * 100));
		 config_update_game(&configEntries[i], &configEntries[configEntriesCount], false);
	 }
	 // Write out a new swiss.ini
	 config_update_global(false);
	 // Write out new recent.ini
	 config_update_recent(false);
	 // Kill off the old swiss.ini
	 config_file_delete(SWISS_SETTINGS_FILENAME_LEGACY);
	 free(configEntries);
}

/* settingsFile: configData is a whole global.ini. Otherwise it is a loader's
 * arguments, which change only the settings they name. */
void config_parse_global(char *configData, bool settingsFile) {
	char *line, *linectx = NULL;
	/* Before Temperature had its own line, Clock placed the dial too. */
	int clockRead = -1;
	bool temperatureRead = false;
	int libraryFolders = -1;	/* -1: not named */
	/* Before the sides were named, Hide Apps Face took Apps off the cube. */
	bool hideAppsRead = false;
	bool sidesRead = false;
	line = strtok_r( configData, "\r\n", &linectx );
	while( line != NULL ) {
		//print_debug("Line [%s]\n", line);
		if(line[0] != '#') {
			char *name, *value = NULL;
			name = strtok_r(line, "=", &value);
			
			if(value != NULL) {
				//print_debug("Name [%s] Value [%s]\n", name, value);

				if(!strcmp("Force NTSC Video Mode", name)) {
					for(int i = 0; i < 8; i++) {
						if(!strcmp(gameVModeStr[i], value)) {
							swissSettings.gameVModeNtsc = i;
							break;
						}
					}
				}
				else if(!strcmp("Force PAL Video Mode", name)) {
					for(int i = 0; i < 15; i++) {
						if(!strcmp(gameVModeStr[i], value)) {
							swissSettings.gameVModePal = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Horizontal Scale", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(forceHScaleStr[i], value)) {
							swissSettings.forceHScale = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Vertical Offset", name)) {
					swissSettings.forceVOffset = atoi(value);
				}
				else if(!strcmp("Force Vertical Filter", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(forceVFilterStr[i], value)) {
							swissSettings.forceVFilter = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Field Rendering", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(forceVJitterStr[i], value)) {
							swissSettings.forceVJitter = i;
							break;
						}
					}
				}
				else if(!strcmp("Fix Pixel Center", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(fixPixelCenterStr[i], value)) {
							swissSettings.fixPixelCenter = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Alpha Dithering", name)) {
					swissSettings.disableDithering = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Anisotropic Filter", name)) {
					swissSettings.forceAnisotropy = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Widescreen", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(forceWidescreenStr[i], value)) {
							swissSettings.forceWidescreen = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Polling Rate", name)) {
					for(int i = 0; i < 13; i++) {
						if(!strcmp(forcePollRateStr[i], value)) {
							swissSettings.forcePollRate = i;
							break;
						}
					}
				}
				else if(!strcmp("Invert Camera Stick", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(invertCStickStr[i], value)) {
							swissSettings.invertCStick = i;
							break;
						}
					}
				}
				else if(!strcmp("Swap Camera Stick", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(swapCStickStr[i], value)) {
							swissSettings.swapCStick = i;
							break;
						}
					}
				}
				else if(!strcmp("Digital Trigger Level", name)) {
					swissSettings.triggerLevel = atoi(value);
				}
				else if(!strcmp("Emulate Audio Streaming", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(emulateAudioStreamStr[i], value)) {
							swissSettings.emulateAudioStream = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Read Speed", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(emulateReadSpeedStr[i], value)) {
							swissSettings.emulateReadSpeed = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Broadband Adapter", name)) {
					swissSettings.emulateEthernet = !strcmp("Yes", value);
				}
				else if(!strcmp("Emulate Memory Card", name)) {
					swissSettings.emulateMemoryCard = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Memory Card", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(disableMemoryCardStr[i], value)) {
							swissSettings.disableMemoryCard = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Hypervisor", name)) {
					swissSettings.disableHypervisor = !strcmp("Yes", value);
				}
				else if(!strcmp("Prefer Clean Boot", name)) {
					swissSettings.preferCleanBoot = !strcmp("Yes", value);
				}
				else if(!strcmp("RetroTINK-4K Profile", name)) {
					swissSettings.rt4kProfile = atoi(value);
				}
				
				// Swiss settings
				else if(!strcmp("SD/IDE Speed", name)) {
					swissSettings.exiSpeed = !strcmp("32MHz", value);
				}
				else if(!strcmp("System Boot Mode", name)) {
					swissSettings.sramBoot = !strcmp("Production", value) ? SYS_BOOT_PRODUCTION : SYS_BOOT_DEVELOPMENT;
				}
				else if(!strcmp("System Sound", name)) {
					swissSettings.sramStereo = !strcmp("Stereo", value) ? SYS_SOUND_STEREO : SYS_SOUND_MONO;
				}
				else if(!strcmp("System Video", name) && swissSettings.aveCompat != AVE_RVL_COMPAT) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(sramVideoStr[i], value)) {
							swissSettings.sramVideo = i;
							break;
						}
					}
				}
				else if(!strcmp("Screen Position", name)) {
					swissSettings.sramHOffset = atoi(value);
				}
				else if(!strcmp("System Language", name)) {
					for(int i = 0; i < SRAM_LANGUAGE_MAX; i++) {
						if(!strcmp(sramLanguageStr[i], value)) {
							swissSettings.sramLanguage = i;
							break;
						}
					}
				}
				else if(!strcmp("Swiss Video Mode", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(uiVModeStr[i], value)) {
							swissSettings.uiVMode = i;
							break;
						}
					}
				}
				else if(!strcmp("Apps Video Mode", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(uiVModeStr[i], value)) {
							swissSettings.appsVMode = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable PAD Recalibration", name)) {
					swissSettings.disableRecalibration = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable PAD Rumble", name)) {
					swissSettings.disableRumble = !strcmp("Yes", value);
				}
				else if(!strcmp("Enable Debug", name)) {
					swissSettings.enableUSBGecko = !strcmp("Yes", value) ? USBGECKO_MEMCARD_SLOT_B : USBGECKO_OFF;
				}
				else if(!strcmp("Enable USB Gecko", name) || !strcmp("USB Gecko debug output", name)) {
					for(int i = 0; i < USBGECKO_MAX; i++) {
						if(!strcmp(enableUSBGeckoStr[i], value)) {
							swissSettings.enableUSBGecko = i;
							break;
						}
					}
				}
				else if(!strcmp("Wait for USB Gecko", name)) {
					swissSettings.waitForUSBGecko = !strcmp("Yes", value);
				}
				else if(!strcmp("Simulated MRAM Size", name)) {
					for(int i = 0; i < 6; i++) {
						if(!strcmp(simulatedMemSizeStr[i], value)) {
							if(simulatedMemSizeInt[i] <= SYS_GetPhysicalMemSize())
								swissSettings.simulatedMemSize = i;
							break;
						}
					}
				}
				else if(!strcmp("Hide Unknown file types", name)) {
					swissSettings.hideUnknownFileTypes = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable UI Animations", name)) {
					swissSettings.disableUIAnimations = !strcmp("Yes", value);
				}
				else if(!strcmp("Reduce UI Animations", name)) {
					swissSettings.reduceUIAnimations = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Panel Transparency", name)) {
					swissSettings.disablePanelTransparency = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Animated Backdrop", name)) {
					swissSettings.disableAnimatedBackdrop = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Menu Music", name)) {
					swissSettings.disableMenuMusic = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Menu SFX", name)) {
					swissSettings.disableMenuSFX = !strcmp("Yes", value);
				}
				else if(!strcmp("Memory Card Folder Colors", name)) {
					UIFolder_ParseColors(&folderColors, value);
				}
				else if(!strcmp("Menu Color", name)) {
					for(int i = 0; i < UI_COLOR_MAX; i++) {
						if(!strcmp(uiColorStr[i], value)) {
							swissSettings.uiColor = i;
							break;
						}
					}
				}
				else if(!strcmp("Backdrop Color", name)) {
					for(int i = 0; i <= UI_COLOR_MAX; i++) {
						if(!strcmp(uiLayerColorStr[i], value)) {
							swissSettings.uiBackdropColor = i;
							break;
						}
					}
				}
				else if(!strcmp("Wave Color", name)) {
					for(int i = 0; i <= UI_COLOR_MAX; i++) {
						if(!strcmp(uiLayerColorStr[i], value)) {
							swissSettings.uiWaveColor = i;
							break;
						}
					}
				}
				else if(!strcmp("Menu Widescreen", name)) {
					swissSettings.menuWidescreen = !strcmp("Yes", value);
				}
				else if(!strcmp("Library Icon", name)) {
					for(int i = 0; i < UI_HOME_ICON_CHOICES; i++) {
						if(!strcmp(libraryIconStr[i], value)) {
							swissSettings.libraryIcon = i;
							break;
						}
					}
				}
				else if(!strcmp("Source Icon", name)) {
					for(int i = 0; i < UI_HOME_ICON_CHOICES; i++) {
						if(!strcmp(sourceIconStr[i], value)) {
							swissSettings.sourceIcon = i;
							break;
						}
					}
				}
				else if(!strcmp("Settings Icon", name)) {
					for(int i = 0; i < UI_HOME_ICON_CHOICES; i++) {
						if(!strcmp(settingsIconStr[i], value)) {
							swissSettings.settingsIcon = i;
							break;
						}
					}
				}
				else if(!strcmp("System Icon", name)) {
					for(int i = 0; i < UI_HOME_ICON_CHOICES; i++) {
						if(!strcmp(systemIconStr[i], value)) {
							swissSettings.systemIcon = i;
							break;
						}
					}
				}
				else if(!strcmp("Up Face", name) || !strcmp("Left Face", name) ||
						!strcmp("Right Face", name) || !strcmp("Down Face", name)) {
					int *side = name[0] == 'U' ? &swissSettings.upFace :
						name[0] == 'L' ? &swissSettings.leftFace :
						name[0] == 'R' ? &swissSettings.rightFace : &swissSettings.downFace;
					char **names = name[0] == 'U' ? upFaceStr : name[0] == 'L' ? leftFaceStr :
						name[0] == 'R' ? rightFaceStr : downFaceStr;
					for(int i = 0; i < UI_HOME_FACE_COUNT; i++) {
						if(!strcmp(names[i], value)) {
							*side = i;
							sidesRead = true;
							break;
						}
					}
				}
				else if(!strcmp("Hide Apps Face", name)) {
					hideAppsRead = !strcmp("Yes", value);
				}
				else if(!strcmp("Cube", name)) {
					swissSettings.cubeStyle = !strcmp("Classic", value);
				}
				else if(!strcmp("Clock", name)) {
					for(int i = 0; i < CLOCK_POSITION_MAX; i++) {
						if(!strcmp(clockPositionStr[i], value)) {
							swissSettings.clockPosition = clockRead = i;
							break;
						}
					}
				}
				else if(!strcmp("Temperature", name)) {
					for(int i = 0; i < CLOCK_POSITION_MAX; i++) {
						if(!strcmp(clockPositionStr[i], value)) {
							swissSettings.temperaturePosition = i;
							temperatureRead = true;
							break;
						}
					}
				}
				else if(!strcmp("Wave Speed", name)) {
					for(int i = 0; i < WAVE_SPEED_MAX; i++) {
						if(!strcmp(waveSpeedStr[i], value)) {
							swissSettings.waveSpeed = i;
							break;
						}
					}
				}
				else if(!strcmp("Spin Discs", name)) {
					swissSettings.stillDiscs = !strcmp("No", value);
				}
				else if(!strcmp("Library Folders", name)) {
					libraryFolders = !strcmp("Yes", value);
				}
				else if(!strcmp("Hide Saves on Details", name)) {
					swissSettings.hideDetailSaves = !strcmp("Yes", value);
				}
				else if(!strcmp("Always Show Saves on Details", name)) {
					swissSettings.alwaysShowDetailSaves = !strcmp("Yes", value);
				}
				else if(!strcmp("Idle Animation", name)) {
					swissSettings.idleAnimation = !strcmp("Sway", value);
				}
				else if(!strcmp("Hide Waves", name)) {
					swissSettings.hideWaves = !strcmp("Yes", value);
				}
				else if(!strcmp("Hide On-screen Controls", name)) {
					swissSettings.hideHomeControls = !strcmp("Yes", value);
				}
				else if(!strcmp("Hide Face Labels", name)) {
					swissSettings.hideFaceLabels = !strcmp("Yes", value);
				}
				else if(!strcmp("Library Layout", name)) {
					for(int i = 0; i < UI_GAMEFLOW_LAYOUT_COUNT; i++) {
						if(!strcmp(libraryLayoutStr[i], value)) {
							swissSettings.libraryLayout = i;
							break;
						}
					}
				}
				else if(!strcmp("Init DVD Drive at startup", name)) {
					swissSettings.initDVDDriveAtStart = !strcmp("Yes", value);
				}
				else if(!strcmp("Stop DVD Motor on startup", name) || !strcmp("Stop DVD Drive motor", name)) {
					swissSettings.stopMotor = !strcmp("Yes", value);
				}
				else if(!strcmp("Configure Audio Buffer", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(configAudioBufferStr[i], value)) {
							swissSettings.configAudioBuffer = i;
							break;
						}
					}
				}
				else if(!strcmp("Enable WiiRD debug", name)) {
					swissSettings.wiirdDebug = !strcmp("Yes", value);
				}
				else if(!strcmp("Enable File Management", name)) {
					swissSettings.enableFileManagement = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable MemCard PRO GameID", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(disableMCPGameIDStr[i], value)) {
							swissSettings.disableMCPGameID = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Video Patches", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(disableVideoPatchesStr[i], value)) {
							swissSettings.disableVideoPatches = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Video Active", name)) {
					swissSettings.forceVideoActive = !strcmp("Yes", value);
				}
				else if(!strcmp("Force DTV Status", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(forceDTVStatusStr[i], value)) {
							swissSettings.forceDTVStatus = i;
							break;
						}
					}
				}
				else if(!strcmp("Last DTV Status", name)) {
					swissSettings.lastDTVStatus = !strcmp("Yes", value);
				}
				else if(!strcmp("Pause for resolution change", name)) {
					swissSettings.pauseAVOutput = !strcmp("Yes", value);
				}
				else if(!strcmp("AutoBoot", name)) {
					swissSettings.autoBoot = !strcmp("Yes", value);
				}
				else if(!strcmp("AutoCheats", name)) {
					swissSettings.autoCheats = !strcmp("Yes", value);
				}
				else if(!strcmp("InitNetwork", name)) {
					swissSettings.initNetworkAtStart = !strcmp("Yes", value);
				}
				else if(!strcmp("IGRType", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(igrTypeStr[i], value)) {
							swissSettings.igrType = i;
							break;
						}
					}
				}
				else if(!strcmp("AVECompat", name)) {
					for(int i = 0; i < AVE_COMPAT_MAX; i++) {
						if(!strcmp(aveCompatStr[i], value)) {
							swissSettings.aveCompat = i;
							break;
						}
					}
				}
				else if(!strcmp("FileBrowserType", name)) {
					for(int i = 0; i < BROWSER_MAX; i++) {
						if(!strcmp(fileBrowserTypeStr[i], value)) {
							swissSettings.fileBrowserType = i;
							break;
						}
					}
				}
				else if(!strcmp("AppsBrowserType", name)) {
					for(int i = 0; i < BROWSER_MAX; i++) {
						if(!strcmp(fileBrowserTypeStr[i], value)) {
							swissSettings.appsBrowserType = i;
							break;
						}
					}
				}
				else if(!strcmp("GameBrowserType", name)) {
					for(int i = 0; i < BROWSER_MAX; i++) {
						if(!strcmp(fileBrowserTypeStr[i], value)) {
							swissSettings.gameBrowserType = i;
							break;
						}
					}
				}
				else if(!strcmp("BS2Boot", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(bs2BootStr[i], value)) {
							swissSettings.bs2Boot = i;
							break;
						}
					}
				}
				else if(!strcmp("RT4KHostIP", name)) {
					strlcpy(swissSettings.rt4kHostIp, value, sizeof(swissSettings.rt4kHostIp));
				}
				else if(!strcmp("RT4KPort", name)) {
					swissSettings.rt4kPort = atoi(value);
				}
				else if(!strcmp("RT4KOptim", name)) {
					swissSettings.rt4kOptim = !strcmp("Yes", value);
				}
				else if(!strcmp("SMBUserName", name)) {
					strlcpy(swissSettings.smbUser, value, sizeof(swissSettings.smbUser));
				}
				else if(!strcmp("SMBPassword", name)) {
					strlcpy(swissSettings.smbPassword, value, sizeof(swissSettings.smbPassword));
				}
				else if(!strcmp("SMBShareName", name)) {
					strlcpy(swissSettings.smbShare, value, sizeof(swissSettings.smbShare));
				}
				else if(!strcmp("SMBHostIP", name)) {
					strlcpy(swissSettings.smbServerIp, value, sizeof(swissSettings.smbServerIp));
				}
				else if(!strcmp("FTPUserName", name)) {
					strlcpy(swissSettings.ftpUserName, value, sizeof(swissSettings.ftpUserName));
				}
				else if(!strcmp("FTPPassword", name)) {
					strlcpy(swissSettings.ftpPassword, value, sizeof(swissSettings.ftpPassword));
				}
				else if(!strcmp("FTPHostIP", name)) {
					strlcpy(swissSettings.ftpHostIp, value, sizeof(swissSettings.ftpHostIp));
				}
				else if(!strcmp("FTPPort", name)) {
					swissSettings.ftpPort = atoi(value);
				}
				else if(!strcmp("FTPUsePasv", name)) {
					swissSettings.ftpUsePasv = !strcmp("Yes", value);
				}
				else if(!strcmp("FSPHostIP", name)) {
					strlcpy(swissSettings.fspHostIp, value, sizeof(swissSettings.fspHostIp));
				}
				else if(!strcmp("FSPPort", name)) {
					swissSettings.fspPort = atoi(value);
				}
				else if(!strcmp("FSPPassword", name)) {
					strlcpy(swissSettings.fspPassword, value, sizeof(swissSettings.fspPassword));
				}
				else if(!strcmp("FSPPathMTU", name)) {
					swissSettings.fspPathMtu = atoi(value);
				}
				else if(!strcmp("BBALocalIP", name)) {
					strlcpy(swissSettings.bbaLocalIp, value, sizeof(swissSettings.bbaLocalIp));
				}
				else if(!strcmp("BBANetmask", name)) {
					swissSettings.bbaNetmask = atoi(value);
				}
				else if(!strcmp("BBAGateway", name)) {
					strlcpy(swissSettings.bbaGateway, value, sizeof(swissSettings.bbaGateway));
				}
				else if(!strcmp("BBAUseDHCP", name)) {
					swissSettings.bbaUseDhcp = !strcmp("Yes", value);
				}
				else if(!strcmp("NTPSync", name)) {
					swissSettings.ntpSync = !strcmp("Yes", value);
				}
				else if(!strcmp("NTPHostIP", name)) {
					strlcpy(swissSettings.ntpHostIp, value, sizeof(swissSettings.ntpHostIp));
				}
				else if(!strcmp("TimeZone", name)) {
					strlcpy(swissSettings.timeZone, value, sizeof(swissSettings.timeZone));
				}
				else if(!strcmp("LabServer", name)) {
					swissSettings.labServer = !strcmp("Yes", value);
				}
				else if(!strcmp("LogHost", name)) {
					strlcpy(swissSettings.logHost, value, sizeof(swissSettings.logHost));
				}
				else if(!strcmp("ShowHiddenFiles", name)) {
					swissSettings.showHiddenFiles = !strcmp("Yes", value);
				}
				else if(!strcmp("RecentListLevel", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(recentListLevelStr[i], value)) {
							swissSettings.recentListLevel = i;
							break;
						}
					}
				}
				else if(!strcmp("GCLoaderHWVersion", name)) {
					swissSettings.gcloaderHwVersion = atoi(value);
				}
				else if(!strcmp("GCLoaderTopVersion", name)) {
					strlcpy(swissSettings.gcloaderTopVersion, value, sizeof(swissSettings.gcloaderTopVersion));
				}
				else if(!strcmp("Autoload", name)) {
					strlcpy(swissSettings.autoload, value, sizeof(swissSettings.autoload));
				}
				else if(!strcmp("FlattenDir", name)) {
					strlcpy(swissSettings.flattenDir, value, sizeof(swissSettings.flattenDir));
				}
				else if(!strcmp("Save Folder", name)) {
					strlcpy(swissSettings.saveFolder, value, sizeof(swissSettings.saveFolder));
				}
				else if(!strcmp("Library Path", name)) {
					strlcpy(swissSettings.libraryPath, value, sizeof(swissSettings.libraryPath));
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}
	if(settingsFile && clockRead >= 0 && !temperatureRead) {
		swissSettings.temperaturePosition = clockRead;
	}
	if(hideAppsRead && !sidesRead) {
		int *sides[] = {&swissSettings.upFace, &swissSettings.leftFace,
			&swissSettings.rightFace, &swissSettings.downFace};
		char **names[] = {upFaceStr, leftFaceStr, rightFaceStr, downFaceStr};
		for(int i = 0; i < 4; i++) {
			if(strcmp(names[i][*sides[i]], "Apps")) continue;
			for(int choice = 0; choice < UI_HOME_FACE_COUNT; choice++) {
				if(!strcmp(names[i][choice], "None")) *sides[i] = choice;
			}
		}
	}
	if(settingsFile) {
		// FlattenDir was just read as saved: Library Folders takes it from there
		swissSettings.libraryFolders = 0;
		config_set_library_folders(libraryFolders == 1);
	}
	else if(libraryFolders >= 0) {
		config_set_library_folders(libraryFolders);
	}
}

void config_set_library_folders(bool on) {
	// Keep the FlattenDir it replaces, to save and to put back
	if(on && !swissSettings.libraryFolders) {
		/* Off is one list of every game: a FlattenDir that is empty or
		 * already the folders' own pattern keeps the default instead. */
		const char *own = swissSettings.flattenDir;
		if(!own[0] || !strcmp(own, UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN)) {
			own = "*/games";
		}
		strlcpy(swissSettings.libraryFoldersFlattenDir, own,
			sizeof(swissSettings.libraryFoldersFlattenDir));
	}
	if(on) {
		strlcpy(swissSettings.flattenDir, UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN,
			sizeof(swissSettings.flattenDir));
	}
	else if(swissSettings.libraryFolders) {
		strlcpy(swissSettings.flattenDir, swissSettings.libraryFoldersFlattenDir,
			sizeof(swissSettings.flattenDir));
	}
	swissSettings.libraryFolders = on;
}

void config_parse_args(int argc, char *argv[]) {
	if(argc == 0 || argv == NULL) {
		return;
	}
	char *argz;
	size_t argz_len;
	argz_create(&argv[1], &argz, &argz_len);
	argz_stringify(argz, argz_len, '\n');
	if(argz != NULL) {
		config_parse_global(argz, false);
		free(argz);
	}
}

void config_parse_recent(char *configData) {
	char *line, *linectx = NULL;
	line = strtok_r( configData, "\r\n", &linectx );
	while( line != NULL ) {
		//print_debug("Line [%s]\n", line);
		if(line[0] != '#') {
			char *name, *value = NULL;
			name = strtok_r(line, "=", &value);
			
			if(value != NULL) {
				//print_debug("Name [%s] Value [%s]\n", name, value);
				if(!strncmp("Recent_", name, strlen("Recent_"))) {
					int recent_slot = atoi(name+strlen("Recent_"));
					if(recent_slot >= 0 && recent_slot < RECENT_MAX) {
						//print_debug("found recent num %i [%s]\n", recent_slot, value);
						strlcpy(swissSettings.recent[recent_slot], value, PATHNAME_MAX);
					}
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}
}

void config_parse_game(char *configData, ConfigEntry *entry) {
	// Parse each entry and put it into our array
	char *line, *linectx = NULL;
	line = strtok_r( configData, "\r\n", &linectx );
	while( line != NULL ) {
		//print_debug("Line [%s]\n", line);
		if(line[0] != '#') {
			char *name, *value = NULL;
			name = strtok_r(line, "=", &value);
			
			if(value != NULL) {
				//print_debug("Name [%s] Value [%s]\n", name, value);
				if(!strcmp("Name", name)) {
					strncpy(entry->game_name, value, 64);
				}
				else if(!strcmp("Comment", name)) {
					strncpy(entry->comment, value, 128);
				}
				else if(!strcmp("Status", name)) {
					strncpy(entry->status, value, 32);
				}
				else if(!strcmp("Game Language", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(sramLanguageStr[i], value)) {
							entry->gameLanguage = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Video Mode", name)) {
					for(int i = 0; i < 15; i++) {
						if(!strcmp(gameVModeStr[i], value)) {
							entry->gameVMode = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Horizontal Scale", name)) {
					for(int i = 0; i < 9; i++) {
						if(!strcmp(forceHScaleStr[i], value)) {
							entry->forceHScale = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Vertical Offset", name)) {
					entry->forceVOffset = atoi(value);
				}
				else if(!strcmp("Force Vertical Filter", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(forceVFilterStr[i], value)) {
							entry->forceVFilter = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Field Rendering", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(forceVJitterStr[i], value)) {
							entry->forceVJitter = i;
							break;
						}
					}
				}
				else if(!strcmp("Fix Pixel Center", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(fixPixelCenterStr[i], value)) {
							entry->fixPixelCenter = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Alpha Dithering", name)) {
					entry->disableDithering = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Anisotropic Filter", name)) {
					entry->forceAnisotropy = !strcmp("Yes", value);
				}
				else if(!strcmp("Force Widescreen", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(forceWidescreenStr[i], value)) {
							entry->forceWidescreen = i;
							break;
						}
					}
				}
				else if(!strcmp("Force Polling Rate", name)) {
					for(int i = 0; i < 13; i++) {
						if(!strcmp(forcePollRateStr[i], value)) {
							entry->forcePollRate = i;
							break;
						}
					}
				}
				else if(!strcmp("Invert Camera Stick", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(invertCStickStr[i], value)) {
							entry->invertCStick = i;
							break;
						}
					}
				}
				else if(!strcmp("Swap Camera Stick", name)) {
					for(int i = 0; i < 4; i++) {
						if(!strcmp(swapCStickStr[i], value)) {
							entry->swapCStick = i;
							break;
						}
					}
				}
				else if(!strcmp("Digital Trigger Level", name)) {
					entry->triggerLevel = atoi(value);
				}
				else if(!strcmp("Emulate Audio Streaming", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(emulateAudioStreamStr[i], value)) {
							entry->emulateAudioStream = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Read Speed", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(emulateReadSpeedStr[i], value)) {
							entry->emulateReadSpeed = i;
							break;
						}
					}
				}
				else if(!strcmp("Emulate Broadband Adapter", name)) {
					entry->emulateEthernet = !strcmp("Yes", value);
				}
				else if(!strcmp("Disable Memory Card", name)) {
					for(int i = 0; i < 3; i++) {
						if(!strcmp(disableMemoryCardStr[i], value)) {
							entry->disableMemoryCard = i;
							break;
						}
					}
				}
				else if(!strcmp("Disable Hypervisor", name)) {
					entry->disableHypervisor = !strcmp("Yes", value);
				}
				else if(!strcmp("Prefer Clean Boot", name)) {
					entry->preferCleanBoot = !strcmp("Yes", value);
				}
				else if(!strcmp("RetroTINK-4K Profile", name)) {
					entry->rt4kProfile = atoi(value);
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}
}

void config_find(ConfigEntry *entry) {
	// Fill out defaults
	config_defaults(entry);
	
	if(!config_set_device()) {
		return;
	}
	print_debug("config_find: Looking for config file with ID %s\n",entry->game_id);
	// See if we have an actual config file for this game
	concatf_path(txtbuffer, SWISS_GAME_SETTINGS_DIR, "%.4s.ini", entry->game_id);
	char* configEntry = config_file_read(txtbuffer);
	if(configEntry) {
		config_parse_game(configEntry, entry);
		free(configEntry);
	}
	config_unset_device();
}

/** 
	Initialises the configuration file
	Returns 1 on successful file open, 0 otherwise
*/
int config_init(void (*progress_indicator)(char*, int, int)) {
	int res = 0;
	memset(&folderColors, 0, sizeof(folderColors));
	progress_indicator("Loading settings", 1, -2);
	if(!config_set_device()) {
		progress_indicator(NULL, 0, 0);
		return res;
	}
	
	// Make the new settings base dir(s) if we don't have them already
	ensure_path(DEVICE_CONFIG, SWISS_BASE_DIR, NULL, true);
	ensure_path(DEVICE_CONFIG, SWISS_SETTINGS_DIR, NULL, false);
	ensure_path(DEVICE_CONFIG, SWISS_GAME_SETTINGS_DIR, NULL, false);
	
	// Read config (legacy /swiss.ini format)
	char* configData = config_file_read(SWISS_SETTINGS_FILENAME_LEGACY);
	if(configData != NULL) {
		progress_indicator(NULL, 0, 0);
		progress_indicator("Migrating settings to new format.", 1, -1);
		config_parse_legacy(configData, progress_indicator);
		free(configData);
	}
	
	// Read config (new format)
	concat_path(txtbuffer, SWISS_SETTINGS_DIR, SWISS_SETTINGS_FILENAME);
	configData = config_file_read(txtbuffer);
	globalFileLoaded = configData != NULL;
	if(configData != NULL) {
		config_parse_global(configData, true);
		free(configData);
		res = 1;
	}
	
	config_load_play_history();

	// Read the recent list if enabled
	if(swissSettings.recentListLevel > 0) {
		concat_path(txtbuffer, SWISS_SETTINGS_DIR, SWISS_RECENTLIST_FILENAME);
		configData = config_file_read(txtbuffer);
		if(configData != NULL) {
			config_parse_recent(configData);
			free(configData);
		}
	}
	progress_indicator(NULL, 0, 0);
	config_unset_device();
	return res;
}

static char *sramLanguageEnvStr[] = {"en_GB", "de_DE", "fr_FR", "es_ES", "it_IT", "nl_NL", "ja_JP", "en_US"};

void config_init_environ() {
	char *value;
	value = getenv("AVE");
	if(value == NULL || *value == '\0') {
		setenv("AVE", aveCompatStr[swissSettings.aveCompat], 1);
	} else {
		for(int i = 0; i < AVE_COMPAT_MAX; i++) {
			if(!strcmp(aveCompatStr[i], value)) {
				swissSettings.aveCompat = i;
				break;
			}
		}
	}
	value = getenv("LANG");
	if(value == NULL || *value == '\0') {
		sprintf(txtbuffer, "LANG=%s.%s", sramLanguageEnvStr[swissSettings.sramLanguage], nl_langinfo(CODESET));
		putenv(txtbuffer);
	} else {
		char *dot = strchrnul(value, '.');
		for(int i = 0; i < SRAM_LANGUAGE_MAX; i++) {
			if(!strncmp(sramLanguageEnvStr[i], value, dot - value)) {
				swissSettings.sramLanguage = i;
				break;
			}
		}
	}
	value = getenv("CUBEBOOT");
	if(value != NULL) {
		swissSettings.cubebootInvoked = !!atoi(value);
	}
	value = getenv("FLIPPYDRIVE");
	if(value != NULL) {
		swissSettings.hasFlippyDrive = !!atoi(value);
	}
	value = getenv("USBGECKO_CHANNEL");
	if(value != NULL) {
		swissSettings.enableUSBGecko = USBGECKO_MEMCARD_SLOT_A + atoi(value);
	}
	value = getenv("USBGECKO_SAFE");
	if(value != NULL) {
		swissSettings.waitForUSBGecko = !!atoi(value);
	}
	value = getenv("VIDEO_PIXEL_PERFECT");
	if(value != NULL) {
		swissSettings.rt4kOptim = !!atoi(value);
	}
}

void config_update_environ() {
	setenv("AVE", aveCompatStr[swissSettings.aveCompat], 1);
	sprintf(txtbuffer, "LANG=%s.%s", sramLanguageEnvStr[swissSettings.sramLanguage], nl_langinfo(CODESET));
	putenv(txtbuffer);
	
	if(swissSettings.enableUSBGecko) {
		sprintf(txtbuffer, "USBGECKO_CHANNEL=%i", swissSettings.enableUSBGecko - USBGECKO_MEMCARD_SLOT_A);
		putenv(txtbuffer);
		sprintf(txtbuffer, "USBGECKO_SAFE=%i", swissSettings.waitForUSBGecko);
		putenv(txtbuffer);
	} else {
		unsetenv("USBGECKO_CHANNEL");
		unsetenv("USBGECKO_SAFE");
	}
	
	if(in_range(swissSettings.aveCompat, GCDIGITAL_COMPAT, GCVIDEO_COMPAT) && swissSettings.rt4kOptim)
		setenv("VIDEO_PIXEL_PERFECT", "1", 1);
	else
		unsetenv("VIDEO_PIXEL_PERFECT");
	
	unsetenv("WIILOAD");
	
	__PADDisableRecalibration(swissSettings.disableRecalibration);
	__PADDisableRumble(swissSettings.disableRumble);
}

SwissSettings backup;
static char gameVModePalEntries[][4] = {"DLSP", "G3FD", "G3FF", "G3FP", "G3FS", "GLRD", "GLRF", "GLRP", "GM8P", "GSWD", "GSWF", "GSWI", "GSWP", "GSWS"};

void config_load_current(ConfigEntry *entry) {
	wait_network();
	// load settings for this game to current settings
	memcpy(&backup, &swissSettings, sizeof(SwissSettings));
	swissSettings.gameVMode = entry->gameVMode;
	swissSettings.forceHScale = entry->forceHScale;
	swissSettings.forceVOffset = entry->forceVOffset;
	swissSettings.forceVFilter = entry->forceVFilter;
	swissSettings.forceVJitter = entry->forceVJitter;
	swissSettings.fixPixelCenter = entry->fixPixelCenter;
	swissSettings.disableDithering = entry->disableDithering;
	swissSettings.forceAnisotropy = entry->forceAnisotropy;
	swissSettings.forceWidescreen = entry->forceWidescreen;
	swissSettings.fontEncode = entry->region == 'J' ? SYS_FONTENC_SJIS : SYS_FONTENC_ANSI;
	swissSettings.forcePollRate = entry->forcePollRate;
	swissSettings.invertCStick = entry->invertCStick;
	swissSettings.swapCStick = entry->swapCStick;
	swissSettings.triggerLevel = entry->triggerLevel;
	swissSettings.emulateAudioStream = entry->emulateAudioStream;
	swissSettings.emulateReadSpeed = entry->emulateReadSpeed;
	swissSettings.emulateEthernet = entry->emulateEthernet;
	swissSettings.disableMemoryCard = entry->disableMemoryCard;
	swissSettings.disableHypervisor = entry->disableHypervisor;
	
	if(entry->region == 'P')
		swissSettings.sram60Hz = getTVFormat() != VI_PAL;
	else if(!strchr("A?", entry->region))
		swissSettings.sram60Hz = 0;
	
	if(!strchr("PA?", entry->region) && in_range(swissSettings.sramLanguage, SYS_LANG_ENGLISH, SYS_LANG_DUTCH))
		swissSettings.sramLanguage = SYS_LANG_ENGLISH;
	
	if(in_range(entry->gameLanguage, SYS_LANG_ENGLISH, SYS_LANG_ENGLISH_US)) {
		swissSettings.sramLanguage = entry->gameLanguage;
		swissSettings.fontEncode = entry->gameLanguage == SYS_LANG_JAPANESE ? SYS_FONTENC_SJIS : SYS_FONTENC_ANSI;
	}
	
	if(entry->region == 'P')
		swissSettings.sramVideo = SYS_VIDEO_PAL;
	else if((swissSettings.sramVideo == SYS_VIDEO_PAL && !strchr("A?", entry->region)) ||
			(swissSettings.sramVideo == SYS_VIDEO_MPAL && getDTVStatus()) ||
			swissSettings.fontEncode == SYS_FONTENC_SJIS)
		swissSettings.sramVideo = SYS_VIDEO_NTSC;
	
	if(swissSettings.gameVMode > 0 && swissSettings.disableVideoPatches < 2) {
		swissSettings.sram60Hz = in_range(swissSettings.gameVMode, 1, 7);
		swissSettings.sramProgressive = in_range(swissSettings.gameVMode, 4, 7) || in_range(swissSettings.gameVMode, 11, 14);
		
		if(swissSettings.sram60Hz) {
			for(int i = 0; i < countof(gameVModePalEntries); i++) {
				if(!strncmp(entry->game_id, gameVModePalEntries[i], 4)) {
					swissSettings.gameVMode += 7;
					break;
				}
			}
		}
		
		if(swissSettings.sramProgressive && !getDTVStatus())
			swissSettings.gameVMode = 0;
		
		if(swissSettings.sramVideo == SYS_VIDEO_PAL)
			swissSettings.sramProgressive &= swissSettings.sram60Hz;
		else
			swissSettings.sram60Hz = 0;
	} else if(swissSettings.sramProgressive) {
		if(swissSettings.sramVideo == SYS_VIDEO_PAL) {
			swissSettings.sramProgressive = 0;
			swissSettings.gameVMode = -2;
		} else
			swissSettings.gameVMode = -1;
	} else
		swissSettings.gameVMode = 0;
	
	if(!strncmp(entry->game_id, "GB3E", 4))
		swissSettings.sramProgressive = 0;
	
	switch(swissSettings.gameVMode) {
		case 2: case 9: case 5: case 12: case -1: case -2:
			if(!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			break;
		case 3: case 10:
			swissSettings.forceVOffset &= ~1;
			if(!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if(!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 2;
			break;
		case 4: case 11:
			swissSettings.forceVOffset &= ~1;
			if(!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if(!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 1;
			break;
		case 6: case 13:
			if(!swissSettings.forceHScale)
				swissSettings.forceHScale = 1;
			swissSettings.forceVOffset &= ~1;
			if(!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if(!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 1;
			break;
		case 7: case 14:
			if(!swissSettings.forceHScale)
				swissSettings.forceHScale = 1;
			swissSettings.forceVOffset &= ~1;
			if(!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			break;
	}
}

void config_unload_current() {
	swissSettings.gameVMode = backup.gameVMode;
	swissSettings.forceHScale = backup.forceHScale;
	swissSettings.forceVOffset = backup.forceVOffset;
	swissSettings.forceVFilter = backup.forceVFilter;
	swissSettings.forceVJitter = backup.forceVJitter;
	swissSettings.fixPixelCenter = backup.fixPixelCenter;
	swissSettings.disableDithering = backup.disableDithering;
	swissSettings.forceAnisotropy = backup.forceAnisotropy;
	swissSettings.forceWidescreen = backup.forceWidescreen;
	swissSettings.fontEncode = backup.fontEncode;
	swissSettings.forcePollRate = backup.forcePollRate;
	swissSettings.invertCStick = backup.invertCStick;
	swissSettings.swapCStick = backup.swapCStick;
	swissSettings.triggerLevel = backup.triggerLevel;
	swissSettings.wiirdDebug = backup.wiirdDebug;
	swissSettings.sram60Hz = backup.sram60Hz;
	swissSettings.sramProgressive = backup.sramProgressive;
	swissSettings.emulateAudioStream = backup.emulateAudioStream;
	swissSettings.emulateReadSpeed = backup.emulateReadSpeed;
	swissSettings.emulateEthernet = backup.emulateEthernet;
	swissSettings.disableMemoryCard = backup.disableMemoryCard;
	swissSettings.disableHypervisor = backup.disableHypervisor;
	swissSettings.sramLanguage = backup.sramLanguage;
	swissSettings.sramVideo = backup.sramVideo;
}
