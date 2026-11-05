#!/usr/bin/env python3
"""Run Memory Cards' cube screen against a checked GX stream.

_DrawSaveCubes and its helpers are compiled out of FrameBufferMagic.c with
the real cubes (ui_save_cubes.c), springs (ui_motion.c), icon clock
(ui_saves.c) and screen shapes (ui_stage.c). GX, the font, the backdrop and
the pictures are stand-ins that check every primitive is declared and sent
in full under the pipeline it needs, and log what is drawn. The tests then
check:

- every resting cube's faces go in one batch, then their icons, then the
  focused cube's faces and its icon, last;
- the texture cache is cleared once a frame when an icon is drawn, never
  when none is, and every icon is a 32x32 RGB5A3 frame of a save's slot;
- icons keep their own colors under any Menu Color while the cubes turn;
- a row outside the window, a clear cube and a stack with no grid draw
  nothing; every face is wound clockwise;
- Motion Off is still; the busiest frame stays inside its budget.
"""

import os
import math
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"


def between(source: str, start: str, end: str, inclusive: bool = False) -> str:
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last + (len(end) if inclusive else 0)]


PRELUDE = r"""
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_save_cubes.h"
#include "ui_stage.h"
#include "ui_folder.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { u32 val[8]; } GXTexObj;
typedef struct uiDrawObj { int type; void *data; struct uiDrawObj *child; bool disposed; } uiDrawObj_t;
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2
enum { GX_QUADS = 0x80, GX_TRIANGLES = 0x90, GX_VTXFMT0 = 0, GX_TF_RGB5A3 = 5,
	GX_CLAMP = 0, GX_FALSE = 0, GX_TEXMAP0 = 0, GX_BM_NONE = 0, GX_BM_BLEND = 1,
	GX_BL_SRCALPHA = 4, GX_BL_INVSRCALPHA = 5, GX_LO_CLEAR = 0, GX_NEAR = 0,
	GX_LINEAR = 1 };
static const GXColor settingsInk = {241, 244, 255, 255};
static const GXColor settingsQuiet = {173, 187, 216, 255};
static const GXColor settingsSwatch = {122, 104, 224, 255};

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #c); exit(73); } } while(0)
/* The icons' texels: a save's eight frames, as the pool holds them. */
static u8 texels[8 * 2048] __attribute__((aligned(32)));
static u8 banner[6144] __attribute__((aligned(32)));
/* drawInit's textured pipeline, the raster one over it, or the icons'
 * (one TEV stage, blended by alpha); DIRTY after text or a picture. */
enum { DIRTY, TEXTURED, RASTER, ICONS };
static int pipeline = DIRTY, stages = 2, primitive;
static bool active, loaded, shift;
static int declared, emitted, phase;
static void drawInit(void) { CHECK(!active); pipeline = TEXTURED; stages = 2; loaded = false; }
static void _SetupRasterColor(void) { CHECK(!active && pipeline == TEXTURED); pipeline = RASTER; }
/* Menu Color: a non-Indigo one turns every color it is given. */
static void UIColor_Apply(u8 *r, u8 *g, u8 *b)
{
	if(shift) { u8 red = *r; *r = *b; *b = *g; *g = (u8)(red / 2); }
}
static void GX_SetNumTevStages(int n) { CHECK(!active); stages = n; }
static void GX_SetBlendMode(int mode, int source, int destination, int op)
{
	(void)op;
	CHECK(!active && mode != GX_BM_NONE);
	if(pipeline == TEXTURED && stages == 1 && source == GX_BL_SRCALPHA &&
		destination == GX_BL_INVSRCALPHA) pipeline = ICONS;
}
static void GX_InvalidateTexAll(void) { CHECK(!active); printf("I\n"); }
static void GX_InitTexObj(GXTexObj *object, void *pointer, int width, int height,
	int format, int s, int t, int mip)
{
	(void)object;
	CHECK(!active && ((uintptr_t)pointer & 31u) == 0 && mip == GX_FALSE);
	printf("T %ld %d %d %d %d %d\n", (long)((u8 *)pointer - texels), width, height,
		format, s, t);
}
static void GX_InitTexObjFilterMode(GXTexObj *object, int minimum, int magnify)
{
	(void)object; (void)minimum; (void)magnify;
}
static void GX_LoadTexObj(GXTexObj *object, int map)
{
	(void)object;
	CHECK(!active && pipeline == ICONS && map == GX_TEXMAP0);
	loaded = true;
	printf("L\n");
}
static float lastX, lastY;
static u8 lastColor[4];
static void GX_Begin(int type, int format, int count)
{
	CHECK(!active && format == GX_VTXFMT0 && count > 0);
	CHECK(count % (type == GX_QUADS ? 4 : 3) == 0);
	CHECK(pipeline == RASTER || (pipeline == ICONS && loaded && type == GX_QUADS && count == 4));
	active = true; declared = count; emitted = 0; phase = 0; primitive = type;
	printf("B %d %d %d\n", type, count, pipeline == ICONS);
}
static void GX_Position3f32(float x, float y, float z)
{
	CHECK(active && phase == 0 && z == 0.0f && isfinite(x) && isfinite(y));
	lastX = x; lastY = y; phase = 1;
}
static void GX_Color4u8(u8 r, u8 g, u8 b, u8 a)
{
	CHECK(active && phase == 1);
	lastColor[0] = r; lastColor[1] = g; lastColor[2] = b; lastColor[3] = a;
	phase = 2;
}
static void GX_TexCoord2f32(float s, float t)
{
	CHECK(active && phase == 2 && emitted < declared);
	CHECK(pipeline == ICONS || (s == 0.0f && t == 0.0f));
	printf("V %.3f %.3f %u %u %u %u %.1f %.1f\n", lastX, lastY, lastColor[0], lastColor[1],
		lastColor[2], lastColor[3], s, t);
	phase = 0; emitted++;
}
static void GX_End(void) { CHECK(active && phase == 0 && emitted == declared); active = false; }
static void _putFlatVertex(float x, float y, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(x, y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}
static void _putFlatRect(float x, float y, float width, float height, GXColor color)
{
	_putFlatVertex(x, y, color);
	_putFlatVertex(x + width, y, color);
	_putFlatVertex(x + width, y + height, color);
	_putFlatVertex(x, y + height, color);
}
static void drawStringMedium(int x, int y, const char *text, float scale, int align, GXColor color)
{
	(void)align;
	CHECK(!active && text != NULL && scale > 0.0f);
	printf("S %d %d %u %u %u %u %s\n", x, y, color.r, color.g, color.b, color.a, text);
	pipeline = DIRTY;
}
static void _DrawHintText(int x, int y, const char *text, float scale, int align, GXColor color)
{
	(void)align; (void)color;
	CHECK(!active && text != NULL && scale > 0.0f);
	printf("H %d %d %s\n", x, y, text);
	pipeline = DIRTY;
}
static int GetTextSizeInPixels(const char *text) { return 10 * (int)strlen(text); }
static void _DrawTexObjNow(GXTexObj *texture, int x, int y, int width, int height, int depth,
	float s1, float s2, float t1, float t2, int centered)
{
	(void)texture; (void)s1; (void)s2; (void)t1; (void)t2;
	CHECK(!active && pipeline == TEXTURED && depth == 0 && centered == 0);
	printf("P %d %d %d %d\n", x, y, width, height);
	pipeline = DIRTY;
}
static void _SavesFolder(int x, int y, GXColor color)
{
	(void)color;
	printf("G %d %d\n", x, y);
	pipeline = DIRTY;
}
static void _SaveCubesBackdrop(float paper, float handover)
{
	CHECK(!active && paper >= 0.0f && paper <= 1.0f && handover >= 0.0f && handover <= 1.0f);
	printf("K %.2f %.2f\n", paper, handover);
	pipeline = DIRTY;
}
static float animDelta;
static float UIAnim_Delta(void) { return animDelta; }
static uiMotionMode_t motionMode;
static uiMotionMode_t _CurrentMotionMode(void) { return motionMode; }
"""

DRIVER = r"""
static drawSaveCubesEvent_t page;
static uiDrawObj_t event = {0, &page, NULL, false};
static uiSavesArt_t art;

/* Stack s: cells of saves (each with an icon, but cell 6 not read yet), a
 * folder at cell 2 of the right stack, and free cells after saves. */
static void stack(int s, int cells, int first, int saves, unsigned listing)
{
	uiSaveCubesStack_t *stack = &page.snapshot.grid.stack[s];

	stack->cells = (s16)cells;
	stack->first = (s16)first;
	stack->listing = listing;
	for(int k = 0; k < UI_SAVE_CUBES_DRAWN; k++) {
		int cell = (first - 1) * 4 + k;
		uiSaveCubesCell_t *c = &stack->cell[k];

		memset(c, 0, sizeof(*c));
		c->kind = cell < saves ? (s == 1 && cell == 2 ? UI_SAVE_CUBES_KIND_FOLDER :
			UI_SAVE_CUBES_KIND_SAVE) : UI_SAVE_CUBES_KIND_EMPTY;
		if(c->kind == UI_SAVE_CUBES_KIND_SAVE && cell != 6) {
			c->texels = texels;
			c->art = &art;
		}
	}
	snprintf(page.snapshot.stack[s].name, sizeof(page.snapshot.stack[s].name), "%c", 'A' + s);
	snprintf(page.snapshot.stack[s].free, sizeof(page.snapshot.stack[s].free), "%d", 59);
	snprintf(page.snapshot.stack[s].note[0], 96, "Nothing is inserted in Slot %c.", 'A' + s);
	snprintf(page.snapshot.stack[s].note[1], 96, "Put one in, then press L or R.");
	page.snapshot.stack[s].noteScale[0] = 0.6f;
	page.snapshot.stack[s].noteScale[1] = 0.48f;
}

int main(void)
{
	char line[160];

	art.steps = 2;
	art.stepFrame[0] = 0;
	art.stepFrame[1] = 1;
	art.stepHold[0] = art.stepHold[1] = 1;
	art.period = 2;
	snprintf(page.snapshot.hint[0], 96, "STICK / D-PAD  Select   B  Finish   A  Confirm");
	snprintf(page.snapshot.hint[1], 96, "L/R  Card");
	while(fgets(line, sizeof(line), stdin)) {
		int a, b, c, d, e, f, g, h;
		float dt;
		if(sscanf(line, "S %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) stack(a, b, c, d, (unsigned)e);
		else if(sscanf(line, "C %d %d", &a, &b) == 2) {
			page.snapshot.grid.focusStack = (s8)a;
			page.snapshot.grid.focusCell = (s16)b;
		}
		else if(sscanf(line, "J %d %d", &a, &b) == 2) {
			uiSaveCubesStack_t *s = &page.snapshot.grid.stack[1];
			int k = 2 - (s->first - 1) * UI_SAVE_CUBES_COLUMNS;
			CHECK(k >= 0 && k < UI_SAVE_CUBES_DRAWN);
			s->cell[k].kind = (u8)a;
			s->cell[k].folderColor = (u8)b;
			s->cell[k].texels = a == UI_SAVE_CUBES_KIND_SAVE ? texels : NULL;
			s->cell[k].art = a == UI_SAVE_CUBES_KIND_SAVE ? &art : NULL;
		}
		else if(sscanf(line, "D %d", &a) == 1) {
			uiFolderSnapshot_t folder = {0};
			uiDrawObj_t dialog = {0, &folder, NULL, false};
			folder.lineCount = 9; folder.firstLine = a == 2 ? 3u : 0u; folder.color = 2u;
			for(unsigned n = 0; n < folder.lineCount; ++n)
				snprintf(folder.lines[n], sizeof(folder.lines[n]), "sda:/swiss/saves/Path line %u", n);
			snprintf(folder.summary, sizeof(folder.summary), "2 saves. Read-only card image.");
			snprintf(folder.contents[0], sizeof(folder.contents[0]), "Copper Archive");
			snprintf(folder.contents[1], sizeof(folder.contents[1]), "Moonlit Lake");
			if(a == 1) snprintf(folder.status, sizeof(folder.status), "Could not save. Check the Configuration Device.");
			printf("F\n"); drawInit(); _DrawMemoryCardFolder(&dialog);
			CHECK(!active && pipeline == TEXTURED);
		}
		else if(sscanf(line, "I %d %d %d", &a, &b, &c) == 3) {
			page.snapshot.info = (u8)a;
			page.snapshot.banner = b ? banner : NULL;
			page.snapshot.folder = (u8)c;
			snprintf(page.snapshot.line[0], 48, "Sky Harbor Racing");
			snprintf(page.snapshot.line[1], 48, "Cup 3 cleared");
			snprintf(page.snapshot.blocks, 16, "11");
		}
		else if(sscanf(line, "O %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
			/* An operation: kind, phase, serial, the left stack's cell it
			 * leaves and the right's it goes to. */
			uiSaveCubesOp_t *op = &page.snapshot.grid.op;
			op->kind = (u8)a;
			op->phase = (u8)b;
			op->serial = (u16)c;
			op->from = 0;
			op->fromCell = (s16)d;
			op->toCell = (s16)e;
			op->cube.texels = texels;
			op->cube.art = &art;
			op->cube.kind = UI_SAVE_CUBES_KIND_SAVE;
		}
		else if(sscanf(line, "G %d %d", &a, &b) == 2) {
			page.snapshot.grid.ghost = (u8)a;
			page.snapshot.grid.ghostCell = (s16)b;
		}
		else if(sscanf(line, "E %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
			/* A box: open, its items, the focus, whether titled, which dim. */
			uiSaveCubesMenu_t *menu = &page.snapshot.menu;
			static const char *const items[3] = {"Move", "Copy", "Erase"};
			page.snapshot.grid.menu = (u8)a;
			page.snapshot.grid.menuSerial++;
			page.snapshot.grid.menuFocus = (u8)c;
			menu->count = (u8)b;
			menu->dim = (u8)e;
			menu->width = 112;
			snprintf(menu->title, sizeof(menu->title), "%s", d ? "Copy to Slot B?" : "");
			for(f = 0; f < b && f < 3; ++f) snprintf(menu->item[f], sizeof(menu->item[f]), "%s", items[f]);
		}
		else if(sscanf(line, "Y %d", &a) == 1) {
			/* A message a px wide in the stand-in font, at its 0.56. */
			page.snapshot.grid.message = 1;
			b = (int)((float)a / (10.0f * 0.56f));
			memset(page.snapshot.message, 'x', (size_t)b);
			page.snapshot.message[b] = '\0';
		}
		else if(sscanf(line, "X %d", &a) == 1) {
			page.snapshot.grid.message = (u8)a;
			if(a) snprintf(page.snapshot.message, sizeof(page.snapshot.message), "Finished copying.");
			else page.snapshot.message[0] = '\0';
		}
		else if(sscanf(line, "Q %d", &a) == 1) page.snapshot.grid.leaving = (u8)a;
		else if(sscanf(line, "W %d", &a) == 1) UIStage_SetWide(a != 0);
		else if(sscanf(line, "M %d", &a) == 1) motionMode = (uiMotionMode_t)a;
		else if(sscanf(line, "U %d", &a) == 1) shift = a != 0;
		else if(line[0] == 'R') memset(&page.motion, 0, sizeof(page.motion));
		else if(sscanf(line, "N %d %f", &a, &dt) == 2) {
			for(b = 0; b < a; ++b) {
				animDelta = dt;
				printf("F\n");
				drawInit();	/* as videoDrawEvent does before each event */
				_DrawSaveCubes(&event);
				CHECK(!active && pipeline == TEXTURED);
			}
		}
		else { fprintf(stderr, "bad command: %s", line); (void)f; (void)g; (void)h; return 64; }
	}
	return 0;
}
"""


def screen(frame_c: str, frame_h: str) -> str:
    return "\n".join([
        between(frame_h, "typedef struct {\n\tchar control[32];", "} uiSaveCubesPageSnapshot_t;", True),
        between(frame_c, "/* What one frame's cubes need. */", "uiDrawObj_t* DrawSaveCubesPage("),
        between(frame_c, "static void _DrawMemoryCardFolder(", "uiDrawObj_t* DrawMemoryCardFolder("),
    ])


def parse(log: str) -> list[dict]:
    frames = []
    for line in log.splitlines():
        tag, _, rest = line.partition(" ")
        if tag == "F":
            frames.append({"events": [], "batches": [], "textures": [], "invalidations": 0})
            continue
        frame = frames[-1]
        if tag == "B":
            kind, count, icon = map(int, rest.split())
            frame["batches"].append({"kind": kind, "count": count, "icon": bool(icon),
                                     "vertices": [], "at": len(frame["events"])})
            frame["events"].append(("B", len(frame["batches"]) - 1))
        elif tag == "V":
            x, y, r, g, b, a, s, t = rest.split()
            frame["batches"][-1]["vertices"].append(
                (float(x), float(y), int(r), int(g), int(b), int(a), float(s), float(t)))
        elif tag == "T":
            offset, width, height, fmt, s, t = map(int, rest.split())
            frame["textures"].append({"offset": offset, "size": (width, height), "format": fmt,
                                      "wrap": (s, t), "loaded": False})
            frame["events"].append(("T", None))
        elif tag == "L":
            frame["textures"][-1]["loaded"] = True
            frame["events"].append(("L", None))
        elif tag == "I":
            frame["invalidations"] += 1
            frame["events"].append(("I", None))
        else:
            frame["events"].append((tag, rest))
    return frames


def quads(batch: dict) -> list[list[tuple]]:
    per = 4 if batch["kind"] == 0x80 else 3
    return [batch["vertices"][i:i + per] for i in range(0, len(batch["vertices"]), per)]


def area(quad: list[tuple]) -> float:
    return 0.5 * sum(quad[i][0] * quad[(i + 1) % 4][1] - quad[(i + 1) % 4][0] * quad[i][1]
                     for i in range(4))


def cube_batches(frame: dict) -> list[dict]:
    """Batches of cube faces: raster quads that aren't a box's 20 vertices."""
    return [b for b in frame["batches"] if not b["icon"] and b["kind"] == 0x80 and b["count"] != 20]


def icons(frame: dict) -> list[dict]:
    return [b for b in frame["batches"] if b["icon"]]


def width(batch: dict) -> float:
    xs = [v[0] for v in batch["vertices"]]
    return max(xs) - min(xs)


AT_REST = "S 0 40 1 30 1\nS 1 20 0 18 2\nC 0 5\nI 1 1 0\nN 120 0.0166667\n"
OPEN = 90   # frames: by 1.5 s the screen has opened


class SaveCubesGXStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frame_c = (GUI / "FrameBufferMagic.c").read_text()
        cls.frame_h = (GUI / "FrameBufferMagic.h").read_text()
        cls.cubes_c = (GUI / "ui_save_cubes.c").read_text()
        cls.source = screen(cls.frame_c, cls.frame_h)
        cls.directory = tempfile.TemporaryDirectory(prefix="swiss-save-cubes-gx-")
        cls.program = cls.build(cls.source, cls.cubes_c, "cubes")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    @classmethod
    def build(cls, source: str, cubes: str, name: str) -> Path:
        work = Path(cls.directory.name)
        harness = work / f"{name}.c"
        pure = work / f"{name}_cubes.c"
        program = work / name
        harness.write_text("\n".join([PRELUDE, source, DRIVER]))
        pure.write_text(cubes)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c99", "-Wall", "-Wextra", "-pedantic", "-Wno-unused-function", f"-I{GUI}",
            str(harness), str(pure), str(GUI / "ui_motion.c"), str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"),
            str(GUI / "ui_stage.c"), str(GUI / "ui_folder.c"), "-o", str(program), "-lm",
        ]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
        assert compiled.returncode == 0, compiled.stdout + compiled.stderr
        return program

    def run_script(self, script: str, program: Path | None = None) -> list[dict]:
        result = subprocess.run([str(program or self.program)], input=script,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        return parse(result.stdout)

    def test_folder_identity_color_survives_theme_in_both_shapes(self):
        for wide in (0, 1):
            for theme in (0, 1):
                with self.subTest(wide=wide, theme=theme):
                    frame = self.run_script(
                        f"W {wide}\nU {theme}\n" + AT_REST.replace("N 120", "J 1 2\nN 120"))[-1]
                    colors = {vertex[2:5] for batch in cube_batches(frame)
                              for vertex in batch["vertices"]}
                    self.assertIn((68, 170, 230), colors, "Azure folder rim keeps its path identity")
                    self.assertTrue(all(0 <= value <= 255 for color in colors for value in color))

    def test_folder_color_field_cannot_recolor_save_items(self):
        for wide in (0, 1):
            for theme in (0, 1):
                prefix = f"W {wide}\nU {theme}\n"
                zero = self.run_script(prefix + AT_REST.replace("N 120", "J 0 0\nN 120"))[-1]
                colored = self.run_script(prefix + AT_REST.replace("N 120", "J 0 2\nN 120"))[-1]
                self.assertEqual(zero, colored, "only folder cube identities use the color map")

    def test_memory_folder_dialog_draws_native_panel_content_and_controls(self):
        for wide in (0, 1):
            for theme in (0, 1):
                for state in (0, 1, 2):
                    with self.subTest(wide=wide, theme=theme, state=state):
                        frame = self.run_script(f"W {wide}\nU {theme}\nD {state}\n")[-1]
                        events = "\n".join(text for tag, text in frame["events"] if tag in ("S", "H"))
                        self.assertIn("FOLDER", events)
                        self.assertIn("FULL PATH", events)
                        self.assertIn("Read-only card image", events)
                        self.assertIn("Copper Archive", events)
                        self.assertIn("Moonlit Lake", events)
                        self.assertIn("Azure", events)
                        self.assertIn("Y  Reset   A  Save   B  Cancel", events)
                        self.assertIn("UP/DOWN  Scroll path", events)
                        if state == 1:
                            self.assertIn("Could not save", events)
                        path_lines = [text for tag, text in frame["events"] if tag == "S" and "Path line" in text]
                        self.assertEqual(len(path_lines), 6)
                        first = 3 if state == 2 else 0
                        self.assertTrue(path_lines[0].endswith(f"Path line {first}"))
                        self.assertTrue(path_lines[-1].endswith(f"Path line {first + 5}"))

    def assert_rest(self, frame: dict) -> None:
        """Slot A's 16 cubes in its window, Slot B's 16, the focus (A's cell
        5) last: one batch of resting faces, their icons, then the focus."""
        bodies = cube_batches(frame)
        self.assertEqual(len(bodies), 2, "resting faces, then the focused cube's")
        resting, focused = bodies
        # 31 resting cubes: 15 of A's saves and B's 16 cells (a folder, saves
        # and free cells). Each shows two to four faces' quads.
        self.assertGreater(len(quads(resting)), 31 * 3)
        self.assertGreater(width(focused), 56.0, "the focus is 1.5x")
        self.assertLess(width(focused), 80.0)
        drawn = icons(frame)
        # The saves in each window, less cell 6 (not read yet) of each, A's
        # focus and B's folder; then the focus's.
        self.assertEqual(len(drawn), (16 - 2) + (16 - 2) + 1)
        self.assertLess(drawn[-2]["at"], focused["at"], "the focus is drawn after the icons at rest")
        self.assertGreater(drawn[-1]["at"], focused["at"], "its icon after its faces")
        self.assertLess(resting["at"], drawn[0]["at"], "every face at rest before an icon")
        for quad in quads(resting) + quads(focused):
            if quad[2][5] == quad[3][5] == 0:
                self.assertLess(area(quad), 0.0, "coverage must extend outside its face")
                self.assertGreater(quad[0][5], 0)
                self.assertEqual(quad[0][2:6], quad[1][2:6])
                self.assertEqual(quad[0][2:5], quad[2][2:5])
            else:
                self.assertGreater(area(quad), 0.0, "a face wound the wrong way")
        self.assertEqual(frame["events"][0], ("K", "1.00 0.00"), "the backdrop, the screen open")

    def test_rest(self):
        for wide in (0, 1):
            with self.subTest(wide=wide):
                frames = self.run_script(f"W {wide}\n" + AT_REST)
                self.assert_rest(frames[-1])

    def test_one_pixel_silhouette_coverage_preserves_fill_and_icons(self):
        # Compile a hard-edge reference from the same real draw path. The
        # added strips must leave every original fill and icon unchanged.
        call = "int edges = UISaveCubes_Coverage(draw->quads + quads, n,\n\t\t\tUIStage_PixelWidth(), draw->coverage + coverage);"
        self.assertIn(call, self.source)
        hard = self.build(self.source.replace(call, "int edges = 0;"), self.cubes_c, "hard")
        for wide in (0, 1):
            script = f"W {wide}\n" + AT_REST
            smooth = self.run_script(script)[-1]
            original = self.run_script(script, hard)[-1]
            self.assertEqual([b["vertices"] for b in icons(smooth)],
                             [b["vertices"] for b in icons(original)])
            self.assertEqual(smooth["textures"], original["textures"])
            strips = []
            for current, reference in zip(cube_batches(smooth), cube_batches(original)):
                fills = [q for q in quads(current) if q[2][5] != 0]
                self.assertEqual(fills, quads(reference))
                strips.extend(q for q in quads(current) if q[2][5] == 0)
            self.assertGreater(len(strips), 32 * 3, "the cube outlines have no coverage")
            pixel = 4.0 / 3.0 if wide else 1.0
            for strip in strips:
                dx = (strip[1][0] - strip[0][0]) / pixel
                dy = strip[1][1] - strip[0][1]
                length = math.hypot(dx, dy)
                for inner, outer in ((0, 3), (1, 2)):
                    x = (strip[outer][0] - strip[inner][0]) / pixel
                    y = strip[outer][1] - strip[inner][1]
                    self.assertAlmostEqual((dy * x - dx * y) / length, 1.0, delta=0.003)
                    self.assertEqual(strip[outer][5], 0)

    def test_coverage_fades_with_its_cube(self):
        frames = self.run_script("W 1\n" + AT_REST +
                                 "S 0 40 2 30 1\nC 0 9\nN 12 0.0166667\n")
        partial = False
        for frame in frames:
            for batch in cube_batches(frame):
                fill = [q for q in quads(batch) if area(q) > 0.0]
                for strip in (q for q in quads(batch) if area(q) < 0.0):
                    self.assertEqual(strip[0][2:6], strip[1][2:6])
                    self.assertEqual((strip[2][5], strip[3][5]), (0, 0))
                    # Coverage starts at the original boundary and carries
                    # exactly its color and fade, including translucent cells.
                    matching = [q for q in fill for v in range(4)
                                if q[v] == strip[0] and q[(v + 1) % 4] == strip[1]]
                    self.assertEqual(len(matching), 1)
                    partial |= 0 < strip[0][5] < 150
        self.assertTrue(partial, "the test never reached a fading silhouette")

    def test_one_invalidation_a_frame_and_icon_textures(self):
        frames = self.run_script(AT_REST)[OPEN:]
        for frame in frames:
            self.assertEqual(frame["invalidations"], 1)
            for texture in frame["textures"]:
                if not texture["loaded"]:
                    continue	# the info bar's banner
                self.assertEqual((texture["size"], texture["format"], texture["wrap"]),
                                 ((32, 32), 5, (0, 0)))
                self.assertEqual(texture["offset"] % 2048, 0)
                self.assertTrue(0 <= texture["offset"] < 8 * 2048)
        # The icons animate: both frames show over two ticks.
        offsets = {t["offset"] for f in frames for t in f["textures"] if t["loaded"]}
        self.assertEqual(offsets, {0, 2048})
        # No icon, no clearing: free cells and folders only.
        frames = self.run_script("S 0 16 0 0 1\nS 1 16 0 0 2\nC 0 0\nN 10 0.0166667\n")
        for frame in frames:
            self.assertEqual(frame["invalidations"], 0)
            self.assertEqual(icons(frame), [])

    def test_icons_keep_their_colors(self):
        plain = self.run_script(AT_REST)[-1]
        turned = self.run_script("U 1\n" + AT_REST)[-1]
        for batch in icons(turned):
            for vertex in batch["vertices"]:
                self.assertEqual(vertex[2:5], (255, 255, 255))
        self.assertNotEqual([v[2:6] for v in cube_batches(plain)[0]["vertices"]],
                            [v[2:6] for v in cube_batches(turned)[0]["vertices"]],
                            "the cubes follow Menu Color")

    def test_what_draws_nothing(self):
        # A stack with no grid draws no cube, only why.
        frames = self.run_script(f"S 0 0 0 0 1\nS 1 16 0 5 2\nC 1 0\nN {OPEN} 0.0166667\n")
        frame = frames[-1]
        notes = [e for e in frame["events"] if e[0] == "S" and "Nothing is inserted" in e[1]]
        self.assertEqual(len(notes), 1)
        for batch in cube_batches(frame) + icons(frame):
            for vertex in batch["vertices"]:
                self.assertGreater(vertex[0], 320.0)
        # Rows outside the window fade while it scrolls, then draw nothing.
        frames = self.run_script(AT_REST + "S 0 40 2 30 1\nC 0 9\nN 3 0.0166667\nN 120 0.0166667\n")
        alphas = [v[5] for b in cube_batches(frames[121]) for v in b["vertices"]]
        self.assertTrue(any(0 < a < 200 for a in alphas), "no row faded")
        self.assertTrue(all(84.0 < v[1] < 370.0 for b in cube_batches(frames[-1])
                            for v in b["vertices"]))
        self.assertEqual(len(icons(frames[-1])), (16 - 1) + (16 - 2) + 1)

    def test_the_info_bar(self):
        base = "S 0 40 1 30 1\nS 1 20 0 18 2\nC 0 5\n"
        def shown(info):
            frame = self.run_script(base + info + f"N {OPEN} 0.0166667\n")[-1]
            return [e for e in frame["events"] if e[0] in ("P", "G")], \
                any(e[0] == "S" and "Sky Harbor" in e[1] for e in frame["events"])
        # The banner; a save without one shows its icon in its place, as the
        # IPL; a folder its picture; a free cell nothing at all.
        self.assertEqual(shown("I 1 1 0\n"), ([("P", "56 381 96 32")], True))
        self.assertEqual(shown("I 1 0 0\n"), ([("P", "88 381 32 32")], True))
        self.assertEqual(shown("I 1 0 1\n"), ([("G", "56 381")], True))
        self.assertEqual(shown("I 0 0 0\n"), ([], False))

    def test_motion_off_is_still(self):
        frames = self.run_script("M 2\n" + AT_REST)
        self.assertEqual(frames[-1]["batches"], frames[-2]["batches"])
        self.assertEqual({t["offset"] for f in frames for t in f["textures"] if t["loaded"]}, {0})
        self.assert_rest(frames[-1])

    def test_the_busiest_frame_holds_its_budget(self):
        # Both stacks full and scrolling, Menu Widescreen, the focus grown
        # and an info bar: the cubes, boxes and arrows a frame sends.
        frames = self.run_script("W 1\nS 0 128 4 127 1\nS 1 128 4 127 2\nC 0 17\nI 1 1 0\n"
                                 "N 120 0.0166667\nS 0 128 5 127 1\nS 1 128 5 127 2\nC 0 21\n"
                                 "N 6 0.0166667\n")
        frame = frames[-1]
        vertices = sum(len(b["vertices"]) for b in frame["batches"])
        self.assertGreater(len(icons(frame)), 32)
        self.assertLessEqual(vertices, 3500)
        self.assertLessEqual(len(frame["batches"]), 140)

    def test_no_depth_and_no_opaque_blend(self):
        self.assertNotIn("GX_SetZMode", self.source)
        self.assertNotIn("GX_BM_NONE", self.source)
        self.assertNotIn("sqrtf", self.cubes_c)

    def mutant(self, where: str, old: str, new: str) -> Path:
        source, cubes = self.source, self.cubes_c
        if where == "fbm":
            self.assertIn(old, source)
            source = source.replace(old, new, 1)
        else:
            self.assertIn(old, cubes)
            cubes = cubes.replace(old, new, 1)
        return self.build(source, cubes, "mutant")

    def test_opening_and_leaving(self):
        frames = self.run_script(AT_REST + "Q 1\nN 30 0.0166667\n")
        first, opened, gone = frames[0], frames[OPEN], frames[-1]
        # The first frame: the Home cube where Home left it, no paper, no
        # cubes and no words yet.
        self.assertEqual(first["events"][0], ("K", "0.00 1.00"))
        self.assertEqual(cube_batches(first) + icons(first), [])
        self.assertFalse([e for e in first["events"] if e[0] in ("S", "H")])
        # Opened: the paper, every cube and the hints; the Home cube gone.
        self.assertEqual(opened["events"][0], ("K", "1.00 0.00"))
        self.assertTrue([e for e in opened["events"] if e[0] == "H"])
        # Left: the Home cube back, the paper, words and cubes gone.
        self.assertEqual(gone["events"][0], ("K", "0.00 1.00"))
        self.assertEqual(cube_batches(gone) + icons(gone), [])
        self.assertFalse([e for e in gone["events"] if e[0] in ("S", "H")])

    def menu_frame(self, script: str, program: Path | None = None) -> tuple[dict, list]:
        frame = self.run_script(AT_REST + script, program)[-1]
        strings = [(i, e[1].split(" ", 6)) for i, e in enumerate(frame["events"]) if e[0] == "S"]
        return frame, strings

    def test_the_box_beside_the_cube(self):
        # Move, Copy and Erase beside the focused cube (left stack, cell 5),
        # Move dimmed, Copy focused: after every cube, at x 192, from y 112.
        frame, strings = self.menu_frame("E 1 3 1 0 1\nN 15 0.0166667\n")
        items = [(int(x), int(y), (int(r), int(g), int(b)), text)
                 for _, (x, y, r, g, b, a, text) in strings if text in ("Move", "Copy", "Erase")]
        self.assertEqual(items, [(208, 132, (120, 120, 140), "Move"),
                                 (208, 156, (255, 236, 170), "Copy"),
                                 (208, 180, (255, 255, 255), "Erase")])
        last_cube = icons(frame)[-1]["at"]	# the focused cube's icon
        boxes = [b for b in frame["batches"] if b["count"] == 20 and b["at"] > last_cube]
        self.assertEqual(len(boxes), 1, "the box, over every cube")
        self.assertEqual(min(v[0] for v in boxes[0]["vertices"]), 192.0)
        self.assertEqual(min(v[1] for v in boxes[0]["vertices"]), 112.0)
        # A question: its title's box above the Yes/No.
        frame, strings = self.menu_frame("E 1 2 0 1 0\nN 15 0.0166667\n")
        self.assertTrue(any(text == "Copy to Slot B?" for _, (*_, text) in strings))
        last_cube = icons(frame)[-1]["at"]
        self.assertEqual(len([b for b in frame["batches"] if b["count"] == 20 and
                              b["at"] > last_cube]), 2)
        # Closed, it fades in 0.1 s.
        frames = self.run_script(AT_REST + "E 1 3 1 0 1\nN 15 0.0166667\nE 0 3 1 0 1\n"
                                 "N 3 0.0166667\nN 10 0.0166667\n")
        fading = [int(e[1].split(" ", 6)[5]) for e in frames[-11]["events"]
                  if e[0] == "S" and e[1].endswith("Erase")]
        self.assertTrue(fading and 0 < fading[0] < 255)
        self.assertFalse([e for e in frames[-1]["events"] if e[0] == "S" and
                          e[1].endswith("Erase")])
        # Mutants: a dimmed item drawn as any other; the box under the cubes.
        program = self.mutant("fbm", "GXColor ink = (menu->dim >> i) & 1u ?",
                              "GXColor ink = 0 ?")
        _, strings = self.menu_frame("E 1 3 1 0 1\nN 15 0.0166667\n", program)
        self.assertTrue(any(text == "Move" and (r, g, b) == ("255", "255", "255")
                            for _, (x, y, r, g, b, a, text) in strings))
        floating = "\tfor(i = floating; i < count; i++) {\n\t\t_SaveCubesEmit(&data->draw, i, i + 1);\n\t}\n"
        menu = ("\tif(grid->menu) {\n\t\tdata->menu = s->menu;\n\t\tdata->menuFocus = grid->menuFocus;\n"
                "\t}\n\t_SaveCubesMenu(&data->menu, data->menuFocus, grid, &data->motion);\n")
        program = self.mutant("fbm", floating + menu, menu + floating)
        frame, _ = self.menu_frame("E 1 3 1 0 1\nN 15 0.0166667\n", program)
        self.assertEqual([b for b in frame["batches"] if b["count"] == 20 and
                          b["at"] > icons(frame)[-1]["at"]], [])

    def test_a_message(self):
        frames = self.run_script(AT_REST + "X 1\nN 15 0.0166667\nX 0\nN 3 0.0166667\n"
                                 "N 10 0.0166667\n")
        shown, fading, gone = frames[-14], frames[-11], frames[-1]
        def message(frame):
            return [e[1].split(" ", 6) for e in frame["events"]
                    if e[0] == "S" and e[1].endswith("Finished copying.")]
        (x, y, r, g, b, a, _), = message(shown)
        self.assertEqual((x, y, a), ("320", "225", "255"))
        self.assertLess(int(message(fading)[0][5]), 255)
        self.assertEqual(message(gone), [])
        box = [bt for bt in shown["batches"] if bt["count"] == 20 and
               min(v[1] for v in bt["vertices"]) == 200.0]
        self.assertEqual(len(box), 1)
        self.assertEqual(box[0]["vertices"][0][2:5], (120, 16, 36))
        # saves.c fits a message to UI_SAVE_CUBES_MESSAGE_WIDTH: as wide as
        # that, its box stays on the 4:3 stage, the info bar's margins.
        widest = int(re.search(r"#define UI_SAVE_CUBES_MESSAGE_WIDTH (\d+)",
                               (GUI / "ui_save_cubes.h").read_text()).group(1))
        shown = self.run_script(AT_REST + f"Y {widest}\nN 15 0.0166667\n")[-1]
        box, = [bt for bt in shown["batches"] if bt["count"] == 20 and
                min(v[1] for v in bt["vertices"]) == 200.0]
        self.assertGreaterEqual(min(v[0] for v in box["vertices"]), 40.0)
        self.assertLessEqual(max(v[0] for v in box["vertices"]), 600.0)
        self.assertGreater(max(v[0] for v in box["vertices"]), 590.0)

    def test_a_flight_and_a_burst(self):
        at_rest = self.run_script(AT_REST)[-1]
        # A copy from the left stack's cell 5 to the right's cell 2, 0.1 s
        # in: one cube more, floating over the rest, its icon after it.
        flying = self.run_script(AT_REST + "O 1 0 1 5 2\nN 7 0.0166667\n")[-1]
        self.assertEqual(len(cube_batches(flying)), len(cube_batches(at_rest)) + 1)
        self.assertEqual(len(icons(flying)), len(icons(at_rest)) + 1)
        self.assertGreater(icons(flying)[-1]["at"], cube_batches(flying)[-1]["at"])
        self.assertGreater(width(cube_batches(flying)[-1]), 56.0, "the flight grows toward the eye")
        # Erased: eight pieces a sixth of a cube, each a batch, no icons,
        # pale as the cube they were and over every other cube.
        burst = self.run_script(AT_REST + "O 3 0 2 5 0\nN 30 0.0166667\nO 3 1 2 5 0\n"
                                "N 3 0.0166667\n")[-1]
        pieces = [b for b in cube_batches(burst) if width(b) < 20.0]
        self.assertEqual(len(pieces), 8)
        self.assertEqual(pieces, cube_batches(burst)[-8:])
        for piece in pieces:
            self.assertGreater(width(piece), 8.0)
            self.assertGreater(max(v[2] for v in piece["vertices"]), 180)
        self.assertEqual(len(icons(burst)), len(icons(at_rest)))
        # The busiest such frame, in Menu Widescreen, holds the budget.
        frames = self.run_script("W 1\nS 0 128 4 127 1\nS 1 128 4 127 2\nC 0 17\nI 1 1 0\n"
                                 "N 120 0.0166667\nO 1 0 1 17 21\nS 0 128 5 127 1\n"
                                 "S 1 128 5 127 2\nC 0 21\nN 18 0.0166667\n")
        frame = frames[-1]
        self.assertLessEqual(sum(len(b["vertices"]) for b in frame["batches"]), 3500)
        self.assertLessEqual(len(frame["batches"]), 140)

    def test_mutations_are_rejected(self):
        fbm, pure = "fbm", "pure"
        mutations = {
            "a face's area read backwards": (pure, "if(cubesArea(x, y, cubesFace[i]) > 0.5f)",
                                             "if(cubesArea(x, y, cubesFace[i]) < -0.5f)"),
            "no texture cache clearing": (fbm, "\t\t\t\tGX_InvalidateTexAll();\n", ""),
            "the cache cleared for every icon": (
                fbm, "\t\tGX_LoadTexObj(&draw->icon, GX_TEXMAP0);",
                "\t\tGX_InvalidateTexAll();\n\t\tGX_LoadTexObj(&draw->icon, GX_TEXMAP0);"),
            "the focus drawn at rest": (pure, "floats[n++] = focused || g > CUBES_FLOATING || mine;",
                                        "floats[n++] = false;"),
            "icons through Menu Color": (
                fbm, "_SaveCubesVertex(quad->x[v], quad->y[v], white, s[v], t[v]);",
                "{ GXColor c = white; UIColor_Apply(&c.r, &c.g, &c.b);"
                " _SaveCubesVertex(quad->x[v], quad->y[v], c, s[v], t[v]); }"),
            "no window fade": (pure, "fade = outside <= 0.0f ? 1.0f : 1.0f - outside / CUBES_FADE_SPAN;",
                               "fade = 1.0f;"),
        }
        script = "U 1\n" + AT_REST
        for name, (where, old, new) in mutations.items():
            with self.subTest(mutation=name):
                source, cubes = self.source, self.cubes_c
                if where == fbm:
                    self.assertIn(old, source)
                    source = source.replace(old, new, 1)
                else:
                    self.assertIn(old, cubes)
                    cubes = cubes.replace(old, new, 1)
                program = self.build(source, cubes, "mutant")
                result = subprocess.run([str(program)], input=script, capture_output=True,
                                        text=True, timeout=60)
                if result.returncode != 0:
                    continue	# a stand-in caught it
                frames = parse(result.stdout)
                with self.assertRaises(AssertionError):
                    self.assert_rest(frames[-1])
                    for frame in frames:
                        self.assertEqual(frame["invalidations"], 1)
                        for batch in icons(frame):
                            for vertex in batch["vertices"]:
                                self.assertEqual(vertex[2:5], (255, 255, 255))


if __name__ == "__main__":
    unittest.main(verbosity=2)
