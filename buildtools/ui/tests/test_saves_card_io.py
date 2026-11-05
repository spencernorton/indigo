#!/usr/bin/env python3
"""Run the real card driver and Memory Cards copy engine against two cards.

The CARD API model deliberately implements libogc2's fixed-code/string
boundary and its leading-0xff wildcard lookup. The production read, write,
erase, lookup and copy functions are extracted unchanged. Saves sharing a
filename must still keep their game and maker identity, including when a
failed write or read-back removes only the newly created destination.

Game Details' load (copyRead, Saves_LoadCopy) runs too, with two SD cards
beside the memory cards: the settings device and the Source, whose
swiss/saves holds the public test card image and a .gci. A copy is read
from the device the scan found it on, through saves_raw.c's own readers for
an image, and the card's own copy is kept on the settings device.
"""

import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
DRIVER = ROOT / "cube/swiss/source/devices/memcard/deviceHandler-CARD.c"
SAVES = ROOT / "cube/swiss/source/gui/saves.c"

PRELUDE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ui_saves.h"
#include "saves_stats.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef struct {
    s32 chn, fileno;
    u32 filelen;
    u8 permissions;
    char filename[32];
    u8 gamecode[4], company[2];
    bool showall;
} card_dir;
typedef struct { s32 chn, filenum; u32 offset, len; } card_file;
typedef struct {
    u8 gamecode[4], company[2];
    char filename[32];
    u32 len, time, icon_addr, comment_addr;
    u16 icon_fmt, icon_speed;
    u8 banner_fmt;
} card_stat;
typedef struct { u32 totalSpace; } device_info;
typedef struct file_handle file_handle;
typedef struct DEVICEHANDLER_STRUCT {
    file_handle *initial;
    s32 (*init)(file_handle *);
    device_info *(*info)(file_handle *);
    s32 (*readDir)(file_handle *, file_handle **, u32);
    s32 (*readFile)(file_handle *, void *, u32);
    s32 (*writeFile)(file_handle *, const void *, u32);
    s32 (*deleteFile)(file_handle *);
    s32 (*closeFile)(file_handle *);
    long long (*seekFile)(file_handle *, long long, u32);
    s32 (*statFile)(file_handle *);
    s32 (*deinit)(file_handle *);
} DEVICEHANDLER_INTERFACE;
struct file_handle {
    char name[1024];
    u32 size, offset;
    unsigned fileType;
    u8 other[128];
    DEVICEHANDLER_INTERFACE *device;
};
#define IS_FILE 1
#define PATHNAME_MAX 1024
#define DEVICE_CUR 0
#define DEVICE_CONFIG 1
#define CARD_FILENAMELEN 32
#define CARD_MAXFILES 127
#define CARD_ERROR_READY 0
#define CARD_ERROR_NOFILE -4
#define CARD_ERROR_BUSY -1
#define CARD_ERROR_FATAL_ERROR -128
#define CARD_ERROR_INSSPACE -9
#define CARD_ERROR_NOENT -8
#define CARD_ERROR_EXIST -7
#define CARD_ERROR_NOCARD -3
#define DEVICE_HANDLER_SEEK_SET 0
#define SAVES_MAX_BYTES (16u * 1024u * 1024u)
#define SAVES_SYSTEM_BLOCKS 5
#define CARD_SPEED_FAST 1
#define CARD_BANNER_NONE 0
#define CARD_ICON_RGB 2
#define D_FAIL 0
#define print_debug(...) ((void)0)
#define DCFlushRange(p, n) ((void)0)
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
#define CARD_SetGameAndCompany() CARD_SetGamecode("SWIS"); CARD_SetCompany("S0")
typedef int uiDrawObj_t;
static uiDrawObj_t *DrawMessageBox(int style, const char *text) {
    (void)style; (void)text; return NULL;
}
static void DrawPublish(uiDrawObj_t *box) { (void)box; }
static void DrawDispose(uiDrawObj_t *box) { (void)box; }
static void wait_press_A(void) {}
static const char *cardError(int error) { (void)error; return "error"; }
static void *memalign(size_t alignment, size_t bytes) {
    (void)alignment; return malloc(bytes);
}
static char *getRelativeName(char *path) { return strrchr(path, '/') + 1; }
static file_handle initial_CARDA = { .name = "carda:/" };
static file_handle initial_CARDB = { .name = "cardb:/" };
static u32 card_sectorsize[2] = {8192, 8192};
static const u8 gamecube_rgb[1] = {0};
static bool isCopyGCIMode;
static struct { bool mounted; } places[2];

/* Fixed-length codes are libogc2 API filters; a rejected string length
 * leaves the filter as ff, which means any game or maker. */
static u8 gameFilter[4], makerFilter[2];
static void CARD_SetGamecode(const char *code) {
    memset(gameFilter, 0xff, sizeof(gameFilter));
    if(code != NULL && strlen(code) <= 4) memcpy(gameFilter, code, 4);
}
static void CARD_SetCompany(const char *code) {
    memset(makerFilter, 0xff, sizeof(makerFilter));
    if(code != NULL && strlen(code) <= 2) memcpy(makerFilter, code, 2);
}
typedef struct {
    bool live;
    card_dir dir;
    u8 bytes[8192];
    card_stat stat;
} record;
static record cards[2][8];
static bool failWrite, failReadBack, wrongReadIdentity;
/* A memory card emulator switching under the copy: from that write or read
 * on, Slot B holds another card whose save has the same game, maker and
 * name ('S'), mounted afresh. */
static bool changeOnWrite, changeOnRead;
static bool changeToEmptyOnWrite, changeToEmptyOnRead, changeToEmptyOnInit, armChangeOnDelete;
static int changeToEmptyReadsSkip;
static u32 mountCount[2];
static u32 card_mount_count(int slot) { return mountCount[slot]; }
static void changeCard(int slot, int i) {
    memset(cards[slot][i].bytes, 'S', 8192); mountCount[slot]++;
}
static void changeToEmptyCard(int slot) {
    memset(cards[slot], 0, sizeof(cards[slot])); mountCount[slot]++;
}
static int saveOn(int slot, const char *game) {
    for(int i = 0; i < 8; i++)
        if(cards[slot][i].live && !memcmp(cards[slot][i].dir.gamecode, game, 4)) return i;
    return -1;
}
static int failWritesLeft, failReadBacksLeft;
/* Reads that read back true before failReadBacksLeft starts: cardReplace
 * reads the card's own copy once more before it deletes it. */
static int failReadBacksSkip;
static bool readBackFails(void) {
    if(failReadBack) return true;
    if(failReadBacksLeft > 0 && failReadBacksSkip > 0) { failReadBacksSkip--; return false; }
    return failReadBacksLeft > 0 && failReadBacksLeft--;
}
static int writes, deletes, mounts;
static int lookup(int slot, const char *name) {
    for(int i = 0; i < 8; i++) {
        record *r = &cards[slot][i];
        if(r->live && !strncmp(r->dir.filename, name, 32) &&
           (gameFilter[0] == 0xff || !memcmp(r->dir.gamecode, gameFilter, 4)) &&
           (makerFilter[0] == 0xff || !memcmp(r->dir.company, makerFilter, 2)))
            return i;
    }
    return -1;
}
static s32 CARD_Open(int slot, const char *name, card_file *file) {
    int i = lookup(slot, name);
    if(i < 0) return CARD_ERROR_NOFILE;
    *file = (card_file){ .chn = slot, .filenum = i, .len = 8192 };
    return CARD_ERROR_READY;
}
static s32 CARD_Create(int slot, const char *name, u32 length, card_file *file) {
    assert(length == 8192);
    if(lookup(slot, name) >= 0) return CARD_ERROR_EXIST;
    for(int i = 0; i < 8; i++) {
        record *r = &cards[slot][i];
        if(r->live) continue;
        memset(r, 0, sizeof(*r)); r->live = true;
        r->dir.chn = slot; r->dir.fileno = i; r->dir.filelen = length;
        r->dir.showall = true;
        memcpy(r->dir.gamecode, gameFilter, 4);
        memcpy(r->dir.company, makerFilter, 2);
        memcpy(r->dir.filename, name, strnlen(name, 32));
        memcpy(r->stat.gamecode, gameFilter, 4);
        memcpy(r->stat.company, makerFilter, 2);
        memcpy(r->stat.filename, r->dir.filename, 32);
        r->stat.len = length;
        return CARD_Open(slot, name, file);
    }
    return CARD_ERROR_NOENT;
}
static s32 CARD_Read(card_file *file, void *buffer, u32 length, u32 at) {
    assert(at == 0 && length == 8192);
    if(changeOnRead) { changeOnRead = false; changeCard(file->chn, file->filenum); }
    if(changeToEmptyOnRead) {
        if(changeToEmptyReadsSkip > 0) { changeToEmptyReadsSkip--; }
        else { changeToEmptyOnRead = false; changeToEmptyCard(file->chn); }
    }
    memcpy(buffer, cards[file->chn][file->filenum].bytes, length);
    if(readBackFails()) ((u8 *)buffer)[0] ^= 1;
    return CARD_ERROR_READY;
}
static s32 CARD_ReadUnaligned(card_file *file, void *buffer, u32 length, u32 at, int slot) {
    (void)slot; return CARD_Read(file, buffer, length, at);
}
static s32 CARD_Write(card_file *file, const void *buffer, u32 length, u32 at) {
    writes++; assert(at == 0 && length == 8192);
    memcpy(cards[file->chn][file->filenum].bytes, buffer, length);
    if(changeOnWrite) { changeOnWrite = false; changeCard(file->chn, file->filenum); }
    if(changeToEmptyOnWrite) { changeToEmptyOnWrite = false; changeToEmptyCard(file->chn); }
    return failWrite || (failWritesLeft > 0 && failWritesLeft--) ?
        CARD_ERROR_FATAL_ERROR : CARD_ERROR_READY;
}
static s32 CARD_GetStatus(int slot, int i, card_stat *stat) {
    *stat = cards[slot][i].stat;
    if(wrongReadIdentity) stat->gamecode[3] ^= 1;
    return CARD_ERROR_READY;
}
static s32 CARD_SetStatus(int slot, int i, const card_stat *stat) {
    record *r = &cards[slot][i];
    r->stat.banner_fmt = stat->banner_fmt; r->stat.time = stat->time;
    r->stat.icon_addr = stat->icon_addr; r->stat.icon_fmt = stat->icon_fmt;
    r->stat.icon_speed = stat->icon_speed; r->stat.comment_addr = stat->comment_addr;
    return CARD_ERROR_READY;
}
static s32 CARD_Close(card_file *file) { (void)file; return 0; }
static s32 CARD_Delete(int slot, const char *name) {
    int i = lookup(slot, name);
    if(i < 0) return CARD_ERROR_NOFILE;
    if(armChangeOnDelete && !strncmp(name, "same-name", 32) &&
       !memcmp(cards[slot][i].dir.gamecode, "GZLE", 4)) {
        changeToEmptyOnInit = true;
        armChangeOnDelete = false;
    }
    cards[slot][i].live = false; deletes++; return 0;
}
static bool findFile(file_handle *file) { (void)file; assert(false); return false; }
static s32 mounted(file_handle *file) {
    mounts++;
    if(changeToEmptyOnInit) {
        changeToEmptyOnInit = false;
        int slot = file == &initial_CARDB;
        changeToEmptyCard(slot);
    }
    return 0;
}
static device_info *info(file_handle *file) {
    static device_info room = { 64u * 8192u }; (void)file; return &room;
}
static s32 closeFile(file_handle *file) { (void)file; return 0; }
static long long seekFile(file_handle *file, long long at, u32 type) {
    assert(type == DEVICE_HANDLER_SEEK_SET); file->offset = (u32)at; return at;
}
static void concatf_path(char *out, const char *root, const char *fmt,
                         int length, const char *name) {
    (void)fmt; snprintf(out, 1024, "%s%.*s", root, length, name);
}
static DEVICEHANDLER_INTERFACE cardDevices[2];
static DEVICEHANDLER_INTERFACE *slotDevice(int slot) { return &cardDevices[slot]; }
static const char *slotName(int slot) { return slot ? "Slot B" : "Slot A"; }
static bool isCard(DEVICEHANDLER_INTERFACE *device) {
    return device == &cardDevices[0] || device == &cardDevices[1];
}
static void handle(file_handle *file, int slot, int i) {
    memset(file, 0, sizeof(*file));
    snprintf(file->name, sizeof(file->name), "card%c:/%.*s", 'a' + slot,
             32, cards[slot][i].dir.filename);
    file->device = &cardDevices[slot]; file->size = 8192;
    memcpy(file->other, &cards[slot][i].dir, sizeof(card_dir));
}
static s32 unmounted(file_handle *file) { (void)file; return 0; }

/* Two SD cards' files, by name: the settings device (gcldr:) and the Source
 * (sdc:), with what was written and deleted on each. */
typedef struct { char name[1024]; u8 *data; u32 size; } sdFile;
static struct { sdFile file[4]; int count, writes, deletes; } sdCards[2];
static file_handle initial_SETTINGS = { .name = "gcldr:/" };
static file_handle initial_SOURCE = { .name = "sdc:/" };
static DEVICEHANDLER_INTERFACE settingsSd, sourceSd;
static DEVICEHANDLER_INTERFACE *devices[2];
static bool configOk;
static int folderEnsures;
static int sdOf(file_handle *file) {
    assert(file->device == &settingsSd || file->device == &sourceSd);
    return file->device == &sourceSd;
}
static sdFile *sdFind(file_handle *file) {
    int card = sdOf(file);
    for(int i = 0; i < sdCards[card].count; i++)
        if(!strcmp(sdCards[card].file[i].name, file->name)) return &sdCards[card].file[i];
    return NULL;
}
static void sdPut(int card, const char *name, const u8 *data, u32 size) {
    sdFile *f = &sdCards[card].file[sdCards[card].count++];
    assert(sdCards[card].count <= 4);
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->data = malloc(size); assert(f->data != NULL);
    memcpy(f->data, data, size); f->size = size;
}
static s32 sdStat(file_handle *file) { return sdFind(file) != NULL ? 0 : -1; }
/* As the FAT driver's writeFile, on a name folderWrite found free. */
static s32 sdWrite(file_handle *file, const void *data, u32 size) {
    assert(sdFind(file) == NULL);
    sdCards[sdOf(file)].writes++;
    sdPut(sdOf(file), file->name, data, size);
    return (s32)size;
}
static s32 sdRead(file_handle *file, void *out, u32 size) {
    sdFile *f = sdFind(file);
    if(f == NULL || file->offset > f->size || size > f->size - file->offset) return -1;
    memcpy(out, f->data + file->offset, size); file->offset += size;
    return (s32)size;
}
static long long sdSeek(file_handle *file, long long at, u32 type) {
    assert(type == DEVICE_HANDLER_SEEK_SET); file->offset = (u32)at; return at;
}
static s32 sdClose(file_handle *file) { (void)file; return 0; }
static s32 sdDelete(file_handle *file) {
    int card = sdOf(file);
    sdFile *f = sdFind(file);
    if(f == NULL) return -1;
    free(f->data); *f = sdCards[card].file[--sdCards[card].count];
    sdCards[card].deletes++;
    return 0;
}
static bool config_set_device(void) {
    devices[DEVICE_CONFIG] = configOk ? &settingsSd : NULL; return configOk;
}
static void config_unset_device(void) {}
static const char *saves_folder(void) { return "swiss/saves"; }
static size_t concat_path(char *out, const char *dir, const char *base) {
    size_t n = strlen(dir);
    return (size_t)snprintf(out, PATHNAME_MAX, "%s%s%s", dir,
        n && dir[n - 1] != '/' && base[0] != '/' ? "/" : "", base);
}
static void folderEnsure(DEVICEHANDLER_INTERFACE *device, const char *path) {
    assert(device == &settingsSd && !strcmp(path, "swiss/saves")); folderEnsures++;
}
static s32 readDir(file_handle *root, file_handle **entries, u32 type) {
    (void)type; int slot = root == &initial_CARDB, n = 1;
    *entries = calloc(9, sizeof(file_handle)); assert(*entries != NULL);
    for(int i = 0; i < 8; i++) if(cards[slot][i].live)
        handle(&(*entries)[n++], slot, i);
    return n;
}
static void setGCIInfo(const void *buffer);
static void setCopyGCIMode(bool mode);
"""

MAIN = r"""
static void addNamed(int slot, int i, const char *game, const char *maker,
                     const char *name, u8 byte) {
    CARD_SetGamecode(game); CARD_SetCompany(maker);
    card_file file; assert(CARD_Create(slot, name, 8192, &file) == 0);
    assert(file.filenum == i);
    memset(cards[slot][i].bytes, byte, 8192);
}
static void add(int slot, int i, const char *game, const char *maker, u8 byte) {
    addNamed(slot, i, game, maker, "same-name", byte);
}
static void reset(void) {
    memset(cards, 0, sizeof(cards)); writes = deletes = mounts = 0;
    failWrite = failReadBack = wrongReadIdentity = false;
    changeOnWrite = changeOnRead = false;
    changeToEmptyOnWrite = changeToEmptyOnRead = changeToEmptyOnInit = armChangeOnDelete = false;
    changeToEmptyReadsSkip = 0;
    mountCount[0] = mountCount[1] = 0;
    failWritesLeft = failReadBacksLeft = failReadBacksSkip = 0;
    cardDevices[0] = (DEVICEHANDLER_INTERFACE){ &initial_CARDA, mounted, info,
        readDir, deviceHandler_CARD_readFile, deviceHandler_CARD_writeFile,
        deviceHandler_CARD_deleteFile, closeFile, seekFile, NULL, unmounted };
    cardDevices[1] = cardDevices[0]; cardDevices[1].initial = &initial_CARDB;
    for(int card = 0; card < 2; card++) {
        for(int i = 0; i < sdCards[card].count; i++) free(sdCards[card].file[i].data);
    }
    memset(sdCards, 0, sizeof(sdCards)); folderEnsures = 0; configOk = true;
    settingsSd = (DEVICEHANDLER_INTERFACE){ .initial = &initial_SETTINGS,
        .readFile = sdRead, .writeFile = sdWrite, .deleteFile = sdDelete,
        .closeFile = sdClose, .seekFile = sdSeek, .statFile = sdStat };
    sourceSd = settingsSd; sourceSd.initial = &initial_SOURCE;
    devices[DEVICE_CUR] = &sourceSd; devices[DEVICE_CONFIG] = NULL;
}
static void verifySurvivor(int slot, u8 byte) {
    assert(cards[slot][0].live);
    for(int i = 0; i < 8192; i++) assert(cards[slot][0].bytes[i] == byte);
}
static void readErase(const char *game, const char *maker) {
    reset(); add(0, 0, "GALE", "01", 'E'); add(0, 1, game, maker, 'P');
    file_handle file; handle(&file, 0, 1);
    card_dir *dir = (card_dir *)file.other;
    if(game[0] != '\0' && maker[0] != '\0') {
        assert(strlen((char *)dir->gamecode) == 7);
        assert(strlen((char *)dir->company) == 3);
    }
    u32 length = 0; u8 *data = saveRead(&file, &length);
    assert(data != NULL && length == 8256);
    assert(!memcmp(data, game, 4) && !memcmp(data + 4, maker, 2));
    for(int i = 64; i < 8256; i++) assert(data[i] == 'P');
    free(data);
    assert(deviceHandler_CARD_deleteFile(&file) == 0);
    assert(!cards[0][1].live && deletes == 1); verifySurvivor(0, 'E');
}
static void copyCases(const char *game, const char *maker) {
    for(int failure = 0; failure < 4; failure++) {
        reset(); add(0, 0, "GALE", "01", 'E'); add(0, 1, game, maker, 'P');
        add(1, 0, "GALE", "01", 'E');
        file_handle source; handle(&source, 0, 1);
        u32 length = 0; u8 *data = saveRead(&source, &length);
        assert(data != NULL);
        /* readFile builds its GCI's integer fields in the target's native
         * big-endian layout. Normalise only the block count on this host
         * before ui_saves.c's deliberately byte-based parser uses it. */
        data[0x38] = 0; data[0x39] = 1;
        failWrite = failure == 1; failReadBack = failure == 2;
        wrongReadIdentity = failure == 3;
        char why[160];
        bool ok = cardWrite(1, data, data + 64, length - 64, why, sizeof(why));
        assert(ok == (failure == 0));
        verifySurvivor(1, 'E');
        assert(cards[1][1].live == ok);
        assert(deletes == (ok ? 0 : 1));
        if(ok) {
            /* Move uses this same source handle only after cardWrite succeeds. */
            assert(deviceHandler_CARD_deleteFile(&source) == 0);
            assert(!cards[0][1].live); verifySurvivor(0, 'E');
            assert(!memcmp(cards[1][1].dir.gamecode, game, 4));
            assert(!memcmp(cards[1][1].dir.company, maker, 2));
            for(int i = 0; i < 8192; i++) assert(cards[1][1].bytes[i] == 'P');
            /* Copying the exact identity again never writes or erases it. */
            int beforeWrites = writes, beforeDeletes = deletes;
            assert(!cardWrite(1, data, data + 64, length - 64, why, sizeof(why)));
            assert(writes == beforeWrites && deletes == beforeDeletes);
        }
        free(data);
    }
}
/* The card changed during a failed write or during the read-back: the
 * same-named save on the card there now isn't this write's, so it stays
 * (review finding: cardWrite deleted it). */
static void changedCardCases(void) {
    for(int when = 0; when < 2; when++) {
        reset(); add(0, 0, "GALP", "01", 'P');
        file_handle source; handle(&source, 0, 0);
        u32 length = 0; u8 *data = saveRead(&source, &length);
        assert(data != NULL);
        data[0x38] = 0; data[0x39] = 1;
        failWrite = when == 0; changeOnWrite = when == 0; changeOnRead = when == 1;
        char why[160];
        assert(!cardWrite(1, data, data + 64, length - 64, why, sizeof(why)));
        assert(writes == 1 && deletes == 0 && strstr(why, "changed") != NULL);
        int at = saveOn(1, "GALP");
        assert(at >= 0);
        for(int i = 0; i < 8192; i++) assert(cards[1][at].bytes[i] == 'S');
        free(data);
    }
}
static void wildcardCases(void) {
    for(int position = 0; position <= 4; position += 4) {
        reset(); add(1, 0, "GALE", "01", 'E');
        u8 entry[64] = {0}, blocks[8192]; char why[160];
        memcpy(entry, "GALE01", 6); memcpy(entry + 8, "same-name", 9);
        entry[0x39] = 1; entry[position] = 0xff; memset(blocks, 'P', sizeof(blocks));
        assert(!cardWrite(1, entry, blocks, sizeof(blocks), why, sizeof(why)));
        assert(writes == 0 && deletes == 0 && mounts == 0); verifySurvivor(1, 'E');
        /* The driver's public copy entry point has the same safeguard. */
        file_handle dest = { .name = "cardb:/same-name" };
        setGCIInfo(entry);
        assert(deviceHandler_CARD_writeFile(&dest, blocks, sizeof(blocks)) < 0);
        setGCIInfo(NULL);
        assert(writes == 0 && deletes == 0); verifySurvivor(1, 'E');
        handle(&dest, 1, 0);
        ((card_dir *)dest.other)->gamecode[position == 0 ? 0 : 3] =
            position == 0 ? 0xff : 'E';
        if(position == 4) ((card_dir *)dest.other)->company[0] = 0xff;
        assert(deviceHandler_CARD_deleteFile(&dest) < 0);
        u32 length = 0; assert(saveRead(&dest, &length) == NULL);
        assert(writes == 0 && deletes == 0); verifySurvivor(1, 'E');
    }
}
/* Detail's load before a launch: the card's own copy of the game's save
 * ('O') gives way to the chosen one ('N'), and comes back if that fails. */
static void replaceCases(void) {
    for(int failure = 0; failure < 5; failure++) {
        reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
        file_handle own; handle(&own, 1, 1);
        u32 ownLength = 0; u8 *ownData = saveRead(&own, &ownLength);
        assert(ownData != NULL && ownLength == 8256);
        ownData[0x38] = 0; ownData[0x39] = 1;
        u8 entry[64], blocks[8192]; char why[256] = "";
        memcpy(entry, ownData, 64); memset(blocks, 'N', sizeof(blocks));
        bool none = failure == 4;  /* the card had no copy of its own */
        if(none) assert(deviceHandler_CARD_deleteFile(&own) == 0);
        failWritesLeft = failure == 1 ? 1 : failure == 3 ? 2 : 0;
        failReadBacksLeft = failure == 2 ? 1 : 0;
        failReadBacksSkip = none ? 0 : 1;
        int before = deletes;
        bool ok = cardReplace(1, entry, blocks, sizeof(blocks),
            none ? NULL : ownData, ownLength, why, sizeof(why));
        assert(ok == (failure == 0 || failure == 4));
        verifySurvivor(1, 'E');
        int at = saveOn(1, "GZLE");
        u8 expect = ok ? 'N' : 'O';
        if(failure == 3) {
            /* Neither went on: the card's own copy is in the Save Folder. */
            assert(at < 0 && strstr(why, "Save Folder") != NULL);
        } else {
            assert(at >= 0);
            for(int i = 0; i < 8192; i++) assert(cards[1][at].bytes[i] == expect);
            assert(!memcmp(cards[1][at].dir.company, "01", 2));
            if(!ok) assert(strstr(why, "went back") != NULL);
        }
        /* The own copy is removed once, and a failed new one once more. */
        assert(deletes - before == (none ? 0 : 1) + (ok ? 0 : failure == 3 ? 2 : 1));
        free(ownData);
    }
    /* The card was swapped after its own copy went to the Save Folder: the
     * save there now isn't that copy, so nothing on the card is touched. */
    reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
    file_handle own; handle(&own, 1, 1);
    u32 ownLength = 0; u8 *ownData = saveRead(&own, &ownLength);
    assert(ownData != NULL && ownLength == 8256);
    u8 entry[64], blocks[8192]; char why[256] = "";
    memcpy(entry, ownData, 64); entry[0x38] = 0; entry[0x39] = 1;
    memset(blocks, 'N', sizeof(blocks));
    memset(cards[1][1].bytes, 'S', 8192);
    int before = deletes, wrote = writes;
    assert(!cardReplace(1, entry, blocks, sizeof(blocks), ownData, ownLength, why, sizeof(why)));
    assert(strstr(why, "changed") != NULL && deletes == before && writes == wrote);
    int at = saveOn(1, "GZLE");
    assert(at >= 0 && cards[1][at].bytes[0] == 'S' && cards[1][at].bytes[8191] == 'S');
    verifySurvivor(1, 'E');
    free(ownData);
}
static void replaceEmptyCases(void) {
    /* 1. Boundary injection on first write: card swapped to EMPTY card */
    reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
    file_handle own; handle(&own, 1, 1);
    u32 ownLength = 0; u8 *ownData = saveRead(&own, &ownLength);
    assert(ownData != NULL && ownLength == 8256);
    ownData[0x38] = 0; ownData[0x39] = 1;
    u8 entry[64], blocks[8192]; char why[256] = "";
    memcpy(entry, ownData, 64); memset(blocks, 'N', sizeof(blocks));
    changeToEmptyOnWrite = true; failWritesLeft = 1;
    int writesBefore = writes;
    assert(!cardReplace(1, entry, blocks, sizeof(blocks), ownData, ownLength, why, sizeof(why)));
    assert(writes - writesBefore == 1);
    assert(saveOn(1, "GZLE") < 0);
    assert(strstr(why, "Save Folder") != NULL);
    assert(strstr(why, "went back") == NULL);
    free(ownData);

    /* 2. Boundary injection on readback: card swapped to EMPTY card */
    reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
    handle(&own, 1, 1);
    ownLength = 0; ownData = saveRead(&own, &ownLength);
    assert(ownData != NULL && ownLength == 8256);
    ownData[0x38] = 0; ownData[0x39] = 1;
    memcpy(entry, ownData, 64); memset(blocks, 'N', sizeof(blocks));
    changeToEmptyOnRead = true; changeToEmptyReadsSkip = 1; failWritesLeft = 0;
    writesBefore = writes;
    assert(!cardReplace(1, entry, blocks, sizeof(blocks), ownData, ownLength, why, sizeof(why)));
    assert(writes - writesBefore == 1);
    assert(saveOn(1, "GZLE") < 0);
    assert(strstr(why, "Save Folder") != NULL);
    assert(strstr(why, "went back") == NULL);
    free(ownData);

    /* 3. Card remounted to EMPTY during device init before first write */
    reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
    handle(&own, 1, 1);
    ownLength = 0; ownData = saveRead(&own, &ownLength);
    assert(ownData != NULL && ownLength == 8256);
    ownData[0x38] = 0; ownData[0x39] = 1;
    memcpy(entry, ownData, 64); memset(blocks, 'N', sizeof(blocks));
    armChangeOnDelete = true;
    writesBefore = writes;
    assert(!cardReplace(1, entry, blocks, sizeof(blocks), ownData, ownLength, why, sizeof(why)));
    assert(writes == writesBefore);
    assert(saveOn(1, "GZLE") < 0);
    assert(strstr(why, "Save Folder") != NULL);
    assert(strstr(why, "went back") == NULL);
    free(ownData);

    /* 4. Card remounted to EMPTY on initial device init before own deletion */
    reset(); add(1, 0, "GALE", "01", 'E'); add(1, 1, "GZLE", "01", 'O');
    handle(&own, 1, 1);
    ownLength = 0; ownData = saveRead(&own, &ownLength);
    assert(ownData != NULL && ownLength == 8256);
    ownData[0x38] = 0; ownData[0x39] = 1;
    memcpy(entry, ownData, 64); memset(blocks, 'N', sizeof(blocks));
    changeToEmptyOnInit = true;
    writesBefore = writes;
    assert(!cardReplace(1, entry, blocks, sizeof(blocks), ownData, ownLength, why, sizeof(why)));
    assert(writes == writesBefore);
    assert(saveOn(1, "GZLE") < 0);
    assert(strstr(why, "changed") != NULL);
    assert(strstr(why, "nothing was replaced") != NULL);
    assert(strstr(why, "went back") == NULL);
    free(ownData);

    /* 5. cardWriteBound directly rejects mismatched expected generation */
    reset();
    u32 expected = 0;
    mountCount[1] = 1;
    writesBefore = writes;
    assert(!cardWriteBound(1, entry, blocks, sizeof(blocks), &expected, why, sizeof(why)));
    assert(writes == writesBefore);
    assert(strstr(why, "changed") != NULL);
}
static u8 *loadFile(const char *path, u32 *size) {
    FILE *f = fopen(path, "rb"); long length; u8 *data;
    assert(f && fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) > 0);
    assert(fseek(f, 0, SEEK_SET) == 0); *size = (u32)length;
    data = malloc(*size); assert(data && fread(data, 1u, *size, f) == *size);
    assert(fclose(f) == 0); return data;
}
/* Detail's load of a copy in the Source's swiss/saves, onto the card in
 * Slot B: the test card image's one-block save (ZRVE, ZZ, "Other Save") and
 * a .gci of it. */
static void sourceCopyCases(const char *imagePath, const char *gciPath) {
    const char *imageName = "sdc:/swiss/saves/MemoryCardA.USA.raw";
    const char *gciName = "sdc:/swiss/saves/other.gci";
    u32 imageSize, gciSize; char why[256];
    u8 *image = loadFile(imagePath, &imageSize), *gci = loadFile(gciPath, &gciSize);
    savesCopy_t copy;
    assert(gciSize == 64 + 8192);

    /* From the image, read through saves_raw.c's own readers, over the
     * card's own copy, which goes to the settings device's Save Folder
     * first; nothing on the Source changes. */
    reset(); add(1, 0, "GALE", "01", 'E'); addNamed(1, 1, "ZRVE", "ZZ", "Other Save", 'O');
    sdPut(1, imageName, image, imageSize); sdPut(1, gciName, gci, gciSize);
    memset(&copy, 0, sizeof(copy));
    copy.source = SAVES_COPY_IMAGE; copy.device = &sourceSd; copy.ordinal = 1u;
    snprintf(copy.path, sizeof(copy.path), "%s", imageName); copy.size = imageSize;
    memcpy(copy.entry, gci, 64);
    assert(Saves_LoadCopy(1, &copy, why, sizeof(why)));
    int at = saveOn(1, "ZRVE");
    assert(at >= 0 && !memcmp(cards[1][at].bytes, gci + 64, 8192)); verifySurvivor(1, 'E');
    assert(folderEnsures == 1 && sdCards[0].count == 1 && sdCards[0].writes == 1);
    sdFile *kept = &sdCards[0].file[0];
    assert(!strncmp(kept->name, "gcldr:/swiss/saves/", 19) && kept->size == 8256 &&
           !memcmp(kept->data, "ZRVEZZ", 6));
    for(int i = 64; i < 8256; i++) assert(kept->data[i] == 'O');
    assert(sdCards[1].count == 2 && sdCards[1].writes == 0 && sdCards[1].deletes == 0);

    /* The .gci beside it, onto a card without a copy of its own: nothing to
     * keep, nothing written on either SD card. */
    reset(); add(1, 0, "GALE", "01", 'E');
    sdPut(1, imageName, image, imageSize); sdPut(1, gciName, gci, gciSize);
    copy.source = SAVES_COPY_FILE; copy.ordinal = 0u;
    snprintf(copy.path, sizeof(copy.path), "%s", gciName); copy.size = gciSize;
    assert(Saves_LoadCopy(1, &copy, why, sizeof(why)));
    at = saveOn(1, "ZRVE");
    assert(at >= 0 && !memcmp(cards[1][at].bytes, gci + 64, 8192));
    assert(sdCards[0].count == 0 && sdCards[0].writes == 0 && sdCards[1].writes == 0);

    /* A copy is read from the device the scan found it on, whatever its
     * path says: the settings device has no such file, so nothing changes. */
    reset(); add(1, 0, "GALE", "01", 'E'); addNamed(1, 1, "ZRVE", "ZZ", "Other Save", 'O');
    sdPut(1, gciName, gci, gciSize); copy.device = &settingsSd;
    assert(!Saves_LoadCopy(1, &copy, why, sizeof(why)) && strstr(why, "couldn't be read"));
    assert(writes == 0 && deletes == 0 && cards[1][1].bytes[0] == 'O' && sdCards[0].writes == 0);

    /* Without the settings device the card's own copy has nowhere to go,
     * so a copy on the Source isn't read or loaded either. */
    reset(); add(1, 0, "GALE", "01", 'E'); addNamed(1, 1, "ZRVE", "ZZ", "Other Save", 'O');
    sdPut(1, gciName, gci, gciSize); copy.device = &sourceSd; configOk = false;
    assert(!Saves_LoadCopy(1, &copy, why, sizeof(why)) && strstr(why, "no SD card"));
    assert(writes == 0 && deletes == 0 && cards[1][1].bytes[0] == 'O');
    /* Detail's line for a copy: the game's own second comment line, read
     * where the copy is, its entry and comment and nothing more. */
    char line[33];
    reset(); sdPut(1, imageName, image, imageSize); sdPut(1, gciName, gci, gciSize);
    memset(&copy, 0, sizeof(copy));
    copy.source = SAVES_COPY_IMAGE; copy.device = &sourceSd; copy.size = imageSize;
    snprintf(copy.path, sizeof(copy.path), "%s", imageName);
    memcpy(copy.entry, "GACZ01", 6); strcpy((char *)copy.entry + 8, "Demo Save");
    assert(Saves_CopyLine(&copy, line, sizeof(line)) && !strcmp(line, "SD card image"));
    copy.ordinal = 1u; memcpy(copy.entry, gci, 64);
    assert(Saves_CopyLine(&copy, line, sizeof(line)) && !strcmp(line, "Synthetic save"));
    /* Not the save the scan saw there: no line. */
    copy.ordinal = 0u;
    assert(!Saves_CopyLine(&copy, line, sizeof(line)) && line[0] == '\0');
    copy.source = SAVES_COPY_FILE; copy.size = gciSize;
    snprintf(copy.path, sizeof(copy.path), "%s", gciName);
    assert(Saves_CopyLine(&copy, line, sizeof(line)) && !strcmp(line, "Synthetic save"));
    /* From the device the scan found it on, whatever its path says. */
    copy.device = &settingsSd;
    assert(!Saves_CopyLine(&copy, line, sizeof(line)) && line[0] == '\0');
    assert(sdCards[0].writes == 0 && sdCards[1].writes == 0 && sdCards[1].deletes == 0);
    /* A card's copy, through the card driver: the comment where its entry
     * says, here at the start of the save. */
    reset(); add(1, 0, "GALE", "01", 'E'); addNamed(1, 1, "ZRVE", "ZZ", "Other Save", 'O');
    memcpy(cards[1][1].bytes + 32, "Last saved on 1/6", 18);
    memset(&copy, 0, sizeof(copy)); copy.source = SAVES_COPY_SLOT_B;
    memcpy(copy.entry, "ZRVEZZ", 6); strcpy((char *)copy.entry + 8, "Other Save");
    assert(Saves_CopyLine(&copy, line, sizeof(line)) && !strcmp(line, "Last saved on 1/6"));
    assert(writes == 0 && deletes == 0);
    /* Gone from the card since: none. */
    strcpy((char *)copy.entry + 8, "Gone");
    assert(!Saves_CopyLine(&copy, line, sizeof(line)) && line[0] == '\0');
    assert(!Saves_CopyLine(NULL, line, sizeof(line)) && line[0] == '\0');
    reset(); free(image); free(gci);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    replaceEmptyCases();
    replaceCases();
    readErase("GALP", "01"); readErase("GALE", "02");
    readErase("\0ABC", "\0D");
    copyCases("GALP", "01"); copyCases("GALE", "02");
    copyCases("\0ABC", "\0D"); wildcardCases(); changedCardCases();
    sourceCopyCases(argv[1], argv[2]);
    puts("card identity: reads, Erase, Copy, Move, both rollback paths, a card changed under them, wildcards and Detail's load and lines, from the Source too PASS");
    return 0;
}
"""


def harness(driver=None, saves=None):
    driver = DRIVER.read_text() if driver is None else driver
    saves = SAVES.read_text() if saves is None else saves
    raw = (SAVES.parent / "saves_raw.c").read_text()
    header = (DRIVER.parent / "deviceHandler-CARD.h").read_text()
    gci = header[header.index("typedef struct {"):header.index(" GCI;") + 5]
    prelude = PRELUDE
    raw_reader = "static bool readSaveAt(" in saves
    if raw_reader:
        prelude = prelude.replace("static struct { bool mounted; } places[2];", r"""
#include "ui_saves_raw.h"
#define SAVES_TABS 4
#define SAVES_TAB_FOLDER 2
typedef struct {
    bool mounted, rawOpen;
    file_handle rawImage, *entries;
    uiSavesRawCard_t *rawCard;
    int entryCount;
} savesPlace_t;
static savesPlace_t places[SAVES_TABS];
""")
    pieces = [prelude, gci, "static GCI *gciInfo;"]
    if "static bool CARD_SetEntryIdentity(" in driver:
        pieces.append(extract_function(driver, "static bool CARD_SetEntryIdentity("))
    for marker in ("void setGCIInfo(", "void setCopyGCIMode(",
                   "s32 deviceHandler_CARD_readFile(", "s32 deviceHandler_CARD_writeFile(",
                   "s32 deviceHandler_CARD_deleteFile("):
        pieces.append(extract_function(driver, marker))
    if raw_reader:
        # saves_raw.c's own readers: Detail's load reads a card image with them.
        for marker in ("static bool imageReader(", "static bool canRead(",
                       "uiSavesRawStatus_t SavesRaw_Load(", "bool SavesRaw_ReadGci("):
            pieces.append(extract_function(raw, marker))
        for marker in ("static savesPlace_t *rawSource(", "static bool readSaveAt("):
            pieces.append(extract_function(saves, marker))
    for marker in ("static u8 *saveRead(", "static file_handle *cardFind(",
                   "static const char *cardWhy(", "static bool cardWriteBound(",
                   "static bool cardWrite(",
                   "static bool cardReplace(", "static bool folderWrite(",
                   "static u8 *copyRead(", "bool Saves_CopyLine(", "bool Saves_LoadCopy("):
        pieces.append(extract_function(saves, marker))
    return "\n".join(pieces + [MAIN])


def main():
    global ROOT, DRIVER, SAVES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="Review a separate source worktree without editing it")
    args = parser.parse_args()
    ROOT = args.source_root.resolve()
    DRIVER = ROOT / "cube/swiss/source/devices/memcard/deviceHandler-CARD.c"
    SAVES = ROOT / "cube/swiss/source/gui/saves.c"
    spec = importlib.util.spec_from_file_location(
        "card_io_fixture", ROOT / "buildtools/ui/qa/make_test_saves.py")
    fixture = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = fixture
    spec.loader.exec_module(fixture)
    image, (_, other) = fixture.virtual_card()
    with tempfile.TemporaryDirectory(prefix="indigo-card-io-") as temp:
        source = Path(temp) / "card_io.c"
        binary = Path(temp) / "card_io"
        (Path(temp) / "card.raw").write_bytes(image)
        (Path(temp) / "other.gci").write_bytes(other)
        source.write_text(harness())
        flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare"]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                flags += ["-fno-pie", "-no-pie"]
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + flags +
                       ["-I", str(ROOT / "cube/swiss/source/gui"), str(source),
                        str(ROOT / "cube/swiss/source/gui/ui_saves.c"), str(ROOT / "cube/swiss/source/gui/ui_lang.c"),
                        str(ROOT / "cube/swiss/source/gui/ui_saves_metadata.c"),
                        str(ROOT / "cube/swiss/source/gui/ui_saves_raw.c"), "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary), str(Path(temp) / "card.raw"),
                        str(Path(temp) / "other.gci")], check=True)


if __name__ == "__main__":
    main()
