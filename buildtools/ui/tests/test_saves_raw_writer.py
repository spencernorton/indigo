#!/usr/bin/env python3
"""Card images change in place: the real writer against a FatFs model.

The writer must open an image as it is (never create, truncate or grow it),
write an added save's blocks, then its block map, then its directory (an
erase: the directory, then the map), each synced before the next, and read
every block back through a fresh read-only open. Stopped at any write, the
image still reads as a card: as it was, or with blocks held and no save in
them. A copy that doesn't read back the same is taken out again.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"


def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <stdlib.h>
#include <strings.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_LOCKED = 16 } FRESULT;
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_NEW 0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS 0x10
#define FA_OPEN_APPEND 0x30
#define IS_FILE 1u
#define FEAT_WRITE 2u
typedef struct { uint32_t fptr, objsize; BYTE mode; bool open; } FFFIL;
#define f_size(fp) ((fp)->objsize)
#define f_tell(fp) ((fp)->fptr)
typedef struct file_handle file_handle;
typedef struct {
	u32 features;
	s32 (*writeFile)(file_handle *, const void *, u32);
	s32 (*closeFile)(file_handle *);
} device_t;
#define DEVICEHANDLER_INTERFACE device_t
struct file_handle { char name[64]; uint32_t size; unsigned fileType; device_t *device; };

static s32 deviceHandler_FAT_writeFile(file_handle *file, const void *data, u32 length)
{
	(void)file; (void)data; (void)length;
	assert(!"the device's writeFile would create the image anew");
	return -1;
}
static s32 otherWriteFile(file_handle *file, const void *data, u32 length)
{
	(void)file; (void)data; (void)length;
	return -1;
}
static unsigned closes;
static s32 closeFile(file_handle *file) { (void)file; ++closes; return 0; }
static void *memalign(size_t alignment, size_t size) { return aligned_alloc(alignment, size); }

/* The card: the image's bytes, and what was done to them, in order. */
static uint8_t disk[IMAGE_SIZE];
static uint8_t original[IMAGE_SIZE];
enum { WRITE = 1, SYNC, OPEN_RW, OPEN_R, CLOSE };
static struct { int kind; uint32_t block; } steps[64];
static unsigned stepCount, writes, opens, failWrite, failSync, syncs;
static bool lockedOpen, held;
static uint32_t corruptBlock;

static void step(int kind, uint32_t block)
{
	assert(stepCount < 64u);
	steps[stepCount].kind = kind;
	steps[stepCount++].block = block;
}

static FRESULT f_open(FFFIL *fp, const char *path, BYTE mode)
{
	assert(!strcmp(path, "sdc:/swiss/saves/MemoryCardA.USA.raw"));
	/* Opened as it is: no creating, truncating or appending, ever. */
	assert((mode & ~(FA_READ | FA_WRITE)) == 0 && (mode & FA_READ));
	assert(!held);
	++opens;
	if(lockedOpen) return FR_LOCKED;
	held = true;
	fp->fptr = 0u;
	fp->objsize = IMAGE_SIZE;
	fp->mode = mode;
	fp->open = true;
	step((mode & FA_WRITE) ? OPEN_RW : OPEN_R, 0u);
	return FR_OK;
}
static FRESULT f_close(FFFIL *fp)
{
	assert(fp->open);
	fp->open = false;
	held = false;
	step(CLOSE, 0u);
	return FR_OK;
}
static FRESULT f_lseek(FFFIL *fp, uint32_t offset)
{
	assert(fp->open);
	/* Past the end in write mode, FatFs would grow the file. */
	assert(offset <= fp->objsize);
	fp->fptr = offset;
	return FR_OK;
}
static FRESULT f_read(FFFIL *fp, void *data, UINT length, UINT *done)
{
	assert(fp->open && fp->fptr + length <= fp->objsize);
	assert(((uintptr_t)data & 31u) == 0u);
	memcpy(data, disk + fp->fptr, length);
	if(corruptBlock != 0u && fp->fptr == corruptBlock * UI_SAVES_BLOCK_SIZE &&
		!(fp->mode & FA_WRITE)) ((uint8_t *)data)[100] ^= 1u;
	fp->fptr += length;
	*done = length;
	return FR_OK;
}
static FRESULT f_write(FFFIL *fp, const void *data, UINT length, UINT *done)
{
	assert(fp->open && (fp->mode & FA_WRITE));
	assert(fp->fptr % UI_SAVES_BLOCK_SIZE == 0u && length == UI_SAVES_BLOCK_SIZE);
	assert(fp->fptr + length <= fp->objsize);
	assert(((uintptr_t)data & 31u) == 0u);
	*done = 0;
	if(++writes == failWrite) {
		/* Cut off partway: the first sector of the block lands. */
		memcpy(disk + fp->fptr, data, 512u);
		return FR_DISK_ERR;
	}
	memcpy(disk + fp->fptr, data, length);
	step(WRITE, fp->fptr / UI_SAVES_BLOCK_SIZE);
	fp->fptr += length;
	*done = length;
	return FR_OK;
}
static FRESULT f_sync(FFFIL *fp)
{
	assert(fp->open && (fp->mode & FA_WRITE));
	if(++syncs == failSync) return FR_DISK_ERR;
	step(SYNC, 0u);
	return FR_OK;
}

/* WRITER */

static device_t sd = {FEAT_WRITE, deviceHandler_FAT_writeFile, closeFile};
static file_handle image;

static void load(void)
{
	fixture();
	for(uint32_t i = 0u; i < IMAGE_SIZE; ++i) disk[i] = (uint8_t)(i * 7u + (i >> 13));
	memcpy(disk, metadata, sizeof(metadata));
	memcpy(original, disk, sizeof(disk));
	memset(&image, 0, sizeof(image));
	strcpy(image.name, "sdc:/swiss/saves/MemoryCardA.USA.raw");
	image.size = IMAGE_SIZE;
	image.fileType = IS_FILE;
	image.device = &sd;
	sd.features = FEAT_WRITE;
	sd.writeFile = deviceHandler_FAT_writeFile;
	stepCount = writes = opens = syncs = closes = 0u;
	failWrite = failSync = 0u;
	corruptBlock = 0u;
	lockedOpen = held = false;
}

static uint8_t *gci(const char *id, const char *name, unsigned blocks, uint32_t *length)
{
	uint8_t *save = calloc(1, UI_SAVES_ENTRY_SIZE + blocks * UI_SAVES_BLOCK_SIZE);
	assert(save != NULL);
	memcpy(save, id, 6u);
	save[6] = 0xffu;
	memcpy(save + 8u, name, strlen(name));
	put16(save + 0x36u, 0xbeefu);
	put16(save + 0x38u, blocks);
	for(unsigned i = 0u; i < blocks * UI_SAVES_BLOCK_SIZE; ++i) {
		save[UI_SAVES_ENTRY_SIZE + i] = (uint8_t)(i / UI_SAVES_BLOCK_SIZE * 31u + i);
	}
	*length = UI_SAVES_ENTRY_SIZE + blocks * UI_SAVES_BLOCK_SIZE;
	return save;
}

static bool readDisk(void *opaque, uint32_t offset, void *destination, uint32_t length)
{
	(void)opaque;
	memcpy(destination, disk + offset, length);
	return true;
}

/* The card on the disk as the console reads it. */
static uiSavesRawStatus_t reads(unsigned *count)
{
	uiSavesRawStatus_t status = UISavesRaw_Parse(disk, UI_SAVES_RAW_METADATA_SIZE,
		IMAGE_SIZE, &card);
	*count = card.count;
	return status;
}

static void expectSteps(const int *kinds, const uint32_t *blocks, unsigned count)
{
	assert(stepCount == count);
	for(unsigned i = 0u; i < count; ++i) {
		assert(steps[i].kind == kinds[i]);
		if(kinds[i] == WRITE) assert(steps[i].block == blocks[i]);
	}
}

static void testAdd(void)
{
	static const int kinds[] = {OPEN_RW, WRITE, WRITE, WRITE, SYNC, WRITE, SYNC,
		WRITE, SYNC, CLOSE, OPEN_R, CLOSE};
	static const uint32_t blocks[] = {0, 10, 11, 12, 0, 3, 0, 1, 0, 0, 0, 0};
	uint32_t length;
	uint8_t *save = gci("NEWS01", "New Save", 3u, &length);
	uint8_t *back = malloc(length);
	unsigned count;

	load();
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_OK);
	expectSteps(kinds, blocks, sizeof(kinds) / sizeof(kinds[0]));
	assert(closes >= 1u && !held);
	/* Only the save's blocks, one directory and one map copy changed. */
	for(unsigned block = 0u; block < IMAGE_SIZE / UI_SAVES_BLOCK_SIZE; ++block) {
		bool changed = memcmp(disk + block * UI_SAVES_BLOCK_SIZE,
			original + block * UI_SAVES_BLOCK_SIZE, UI_SAVES_BLOCK_SIZE) != 0;
		assert(changed == (block == 1u || block == 3u || (block >= 10u && block <= 12u)));
	}
	assert(reads(&count) == UI_SAVES_RAW_OK && count == 3u);
	unsigned at = UISavesRaw_Find(&card, save);
	assert(at < count && UISavesRaw_GciSize(&card, at) == length);
	assert(UISavesRaw_ReadGci(&card, at, 0u, back, length, readDisk, NULL));
	assert(!memcmp(back + UI_SAVES_ENTRY_SIZE, save + UI_SAVES_ENTRY_SIZE,
		length - UI_SAVES_ENTRY_SIZE));
	assert(!memcmp(back, save, 0x36u) && back[0x36] == 0u && back[0x37] == 10u);
	/* The same save again: refused before anything is written. */
	stepCount = writes = 0u;
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_EXISTS);
	assert(writes == 0u && !held);
	/* A length that isn't the entry's blocks: not even opened. */
	opens = 0u;
	assert(SavesRaw_Add(&image, save, length - 1u) == UI_SAVES_RAW_INVALID_ARGUMENT);
	assert(SavesRaw_Add(&image, NULL, length) == UI_SAVES_RAW_INVALID_ARGUMENT);
	assert(opens == 0u);
	free(back);
	free(save);
}

static void testErase(void)
{
	static const int kinds[] = {OPEN_RW, WRITE, SYNC, WRITE, SYNC, CLOSE, OPEN_R, CLOSE};
	static const uint32_t blocks[] = {0, 1, 0, 3, 0, 0, 0, 0};
	uint8_t entry[UI_SAVES_ENTRY_SIZE];
	unsigned count;

	load();
	assert(reads(&count) == UI_SAVES_RAW_OK && count == 2u);
	memcpy(entry, UISavesRaw_Entry(&card, 0u), sizeof(entry));
	assert(SavesRaw_Erase(&image, entry) == UI_SAVES_RAW_OK);
	expectSteps(kinds, blocks, sizeof(kinds) / sizeof(kinds[0]));
	assert(reads(&count) == UI_SAVES_RAW_OK && count == 1u && card.freeBlocks == 58u);
	/* The erased save's bytes stay, as the console leaves them. */
	assert(!memcmp(disk + 5u * UI_SAVES_BLOCK_SIZE, original + 5u * UI_SAVES_BLOCK_SIZE,
		UI_SAVES_BLOCK_SIZE));
	stepCount = writes = 0u;
	assert(SavesRaw_Erase(&image, entry) == UI_SAVES_RAW_NOT_FOUND);
	assert(writes == 0u && !held);
}

/* Stopped at any write or sync: the card still reads, with its saves whole. */
static void testCutOff(void)
{
	uint32_t length;
	uint8_t *save = gci("NEWS01", "New Save", 3u, &length);
	uint8_t entry[UI_SAVES_ENTRY_SIZE];
	unsigned count;

	/* A cut write lands its first sector. A small card's whole map is in
	 * that sector, so a cut map write lands it; a cut directory write
	 * doesn't sum and the other copy stands. */
	for(unsigned cut = 1u; cut <= 5u; ++cut) {
		load();
		failWrite = cut;
		assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_WRITE_ERROR);
		assert(!held && reads(&count) == UI_SAVES_RAW_OK && count == 2u);
		assert(card.freeBlocks == (cut >= 4u ? 53u : 56u));
	}
	for(unsigned cut = 1u; cut <= 3u; ++cut) {
		load();
		failSync = cut;
		assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_WRITE_ERROR);
		assert(!held && reads(&count) == UI_SAVES_RAW_OK && count >= 2u);
	}
	for(unsigned cut = 1u; cut <= 2u; ++cut) {
		load();
		assert(reads(&count) == UI_SAVES_RAW_OK);
		memcpy(entry, UISavesRaw_Entry(&card, 0u), sizeof(entry));
		failWrite = cut;
		assert(SavesRaw_Erase(&image, entry) == UI_SAVES_RAW_WRITE_ERROR);
		assert(!held && reads(&count) == UI_SAVES_RAW_OK);
		/* Cut in the directory: as it was; in the map: gone, freed. */
		assert(count == (cut == 1u ? 2u : 1u) &&
			card.freeBlocks == (cut == 1u ? 56u : 58u));
	}
	free(save);
}

/* A copy that doesn't read back the same is taken out again. */
static void testMismatch(void)
{
	uint32_t length;
	uint8_t *save = gci("NEWS01", "New Save", 3u, &length);
	unsigned count;

	load();
	corruptBlock = 11u;
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_MISMATCH);
	assert(!held && reads(&count) == UI_SAVES_RAW_OK && count == 2u);
	assert(UISavesRaw_Find(&card, save) == count && card.freeBlocks == 56u);
	free(save);
}

static void testRefused(void)
{
	uint32_t length;
	uint8_t *save = gci("NEWS01", "New Save", 1u, &length);
	uint8_t entry[UI_SAVES_ENTRY_SIZE];

	/* Not a FatFs device, or one that doesn't write: never opened. */
	load();
	sd.writeFile = otherWriteFile;
	assert(!SavesRaw_Writable(&image));
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_READ_ONLY);
	assert(SavesRaw_Erase(&image, save) == UI_SAVES_RAW_READ_ONLY);
	assert(opens == 0u);
	load();
	sd.features = 0u;
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_READ_ONLY && opens == 0u);
	load();
	image.fileType = 0u;
	assert(!SavesRaw_Writable(&image));
	/* Not the image the page listed. */
	load();
	image.size = IMAGE_SIZE - UI_SAVES_BLOCK_SIZE;
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_INVALID_SIZE);
	assert(writes == 0u && !held);
	/* Held open elsewhere. */
	load();
	lockedOpen = true;
	assert(SavesRaw_Add(&image, save, length) == UI_SAVES_RAW_WRITE_ERROR);
	assert(writes == 0u);
	load();
	lockedOpen = true;
	memcpy(entry, metadata + 2u * UI_SAVES_BLOCK_SIZE, sizeof(entry));
	assert(SavesRaw_Erase(&image, entry) == UI_SAVES_RAW_WRITE_ERROR && writes == 0u);
	assert(!memcmp(disk, original, sizeof(disk)));
	free(save);
}

int main(void)
{
	testAdd();
	testErase();
	testCutOff();
	testMismatch();
	testRefused();
	puts("raw memory card writer tests passed");
	return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = (GUI / "saves_raw.c").read_text()
    writer = "\n\n".join(extract(source, signature) for signature in (
        "bool SavesRaw_Writable(", "static bool imageAt(", "static bool imageWrite(",
        "static bool imageRead(", "static bool writeSystem(",
        "static uiSavesRawStatus_t imageOpen(", "static bool readBack(",
        "static uiSavesRawStatus_t eraseOpen(", "uiSavesRawStatus_t SavesRaw_Add(",
        "uiSavesRawStatus_t SavesRaw_Erase("))
    fixture = (ROOT / "buildtools/ui/tests/test_ui_saves_raw.c").read_text().split("static void expect(")[0]
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        harness = path / "writer.c"
        harness.write_text(fixture + HARNESS.replace("/* WRITER */", writer))
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-O1", "-I", str(GUI), str(harness), str(GUI / "ui_saves_raw.c"),
                   str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"), "-o", str(path / "writer")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            # Match the Linux host suite: high ASLR entropy can crash PIE ASan before main.
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "writer")], check=True)


if __name__ == "__main__":
    main()
