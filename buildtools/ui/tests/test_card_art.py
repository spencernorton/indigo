#!/usr/bin/env python3
"""card_art: the posters made on the console for apps and folders of games.

card_art.c runs here as it does on the console, with ui_png.c and ui_apps.c,
over small stand-ins for libogc (its threads and semaphore as pthreads, GX as
the texture's address, the video lock as a mutex) and the IPL font (a block
font). Every allocation the three make is counted. A driver plays the source
Apps and the Library give it: pictures from files, names, and a log of what
card_art asked for. The tests hold card_art to its guards: a picture over
UI_PNG_MAX_FILE is never read, one that can't be used falls back to the
card's name, the memory it holds is the posters' block, one picture and one
poster's work at most, and all of it comes back.

usage: test_card_art.py [--sanitize]
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from PIL import Image

SANITIZE = "--sanitize" in sys.argv
if SANITIZE:
    sys.argv.remove("--sanitize")
HERE = pathlib.Path(__file__).resolve().parent
GUI = HERE.parents[2] / "cube" / "swiss" / "source" / "gui"
SLOTS_BYTES = 25 * 43648

STUBS = {
    "gccore.h": r"""
#ifndef STUB_GCCORE_H
#define STUB_GCCORE_H
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
typedef struct { void *data; } GXTexObj;
enum { GX_TF_CMPR = 14, GX_CLAMP = 0, GX_FALSE = 0, GX_TRUE = 1,
	GX_LINEAR = 1, GX_LIN_MIP_LIN = 5, GX_ANISO_1 = 0 };
void GX_InitTexObj(GXTexObj *obj, void *img, u16 width, u16 height, u8 format,
	u8 wrapS, u8 wrapT, u8 mipmap);
void GX_InitTexObjLOD(GXTexObj *obj, u8 minFilter, u8 magFilter, float minLod,
	float maxLod, float bias, u8 biasClamp, u8 edgeLod, u8 anisotropy);
void DCFlushRange(void *start, u32 length);
void VIDEO_WaitVSync(void);
#endif
""",
    "ogc/lwp.h": r"""
#include <gccore.h>
typedef u32 lwp_t;
#define LWP_THREAD_NULL 0xffffffffu
#define LWP_PRIO_NORMAL 64
s32 LWP_CreateThread(lwp_t *thread, void *(*entry)(void *), void *arg,
	void *stack, u32 stackSize, u8 priority);
s32 LWP_JoinThread(lwp_t thread, void **value);
""",
    "ogc/semaphore.h": r"""
#include <gccore.h>
#define sem_t ogcSem_t
typedef u32 sem_t;
s32 LWP_SemInit(sem_t *sem, u32 start, u32 max);
s32 LWP_SemWait(sem_t sem);
s32 LWP_SemPost(sem_t sem);
s32 LWP_SemDestroy(sem_t sem);
""",
    "ogc/lwp_watchdog.h": r"""
#include <gccore.h>
u64 gettime(void);
int64_t SYS_GetSystemTime(void);
#define ticks_to_millisecs(ticks) (ticks)
""",
    "FrameBufferMagic.h": r"""
void DrawWithVideoLocked(void (*change)(void *context), void *context);
""",
    "IPLFontWrite.h": r"""
#include <gccore.h>
int fontCellHeight(void);
bool fontGlyph(unsigned char c, u8 *coverage, int stride, int maxWidth, int *width);
""",
    # Built into card_art.c and ui_png.c ahead of everything: their every
    # allocation goes through the driver's counter.
    "count.h": r"""
#include <stdlib.h>
#include <string.h>
void *countMalloc(size_t size);
void *countCalloc(size_t count, size_t size);
void *countMemalign(size_t alignment, size_t size);
void countFree(void *data);
#define malloc countMalloc
#define calloc countCalloc
#define memalign countMemalign
#define free countFree
""",
    "malloc.h": "#include <stdlib.h>\n",
    "util.h": "void print_debug(const char *fmt, ...);\n",
}

DRIVER = r"""
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include <ogc/lwp_watchdog.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "card_art.h"
#include <assert.h>
#include "ui_bnr.h"
#include "ui_png.h"

/* -- the counter ------------------------------------------------------------ */

typedef struct { void *base; size_t size; } countTag_t;
static pthread_mutex_t countLock = PTHREAD_MUTEX_INITIALIZER;
static size_t countLive, countPeak, slotBlocks;
static int failSlots;	/* the posters' block can't be had */
/* The posters' block, filled with SENTINEL when it is had, so a slot's
 * poster shows where card_art wrote one. */
static u8 *slotsBlock;
#define SENTINEL 0xa5u

static void *countAligned(size_t alignment, size_t size)
{
	size_t pad = alignment;
	void *base;
	countTag_t *tag;

	if(failSlots && size == 25u * UI_PNG_POSTER_BYTES) return NULL;
	while(pad < sizeof(countTag_t)) pad += alignment;
	if(posix_memalign(&base, alignment, pad + size) != 0) return NULL;
	tag = (countTag_t *)((char *)base + pad) - 1;
	tag->base = base;
	tag->size = size;
	pthread_mutex_lock(&countLock);
	countLive += size;
	if(countLive > countPeak) countPeak = countLive;
	if(size == 25u * UI_PNG_POSTER_BYTES) slotBlocks++;
	pthread_mutex_unlock(&countLock);
	if(size == 25u * UI_PNG_POSTER_BYTES) {
		slotsBlock = (u8 *)base + pad;
		memset(slotsBlock, SENTINEL, size);
	}
	return (char *)base + pad;
}

void *countMalloc(size_t size) { return countAligned(16u, size); }
void *countMemalign(size_t alignment, size_t size) { return countAligned(alignment, size); }

void *countCalloc(size_t count, size_t size)
{
	void *data;

	if(size != 0u && count > (size_t)-1 / size) return NULL;
	data = countMalloc(count * size);
	if(data != NULL) memset(data, 0, count * size);
	return data;
}

void countFree(void *data)
{
	countTag_t *tag;

	if(data == NULL) return;
	if(data == slotsBlock) slotsBlock = NULL;
	tag = (countTag_t *)data - 1;
	pthread_mutex_lock(&countLock);
	countLive -= tag->size;
	pthread_mutex_unlock(&countLock);
	free(tag->base);
}

/* -- libogc, as pthreads ---------------------------------------------------- */

static pthread_t threads[8];
static u32 threadCount;
static pthread_t menuThread;
static pthread_mutex_t videoLock = PTHREAD_MUTEX_INITIALIZER;

void GX_InitTexObj(GXTexObj *obj, void *img, u16 width, u16 height, u8 format,
	u8 wrapS, u8 wrapT, u8 mipmap)
{
	(void)width; (void)height; (void)format; (void)wrapS; (void)wrapT; (void)mipmap;
	obj->data = img;
}

void GX_InitTexObjLOD(GXTexObj *obj, u8 minFilter, u8 magFilter, float minLod,
	float maxLod, float bias, u8 biasClamp, u8 edgeLod, u8 anisotropy)
{
	(void)obj; (void)minFilter; (void)magFilter; (void)minLod; (void)maxLod;
	(void)bias; (void)biasClamp; (void)edgeLod; (void)anisotropy;
}

void DCFlushRange(void *start, u32 length) { (void)start; (void)length; }

static void sleepMs(long ms)
{
	struct timespec wait = {0, ms * 1000000L};
	while(nanosleep(&wait, &wait) != 0 && errno == EINTR) {
	}
}

void VIDEO_WaitVSync(void) { sleepMs(1); }

u64 gettime(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (u64)now.tv_sec * 1000u + (u64)now.tv_nsec / 1000000u;
}

int64_t SYS_GetSystemTime(void) { return (int64_t)gettime(); }

s32 LWP_CreateThread(lwp_t *thread, void *(*entry)(void *), void *arg,
	void *stack, u32 stackSize, u8 priority)
{
	(void)stack; (void)stackSize; (void)priority;
	if(threadCount == 8u || pthread_create(&threads[threadCount], NULL, entry, arg) != 0)
		return -1;
	*thread = threadCount++;
	return 0;
}

s32 LWP_JoinThread(lwp_t thread, void **value)
{
	return pthread_join(threads[thread], value) == 0 ? 0 : -1;
}

typedef struct { pthread_mutex_t lock; pthread_cond_t posted; u32 count, max; bool used; } hostSem_t;
static hostSem_t sems[8];

s32 LWP_SemInit(sem_t *sem, u32 start, u32 max)
{
	for(u32 i = 0u; i < 8u; ++i) {
		if(!sems[i].used) {
			pthread_mutex_init(&sems[i].lock, NULL);
			pthread_cond_init(&sems[i].posted, NULL);
			sems[i].count = start;
			sems[i].max = max;
			sems[i].used = true;
			*sem = i;
			return 0;
		}
	}
	return -1;
}

s32 LWP_SemWait(sem_t sem)
{
	hostSem_t *s = &sems[sem];
	pthread_mutex_lock(&s->lock);
	while(s->count == 0u) pthread_cond_wait(&s->posted, &s->lock);
	s->count--;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

s32 LWP_SemPost(sem_t sem)
{
	hostSem_t *s = &sems[sem];
	pthread_mutex_lock(&s->lock);
	if(s->count < s->max) s->count++;
	pthread_cond_signal(&s->posted);
	pthread_mutex_unlock(&s->lock);
	return 0;
}

s32 LWP_SemDestroy(sem_t sem)
{
	pthread_cond_destroy(&sems[sem].posted);
	pthread_mutex_destroy(&sems[sem].lock);
	sems[sem].used = false;
	return 0;
}

void DrawWithVideoLocked(void (*change)(void *context), void *context)
{
	pthread_mutex_lock(&videoLock);
	change(context);
	pthread_mutex_unlock(&videoLock);
}

/* Indigo's debug output: printed as "debug ..." lines. */
#include <stdarg.h>
void print_debug(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fputs("debug ", stdout);
	vprintf(fmt, args);
	va_end(args);
}

/* The block font: every letter a block in a 12-pixel cell. */
int fontCellHeight(void) { return 12; }

bool fontGlyph(unsigned char c, u8 *coverage, int stride, int maxWidth, int *width)
{
	int w = c == ' ' ? 5 : 8;
	if(w > maxWidth) return false;
	*width = w;
	for(int y = 1; c != ' ' && y <= 10; ++y)
		for(int x = 1; x < w - 1; ++x)
			coverage[y * stride + x] = 255u;
	return true;
}

/* -- the source ------------------------------------------------------------- */

#define CARDS 8
static char pictures[CARDS][1024];	/* "" without one */
static char names[CARDS][64];
static bool failed[CARDS];	/* not offered again, as Apps and the Library do */
static u8 *picturePoster[CARDS], *namePoster[CARDS];	/* what each should look like */
static int reads[CARDS], readsOffMenu[CARDS], verdicts[CARDS][8], verdictCount[CARDS];
static pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;
static bool failRead[CARDS];	/* its read fails, as a card pulled out would */
/* A held card's read waits until the menu thread starts to pause, so the
 * pause lands with its picture in hand. */
static volatile int holdCard = -1;
static volatile bool holdStarted, pausing;

static uint32_t pictureSize(int32_t card)
{
	struct stat st;

	if(pictures[card][0] == '\0' || failed[card] || stat(pictures[card], &st) != 0)
		return 0u;
	return st.st_size > 0xffffffffLL ? 0xffffffffu : (uint32_t)st.st_size;
}

static unsigned menuReadCalls, menuReadMax, menuReadBytes;
static uint32_t nextOffset[CARDS];

static bool readPicture(int32_t card, uint32_t offset, uint8_t *data, uint32_t size)
{
	FILE *file = fopen(pictures[card], "rb");
	size_t got = 0u;

	pthread_mutex_lock(&logLock);
	if(offset == 0u) {
		reads[card]++;
		nextOffset[card] = 0u;
		if(!pthread_equal(pthread_self(), menuThread)) readsOffMenu[card]++;
	}
	if(pthread_equal(pthread_self(), menuThread)) {
		menuReadCalls++;
		menuReadBytes += size;
		if(size > menuReadMax) menuReadMax = size;
		assert(size <= CARD_ART_READ_BYTES);
	}
	assert(offset == nextOffset[card]);
	nextOffset[card] += size;
	pthread_mutex_unlock(&logLock);
	if(card == holdCard) {
		holdCard = -1;
		holdStarted = true;
		for(int waited = 0; !pausing && waited < 5000; ++waited) sleepMs(1);
		sleepMs(30);	/* the pause's stop is set by now */
	}
	if(failRead[card]) {
		if(file != NULL) fclose(file);
		return false;
	}
	if(file != NULL) {
		if(fseek(file, (long)offset, SEEK_SET) == 0) got = fread(data, 1u, size, file);
		fclose(file);
	}
	return got == size;
}

static const char *cardName(int32_t card) { return names[card]; }

static void verdict(int32_t card, bool ok)
{
	pthread_mutex_lock(&logLock);
	if(verdictCount[card] < 8) verdicts[card][verdictCount[card]++] = ok;
	pthread_mutex_unlock(&logLock);
	failed[card] = !ok;
}

static bool glyph(void *context, unsigned char c,
	uint8_t coverage[UI_PNG_GLYPH_MAX * UI_PNG_GLYPH_MAX], int *width)
{
	(void)context;
	return fontGlyph(c, coverage, UI_PNG_GLYPH_MAX, UI_PNG_GLYPH_MAX, width);
}

/* What card's poster should be, both ways, made before anything is counted
 * (with the real malloc: the counter isn't built into this file). */
static void expect(int card)
{
	uiPngFont_t font = {12, glyph, NULL};
	FILE *file;

	picturePoster[card] = calloc(1u, UI_PNG_POSTER_BYTES);
	namePoster[card] = calloc(1u, UI_PNG_POSTER_BYTES);
	if(!UIPng_NamePoster(names[card], &font, namePoster[card])) {
		free(namePoster[card]);
		namePoster[card] = NULL;
	}
	if(pictures[card][0] != '\0' && (file = fopen(pictures[card], "rb")) != NULL) {
		static u8 bytes[8u << 20];
		size_t size = fread(bytes, 1u, sizeof(bytes), file);
		fclose(file);
		if(size <= UI_PNG_MAX_FILE) {
			if(UIBnr_IsBanner(bytes, size)) {
				u8 rgba[UI_BNR_RGBA_BYTES];
				if(UIBnr_DecodeRgba(bytes, size, rgba) &&
				   UIPng_RgbaPoster(rgba, UI_BNR_WIDTH, UI_BNR_HEIGHT,
					picturePoster[card]))
					return;
			} else if(UIPng_Poster(bytes, size, picturePoster[card])) {
				return;
			}
		}
	}
	free(picturePoster[card]);
	picturePoster[card] = NULL;
}

static const char *posterOf(int card)
{
	GXTexObj *poster;
	const char *what = "none";

	pthread_mutex_lock(&videoLock);
	poster = CardArt_Poster(card);
	if(poster != NULL) {
		what = picturePoster[card] != NULL &&
			memcmp(poster->data, picturePoster[card], UI_PNG_POSTER_BYTES) == 0 ? "picture" :
			namePoster[card] != NULL &&
			memcmp(poster->data, namePoster[card], UI_PNG_POSTER_BYTES) == 0 ? "name" : "other";
	}
	pthread_mutex_unlock(&videoLock);
	return what;
}

/* -- the script ------------------------------------------------------------- *
 *   card I PATH|- NAME   open SAFE   want I...   settle MS   poll
 *   pause   resume   close   failslots   failread I   resetpeak
 *   hold I (its next read waits for the pause)   pollheld MS (polls until
 *   it waits)   written ("written N": the slots card_art has written to)
 *   show I   mem   age I   sleep MS   -- show prints "card I POSTER READS
 *   OFFMENU VERDICTS", mem "mem LIVE PEAK BLOCKS", age "age I MS" (how long
 *   ago its poster was made). */
int main(void)
{
	char line[2048];
	int32_t wanted[CARDS];
	u32 wantedCount = 0u;

	menuThread = pthread_self();
	setvbuf(stdout, NULL, _IOLBF, 0);
	while(fgets(line, sizeof(line), stdin) != NULL) {
		char word[16] = "", path[1024], name[64];
		int card, safe, ms;

		if(sscanf(line, "%15s", word) != 1) continue;
		if(strcmp(word, "card") == 0 &&
			sscanf(line, "card %d %1023s %63[^\n]", &card, path, name) == 3 &&
			card >= 0 && card < CARDS) {
			snprintf(pictures[card], sizeof(pictures[card]), "%s",
				strcmp(path, "-") == 0 ? "" : path);
			snprintf(names[card], sizeof(names[card]), "%s", name);
			expect(card);
		}
		else if(strcmp(word, "open") == 0 && sscanf(line, "open %d", &safe) == 1) {
			cardArtSource_t source = {pictureSize, readPicture, cardName, verdict, safe != 0};
			CardArt_Open(&source);
		}
		else if(strcmp(word, "want") == 0) {
			char *at = line + 4;
			int consumed;
			wantedCount = 0u;
			while(wantedCount < CARDS && sscanf(at, "%d%n", &card, &consumed) == 1) {
				wanted[wantedCount++] = card;
				at += consumed;
			}
			CardArt_Want(wanted, wantedCount);
		}
		else if(strcmp(word, "settle") == 0 && sscanf(line, "settle %d", &ms) == 1) {
			/* Polls as the menu's idle frames do, until every wanted card
			 * has its poster or the time is up. */
			u64 end = gettime() + (u64)ms;
			for(;;) {
				u32 i;
				pthread_mutex_lock(&videoLock);
				for(i = 0u; i < wantedCount && CardArt_Poster(wanted[i]) != NULL; ++i) {
				}
				pthread_mutex_unlock(&videoLock);
				if(i == wantedCount) {
					puts("settled");
					break;
				}
				if(gettime() >= end) {
					puts("timeout");
					break;
				}
				VIDEO_WaitVSync();
				CardArt_Poll();
			}
		}
		else if(strcmp(word, "poll") == 0) CardArt_Poll();
		else if(strcmp(word, "hold") == 0 && sscanf(line, "hold %d", &card) == 1) {
			holdCard = card;
		}
		else if(strcmp(word, "pollheld") == 0 && sscanf(line, "pollheld %d", &ms) == 1) {
			u64 end = gettime() + (u64)ms;
			while(!holdStarted && gettime() < end) {
				VIDEO_WaitVSync();
				CardArt_Poll();
			}
			puts(holdStarted ? "held" : "never held");
		}
		else if(strcmp(word, "failread") == 0 && sscanf(line, "failread %d", &card) == 1) {
			failRead[card] = true;
		}
		else if(strcmp(word, "traffic") == 0) {
			printf("traffic %u %u %u\n", menuReadCalls, menuReadMax, menuReadBytes);
		}
		else if(strcmp(word, "pause") == 0) {
			pausing = true;
			CardArt_Pause();
			pausing = false;
		}
		else if(strcmp(word, "resume") == 0) CardArt_Resume();
		else if(strcmp(word, "close") == 0) CardArt_Close();
		else if(strcmp(word, "failslots") == 0) failSlots = 1;
		else if(strcmp(word, "resetpeak") == 0) {
			pthread_mutex_lock(&countLock);
			countPeak = countLive;
			pthread_mutex_unlock(&countLock);
		}
		else if(strcmp(word, "show") == 0 && sscanf(line, "show %d", &card) == 1) {
			pthread_mutex_lock(&logLock);
			printf("card %d %s %d %d", card, posterOf(card), reads[card], readsOffMenu[card]);
			for(int i = 0; i < verdictCount[card]; ++i) printf(" %d", verdicts[card][i]);
			pthread_mutex_unlock(&logLock);
			putchar('\n');
		}
		else if(strcmp(word, "written") == 0) {
			int written = 0;
			pthread_mutex_lock(&videoLock);
			for(u32 slot = 0u; slotsBlock != NULL && slot < 25u; ++slot) {
				const u8 *texels = slotsBlock + slot * UI_PNG_POSTER_BYTES;
				for(u32 i = 0u; i < UI_PNG_POSTER_BYTES; ++i) {
					if(texels[i] != SENTINEL) {
						written++;
						break;
					}
				}
			}
			pthread_mutex_unlock(&videoLock);
			printf("written %d\n", written);
		}
		else if(strcmp(word, "age") == 0 && sscanf(line, "age %d", &card) == 1) {
			pthread_mutex_lock(&videoLock);
			printf("age %d %u\n", card, (unsigned)CardArt_PosterAgeMs(card));
			pthread_mutex_unlock(&videoLock);
		}
		else if(strcmp(word, "sleep") == 0 && sscanf(line, "sleep %d", &ms) == 1) {
			sleepMs(ms);
		}
		else if(strcmp(word, "mem") == 0) {
			pthread_mutex_lock(&countLock);
			printf("mem %zu %zu %zu\n", countLive, countPeak, slotBlocks);
			pthread_mutex_unlock(&countLock);
		}
		else {
			fprintf(stderr, "bad line: %s", line);
			return 2;
		}
	}
	return 0;
}
"""

CC = os.environ.get("CC", "cc")
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g"]
LINK = []
if SANITIZE:
    FLAGS += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
              "-fno-omit-frame-pointer"]
    if sys.platform.startswith("linux"):
        # As the Makefile, for ASan's shadow: no PIE, compiled and linked.
        FLAGS.append("-fno-pie")
        LINK.append("-no-pie")


def build(work: pathlib.Path) -> pathlib.Path:
    """The driver, with card_art.c copied beside the stand-ins so its own
    "FrameBufferMagic.h" and "IPLFontWrite.h" are theirs."""
    for name, text in STUBS.items():
        (work / name).parent.mkdir(parents=True, exist_ok=True)
        (work / name).write_text(text)
    shutil.copy(GUI / "card_art.c", work / "card_art.c")
    (work / "driver.c").write_text(DRIVER)
    include = ["-I", str(work), "-I", str(GUI)]
    counted = ["-include", str(work / "count.h")]
    objects = []
    for source, extra in ((work / "card_art.c", counted), (GUI / "ui_png.c", counted),
                          (GUI / "ui_bnr.c", counted), (GUI / "ui_apps.c", []),
                          (work / "driver.c", [])):
        target = work / (source.stem + ".o")
        subprocess.run([CC, *FLAGS, *include, *extra, "-c", str(source), "-o", str(target)],
                       check=True)
        objects.append(str(target))
    binary = work / "card_art_driver"
    subprocess.run([CC, *FLAGS, *LINK, *objects, "-o", str(binary), "-lz", "-lm", "-lpthread"],
                   check=True)
    return binary


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(width: int, height: int, colour: tuple, size: int | None = None) -> bytes:
    """A picture in two colours, padded to exactly size bytes with an
    ancillary chunk ui_png skips."""
    image = Image.new("RGB", (width, height), colour)
    image.paste((255 - colour[0], 255 - colour[1], 255 - colour[2]), (0, 0, width // 2, height))
    out = io.BytesIO()
    image.save(out, "PNG")
    data = out.getvalue()
    if size is not None:
        pad = size - len(data) - 12
        assert pad >= 0, (size, len(data))
        data = data[:-12] + chunk(b"paDd", bytes(pad)) + data[-12:]
        assert len(data) == size
    return data


class CardArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.work = pathlib.Path(cls.dir.name)
        cls.binary = build(cls.work)
        text = (GUI / "ui_png.h").read_text()
        cls.max_file = 2 * 1024 * 1024
        cls.max_work = 1536 * 1024
        assert "#define UI_PNG_MAX_FILE (2u * 1024u * 1024u)" in text
        assert "#define UI_PNG_MAX_WORK (1536u * 1024u)" in text
        cls.files = {}
        for name, data in {
            "good": png(300, 400, (200, 40, 40)),
            "exactly the limit": png(300, 400, (40, 200, 40), cls.max_file),
            "a byte over": png(300, 400, (40, 40, 200), cls.max_file + 1),
            "corrupt": png(300, 400, (90, 90, 90))[:-40] + bytes(28),
            "big": png(2048, 2048, (10, 120, 220)),
        }.items():
            path = cls.work / (name.replace(" ", "_") + ".png")
            path.write_bytes(data)
            cls.files[name] = str(path)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_script(self, *lines: str) -> list[str]:
        result = subprocess.run([str(self.binary)], input="\n".join(lines) + "\n",
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr[-3000:])
        return result.stdout.splitlines()

    def cards(self, out: list[str]) -> dict:
        """card -> (poster, reads, reads off the menu thread, verdicts)."""
        found = {}
        for line in out:
            if line.startswith("card "):
                words = line.split()
                found[int(words[1])] = (words[2], int(words[3]), int(words[4]),
                                        [int(v) for v in words[5:]])
        return found

    @staticmethod
    def mems(out: list[str]) -> list[tuple[int, int, int]]:
        return [tuple(map(int, l.split()[1:])) for l in out if l.startswith("mem ")]

    def standard_cards(self) -> list[str]:
        f = self.files
        return [f"card 0 {f['good']} Racing", f"card 1 {f['exactly the limit']} Classics",
                f"card 2 {f['a byte over']} Old saves", f"card 3 {f['corrupt']} Broken",
                "card 4 - No picture", f"card 5 {f['good']} Pulled out", "failread 5"]

    def test_each_card_gets_its_poster_and_the_guards_hold(self):
        out = self.run_script(*self.standard_cards(), "resetpeak", "open 1", "mem",
                              "want 0 1 2 3 4 5", "settle 20000",
                              *[f"show {i}" for i in range(6)], "mem", "close", "mem")
        self.assertIn("settled", out)
        cards = self.cards(out)
        # A picture is read once, off the menu thread, and shown.
        self.assertEqual(cards[0], ("picture", 1, 1, [1]))
        # A file of exactly UI_PNG_MAX_FILE is read and shown; a byte more
        # is never read and the card shows its name.
        self.assertEqual(cards[1], ("picture", 1, 1, [1]))
        self.assertEqual(cards[2], ("name", 0, 0, [0]))
        # A picture that can't be read: its name, and it isn't tried again.
        self.assertEqual(cards[3], ("name", 1, 1, [0]))
        # No picture: its name, nothing read, nothing to judge.
        self.assertEqual(cards[4], ("name", 0, 0, []))
        # A picture the device fails to read: its name, and not tried again.
        self.assertEqual(cards[5], ("name", 1, 1, [0]))
        # Each stop of the poster thread reports its stack (its pthread
        # stand-in has a stack of its own, so this one reads unused).
        self.assertTrue(any(l.startswith("debug card_art: poster stack ") and
                            l.endswith(" of 32768 bytes used") for l in out), out[-5:])
        (open_live, _, blocks), (live, peak, _), (closed, _, _) = self.mems(out)
        # Open holds the posters' block and nothing else ...
        self.assertEqual((open_live, blocks), (SLOTS_BYTES, 1))
        self.assertEqual(live, SLOTS_BYTES)
        # ... and its work never more than one picture and one poster's.
        self.assertLessEqual(peak, SLOTS_BYTES + self.max_file + self.max_work)
        self.assertGreater(peak, SLOTS_BYTES + self.max_file)  # the 2 MB file was read
        self.assertEqual(closed, 0)

    def test_a_poster_knows_how_long_ago_it_was_made(self):
        """The Library fades a poster in over its first moments: its age
        counts from when it was made, and a card without one has none."""
        out = self.run_script(*self.standard_cards(), "open 1", "want 0 4", "settle 20000",
                              "age 0", "age 4", "sleep 300", "age 0", "age 7", "close")
        self.assertIn("settled", out)
        ages = [tuple(map(int, l.split()[1:])) for l in out if l.startswith("age ")]
        (_, first), (_, name), (_, later), (_, none) = ages
        self.assertLess(first, 2000)
        self.assertLess(name, 2000)
        self.assertGreaterEqual(later - first, 300)
        self.assertLess(later - first, 1500)
        self.assertEqual(none, 0)

    def test_a_device_that_isnt_thread_safe_is_read_between_frames(self):
        out = self.run_script(*self.standard_cards(), "open 0", "want 0 1 3", "settle 20000",
                              "show 0", "show 1", "show 3", "close", "mem")
        cards = self.cards(out)
        self.assertEqual(cards[0], ("picture", 1, 0, [1]))
        self.assertEqual(cards[1], ("picture", 1, 0, [1]))
        self.assertEqual(cards[3], ("name", 1, 0, [0]))
        self.assertEqual(self.mems(out)[-1][0], 0)

    def test_pause_stops_the_work_and_keeps_the_posters(self):
        f = self.files
        out = self.run_script(f"card 0 {f['good']} Racing", f"card 1 {f['big']} Big",
                              "open 1", "want 0", "settle 20000",
                              # Pause as the big picture's poster begins.
                              "hold 1", "want 0 1", "pollheld 20000", "pause",
                              "show 0", "show 1", "mem", "written", "poll", "show 1",
                              "resume", "settle 20000", "show 1", "written", "close", "mem")
        self.assertEqual((out.count("settled"), out.count("held")), (2, 1))
        shows = [l.split(" ", 2)[2] for l in out if l.startswith("card ")]
        # Paused: card 0 keeps its poster; the stopped poster made nothing,
        # not even in its slot (ui_png stopped before writing it), is no
        # verdict on its picture and gave its memory back; and a poll does
        # nothing.
        written = [l for l in out if l.startswith("written ")]
        self.assertEqual(shows[0], "picture 1 1 1")
        self.assertEqual(shows[1], "none 1 1")
        self.assertEqual(written[0], "written 1")
        self.assertEqual(self.mems(out)[0][0], SLOTS_BYTES)
        self.assertEqual(shows[2], "none 1 1")
        # Resumed: the picture is read again and its poster made, judged once.
        self.assertEqual(shows[3], "picture 2 2 1")
        self.assertEqual(written[1], "written 2")
        self.assertEqual(self.mems(out)[-1][0], 0)

    def test_partial_menu_read_can_be_paused_or_scrolled_away(self):
        picture = self.files["exactly the limit"]
        out = self.run_script(f"card 0 {picture} Large", "card 1 - Small",
                              "open 0", "want 0", "poll", "traffic", "show 0",
                              "pause", "mem", "resume", "want 1", "settle 20000",
                              "show 1", "traffic", "close", "mem")
        traffic = [tuple(map(int, line.split()[1:])) for line in out if line.startswith("traffic ")]
        self.assertEqual(traffic, [(1, 16384, 16384), (1, 16384, 16384)])
        self.assertIn("card 0 none 1 0", out)
        self.assertEqual(self.cards(out)[1], ("name", 0, 0, []))
        self.assertEqual([row[0] for row in self.mems(out)], [SLOTS_BYTES, 0])

    def test_menu_read_finishes_in_bounded_sequential_ranges(self):
        picture = self.files["exactly the limit"]
        out = self.run_script(f"card 0 {picture} Large", "open 0", "want 0",
                              "settle 20000", "show 0", "traffic", "close", "mem")
        self.assertIn("settled", out)
        self.assertEqual(self.cards(out)[0], ("picture", 1, 0, [1]))
        self.assertIn("traffic 128 16384 2097152", out)
        self.assertEqual(self.mems(out)[-1][0], 0)

    def test_open_again_takes_the_new_cards_in_the_same_memory(self):
        f = self.files
        out = self.run_script(f"card 0 {f['good']} Racing", "card 1 - Classics", "open 1",
                              "want 0 1", "settle 20000", "show 0", "show 1", "mem",
                              # Another listing: the old posters go at once,
                              # the block stays.
                              "open 1", "show 0", "show 1", "mem",
                              "want 1", "settle 20000", "show 1", "close", "mem")
        cards = [l for l in out if l.startswith("card ")]
        self.assertEqual(cards[:2], ["card 0 picture 1 1 1", "card 1 name 0 0"])
        self.assertEqual(cards[2:4], ["card 0 none 1 1 1", "card 1 none 0 0"])
        before, after, closed = self.mems(out)
        self.assertEqual((before[0], before[2]), (SLOTS_BYTES, 1))
        self.assertEqual((after[0], after[2]), (SLOTS_BYTES, 1))
        self.assertEqual(cards[4], "card 1 name 0 0")
        self.assertEqual(closed[0], 0)

    def test_without_memory_for_posters_every_card_shows_its_name(self):
        f = self.files
        out = self.run_script(f"card 0 {f['good']} Racing", "failslots", "open 1",
                              "want 0", "settle 300", "show 0", "mem", "close", "mem")
        self.assertIn("timeout", out)
        # Nothing read, nothing made: the renderer draws the card's name.
        self.assertEqual(self.cards(out)[0], ("none", 0, 0, []))
        self.assertEqual([m[0] for m in self.mems(out)], [0, 0])

    def test_opening_bnr_renders_a_poster_without_leaking(self):
        bnr = bytearray(6496)
        bnr[:4] = b"BNR1"
        for i in range(0, 96 * 32 * 2, 2):
            struct.pack_into(">H", bnr, 32 + i, 0x8000 | (8 << 10) | (6 << 5) | 24)
        bnr_path = self.work / "opening.bnr"
        bnr_path.write_bytes(bytes(bnr))
        out = self.run_script(f"card 0 {bnr_path} Indigo", "open 1", "want 0",
                              "settle 20000", "show 0", "mem", "close", "mem")
        self.assertIn("settled", out)
        self.assertEqual(self.cards(out)[0], ("picture", 1, 1, [1]))
        self.assertEqual([m[0] for m in self.mems(out)], [SLOTS_BYTES, 0])


if __name__ == "__main__":
    unittest.main(verbosity=1)
