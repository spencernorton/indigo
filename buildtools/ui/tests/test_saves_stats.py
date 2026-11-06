#!/usr/bin/env python3
"""Exercise the real stats collector and RAW adapter with read-only devices."""

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui_saves_metadata.h"
#include "ui_saves_raw.h"
#include "saves_stats.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef struct file_handle file_handle;
typedef struct DEVICEHANDLER_STRUCT DEVICEHANDLER_INTERFACE;
struct file_handle {
	char name[1024]; u32 size, offset; unsigned fileType;
	const u8 *data; DEVICEHANDLER_INTERFACE *device;
};
struct DEVICEHANDLER_STRUCT {
	file_handle *initial; u32 location, features;
	s32 (*init)(file_handle *); s32 (*deinit)(file_handle *);
	s32 (*readDir)(file_handle *, file_handle **, u32);
	int64_t (*seekFile)(file_handle *, int64_t, unsigned);
	s32 (*readFile)(file_handle *, void *, u32);
	s32 (*writeFile)(file_handle *, const void *, u32);
	s32 (*closeFile)(file_handle *);
};
#define FEAT_WRITE 0x100u
/* The FAT driver's writer marks a FatFs device; the scan never writes. */
static s32 deviceHandler_FAT_writeFile(file_handle *f, const void *data, u32 bytes)
{
	(void)f; (void)data; (void)bytes; assert(!"the save scan writes"); return -1;
}
typedef struct {
	u8 gamecode[4], company[2], filename[32];
	u32 filelen; int fileno;
} card_dir;
typedef struct {
	u8 gamecode[4], company[2], filename[32];
	u32 len, time;
} card_stat;
#define IS_FILE 1u
#define IS_DIR 2u
#define DEVICE_HANDLER_SEEK_SET 0u
#define DEVICE_CUR 0
#define DEVICE_CONFIG 1
#define LOC_MEMCARD_SLOT_A 1u
#define LOC_MEMCARD_SLOT_B 2u
#define CARD_ERROR_READY 0
#define CARD_ERROR_BUSY (-1)
#define CARD_ERROR_NOCARD (-3)
#define CARD_ERROR_NOFILE (-4)
#define CARD_ERROR_WRONGDEVICE (-5)
/* sd holds the settings; sdc is another FatFs card, the Source with games on
 * it, where Swiss's Emulate Memory Card keeps their card images. */
static DEVICEHANDLER_INTERFACE __device_card_a, __device_card_b, sd, sdc, occupied;
static DEVICEHANDLER_INTERFACE *devices[2];
static file_handle initial[4];
static u8 *image, *gci;
static size_t imageSize, gciSize;
static bool configOk, directoryError, shortRead, seekError, invalidSave, hugeSave;
static bool statusError, statusMismatch, enumError, nestedFolder;
static unsigned listingMode, fileReads, fileCloses, configCloses, probes[2];
static unsigned mounts[2], unmounts[2], statusReads, maximumRead;
static int probeResult[2];
static card_dir physical;
/* The Source's swiss/saves: missing (0 files), or the game's card image and
 * a .gci of its save (2), and another file of the game, a second newer
 * than the rest (3). How often it was listed. */
static unsigned sourceFiles, sourceListings;
static u8 *otherFile;
static const char *saveFolder;
/* listingMode 3: this many .gci copies, each dated a second apart in an
 * order that isn't the listing's (copy i is updated + dateOf(i)); 4: the
 * same copies listed newest first (5 is its own inverse mod 12). */
#define DATED_COPIES 12u
static u8 *dated[DATED_COPIES];
static u32 dateOf(unsigned i) { return (i * 5u) % DATED_COPIES; }
static unsigned enumeration[2];
static u32 updated;
static void *host_memalign(size_t alignment, size_t bytes)
{
	void *p = NULL;
	assert(alignment == 32u);
	return posix_memalign(&p, alignment, bytes) == 0 ? p : NULL;
}
#define memalign host_memalign
static const char *saves_folder(void) { return saveFolder; }
static bool config_set_device(void)
{
	devices[DEVICE_CONFIG] = configOk ? &sd : NULL;
	return configOk;
}
static void config_unset_device(void) { ++configCloses; }
static const char *getRelativeName(const char *name)
{
	const char *p = strrchr(name, '/'); return p ? p + 1 : name;
}
static void concat_path(char *out, const char *root, const char *path)
{
	snprintf(out, 1024u, "%s%s", root, path);
}
static s32 CARD_ProbeEx(s32 slot, void *a, void *b)
{
	assert(slot >= 0 && slot < 2 && !a && !b);
	++probes[slot]; return probeResult[slot];
}
static s32 mount(file_handle *f)
{
	unsigned slot = f == &initial[0] ? 0u : 1u;
	++mounts[slot]; return 0;
}
static s32 unmount(file_handle *f)
{
	unsigned slot = f == &initial[0] ? 0u : 1u;
	++unmounts[slot]; return 0;
}
/* saves.c reads each listed copy's own line, while its card or SD card is
 * still mounted. */
static unsigned lineCalls;
static bool Saves_CopyLine(const savesCopy_t *copy, char *out, size_t size)
{
	if(copy->source == SAVES_COPY_SLOT_A || copy->source == SAVES_COPY_SLOT_B) {
		unsigned slot = copy->source == SAVES_COPY_SLOT_B;
		assert(mounts[slot] > unmounts[slot] && copy->device == NULL);
	}
	else assert(copy->device != NULL && configCloses == 0u);
	++lineCalls;
	snprintf(out, size, "line %u %s", (unsigned)copy->source,
		copy->path[0] != '\0' ? getRelativeName(copy->path) : "card");
	return true;
}
static s32 CARD_FindFirst(s32 slot, card_dir *dir, bool all)
{
	assert(all); enumeration[slot] = 0u;
	if(enumError) return CARD_ERROR_BUSY;
	*dir = physical; return CARD_ERROR_READY;
}
static s32 CARD_FindNext(card_dir *dir)
{
	(void)dir; return CARD_ERROR_NOFILE;
}
static s32 CARD_GetStatus(s32 slot, int file, card_stat *status)
{
	assert(slot >= 0 && slot < 2 && file == physical.fileno); ++statusReads;
	if(statusError) return CARD_ERROR_BUSY;
	memcpy(status->gamecode, physical.gamecode, 4u);
	memcpy(status->company, physical.company, 2u);
	memcpy(status->filename, physical.filename, 32u);
	status->len = physical.filelen; status->time = updated + 500u;
	if(statusMismatch) status->company[1] ^= 1u;
	return CARD_ERROR_READY;
}
static int64_t seek(file_handle *f, int64_t at, unsigned origin)
{
	assert(origin == 0u && at >= 0 && (uint64_t)at <= f->size);
	if(seekError) return -1;
	f->offset = (u32)at; return at;
}
static s32 readFile(file_handle *f, void *out, u32 bytes)
{
	assert(((uintptr_t)out & 31u) == 0u && bytes <= f->size - f->offset);
	assert(bytes <= UI_SAVES_RAW_METADATA_SIZE);
	++fileReads; if(bytes > maximumRead) maximumRead = bytes;
	if(shortRead) return (s32)bytes - 1;
	memcpy(out, f->data + f->offset, bytes); f->offset += bytes;
	return (s32)bytes;
}
static s32 closeFile(file_handle *f) { (void)f; ++fileCloses; return 0; }
/* Swiss's swiss/saves on the Source, whatever the Save Folder is. */
static s32 sourceDir(file_handle *f, file_handle **files)
{
	unsigned i;
	assert(!strcmp(f->name, "sdc:/swiss/saves")); ++sourceListings;
	if(sourceFiles == 0u) return -1;
	*files = calloc(sourceFiles, sizeof(**files)); assert(*files);
	for(i = 0u; i < sourceFiles; i++) {
		file_handle *entry = &(*files)[i];
		entry->fileType = IS_FILE; entry->device = &sdc;
		if(i == 0u) {
			strcpy(entry->name, "sdc:/swiss/saves/MemoryCardA.USA.raw");
			entry->data = image; entry->size = (u32)imageSize;
		}
		else if(i == 1u) {
			strcpy(entry->name, "sdc:/swiss/saves/export.gci");
			entry->data = gci; entry->size = (u32)gciSize;
		}
		else {
			strcpy(entry->name, "sdc:/swiss/saves/other.gci");
			entry->data = otherFile; entry->size = (u32)gciSize;
		}
	}
	return (s32)sourceFiles;
}
static s32 readDir(file_handle *f, file_handle **files, u32 kind)
{
	unsigned count = listingMode == 1u ? 17u : listingMode == 2u ? 300u :
		listingMode >= 3u ? DATED_COPIES : 5u;
	unsigned i;
	char folder[1024];
	assert(kind == UINT32_MAX);
	if(f->device == &sdc) return sourceDir(f, files);
	snprintf(folder, sizeof(folder), "sd:/%s", saveFolder);
	assert(f->device == &sd && !strcmp(f->name, folder));
	if(directoryError) return -1;
	*files = calloc(count, sizeof(**files)); assert(*files);
	for(i = 0u; i < count; i++) {
		file_handle *entry = &(*files)[i];
		entry->fileType = IS_FILE; entry->device = &sd;
		entry->data = listingMode == 3u ? dated[i] :
			listingMode == 4u ? dated[(DATED_COPIES - 1u - i) * 5u % DATED_COPIES] : gci;
		entry->size = (u32)gciSize;
		snprintf(entry->name, sizeof(entry->name), "sd:/swiss/saves/save%u.gci", i);
		if(listingMode == 1u || (listingMode == 0u && i == 1u)) {
			snprintf(entry->name, sizeof(entry->name), "sd:/swiss/saves/card%u.raw", i);
			entry->data = image; entry->size = (u32)imageSize;
		}
		if(listingMode == 0u && i == 2u) {
			strcpy(entry->name, "sd:/swiss/saves/._save.gci");
		}
		if(listingMode == 0u && i == 3u) {
			entry->fileType = IS_DIR;
			strcpy(entry->name, nestedFolder ? "sd:/swiss/saves/older" : "sd:/swiss/saves/.system");
		}
		if(listingMode == 0u && i == 4u) {
			strcpy(entry->name, "sd:/swiss/saves/readme.txt");
		}
		if(invalidSave && i == 0u) entry->size = 17u;
		if(hugeSave && i == 0u) entry->size = UINT32_MAX;
	}
	return (s32)count;
}
static void reset(void)
{
	configOk = true; directoryError = shortRead = seekError = invalidSave = hugeSave = false;
	statusError = statusMismatch = enumError = nestedFolder = false; listingMode = 0u;
	fileReads = fileCloses = configCloses = maximumRead = statusReads = lineCalls = 0u;
	memset(probes, 0, sizeof(probes)); memset(mounts, 0, sizeof(mounts));
	memset(unmounts, 0, sizeof(unmounts));
	probeResult[0] = CARD_ERROR_READY; probeResult[1] = CARD_ERROR_NOCARD;
	devices[0] = NULL; devices[1] = NULL;
	sourceFiles = 2u; sourceListings = 0u; saveFolder = "swiss/saves";
	sd.features = sdc.features = FEAT_WRITE;
	sd.writeFile = sdc.writeFile = deviceHandler_FAT_writeFile;
}
static u8 *load(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb"); long length; u8 *data;
	assert(f && fseek(f, 0, SEEK_END) == 0); length = ftell(f); assert(length > 0);
	assert(fseek(f, 0, SEEK_SET) == 0); *size = (size_t)length;
	data = malloc(*size); assert(data && fread(data, 1u, *size, f) == *size);
	assert(fclose(f) == 0); return data;
}
'''

MAIN = r'''
int main(int argc, char **argv)
{
	uiSavesGameStats_t stats; savesCopies_t copies; char id[6], wrong[6]; unsigned i;
	assert(argc == 3); image = load(argv[1], &imageSize); gci = load(argv[2], &gciSize);
	memcpy(id, gci, 6u); memcpy(wrong, id, 6u); wrong[5] = wrong[5] == '0' ? '1' : '0';
	updated = (u32)gci[0x28] << 24 | (u32)gci[0x29] << 16 |
		(u32)gci[0x2a] << 8 | gci[0x2b];
	memset(&physical, 0, sizeof(physical));
	memcpy(physical.gamecode, id, 4u); memcpy(physical.company, id + 4, 2u);
	memcpy(physical.filename, gci + 8, 32u); physical.filelen = 2u * 8192u;
	strcpy(initial[2].name, "sd:/"); strcpy(initial[3].name, "sdc:/");
	__device_card_a.initial = &initial[0]; __device_card_b.initial = &initial[1];
	__device_card_a.init = __device_card_b.init = mount;
	__device_card_a.deinit = __device_card_b.deinit = unmount;
	sd.initial = &initial[2]; sd.readDir = readDir; sd.seekFile = seek;
	sd.readFile = readFile; sd.closeFile = closeFile;
	sdc = sd; sdc.initial = &initial[3];
	/* Another file of the game: another name, saved a second later. */
	otherFile = malloc(gciSize); assert(otherFile);
	memcpy(otherFile, gci, gciSize); otherFile[8 + 9] = 'B';
	otherFile[0x2b] = (u8)(otherFile[0x2b] + 1u);
	for(i = 0u; i < DATED_COPIES; i++) {
		u32 when = updated + dateOf(i);
		dated[i] = malloc(gciSize); assert(dated[i]);
		memcpy(dated[i], gci, gciSize);
		dated[i][0x28] = (u8)(when >> 24); dated[i][0x29] = (u8)(when >> 16);
		dated[i][0x2a] = (u8)(when >> 8); dated[i][0x2b] = (u8)when;
	}
	/* The card's copy, the .gci and the image's save are one file in three
	 * places: three copies, its two blocks once. */
	reset(); Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 3u && stats.files == 1u && stats.blocks == 2u);
	assert(lineCalls == 0u);
	assert(stats.sourceSaves[0] == 1u && stats.sourceSaves[1] == 0u && stats.sourceSaves[2] == 2u);
	assert(!stats.partial && stats.checkedSources == 5u);
	assert(stats.updatedKnown && stats.latestUpdated == updated + 500u);
	assert(fileReads == 2u && fileCloses == 2u && maximumRead == 40960u);
	assert(configCloses == 1u && unmounts[0] == 1u && mounts[1] == 0u);
	/* The same scan lists where each copy is, for Detail's choice: the card
	 * in Slot A first, then the Save Folder's .gci and its card image's save
	 * (the same date: in the order the folder lists them). */
	reset(); memset(&copies, 0xa5, sizeof(copies));
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == 3u && copies.count == 3u);
	/* Each listed copy's own line, read where the copy is. */
	assert(lineCalls == 3u && !strcmp(copies.copy[0].line, "line 0 card") &&
		!strcmp(copies.copy[1].line, "line 2 save0.gci") &&
		!strcmp(copies.copy[2].line, "line 3 card1.raw"));
	assert(unmounts[0] == 1u && configCloses == 1u);
	assert(copies.cards[0] && !copies.cards[1]);
	assert(copies.copy[0].source == SAVES_COPY_SLOT_A &&
		!memcmp(copies.copy[0].entry, id, 6u) &&
		!memcmp(copies.copy[0].entry + 8, physical.filename, 32u) &&
		copies.copy[0].entry[0x39] == 2u && copies.copy[0].path[0] == '\0' &&
		copies.copy[0].device == NULL);
	assert(copies.copy[1].source == SAVES_COPY_FILE && copies.copy[1].device == &sd &&
		!strcmp(copies.copy[1].path, "sd:/swiss/saves/save0.gci") &&
		copies.copy[1].size == (u32)gciSize && !memcmp(copies.copy[1].entry, gci, 64u));
	assert(copies.copy[2].source == SAVES_COPY_IMAGE && copies.copy[2].device == &sd &&
		!strcmp(copies.copy[2].path, "sd:/swiss/saves/card1.raw") &&
		copies.copy[2].size == (u32)imageSize && copies.copy[2].ordinal == 0u &&
		!memcmp(copies.copy[2].entry, id, 6u));
	assert(Saves_SourceDevice() == NULL && sourceListings == 0u);
	/* Settings on one card, games on another: Swiss's Emulate Memory Card
	 * keeps the game's card images in the Source's swiss/saves, so their
	 * saves and a .gci there count too, each copy with the device it is on,
	 * after the Save Folder's (the same date: in the order they were read). */
	reset(); devices[DEVICE_CUR] = &sdc; memset(&copies, 0xa5, sizeof(copies));
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(sourceListings == 1u && Saves_SourceDevice() == &sdc);
	assert(stats.saves == 5u && stats.files == 1u && stats.blocks == 2u &&
		stats.sourceSaves[2] == 4u);
	assert(!stats.partial && stats.checkedSources == 5u && configCloses == 1u);
	assert(copies.count == 5u && copies.copy[0].source == SAVES_COPY_SLOT_A);
	assert(copies.copy[1].device == &sd && copies.copy[2].device == &sd);
	assert(copies.copy[3].source == SAVES_COPY_IMAGE && copies.copy[3].device == &sdc &&
		!strcmp(copies.copy[3].path, "sdc:/swiss/saves/MemoryCardA.USA.raw") &&
		copies.copy[3].size == (u32)imageSize && copies.copy[3].ordinal == 0u &&
		!memcmp(copies.copy[3].entry, id, 6u));
	assert(copies.copy[4].source == SAVES_COPY_FILE && copies.copy[4].device == &sdc &&
		!strcmp(copies.copy[4].path, "sdc:/swiss/saves/export.gci") &&
		copies.copy[4].size == (u32)gciSize && !memcmp(copies.copy[4].entry, gci, 64u));
	assert(lineCalls == 5u && !strcmp(copies.copy[3].line, "line 3 MemoryCardA.USA.raw"));
	/* Another file of the game there: two files, each one's blocks once,
	 * and each file's copies together on the list, the card's file first,
	 * though the other file is the newest. */
	reset(); devices[DEVICE_CUR] = &sdc; sourceFiles = 3u;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == 6u && stats.files == 2u && stats.blocks == 4u);
	assert(copies.count == 6u && copies.copy[0].source == SAVES_COPY_SLOT_A);
	for(i = 0u; i < 5u; i++) assert(!memcmp(copies.copy[i].entry + 8, gci + 8, 32u));
	assert(!strcmp(copies.copy[5].path, "sdc:/swiss/saves/other.gci") &&
		copies.copy[5].entry[8 + 9] == 'B');
	assert(UISaves_UpdatedSeconds(copies.copy[5].entry) == updated + 1u);
	/* Swiss's folder there, not the Save Folder's name. */
	reset(); devices[DEVICE_CUR] = &sdc; saveFolder = "saves";
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(sourceListings == 1u && stats.saves == 5u && !stats.partial);
	/* Swiss makes that folder only once a game starts there with Emulate
	 * Memory Card on: a Source without one has no copies, and nothing is
	 * partial about it. */
	reset(); devices[DEVICE_CUR] = &sdc; sourceFiles = 0u;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(sourceListings == 1u && stats.saves == 3u && copies.count == 3u && !stats.partial);
	/* The Source is read only when it is another card than the settings
	 * device, FatFs, and takes writes (Memory Cards' rule): never twice. */
	reset(); devices[DEVICE_CUR] = &sd;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(Saves_SourceDevice() == NULL && sourceListings == 0u && stats.saves == 3u);
	reset(); devices[DEVICE_CUR] = &sdc; sdc.writeFile = NULL;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(Saves_SourceDevice() == NULL && sourceListings == 0u && stats.saves == 3u);
	reset(); devices[DEVICE_CUR] = &sdc; sdc.features = 0u;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(Saves_SourceDevice() == NULL && sourceListings == 0u && stats.saves == 3u);
	/* Without a settings device the Source's copies still count; the scan
	 * says it is partial, the Save Folder unread. */
	reset(); devices[DEVICE_CUR] = &sdc; configOk = false;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(sourceListings == 1u && stats.saves == 3u && stats.partial && configCloses == 0u);
	/* The list holds eight; the totals still count every one. */
	reset(); listingMode = 1u; probeResult[0] = CARD_ERROR_NOCARD;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == 16u && copies.count == SAVES_COPIES_MAX && !copies.cards[0]);
	/* However many the Save Folder holds, the card's own copy, the one the
	 * game reads, stays on the list, first. */
	reset(); listingMode = 1u;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == 17u && copies.count == SAVES_COPIES_MAX && copies.cards[0]);
	assert(copies.copy[0].source == SAVES_COPY_SLOT_A);
	for(i = 1u; i < copies.count; i++) assert(copies.copy[i].source == SAVES_COPY_IMAGE);
	/* Of the Save Folder's copies the newest stay, newest first, whatever
	 * order the folder lists them in. */
	reset(); listingMode = 3u;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == DATED_COPIES + 1u && copies.count == SAVES_COPIES_MAX);
	assert(copies.copy[0].source == SAVES_COPY_SLOT_A);
	for(i = 1u; i < copies.count; i++) {
		assert(copies.copy[i].source == SAVES_COPY_FILE);
		assert(UISaves_UpdatedSeconds(copies.copy[i].entry) ==
			updated + DATED_COPIES - i);
	}
	/* Two cards' copies keep the order the slots were read in, Slot A's
	 * first, whichever Save Folder copies they took the place of (here the
	 * oldest is last on the list, the next oldest just before it). */
	reset(); listingMode = 4u; probeResult[1] = CARD_ERROR_READY;
	Saves_CollectGameStats(id, &stats, &copies, 3u);
	assert(stats.saves == DATED_COPIES + 2u && copies.count == SAVES_COPIES_MAX);
	assert(copies.cards[0] && copies.cards[1]);
	assert(copies.copy[0].source == SAVES_COPY_SLOT_A &&
		copies.copy[1].source == SAVES_COPY_SLOT_B);
	for(i = 2u; i < copies.count; i++) {
		assert(copies.copy[i].source == SAVES_COPY_FILE);
		assert(UISaves_UpdatedSeconds(copies.copy[i].entry) ==
			updated + DATED_COPIES + 1u - i);
	}
	/* No copy of another game's save, and an empty list without an ID. */
	reset(); Saves_CollectGameStats(wrong, &stats, &copies, 3u);
	assert(stats.saves == 0u && copies.count == 0u && copies.cards[0]);
	memset(&copies, 0xa5, sizeof(copies));
	Saves_CollectGameStats(NULL, &stats, &copies, 3u);
	assert(copies.count == 0u && !copies.cards[0]);
	reset(); Saves_CollectGameStats(wrong, &stats, NULL, 3u);
	assert(stats.saves == 0u && stats.blocks == 0u && !stats.updatedKnown);
	reset(); statusError = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 3u && stats.partial && stats.latestUpdated == updated);
	reset(); statusMismatch = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 3u && stats.partial && stats.latestUpdated == updated);
	reset(); invalidSave = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 2u && stats.blocks == 2u && stats.partial && fileReads == 1u);
	reset(); hugeSave = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 2u && stats.blocks == 2u && stats.partial && fileReads == 1u);
	reset(); shortRead = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 1u && stats.partial && fileCloses == 2u);
	reset(); seekError = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 1u && stats.partial && fileReads == 0u && fileCloses == 2u);
	reset(); configOk = false; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 1u && stats.partial && fileReads == 0u && configCloses == 0u);
	reset(); directoryError = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 1u && stats.partial && stats.checkedSources == 1u);
	reset(); enumError = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 2u && stats.partial && unmounts[0] == 1u);
	reset(); devices[0] = &occupied; occupied.location = LOC_MEMCARD_SLOT_A;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 2u && probes[0] == 0u && mounts[0] == 0u);
	reset(); devices[0] = &__device_card_a; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 3u && unmounts[0] == 0u);
	reset(); probeResult[0] = CARD_ERROR_BUSY; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 2u && stats.partial && probes[0] == 1u && mounts[0] == 0u);
	reset(); listingMode = 1u; probeResult[0] = CARD_ERROR_NOCARD;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 16u && stats.files == 1u && stats.blocks == 2u && stats.partial &&
		fileReads == 16u);
	reset(); listingMode = 2u; probeResult[0] = CARD_ERROR_NOCARD;
	Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 256u && stats.partial && fileReads == 256u);
	/* A card still switching to the game is left out of the mask: never
	 * probed or mounted, and the rest still counted. */
	reset(); Saves_CollectGameStats(id, &stats, NULL, 2u);
	assert(stats.saves == 2u && probes[0] == 0u && mounts[0] == 0u && probes[1] == 1u);
	assert((stats.checkedSources & 1u) == 0u);
	reset(); Saves_CollectGameStats(id, &stats, NULL, 0u);
	assert(stats.saves == 2u && probes[0] == 0u && probes[1] == 0u);
	reset(); nestedFolder = true; Saves_CollectGameStats(id, &stats, NULL, 3u);
	assert(stats.saves == 3u && stats.blocks == 2u && stats.partial);
	reset(); for(i = 0u; i < 6u; i++) wrong[i] = '\0';
	Saves_CollectGameStats(wrong, &stats, NULL, 3u);
	assert(stats.saves == 0u && stats.checkedSources == 0u && fileReads == 0u && probes[0] == 0u);
	Saves_CollectGameStats(NULL, &stats, NULL, 3u); assert(stats.saves == 0u);
	Saves_CollectGameStats(id, NULL, NULL, 3u);
	for(i = 0u; i < DATED_COPIES; i++) free(dated[i]);
	free(image); free(gci); free(otherFile);
	puts("save stats: exact IDs, real RAW metadata, bounded reads, partial failures, retained mounts, copy places and devices, the Source's swiss/saves, totals by file, each file's copies together with their lines, the cards first in slot order and the newest kept PASS");
	return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location(
        "stats_fixture", ROOT / "buildtools/ui/qa/make_test_saves.py")
    fixture = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = fixture
    spec.loader.exec_module(fixture)
    image, entries = fixture.virtual_card()
    sources = []
    for name in ("saves_raw.c", "saves_stats.c"):
        # The scan only reads images: saves_raw.c's in-place writer (FatFs) stays out.
        text = (GUI / name).read_text()
        writer = text.find(" * Changing an image in place.")
        if writer >= 0:
            text = text[:text.rindex("/*", 0, writer)]
        sources.append("\n".join(line for line in text.splitlines()
                                  if not line.startswith("#include")))
    with tempfile.TemporaryDirectory(prefix="save-stats-") as tmp:
        path = Path(tmp)
        (path / "fixture.raw").write_bytes(image)
        (path / "fixture.gci").write_bytes(entries[0])
        (path / "test.c").write_text(HARNESS + "\n".join(sources) + MAIN)
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200112L",
                   "-Wall", "-Wextra", "-Werror", "-g", "-I", str(GUI),
                   str(path / "test.c"), str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"),
                   str(GUI / "ui_saves_metadata.c"), str(GUI / "ui_saves_raw.c"),
                   "-o", str(path / "test")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test"), str(path / "fixture.raw"),
                        str(path / "fixture.gci")], check=True)


if __name__ == "__main__":
    main()
