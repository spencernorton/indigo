#!/usr/bin/env python3
"""Run the real Library renderer against a checked GX stream, in every layout.

_DrawGameflow and its helpers are compiled out of FrameBufferMagic.c with the
real ring/grid state, window and navigation code. GX, the font and the poster
cache are stand-ins that check every primitive is declared and emitted in full
and log what is drawn. The tests then check:

- Horizontal draws exactly what the base commit's renderer drew, frame for
  frame, through moves, wraps, pages, Detail and every motion mode: only the
  Library's command line and the edits in BASE_EDITS are new.
- Horizontal shows two covers either side of the selected one.
- Vertical and Grid place their cards where the layouts say, show every card
  a user can see with its cover, never draw a card twice at rest, and move
  without a card jumping when the selection changes (a small grid wraps round
  the screen, so a row can leave at one edge while it arrives at the other).
- Spotlight lays the ring out as a row of banners under the selected game's
  gameplay still (its cover, else its banner card, when it has no still), with
  its title, publisher, description and facts in the column beside it.
- Jewel Case draws no cards but the selected game's case, its disc printed
  with the middle of its cover, and two cases while it moves: the one
  leaving and the one arriving. Without the memory for its case it is the
  carousel.
- From every layout the focused card flies to Detail's pose.
"""

from functools import lru_cache
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from test_cheats_gx_stream import extract_function


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"
# The layouts were added on top of this commit, and its renderer is the
# reference the Horizontal layout must reproduce. Move it forward on purpose,
# and only when the carousel's own drawing changes.
BASE = "v1.22.0"
# The carousel's deliberate changes since BASE, made to its tree before it is
# built: (file, text found exactly once, replacement). The second card either
# side became a receding card with its cover (or banner) and settings mark.
BASE_EDITS = (
    ("FrameBufferMagic.c",
     "{{{32.0f, 159.0f}, {52.0f, 151.0f}, {52.0f, 266.0f}, {32.0f, 258.0f}}},",
     "{{{14.0f, 151.0f}, {72.0f, 143.0f}, {72.0f, 273.0f}, {14.0f, 265.0f}}},"),
    ("FrameBufferMagic.c",
     "{{{588.0f, 151.0f}, {608.0f, 159.0f}, {608.0f, 258.0f}, {588.0f, 266.0f}}},",
     "{{{568.0f, 143.0f}, {626.0f, 151.0f}, {626.0f, 265.0f}, {568.0f, 273.0f}}},"),
    ("FrameBufferMagic.c", "if(distance >= 1.5f) {", "if(distance >= 3.0f) {"),
    ("FrameBufferMagic.c", "if(fabsf(cards[i].visualSlot) < 1.5f &&",
     "if(fabsf(cards[i].visualSlot) < 3.0f &&"),
    ("ui_gameflow_library.c", "if(distance >= 1.5f) {", "if(distance >= 3.0f) {"),
    # The titles fade through: out over the first half of a move, then in.
    ("FrameBufferMagic.c", "\t\ttitleTravel * (1.0f - frame->detailProgress), reveal);",
     "\t\t_GameflowClamp(2.0f * titleTravel - 1.0f, 0.0f, 1.0f) *\n"
     "\t\t(1.0f - frame->detailProgress), reveal);"),
    ("FrameBufferMagic.c", "\t\t(1.0f - titleTravel) * (1.0f - frame->detailProgress), reveal);",
     "\t\t_GameflowClamp(1.0f - 2.0f * titleTravel, 0.0f, 1.0f) *\n"
     "\t\t(1.0f - frame->detailProgress), reveal);"),
    # A held stick: the strip may run two cards behind, so the window holds
    # four either side.
    ("ui_gameflow.c", "if(travel < -1.0f) {\n\t\treturn -1.0f;\n\t}\n"
     "\tif(travel > 1.0f) {\n\t\treturn 1.0f;",
     "if(travel < -2.0f) {\n\t\treturn -2.0f;\n\t}\n"
     "\tif(travel > 2.0f) {\n\t\treturn 2.0f;"),
    # A ring smaller than that window keeps within one card.
    ("ui_gameflow.c", "\ttravel = clampCarouselTravel(travel + visualStep);\n",
     "\ttravel = clampCarouselTravel(travel + visualStep);\n"
     "\tif(state->itemCount < 9u) {\n"
     "\t\ttravel = travel < -1.0f ? -1.0f : (travel > 1.0f ? 1.0f : travel);\n\t}\n"),
    ("ui_gameflow_library.c", "0, -1, 1, -2, 2, -3, 3\n", "0, -1, 1, -2, 2, -3, 3, -4, 4\n"),
    ("ui_gameflow_library.h", "#define UI_GAMEFLOW_LIBRARY_WINDOW 7u",
     "#define UI_GAMEFLOW_LIBRARY_WINDOW 9u"),
    ("FrameBufferMagic.h", "#define UI_GAMEFLOW_RENDER_SLOTS 7u",
     "#define UI_GAMEFLOW_RENDER_SLOTS 9u"),
    ("FrameBufferMagic.c", "record->relativeSlot < -3 || record->relativeSlot > 3) {",
     "record->relativeSlot < -4 || record->relativeSlot > 4) {"),
    # A moving card goes a whole pixel of its farthest-moving corner at a
    # time, so each edge moves one way and its size changes one way.
    ("FrameBufferMagic.c", "\tfloat progress = clamped - (float)lower;\n\tint i;\n",
     "\tfloat progress = clamped - (float)lower;\n\tint i;\n\tfloat travel = 0.0f;\n"
     "\tfor(i = 0; i < 4; ++i) {\n"
     "\t\ttravel = fmaxf(travel, fabsf(gameflowSlotPoses[upper + 3].point[i].x -\n"
     "\t\t\tgameflowSlotPoses[lower + 3].point[i].x));\n"
     "\t\ttravel = fmaxf(travel, fabsf(gameflowSlotPoses[upper + 3].point[i].y -\n"
     "\t\t\tgameflowSlotPoses[lower + 3].point[i].y));\n\t}\n"
     "\tif(travel > 0.0f) {\n\t\tprogress = _GameflowRound(progress * travel) / travel;\n\t}\n"),
)
PURE = ("ui_gameflow.c", "ui_motion.c", "ui_gameflow_library.c",
        "ui_command_rail.c", "ui_gameflow_detail.c", "ui_game_history.c")
# The current renderer also draws the launch screen (test_launch_gx_stream.py),
# and wraps Spotlight's description as the cheat browser wraps its names.
LAUNCH = ("ui_launch.c", "ui_stage.c", "ui_cheats.c")


def between(source: str, start: str, end: str, inclusive: bool = False) -> str:
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last + (len(end) if inclusive else 0)]


@lru_cache(maxsize=1)
def reference_text_preparation() -> str:
    source = subprocess.check_output(["git", "-C", str(ROOT), "show",
                                      "v2.5.0:cube/swiss/source/gui/FrameBufferMagic.c"], text=True)
    return "\n".join(extract_function(source, "static void " + name + "(").replace(
        name + "(", "reference" + name + "(", 1)
        for name in ("_GameflowPrepareCardPresentation", "_GameflowPrepareSpotlight"))


def renderer(frame_c: str, frame_h: str) -> str:
    return "\n".join([
        '#include "ui_folder.h"' if "UIFolder_ColorRGB" in frame_c else "",
        between(frame_h, "typedef struct uiDrawObj {", "} uiDrawObj_t;", True),
        between(frame_h, "#define UI_GAMEFLOW_RENDER_SLOTS", "} uiGameflowRenderSnapshot_t;", True),
        between(frame_c, "typedef struct drawGameflowDetailPresentation {",
                "} drawGameflowEvent_t;", True),
        between(frame_c, "typedef struct gameflowPoint {", "static void _DrawHomeText("),
        extract_function(frame_c, "static bool _GameflowSnapshotValid("),
        extract_function(frame_c, "static void _GameflowCopySnapshot("),
        extract_function(frame_c, "bool DrawUpdateGameflow(")
        if "static void _GameflowPrepareSnapshot(" in frame_c else "",
        reference_text_preparation() if "static void _GameflowPrepareSnapshot(" in frame_c else "",
    ])


PRELUDE = r"""
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UI_ASSETS_HOST_BUILD 1
#include "ui_assets.h"
#include "ui_gameflow.h"
#include "ui_gameflow_detail.h"
#include "ui_gameflow_library.h"
#include "ui_command_rail.h"
#include "ui_scene.h"
#if LAYOUTS
#include "ui_cheats.h"
#include "ui_jewel_case.h"
#include "ui_launch.h"
#include "ui_stage.h"
#endif

typedef int8_t s8;
typedef struct { u8 r, g, b, a; } GXColor;
#define BNR_PIXELDATA_LEN (96*32*2)
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2
#define UI_COLOR_INDIGO 0
enum { GX_QUADS = 0x80, GX_TRIANGLESTRIP = 0x98, GX_VTXFMT0 = 0, GX_TEXMAP0 = 0, GX_BM_BLEND = 1,
	GX_BL_SRCALPHA = 4, GX_BL_INVSRCALPHA = 5, GX_LO_CLEAR = 0,
	GX_TF_RGB5A3 = 5, GX_CLAMP = 0, GX_FALSE = 0, GX_LINEAR = 1, GX_NEAR = 0 };
static struct { int uiColor, stillDiscs; } swissSettings;

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #c); exit(73); } } while(0)
static FILE *out;
static bool active, videoLocked;
enum { EV_GAMEFLOW = 77 };
static const int _videomutex = 1;
static void LWP_MutexLock(int mutex) { CHECK(mutex==1 && !videoLocked);videoLocked=true; }
static void LWP_MutexUnlock(int mutex) { CHECK(mutex==1 && videoLocked);videoLocked=false; }
static int declared, emitted, phase;
static void drawInit(void) { CHECK(!active); }
static void _SetupRasterColor(void) { CHECK(!active); }
static void UIColor_Apply(u8 *r, u8 *g, u8 *b) { (void)r; (void)g; (void)b; }
static void GX_SetNumTevStages(int n) { CHECK(!active); (void)n; }
static void GX_SetBlendMode(int a, int b, int c, int d) { CHECK(!active); (void)a; (void)b; (void)c; (void)d; }
static void GX_InvalidateTexAll(void) { CHECK(!active); }
static void GX_LoadTexObj(GXTexObj *texture, int map) { CHECK(!active); (void)map; fprintf(out, "X %s\n", (const char *)texture->data); }
static void GX_Begin(int primitive, int format, int count)
{
	CHECK(!active && format == GX_VTXFMT0 && count > 0 && (primitive == GX_QUADS ?
		count % 4 == 0 : primitive == GX_TRIANGLESTRIP && count >= 4 && count % 2 == 0));
	active = true; declared = count; emitted = 0; phase = 0;
	fprintf(out, primitive == GX_QUADS ? "B %d\n" : "B %d strip\n", count);
}
static void GX_Position3f32(float x, float y, float z)
{
	CHECK(active && phase == 0 && z == 0.0f && isfinite(x) && isfinite(y));
	phase = 1; fprintf(out, "P %.2f %.2f\n", x, y);
}
static void GX_Color4u8(u8 r, u8 g, u8 b, u8 a) { CHECK(active && phase == 1); phase = 2; fprintf(out, "C %u %u %u %u\n", r, g, b, a); }
static void GX_TexCoord2f32(float s, float t)
{
	CHECK(active && phase == 2 && emitted < declared); phase = 0; emitted++;
	fprintf(out, "T %.4f %.4f\n", s, t);
}
static void GX_End(void) { CHECK(active && phase == 0 && emitted == declared); active = false; }
static void DCFlushRange(void *p, u32 n) { (void)p; (void)n; }
static void GX_InitTexObj(GXTexObj *t, void *d, int w, int h, int f, int s, int tt, int m)
{ (void)w; (void)h; (void)f; (void)s; (void)tt; (void)m; t->data = d; }
static void GX_InitTexObjFilterMode(GXTexObj *t, int a, int b) { (void)t; (void)a; (void)b; }
static void drawStringMedium(int x, int y, const char *text, float scale, int align, GXColor c)
{
	CHECK(!active);
	fprintf(out, "S %d %d %.3f %d %u %s\n", x, y, scale, align, c.a, text);
	fprintf(out, "I %d %d %u %u %u %u\n", x, y, c.r, c.g, c.b, c.a);
}
static void _DrawHintText(int x, int y, const char *text, float scale, int align, GXColor c)
{ CHECK(!active); fprintf(out, "H %d %d %.3f %d %u %s\n", x, y, scale, align, c.a, text); }
static void _HintRoundRect(float cx, float cy, float w, float h, float r, GXColor c)
{ CHECK(!active); fprintf(out, "R %.2f %.2f %.2f %.2f %.2f %u\n", cx, cy, w, h, r, c.a); }
static int GetTextSizeInPixels(const char *text) { CHECK(!videoLocked);return 11 * (int)strlen(text); }
static float GetTextScaleToFitInWidthWithMax(const char *text, int width, float maximum)
{
	float size = (float)GetTextSizeInPixels(text);
	float scale = size < (float)width ? 1.0f : (float)width / size;
	return scale < maximum ? scale : maximum;
}
/* When every picture arrived, by a clock the frames advance: long ago,
 * until "Y" has them all arrive now, or "Y n" game n's still. */
static u32 clockMs = 600000u, artArrivedMs, stillArrivedMs[1000];
u32 UIAssets_PeekAgeMs(uiPosterHandle_t handle) { (void)handle; return clockMs - artArrivedMs; }
u32 UIStills_PeekAgeMs(uiPosterHandle_t handle)
{
	return clockMs - (handle.slot < 1000u &&
		stillArrivedMs[handle.slot] > artArrivedMs ?
		stillArrivedMs[handle.slot] : artArrivedMs);
}
static u32 CardArt_PosterAgeMs(int32_t card) { (void)card; return clockMs - artArrivedMs; }
/* Every seventh game has no cover, so the fallback art is drawn too. The
 * launch test closes the pack, as the hand-off does. */
static GXTexObj covers[1000];
static char coverIds[1000][8];
static bool packClosed;
uiPosterResult_t UIAssets_Query(const char *id, size_t length, bool bnr, uiPosterHandle_t *handle)
{
	int n = atoi(id + 1);
	(void)bnr;
	if(packClosed) return UI_POSTER_CORRUPT_OR_UNAVAILABLE;
	if(length < 6 || id[0] != 'G' || n % 7 == 3) return UI_POSTER_PROCEDURAL_CARD;
	handle->slot = (u16)n; handle->generation = 1u;
	return UI_POSTER_EXACT;
}
GXTexObj *UIAssets_Peek(uiPosterHandle_t handle)
{
	memcpy(coverIds[handle.slot], "", 1);
	snprintf(coverIds[handle.slot], sizeof(coverIds[0]), "G%03uE0", handle.slot);
	covers[handle.slot].data = coverIds[handle.slot];
	return &covers[handle.slot];
}
bool UIAssets_DominantColor(const char *id, size_t length, u8 *r, u8 *g, u8 *b)
{ (void)id; (void)length; (void)r; (void)g; (void)b; return false; }
#if LAYOUTS
/* Every third game has no gameplay still, so Spotlight shows its cover. */
static GXTexObj stills[1000];
static char stillIds[1000][16];
uiPosterResult_t UIStills_Query(const char *id, size_t length, bool bnr, uiPosterHandle_t *handle)
{
	int n = atoi(id + 1);
	(void)bnr;
	if(packClosed || length < 6 || id[0] != 'G' || n % 3 == 2) return UI_POSTER_USE_BNR;
	handle->slot = (u16)n; handle->generation = 1u;
	return UI_POSTER_EXACT;
}
GXTexObj *UIStills_Peek(uiPosterHandle_t handle)
{
	snprintf(stillIds[handle.slot], sizeof(stillIds[0]), "still:G%03uE0", handle.slot);
	stills[handle.slot].data = stillIds[handle.slot];
	return &stills[handle.slot];
}
/* Every fourth game (2, 6, 10, ...) has a disc in discs.pak; Q has them all
 * still being read. */
static GXTexObj discs[1000];
static char discIds[1000][16];
static bool discsLoading;
uiPosterResult_t UIDiscs_Query(const char *id, size_t length, bool bnr, uiPosterHandle_t *handle)
{
	int n = atoi(id + 1);
	(void)bnr;
	if(packClosed || length < 6 || id[0] != 'G' || n % 4 != 2) return UI_POSTER_USE_BNR;
	handle->slot = (u16)n; handle->generation = 1u;
	return UI_POSTER_EXACT;
}
GXTexObj *UIDiscs_Peek(uiPosterHandle_t handle)
{
	if(discsLoading) return NULL;
	snprintf(discIds[handle.slot], sizeof(discIds[0]), "disc:G%03uE0", handle.slot);
	discs[handle.slot].data = discIds[handle.slot];
	return &discs[handle.slot];
}
u32 UIDiscs_PeekAgeMs(uiPosterHandle_t handle) { (void)handle; return clockMs - artArrivedMs; }
#endif
/* Posters made on the console (gui/card_art.c), an app's or a folder of
 * games': every third card has none. */
static GXTexObj artPosters[64];
static char artPosterNames[64][8];
static GXTexObj *CardArt_Poster(int32_t card)
{
	if(card < 0 || card >= 64 || card % 3 == 2) return NULL;
	snprintf(artPosterNames[card], sizeof(artPosterNames[0]), "ART%03d", (int)card);
	artPosters[card].data = artPosterNames[card];
	return &artPosters[card];
}
static uiSceneFrame_t sceneFrame;
const uiSceneFrame_t *UIScene_Frame(void) { return &sceneFrame; }
static float animDelta, animSeconds;
static float UIAnim_Delta(void) { return animDelta; }
static float UIAnim_Seconds(void) { return animSeconds; }
static uiMotionMode_t motionMode;
static uiMotionMode_t _CurrentMotionMode(void) { return motionMode; }
#if LAYOUTS
/* The jewel case's renderer (ui_jewel_case.c) has GX of its own: here it
 * logs each case it is given, a "J" line each, and K says it has no memory
 * for its case. */
static struct { s8 substickX, substickY; } padInstrument;
static bool jewelUnready;
bool UIJewelCase_Ready(void) { return !jewelUnready; }
void UIJewelCase_Draw(const uiJewelCaseShot_t *shots, int count, int direction,
	float seconds, float turnX, float turnY, uiMotionMode_t motion, bool spin)
{
	CHECK(!active && count >= 0 && count <= 9 && isfinite(seconds) && turnX == 0.0f &&
		turnY == 0.0f && motion == motionMode && spin == !swissSettings.stillDiscs);
	for(int i = 0; i < count; ++i) {
		CHECK(shots[i].slot > -1.0f && shots[i].slot < 1.0f && shots[i].alpha > 0.0f);
		fprintf(out, "J %.3f %u %s %.2f %.2f %.4f %.4f %.4f %.4f %d\n", shots[i].slot,
			shots[i].index, shots[i].print ? (const char *)shots[i].print->data : "-",
			shots[i].printAlpha, shots[i].alpha, shots[i].rect[0], shots[i].rect[1],
			shots[i].rect[2], shots[i].rect[3], direction);
	}
}
#endif
"""

DRIVER = r"""
static drawGameflowEvent_t *eventData;
static uiDrawObj_t event;
static uiGameflowRenderSnapshot_t snapshot;
static uint32_t generation;
/* A: the cards are apps, as Apps shows them. */
static bool apps;
/* O: Library Folders, inside a folder: every fourth card is a folder of
 * games, and the heading names the folder. */
static bool folders;
/* W: every game has a disc banner, as Spotlight's always do. */
static bool banners;

/* L layout count selected (A: apps, O: folders) | P selected hint rowDirection snap
 * | M motion | D mode | W (banners) | Y [game] (pictures, or its still, arrive)
 * | K (no jewel case) | Q (discs being read) | N frames dt  -- the log has one "F" per frame. */
static void publish(int layout, uint32_t count, uint32_t selected, int hint,
	int rowDirection, int snap, bool first)
{
	uiGameflowLibraryWindowSlot_t slots[25];
	size_t n;
	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.selection.generation = ++generation;
	snapshot.selection.itemCount = count;
	snapshot.selection.selectedIndex = selected;
	snapshot.selection.directionHint = (uiGameflowDirection_t)hint;
	snapshot.selection.snapTransition = snap != 0;
#if LAYOUTS
	snapshot.layout = (u8)layout;
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		snapshot.columns = 5;
		n = UIGameflowLibrary_BuildGridWindow(count, selected, 5u,
			(uiGameflowDirection_t)rowDirection, slots);
	}
	else
#else
	(void)layout; (void)rowDirection;
#endif
	n = UIGameflowLibrary_BuildWindow(count, selected,
		(uiGameflowDirection_t)hint, slots);
	snapshot.recordCount = (u32)n;
	for(size_t i = 0; i < n; ++i) {
		uiGameflowCardSnapshot_t *record = &snapshot.records[i];
		record->flags = UI_GAMEFLOW_CARD_VALID |
			(slots[i].index % 5u == 1u ? UI_GAMEFLOW_CARD_CUSTOM : 0u);
		record->libraryIndex = slots[i].index;
		record->relativeSlot = slots[i].relativeSlot;
#if LAYOUTS
		record->column = slots[i].column;
#endif
		snprintf(record->gameId, sizeof(record->gameId), "G%03uE0", slots[i].index);
		snprintf(record->title, sizeof(record->title), "Game number %u", slots[i].index);
		snprintf(record->company, sizeof(record->company), "Company %u", slots[i].index % 9u);
		snprintf(record->facts, sizeof(record->facts), "%s  |  1.4 GB", record->gameId);
#if LAYOUTS
		if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT || banners) {
			record->flags |= UI_GAMEFLOW_CARD_HAS_BANNER;
			snprintf((char *)record->banner, 16, "banner:%s", record->gameId);
		}
		if(folders && slots[i].index % 4u == 2u) {
			/* No ID, no banner, and a name longer than a card holds. */
			record->flags = (u8)(record->flags & ~UI_GAMEFLOW_CARD_HAS_BANNER);
			memset(record->banner, 0, 16);
			memset(record->gameId, 0, sizeof(record->gameId));
			record->subfolder = 1u;
			snprintf(record->title, sizeof(record->title), "Folder of games %u", slots[i].index);
			snprintf(record->company, sizeof(record->company), "FOLDER");
			snprintf(record->facts, sizeof(record->facts), "A  OPEN");
		}
#endif
#ifdef UI_GAMEFLOW_CARD_APP	/* the reference renderer has no Apps */
		if(apps) {
			record->flags = UI_GAMEFLOW_CARD_VALID | UI_GAMEFLOW_CARD_APP;
			memset(record->gameId, 0, sizeof(record->gameId));
			/* Short names, and long ones a card cuts. */
			snprintf(record->title, sizeof(record->title), slots[i].index % 2u ?
				"Game Boy Interface %u" : "gbi%u", slots[i].index);
			snprintf(record->company, sizeof(record->company), "app%u.dol", slots[i].index);
		}
#endif
	}
#if LAYOUTS
	if(folders) {
		snprintf(snapshot.folder, sizeof(snapshot.folder), "RACING / CLASSICS");
	}
	/* A folder of games has no description, as swiss.c leaves it. */
	if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT && selected != 27u &&
		!(folders && selected % 4u == 2u)) {
		/* Padded with spaces and no NUL, as a banner's may be: anything
		 * read past it would show as a word. Game 27 has none. */
		memset(snapshot.description, ' ', sizeof(snapshot.description));
		snprintf(snapshot.description, sizeof(snapshot.description),
			"The story of game number %u,\ntold in a banner that\n"
			"breaks its lines   the way discs do.", selected);
		snapshot.description[strlen(snapshot.description)] = ' ';
	}
#endif
	CHECK(_GameflowSnapshotValid(&snapshot));
	if(first) {
		memset(eventData, 0, sizeof(*eventData));
		UIGameflow_Init(&eventData->state);
#if LAYOUTS
		UIGameflow_SetColumns(&eventData->state, snapshot.columns);
		eventData->snapshot.layout = snapshot.layout;
		eventData->snapshot.columns = snapshot.columns;
#endif
	}
#if LAYOUTS
	CHECK(DrawUpdateGameflow(&event, &snapshot));
#else
	CHECK(UIGameflow_ApplySnapshot(&eventData->state, &snapshot.selection, motionMode));
	_GameflowCopySnapshot(eventData, &snapshot);
#endif
}

int main(void)
{
	char line[128];
	int layout = 0;
	uint32_t count = 0;
	out = stdout;
	eventData = calloc(1, sizeof(*eventData));
	CHECK(eventData != NULL);
	event.data = eventData;
	event.type = EV_GAMEFLOW;
	sceneFrame.scene = UI_SCENE_LIBRARY;
	sceneFrame.chromeProgress = 1.0f;
	sceneFrame.libraryReveal = 1.0f;
	while(fgets(line, sizeof(line), stdin)) {
		unsigned a, b; int c, d, e; float dt;
		if(sscanf(line, "L %d %u %u", &layout, &a, &b) == 3) {
			count = a; apps = false; folders = false; publish(layout, count, b, 0, 0, 0, true);
		}
		else if(sscanf(line, "A %d %u %u", &layout, &a, &b) == 3) {
			count = a; apps = true; folders = false; publish(layout, count, b, 0, 0, 0, true);
		}
		else if(sscanf(line, "O %d %u %u", &layout, &a, &b) == 3) {
			count = a; apps = false; folders = true; publish(layout, count, b, 0, 0, 0, true);
		}
		else if(sscanf(line, "P %u %d %d %d", &a, &c, &d, &e) == 4) {
			publish(layout, count, a, c, d, e, false);
		}
		else if(sscanf(line, "M %d", &c) == 1) motionMode = (uiMotionMode_t)c;
		else if(sscanf(line, "Y %u", &a) == 1) stillArrivedMs[a % 1000u] = clockMs;
		else if(line[0] == 'Y') artArrivedMs = clockMs;
		else if(line[0] == 'W') banners = true;
#if LAYOUTS
		else if(line[0] == 'K') jewelUnready = true;
		else if(line[0] == 'Q') discsLoading = true;
#endif
#if LAYOUTS
		else if(line[0] == 'C') {
			drawGameflowEvent_t *reference = malloc(sizeof(*reference));
			uiGameflowRenderSnapshot_t *original = malloc(sizeof(*original));
			CHECK(reference && original);
			for(u32 i=0;i<snapshot.recordCount;i++) {
				uiGameflowCardSnapshot_t *record=&snapshot.records[i];
				memset(record->title,'W',sizeof(record->title)-1);
				memset(record->company,'C',sizeof(record->company)-1);
				memset(record->facts,'F',sizeof(record->facts)-1);
				record->title[sizeof(record->title)-1]=0;
				record->company[sizeof(record->company)-1]=0;
				record->facts[sizeof(record->facts)-1]=0;
			}
			snapshot.selection.generation=++generation;
			memcpy(original,&snapshot,sizeof(snapshot));
			memcpy(reference,eventData,sizeof(*reference));
			memcpy(&reference->snapshot,&snapshot,sizeof(snapshot));
			for(u32 i=0;i<snapshot.recordCount;i++)
				reference_GameflowPrepareCardPresentation(reference,i);
			reference_GameflowPrepareSpotlight(reference);
			CHECK(DrawUpdateGameflow(&event,&snapshot));
			CHECK(!memcmp(original,&snapshot,sizeof(snapshot)));
			for(u32 i=0;i<snapshot.recordCount;i++) {
				CHECK(!strcmp(eventData->snapshot.records[i].title,reference->snapshot.records[i].title));
				CHECK(!strcmp(eventData->snapshot.records[i].company,reference->snapshot.records[i].company));
				CHECK(!strcmp(eventData->snapshot.records[i].facts,reference->snapshot.records[i].facts));
				CHECK(!memcmp(&eventData->cardPresentation[i],&reference->cardPresentation[i],
					sizeof(eventData->cardPresentation[i])));
			}
			CHECK(!memcmp(eventData->spotlightLines,reference->spotlightLines,sizeof(eventData->spotlightLines)));
			free(original);free(reference);
		}
		else if(line[0] == 'T') {
			drawGameflowEvent_t *saved = malloc(sizeof(*saved));
			CHECK(saved != NULL);
			memcpy(saved, eventData, sizeof(*saved));
			snapshot.records[0].title[0] = '!';
			/* A stale generation must not publish even prepared text. */
			CHECK(!DrawUpdateGameflow(&event, &snapshot));
			CHECK(!memcmp(saved, eventData, sizeof(*saved)) && !videoLocked);
			snapshot.selection.generation++;
			snapshot.layout = UI_GAMEFLOW_LAYOUT_VERTICAL;
			CHECK(!DrawUpdateGameflow(&event, &snapshot));
			CHECK(!memcmp(saved, eventData, sizeof(*saved)) && !videoLocked);
			snapshot.layout = saved->snapshot.layout;
			event.disposed = true;
			CHECK(!DrawUpdateGameflow(&event, &snapshot));
			CHECK(!memcmp(saved, eventData, sizeof(*saved)) && !videoLocked);
			event.disposed = false;
			memcpy(&snapshot, &saved->snapshot, sizeof(snapshot));
			free(saved);
		}
#endif
		else if(sscanf(line, "D %d", &c) == 1) {
			UIGameflow_SetMode(&eventData->state, (uiGameflowMode_t)c, motionMode);
			sceneFrame.scene = c ? UI_SCENE_GAME_DETAIL : UI_SCENE_LIBRARY;
		}
		else if(sscanf(line, "N %u %f", &a, &dt) == 2) {
			for(b = 0; b < a; ++b) {
				animDelta = dt; animSeconds += dt;
				clockMs += (u32)(dt * 1000.0f + 0.5f);
				fprintf(out, "F\n");
				_DrawGameflow(&event);
			}
		}
		else { fprintf(stderr, "bad command: %s", line); return 64; }
	}
	return 0;
}
"""


def build(work: Path, gui: Path, frame_c: str, frame_h: str, layouts: bool, name: str,
          driver: str = DRIVER) -> Path:
    source = work / f"{name}.c"
    source.write_text(PRELUDE + renderer(frame_c, frame_h) + driver)
    binary = work / name
    flags = ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function",
             "-Wno-unused-parameter", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=all", f"-DLAYOUTS={1 if layouts else 0}",
             "-I" + str(gui)]
    if os.uname().sysname == "Linux":
        flags += ["-fno-pie", "-no-pie"]
    # The archived comparison predates save metadata. Only today's Detail
    # header uses the shared formatter; missing current sources still fail
    # compilation rather than quietly excluding the new feature.
    metadata = (("ui_saves_metadata.c", "ui_saves.c") if
                '#include "ui_saves_metadata.h"' in (gui / "ui_gameflow_detail.h").read_text() else ())
    result = subprocess.run(shlex.split(os.environ.get("CC", "cc")) + flags +
                            ["-o", str(binary), str(source)] +
                            [str(gui / pure) for pure in PURE + metadata + (LAUNCH if layouts else ()) +
                             (("ui_folder.c",) if "UIFolder_ColorRGB" in frame_c else ())] +
                            ["-lm"],
                            capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise AssertionError(result.stderr[-4000:])
    return binary


def frames(log: str) -> list:
    """Each frame's lines, after the first 'F'."""
    return re.split(r"(?m)^F\n", log)[1:]


def covers(frame: str) -> dict:
    """gameId -> list of drawn cover quads (x0, y0, x1, y1) in one frame."""
    found = {}
    lines = frame.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("X "):
            points = [tuple(map(float, l.split()[1:])) for l in lines[i + 1:i + 14]
                      if l.startswith("P ")][:4]
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            found.setdefault(line[2:], []).append((min(xs), min(ys), max(xs), max(ys)))
    return found


def centre(box):
    return ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)


def shells(frame: str) -> dict:
    """gameId -> the corners of its card in one frame: of the quads every card
    is drawn as first, its foot dark, the one nearest the middle of its cover."""
    lines = frame.splitlines()
    quads = []
    for i, line in enumerate(lines):
        block = lines[i + 1:i + 1 + 3 * int(line.split()[1])] if line.startswith("B ") else []
        if block and "strip" not in line and block[7].startswith("C 10 8 30 "):
            points = [tuple(map(float, block[k].split()[1:])) for k in range(0, len(block), 3)]
            quads = [points[k:k + 4] for k in range(0, len(points), 4)]
            break
    found = {}
    for game, boxes in covers(frame).items():
        if game.startswith("still:"):  # Spotlight's picture, not a card
            continue
        x, y = centre(boxes[0])
        found[game] = min(quads, key=lambda q: (sum(p[0] for p in q) / 4 - x) ** 2 +
                          (sum(p[1] for p in q) / 4 - y) ** 2)
    return found


# Horizontal through moves, wraps, page snaps, Detail and back, in each
# motion mode and small libraries; the same script runs on both renderers.
HORIZONTAL = []
for mode in (0, 1, 2):
    for count, start in ((40, 1), (2, 0), (3, 2), (7, 6), (1, 0)):
        HORIZONTAL += [f"M {mode}", f"L 0 {count} {start}", "N 2 0.0167"]
        selected = start
        for move, (hint, snap) in enumerate(((1, 0), (1, 0), (-1, 0), (1, 1), (-1, 1), (-1, 0))):
            if snap:
                selected = 0 if selected == count - 1 else min(count - 1, selected + 9) \
                    if hint > 0 else (count - 1 if selected == 0 else max(0, selected - 9))
            else:
                selected = (selected + hint) % count
            HORIZONTAL += [f"P {selected} {hint} 0 {snap}", "N 3 0.0167"]
            if move == 2:
                HORIZONTAL += ["N 1 0.004", "D 1", "N 20 0.0167", "D 0", "N 20 0.0167"]
        HORIZONTAL += ["N 30 0.0167"]


class GameflowGxStream(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="swiss-gameflow-gx-")
        work = Path(cls.tmp.name)
        cls.frame_c = (GUI / "FrameBufferMagic.c").read_text()
        cls.frame_h = (GUI / "FrameBufferMagic.h").read_text()
        cls.binary = build(work, GUI, cls.frame_c, cls.frame_h, True, "layouts")
        base = work / "base-tree"
        base.mkdir()
        archive = subprocess.run(["git", "-C", str(ROOT), "archive", BASE,
                                  "cube/swiss/source/gui"], capture_output=True)
        if archive.returncode:
            raise AssertionError(archive.stderr.decode()[-2000:])
        subprocess.run(["tar", "-x", "-C", str(base)], input=archive.stdout, check=True)
        base_gui = base / "cube/swiss/source/gui"
        for name, old, new in BASE_EDITS:
            text = (base_gui / name).read_text()
            if text.count(old) != 1:
                raise AssertionError(f"{BASE} {name}: {old!r} is not there exactly once")
            (base_gui / name).write_text(text.replace(old, new))
        cls.base = build(work, base_gui, (base_gui / "FrameBufferMagic.c").read_text(),
                         (base_gui / "FrameBufferMagic.h").read_text(), False, "base")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_script(self, script, binary=None):
        result = subprocess.run([str(binary or self.binary)], input="\n".join(script) + "\n",
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr[-2000:])
        return result.stdout

    def test_horizontal_draws_what_the_carousel_always_drew(self):
        now = self.run_script(HORIZONTAL)
        before = self.run_script(HORIZONTAL, self.base)
        self.assertGreater(len(frames(now)), 600)
        new_hint = "H 320 428 0.460 1 {} D-PAD  BROWSE   A  OPEN   Y  SETTINGS   X  BACK   B  HOME"
        old_hint = "H 320 428 0.460 1 {} D-PAD  BROWSE   A  OPEN   X  BACK   B  HOME"
        hints = 0
        for number, (a, b) in enumerate(zip(frames(now), frames(before))):
            a_lines, b_lines = a.splitlines(), b.splitlines()
            self.assertEqual(len(a_lines), len(b_lines), f"frame {number}")
            for x, y in zip(a_lines, b_lines):
                if x != y:
                    alpha = x.split()[5]
                    self.assertEqual((x, y), (new_hint.format(alpha), old_hint.format(alpha)),
                                     f"frame {number}")
                    hints += 1
        self.assertEqual(len(frames(now)), len(frames(before)))
        self.assertGreater(hints, 300)

    def test_horizontal_shows_two_covers_either_side(self):
        log = self.run_script(["L 0 40 20", "N 40 0.0167"])
        rest = covers(frames(log)[-1])
        games = [f"G{i:03d}E0" for i in range(18, 23)]
        self.assertEqual(set(rest), set(games))
        for game in games:
            self.assertEqual(len(rest[game]), 1, game)
            self.assertTrue(0 <= rest[game][0][0] and rest[game][0][2] <= 640, game)
        self.assertEqual([centre(rest[g][0])[0] for g in games],
                         sorted(centre(rest[g][0])[0] for g in games))
        # The second either side is a card turned away, not an edge-on sliver.
        for game in (games[0], games[-1]):
            self.assertGreaterEqual(rest[game][0][2] - rest[game][0][0], 50, game)
        # A game without a cover shows its banner or emblem there instead.
        log = self.run_script(["L 0 40 19", "N 40 0.0167"])
        self.assertNotIn("G017E0", covers(frames(log)[-1]))
        self.assertEqual(frames(log)[-1].count("B 24\n"), 1)

    def test_apps_show_as_the_library(self):
        """Cards of apps are the Apps screen, in every layout: its heading,
        its controls (A starts, nothing opens settings or goes back a
        folder), each app's own poster, and on an app without one its name,
        cut to what a card holds, where a game's ID goes."""
        for layout, heading in ((0, "S 320 70 0.500 1"), (1, "S 262 177 0.420 0"),
                                (2, "S 320 40 0.460 1")):
            with self.subTest(layout=layout):
                result = subprocess.run([str(self.binary)], input=f"A {layout} 12 4\nN 40 0.0167\n",
                                        capture_output=True, encoding="latin-1", timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr[-2000:])
                last = frames(result.stdout)[-1]
                self.assertIn(" APPS\n", last)
                self.assertTrue(any(l.startswith(heading) and l.endswith(" APPS")
                                    for l in last.splitlines()), heading)
                self.assertNotIn("GAME LIBRARY", last)
                self.assertIn("D-PAD  BROWSE   A  START   B  HOME", last)
                self.assertNotIn("SETTINGS", last)
                drawn = set(covers(last))
                self.assertIn("ART004", drawn)
                self.assertFalse({name for name in drawn if not name.startswith("ART")})
                self.assertFalse({f"ART{n:03d}" for n in range(12) if n % 3 == 2} & drawn)
                texts = [l.split(" ", 6)[6] for l in last.splitlines() if l.startswith("S ")]
                self.assertIn("gbi4", texts)
        # In front, an app without a picture (every third) shows its name
        # where a game's ID goes, cut to eight letters, over "APP".
        for selected, name in ((2, "GBI2"), (5, "GAME BOY\x85")):
            result = subprocess.run([str(self.binary)], input=f"A 0 12 {selected}\nN 40 0.0167\n",
                                    capture_output=True, encoding="latin-1", timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr[-2000:])
            last = frames(result.stdout)[-1]
            # split, not splitlines: the IPL font's ellipsis is U+0085, a
            # line break to Python.
            texts = [l.split(" ", 6)[6] for l in last.split("\n") if l.startswith("S ")]
            self.assertIn(name, texts)
            self.assertIn("APP", texts)
            self.assertNotIn(f"ART{selected:03d}", covers(last))

    def test_a_folder_of_games(self):
        """Library Folders, inside a folder: in every layout the heading names
        the folder and B goes back up it. A folder of games without art shows
        its name cut to a card's eight letters over FOLDER, on a cover card and
        on Spotlight's row alike; selected in Spotlight, the column holds no
        description line, since a folder has none. A folder with a picture
        shows its poster, as an app does."""
        def last_frame(script):
            result = subprocess.run([str(self.binary)], input=script, capture_output=True,
                                    encoding="latin-1", timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr[-2000:])
            return frames(result.stdout)[-1]

        def texts(frame):
            # split, not splitlines: the IPL font's ellipsis is U+0085.
            return [l.split(" ", 6)[6] for l in frame.split("\n") if l.startswith("S ")]

        for layout, heading in ((0, "S 320 70 0.500 1"), (1, "S 262 177 0.420 0"),
                                (2, "S 320 40 0.460 1"), (3, "S 36 62 0.420 0")):
            with self.subTest(layout=layout):
                last = last_frame(f"O {layout} 12 4\nN 40 0.0167\n")
                self.assertTrue(any(l.startswith(heading) and l.endswith(" RACING / CLASSICS")
                                    for l in last.split("\n")), heading)
                self.assertNotIn("GAME LIBRARY", last)
                self.assertIn("D-PAD  BROWSE   A  OPEN   Y  SETTINGS   B  BACK", last)
                self.assertNotIn("B  HOME", last)
        # Selected: card 2 in the carousel shows FOLDER OF\x85 over FOLDER.
        shown = texts(last_frame("O 0 12 2\nN 40 0.0167\n"))
        self.assertIn("FOLDER O\x85", shown)
        self.assertIn("FOLDER", shown)
        self.assertNotIn("Folder of games 2", [t for t in shown if t.startswith("FOLDER")])
        # Spotlight: folder 26 selected. Its tile on the row has its short
        # name, never the whole one, and the column no description line.
        frame = last_frame("O 3 40 26\nN 40 0.0167\n")
        whole = [l.split(" ")[1] for l in frame.split("\n")
                 if l.startswith("S ") and l.split(" ", 6)[6] == "Folder of games 26"]
        self.assertEqual(whole, ["380"])  # only the column's title, not the row
        self.assertGreaterEqual(texts(frame).count("FOLDER O\x85"), 2)  # panel and tile
        column = self.column(frame)
        self.assertEqual(column[92], "Folder of games 26")
        self.assertNotIn(154, column)
        # A game beside it still says when it has no description.
        self.assertEqual(self.column(last_frame("O 3 40 27\nN 40 0.0167\n")).get(154),
                         "No description for this game.")
        # Folder 6 has a picture: its poster, in front and in Spotlight's
        # panel.
        for script in ("O 0 12 6\nN 40 0.0167\n", "O 3 40 6\nN 40 0.0167\n"):
            self.assertIn("ART006", covers(last_frame(script)))

    def test_vertical_column(self):
        log = self.run_script(["L 1 40 12", "N 40 0.0167"])
        rest = covers(frames(log)[-1])
        # The selected cover and its two neighbours show their covers.
        self.assertEqual(set(rest), {"G011E0", "G012E0", "G013E0"})
        # The selected cover is large at the left, a neighbour above and one below.
        selected = rest["G012E0"][0]
        self.assertEqual(len(rest["G012E0"]), 1)
        self.assertLess(selected[2], 240)
        self.assertGreater(selected[3] - selected[1], 200)
        above, below = rest["G011E0"][0], rest["G013E0"][0]
        self.assertLess(above[3], selected[1])
        self.assertGreater(below[1], selected[3])
        self.assertLess(above[3] - above[1], 70)
        for boxes in rest.values():
            for box in boxes:
                self.assertTrue(0 <= box[1] and box[3] <= 430, box)
        # Its title, publisher and facts sit to the right of the cover.
        text = [l for l in frames(log)[-1].splitlines() if l.startswith("S ")]
        self.assertIn("S 262 206", "\n".join(text))
        self.assertTrue(any(l.startswith("S 262 ") and l.endswith("Game number 12") for l in text))
        self.assertTrue(any(l.startswith("S 262 ") and l.endswith("G012E0  |  1.4 GB") for l in text))
        # Up steps back one card without a jump, and the ring wraps.
        log = self.run_script(["L 1 40 0", "N 40 0.0167", "P 39 -1 0 0", "N 1 0.0",
                               "N 40 0.0167"])
        before, after, rest = (covers(f) for f in (frames(log)[39], frames(log)[40],
                                                   frames(log)[-1]))
        for game, boxes in before.items():
            if game in after:
                self.assertEqual(boxes, after[game], game)
        self.assertLess(rest["G039E0"][0][2], 240)
        self.assertGreater(rest["G039E0"][0][3] - rest["G039E0"][0][1], 200)

    def test_game_folders_offer_navigation_without_identity_controls(self):
        for layout in range(4):
            with self.subTest(layout=layout):
                result = subprocess.run([str(self.binary)], input=f"O {layout} 12 2\nN 40 0.0167\n",
                                        capture_output=True, encoding="latin-1", timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr[-2000:])
                frame = frames(result.stdout)[-1]
                self.assertNotIn("C 68 170 230 255", frame)
                self.assertNotIn("Y  FOLDER", frame)
                self.assertIn("A  OPEN", frame)
                self.assertNotIn("Y  SETTINGS", frame)

    def test_grid_rows_and_highlight(self):
        log = self.run_script(["L 2 40 18", "N 40 0.0167"])
        rest = covers(frames(log)[-1])
        # Fifteen cards are drawn at all: the rows two away are gone.
        self.assertEqual(frames(log)[-1].splitlines()[0], "B 60")
        # Three rows of five on screen, every card with a cover showing it.
        expected = {f"G{i:03d}E0" for i in range(10, 25) if i % 7 != 3}
        self.assertEqual(set(rest), expected)
        for game, boxes in rest.items():
            self.assertEqual(len(boxes), 1, game)
            x, y = centre(boxes[0])
            index = int(game[1:4])
            self.assertAlmostEqual(x, 120 + (index % 5) * 100, delta=1.0)
            self.assertAlmostEqual(y, 108 + (index // 5 - 2) * 108, delta=1.0)
            width = boxes[0][2] - boxes[0][0]
            self.assertAlmostEqual(width, (79.2 if index == 18 else 72.0) * (1 - 2 / 30),
                                   delta=1.5, msg=game)
        # The highlight frames the selected card: three rings of 16 vertices.
        highlight = frames(log)[-1].split("B 48\n")
        self.assertEqual(len(highlight), 2)
        ring = [tuple(map(float, l.split()[1:])) for l in highlight[1].splitlines()
                if l.startswith("P ")][:48]
        xs, ys = [p[0] for p in ring], [p[1] for p in ring]
        self.assertAlmostEqual((min(xs) + max(xs)) / 2, 420, delta=1.0)
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, 216, delta=1.0)
        # It reaches the rows above and below but never covers their cards.
        self.assertLessEqual(max(ys) - min(ys), 2 * (108 - 96 / 2))
        # Every card showing its art carries the own-settings mark (a plate,
        # three bars and three knobs), sized to the card: games 11, 16, 21.
        marks = [l for l in frames(log)[-1].splitlines() if l.startswith("R ")]
        self.assertEqual(len(marks), 3 * 7)
        plates = sorted(float(l.split()[3]) for l in marks[::7])
        self.assertAlmostEqual(plates[0], 96 * 0.13, delta=0.5)
        # The selected title and publisher sit in the strip above the hint.
        text = frames(log)[-1]
        self.assertIn("S 320 388", text)
        self.assertIn("Game number 18", text)
        self.assertIn("H 320 428", text)

    GRID_MOVES = {
            "right": (40, 17, "P 18 1 0 0"),
            "row wrap right": (40, 19, "P 20 1 1 0"),
            "down": (40, 17, "P 22 1 1 0"),
            "up": (40, 17, "P 12 -1 -1 0"),
            "down from the short last row": (42, 41, "P 1 1 1 0"),
            "up to the short last row": (42, 1, "P 41 -1 -1 0"),
            "three rows down": (13, 7, "P 12 1 1 0"),
            "three rows up": (13, 2, "P 12 -1 -1 0"),
            "four rows down": (18, 12, "P 17 1 1 0"),
            "four rows up": (18, 2, "P 17 -1 -1 0"),
    }

    def check_grid_move(self, count, start, move):
        log = self.run_script([f"L 2 {count} {start}", "N 40 0.0167", move,
                               "N 1 0.0", "N 60 0.0167"])
        before, after = covers(frames(log)[39]), covers(frames(log)[40])
        for game, boxes in before.items():
            self.assertIn(game, after, game)
            for box in boxes:
                self.assertTrue(any(max(abs(a - b) for a, b in zip(box, other)) < 1.01
                                    for other in after[game]), (game, box, after[game]))
        rest = covers(frames(log)[-1])
        for game, boxes in rest.items():
            self.assertEqual(len(boxes), 1, game)

    def check_all_grid_moves(self):
        for count, start, move in self.GRID_MOVES.values():
            self.check_grid_move(count, start, move)

    def test_grid_moves_never_jump(self):
        for name, (count, start, move) in self.GRID_MOVES.items():
            with self.subTest(move=name):
                self.check_grid_move(count, start, move)

    def test_tiny_grids_and_columns_draw_each_game_once(self):
        for layout in (1, 2, 3):
            for count in range(1, 11):
                for selected in range(count):
                    with self.subTest(layout=layout, count=count, selected=selected):
                        log = self.run_script([f"L {layout} {count} {selected}",
                                               "N 30 0.0167"])
                        rest = covers(frames(log)[-1])
                        for game, boxes in rest.items():
                            self.assertEqual(len(boxes), 1, game)
                        if layout == 2 and count <= 5:
                            self.assertEqual(len(rest), sum(1 for i in range(count) if i % 7 != 3))

    PANEL = (36.0, 78.0, 356.0, 318.0)

    def column(self, frame):
        """Spotlight's column: y -> text of each line drawn at x 380."""
        lines = [l.split(None, 6) for l in frame.splitlines() if l.startswith("S 380 ")]
        return {int(parts[2]): parts[6] for parts in lines}

    def test_spotlight_row_picture_and_column(self):
        log = self.run_script(["L 3 40 21", "N 40 0.0167"])
        rest = covers(frames(log)[-1])
        games = [f"G{i:03d}E0" for i in range(19, 24)]
        # The row: the selected banner and two either side, left to right, a
        # banner pixel for pixel and the selected one a quarter larger.
        self.assertEqual({g for g in rest if g.startswith("banner:")},
                         {f"banner:{g}" for g in games})
        for game, x in zip(games, (80, 200, 320, 440, 560)):
            boxes = rest[f"banner:{game}"]
            self.assertEqual(len(boxes), 1, game)
            self.assertAlmostEqual(centre(boxes[0])[0], x, delta=1.0, msg=game)
            self.assertAlmostEqual(centre(boxes[0])[1], 372, delta=1.0, msg=game)
            self.assertAlmostEqual(boxes[0][2] - boxes[0][0],
                                   120 if game == "G021E0" else 96, delta=0.5, msg=game)
        # The picture: the selected game's still, 320x240, pixel for pixel,
        # and nothing else: no cover, no other still.
        self.assertEqual(rest.get("still:G021E0"), [self.PANEL])
        self.assertEqual({g for g in rest if not g.startswith("banner:")}, {"still:G021E0"})
        # Inside a CRT's action-safe area, 5% in from each edge: overscan
        # never hides the still or a banner.
        for game, boxes in rest.items():
            for x0, y0, x1, y1 in boxes:
                self.assertTrue(32 <= x0 and x1 <= 608 and 24 <= y0 and y1 <= 456, game)
        # The column: title, publisher, description and facts.
        column = self.column(frames(log)[-1])
        self.assertEqual(column[92], "Game number 21")
        self.assertEqual(column[118], "Company 3")
        self.assertEqual(column[306], "G021E0  |  1.4 GB")
        described = [column[y] for y in range(154, 154 + 6 * 22, 22) if y in column]
        self.assertGreaterEqual(len(described), 2)
        prose = " ".join(described)
        # The banner's line breaks and runs of spaces are gone, and nothing
        # past its 128 characters is read.
        self.assertEqual(prose, "The story of game number 21, told in a banner "
                                "that breaks its lines the way discs do.")
        self.assertNotIn("  ", prose)
        self.assertNotIn("banner:", prose)
        self.assertIn("H 320 428", frames(log)[-1])
        # Game 21 has settings of its own: its still carries the mark a cover
        # does (a plate, three bars and three knobs), at the panel's top
        # right and sized to it; the banners on the row carry none.
        marks = [l.split() for l in frames(log)[-1].splitlines() if l.startswith("R ")]
        self.assertEqual(len(marks), 7)
        x, y, size = (float(v) for v in marks[0][1:4])
        self.assertAlmostEqual(x, 36 + 0.85 * 320, delta=1.0)
        self.assertAlmostEqual(y, 78 + 0.085 * 240, delta=1.0)
        self.assertAlmostEqual(size, 0.13 * 240, delta=0.5)

    def test_spotlight_marks_only_a_game_with_its_own_settings(self):
        # Game 22 has none, and game 21's banner beside it carries no mark.
        rest = frames(self.run_script(["L 3 40 22", "N 40 0.0167"]))[-1]
        self.assertIn("banner:G021E0", rest)
        self.assertNotIn("\nR ", "\n" + rest)
        # Game 26 has no still but its settings: its cover carries the mark.
        rest = frames(self.run_script(["L 3 40 26", "N 40 0.0167"]))[-1]
        marks = [l.split() for l in rest.splitlines() if l.startswith("R ")]
        self.assertEqual(len(marks), 7)
        self.assertAlmostEqual(float(marks[0][1]), 106 + 0.85 * 180, delta=6.0)

    def test_spotlight_never_leaves_the_column_blank(self):
        # Game 27's snapshot carries no description: the column says so.
        column = self.column(frames(self.run_script(["L 3 40 27", "N 40 0.0167"]))[-1])
        self.assertEqual(column[92], "Game number 27")
        self.assertEqual(column.get(154), "No description for this game.")
        self.assertNotIn(176, column)

    def test_spotlight_without_a_still(self):
        # Game 23 has no still but a cover: the cover stands in the middle of
        # the panel, where it leaves for Detail from.
        rest = covers(frames(self.run_script(["L 3 40 23", "N 40 0.0167"]))[-1])
        self.assertNotIn("still:G023E0", rest)
        box = rest["G023E0"][0]
        self.assertAlmostEqual(box[0], 106 + 6, delta=1.0)
        self.assertAlmostEqual(box[2], 286 - 6, delta=1.0)
        self.assertAlmostEqual(box[1], 78 + 8, delta=1.0)
        self.assertAlmostEqual(box[3], 318 - 8, delta=1.0)
        # Game 17 has neither: its banner card stands there instead, as in
        # Detail, as well as its banner on the row.
        rest = covers(frames(self.run_script(["L 3 40 17", "N 40 0.0167"]))[-1])
        self.assertNotIn("G017E0", rest)
        panel, row = sorted(rest["banner:G017E0"], key=lambda box: box[1])
        self.assertTrue(106 <= panel[0] and panel[2] <= 286 and panel[3] <= 318, panel)
        self.assertAlmostEqual(centre(row)[1], 372, delta=1.0)

    def test_spotlight_moves_never_jump(self):
        log = self.run_script(["L 3 40 21", "N 40 0.0167", "P 22 1 0 0", "N 1 0.0",
                               "N 40 0.0167"])
        before, after = covers(frames(log)[39]), covers(frames(log)[40])
        for game, boxes in before.items():
            if game.startswith("banner:") and game in after:
                self.assertEqual(boxes, after[game], game)
        # The old still fades out as the new one fades in.
        self.assertTrue(any("X still:G021E0" in f and "X still:G022E0" in f
                            for f in frames(log)[41:]))
        rest = covers(frames(log)[-1])
        self.assertEqual(rest["still:G022E0"], [self.PANEL])
        self.assertNotIn("still:G021E0", rest)
        self.assertAlmostEqual(centre(rest["banner:G022E0"][0])[0], 320, delta=1.0)
        self.assertEqual(self.column(frames(log)[-1])[92], "Game number 22")

    @staticmethod
    def texture_alpha(frame: str, name: str):
        """The alpha a texture is first drawn with in a frame, or None."""
        lines = frame.splitlines()
        if f"X {name}" not in lines:
            return None
        at = lines.index(f"X {name}")
        return int(next(l for l in lines[at:] if l.startswith("C ")).split()[4])

    def test_a_picture_fades_in_as_it_arrives(self):
        for script, picture, fallbacks in (
                # A cover, over the card it stood in for; five cards show.
                (["L 0 40 20"], "G020E0", 5),
                # An app's poster made on the console.
                (["A 0 10 4"], "ART004", None),
                # Spotlight's still, over the cover it stood in for.
                (["L 3 40 21"], "still:G021E0", None)):
            with self.subTest(picture=picture):
                log = frames(self.run_script(script + ["N 40 0.0167", "Y", "N 20 0.0167"]))
                # Read long before it shows, it shows at once.
                self.assertEqual(self.texture_alpha(log[0], picture), 255)
                alphas = [self.texture_alpha(f, picture) for f in log[40:]]
                self.assertLess(alphas[0], 255 * 0.15)
                self.assertEqual(alphas, sorted(alphas))
                self.assertTrue(all(b - a < 255 * 0.3 for a, b in zip(alphas, alphas[1:])),
                                alphas)
                # About 200 ms: full by the thirteenth frame.
                self.assertEqual(alphas[13:], [255] * 7)
                if fallbacks:
                    self.assertEqual(log[40].count("B 24\n"), fallbacks)
                    self.assertEqual(log[-1].count("B 24\n"), 0)
                if picture.startswith("still:"):
                    self.assertIsNotNone(self.texture_alpha(log[40], "G021E0"))
                    self.assertIsNone(self.texture_alpha(log[-1], "G021E0"))
        # Off shows it at once.
        log = frames(self.run_script(["M 2", "L 0 40 20", "N 5 0.0167", "Y", "N 1 0.0167"]))
        self.assertEqual(self.texture_alpha(log[-1], "G020E0"), 255)

    def test_a_stand_in_gives_way_as_its_picture_arrives(self):
        """What stood in for a picture shows only as much as the arriving
        picture does not cover yet: the two read as one card at the card's
        own strength, so nothing of the stand-in is left to vanish when the
        picture is whole. The cards either side are drawn at less than full
        strength, which is where it showed."""
        log = frames(self.run_script(["W", "L 0 40 20", "N 40 0.0167", "Y",
                                      "N 20 0.0167"]))[40:]
        for game in ("G019E0", "G020E0", "G021E0"):
            with self.subTest(game=game):
                shown = []
                for frame in log:
                    picture = self.texture_alpha(frame, game) / 255
                    stand_in = (self.texture_alpha(frame, "banner:" + game) or 0) / 255
                    shown.append(round(picture + stand_in * (1 - picture), 3))
                self.assertIsNotNone(self.texture_alpha(log[5], "banner:" + game))
                self.assertTrue(all(abs(s - shown[-1]) < 0.01 for s in shown), shown)
        # Spotlight: a still that arrives as the row moves fades in over its
        # cover, and the two come in together, never more and then less.
        log = frames(self.run_script(["L 3 40 21", "N 40 0.0167", "P 22 1 0 0", "Y 22",
                                      "N 30 0.0167"]))[40:]
        shown = []
        for frame in log:
            still = (self.texture_alpha(frame, "still:G022E0") or 0) / 255
            cover = (self.texture_alpha(frame, "G022E0") or 0) / 255
            shown.append(still + cover * (1 - still))
        self.assertIsNotNone(self.texture_alpha(log[5], "G022E0"))
        self.assertIsNone(self.texture_alpha(log[-1], "G022E0"))
        self.assertTrue(all(b >= a - 0.005 for a, b in zip(shown, shown[1:])), shown)

    def test_spotlight_keeps_the_old_picture_as_a_new_still_arrives(self):
        """The old picture only ever fades as the row moves: when the new
        still arrives part way through the move and takes over from its
        cover, the old one does not step back up under it; and opening
        Detail part way through fades it with the new one, rather than
        showing more of it as the panel fades."""
        def old_shown(log):
            shown = []
            for frame in log:
                old = (self.texture_alpha(frame, "still:G021E0") or 0) / 255
                new = (self.texture_alpha(frame, "still:G022E0") or 0) / 255
                shown.append(round(old * (1 - new), 3))
            return shown
        shown = old_shown(frames(self.run_script(
            ["L 3 40 21", "N 40 0.0167", "P 22 1 0 0", "Y 22", "N 60 0.0167"]))[40:])
        self.assertGreater(shown[0], 0.9)
        self.assertEqual(shown[-1], 0)
        self.assertTrue(all(b <= a + 0.005 for a, b in zip(shown, shown[1:])), shown)
        # The old picture against the new one: their share of the mix is
        # the move's, however far the panel has faded.
        mix = []
        for frame in frames(self.run_script(
                ["L 3 40 21", "N 40 0.0167", "P 22 1 0 0", "N 6 0.0167", "D 1",
                 "N 30 0.0167"]))[45:]:
            old = (self.texture_alpha(frame, "still:G021E0") or 0) / 255
            new = (self.texture_alpha(frame, "still:G022E0") or 0) / 255
            if new > 0.05:
                mix.append(round(old * (1 - new) / new, 3))
        self.assertGreater(len(mix), 10)
        self.assertTrue(all(b <= a + 0.01 for a, b in zip(mix, mix[1:])), mix)

    def test_moving_cards_change_size_one_way(self):
        """A step moves every card between two poses: its size along the
        strip changes one way, never a pixel back and forth as its corners
        round. Each card goes a whole pixel of its farthest-moving corner at
        a time."""
        width = lambda box: box[2] - box[0]
        height = lambda box: box[3] - box[1]
        for script, sizes in ((["L 0 40 20", "P 21 1 0 0"], (width,)),
                              (["L 1 40 20", "P 21 1 0 0"], (height,)),
                              (["L 2 40 12", "P 13 1 0 0"], (width, height)),
                              (["L 2 40 12", "P 17 1 0 0"], (width, height))):
            with self.subTest(script=script):
                log = [covers(f) for f in frames(self.run_script(
                    [script[0], "N 40 0.0167", script[1], "N 50 0.0167"]))[40:]]
                games = set.intersection(*(set(f) for f in log))
                self.assertGreater(len(games), 1)
                for game in games:
                    for size in sizes:
                        seen = [size(f[game][0]) for f in log if len(f[game]) == 1]
                        steps = [b - a for a, b in zip(seen, seen[1:]) if abs(b - a) > 0.001]
                        self.assertTrue(all(step > 0 for step in steps) or
                                        all(step < 0 for step in steps), (game, seen))

    def test_moving_cards_edges_move_one_way(self):
        """Through a step each corner of every card moves one way to rest,
        never a pixel back as the spring's tail crosses half pixels: a card
        whose edge steps back and forth shakes before it stops."""
        for script in (["L 0 40 20", "P 21 1 0 0"], ["L 0 40 20", "P 19 -1 0 0"],
                       ["L 1 40 20", "P 21 1 0 0"], ["L 3 40 20", "P 21 1 0 0"],
                       ["L 2 40 12", "P 13 1 0 0"], ["L 2 40 12", "P 17 1 0 0"]):
            with self.subTest(script=script):
                log = frames(self.run_script([script[0], "N 40 0.0167", script[1],
                                              "N 50 0.0167"]))[40:]
                corners = [shells(f) for f in log]
                games = set.intersection(*({game for game, boxes in covers(f).items()
                                            if len(boxes) == 1} & set(c)
                                           for f, c in zip(log, corners)))
                self.assertGreater(len(games), 1)
                for game in games:
                    for corner in range(4):
                        for axis in (0, 1):
                            seen = [f[game][corner][axis] for f in corners]
                            steps = [b - a for a, b in zip(seen, seen[1:]) if b != a]
                            self.assertTrue(all(step > 0 for step in steps) or
                                            all(step < 0 for step in steps),
                                            (game, corner, "xy"[axis], seen))

    def test_spotlight_mixes_stills_without_a_dip(self):
        """Still to still, the old one stays whole under the new one as it
        fades in, so the panel never shows through half way; still to cover,
        the old still fades out round the smaller cover."""
        log = frames(self.run_script(["L 3 40 21", "N 40 0.0167", "P 22 1 0 0",
                                      "N 40 0.0167"]))[40:]
        mixed = [(self.texture_alpha(f, "still:G021E0"), self.texture_alpha(f, "still:G022E0"))
                 for f in log]
        during = [(old, new) for old, new in mixed if old is not None]
        self.assertGreater(len(during), 5)
        self.assertTrue(all(old == 255 for old, _ in during), during)
        news = [new for _, new in mixed]
        self.assertEqual(news, sorted(news))
        self.assertEqual(mixed[-1], (None, 255))
        log = frames(self.run_script(["L 3 40 22", "N 40 0.0167", "P 23 1 0 0",
                                      "N 40 0.0167"]))[40:]
        olds = [self.texture_alpha(f, "still:G022E0") for f in log]
        olds = [alpha for alpha in olds if alpha is not None]
        self.assertGreater(len(olds), 5)
        self.assertEqual(olds, sorted(olds, reverse=True))
        self.assertLess(olds[-1], 128)

    def jewel_cases(self, frame: str) -> list:
        """The cases a frame hands the jewel case's renderer: (slot, game,
        print, print's strength, strength, print's square, direction)."""
        found = []
        for line in frame.splitlines():
            if line.startswith("J "):
                f = line.split()
                found.append((float(f[1]), int(f[2]), f[3], float(f[4]), float(f[5]),
                              tuple(map(float, f[6:10])), int(f[10])))
        return found

    def test_jewel_case_shows_one_case_and_two_while_moving(self):
        log = self.run_script(["L 4 40 20", "N 40 0.0167"])
        last = frames(log)[-1]
        self.assertEqual(self.jewel_cases(last),
                         [(0.0, 20, "G020E0", 1.0, 1.0, (0.0, 0.125, 0.75, 0.875), 0)])
        # No cards: the case stands where the carousel's covers were, its
        # game's title and publisher under it.
        self.assertEqual(covers(last), {})
        self.assertIn("S 320 350 ", last)
        self.assertIn("Game number 20", last)
        self.assertIn("S 320 70 0.500 1 ", last)
        self.assertIn("H 320 428 ", last)
        # On to the next game: the old case leaves as the new one arrives.
        log = self.run_script(["L 4 40 20", "N 40 0.0167", "P 21 1 0 0", "N 4 0.0167"])
        moving = [self.jewel_cases(frame) for frame in frames(log)[-4:]]
        for cases in moving:
            self.assertEqual(sorted(c[1] for c in cases), [20, 21])
            leaving = next(c for c in cases if c[1] == 20)
            arriving = next(c for c in cases if c[1] == 21)
            self.assertLess(leaving[0], 0.0)
            self.assertGreater(arriving[0], 0.0)
            self.assertEqual({c[6] for c in cases}, {1})
        self.assertLess(moving[-1][0][0] if moving[-1][0][1] == 21 else moving[-1][1][0],
                        moving[0][0][0] if moving[0][0][1] == 21 else moving[0][1][0])
        log = self.run_script(["L 4 40 20", "N 40 0.0167", "P 19 -1 0 0", "N 3 0.0167"])
        cases = self.jewel_cases(frames(log)[-1])
        self.assertEqual(sorted((c[1], c[0] > 0.0, c[6]) for c in cases),
                         [(19, False, -1), (20, True, -1)])
        log = self.run_script(["L 4 40 20", "N 40 0.0167", "P 21 1 0 0", "N 90 0.0167"])
        self.assertEqual([c[:3] for c in self.jewel_cases(frames(log)[-1])],
                         [(0.0, 21, "G021E0")])
        # A game without a cover has a bare disc.
        log = self.run_script(["L 4 40 24", "N 40 0.0167"])
        self.assertEqual([c[2] for c in self.jewel_cases(frames(log)[-1])], ["-"])
        # The case gives way as Detail opens, and comes back as it closes.
        log = self.run_script(["L 4 40 20", "N 40 0.0167", "D 1", "N 60 0.0167"])
        strengths = [sum(c[4] for c in self.jewel_cases(frame)) for frame in frames(log)[40:]]
        self.assertEqual(strengths[-1], 0.0)
        self.assertTrue(all(a >= b for a, b in zip(strengths, strengths[1:])))
        log = self.run_script(["L 4 40 20", "N 40 0.0167", "D 1", "N 60 0.0167", "D 0",
                               "N 60 0.0167"])
        self.assertEqual([c[:5] for c in self.jewel_cases(frames(log)[-1])],
                         [(0.0, 20, "G020E0", 1.0, 1.0)])
        self.assertEqual(covers(frames(log)[-1]), {})

    def test_jewel_case_prints_the_disc_first(self):
        """A game discs.pak has gets its own disc over the whole disc, bare
        while it is read; any other game, the middle of its cover."""
        log = self.run_script(["L 4 40 22", "N 40 0.0167"])
        self.assertEqual([c[2:6] for c in self.jewel_cases(frames(log)[-1])],
                         [("disc:G022E0", 1.0, 1.0, (0.0, 0.0, 1.0, 1.0))])
        log = self.run_script(["L 4 40 21", "N 40 0.0167"])
        self.assertEqual([c[2:6] for c in self.jewel_cases(frames(log)[-1])],
                         [("G021E0", 1.0, 1.0, (0.0, 0.125, 0.75, 0.875))])
        log = self.run_script(["Q", "L 4 40 22", "N 40 0.0167"])
        self.assertEqual([c[2] for c in self.jewel_cases(frames(log)[-1])], ["-"])

    def test_jewel_case_without_its_case_is_the_carousel(self):
        jewel = self.run_script(["K", "L 4 40 20", "N 40 0.0167"])
        carousel = self.run_script(["L 0 40 20", "N 40 0.0167"])
        self.assertNotIn("J ", jewel)
        self.assertEqual(set(covers(frames(jewel)[-1])), {f"G{i:03d}E0" for i in range(18, 23)})
        self.assertEqual(frames(jewel)[-1], frames(carousel)[-1])

    def test_every_layout_flies_to_detail(self):
        for layout in (0, 1, 2, 3, 4):
            with self.subTest(layout=layout):
                log = self.run_script([f"L {layout} 40 18", "N 30 0.0167", "D 1",
                                       "N 60 0.0167"])
                rest = covers(frames(log)[-1])
                self.assertEqual(set(rest), {"G018E0"})
                box = rest["G018E0"][0]
                self.assertAlmostEqual(box[0], 48 + 6, delta=1.0)
                self.assertAlmostEqual(box[2], 228 - 6, delta=1.0)
                self.assertAlmostEqual(box[1], 96 + 8, delta=1.0)
                self.assertAlmostEqual(box[3], 336 - 8, delta=1.0)
                self.assertNotIn("H 320 428", frames(log)[-1])

    def test_off_and_reduced_snap_like_the_carousel(self):
        for layout in (1, 2, 3, 4):
            with self.subTest(layout=layout):
                log = self.run_script(["M 2", f"L {layout} 40 17", "N 5 0.0167",
                                       "P 22 1 1 0", "N 1 0.0167"])
                self.assertEqual(frames(log)[-1], self.run_script(
                    ["M 2", f"L {layout} 40 22", "N 5 0.0167"]).split("F\n")[-1])

    def test_staged_clipping_matches_release_and_keeps_input_immutable(self):
        for layout in range(5):
            with self.subTest(layout=layout):
                log = self.run_script([f"L {layout} 40 17", "C", "N 1 0.0167"])
                self.assertEqual(len(frames(log)), 1)

    def test_publication_rejects_stale_layout_and_disposal(self):
        script = ["L 0 40 17", "N 5 0.0167", "T", "N 1 0.0167"]
        actual = frames(self.run_script(script))[-1]
        expected = frames(self.run_script([item for item in script if item != "T"]))[-1]
        self.assertEqual(actual, expected)

    def test_mutants_fail(self):
        work = Path(self.tmp.name)
        mutants = {
            "snapshot text is clipped in place": (
                "float scale = _GameflowPrepareDetailText(text, capacity, width, maximum, floor);",
                "float scale = _GameflowPrepareDetailText((char *)source, capacity, width, maximum, floor);"),
            "text preparation holds the video lock": (
                "_GameflowPrepareSnapshot(snapshot, &presentation);\n\tLWP_MutexLock(_videomutex);",
                "LWP_MutexLock(_videomutex);\n\t_GameflowPrepareSnapshot(snapshot, &presentation);"),
            "grid rows never wrap round the screen": ("int copies = rows >= 3u ? 1 : 0;",
                                                      "int copies = 0;"),
            "rows two away show": ("\tif(distance >= 2.0f) {\n\t\treturn 0.0f;\n\t}\n\treturn distance <= 1.0f ? 1.0f - 0.34f * distance :\n\t\t0.66f * (2.0f - distance);",
                                   "\tif(distance >= 3.0f) {\n\t\treturn 0.0f;\n\t}\n\treturn distance <= 1.0f ? 1.0f - 0.34f * distance :\n\t\t0.33f * (3.0f - distance);"),
            "the grid's far cards lose their covers": ("card->art = fabsf(row) < 1.5f;",
                                                       "card->art = distance < 1.5f;"),
            "the column uses the carousel's poses": ("_GameflowSamplePoseIn(gameflowVerticalPoses, slot) :",
                                                     "_GameflowSamplePose(slot) :"),
            "the highlight stays put": ("_GameflowGridQuad(frame->columnPosition, 0.0f,\n\t\t1.0f);",
                                        "_GameflowGridQuad(0.0f, 0.0f,\n\t\t1.0f);"),
            "the row's second covers stay hidden": ("fabsf(slot) < 1.5f : fabsf(slot) < 3.0f;",
                                                    "fabsf(slot) < 1.5f : fabsf(slot) < 1.5f;"),
            "Spotlight's row uses the carousel's poses": (
                "_GameflowSamplePoseIn(gameflowSpotlightPoses, slot) :", "_GameflowSamplePose(slot) :"),
            "Spotlight's picture ignores the still": (
                "texture = _GameflowStillTexture(record);", "texture = NULL;"),
            "the description is wrapped too wide": (
                "text, GAMEFLOW_SPOTLIGHT_COLUMN_W, GAMEFLOW_SPOTLIGHT_TEXT_SCALE,",
                "text, 4 * GAMEFLOW_SPOTLIGHT_COLUMN_W, GAMEFLOW_SPOTLIGHT_TEXT_SCALE,"),
            "a game without a description leaves the column blank": (
                "memcpy(text, GAMEFLOW_SPOTLIGHT_NO_DESCRIPTION,", "(void)(text, GAMEFLOW_SPOTLIGHT_NO_DESCRIPTION,"),
            "the banner's line breaks stay": ("if(c == '\\r' || c == '\\n' || c == '\\t') {",
                                             "if(false) {"),
            "the description is read past the banner": ("in < sizeof(text) && description[in]",
                                                       "in < 2u * sizeof(text) && description[in]"),
            "the jewel case shows the ring's cards": (
                "presence = !focused ? 0.0f :", "presence = !focused ? presence :"),
            "the jewel case draws only the selected case": (
                "!(fabsf(slot) < 1.0f)) {", "!(fabsf(slot) < 0.001f)) {"),
            "the disc carries the whole canvas": (
                "{0.0f, 0.125f, 0.75f, 0.875f};", "{0.0f, 0.0f, 1.0f, 1.0f};"),
            "the jewel case ignores discs.pak": (
                "if(_GameflowDiscTexture(record, &shot->print, &shot->printAlpha)) {",
                "if(false) {"),
        }
        tests = (self.test_staged_clipping_matches_release_and_keeps_input_immutable,
                 self.test_grid_rows_and_highlight, self.check_all_grid_moves,
                 self.test_vertical_column, self.test_horizontal_shows_two_covers_either_side,
                 self.test_spotlight_row_picture_and_column, self.test_spotlight_moves_never_jump,
                 self.test_spotlight_never_leaves_the_column_blank,
                 self.test_jewel_case_shows_one_case_and_two_while_moving,
                 self.test_jewel_case_prints_the_disc_first)
        original = self.binary
        try:
            for index, (name, (old, new)) in enumerate(mutants.items()):
                with self.subTest(mutant=name):
                    self.assertEqual(self.frame_c.count(old), 1, name)
                    self.binary = build(work, GUI, self.frame_c.replace(old, new),
                                        self.frame_h, True, f"mutant{index}")
                    failed = False
                    for test in tests:
                        try:
                            test()
                        except AssertionError:
                            failed = True
                            break
                    self.assertTrue(failed, f"mutant escaped: {name}")
        finally:
            self.binary = original


if __name__ == "__main__":
    unittest.main(verbosity=2)
