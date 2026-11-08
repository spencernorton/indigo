/* saves.c - Memory Cards (Home > System > Memory Cards)

   The saves on the memory cards in Slot A and Slot B and in folders on the
   device Indigo keeps its settings on (and on the Source, when that is
   another FatFs device), as the IPL's Memory Card screen shows two cards:
   two stacks of save cubes, either of them Slot A, Slot B or the SD card,
   with Move, Copy and Erase from one to the other. The card and FAT
   drivers do the reading and writing, as they do for the file browser's
   Copy; a card image on the SD card changes in place (saves_raw.c). This
   file decides what to call, reads every copy back before calling it done,
   and removes a Move's original only after that. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include <ogc/card.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "config.h"
#include "files.h"
#include "util.h"
#include "input.h"
#include "ui_cheats.h"
#include "ui_menu_input.h"
#include "menuaudio.h"
#include "ui_saves.h"
#include "ui_lang.h"
#include "ui_saves_metadata.h"
#include "ui_saves_details.h"
#include "ui_presentation.h"
#include "ui_settings_layout.h"
#include "saves.h"
#include "saves_raw.h"
#include "ui_folder.h"

#define SAVES_DEFAULT_FOLDER "swiss/saves"
#define SAVES_LIST_MAX 256
#define SAVES_TABS 4
#define SAVES_TAB_FOLDER 2
/* A card's first five blocks hold its header, directory and block map. */
#define SAVES_SYSTEM_BLOCKS 5
/* The largest save a card holds (2043 blocks) in the largest wrapper. */
#define SAVES_MAX_BYTES (0x150u + 2043u * UI_SAVES_BLOCK_SIZE)

typedef struct {
	DEVICEHANDLER_INTERFACE *device;
	file_handle dir;			/* the folder listed */
	file_handle *entries;			/* readDir's */
	int entryCount;
	file_handle *list[SAVES_LIST_MAX];	/* what the page lists, in order */
	int count;
	int selection;
	int top;				/* the cube window's first row */
	bool rawOpen;
	file_handle rawImage;
	uiSavesRawCard_t *rawCard;
	int rawReturn;
	bool ready;
	bool mounted;				/* this screen mounted the card */
	int totalBlocks;
	int freeBlocks;
	u32 listing;				/* which listing this is, for the page */
	u8 change;				/* how the listing came: uiSaveCubesChange_t */
	s16 changeAt;				/* the cell a save went from or came to */
	char note[2][96];			/* why nothing is listed */
} savesPlace_t;

typedef struct {
	uiMenuInputState_t menu;
	u32 lastRetrace;
	u32 repeatHeld;
	u32 repeatTime;
	bool repeated;
	u32 quiet;				/* VSyncs in a row with nothing held */
} savesInput_t;

/* A save's art as the pool holds it. */
typedef struct {
	bool failed;				/* unreadable or nonsense: not read again */
	uiSavesArt_t art;
	char line[2][33];			/* its comment's two lines */
} savesSlot_t;

static savesPlace_t places[SAVES_TABS];
static savesPlace_t chooser;
static u32 listings;
static uiSavesPageSnapshot_t shown;
static uiSaveCubesPageSnapshot_t screen;
/* The folder the SD card's stack opens at, the Save Folder: A opens the
 * folders in it and B goes back up, no further. */
static char folderHome[2][PATHNAME_MAX];
/* The settings device is mounted, so the SD card's place can be listed. */
static bool foldersReady;
/* The devices the SD card's stacks can show: the settings device, then the
 * Source when it is another FatFs device that takes writes
 * (Saves_SourceDevice, which Game Details reads too). Swiss's Emulate
 * Memory Card keeps a game's card images on the device the game starts
 * from, so with settings on one card and games on another, they are on the
 * Source. The page mounts the settings device; the Source is mounted
 * already. */
static DEVICEHANDLER_INTERFACE *sdDevice[2];
static int sdDevices;
/* What the screen last showed, so a box, an operation or a message can show
 * it again. */
static int screenStacks[UI_SAVE_CUBES_STACKS];
static int screenFocus = -1;
static uiDrawObj_t *screenPage;
/* What goes over it: an operation in flight, the box beside the focused
 * cube and its ghost, a message, the leaving. */
static struct {
	uiSaveCubesOp_t op;
	uiSaveCubesMenu_t menu;
	u8 menuOpen, menuFocus;
	u16 menuSerial;
	u8 ghost;
	s16 ghostCell;
	char reason[96];			/* why the focused item can't be chosen */
	char message[96];
	u8 leaving;
	s8 storageStack;		/* chooser column, even without a card */
} over;
/* Each place's cubes as ids, before an operation and after, to tell what
 * moved. */
static u32 placeIds[SAVES_TABS][SAVES_LIST_MAX];
static int placeIdCount[SAVES_TABS];

const char *saves_folder(void)
{
	return swissSettings.saveFolder[0] ? swissSettings.saveFolder :
		SAVES_DEFAULT_FOLDER;
}

static bool isCard(const DEVICEHANDLER_INTERFACE *device)
{
	return device == &__device_card_a || device == &__device_card_b;
}

static DEVICEHANDLER_INTERFACE *slotDevice(int slot)
{
	return slot ? &__device_card_b : &__device_card_a;
}

static const char *slotName(int slot)
{
	return UI_T(slot ? "Slot B" : "Slot A");
}

static bool isSaveName(const char *name)
{
	size_t length = strlen(name);

	return length > 4u && (!strcasecmp(name + length - 4u, ".gci") ||
		!strcasecmp(name + length - 4u, ".gcs") ||
		!strcasecmp(name + length - 4u, ".sav"));
}

/* Synthetic .gci handles never reach a device handler: their bytes come
 * from the selected image's checked block chain. */
static savesPlace_t *rawSource(const file_handle *save, unsigned *ordinal)
{
	int tab, i;

	for(tab = SAVES_TAB_FOLDER; tab < SAVES_TABS; tab++) {
		savesPlace_t *place = &places[tab];

		if(!place->rawOpen || place->rawCard == NULL) continue;
		for(i = 0; i < place->entryCount; i++) {
			if(save == &place->entries[i]) {
				if(ordinal != NULL) *ordinal = (unsigned)i;
				return place;
			}
		}
	}
	return NULL;
}

static bool readSaveAt(file_handle *save, u32 offset, void *data, u32 length)
{
	unsigned ordinal;
	savesPlace_t *raw = rawSource(save, &ordinal);
	bool card = isCard(save->device), ok;

	if(raw != NULL) {
		return SavesRaw_ReadGci(&raw->rawImage, raw->rawCard, ordinal, offset,
			data, length);
	}
	if(card) setCopyGCIMode(true);
	ok = save->device->seekFile(save, offset, DEVICE_HANDLER_SEEK_SET) == offset &&
		save->device->readFile(save, data, length) == (s32)length;
	if(card) setCopyGCIMode(false);
	save->device->closeFile(save);
	return ok;
}

/* One-time folder previews read only the checked directory entry and, when
 * present, its first comment line. No icon pixels or full save payload are loaded. */
static void folderSaveTitle(file_handle *source, const uiSavesRawCard_t *raw,
	unsigned ordinal, char *out, size_t capacity)
{
	file_handle file = *source;
	u8 head[UI_SAVES_HEAD_SIZE] ATTRIBUTE_ALIGN(32);
	u8 entry[UI_SAVES_ENTRY_SIZE], comment[32] ATTRIBUTE_ALIGN(32);
	uiSavesArt_t art;
	size_t body = 0u;
	bool hasComment = false;
	file.fp = NULL; file.ffsFp = NULL; file.meta = NULL; file.uiObj = NULL;
	if(raw != NULL) {
		const u8 *selected = UISavesRaw_Entry(raw, ordinal);
		if(selected != NULL) {
			memcpy(entry, selected, sizeof(entry));
			body = UI_SAVES_ENTRY_SIZE;
			if(UISaves_ArtLayout(entry, UISavesRaw_GciSize(raw, ordinal) - body, &art) &&
				art.comment) hasComment = SavesRaw_ReadGci(&file, raw, ordinal,
					(u32)body + art.commentAt, comment, sizeof(comment));
		}
	}
	else {
		u32 length = file.size < sizeof(head) ? file.size : sizeof(head);
		if(readSaveAt(&file, 0u, head, length))
			body = UISaves_FindEntryPrefix(head, length, file.size, entry);
		if(body != 0u && UISaves_ArtLayout(entry, file.size - body, &art) && art.comment)
			hasComment = readSaveAt(&file, (u32)body + art.commentAt, comment, sizeof(comment));
	}
	if(body != 0u && UISaves_DisplayTitle(out, capacity, entry,
		hasComment ? comment : NULL, hasComment ? sizeof(comment) : 0u)) return;
	const char *leaf = getRelativeName(source->name);
	if(!UISaves_DisplayText(out, capacity, (const u8*)leaf, strnlen(leaf, PATHNAME_MAX)))
		snprintf(out, capacity, UI_T("Unnamed save"));
}

static bool folderIdentity(const file_handle *entry)
{
	return entry != NULL && entry->fileType != IS_SPECIAL &&
		(entry->fileType == IS_DIR || (entry->fileType == IS_FILE &&
		SavesRaw_IsImageName(entry->name))) && !isCard(entry->device);
}

static void folderContents(uiFolderSnapshot_t *snapshot, file_handle *chosen)
{
	file_handle source = *chosen;
	source.fp = NULL; source.ffsFp = NULL; source.meta = NULL; source.uiObj = NULL;
	if(SavesRaw_IsImageName(source.name)) {
		uiSavesRawCard_t *card = calloc(1, sizeof(*card));
		uiSavesRawStatus_t status = card != NULL ? SavesRaw_Load(&source, card) :
			UI_SAVES_RAW_READ_ERROR;
		if(status == UI_SAVES_RAW_OK) {
			snprintf(snapshot->summary, sizeof(snapshot->summary), UI_T("%u saves. %s"),
				(unsigned)card->count, SavesRaw_Writable(&source) ?
				"Card image." : "Read-only card image.");
			for(unsigned i = 0; i < card->count && i < 2u; ++i)
				folderSaveTitle(&source, card, i, snapshot->contents[i], sizeof(snapshot->contents[i]));
			if(card->count == 0u) strcpy(snapshot->contents[0], "This card image has no saves.");
		}
		else {
			strcpy(snapshot->summary, "Card image. Contents unavailable.");
			snprintf(snapshot->contents[0], sizeof(snapshot->contents[0]), "%s", UISavesRaw_StatusText(status));
		}
		strcpy(snapshot->status, "Color is saved in Indigo settings, not in the card image.");
		free(card);
		return;
	}
	file_handle *entries = NULL;
	int count = source.device != NULL && source.device->readDir != NULL ?
		source.device->readDir(&source, &entries, -1) : -1;
	if(count < 0 || (count > 0 && entries == NULL)) {
		strcpy(snapshot->summary, "This folder's contents could not be read.");
		free(entries);
		return;
	}
	unsigned saves = 0u, images = 0u, folders = 0u, shown = 0u;
	const int maximum = 4096;
	for(int i = 0; i < count && i < maximum; ++i) {
		file_handle *entry = &entries[i];
		const char *leaf = getRelativeName(entry->name);
		if(entry->fileType == IS_SPECIAL || leaf[0] == '.') continue;
		if(entry->fileType == IS_DIR) ++folders;
		else if(entry->fileType == IS_FILE && isSaveName(leaf)) {
			++saves;
			if(shown < 2u) folderSaveTitle(entry, NULL, 0u,
				snapshot->contents[shown++], sizeof(snapshot->contents[0]));
		}
		else if(entry->fileType == IS_FILE && SavesRaw_IsImageName(leaf)) ++images;
	}
	snprintf(snapshot->summary, sizeof(snapshot->summary), UI_T("%u save files, %u card images, %u folders%s"),
		saves, images, folders, count > maximum ? " (preview limit)" : "");
	if(shown == 0u) strcpy(snapshot->contents[0], images > 0u ?
		"Open a card image to browse its saves." : "No save files in this folder.");
	for(int i = 0; i < count; ++i) {
		if(source.device->closeFile != NULL) source.device->closeFile(&entries[i]);
	}
	free(entries);
	if(source.device != NULL && source.device->closeFile != NULL) source.device->closeFile(&source);
}

/* ------------------------------------------------------------------------
 * Saves' art: banners, icons and comments, read one save at a time while
 * nothing is pressed, into a slot for each save on screen.
 * --------------------------------------------------------------------- */
/* Two stacks of six rows of four: the window and a row either side. */
#define SAVES_SLOTS 48
/* A slot's texels, all RGB5A3: the icon's 8 frames, then the banner. */
#define SAVES_SLOT_BYTES (UI_SAVES_ICON_FRAMES * UI_SAVES_ICON_BYTES + \
	UI_SAVES_BANNER_BYTES)
/* What one save's art is read into: a wrapper's header and entry, then the
 * part of the save the art spans. */
#define SAVES_SCRATCH_BYTES (UI_SAVES_HEAD_SIZE + UI_SAVES_ART_MAX_END)
/* VSyncs in a row with no button or stick held before a save is read while
 * scrolling. A read holds input up for tens of milliseconds, so a tap never
 * falls in one. */
#define SAVES_QUIET 15

static u32 slotTags[SAVES_SLOTS];		/* each slot's save, 0 for none */
static savesSlot_t slots[SAVES_SLOTS];
/* A ceiling chosen on purpose: one 1.03 MiB block while the page is open,
 * as big as the Library's poster reservation, and one slot more for a cube
 * in flight (operations come one at a time). A copy it would crowd has it
 * let go first (artRoom); a shared pool of 2 KiB frames is the way on if
 * that is ever too often. */
static u8 *pool;				/* SAVES_SLOTS + 1 x SAVES_SLOT_BYTES, or NULL */
#define SAVES_FLIGHT (pool + SAVES_SLOTS * SAVES_SLOT_BYTES)
static savesSlot_t flight;			/* the art of the save in flight */
static u8 *scratch;				/* SAVES_SCRATCH_BYTES, or NULL */
/* The saves on screen, nearest the focus first. placeClear empties it, as
 * the handles point into the listing it frees. */
static file_handle *wanted[SAVES_SLOTS];
static u32 wantTags[SAVES_SLOTS];
static int wantCount;
/* A stack has a new listing (the screen opening, L or R, a folder opened or
 * left, a place read again): its saves are read one after another at once,
 * not after SAVES_QUIET, so their icons come in with their cubes. */
static bool artFresh;

/* Which save this is, for its slot: its place, then its game, maker and
 * name on a card or its path in a folder, then its size. */
static u32 saveTag(int tab, const file_handle *save)
{
	u8 place = (u8)tab;
	u32 tag = UISaves_Id(UI_SAVES_ID_START, &place, 1u);
	unsigned ordinal;
	savesPlace_t *raw = rawSource(save, &ordinal);

	if(raw != NULL) {
		tag = UISaves_Id(tag, raw->rawImage.name, strlen(raw->rawImage.name));
		tag = UISaves_Id(tag, UISavesRaw_Entry(raw->rawCard, ordinal),
			UI_SAVES_ENTRY_SIZE);
	}
	else if(isCard(save->device)) {
		const card_dir *dir = (const card_dir *)save->other;

		tag = UISaves_Id(tag, dir->gamecode, 4u);
		tag = UISaves_Id(tag, dir->company, 2u);
		tag = UISaves_Id(tag, dir->filename, strnlen(dir->filename,
			CARD_FILENAMELEN));
	}
	else {
		tag = UISaves_Id(tag, save->name, strlen(save->name));
	}
	return UISaves_Id(tag, &save->size, sizeof(save->size));
}

/* A folder's parent ("..") is no cube: B goes up instead. */
static int placeSkip(const savesPlace_t *place)
{
	return place->count > 0 && place->list[0]->fileType == IS_SPECIAL;
}

/* The save or folder in a place's cell, or NULL for a free cell. */
static file_handle *placeAt(const savesPlace_t *place, int cell)
{
	int at = cell + placeSkip(place);

	return cell >= 0 && at < place->count ? place->list[at] : NULL;
}

/* Adds the saves in the rows a place's stack draws (its window and a row
 * each side) to those on screen, nearest focus (a cell, or -1) first. */
static void artWant(int tab, int focus)
{
	savesPlace_t *place = &places[tab];
	int order[UI_SAVE_CUBES_DRAWN];
	int count = UISaves_LoadOrder(focus, place->top - 1,
		UI_SAVE_CUBES_DRAWN_ROWS, UI_SAVE_CUBES_COLUMNS,
		place->count - placeSkip(place), order);
	int i;

	for(i = 0; i < count && wantCount < SAVES_SLOTS; i++) {
		file_handle *save = placeAt(place, order[i]);

		if(save->fileType == IS_FILE && !SavesRaw_IsImageName(save->name)) {
			wanted[wantCount] = save;
			wantTags[wantCount++] = saveTag(tab, save);
		}
	}
}

/* Reads save's art into slot s: its entry, then the part of its data the art
 * spans, decoded into the slot's texels, and its comment. A card's save is
 * read as a .gci, entry first, so one Swiss wrote isn't read 8 KiB in. A save
 * that can't be read, or whose entry makes no sense, stays failed: it isn't
 * read again while it's on screen, as the card driver's readFile leaks a
 * buffer on an error. */
static void artRead(file_handle *save, int s, u32 tag)
{
	savesSlot_t *slot = &slots[s];
	u8 *texels = pool != NULL ? pool + s * SAVES_SLOT_BYTES : NULL;
	bool card = isCard(save->device);
	u8 entry[UI_SAVES_ENTRY_SIZE];
	size_t at = 0u;
	u32 want;
	s32 got;
	int i;

	memset(slot, 0, sizeof(*slot));
	slot->failed = true;
	if(card) {
		/* The entry alone: the driver writes it before any block. It takes
		 * banner_fmt from libogc2's CARD_GetStatus, which keeps only the
		 * banner's bits when there is a banner, so a card's bouncing icon
		 * plays as a loop here; a file's keeps its bounce. */
		got = readSaveAt(save, 0u, scratch, UI_SAVES_ENTRY_SIZE) ?
			UI_SAVES_ENTRY_SIZE : -1;
		if(got == UI_SAVES_ENTRY_SIZE) {
			memcpy(entry, scratch, UI_SAVES_ENTRY_SIZE);
			at = UI_SAVES_ENTRY_SIZE;
		}
	}
	else {
		want = save->size < UI_SAVES_HEAD_SIZE ? save->size : UI_SAVES_HEAD_SIZE;
		got = readSaveAt(save, 0u, scratch, want) ? (s32)want : -1;
		if(got == (s32)want) {
			at = UISaves_FindEntryPrefix(scratch, want, save->size, entry);
		}
	}
	if(at != 0u) {
		u32 dataLength = card ? save->size : save->size - at;
		UISaves_DisplayTitle(slot->line[0], sizeof(slot->line[0]), entry, NULL, 0u);
		if(UISaves_ArtLayout(entry, dataLength, &slot->art)) {
			want = (u32)at + slot->art.end;
			got = readSaveAt(save, 0u, scratch, want) ? (s32)want : -1;
			slot->failed = got != (s32)want;
		}
		else if((entry[0x07] & 3u) == 0u &&
			!strcmp(UISaves_IconDescription(entry, dataLength, false), "None stored")) {
			/* A valid save can deliberately have neither icons nor comments. */
			slot->failed = false;
		}
	}
	if(!slot->failed) {
		const u8 *data = scratch + at;
		const uiSavesArt_t *art = &slot->art;

		if(art->comment) {
			UISaves_DisplayTitle(slot->line[0], sizeof(slot->line[0]), entry,
				data + art->commentAt, 32u);
			UISaves_DisplayText(slot->line[1], sizeof(slot->line[1]),
				data + art->commentAt + 32u, 32u);
		}
		if(texels != NULL) {
			for(i = 0; i < (int)art->frames; i++) {
				if(art->frameFormat[i] != UI_SAVES_ART_NONE &&
					!UISaves_ToRgb5a3(data, art->end, art, i,
						texels + i * UI_SAVES_ICON_BYTES)) {
					slot->failed = true;
				}
			}
			if(art->bannerFormat != UI_SAVES_ART_NONE &&
				!UISaves_ToRgb5a3(data, art->end, art, UI_SAVES_ART_BANNER,
					texels + UI_SAVES_ICON_FRAMES * UI_SAVES_ICON_BYTES)) {
				slot->failed = true;
			}
			DCFlushRange(texels, SAVES_SLOT_BYTES);
		}
	}
	slotTags[s] = tag;
}

/* The slot holding the save tag names, or -1. */
static int artSlot(u32 tag)
{
	int s;

	for(s = 0; s < SAVES_SLOTS; s++) {
		if(slotTags[s] == tag) {
			return s;
		}
	}
	return -1;
}

/* The loader: reads the art of the first save on screen that no slot holds
 * yet into a slot that holds no save on screen. Returns whether it read
 * one. Writing a slot is safe only because no snapshot the video thread can
 * draw names a save that isn't on screen: DrawUpdate copies a snapshot in
 * under _videomutex, which the video thread holds from the start of a frame
 * to GX_DrawDone. Drawing outside that lock would turn this into a stale
 * texture or a use after free. */
static bool artLoad(void)
{
	int i, s;

	if(scratch == NULL) {
		return false;
	}
	for(i = 0; i < wantCount; i++) {
		/* Read already, or failed: never again while it's on screen. */
		if(artSlot(wantTags[i]) >= 0) {
			continue;
		}
		s = UISaves_SlotPick(slotTags, SAVES_SLOTS, wantTags, wantCount);
		if(s < 0) {
			return false;
		}
		artRead(wanted[i], s, wantTags[i]);
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------------
 * Input: the cheat browser's, one press at a time with held directions
 * repeating on the shared schedule.
 * --------------------------------------------------------------------- */
#define SAVES_DIRECTIONS (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)
#define SAVES_BUTTONS (SAVES_DIRECTIONS | BUTTON_A | BUTTON_B | BUTTON_X | BUTTON_Y | \
	BUTTON_L | BUTTON_R)

static u32 inputElapsed(u32 *lastRetrace)
{
	u32 retrace = VIDEO_GetRetraceCount();
	u32 count = retrace - *lastRetrace;
	float rate = VIDEO_GetRetraceRate();
	float elapsed;

	*lastRetrace = retrace;
	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	elapsed = (float)count * 1000000.0f / rate;
	return elapsed >= (float)UI_MENU_INPUT_MAX_ELAPSED_US ?
		UI_MENU_INPUT_MAX_ELAPSED_US : (u32)elapsed;
}

/* Presses from before (the A that opened this) aren't for here. */
static void inputInit(savesInput_t *input)
{
	memset(input, 0, sizeof(*input));
	UIMenuInput_Init(&input->menu);
	padsButtonsTaken(SAVES_BUTTONS);
	input->lastRetrace = VIDEO_GetRetraceCount();
}

/* The buttons pressed, or 0 when it read a save's art while waiting and the
 * page should be built again. */
static u32 inputNext(savesInput_t *input)
{
	while(1) {
		u32 held, pressed, elapsed, direction;
		uiMenuInputDirection_t analog;
		bool fresh = artFresh;

		/* A new stack's saves are read back to back. */
		if(!fresh) {
			VIDEO_WaitVSync();
		}
		held = padsButtonsHeld() & SAVES_BUTTONS;
		/* Taken from the scans, not from what is held now: a press made
		 * and let go while a card was read is still seen. */
		pressed = padsButtonsTaken(SAVES_BUTTONS);
		elapsed = inputElapsed(&input->lastRetrace);
		analog = padsMenuInputPoll(&input->menu, elapsed,
			UI_MENU_INPUT_AXIS_BOTH | UI_MENU_INPUT_REPEAT, held != 0u);
		direction = held & SAVES_DIRECTIONS;
		if(direction != input->repeatHeld) {
			input->repeatHeld = direction;
			input->repeatTime = 0u;
			input->repeated = false;
		}
		else if(direction != 0u && (held & ~SAVES_DIRECTIONS) == 0u) {
			input->repeatTime += elapsed;
			if(input->repeatTime >= (input->repeated ? UI_MENU_INPUT_REPEAT_US :
					UI_MENU_INPUT_INITIAL_REPEAT_US)) {
				pressed |= direction;
				input->repeatTime = 0u;
				input->repeated = true;
			}
		}
		if(analog == UI_MENU_INPUT_UP) pressed |= BUTTON_UP;
		if(analog == UI_MENU_INPUT_DOWN) pressed |= BUTTON_DOWN;
		if(analog == UI_MENU_INPUT_LEFT) pressed |= BUTTON_LEFT;
		if(analog == UI_MENU_INPUT_RIGHT) pressed |= BUTTON_RIGHT;
		if(held != 0u || input->menu.owner != UI_MENU_INPUT_NO_OWNER) {
			input->quiet = 0u;
		}
		if(pressed != 0u) {
			return pressed;
		}
		/* Idle: a new stack's saves' art at once, whatever is held;
		 * otherwise one save's a VSync, once nothing has been held for
		 * SAVES_QUIET of them. */
		if(!fresh && input->quiet < SAVES_QUIET) {
			input->quiet++;
		}
		else if(artLoad()) {
			return 0u;
		}
		else {
			artFresh = false;
		}
	}
}

/* A message in the dialog card; its last line becomes the A prompt. */
static void savesTell(int type, const char *text)
{
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(type, text));

	wait_press_A();
	DrawDispose(box);
}

/* ------------------------------------------------------------------------
 * Places: a card, or a folder on the settings device.
 * --------------------------------------------------------------------- */

/* Lets go of what a place listed. */
static void placeClear(savesPlace_t *place)
{
	int i;

	wantCount = 0;
	for(i = 0; i < place->entryCount; i++) {
		if(place->device != NULL && !place->rawOpen) {
			place->device->closeFile(&place->entries[i]);
		}
	}
	free(place->entries);
	place->entries = NULL;
	free(place->rawCard);
	place->rawCard = NULL;
	place->rawOpen = false;
	place->entryCount = 0;
	place->count = 0;
	place->ready = false;
	place->listing = ++listings;
	artFresh = true;
	/* Another listing comes in from nothing, unless placesReload says how
	 * it moved. */
	place->change = UI_SAVE_CUBES_NEW;
	place->note[0][0] = place->note[1][0] = '\0';
}

/* A device that isn't this card but sits in its slot, like an SD adapter
 * that is the source or holds the settings, is never probed as a card. */
static const char *slotTakenBy(int slot)
{
	u32 location = slot ? LOC_MEMCARD_SLOT_B : LOC_MEMCARD_SLOT_A;
	DEVICEHANDLER_INTERFACE *users[2] = {devices[DEVICE_CUR],
		devices[DEVICE_CONFIG]};
	int i;

	for(i = 0; i < 2; i++) {
		if(users[i] != NULL && users[i] != slotDevice(slot) &&
			(users[i]->location & location)) {
			return users[i]->hwName;
		}
	}
	return NULL;
}

static void loadCard(savesPlace_t *place, int slot)
{
	DEVICEHANDLER_INTERFACE *device = slotDevice(slot);
	const char *taken = slotTakenBy(slot);
	device_info *info;
	int used = 0;
	int i;

	placeClear(place);
	place->device = device;
	if(taken != NULL) {
		snprintf(place->note[0], sizeof(place->note[0]), UI_T("%s holds the %s"),
			slotName(slot), taken);
		snprintf(place->note[1], sizeof(place->note[1]),
			"It's in use, so Memory Cards leaves it alone.");
		return;
	}
	/* Mounting again finds a card put in since; the driver keeps a mount
	 * that's still good. */
	if(device->init(device->initial) != 0) {
		switch(device->initial->status) {
			case CARD_ERROR_NOCARD:
				snprintf(place->note[0], sizeof(place->note[0]),
					"Nothing is inserted in %s.", slotName(slot));
				snprintf(place->note[1], sizeof(place->note[1]),
					"Choose storage above to browse virtual cards.");
				break;
			case CARD_ERROR_WRONGDEVICE:
				snprintf(place->note[0], sizeof(place->note[0]),
					"%s holds something else", slotName(slot));
				snprintf(place->note[1], sizeof(place->note[1]),
					"It isn't a Memory Card.");
				break;
			default:
				snprintf(place->note[0], sizeof(place->note[0]),
					"The Memory Card in %s can't be read", slotName(slot));
				snprintf(place->note[1], sizeof(place->note[1]),
					"It may need checking on the console's own screen.");
				break;
		}
		return;
	}
	place->mounted = true;
	place->entryCount = device->readDir(device->initial, &place->entries, -1);
	if(place->entryCount < 1) {
		place->entryCount = 0;
		snprintf(place->note[0], sizeof(place->note[0]),
			"The Memory Card in %s can't be read", slotName(slot));
		return;
	}
	/* entries[0] is the driver's "..". The card's own order, as the IPL. */
	for(i = 1; i < place->entryCount && place->count < SAVES_LIST_MAX; i++) {
		card_dir *dir = (card_dir *)place->entries[i].other;

		/* libogc2's CARD_FindNext leaves a save's permissions out of what
		 * it lists, so no-copy and no-move read as unset; the card's
		 * directory, already read, has them. */
		CARD_GetAttributes(slot, dir->fileno, &dir->permissions);
		place->list[place->count++] = &place->entries[i];
		used += (int)((place->entries[i].size + UI_SAVES_BLOCK_SIZE - 1u) /
			UI_SAVES_BLOCK_SIZE);
	}
	info = device->info(device->initial);
	place->totalBlocks = (int)(info->totalSpace / UI_SAVES_BLOCK_SIZE) -
		SAVES_SYSTEM_BLOCKS;
	place->freeBlocks = place->totalBlocks > used ?
		place->totalBlocks - used : 0;
	place->ready = true;
	if(place->selection >= place->count) {
		place->selection = place->count > 0 ? place->count - 1 : 0;
	}
}

/* The parent, then folders, then saves, each by name. */
static int placeOrder(const void *a, const void *b)
{
	const file_handle *x = *(file_handle *const *)a;
	const file_handle *y = *(file_handle *const *)b;
	int rankX = x->fileType == IS_SPECIAL ? 0 : x->fileType == IS_DIR ? 1 : 2;
	int rankY = y->fileType == IS_SPECIAL ? 0 : y->fileType == IS_DIR ? 1 : 2;

	if(rankX != rankY) {
		return rankX - rankY;
	}
	return strcasecmp(getRelativeName((char *)x->name),
		getRelativeName((char *)y->name));
}

static bool folderIsRoot(const savesPlace_t *place)
{
	return !strcmp(place->dir.name, place->device->initial->name);
}

/* place->dir names the folder, on place->device; lists it. */
static void loadFolder(savesPlace_t *place, bool foldersOnly)
{
	int i;

	placeClear(place);
	if(place->device == NULL) {
		snprintf(place->note[0], sizeof(place->note[0]),
			"No device for settings and saves");
		snprintf(place->note[1], sizeof(place->note[1]),
			"Choose one in Settings, Storage.");
		return;
	}
	place->entryCount = place->device->readDir(&place->dir, &place->entries, -1);
	if(place->entryCount < 0) {
		place->entryCount = 0;
		snprintf(place->note[0], sizeof(place->note[0]),
			"This folder can't be read");
		return;
	}
	for(i = 0; i < place->entryCount && place->count < SAVES_LIST_MAX; i++) {
		file_handle *entry = &place->entries[i];
		const char *leaf = getRelativeName(entry->name);

		if(entry->fileType == IS_SPECIAL) {
			if(!folderIsRoot(place)) {
				place->list[place->count++] = entry;
			}
		}
		/* A dot name is a system file, like macOS's ._ copies of saves. */
		else if(leaf[0] != '.' && (entry->fileType == IS_DIR ||
			(!foldersOnly && entry->fileType == IS_FILE &&
			(isSaveName(leaf) || SavesRaw_IsImageName(leaf))))) {
			place->list[place->count++] = entry;
		}
	}
	qsort(place->list, (size_t)place->count, sizeof(place->list[0]),
		placeOrder);
	place->ready = true;
	if(place->count == 0 || (place->count == 1 &&
		place->list[0]->fileType == IS_SPECIAL)) {
		snprintf(place->note[0], sizeof(place->note[0]), foldersOnly ?
			"No folders in here" : "No saves in this folder");
	}
	if(place->selection >= place->count) {
		place->selection = place->count > 0 ? place->count - 1 : 0;
	}
}

/* Open a card image without changing it. The saved folder remains the
 * navigation parent and export destination for the other SD column. */
static void loadRaw(savesPlace_t *place, const file_handle *source)
{
	file_handle image = *source;
	int returning = place->rawOpen ? place->rawReturn : place->selection;
	int selected = place->rawOpen ? place->selection : 0;
	uiSavesRawStatus_t status;
	int i;

	placeClear(place);
	place->rawImage = image;
	place->rawOpen = true;
	place->rawReturn = returning;
	place->selection = selected;
	place->rawCard = calloc(1, sizeof(*place->rawCard));
	status = place->rawCard != NULL ? SavesRaw_Load(&place->rawImage,
		place->rawCard) : UI_SAVES_RAW_READ_ERROR;
	if(status != UI_SAVES_RAW_OK) {
		snprintf(place->note[0], sizeof(place->note[0]), "%s",
			UISavesRaw_StatusText(status));
		snprintf(place->note[1], sizeof(place->note[1]), UI_T("B  Back to the SD folder"));
		return;
	}
	place->entryCount = place->rawCard->count;
	if(place->entryCount > 0) {
		place->entries = calloc((size_t)place->entryCount, sizeof(*place->entries));
		if(place->entries == NULL) {
			place->entryCount = 0;
			snprintf(place->note[0], sizeof(place->note[0]), UI_T("Not enough memory to list saves"));
			snprintf(place->note[1], sizeof(place->note[1]), UI_T("B  Back to the SD folder"));
			return;
		}
	}
	for(i = 0; i < place->entryCount; i++) {
		file_handle *save = &place->entries[i];

		UISaves_FileName(save->name, sizeof(save->name),
			UISavesRaw_Entry(place->rawCard, (unsigned)i));
		save->fileType = IS_FILE;
		save->device = place->device;
		save->size = UISavesRaw_GciSize(place->rawCard, (unsigned)i);
		place->list[place->count++] = save;
	}
	place->freeBlocks = place->rawCard->freeBlocks;
	place->totalBlocks = place->rawCard->totalBlocks - SAVES_SYSTEM_BLOCKS;
	place->ready = true;
	if(place->selection >= place->count) place->selection = 0;
}

static void loadTab(int tab)
{
	if(tab >= SAVES_TAB_FOLDER) {
		if(places[tab].rawOpen) loadRaw(&places[tab], &places[tab].rawImage);
		else loadFolder(&places[tab], false);
	}
	else {
		loadCard(&places[tab], tab);
	}
}

/* The folder under device's root: "swiss/saves", "/" for the root. */
static void folderSet(savesPlace_t *place, DEVICEHANDLER_INTERFACE *device,
	const char *path)
{
	memset(&place->dir, 0, sizeof(place->dir));
	place->device = device;
	if(place->device != NULL) {
		concat_path(place->dir.name, place->device->initial->name, path);
	}
	place->dir.fileType = IS_DIR;
	place->dir.device = place->device;
	place->selection = 0;
}

/* The SD card's stack tab on device: its Save Folder there, or its root
 * when that can't be read. B goes back up as far as this, no further. */
static void folderOpenHome(int tab, DEVICEHANDLER_INTERFACE *device)
{
	savesPlace_t *place = &places[tab];

	placeClear(place);
	folderSet(place, device, saves_folder());
	loadFolder(place, false);
	if(!place->ready) {
		folderSet(place, device, "/");
		loadFolder(place, false);
	}
	snprintf(folderHome[tab - SAVES_TAB_FOLDER], sizeof(folderHome[0]), "%s",
		place->dir.name);
}

/* The path under the device's root that folderSet takes back. */
static void folderPath(const file_handle *dir, char *out, size_t size)
{
	const char *path = getDevicePath((char *)dir->name);

	snprintf(out, size, "%s", !strcmp(path, "/") || path[0] == '\0' ? "/" :
		path[0] == '/' ? path + 1 : path);
}

/* Each folder along path, made where it's missing (the default Save Folder
 * on a card that hasn't had one yet); makeDir leaves one that's there. */
static void folderEnsure(DEVICEHANDLER_INTERFACE *device, const char *path)
{
	file_handle dir;
	const char *slash;
	size_t root;

	if(device == NULL || device->makeDir == NULL || path[0] == '\0' ||
		!strcmp(path, "/")) {
		return;
	}
	memset(&dir, 0, sizeof(dir));
	dir.fileType = IS_DIR;
	dir.device = device;
	concat_path(dir.name, device->initial->name, path);
	root = strlen(dir.name) - strlen(path);
	for(slash = strchr(path, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
		size_t end = root + (size_t)(slash - path);
		char kept = dir.name[end];

		dir.name[end] = '\0';
		device->makeDir(&dir);
		dir.name[end] = kept;
	}
	device->makeDir(&dir);
}

/* ------------------------------------------------------------------------
 * What a save is called, and its size.
 * --------------------------------------------------------------------- */

/* A save's name on its card, or its file's in a folder: what it's called
 * until its comment is read. */
static void saveTitle(char *out, size_t size, file_handle *save)
{
	unsigned ordinal;
	savesPlace_t *raw = rawSource(save, &ordinal);

	if(raw != NULL) {
		if(!UISaves_DisplayTitle(out, size, UISavesRaw_Entry(raw->rawCard, ordinal), NULL, 0u))
			snprintf(out, size, UI_T("Unnamed save"));
	}
	else if(isCard(save->device)) {
		const card_dir *dir = (const card_dir *)save->other;
		u8 entry[UI_SAVES_ENTRY_SIZE] = {0};
		unsigned blocks = (save->size + UI_SAVES_BLOCK_SIZE - 1u) / UI_SAVES_BLOCK_SIZE;
		memcpy(entry, dir->gamecode, 4u); memcpy(entry + 4u, dir->company, 2u);
		memcpy(entry + 8u, dir->filename, CARD_FILENAMELEN);
		entry[0x38] = (u8)(blocks >> 8); entry[0x39] = (u8)blocks;
		if(!UISaves_DisplayTitle(out, size, entry, NULL, 0u)) snprintf(out, size, UI_T("Unnamed save"));
	}
	else {
		snprintf(out, size, "%s", getRelativeName(save->name));
	}
}

static unsigned saveBlocks(const file_handle *save)
{
	unsigned blocks = save->size / UI_SAVES_BLOCK_SIZE;

	/* A .gci's header adds 64 bytes to its whole blocks. */
	return isCard(save->device) ?
		(save->size + UI_SAVES_BLOCK_SIZE - 1u) / UI_SAVES_BLOCK_SIZE :
		(blocks > 0u ? blocks : 1u);
}

/* ------------------------------------------------------------------------
 * The cube screen: two stacks side by side, each showing a place (Slot A,
 * Slot B or the SD card's open folder), and the focused save below.
 * --------------------------------------------------------------------- */

/* A place's cells: its saves and folders and a free one, in rows; 0 when
 * there is nothing to show (no card, or it can't be read). */
static int placeCells(const savesPlace_t *place)
{
	return place->ready ? UISaveCubes_Cells(place->count - placeSkip(place)) : 0;
}

static int placeCell(const savesPlace_t *place)
{
	int cell = place->selection - placeSkip(place);

	return cell > 0 ? cell : 0;
}

/* The stack the cursor belongs in: its own while it has cubes, else the
 * other, else none (-1). */
static int stackFocus(const int stacks[UI_SAVE_CUBES_STACKS], int focus)
{
	int cells[UI_SAVE_CUBES_STACKS];
	int s;

	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		cells[s] = placeCells(&places[stacks[s]]);
	}
	return UISaveCubes_Home(cells, focus);
}

/* The SD card's stack is in a folder it opened: B goes up, never past the
 * card's root. */
static bool folderBelowHome(int tab)
{
	return tab >= SAVES_TAB_FOLDER && (places[tab].rawOpen ||
		(strcmp(places[tab].dir.name, folderHome[tab - SAVES_TAB_FOLDER]) != 0 &&
		!folderIsRoot(&places[tab])));
}

/* A comment line as it shows: without the spaces that pad it. */
static void saveLine(char *out, size_t size, const char *line)
{
	UISaves_DisplayText(out, size, (const u8*)line, strnlen(line, 33u));
}

/* What a save is called: the first line of its comment, once its art is
 * read, else its name. */
static void saveHeading(char *out, size_t size, int tab, file_handle *save)
{
	int s = artSlot(saveTag(tab, save));

	out[0] = '\0';
	if(s >= 0 && !slots[s].failed) {
		saveLine(out, size, slots[s].line[0]);
	}
	if(out[0] == '\0') {
		saveTitle(out, size, save);
	}
}

/* The info bar: the focused save's banner, comment and blocks, or a
 * folder's name. */
static void screenInfo(uiSaveCubesPageSnapshot_t *g, int tab, file_handle *chosen)
{
	char text[PATHNAME_MAX];
	int s;

	g->info = 1;
	if(chosen->fileType == IS_DIR || SavesRaw_IsImageName(chosen->name)) {
		g->folder = 1;
		snprintf(text, sizeof(text), "%s", getRelativeName(chosen->name));
		snprintf(g->blocks, sizeof(g->blocks), "%s",
			chosen->fileType == IS_DIR ? "Folder" : "Image");
		if(chosen->fileType != IS_DIR) {
			snprintf(g->line[1], sizeof(g->line[1]), UI_T("A  Open virtual memory card"));
		}
	}
	else {
		saveHeading(text, sizeof(text), tab, chosen);
		snprintf(g->blocks, sizeof(g->blocks), "%u", saveBlocks(chosen));
		s = artSlot(saveTag(tab, chosen));
		if(s >= 0) {
			char second[33];

			saveLine(second, sizeof(second), slots[s].line[1]);
			UICheats_Fit(g->line[1], sizeof(g->line[1]), second, 360, 0.5f,
				GetTextSizeInPixels);
			if(pool != NULL && !slots[s].failed &&
				slots[s].art.bannerFormat != UI_SAVES_ART_NONE) {
				g->banner = pool + s * SAVES_SLOT_BYTES +
					UI_SAVES_ICON_FRAMES * UI_SAVES_ICON_BYTES;
			}
		}
		if(places[tab].rawOpen && !SavesRaw_Writable(&places[tab].rawImage)) {
			snprintf(g->line[1], sizeof(g->line[1]), UI_T("Read-only card image. Copy exports a .gci save."));
		}
	}
	UICheats_Fit(g->line[0], sizeof(g->line[0]), text, 410, 0.62f,
		GetTextSizeInPixels);
}

/* The screen as it stands. The saves on screen are listed first, so no cube
 * published names a slot the loader may read into. */
static void screenBuild(const int stacks[UI_SAVE_CUBES_STACKS], int focus)
{
	uiSaveCubesPageSnapshot_t *g = &screen;
	file_handle *chosen;
	int s, k;

	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		screenStacks[s] = stacks[s];
	}
	screenFocus = focus;
	memset(g, 0, sizeof(*g));
	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		savesPlace_t *place = &places[stacks[s]];
		int cells = placeCells(place);

		place->top = cells > 0 ? UISaveCubes_Window(place->top, placeCell(place),
			cells) : 0;
	}
	wantCount = 0;
	if(focus >= 0) {
		artWant(stacks[focus], placeCell(&places[stacks[focus]]));
	}
	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		if(s != focus) {
			artWant(stacks[s], -1);
		}
	}
	g->grid.focusStack = (s8)focus;
	g->grid.focusCell = (s16)(focus >= 0 ? placeCell(&places[stacks[focus]]) : 0);
	if(over.menuOpen && over.storageStack >= 0) {
		g->grid.focusStack = over.storageStack;
		g->grid.focusCell = 0;
	}
	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		savesPlace_t *place = &places[stacks[s]];
		uiSaveCubesStack_t *stack = &g->grid.stack[s];
		uiSaveCubesStackText_t *text = &g->stack[s];

		stack->cells = (s16)placeCells(place);
		stack->first = (s16)place->top;
		stack->listing = place->listing;
		stack->change = place->change;
		stack->changeAt = place->changeAt;
		snprintf(text->control, sizeof(text->control), UI_T("%c  Choose storage"),
			s ? 'R' : 'L');
		if(stack->cells == 0) {
			for(k = 0; k < 2; k++) {
				snprintf(text->note[k], sizeof(text->note[k]), "%s", place->note[k]);
				text->noteScale[k] = GetTextScaleToFitInWidthWithMax(text->note[k],
					264, k ? 0.48f : 0.6f);
			}
		}
		if(stacks[s] < SAVES_TAB_FOLDER) {
			snprintf(text->name, sizeof(text->name), "%c", 'A' + stacks[s]);
			if(place->ready) {
				snprintf(text->free, sizeof(text->free), "%d", place->freeBlocks);
			}
		}
		else {
			/* With two SD devices, a folder shows its device's prefix
			 * ("sdc:/swiss/saves"), as Y's page does. */
			snprintf(text->name, sizeof(text->name), UI_T("SD"));
			UICheats_Fit(text->path, sizeof(text->path),
				place->rawOpen ? getRelativeName(place->rawImage.name) :
				sdDevices > 1 ? place->dir.name : getDevicePath(place->dir.name),
				150, 0.42f, GetTextSizeInPixels);
		}
		for(k = 0; k < UI_SAVE_CUBES_DRAWN; k++) {
			file_handle *entry = placeAt(place, (place->top - 1) *
				UI_SAVE_CUBES_COLUMNS + k);
			uiSaveCubesCell_t *cell = &stack->cell[k];
			int slot;

			cell->kind = entry == NULL ? UI_SAVE_CUBES_KIND_EMPTY :
				(entry->fileType == IS_DIR || SavesRaw_IsImageName(entry->name)) ?
				UI_SAVE_CUBES_KIND_FOLDER :
				UI_SAVE_CUBES_KIND_SAVE;
			if(cell->kind == UI_SAVE_CUBES_KIND_FOLDER && folderIdentity(entry))
				cell->folderColor = config_folder_color(entry->name);
			if(cell->kind == UI_SAVE_CUBES_KIND_SAVE && pool != NULL &&
				(slot = artSlot(saveTag(stacks[s], entry))) >= 0 && !slots[slot].failed) {
				cell->texels = pool + slot * SAVES_SLOT_BYTES;
				cell->art = &slots[slot].art;
			}
		}
	}
	chosen = focus >= 0 ? placeAt(&places[stacks[focus]], g->grid.focusCell) : NULL;
	if(chosen != NULL) {
		screenInfo(g, stacks[focus], chosen);
	}
	g->grid.op = over.op;
	g->grid.ghost = over.ghost;
	g->grid.ghostCell = over.ghostCell;
	g->grid.menu = over.menuOpen;
	g->grid.menuFocus = over.menuFocus;
	g->grid.menuSerial = over.menuSerial;
	g->menu = over.menu;
	g->grid.message = over.message[0] != '\0';
	snprintf(g->message, sizeof(g->message), "%s", over.message);
	g->grid.leaving = over.leaving;
	if(over.op.kind != UI_SAVE_CUBES_OP_NONE) {
		/* The words the GameCube's guidelines ask for while a card works. */
		snprintf(g->hint[0], sizeof(g->hint[0]),
			"Accessing. Do not touch the Memory Card or the POWER Button.");
	}
	else if(over.menuOpen && over.reason[0] != '\0') {
		/* A focused item's reason shows in the info bar in amber; a
		 * dimmed one's below too, as A does nothing on it. */
		UICheats_Fit(g->line[1], sizeof(g->line[1]), over.reason, 360, 0.5f,
			GetTextSizeInPixels);
		g->warn = 1;
		if((over.menu.dim >> over.menuFocus) & 1u) {
			snprintf(g->hint[0], sizeof(g->hint[0]), "%s", over.reason);
			snprintf(g->hint[1], sizeof(g->hint[1]), UI_T("B  Cancel"));
		}
		else {
			snprintf(g->hint[0], sizeof(g->hint[0]), UI_T("B  Cancel   A  Confirm"));
		}
	}
	else if(over.menuOpen) {
		snprintf(g->hint[0], sizeof(g->hint[0]), UI_T("B  Cancel   A  Confirm"));
	}
	else {
			bool folder = folderIdentity(chosen);
			snprintf(g->hint[0], sizeof(g->hint[0]),
				"STICK / D-PAD  Select   B  %s   A  %s",
				focus >= 0 && folderBelowHome(stacks[focus]) ? "Up" : "Finish",
				folder ? "Open" : "Confirm");
			snprintf(g->hint[1], sizeof(g->hint[1]), "%s", folder ?
				"Y  Folder" : "L  Left storage   R  Right storage");
	}
}

static void screenShow(uiDrawObj_t **page)
{
	if(*page == NULL) {
		if((*page = DrawSaveCubesPage(&screen)) != NULL) {
			DrawPublish(*page);
		}
	}
	else {
		DrawUpdateSaveCubesPage(*page, &screen);
	}
	screenPage = *page;
}

static void screenRedraw(void)
{
	screenBuild(screenStacks, screenFocus);
	screenShow(&screenPage);
}

/* What malloc can still hand out: its heap's free chunks, and the arena
 * past the heap that sbrk grows it into. */
static u32 heapFree(void)
{
	struct mallinfo info = mallinfo();

	return (u32)info.fordblks + SYS_GetArena1Size();
}

/* Before a copy of a save bytes long as it is read: when the memory left
 * can't hold it twice and the card driver's room besides, the pool goes,
 * as a copy is worth more than its icons. The screen built without it goes
 * out first, so nothing the video thread draws names it, then it is freed;
 * the cubes are plain until artReturn takes it back. The card device
 * handler doesn't check the write buffer it allocates
 * (deviceHandler-CARD.c), so this headroom is what keeps a large copy
 * beside the pool from failing there. */
static void artRoom(u32 bytes)
{
	u8 *gone = pool;

	if(pool == NULL || !UISaves_CopyCrowded(heapFree(), bytes)) {
		return;
	}
	pool = NULL;
	screenRedraw();
	free(gone);
}

/* After an operation: the pool back if artRoom let it go, every slot read
 * again into it at once, as no slot's texels are in it. */
static void artReturn(void)
{
	if(pool != NULL || (pool = memalign(32, (SAVES_SLOTS + 1) * SAVES_SLOT_BYTES)) == NULL) {
		return;
	}
	memset(slotTags, 0, sizeof(slotTags));
	artFresh = true;
}

/* ------------------------------------------------------------------------
 * Over the screen: the box beside the focused cube, an operation's cube
 * flying while the card works, and messages.
 * --------------------------------------------------------------------- */
static uiMotionMode_t savesMotion(void)
{
	return UIMotion_ModeFromFlags(swissSettings.disableUIAnimations,
		swissSettings.reduceUIAnimations);
}

/* Waits until seconds of video have gone since retrace since, counted at
 * the screen's own rate. */
static void savesWait(u32 since, float seconds)
{
	float rate = VIDEO_GetRetraceRate();

	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	while((float)(VIDEO_GetRetraceCount() - since) < seconds * rate) {
		VIDEO_WaitVSync();
	}
}

/* Y belongs only to the small folder cubes on Memory Cards. The dialog
 * keeps prepared text and previews separate from the published save art. */
static void showFolderIdentity(file_handle *chosen)
{
	uiFolderSnapshot_t *snapshot;
	uiDrawObj_t *page;
	savesInput_t input;
	if(!folderIdentity(chosen)) return;
	snapshot = calloc(1, sizeof(*snapshot));
	if(snapshot == NULL) return;
	if(!UIFolder_PreparePath(snapshot, chosen->name, GetTextSizeInPixels)) {
		free(snapshot);
		return;
	}
	snapshot->color = config_folder_color(chosen->name);
	folderContents(snapshot, chosen);
	for(unsigned i = 0; i < 2u; ++i) {
		char text[sizeof(snapshot->contents[0])];
		memcpy(text, snapshot->contents[i], sizeof(text));
		UICheats_Fit(snapshot->contents[i], sizeof(snapshot->contents[i]), text,
			532, 0.42f, GetTextSizeInPixels);
	}
	page = DrawMemoryCardFolder(snapshot);
	if(page == NULL) { free(snapshot); return; }
	DrawPublish(page);
	inputInit(&input);
	while(1) {
		u32 pressed = inputNext(&input), actions = 0u;
		if(pressed & BUTTON_A) actions |= UI_FOLDER_INPUT_SAVE;
		if(pressed & BUTTON_B) actions |= UI_FOLDER_INPUT_CANCEL;
		if(pressed & BUTTON_Y) actions |= UI_FOLDER_INPUT_RESET;
		if(pressed & BUTTON_LEFT) actions |= UI_FOLDER_INPUT_LEFT;
		if(pressed & BUTTON_RIGHT) actions |= UI_FOLDER_INPUT_RIGHT;
		if(pressed & BUTTON_UP) actions |= UI_FOLDER_INPUT_UP;
		if(pressed & BUTTON_DOWN) actions |= UI_FOLDER_INPUT_DOWN;
		uiFolderAction_t action = UIFolder_Input(snapshot, actions);
		if(action == UI_FOLDER_ACTION_CANCEL) break;
		if(action == UI_FOLDER_ACTION_SAVE) {
			/* Memory Cards holds the settings device: keep it mounted. */
			if(config_set_folder_color(chosen->name, snapshot->color, false)) break;
			strcpy(snapshot->status, "Could not save. Check the Configuration Device or the 32-folder limit.");
		}
		DrawUpdateMemoryCardFolder(page, snapshot);
	}
	DrawDispose(page);
	free(snapshot);
	while(padsButtonsHeld() & SAVES_BUTTONS) VIDEO_WaitVSync();
}

/* The box beside the focused cube, the IPL's, or a question with title
 * above it: Up and Down move, A chooses an item that isn't dimmed, B
 * cancels. An item with a reason (reasons[i]) says it while it is focused:
 * why a dimmed one can't be chosen, or what to know before choosing; the
 * items in ghosts show the ghost cube where a save would land. Returns the
 * index chosen, or -1. */
static int savesMenu(const char *title, const char *const *items, int count,
	int focus, unsigned dim, const char *const *reasons, unsigned ghosts)
{
	savesInput_t input;
	int width, chosen = -1, i;

	if(count <= 0 || count > 4) {
		return -1;
	}
	memset(&over.menu, 0, sizeof(over.menu));
	UICheats_Fit(over.menu.title, sizeof(over.menu.title), title, 236, 0.56f,
		GetTextSizeInPixels);
	width = (int)((float)GetTextSizeInPixels(over.menu.title) * 0.56f) + 24;
	for(i = 0; i < count; i++) {
		int w;

		UICheats_Fit(over.menu.item[i], sizeof(over.menu.item[i]), items[i], 228,
			0.56f, GetTextSizeInPixels);
		w = (int)((float)GetTextSizeInPixels(over.menu.item[i]) * 0.56f) + 32;
		width = w > width ? w : width;
	}
	over.menu.count = (u8)count;
	over.menu.dim = (u8)dim;
	over.menu.width = (u16)width;
	over.menuOpen = 1;
	over.menuSerial++;
	inputInit(&input);
	while(1) {
		u32 pressed;

		over.menuFocus = (u8)focus;
		over.ghost = (u8)((ghosts >> focus) & 1u);
		snprintf(over.reason, sizeof(over.reason), "%s",
			reasons != NULL && reasons[focus] != NULL ? reasons[focus] : "");
		screenRedraw();
		pressed = inputNext(&input);
		if(pressed & BUTTON_UP) {
			focus = focus > 0 ? focus - 1 : count - 1;
			menuaudio_blip();
		}
		else if(pressed & BUTTON_DOWN) {
			focus = focus + 1 < count ? focus + 1 : 0;
			menuaudio_blip();
		}
		else if(pressed & BUTTON_B) {
			break;
		}
		else if((pressed & BUTTON_A) && !((dim >> focus) & 1u)) {
			menuaudio_select();
			chosen = focus;
			break;
		}
	}
	over.menuOpen = over.ghost = 0;
	over.reason[0] = '\0';
	screenRedraw();
	return chosen;
}

/* A message in the IPL's maroon box, that something is done, fitted to the
 * box with an ellipsis: it closes by itself after UI_SAVE_CUBES_MESSAGE
 * seconds, or on A or B. */
static void savesSay(const char *text)
{
	u32 start = VIDEO_GetRetraceCount();
	u32 previous = padsButtonsHeld();
	float rate = VIDEO_GetRetraceRate();
	u32 held, pressed = 0u;

	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	UICheats_Fit(over.message, sizeof(over.message), text,
		UI_SAVE_CUBES_MESSAGE_WIDTH, 0.56f, GetTextSizeInPixels);
	screenRedraw();
	while(UISaveCubes_MessageHolds((float)(VIDEO_GetRetraceCount() - start) / rate,
		pressed != 0u)) {
		VIDEO_WaitVSync();
		held = padsButtonsHeld();
		pressed = held & ~previous & (BUTTON_A | BUTTON_B);
		previous = held;
	}
	over.message[0] = '\0';
	screenRedraw();
}

/* Place tab's cubes as ids, saves' as their art slots know them. */
static void placeRemember(int tab)
{
	savesPlace_t *place = &places[tab];
	int i;

	placeIdCount[tab] = 0;
	for(i = placeSkip(place); i < place->count; i++) {
		placeIds[tab][placeIdCount[tab]++] = saveTag(tab, place->list[i]);
	}
}

static void placesRemember(void)
{
	int tab;

	for(tab = 0; tab < SAVES_TABS; tab++) {
		placeRemember(tab);
	}
}

/* Every place read again after an operation, each knowing how its cubes
 * moved since placesRemember: a save gone, one come, or nothing. */
static void placesReload(void)
{
	static u32 before[SAVES_LIST_MAX];
	int tab, count, at;

	for(tab = 0; tab < SAVES_TABS; tab++) {
		count = placeIdCount[tab];
		memcpy(before, placeIds[tab], (size_t)count * sizeof(before[0]));
		if(tab < SAVES_TAB_FOLDER || foldersReady) {
			loadTab(tab);
		}
		placeRemember(tab);
		at = 0;
		places[tab].change = (u8)(places[tab].ready ? UISaveCubes_Change(before, count,
			placeIds[tab], placeIdCount[tab], &at) : UI_SAVE_CUBES_NEW);
		places[tab].changeAt = (s16)at;
	}
}

/* What an operation starts from: the save's cell and art slot, and the
 * cell of the other stack it would land in. */
static struct {
	int cell;
	int slot;
	int toCell;
} plan;
static u16 opSerial;
static u32 opStarted;

/* Copy or Move: the save's cube leaves for the other stack, or the header
 * of a folder it doesn't show, while the card is read and written. */
static void opBegin(bool move, const char *folder)
{
	memset(&flight, 0, sizeof(flight));
	flight.failed = true;
	if(pool != NULL && plan.slot >= 0 && !slots[plan.slot].failed) {
		/* The slot in flight: no published cube names it until this. */
		flight = slots[plan.slot];
		memcpy(SAVES_FLIGHT, pool + plan.slot * SAVES_SLOT_BYTES, SAVES_SLOT_BYTES);
		DCFlushRange(SAVES_FLIGHT, SAVES_SLOT_BYTES);
	}
	memset(&over.op, 0, sizeof(over.op));
	over.op.kind = move ? UI_SAVE_CUBES_OP_MOVE : UI_SAVE_CUBES_OP_COPY;
	over.op.phase = UI_SAVE_CUBES_GO;
	over.op.serial = ++opSerial;
	over.op.from = (s8)screenFocus;
	over.op.fromCell = (s16)plan.cell;
	over.op.toCell = (s16)(folder != NULL &&
		strcmp(folder, places[screenStacks[!screenFocus]].dir.name) ? -1 : plan.toCell);
	over.op.cube.kind = UI_SAVE_CUBES_KIND_SAVE;
	if(!flight.failed) {
		over.op.cube.texels = SAVES_FLIGHT;
		over.op.cube.art = &flight.art;
	}
	opStarted = VIDEO_GetRetraceCount();
	screenRedraw();
}

/* Erase: the save's cube shrinks while the card deletes it. */
static void eraseBegin(int cell)
{
	memset(&over.op, 0, sizeof(over.op));
	over.op.kind = UI_SAVE_CUBES_OP_ERASE;
	over.op.phase = UI_SAVE_CUBES_GO;
	over.op.serial = ++opSerial;
	over.op.from = (s8)screenFocus;
	over.op.fromCell = (s16)cell;
	opStarted = VIDEO_GetRetraceCount();
	screenRedraw();
}

/* The card has answered, ok: written and read back the same (and a Move's
 * original removed), or erased. Called only then, so a cube lands only on
 * a save that is there. Every place is read again, the cube lands where its
 * save now is, bursts, or goes back, and only when that is done does the
 * screen go on. */
static void opEnd(bool ok)
{
	uiMotionMode_t mode = savesMotion();
	int other = over.op.from >= 0 ? screenStacks[!over.op.from] : -1;
	u32 tag = 0u;
	int s;

	savesWait(opStarted, UISaveCubes_OpSeconds(over.op.kind, UI_SAVE_CUBES_GO, mode));
	placesReload();
	/* Where it came: the one id the other stack didn't have. */
	if(ok && over.op.kind != UI_SAVE_CUBES_OP_ERASE && other >= 0 &&
		places[other].change == UI_SAVE_CUBES_OPENED) {
		over.op.toCell = places[other].changeAt;
		tag = placeIds[other][places[other].changeAt];
	}
	over.op.phase = ok ? UI_SAVE_CUBES_LAND : UI_SAVE_CUBES_BACK;
	screenRedraw();
	/* The save that came shows the art its cube carried, in a slot no cube
	 * on the screen just published names. */
	if(tag != 0u && !flight.failed && artSlot(tag) < 0 &&
		(s = UISaves_SlotPick(slotTags, SAVES_SLOTS, wantTags, wantCount)) >= 0) {
		memcpy(pool + s * SAVES_SLOT_BYTES, SAVES_FLIGHT, SAVES_SLOT_BYTES);
		slots[s] = flight;
		DCFlushRange(pool + s * SAVES_SLOT_BYTES, SAVES_SLOT_BYTES);
		slotTags[s] = tag;
		screenRedraw();
	}
	savesWait(VIDEO_GetRetraceCount(), UISaveCubes_OpSeconds(over.op.kind,
		over.op.phase, mode));
	memset(&over.op, 0, sizeof(over.op));
	screenRedraw();
}

/* ------------------------------------------------------------------------
 * Reading, writing and checking saves.
 * --------------------------------------------------------------------- */

/* The whole save as the .gci layout: a card's entry and blocks, or a file's
 * bytes as they are. The caller frees it. */
static u8 *saveRead(file_handle *save, u32 *length)
{
	bool card = isCard(save->device);
	u32 want = card ? save->size + UI_SAVES_ENTRY_SIZE : save->size;
	u8 *data;
	s32 got;

	if(want <= UI_SAVES_ENTRY_SIZE || want > SAVES_MAX_BYTES ||
		(data = memalign(32, want)) == NULL) {
		return NULL;
	}
	got = readSaveAt(save, 0u, data, want) ? (s32)want : -1;
	if(got != (s32)want) {
		free(data);
		return NULL;
	}
	*length = want;
	return data;
}

/* The save on the card in slot whose game, maker and name are entry's.
 * Reads the card's list into *entries, which the caller frees. */
static file_handle *cardFind(int slot, const u8 *entry, file_handle **entries,
	int *count, int *usedBlocks)
{
	DEVICEHANDLER_INTERFACE *device = slotDevice(slot);
	file_handle *found = NULL;
	int i;

	*entries = NULL;
	*usedBlocks = 0;
	*count = device->readDir(device->initial, entries, -1);
	for(i = 1; i < *count; i++) {
		card_dir *dir = (card_dir *)(*entries)[i].other;

		*usedBlocks += (int)(((*entries)[i].size + UI_SAVES_BLOCK_SIZE - 1u) /
			UI_SAVES_BLOCK_SIZE);
		if(!memcmp(dir->gamecode, entry, 4) &&
			!memcmp(dir->company, entry + 4, 2) &&
			!strncmp(dir->filename, (const char *)entry + 8, CARD_FILENAMELEN)) {
			found = &(*entries)[i];
		}
	}
	return found;
}

static const char *cardWhy(s32 error)
{
	switch(error) {
		case CARD_ERROR_INSSPACE: return "not enough free blocks";
		case CARD_ERROR_NOENT: return "no room for another save";
		case CARD_ERROR_EXIST: return "it already has this save";
		case CARD_ERROR_NOCARD: return "the card was taken out";
		default: return "the card didn't take it";
	}
}

/* Writes a save to the card in slot and reads it back. The driver creates
 * it from entry (game, maker, name, banner and icon places) and writes
 * blocks, the save's data. When boundMount is non-NULL, the write is bound
 * to that mount generation. */
static bool cardWriteBound(int slot, const u8 *entry, const u8 *blocks,
	u32 blockBytes, const u32 *boundMount, char *why, size_t whySize)
{
	DEVICEHANDLER_INTERFACE *device = slotDevice(slot);
	file_handle dest;
	file_handle *entries = NULL;
	file_handle *copy;
	int count, used, total;
	u8 *back;
	u32 backLength = 0;
	u32 mount;
	s32 written;
	bool has, same;

	/* A leading 0xff is a libogc2 lookup wildcard, not an exact save id.
	 * Check here as well as in the SD parser: card-to-card copies bypass it. */
	if(entry[0] == 0xff || entry[4] == 0xff) {
		snprintf(why, whySize, UI_T("This save's game or maker code can't be written."));
		return false;
	}
	memset(&dest, 0, sizeof(dest));
	if(device->init(device->initial) != 0) {
		snprintf(why, whySize, UI_T("%s has no memory card."), slotName(slot));
		return false;
	}
	places[slot].mounted = true;
	/* The clean-up below deletes only on the card this mounted: one taken
	 * out or changed since (a memory card emulator switching) is mounted
	 * again, and its save of the same name isn't this write's. */
	mount = card_mount_count(slot);
	if(boundMount != NULL && mount != *boundMount) {
		snprintf(why, whySize, UI_T("The card in %s changed, so nothing was written."),
			slotName(slot));
		return false;
	}
	/* The driver would write over a save of the same name in place, whatever
	 * its size, so one already there stops the copy. */
	has = cardFind(slot, entry, &entries, &count, &used) != NULL;
	free(entries);
	total = (int)(device->info(device->initial)->totalSpace /
		UI_SAVES_BLOCK_SIZE) - SAVES_SYSTEM_BLOCKS;
	if(count < 1) {
		snprintf(why, whySize, UI_T("The memory card in %s can't be read."),
			slotName(slot));
		return false;
	}
	if(has) {
		snprintf(why, whySize, UI_T("%s already has this save."), slotName(slot));
		return false;
	}
	if(count - 1 >= CARD_MAXFILES) {
		snprintf(why, whySize, UI_T("%s has no room for another save."),
			slotName(slot));
		return false;
	}
	if((int)UISaves_Blocks(entry) > total - used) {
		snprintf(why, whySize, total - used == 1 ?
			UI_T("%s has %d free block; this needs %u.") :
			UI_T("%s has %d free blocks; this needs %u."),
			slotName(slot), total > used ? total - used : 0,
			UISaves_Blocks(entry));
		return false;
	}
	if((boundMount != NULL && card_mount_count(slot) != *boundMount) ||
		card_mount_count(slot) != mount) {
		snprintf(why, whySize, UI_T("The card in %s changed, so nothing was written."),
			slotName(slot));
		return false;
	}
	concatf_path(dest.name, device->initial->name, "%.*s", CARD_FILENAMELEN,
		(const char *)entry + 8);
	dest.device = device;
	setGCIInfo(entry);
	written = device->writeFile(&dest, blocks, blockBytes);
	setGCIInfo(NULL);
	if(written != (s32)blockBytes) {
		/* The save wasn't there before, so what the driver left is ours. */
		if((copy = cardFind(slot, entry, &entries, &count, &used)) != NULL &&
			card_mount_count(slot) == mount) {
			device->deleteFile(copy);
		}
		free(entries);
		snprintf(why, whySize, "%s: %s.", slotName(slot),
			card_mount_count(slot) != mount ? "the card changed meanwhile" :
			cardWhy(written < 0 ? written : CARD_ERROR_FATAL_ERROR));
		return false;
	}
	/* Read it back from the card before calling it copied. */
	copy = cardFind(slot, entry, &entries, &count, &used);
	back = copy != NULL ? saveRead(copy, &backLength) : NULL;
	same = back != NULL && backLength == blockBytes + UI_SAVES_ENTRY_SIZE &&
		!memcmp(back, entry, 6) &&
		!strncmp((const char *)back + 8, (const char *)entry + 8, CARD_FILENAMELEN) &&
		!memcmp(back + UI_SAVES_ENTRY_SIZE, blocks, blockBytes);
	free(back);
	/* Read back from another card: neither proof nor this write's to delete. */
	if(card_mount_count(slot) != mount) {
		free(entries);
		snprintf(why, whySize, UI_T("The card in %s changed while the save was "
			"written."), slotName(slot));
		return false;
	}
	if(!same) {
		if(copy != NULL) {
			device->deleteFile(copy);
		}
		snprintf(why, whySize, UI_T("The copy on %s didn't read back the same."),
			slotName(slot));
	}
	free(entries);
	return same;
}

static bool cardWrite(int slot, const u8 *entry, const u8 *blocks,
	u32 blockBytes, char *why, size_t whySize)
{
	return cardWriteBound(slot, entry, blocks, blockBytes, NULL, why, whySize);
}

/* Writes data to a new file named name in folder on device, as name_2 and
 * so on while a name is taken, and reads it back. */
static bool folderWrite(DEVICEHANDLER_INTERFACE *device, const char *folder,
	const char *name, const u8 *data, u32 length, char *why, size_t whySize)
{
	file_handle dest;
	char leaf[PATHNAME_MAX];
	u8 *back;
	int attempt;
	bool same;

	for(attempt = 1; attempt < 100; attempt++) {
		memset(&dest, 0, sizeof(dest));
		dest.device = device;
		UISaves_NumberedName(leaf, sizeof(leaf), name, attempt);
		concat_path(dest.name, folder, leaf);
		if(device->statFile(&dest) != 0) {
			break;
		}
	}
	if(attempt == 100) {
		snprintf(why, whySize, UI_T("That folder has 99 copies of this save."));
		return false;
	}
	memset(&dest, 0, sizeof(dest));
	dest.device = device;
	concat_path(dest.name, folder, leaf);
	if(device->writeFile(&dest, data, length) != (s32)length ||
		device->closeFile(&dest) != 0) {
		device->closeFile(&dest);
		device->deleteFile(&dest);
		snprintf(why, whySize, UI_T("The SD card didn't take the save."));
		return false;
	}
	back = memalign(32, length);
	dest.offset = 0;
	same = back != NULL && device->readFile(&dest, back, length) == (s32)length &&
		!memcmp(back, data, length);
	device->closeFile(&dest);
	free(back);
	if(!same) {
		device->deleteFile(&dest);
		snprintf(why, whySize, UI_T("The copy didn't read back the same."));
		return false;
	}
	return true;
}

static bool saveDelete(file_handle *save)
{
	unsigned ordinal;
	savesPlace_t *raw = rawSource(save, &ordinal);

	/* A card image's save goes from its image, as listed; an image itself
	 * is never deleted here. */
	if(raw != NULL) {
		return SavesRaw_Erase(&raw->rawImage,
			UISavesRaw_Entry(raw->rawCard, ordinal)) == UI_SAVES_RAW_OK;
	}
	if(SavesRaw_IsImageName(save->name)) return false;
	save->device->closeFile(save);
	return save->device->deleteFile(save) == 0;
}

/* ------------------------------------------------------------------------
 * Game Details: start a game with the copy of its save chosen there.
 * --------------------------------------------------------------------- */

/* Puts a game's chosen save on the card in slot in place of the card's own
 * copy of it (own: that copy's entry and blocks, as saveRead reads it), or
 * adds it when the card has none. The card's own copy goes back if the new
 * one doesn't go on or doesn't read back the same. */
static bool cardReplace(int slot, const u8 *entry, const u8 *blocks,
	u32 blockBytes, const u8 *own, u32 ownLength, char *why, size_t whySize)
{
	DEVICEHANDLER_INTERFACE *device = slotDevice(slot);
	file_handle *entries = NULL;
	file_handle *found;
	char failed[128];
	int count, used;
	u8 *now;
	u32 nowLength = 0;
	u32 mount;
	bool readAgain, same, gone;

	if(device->init(device->initial) != 0) {
		snprintf(why, whySize, UI_T("%s has no memory card."), slotName(slot));
		return false;
	}
	places[slot].mounted = true;
	mount = card_mount_count(slot);

	if(own != NULL) {
		/* Only the copy just kept in the Save Folder goes: a card swapped
		 * since then keeps its save. */
		found = cardFind(slot, entry, &entries, &count, &used);
		now = found != NULL ? saveRead(found, &nowLength) : NULL;
		readAgain = found == NULL || now != NULL;
		/* The same save: game, maker and name, and every byte of it. */
		same = now != NULL && nowLength == ownLength && !memcmp(now, own, 6) &&
			!strncmp((const char *)now + 8, (const char *)own + 8, CARD_FILENAMELEN) &&
			!memcmp(now + UI_SAVES_ENTRY_SIZE, own + UI_SAVES_ENTRY_SIZE,
				ownLength - UI_SAVES_ENTRY_SIZE);
		free(now);
		gone = same && card_mount_count(slot) == mount && device->deleteFile(found) == 0;
		free(entries);
		if(!same || card_mount_count(slot) != mount) {
			snprintf(why, whySize, readAgain ?
				"The card in %s changed, so nothing was replaced." :
				"%s's own copy couldn't be read again.", slotName(slot));
			return false;
		}
		if(!gone) {
			snprintf(why, whySize, UI_T("%s's own copy couldn't be replaced."),
				slotName(slot));
			return false;
		}
	}
	if(cardWriteBound(slot, entry, blocks, blockBytes, &mount, failed, sizeof(failed))) {
		return true;
	}
	if(own == NULL) {
		snprintf(why, whySize, "%s", failed);
	}
	else if(card_mount_count(slot) == mount && cardWriteBound(slot, own,
		own + UI_SAVES_ENTRY_SIZE, ownLength - UI_SAVES_ENTRY_SIZE, &mount,
		why, whySize)) {
		snprintf(why, whySize, UI_T("%s\n%s's own copy went back."), failed,
			slotName(slot));
	}
	else {
		snprintf(why, whySize, UI_T("%s\n%s's own copy is in the Save Folder."),
			failed, slotName(slot));
	}
	return false;
}

/* A copy the details scan found, read whole from where the scan found it
 * (a card, the settings device or the Source): its entry and blocks, with
 * *at where the blocks begin. NULL unless it is still the save the scan saw:
 * the same game, maker and name. */
static u8 *copyRead(const savesCopy_t *copy, u32 *length, size_t *at,
	u8 entry[UI_SAVES_ENTRY_SIZE])
{
	file_handle file;
	file_handle *entries = NULL;
	file_handle *found;
	uiSavesRawCard_t *raw;
	u8 *data = NULL;
	int count, used;

	*at = 0u;
	*length = 0u;
	if(copy->source == SAVES_COPY_SLOT_A || copy->source == SAVES_COPY_SLOT_B) {
		int slot = copy->source == SAVES_COPY_SLOT_B;
		DEVICEHANDLER_INTERFACE *device = slotDevice(slot);

		if(device->init(device->initial) == 0) {
			found = cardFind(slot, copy->entry, &entries, &count, &used);
			/* The card driver reads it as a .gci: its entry, then its blocks. */
			data = found != NULL ? saveRead(found, length) : NULL;
			free(entries);
			*at = UI_SAVES_ENTRY_SIZE;
		}
	}
	else if(copy->device != NULL) {
		memset(&file, 0, sizeof(file));
		snprintf(file.name, sizeof(file.name), "%s", copy->path);
		file.device = copy->device;
		file.fileType = IS_FILE;
		file.size = copy->size;
		if(copy->source == SAVES_COPY_FILE) {
			data = saveRead(&file, length);
			if(data != NULL) {
				*at = UISaves_FindEntry(data, *length, entry);
			}
		}
		else if((raw = calloc(1, sizeof(*raw))) != NULL) {
			if(SavesRaw_Load(&file, raw) == UI_SAVES_RAW_OK &&
				copy->ordinal < raw->count) {
				*length = UISavesRaw_GciSize(raw, copy->ordinal);
				data = *length > UI_SAVES_ENTRY_SIZE && *length <= SAVES_MAX_BYTES ?
					memalign(32, *length) : NULL;
				if(data != NULL && !SavesRaw_ReadGci(&file, raw, copy->ordinal, 0u,
					data, *length)) {
					free(data);
					data = NULL;
				}
				*at = UI_SAVES_ENTRY_SIZE;
			}
			free(raw);
			file.device->closeFile(&file);
		}
	}
	if(data != NULL && copy->source != SAVES_COPY_FILE) {
		memcpy(entry, data, UI_SAVES_ENTRY_SIZE);
	}
	if(data == NULL || *at == 0u || *at >= *length ||
		memcmp(entry, copy->entry, 6u) != 0 ||
		strncmp((const char *)entry + 8, (const char *)copy->entry + 8,
			CARD_FILENAMELEN) != 0) {
		free(data);
		return NULL;
	}
	return data;
}

bool Saves_CopyLine(const savesCopy_t *copy, char *out, size_t size)
{
	file_handle file;
	file_handle *entries = NULL;
	file_handle *found;
	uiSavesRawCard_t *raw;
	uiSavesArt_t art;
	u8 head[UI_SAVES_HEAD_SIZE] ATTRIBUTE_ALIGN(32);
	u8 entry[UI_SAVES_ENTRY_SIZE];
	u8 *data = NULL;
	u32 want;
	size_t body;
	int count, used;
	bool read = false;

	if(out == NULL || size == 0u) return false;
	out[0] = '\0';
	if(copy == NULL) return false;
	if(copy->source == SAVES_COPY_SLOT_A || copy->source == SAVES_COPY_SLOT_B) {
		int slot = copy->source == SAVES_COPY_SLOT_B;
		DEVICEHANDLER_INTERFACE *device = slotDevice(slot);

		/* The card driver reads a save as a .gci from its start: its entry,
		 * for where the comment is, then the save up to the comment's end. */
		if(device->init(device->initial) == 0 &&
			(found = cardFind(slot, copy->entry, &entries, &count, &used)) != NULL &&
			readSaveAt(found, 0u, head, UI_SAVES_ENTRY_SIZE) &&
			UISaves_SameFile(head, copy->entry) &&
			UISaves_ArtLayout(head, found->size, &art) && art.comment) {
			want = UI_SAVES_ENTRY_SIZE + art.commentAt + UI_SAVES_ENTRY_SIZE;
			data = memalign(32, want);
			read = data != NULL && readSaveAt(found, 0u, data, want) &&
				UISaves_DisplayText(out, size, data + want - 32u, 32u);
		}
		free(data);
		free(entries);
	}
	else if(copy->device != NULL) {
		memset(&file, 0, sizeof(file));
		snprintf(file.name, sizeof(file.name), "%s", copy->path);
		file.device = copy->device;
		file.fileType = IS_FILE;
		file.size = copy->size;
		if(copy->source == SAVES_COPY_FILE) {
			want = file.size < UI_SAVES_HEAD_SIZE ? file.size : UI_SAVES_HEAD_SIZE;
			body = readSaveAt(&file, 0u, head, want) ?
				UISaves_FindEntryPrefix(head, want, file.size, entry) : 0u;
			if(body != 0u && UISaves_SameFile(entry, copy->entry) &&
				UISaves_ArtLayout(entry, file.size - body, &art) && art.comment &&
				readSaveAt(&file, (u32)body + art.commentAt + 32u, head, 32u)) {
				read = UISaves_DisplayText(out, size, head, 32u);
			}
		}
		else if((raw = calloc(1, sizeof(*raw))) != NULL) {
			if(SavesRaw_Load(&file, raw) == UI_SAVES_RAW_OK && copy->ordinal < raw->count &&
				UISaves_SameFile(UISavesRaw_Entry(raw, copy->ordinal), copy->entry) &&
				UISaves_ArtLayout(UISavesRaw_Entry(raw, copy->ordinal),
					UISavesRaw_GciSize(raw, copy->ordinal) - UI_SAVES_ENTRY_SIZE, &art) &&
				art.comment && SavesRaw_ReadGci(&file, raw, copy->ordinal,
					UI_SAVES_ENTRY_SIZE + art.commentAt + 32u, head, 32u)) {
				read = UISaves_DisplayText(out, size, head, 32u);
			}
			free(raw);
		}
	}
	return read;
}

bool Saves_LoadCopy(int slot, const savesCopy_t *copy, char *why,
	size_t whySize)
{
	DEVICEHANDLER_INTERFACE *device;
	DEVICEHANDLER_INTERFACE *config;
	file_handle *entries = NULL;
	file_handle *found;
	char folder[PATHNAME_MAX] = "";
	char name[PATHNAME_MAX] = "";
	u8 entry[UI_SAVES_ENTRY_SIZE];
	u8 *data;
	u8 *own = NULL;
	u32 length = 0, ownLength = 0;
	size_t at = 0;
	int count, used, i;
	bool ok = false;

	if(copy == NULL || slot < 0 || slot > 1) {
		snprintf(why, whySize, UI_T("There's no save to put on a card."));
		return false;
	}
	/* The card's own copy is kept on the SD card before anything changes. */
	if(!config_set_device()) {
		snprintf(why, whySize, UI_T("There's no SD card to keep the card's own copy on."));
		return false;
	}
	config = devices[DEVICE_CONFIG];
	device = slotDevice(slot);
	data = copyRead(copy, &length, &at, entry);
	if(data == NULL) {
		snprintf(why, whySize, UI_T("The chosen save couldn't be read."));
	}
	else if(device->init(device->initial) != 0) {
		snprintf(why, whySize, UI_T("%s has no memory card."), slotName(slot));
	}
	else {
		found = cardFind(slot, entry, &entries, &count, &used);
		own = found != NULL ? saveRead(found, &ownLength) : NULL;
		free(entries);
		if(found != NULL && own == NULL) {
			snprintf(why, whySize, UI_T("%s's own copy couldn't be read."),
				slotName(slot));
		}
		else if(own != NULL && ownLength - UI_SAVES_ENTRY_SIZE == length - at &&
			!memcmp(own + UI_SAVES_ENTRY_SIZE, data + at, length - at)) {
			ok = true;	/* the card has this save already */
		}
		else {
			if(own != NULL) {
				concat_path(folder, config->initial->name, saves_folder());
				folderEnsure(config, saves_folder());
				UISaves_FileName(name, sizeof(name), own);
			}
			if(own == NULL ||
				folderWrite(config, folder, name, own, ownLength, why, whySize)) {
				ok = cardReplace(slot, entry, data + at, length - (u32)at, own,
					ownLength, why, whySize);
			}
		}
	}
	free(data);
	free(own);
	/* Unmount the cards this read, unless the game or settings device is one. */
	for(i = 0; i < 2; i++) {
		DEVICEHANDLER_INTERFACE *card = slotDevice(i);

		if(card != devices[DEVICE_CUR] && card != config) {
			card->deinit(card->initial);
		}
		places[i].mounted = false;
	}
	config_unset_device();
	return ok;
}

/* The folder a Copy or Move goes to, as a path on the other stack's device. */
static bool destinationFolder(uiSavesPlace_t to, char *path, size_t size);

/* Copy or Move save to a card, to a folder on the other stack's device, or
 * to the card image the other stack has open. */
static void saveTransfer(file_handle *save, uiSavesPlace_t to, bool move)
{
	char why[128] = "";
	char done[PATHNAME_MAX + 64];
	char folder[PATHNAME_MAX];
	char name[PATHNAME_MAX];
	u8 entry[UI_SAVES_ENTRY_SIZE];
	u8 *data;
	u32 length = 0;
	size_t blocksAt;
	unsigned ordinal = 0u;
	savesPlace_t *raw = rawSource(save, &ordinal);
	bool toFolder = to == UI_SAVES_PLACE_FOLDER || to == UI_SAVES_PLACE_CHOOSE;
	u8 permissions = isCard(save->device) ? ((card_dir *)save->other)->permissions :
		raw != NULL ? UISavesRaw_Entry(raw->rawCard, ordinal)[0x34] : 0u;
	bool ok;

	/* A card image's save carries the card's own no-copy and no-move. */
	if(move && (isCard(save->device) || raw != NULL) &&
		(permissions & (CARD_ATTRIB_NOCOPY | CARD_ATTRIB_NOMOVE))) {
		savesTell(D_WARN, "This game doesn't let its save move.\n"
			"Copy it instead.\nPress A to continue.");
		return;
	}
	if(toFolder && !destinationFolder(to, folder, sizeof(folder))) {
		return;
	}
	artRoom(isCard(save->device) ? save->size + UI_SAVES_ENTRY_SIZE : save->size);
	opBegin(move, toFolder ? folder : NULL);
	data = saveRead(save, &length);
	blocksAt = 0u;
	if(data != NULL && isCard(save->device)) {
		/* The card driver read it as a .gci: its entry, then its blocks. */
		memcpy(entry, data, UI_SAVES_ENTRY_SIZE);
		blocksAt = UI_SAVES_ENTRY_SIZE;
	}
	else if(data != NULL) {
		blocksAt = UISaves_FindEntry(data, length, entry);
	}
	if(data == NULL) {
		snprintf(why, sizeof(why), UI_T("The save couldn't be read."));
		ok = false;
	}
	else if(blocksAt == 0u) {
		snprintf(why, sizeof(why), UI_T("This file isn't a GameCube save."));
		ok = false;
	}
	else if(to <= UI_SAVES_PLACE_SLOT_B) {
		ok = cardWrite(to == UI_SAVES_PLACE_SLOT_B, entry, data + blocksAt,
			length - (u32)blocksAt, why, sizeof(why));
		snprintf(done, sizeof(done), UI_T("Finished %s."), move ? UI_T("moving") : UI_T("copying"));
	}
	else if(to == UI_SAVES_PLACE_IMAGE) {
		/* The image takes a .gci: the entry as found (a wrapper's header,
		 * if any, sits before it), then the blocks. */
		uiSavesRawStatus_t status;

		memcpy(data + blocksAt - UI_SAVES_ENTRY_SIZE, entry, UI_SAVES_ENTRY_SIZE);
		status = SavesRaw_Add(&places[screenStacks[!screenFocus]].rawImage,
			data + blocksAt - UI_SAVES_ENTRY_SIZE,
			length - (u32)blocksAt + UI_SAVES_ENTRY_SIZE);
		ok = status == UI_SAVES_RAW_OK;
		if(!ok) {
			snprintf(why, sizeof(why), "%s.", UISavesRaw_StatusText(status));
		}
		snprintf(done, sizeof(done), UI_T("Finished %s."), move ? UI_T("moving") : UI_T("copying"));
	}
	else {
		DEVICEHANDLER_INTERFACE *device = places[screenStacks[!screenFocus]].device;

		/* A card's save becomes a .gci; a file keeps its name and form. */
		if(isCard(save->device) || rawSource(save, NULL) != NULL) {
			UISaves_FileName(name, sizeof(name), entry);
		}
		else {
			snprintf(name, sizeof(name), "%s", getRelativeName(save->name));
		}
		folderEnsure(device, getDevicePath(folder) + 1);
		ok = folderWrite(device, folder, name, data, length, why, sizeof(why));
		snprintf(done, sizeof(done), UI_T("Finished %s to %s."), move ? UI_T("moving") : UI_T("copying"),
			getDevicePath(folder));
	}
	free(data);
	if(ok && move && !saveDelete(save)) {
		opEnd(true);
		savesTell(D_WARN, "Copied, but the original couldn't be removed.\n"
			"Press A to continue.");
		return;
	}
	opEnd(ok);
	if(ok) {
		savesSay(done);
	}
	else {
		snprintf(done, sizeof(done), UI_T("%s\nNothing was %s.\nPress A to continue."),
			why, UI_T(move ? "moved" : "changed"));
		savesTell(D_FAIL, done);
	}
}

/* ------------------------------------------------------------------------
 * The folder chooser, for Another folder and for Settings > Storage > Save
 * Folder: a list page of a device's folders, the other stack's or the
 * settings device's.
 * --------------------------------------------------------------------- */

/* The folder chooser lists folders, and first the way back up. */
static void pageRow(uiSavesPageRow_t *row, savesPlace_t *place,
	file_handle *entry)
{
	char text[PATHNAME_MAX];

	if(entry->fileType == IS_SPECIAL) {
		char parent[PATHNAME_MAX];

		getParentPath(place->dir.name, parent);
		snprintf(text, sizeof(text), UI_T("Up to %s"), getDevicePath(parent));
	}
	else {
		snprintf(text, sizeof(text), "%s", getRelativeName(entry->name));
		snprintf(row->blocks, sizeof(row->blocks), UI_T("Folder"));
	}
	UICheats_Fit(row->title, sizeof(row->title), text, 330, 0.62f,
		GetTextSizeInPixels);
}

/* The folder chooser's page: the folder open in place, its folders a row
 * each. */
static void pageBuild(savesPlace_t *place)
{
	uiSavesPageSnapshot_t *s = &shown;
	int first = UICheats_WindowStart(place->selection, place->count);
	file_handle *focus = place->count > 0 ? place->list[place->selection] : NULL;
	int i;

	memset(s, 0, sizeof(*s));
	snprintf(s->title, sizeof(s->title), UI_T("Choose a Folder"));
	if(place->device != NULL) {
		snprintf(s->status, sizeof(s->status), "%s",
			DeviceDisplayName(place->device));
		UICheats_Fit(s->section, sizeof(s->section),
			getDevicePath(place->dir.name), 420, 0.42f, GetTextSizeInPixels);
	}
	if(place->count > 0) {
		snprintf(s->position, sizeof(s->position), "%d / %d",
			place->selection + 1, place->count);
	}
	for(i = 0; i < UI_SAVES_PAGE_ROWS && first + i < place->count; i++) {
		pageRow(&s->rows[i], place, place->list[first + i]);
		s->rowCount++;
	}
	s->first = first;
	s->count = place->count;
	s->list = place->listing;
	s->focusRow = (s8)(place->selection - first);
	if(s->rowCount == 0 || (place->count == 1 &&
		place->list[0]->fileType == IS_SPECIAL)) {
		snprintf(s->empty[0], sizeof(s->empty[0]), "%s", place->note[0]);
		snprintf(s->empty[1], sizeof(s->empty[1]), "%s", place->note[1]);
	}
	snprintf(s->hint[0], sizeof(s->hint[0]), UI_T("%sX  Choose this folder"),
		focus != NULL ? "A  Open   " : "");
	snprintf(s->hint[1], sizeof(s->hint[1]), UI_T("B  Cancel"));
}

static void pageShow(uiDrawObj_t **page)
{
	if(*page == NULL) {
		if((*page = DrawSavesPage(&shown)) != NULL) {
			DrawPublish(*page);
		}
	}
	else {
		DrawUpdateSavesPage(*page, &shown);
	}
}

/* Browses device's folders from start; X chooses the one open. The device
 * must be mounted. */
static bool chooseFolder(DEVICEHANDLER_INTERFACE *device, const char *start,
	char *path, size_t size)
{
	uiDrawObj_t *page = NULL;
	savesInput_t input;
	bool chosen = false;

	folderSet(&chooser, device, start);
	loadFolder(&chooser, true);
	if(!chooser.ready) {
		/* The folder is gone: start at the root. */
		folderSet(&chooser, device, "/");
		loadFolder(&chooser, true);
	}
	inputInit(&input);
	while(1) {
		u32 pressed;
		file_handle *focus;

		pageBuild(&chooser);
		pageShow(&page);
		pressed = inputNext(&input);
		focus = chooser.count > 0 ? chooser.list[chooser.selection] : NULL;
		if(pressed & BUTTON_B) {
			break;
		}
		if((pressed & BUTTON_X) && chooser.ready) {
			folderPath(&chooser.dir, path, size);
			chosen = true;
			break;
		}
		if((pressed & BUTTON_A) && focus != NULL) {
			if(focus->fileType == IS_SPECIAL) {
				getParentPath(chooser.dir.name, chooser.dir.name);
			}
			else {
				snprintf(chooser.dir.name, sizeof(chooser.dir.name), "%s",
					focus->name);
			}
			chooser.selection = 0;
			loadFolder(&chooser, true);
		}
		else if(pressed & (BUTTON_UP | BUTTON_LEFT)) {
			chooser.selection = UICheats_Move(chooser.selection, chooser.count,
				-1, (pressed & BUTTON_LEFT) != 0u);
		}
		else if(pressed & (BUTTON_DOWN | BUTTON_RIGHT)) {
			chooser.selection = UICheats_Move(chooser.selection, chooser.count,
				1, (pressed & BUTTON_RIGHT) != 0u);
		}
	}
	placeClear(&chooser);
	DrawDispose(page);
	return chosen;
}

/* The folder the SD card's stack has open, or one chosen from there. */
static bool destinationFolder(uiSavesPlace_t to, char *path, size_t size)
{
	char open[PATHNAME_MAX];
	char chosen[PATHNAME_MAX];
	savesPlace_t *dest = &places[screenStacks[!screenFocus]];

	if(dest->rawOpen || dest->device == NULL || !dest->ready ||
		!(dest->device->features & FEAT_WRITE)) return false;

	if(to == UI_SAVES_PLACE_FOLDER) {
		snprintf(path, size, "%s", dest->dir.name);
		return true;
	}
	folderPath(&dest->dir, open, sizeof(open));
	if(!chooseFolder(dest->device, open, chosen, sizeof(chosen))) {
		return false;
	}
	concat_path(path, dest->device->initial->name, chosen);
	return true;
}

bool saves_choose_folder(char *folder, size_t size)
{
	bool chosen;

	if(!config_set_device()) {
		savesTell(D_FAIL, "There's no device for settings and saves.\n"
			"Press A to continue.");
		return false;
	}
	chosen = chooseFolder(devices[DEVICE_CONFIG], saves_folder(), folder, size);
	config_unset_device();
	return chosen;
}

/* A save's entry as a .gci has it: a card's from its directory, a file's
 * from its first bytes. False for a file that isn't a GameCube save. */
static bool saveEntry(file_handle *save, u8 entry[UI_SAVES_ENTRY_SIZE])
{
	static u8 head[UI_SAVES_HEAD_SIZE] ATTRIBUTE_ALIGN(32);
	u32 want = save->size < UI_SAVES_HEAD_SIZE ? save->size : UI_SAVES_HEAD_SIZE;
	s32 got;
	unsigned ordinal;
	savesPlace_t *raw = rawSource(save, &ordinal);

	memset(entry, 0, UI_SAVES_ENTRY_SIZE);
	if(raw != NULL) {
		memcpy(entry, UISavesRaw_Entry(raw->rawCard, ordinal), UI_SAVES_ENTRY_SIZE);
		return true;
	}
	if(isCard(save->device)) {
		const card_dir *dir = (const card_dir *)save->other;
		unsigned blocks = saveBlocks(save);

		memcpy(entry, dir->gamecode, 4);
		memcpy(entry + 4, dir->company, 2);
		memcpy(entry + 8, dir->filename, CARD_FILENAMELEN);
		entry[0x34] = dir->permissions;
		entry[0x38] = (u8)(blocks >> 8);
		entry[0x39] = (u8)blocks;
		return true;
	}
	got = readSaveAt(save, 0u, head, want) ? (s32)want : -1;
	return got == (s32)want && UISaves_FindEntryPrefix(head, want, save->size,
		entry) != 0u;
}

/* Where a save from the place on screen would go: the other stack's. */
static void saveRoom(int tab, const u8 entry[UI_SAVES_ENTRY_SIZE], bool known,
	uiSavesRoom_t *room)
{
	savesPlace_t *place = &places[tab];
	int i;

	memset(room, 0, sizeof(*room));
	room->ready = place->ready;
	if(tab >= SAVES_TAB_FOLDER && place->rawOpen) {
		room->name = "The card image";
		room->card = true;
		room->image = true;
		room->writable = SavesRaw_Writable(&place->rawImage);
		room->saves = place->count;
		room->freeBlocks = place->freeBlocks;
		room->hasIt = known && place->ready && place->rawCard != NULL &&
			UISavesRaw_Find(place->rawCard, entry) < place->rawCard->count;
		return;
	}
	if(tab >= SAVES_TAB_FOLDER) {
		room->name = "The SD card";
		room->writable = place->device != NULL &&
			(place->device->features & FEAT_WRITE);
		return;
	}
	room->name = slotName(tab);
	room->card = true;
	room->saves = place->count;
	room->freeBlocks = place->freeBlocks;
	for(i = 0; known && place->ready && i < place->count; i++) {
		const card_dir *dir = (const card_dir *)place->list[i]->other;

		if(!memcmp(dir->gamecode, entry, 4) && !memcmp(dir->company, entry + 4, 2) &&
			!strncmp(dir->filename, (const char *)entry + 8, CARD_FILENAMELEN)) {
			room->hasIt = true;
		}
	}
}

static void saveEntry32(u8 *bytes, u32 value)
{
	bytes[0] = (u8)(value >> 24);
	bytes[1] = (u8)(value >> 16);
	bytes[2] = (u8)(value >> 8);
	bytes[3] = (u8)value;
}

/* A physical listing has identity and size, but no date or icon metadata.
 * Read status without changing the save, and don't display another entry's
 * metadata if the card changed since it was listed. */
static bool saveUpdated(file_handle *save,
	u8 entry[UI_SAVES_ENTRY_SIZE], bool known, u32 *seconds)
{
	*seconds = 0u;
	if(!known) return false;
	if(isCard(save->device)) {
		const card_dir *dir = (const card_dir *)save->other;
		card_stat status;
		int slot = save->device == &__device_card_b ? 1 : 0;

		if(dir->chn != slot || dir->filelen != save->size) return false;
		if(CARD_GetStatus(slot, dir->fileno, &status) != CARD_ERROR_READY ||
			memcmp(status.gamecode, dir->gamecode, 4) ||
			memcmp(status.company, dir->company, 2) ||
			strncmp(status.filename, dir->filename, CARD_FILENAMELEN) ||
			status.len != save->size) return false;
		*seconds = status.time;
		entry[0x07] = status.banner_fmt;
		saveEntry32(entry + 0x28, status.time);
		saveEntry32(entry + 0x2C, status.icon_addr);
		entry[0x30] = (u8)(status.icon_fmt >> 8);
		entry[0x31] = (u8)status.icon_fmt;
		entry[0x32] = (u8)(status.icon_speed >> 8);
		entry[0x33] = (u8)status.icon_speed;
		saveEntry32(entry + 0x3C, status.comment_addr);
	}
	else {
		*seconds = UISaves_UpdatedSeconds(entry);
	}
	return true;
}

/* Details are available even when the other stack cannot take a copy.
 * The save format has one update date, never a separate creation date. */
static bool saveDetails(int tab, file_handle *save,
	u8 entry[UI_SAVES_ENTRY_SIZE], bool known, unsigned blocks)
{
	uiSaveDetailsSnapshot_t details;
	savesInput_t input;
	uiDrawObj_t *box;
	char heading[64], date[24];
	const char *updated, *icon;
	const char *source = places[tab].rawOpen ?
		(SavesRaw_Writable(&places[tab].rawImage) ? "Card image" : "Read-only card image") :
		(isCard(save->device) ? slotName(tab) : "SD save");
	u32 seconds;
	bool actions = false, readable = saveUpdated(save, entry, known, &seconds);
	int slot = artSlot(saveTag(tab, save));
	size_t i;

	saveHeading(heading, sizeof(heading), tab, save);
	/* Card filenames and comments are untrusted; a control byte must not
	 * prevent a details panel from opening or change its authored layout. */
	for(i = 0; heading[i] != '\0'; i++) {
		if((u8)heading[i] < 0x20u || (u8)heading[i] == 0x7Fu) heading[i] = ' ';
	}
	if(heading[0] == '\0') snprintf(heading, sizeof(heading), UI_T("Unnamed save"));
	updated = !readable ? "Unable to read metadata" :
		(UISaves_FormatUpdated(seconds, date, sizeof(date)) ? date : "Unknown");
	icon = UISaves_IconDescription(readable ? entry : NULL,
		(size_t)blocks * UI_SAVES_BLOCK_SIZE, swissSettings.disableUIAnimations);
	if(readable && strcmp(icon, "None stored") &&
		(pool == NULL || (slot >= 0 && slots[slot].failed))) {
		icon = "Preview unavailable";
	}
	if(!UISaveDetails_Build(&details, heading, blocks, !known, source, updated, icon)) {
		savesTell(D_FAIL, "Save details couldn't be opened.\nPress A to continue.");
		return false;
	}
	box = DrawSaveDetails(&details);
	if(box == NULL) {
		savesTell(D_FAIL, "Save details couldn't be opened.\nPress A to continue.");
		return false;
	}
	box = DrawPublish(box);
	inputInit(&input);
	while(1) {
		u32 pressed = inputNext(&input);

		if(pressed & BUTTON_B) break;
		if(pressed & BUTTON_A) {
			menuaudio_select();
			actions = true;
			break;
		}
	}
	DrawDispose(box);
	return actions;
}

/* A on a save opens details first, then the IPL's Move / Copy / Erase
 * beside it when A selects Actions. An item that can't
 * be used dimmed with its reason, then a question. Copy and Move go to the
 * other stack: a card, or the SD card's open folder (from a list when it
 * holds folders, with another one to choose). An operation reads every
 * place again itself. */
static void saveOptions(int tab)
{
	static const char *const actions[3] = {"Move", "Copy", "Erase"};
	static const char *const answers[2] = {"Yes", "No"};
	savesPlace_t *place = &places[tab];
	/* The cube the cursor is on: a folder's selection still counts the
	 * ".." no cube shows. */
	file_handle *save = placeAt(place, placeCell(place));
	int toTab = screenStacks[!screenFocus];
	savesPlace_t *dest = &places[toTab];
	char reason[3][96], title[64], open[PATHNAME_MAX];
	const char *why[3] = {reason[0], reason[1], reason[2]};
	/* A folder listed to SAVES_LIST_MAX may leave the save out of its
	 * listing: the copy goes in all the same, perhaps unseen. */
	const char *unseen[2] = {"This folder lists 256 already; the save may not show", NULL};
	const char *where[2] = {open, "Another folder\205"};
	u8 entry[UI_SAVES_ENTRY_SIZE];
	bool known = saveEntry(save, entry), card = isCard(save->device), move, ok;
	unsigned blocks = known ? UISaves_Blocks(entry) : saveBlocks(save), dim = 0u;
	uiSavesRoom_t room;
	uiSavesPlace_t to;
	unsigned ghosts;
	int action, i;

	if(!saveDetails(tab, save, entry, known, blocks)) return;
	memset(reason, 0, sizeof(reason));
	saveRoom(toTab, entry, known, &room);
	for(i = 0; i < 2; i++) {
		if(UISaves_Verdict(i == 0, card || place->rawOpen, entry[0x34], blocks,
			&room, reason[i], sizeof(reason[i])) != UI_SAVES_VERDICT_OK) {
			dim |= 1u << i;
		}
	}
	if(place->rawOpen && !SavesRaw_Writable(&place->rawImage)) {
		dim |= 5u;
		snprintf(reason[0], sizeof(reason[0]), UI_T("This card image is read-only here. Use Copy to export a save."));
		snprintf(reason[2], sizeof(reason[2]), UI_T("This card image is read-only here. Saves cannot be erased."));
	}
	action = savesMenu("", actions, 3, place->rawOpen ? 1 : 0, dim, why, 0u);
	if(action < 0) {
		return;
	}
	placesRemember();
	if(action == 2) {
		/* No first: an erased save is gone. */
		if(savesMenu("Erase this save?", answers, 2, 1, 0u, NULL, 0u) != 0) {
			return;
		}
		eraseBegin(placeCell(place));
		ok = saveDelete(save);
		opEnd(ok);
		if(ok) {
			savesSay("The data was erased.");
		}
		/* The card driver says why itself, in its own box. */
		else if(!card) {
			savesTell(D_FAIL, "The save couldn't be deleted.\n"
				"Press A to continue.");
		}
		return;
	}
	move = action == 0;
	/* Where it would land: the other stack's first free cell, its window
	 * moved to show it, the ghost there while the question is up. A folder
	 * listed to SAVES_LIST_MAX (its ".." counted) may not list the save,
	 * wherever its directory puts it: no ghost, its window on its last
	 * cell, and the question says why. */
	plan.cell = placeCell(place);
	plan.slot = artSlot(saveTag(tab, save));
	plan.toCell = dest->count < SAVES_LIST_MAX ? dest->count - placeSkip(dest) : -1;
	over.ghostCell = (s16)plan.toCell;
	dest->selection = plan.toCell >= 0 ? plan.toCell + placeSkip(dest) : dest->count - 1;
	ghosts = plan.toCell >= 0 ? 3u : 0u;
	if(toTab < SAVES_TAB_FOLDER) {
		snprintf(title, sizeof(title), UI_T("%s to %s?"), move ? UI_T("Move") : UI_T("Copy"),
			slotName(toTab));
		if(savesMenu(title, answers, 2, 0, 0u, NULL, ghosts) != 0) {
			return;
		}
		to = toTab == 0 ? UI_SAVES_PLACE_SLOT_A : UI_SAVES_PLACE_SLOT_B;
	}
	else if(dest->rawOpen) {
		snprintf(title, sizeof(title), UI_T("%s to the card image?"), move ? UI_T("Move") : UI_T("Copy"));
		if(savesMenu(title, answers, 2, 0, 0u, NULL, ghosts) != 0) {
			return;
		}
		to = UI_SAVES_PLACE_IMAGE;
	}
	else {
		bool folders = false;

		for(i = placeSkip(dest); i < dest->count; i++) {
			folders = folders || dest->list[i]->fileType == IS_DIR;
		}
		if(folders) {
			UICheats_Fit(open, sizeof(open), getDevicePath(dest->dir.name), 228, 0.56f,
				GetTextSizeInPixels);
			i = savesMenu(move ? "Move to" : "Copy to", where, 2, 0, 0u,
				ghosts ? NULL : unseen, ghosts & 1u);
			if(i < 0) {
				return;
			}
			to = i == 0 ? UI_SAVES_PLACE_FOLDER : UI_SAVES_PLACE_CHOOSE;
		}
		else {
			snprintf(title, sizeof(title), UI_T("%s to the SD card?"), move ? UI_T("Move") : UI_T("Copy"));
			if(savesMenu(title, answers, 2, 0, 0u, ghosts ? NULL : unseen, ghosts) != 0) {
				return;
			}
			to = UI_SAVES_PLACE_FOLDER;
		}
	}
	saveTransfer(save, to, move);
}

/* The button above each column opens named choices. SD has its own folder
 * position in each column, so an image on one side can export to the other.
 * With two SD devices, each is a choice by its name; one the column isn't
 * on opens at its Save Folder. */
static void chooseStorage(int stacks[UI_SAVE_CUBES_STACKS], int stack)
{
	const char *names[4] = {"Slot A", "Slot B", "SD card", NULL};
	const char *reasons[4] = {NULL, NULL, NULL, NULL};
	unsigned dim = 0u;
	int other = stacks[!stack], count = 3, on = 0, choice, tab, i;

	if(sdDevices > 1) {
		for(i = 0; i < sdDevices; i++) {
			names[2 + i] = DeviceDisplayName(sdDevice[i]);
			if(places[SAVES_TAB_FOLDER + stack].device == sdDevice[i]) on = i;
		}
		count = 2 + sdDevices;
	}
	if(other < SAVES_TAB_FOLDER) {
		dim |= 1u << other;
		reasons[other] = "This slot is displayed in the other column.";
	}
	if(!foldersReady) {
		dim |= 4u;
		reasons[2] = "Choose a device for settings and saves in Settings, Storage.";
	}
	over.storageStack = (s8)stack;
	choice = savesMenu(stack ? "Right storage" : "Left storage", names, count,
		stacks[stack] < SAVES_TAB_FOLDER ? stacks[stack] : 2 + on, dim, reasons, 0u);
	over.storageStack = -1;
	tab = UISaves_StorageTab(stack, choice, other, count - 2);
	if(tab >= SAVES_TAB_FOLDER && sdDevice[choice - 2] != places[tab].device) {
		stacks[stack] = tab;
		folderOpenHome(tab, sdDevice[choice - 2]);
	}
	else if(tab >= 0) {
		stacks[stack] = tab;
		loadTab(tab);
	}
}

void show_saves(void)
{
	uiDrawObj_t *page = NULL;
	savesInput_t input;
	bool folderMounted = config_set_device();
	/* Present cards keep the IPL's slot order; an absent slot opens on SD. */
	int stacks[UI_SAVE_CUBES_STACKS] = {0, 1};
	int focus, s, i;

	memset(places, 0, sizeof(places));
	memset(&over, 0, sizeof(over));
	over.storageStack = -1;
	memset(slotTags, 0, sizeof(slotTags));
	wantCount = 0;
	foldersReady = folderMounted;
	pool = memalign(32, (SAVES_SLOTS + 1) * SAVES_SLOT_BYTES);
	scratch = memalign(32, SAVES_SCRATCH_BYTES);
	sdDevices = 0;
	if(folderMounted) {
		DEVICEHANDLER_INTERFACE *source = Saves_SourceDevice();

		sdDevice[sdDevices++] = devices[DEVICE_CONFIG];
		if(source != NULL) {
			sdDevice[sdDevices++] = source;
		}
		folderEnsure(devices[DEVICE_CONFIG], saves_folder());
	}
	/* The screen goes up at once and opens while the cards are read, each
	 * stack's cubes coming in as its place is listed. Both SD stacks start
	 * on the settings device. */
	screenBuild(stacks, -1);
	screenShow(&page);
	for(i = 0; i < SAVES_TABS; i++) {
		if(i < SAVES_TAB_FOLDER) {
			loadTab(i);
		}
		else if(folderMounted) {
			folderOpenHome(i, sdDevice[0]);
		}
		else {
			places[i].device = NULL;
			folderHome[i - SAVES_TAB_FOLDER][0] = '\0';
			snprintf(places[i].note[0], sizeof(places[i].note[0]),
				"No device for settings and saves");
			snprintf(places[i].note[1], sizeof(places[i].note[1]),
				"Choose one in Settings, Storage.");
		}
		screenBuild(stacks, stackFocus(stacks, 0));
		screenShow(&page);
	}
	UISaves_InitialStorage(places[0].ready, places[1].ready,
		places[SAVES_TAB_FOLDER].ready, stacks);
	focus = stackFocus(stacks, 0);
	inputInit(&input);
	while(1) {
		u32 pressed;

		screenBuild(stacks, focus);
		screenShow(&page);
		pressed = inputNext(&input);
		if(pressed & BUTTON_B) {
			savesPlace_t *place;

			if(focus < 0 || !folderBelowHome(stacks[focus])) break;
			place = &places[stacks[focus]];
			if(place->rawOpen) {
				place->selection = place->rawReturn;
			}
			else {
				getParentPath(place->dir.name, place->dir.name);
				place->selection = 0;
			}
			loadFolder(place, false);
			focus = stackFocus(stacks, focus);
		}
		else if(pressed & (BUTTON_L | BUTTON_R)) {
			int stack = (pressed & BUTTON_L) ? 0 : 1;

			chooseStorage(stacks, stack);
			focus = stackFocus(stacks, focus);
			inputInit(&input);
		}
		else if((pressed & BUTTON_Y) && !(pressed & BUTTON_A) && focus >= 0) {
			file_handle *chosen = placeAt(&places[stacks[focus]],
				placeCell(&places[stacks[focus]]));
			if(folderIdentity(chosen)) showFolderIdentity(chosen);
			inputInit(&input);
		}
		else if((pressed & BUTTON_A) && focus >= 0) {
			savesPlace_t *place = &places[stacks[focus]];
			file_handle *chosen = placeAt(place, placeCell(place));

			if(chosen == NULL) {
				continue;
			}
			menuaudio_select();
			if(SavesRaw_IsImageName(chosen->name)) {
				loadRaw(place, chosen);
				if(!place->ready) {
					savesSay(place->note[0]);
					place->selection = place->rawReturn;
					loadFolder(place, false);
				}
			}
			else if(chosen->fileType == IS_FILE) {
				saveOptions(stacks[focus]);
				artReturn();
			}
			else {
				snprintf(place->dir.name, sizeof(place->dir.name), "%s",
					chosen->name);
				place->selection = 0;
				loadFolder(place, false);
			}
			focus = stackFocus(stacks, focus);
			inputInit(&input);
		}
		else if((pressed & SAVES_DIRECTIONS) && focus >= 0) {
			uiSaveCubesCursor_t cursor;

			for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
				cursor.cells[s] = placeCells(&places[stacks[s]]);
				cursor.first[s] = places[stacks[s]].top;
			}
			cursor.stack = focus;
			cursor.cell = placeCell(&places[stacks[focus]]);
			if(UISaveCubes_Step(&cursor, (pressed & BUTTON_UP) ? UI_SAVE_CUBES_UP :
				(pressed & BUTTON_DOWN) ? UI_SAVE_CUBES_DOWN :
				(pressed & BUTTON_LEFT) ? UI_SAVE_CUBES_LEFT : UI_SAVE_CUBES_RIGHT)) {
				for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
					places[stacks[s]].top = cursor.first[s];
				}
				focus = cursor.stack;
				places[stacks[focus]].selection = cursor.cell +
					placeSkip(&places[stacks[focus]]);
				menuaudio_blip();
			}
		}
	}
	/* The screen goes, the Home cube coming back, before the cards do. */
	over.leaving = 1;
	screenRedraw();
	savesWait(VIDEO_GetRetraceCount(), UISaveCubes_LeaveSeconds(savesMotion()));
	for(i = 0; i < SAVES_TABS; i++) {
		placeClear(&places[i]);
		/* Unmount what this screen mounted, unless it's the source. */
		if(i < SAVES_TAB_FOLDER && places[i].mounted &&
			devices[DEVICE_CUR] != slotDevice(i)) {
			slotDevice(i)->deinit(slotDevice(i)->initial);
		}
	}
	if(folderMounted) {
		config_unset_device();
	}
	DrawDispose(page);
	/* Only now: nothing the video thread draws reads the slots. */
	free(pool);
	free(scratch);
	pool = scratch = NULL;
	while(padsButtonsHeld() & BUTTON_B) {
		VIDEO_WaitVSync();
	}
}
