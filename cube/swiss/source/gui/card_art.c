/* card_art.c - posters made on the console from a card's picture or name.

   See card_art.h. ui_apps' art slots decide which posters are kept, ui_png
   makes them, and this file reads the pictures and runs the thread that
   makes them, for Apps (apps.c) and for the Library's folders (swiss.c). */

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include <ogc/lwp_watchdog.h>
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "ui_apps.h"
#include "ui_png.h"
#include "util.h"
#include "card_art.h"
#include "ui_540p.h"

static cardArtSource_t source;
static bool artOpen;
static uiAppsArt_t art;
static u8 *artTexels;
static GXTexObj artTexture[UI_APPS_ART_SLOTS];
static u32 artReadyMs[UI_APPS_ART_SLOTS];	/* when each was made */
static volatile bool posterStop;

static u32 nowMs(void)
{
	return (u32)ticks_to_millisecs(SYS_GetSystemTime());
}

/* The card's picture, read whole: its bytes in *data (the caller frees
 * them) and their count in *size. False, *data NULL, when it has none or it
 * can't be read. A picture over UI_PNG_MAX_FILE is never read, and one too
 * big or unreadable is reported to the source, which doesn't offer it again. */
static bool preparePicture(int32_t card, u8 **data, u32 *size)
{
	u32 bytes = source.pictureSize(card);

	*data = NULL;
	*size = 0u;
	if(bytes == 0u) {
		return false;
	}
	if(bytes > UI_PNG_MAX_FILE || (*data = malloc(bytes)) == NULL) {
		source.verdict(card, false);
		return false;
	}
	*size = bytes;
	return true;
}

/* Thread-safe devices keep their whole-picture read on the worker. */
static void readPicture(int32_t card, u8 **data, u32 *size)
{
	if(!preparePicture(card, data, size)) return;
	if(!source.readPicture(card, 0u, *data, *size)) {
		source.verdict(card, false);
		free(*data);
		*data = NULL;
	}
}

/* The IPL font, as a poster of a name draws it. */
static bool artGlyph(void *context, unsigned char c,
	uint8_t coverage[UI_PNG_GLYPH_MAX * UI_PNG_GLYPH_MAX], int *width)
{
	(void)context;
	return fontGlyph(c, coverage, UI_PNG_GLYPH_MAX, UI_PNG_GLYPH_MAX, width);
}

/* Posters are made on a thread of their own, below the menus' priority. A
 * poster keeps the console busy for a good part of a second (a PNG's
 * decoding, then 1364 blocks of CMPR, while the video thread draws), and
 * made on the menu thread it held up every button that long. The menu
 * thread picks the next slot; the poster thread reads its picture, makes
 * the poster, and the slot takes it if it still waits for that card. One at
 * a time. A device that isn't thread-safe (the disc drive) is read by the
 * menu thread instead, as the file list's banners are. */
typedef struct {
	int slot;
	int32_t card;
	u8 *data;	/* the picture's bytes, or NULL: its name */
	u32 size;
	u32 readBytes;
	bool read;	/* the poster thread reads the picture */
	bool ok;
} posterJob_t;

#define POSTER_STACK_SIZE (32 * 1024)
#define POSTER_PRIORITY (LWP_PRIO_NORMAL - 1)	/* as the file list's banners */
/* How deep the poster thread's stack went, for Dolphin, which doesn't stop
 * at an overrun as a console does (libogc2 guards a stack's lowest word):
 * the thread fills what its stack has below it when it starts, and reports
 * what was used when it ends. The fill starts STACK_SLACK above where the
 * stack can begin at the lowest, so it never writes outside it. */
#define STACK_FILL 0xA5u
#define STACK_SLACK 1024u

static lwp_t posterThread = LWP_THREAD_NULL;
static sem_t posterStart;
static posterJob_t posterJob;
/* Set by the menu thread as it hands posterJob over, cleared by the poster
 * thread once the slot has it. */
static volatile bool posterBusy;
static bool posterReading;	/* main thread owns an unfinished file read */

typedef struct {
	const int32_t *cards;
	u32 count;
} artWant_t;

static void wantPosters(void *context)
{
	artWant_t *want = context;

	UIAppsArt_Want(&art, want->cards, want->count, nowMs());
}

static void nextPoster(void *context)
{
	posterJob_t *job = context;

	job->slot = UIAppsArt_Next(&art, nowMs());
	job->card = job->slot >= 0 ? art.slots[job->slot].app : -1;
}

static void pictureStillWanted(void *context)
{
	bool *wanted = context;
	const uiAppsArtSlot_t *slot = &art.slots[posterJob.slot];

	*wanted = slot->app == posterJob.card && slot->state == UI_APPS_ART_WANTED;
}

static void posterDone(void *context)
{
	posterJob_t *job = context;

	if(UIAppsArt_Done(&art, job->slot, job->card, job->ok) && job->ok) {
		/* As the poster cache binds a pack record: CMPR, five levels. */
		GX_InitTexObj(&artTexture[job->slot], artTexels +
			(size_t)job->slot * UI_PNG_POSTER_BYTES, UI_PNG_CANVAS,
			UI_PNG_CANVAS, GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_TRUE);
#if UI_EXPERIMENTAL_540P
		if(UI540_Active()) {
			GX_InitTexObjLOD(&artTexture[job->slot], GX_LIN_MIP_LIN, GX_LINEAR,
				0.0f, 4.0f, -0.35f, GX_TRUE, GX_TRUE, GX_ANISO_4);
		} else
#endif
		{
			GX_InitTexObjLOD(&artTexture[job->slot], GX_LIN_MIP_LIN, GX_LINEAR,
				0.0f, 4.0f, 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_1);
		}
		artReadyMs[job->slot] = nowMs();
	}
}

static void forgetPosters(void *context)
{
	(void)context;
	UIAppsArt_Init(&art, nowMs());
}

/* Fills the stack below the caller's frame, down to STACK_SLACK above the
 * lowest the stack can start; returns where the fill begins. */
static volatile u8 *fillStack(void)
{
	volatile u8 here = 0u;
	uintptr_t low = (uintptr_t)&here - POSTER_STACK_SIZE + STACK_SLACK;
	uintptr_t p;

	for(p = low; p < (uintptr_t)&here - 256u; ++p) {
		*(volatile u8 *)p = STACK_FILL;
	}
	return (volatile u8 *)low;
}

/* What the thread used of the stack fillStack filled, on a development
 * console's debug output. */
static void reportStack(volatile u8 *low)
{
	size_t untouched = 0u;

	while(untouched < POSTER_STACK_SIZE - STACK_SLACK &&
		low[untouched] == STACK_FILL) {
		untouched++;
	}
	print_debug("card_art: poster stack %u of %u bytes used%s\n",
		(unsigned)(POSTER_STACK_SIZE - untouched), (unsigned)POSTER_STACK_SIZE,
		untouched == 0u ? " (overrun)" : "");
}

/* The poster thread: each job's poster, from its picture, or when it has
 * none or it can't be used, from its name. */
static void *posterMain(void *unused)
{
	uiPngFont_t font = {fontCellHeight(), artGlyph, NULL};
	volatile u8 *stackLow = fillStack();

	(void)unused;
	for(;;) {
		posterJob_t *job = &posterJob;
		u8 *out;

		LWP_SemWait(posterStart);
		if(posterStop) {
			break;
		}
		out = artTexels + (size_t)job->slot * UI_PNG_POSTER_BYTES;
		if(job->read) {
			(void)readPicture(job->card, &job->data, &job->size);
		}
		/* CardArt_Pause sets posterStop and waits: the poster in hand stops
		 * between rows and blocks, so leaving or starting a game never
		 * waits for a big picture. A poster stopped so is no verdict on the
		 * picture, and its slot keeps waiting for it. */
		job->ok = false;
		if(job->data != NULL) {
			job->ok = UIPng_PosterUntil(job->data, job->size, out, &posterStop);
			if(!posterStop) {
				source.verdict(job->card, job->ok);
			}
			free(job->data);
			job->data = NULL;
		}
		if(!job->ok) {
			job->ok = UIPng_NamePosterUntil(source.name(job->card), &font, out,
				&posterStop);
		}
		if(posterStop) {
			break;
		}
		if(job->ok) {
			DCFlushRange(out, UI_PNG_POSTER_BYTES);
		}
		DrawWithVideoLocked(posterDone, job);
		posterBusy = false;
	}
	reportStack(stackLow);
	return NULL;
}

static void startPosters(void)
{
	if(posterThread != LWP_THREAD_NULL) {
		return;	/* already making posters */
	}
	posterStop = false;
	posterBusy = false;
	posterReading = false;
	if(artTexels == NULL || LWP_SemInit(&posterStart, 0, 1) < 0) {
		return;
	}
	if(LWP_CreateThread(&posterThread, posterMain, NULL, NULL,
		POSTER_STACK_SIZE, POSTER_PRIORITY) < 0) {
		posterThread = LWP_THREAD_NULL;
		LWP_SemDestroy(posterStart);
	}
}

/* Stops the poster thread, and the poster it is making. */
static void stopPosters(void)
{
	if(posterThread == LWP_THREAD_NULL) {
		return;
	}
	posterStop = true;
	LWP_SemPost(posterStart);
	LWP_JoinThread(posterThread, NULL);
	posterThread = LWP_THREAD_NULL;
	LWP_SemDestroy(posterStart);
	/* A job handed over as it was told to stop. */
	free(posterJob.data);
	posterJob.data = NULL;
	posterBusy = false;
	posterReading = false;
}

void CardArt_Open(const cardArtSource_t *cards)
{
	if(artOpen) {
		/* Its posters go at once, before cards of the new source can ask
		 * for them; the memory stays, so there's no frame to wait for. */
		stopPosters();
		DrawWithVideoLocked(forgetPosters, NULL);
	}
	else {
		/* Without room for posters every card shows its name instead. */
		artTexels = memalign(32, UI_APPS_ART_SLOTS * UI_PNG_POSTER_BYTES);
		UIAppsArt_Init(&art, nowMs());
		artOpen = true;
	}
	source = *cards;
	startPosters();
}

void CardArt_Want(const int32_t *cards, uint32_t count)
{
	artWant_t want = {cards, count};

	if(artOpen) {
		DrawWithVideoLocked(wantPosters, &want);
	}
}

/* Hands the poster thread the nearest poster still wanted, when it is free. */
void CardArt_Poll(void)
{
	posterJob_t job;

	if(posterThread == LWP_THREAD_NULL || (posterBusy && !posterReading)) {
		return;
	}
	if(!posterBusy) {
		DrawWithVideoLocked(nextPoster, &job);
		if(job.slot < 0) return;
		job.data = NULL;
		job.size = 0u;
		job.readBytes = 0u;
		job.read = source.threadSafe;
		if(!job.read) {
			(void)preparePicture(job.card, &job.data, &job.size);
		}
		job.ok = false;
		posterJob = job;
		posterBusy = true;
		posterReading = !job.read && job.data != NULL;
	}
	if(posterReading) {
		bool wanted;
		DrawWithVideoLocked(pictureStillWanted, &wanted);
		if(!wanted) {
			free(posterJob.data);
			posterJob.data = NULL;
			posterReading = posterBusy = false;
			return;
		}
		u32 take = posterJob.size - posterJob.readBytes;
		if(take > CARD_ART_READ_BYTES) take = CARD_ART_READ_BYTES;
		if(!source.readPicture(posterJob.card, posterJob.readBytes,
			posterJob.data + posterJob.readBytes, take)) {
			source.verdict(posterJob.card, false);
			free(posterJob.data);
			posterJob.data = NULL;
			posterReading = false;
		}
		else {
			posterJob.readBytes += take;
			posterReading = posterJob.readBytes < posterJob.size;
		}
	}
	if(!posterReading) LWP_SemPost(posterStart);
}

void CardArt_Pause(void)
{
	stopPosters();
}

void CardArt_Resume(void)
{
	if(artOpen) {
		startPosters();
	}
}

void CardArt_Close(void)
{
	int i;

	if(!artOpen) {
		return;
	}
	stopPosters();
	/* Nothing draws a poster once the slots are empty; the GPU finishes a
	 * frame that did before the texels go. */
	DrawWithVideoLocked(forgetPosters, NULL);
	for(i = 0; i < 3; ++i) {
		VIDEO_WaitVSync();
	}
	free(artTexels);
	artTexels = NULL;
	artOpen = false;
}

GXTexObj *CardArt_Poster(int32_t card)
{
	int slot = UIAppsArt_Find(&art, card);

	return slot >= 0 ? &artTexture[slot] : NULL;
}

u32 CardArt_PosterAgeMs(int32_t card)
{
	int slot = UIAppsArt_Find(&art, card);

	return slot >= 0 ? nowMs() - artReadyMs[slot] : 0u;
}
