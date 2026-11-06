#!/usr/bin/env python3
"""The real RAW adapter uses small reads and closes its source on errors."""

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
typedef int32_t s32;
typedef struct file_handle file_handle;
typedef struct {
	int64_t (*seekFile)(file_handle *, int64_t, unsigned);
	s32 (*readFile)(file_handle *, void *, uint32_t);
	s32 (*closeFile)(file_handle *);
} device_t;
struct file_handle { uint32_t offset, size; unsigned fileType; device_t *device; };
#define IS_FILE 1u
#define DEVICE_HANDLER_SEEK_SET 0u
static unsigned reads, closes;
static bool badSeek, shortRead, failAllocation;
static size_t allocated;
static void *memalign(size_t alignment, size_t size)
{
	assert(alignment == 32u &&
		(size == UI_SAVES_RAW_METADATA_SIZE || size == UI_SAVES_BLOCK_SIZE));
	allocated = size;
	return failAllocation ? NULL : aligned_alloc(alignment, size);
}
static int64_t seek(file_handle *file, int64_t where, unsigned origin)
{
	assert(origin == DEVICE_HANDLER_SEEK_SET && where >= 0 && where <= file->size);
	if(badSeek) return -1;
	file->offset = (uint32_t)where;
	return where;
}
static s32 read(file_handle *file, void *destination, uint32_t size)
{
	assert(size <= file->size - file->offset && size <= UI_SAVES_RAW_METADATA_SIZE);
	assert(((uintptr_t)destination & 31u) == 0u);
	++reads;
	if(shortRead) return (s32)size - 1;
	if(file->offset == 0u) {
		assert(size == sizeof(metadata));
		memcpy(destination, metadata, size);
	} else {
		assert(file->offset >= UI_SAVES_RAW_METADATA_SIZE && size <= UI_SAVES_BLOCK_SIZE);
		memset(destination, 0x6b, size);
	}
	file->offset += size;
	return (s32)size;
}
static s32 close(file_handle *file) { (void)file; ++closes; return 0; }
/* ADAPTER */

int main(void)
{
	device_t device = {seek, read, close};
	file_handle image = {0u, IMAGE_SIZE, IS_FILE, &device};
	_Alignas(32) uint8_t output[2u * UI_SAVES_BLOCK_SIZE + 64u];
	fixture();
	assert(SavesRaw_IsImageName("sd:/swiss/saves/MemoryCardA.USA.raw"));
	assert(SavesRaw_IsImageName("backup.RAW"));
	assert(!SavesRaw_IsImageName("._backup.raw"));
	assert(!SavesRaw_IsImageName(NULL));
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_OK);
	assert(allocated == 40960u && reads == 1u && closes == 1u && card.count == 2u);
	assert(SavesRaw_ReadGci(&image, &card, 0u, 0u, output, 128u));
	assert(closes == 2u && reads == 2u);
	assert(!memcmp(output, UISavesRaw_Entry(&card, 0u), UI_SAVES_ENTRY_SIZE));
	assert(output[64] == 0x6bu);
	assert(SavesRaw_ReadGci(&image, &card, 0u, 65u, output + 1u,
		2u * UI_SAVES_BLOCK_SIZE - 1u));
	assert(closes == 3u && reads == 4u && allocated == UI_SAVES_BLOCK_SIZE);
	assert(output[1] == 0x6bu && output[2u * UI_SAVES_BLOCK_SIZE - 1u] == 0x6bu);
	shortRead = true;
	assert(!SavesRaw_ReadGci(&image, &card, 0u, 64u, output + 1u, 1u));
	assert(closes == 4u);
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_READ_ERROR);
	assert(closes == 5u && card.valid == 0u);
	shortRead = false;
	badSeek = true;
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_READ_ERROR);
	assert(closes == 6u && card.valid == 0u);
	badSeek = false;
	failAllocation = true;
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_READ_ERROR);
	assert(closes == 6u && card.valid == 0u);
	failAllocation = false;
	image.size = UINT32_MAX;
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_INVALID_SIZE);
	assert(closes == 6u && card.valid == 0u);
	image.size = 1024u * 1024u + 1u;
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_INVALID_SIZE);
	assert(closes == 6u && card.valid == 0u);
	image.size = IMAGE_SIZE;
	memset(metadata, 0, sizeof(metadata));
	assert(SavesRaw_Load(&image, &card) == UI_SAVES_RAW_UNFORMATTED);
	assert(closes == 7u && card.valid == 0u);
	puts("raw memory card device adapter tests passed");
	return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = (GUI / "saves_raw.c").read_text()
    adapter = "\n\n".join(extract(source, signature) for signature in (
        "bool SavesRaw_IsImageName(", "static bool imageReader(",
        "static bool canRead(", "uiSavesRawStatus_t SavesRaw_Load(",
        "bool SavesRaw_ReadGci("))
    fixture = (ROOT / "buildtools/ui/tests/test_ui_saves_raw.c").read_text().split("static void expect(")[0]
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        harness = path / "adapter.c"
        harness.write_text(fixture + HARNESS.replace("/* ADAPTER */", adapter))
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-O1", "-I", str(GUI), str(harness), str(GUI / "ui_saves_raw.c"),
                   str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"), "-o", str(path / "adapter")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            # Match the Linux host suite: high ASLR entropy can crash PIE ASan before main.
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "adapter")], check=True)


if __name__ == "__main__":
    main()
