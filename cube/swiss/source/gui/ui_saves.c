#include "ui_saves.h"
#include "ui_lang.h"

#include <stdio.h>
#include <string.h>

#define DATEL_MAGIC "DATELGC_SAVE"
#define DATEL_ENTRY 0x80u
#define GCS_MAGIC "GCSAVE"
#define GCS_ENTRY 0x110u

void UISaves_InitialStorage(bool slotA, bool slotB, bool sd, int stacks[2])
{
	if(stacks == NULL) return;
	stacks[0] = !slotA && sd ? 2 : 0;
	stacks[1] = !slotB && sd ? 3 : 1;
}

int UISaves_StorageTab(int stack, int choice, int other, int sdDevices)
{
	if(stack < 0 || stack > 1 || sdDevices < 1 || sdDevices > 2 || choice < 0 ||
		choice >= 2 + sdDevices || (choice < 2 && choice == other)) return -1;
	return choice >= 2 ? 2 + stack : choice;
}

unsigned UISaves_Blocks(const uint8_t entry[UI_SAVES_ENTRY_SIZE])
{
	return ((unsigned)entry[0x38] << 8) | (unsigned)entry[0x39];
}

static void swapPairs(uint8_t *bytes, size_t length)
{
	size_t i;

	for(i = 0u; i + 1u < length; i += 2u) {
		uint8_t first = bytes[i];
		bytes[i] = bytes[i + 1u];
		bytes[i + 1u] = first;
	}
}

size_t UISaves_FindEntry(const uint8_t *file, size_t length,
	uint8_t entry[UI_SAVES_ENTRY_SIZE])
{
	return UISaves_FindEntryPrefix(file, length, length, entry);
}

size_t UISaves_FindEntryPrefix(const uint8_t *head, size_t headLength,
	size_t fileLength, uint8_t entry[UI_SAVES_ENTRY_SIZE])
{
	size_t at = 0u;
	unsigned blocks;

	if(head == NULL || entry == NULL || headLength > fileLength) {
		return 0u;
	}
	if(headLength >= sizeof(DATEL_MAGIC) - 1u &&
		!memcmp(head, DATEL_MAGIC, sizeof(DATEL_MAGIC) - 1u)) {
		at = DATEL_ENTRY;
	}
	else if(headLength >= sizeof(GCS_MAGIC) - 1u &&
		!memcmp(head, GCS_MAGIC, sizeof(GCS_MAGIC) - 1u)) {
		at = GCS_ENTRY;
	}
	if(headLength < at + UI_SAVES_ENTRY_SIZE) {
		return 0u;
	}
	memcpy(entry, head + at, UI_SAVES_ENTRY_SIZE);
	if(at == DATEL_ENTRY) {
		/* Action Replay keeps two stretches byte-swapped, as the file
		 * browser's Copy reads them: bytes 6-7 and the 20 from the icon
		 * address on. */
		swapPairs(entry + 0x06, 2u);
		swapPairs(entry + 0x2C, 20u);
	}
	/* libogc2 uses these leading bytes as lookup wildcards. They cannot
	 * identify one save when a wrapper is copied to a memory card. */
	if(entry[0] == 0xffu || entry[4] == 0xffu) {
		return 0u;
	}
	blocks = UISaves_Blocks(entry);
	if(blocks == 0u || fileLength - at - UI_SAVES_ENTRY_SIZE !=
		(size_t)blocks * UI_SAVES_BLOCK_SIZE) {
		return 0u;
	}
	return at + UI_SAVES_ENTRY_SIZE;
}

/* Printable ASCII a FAT name can hold; anything else is '_'. */
static char fatSafe(uint8_t c)
{
	if(c < 0x20u || c > 0x7Eu || strchr("\\/:*?\"<>|", c) != NULL) {
		return '_';
	}
	return (char)c;
}

void UISaves_FileName(char *out, size_t capacity,
	const uint8_t entry[UI_SAVES_ENTRY_SIZE])
{
	char name[UI_SAVES_NAME_LENGTH + 1u];
	size_t length = 0u;
	size_t i;

	if(out == NULL || capacity == 0u) {
		return;
	}
	for(i = 0u; i < UI_SAVES_NAME_LENGTH && entry[8u + i] != 0u; i++) {
		name[length++] = fatSafe(entry[8u + i]);
	}
	/* FAT drops a name's trailing spaces and dots; keep the name written. */
	while(length > 0u && (name[length - 1u] == ' ' ||
		name[length - 1u] == '.')) {
		length--;
	}
	name[length] = '\0';
	(void)snprintf(out, capacity, "%c%c-%c%c%c%c-%s.gci",
		fatSafe(entry[4]), fatSafe(entry[5]), fatSafe(entry[0]),
		fatSafe(entry[1]), fatSafe(entry[2]), fatSafe(entry[3]),
		length > 0u ? name : "save");
}

void UISaves_NumberedName(char *out, size_t capacity, const char *name,
	int attempt)
{
	const char *dot;

	if(out == NULL || capacity == 0u) {
		return;
	}
	if(name == NULL) {
		name = "";
	}
	dot = strrchr(name, '.');
	if(attempt <= 1) {
		(void)snprintf(out, capacity, "%s", name);
	}
	else if(dot == NULL || dot == name) {
		(void)snprintf(out, capacity, "%s_%d", name, attempt);
	}
	else {
		(void)snprintf(out, capacity, "%.*s_%d%s", (int)(dot - name), name,
			attempt, dot);
	}
}

#define VERDICT_NOCOPY 0x08u	/* CARD_ATTRIB_NOCOPY */
#define VERDICT_NOMOVE 0x10u	/* CARD_ATTRIB_NOMOVE */

uiSavesVerdict_t UISaves_Verdict(bool move, bool fromCard, uint8_t permissions,
	unsigned blocks, const uiSavesRoom_t *to, char *why, size_t size)
{
	uiSavesVerdict_t verdict = UI_SAVES_VERDICT_OK;
	const char *name = to != NULL && to->name != NULL ? UI_T(to->name) : "";
	char text[192] = "";

	if(move && fromCard && (permissions & (VERDICT_NOCOPY | VERDICT_NOMOVE))) {
		verdict = UI_SAVES_VERDICT_NO_MOVE;
		(void)snprintf(text, sizeof(text), UI_T("This game doesn't let its save move"));
	}
	else if(to == NULL || (to->card && !to->image && !to->ready)) {
		verdict = UI_SAVES_VERDICT_NO_CARD;
		(void)snprintf(text, sizeof(text), UI_T("No memory card in %s"), name);
	}
	else if((!to->card || to->image) && (!to->ready || !to->writable)) {
		verdict = UI_SAVES_VERDICT_READ_ONLY;
		(void)snprintf(text, sizeof(text), UI_T("%s can't be written"), name);
	}
	else if(!to->card) {
		/* A folder numbers a name that's taken, and has room enough. */
	}
	else if(to->hasIt) {
		verdict = UI_SAVES_VERDICT_HAS_IT;
		(void)snprintf(text, sizeof(text), UI_T("%s already has this save"), name);
	}
	else if(to->saves >= (int)UI_SAVES_CARD_FILES) {
		verdict = UI_SAVES_VERDICT_FULL;
		(void)snprintf(text, sizeof(text), UI_T("%s has %u saves"), name,
			UI_SAVES_CARD_FILES);
	}
	else if((int)blocks > to->freeBlocks) {
		verdict = UI_SAVES_VERDICT_ROOM;
		(void)snprintf(text, sizeof(text), to->freeBlocks == 1 ?
			UI_T("%s has %d free block; this needs %u") :
			UI_T("%s has %d free blocks; this needs %u"),
			name, to->freeBlocks > 0 ? to->freeBlocks : 0, blocks);
	}
	if(why != NULL && size > 0u) {
		(void)snprintf(why, size, "%s", text);
	}
	return verdict;
}

bool UISaves_CopyCrowded(uint32_t freeBytes, uint32_t bytes)
{
	return (uint64_t)freeBytes < 2u * (uint64_t)bytes + UI_SAVES_COPY_MARGIN;
}

/* ------------------------------------------------------------------------
 * Art.
 * --------------------------------------------------------------------- */
#define ART_ICON_PIXELS 1024u	/* 32 x 32 */
#define ART_BANNER_PIXELS 3072u	/* 96 x 32 */
#define ART_PALETTE_BYTES 512u	/* 256 RGB5A3 colours */

static uint32_t be32(const uint8_t *bytes)
{
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
		((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

/* The bytes a picture of pixels takes in form format. */
static uint64_t artBytes(unsigned format, uint64_t pixels)
{
	return format == UI_SAVES_ART_RGB5A3 ? pixels * 2u :
		format == UI_SAVES_ART_CI8_OWN ? pixels + ART_PALETTE_BYTES :
		format == UI_SAVES_ART_CI8_SHARED ? pixels : 0u;
}

/* What frame shows: itself, or when it has no pixels the next frame that
 * has them, or nothing (Dolphin's reading of the GameCube menu). */
static uint8_t artShown(const uiSavesArt_t *art, unsigned frame)
{
	for(; frame < art->frames; frame++) {
		if(art->frameFormat[frame] != UI_SAVES_ART_NONE) {
			return (uint8_t)frame;
		}
	}
	return UI_SAVES_ART_BLANK;
}

static void artAddStep(uiSavesArt_t *art, unsigned frame, unsigned speeds)
{
	uint8_t hold = (uint8_t)((speeds >> (2u * frame)) & 3u);

	art->stepFrame[art->steps] = artShown(art, frame);
	art->stepHold[art->steps] = hold;
	art->period = (uint8_t)(art->period + hold);
	art->steps++;
}

bool UISaves_ArtLayout(const uint8_t entry[UI_SAVES_ENTRY_SIZE],
	size_t dataLength, uiSavesArt_t *art)
{
	uint64_t frameAt[UI_SAVES_ICON_FRAMES];
	uint64_t limit, at, size;
	uint32_t commentAt;
	unsigned formats, speeds, frames = 0u, banner, i;
	bool shared = false;

	if(art == NULL) {
		return false;
	}
	memset(art, 0, sizeof(*art));
	if(entry == NULL) {
		return false;
	}
	limit = dataLength < UI_SAVES_ART_MAX_END ? dataLength : UI_SAVES_ART_MAX_END;
	formats = ((unsigned)entry[0x30] << 8) | entry[0x31];
	speeds = ((unsigned)entry[0x32] << 8) | entry[0x33];
	/* The banner, then the icon's frames, from the icon address. An address
	 * of 0xFFFFFFFF, no banner and no icon, lies past any save's data. */
	at = be32(entry + 0x2C);
	/* 1 is CI8 with its palette after it, 2 is RGB5A3; else none. */
	banner = entry[0x07] & 3u;
	if(banner == 1u || banner == 2u) {
		banner = banner == 1u ? UI_SAVES_ART_CI8_OWN : UI_SAVES_ART_RGB5A3;
		size = artBytes(banner, ART_BANNER_PIXELS);
		if(at + size <= limit) {
			art->bannerFormat = (uint8_t)banner;
			art->bannerAt = (uint32_t)at;
			art->end = (uint32_t)(at + size);
		}
		at += size;
	}
	if((formats & 3u) != 0u) {
		for(; frames < UI_SAVES_ICON_FRAMES &&
			((speeds >> (2u * frames)) & 3u) != 0u; frames++) {
			unsigned format = (formats >> (2u * frames)) & 3u;

			frameAt[frames] = at;
			at += artBytes(format, ART_ICON_PIXELS);
			shared = shared || format == UI_SAVES_ART_CI8_SHARED;
		}
	}
	if(frames > 0u && at + (shared ? ART_PALETTE_BYTES : 0u) <= limit) {
		art->frames = (uint8_t)frames;
		for(i = 0u; i < frames; i++) {
			art->frameAt[i] = (uint32_t)frameAt[i];
			art->frameFormat[i] = (uint8_t)((formats >> (2u * i)) & 3u);
		}
		art->paletteAt = (uint32_t)at;
		art->end = (uint32_t)(at + (shared ? ART_PALETTE_BYTES : 0u));
		for(i = 0u; i < frames; i++) {
			artAddStep(art, i, speeds);
		}
		/* A bounce plays the frames between the last and the first back
		 * again: 0 1 2 3 2 1. */
		if((entry[0x07] & 4u) != 0u && frames >= 3u) {
			for(i = frames - 2u; i > 0u; i--) {
				artAddStep(art, i, speeds);
			}
		}
	}
	commentAt = be32(entry + 0x3C);
	if((uint64_t)commentAt + UI_SAVES_ENTRY_SIZE <= limit) {
		art->comment = true;
		art->commentAt = commentAt;
		if(commentAt + UI_SAVES_ENTRY_SIZE > art->end) {
			art->end = commentAt + UI_SAVES_ENTRY_SIZE;
		}
	}
	return art->end > 0u;
}

bool UISaves_ToRgb5a3(const uint8_t *data, size_t length,
	const uiSavesArt_t *art, int frame, uint8_t *out)
{
	const uint8_t *palette;
	unsigned width, x, y;
	uint32_t at;
	uint8_t format;

	if(data == NULL || art == NULL || out == NULL) {
		return false;
	}
	if(frame == UI_SAVES_ART_BANNER) {
		width = 96u;
		format = art->bannerFormat;
		at = art->bannerAt;
	}
	else if(frame >= 0 && frame < (int)art->frames) {
		width = 32u;
		format = art->frameFormat[frame];
		at = art->frameAt[frame];
	}
	else {
		return false;
	}
	if(format == UI_SAVES_ART_NONE || at > length ||
		artBytes(format, width * 32u) > length - at) {
		return false;
	}
	if(format == UI_SAVES_ART_RGB5A3) {
		memcpy(out, data + at, width * 32u * 2u);
		return true;
	}
	if(format == UI_SAVES_ART_CI8_SHARED && (art->paletteAt > length ||
		ART_PALETTE_BYTES > length - art->paletteAt)) {
		return false;
	}
	palette = data + (format == UI_SAVES_ART_CI8_OWN ? at + width * 32u :
		art->paletteAt);
	/* CI8 is 8x4 tiles of palette indices; RGB5A3 is 4x4 tiles of
	 * big-endian texels, as the palette holds them. */
	for(y = 0u; y < 32u; y++) {
		for(x = 0u; x < width; x++) {
			unsigned index = data[at + ((y / 4u) * (width / 8u) + x / 8u) * 32u +
				(y % 4u) * 8u + x % 8u];
			unsigned texel = ((y / 4u) * (width / 4u) + x / 4u) * 16u +
				(y % 4u) * 4u + x % 4u;

			out[texel * 2u] = palette[index * 2u];
			out[texel * 2u + 1u] = palette[index * 2u + 1u];
		}
	}
	return true;
}

const char *UISaves_IconDescription(const uint8_t entry[UI_SAVES_ENTRY_SIZE],
	size_t dataLength, bool motionOff)
{
	uiSavesArt_t art;
	unsigned i;

	if(entry == NULL) return "Unavailable";
	/* The format's no-icon markers are valid metadata, not read failures. */
	if(be32(entry + 0x2C) == UINT32_MAX || (entry[0x31] & 3u) == 0u ||
		(entry[0x33] & 3u) == 0u) return "None stored";
	UISaves_ArtLayout(entry, dataLength, &art);
	if(art.steps == 0u) return "Preview unavailable";
	for(i = 1u; i < art.steps; i++) {
		if(art.stepFrame[i] != art.stepFrame[0]) {
			return motionOff ? "Paused: UI Motion Off" : "Animated";
		}
	}
	return "Static";
}

int UISaves_ArtStep(const uiSavesArt_t *art, uint32_t tick)
{
	unsigned i;

	if(art == NULL || art->period == 0u) {
		return -1;
	}
	tick %= art->period;
	for(i = 0u; i < art->steps && i < UI_SAVES_ART_STEPS; i++) {
		if(tick < art->stepHold[i]) {
			return art->stepFrame[i] == UI_SAVES_ART_BLANK ? -1 :
				(int)art->stepFrame[i];
		}
		tick -= art->stepHold[i];
	}
	return -1;
}

/* ------------------------------------------------------------------------
 * The art pool.
 * --------------------------------------------------------------------- */
uint32_t UISaves_Id(uint32_t hash, const void *bytes, size_t length)
{
	const uint8_t *byte = bytes;
	size_t i;

	for(i = 0u; byte != NULL && i < length; i++) {
		hash = (hash ^ byte[i]) * 16777619u;
	}
	return hash != 0u ? hash : 1u;
}

static bool slotWanted(uint32_t tag, const uint32_t *want, int wantCount)
{
	int w;

	for(w = 0; want != NULL && w < wantCount; w++) {
		if(want[w] == tag) {
			return true;
		}
	}
	return false;
}

int UISaves_SlotPick(const uint32_t *tags, int slots, const uint32_t *want,
	int wantCount)
{
	int s;

	if(tags == NULL) {
		return -1;
	}
	for(s = 0; s < slots; s++) {
		if(tags[s] == 0u) {
			return s;
		}
	}
	for(s = 0; s < slots; s++) {
		if(!slotWanted(tags[s], want, wantCount)) {
			return s;
		}
	}
	return -1;
}

static int loadDistance(int cell, int focus, int columns)
{
	int rows, across;

	if(focus < 0) {
		return cell;
	}
	rows = cell / columns - focus / columns;
	across = cell % columns - focus % columns;
	return (rows < 0 ? -rows : rows) + (across < 0 ? -across : across);
}

int UISaves_LoadOrder(int focus, int firstRow, int rows, int columns,
	int count, int *out)
{
	int end = (firstRow + rows) * columns;
	int n = 0;
	int cell, i;

	if(out == NULL || rows <= 0 || columns <= 0) {
		return 0;
	}
	for(cell = firstRow > 0 ? firstRow * columns : 0; cell < end && cell < count;
		cell++) {
		int distance = loadDistance(cell, focus, columns);

		/* Cells come in order, so a tie keeps it. */
		for(i = n; i > 0 && loadDistance(out[i - 1], focus, columns) > distance;
			i--) {
			out[i] = out[i - 1];
		}
		out[i] = cell;
		n++;
	}
	return n;
}
