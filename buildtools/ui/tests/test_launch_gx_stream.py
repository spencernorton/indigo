#!/usr/bin/env python3
"""Run the real launch screen against a checked GX stream, in both screen shapes.

The Library renderer is compiled as test_gameflow_gx_stream.py compiles it,
with the launch screen's own state and ui_launch, and driven through a launch:
Game Detail, A, Swiss's progress messages, the hand-off, or a failure that
goes back to the Library. The tests then check that:

- the screen dims to the frame's edges in 4:3 and in 16:9, and the cover,
  the ring and the text sit where they do in both;
- the cover glides from Detail's pose to the centre, from every layout, and
  keeps its poster when the pack closes for the hand-off;
- the ring is a track and a fill within its vertex budget, the fill never
  falls as boxes come and go, and one caption replaces the file names;
- Animations Off draws a still ring that steps with the launch;
- a failed launch fades out and the Library comes back as it was.
"""

import math
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_gameflow_gx_stream import GUI, build, covers, frames

DRIVER = r"""
static drawGameflowEvent_t *eventData;
static uiDrawObj_t event;
static uiGameflowRenderSnapshot_t snapshot;
static uint32_t generation;
/* Y: the cards are apps, as Apps shows them. */
static bool apps;
static unsigned savesVariant = 1u;

/* L layout count selected | K (Detail's snapshot) | W wide | M motion
 * | D mode | S message (\n and \205 escaped) | C packClosed | E (hand-off)
 * | Z focus (Detail's row) | N frames dt -- the log has one "F" per frame. */
static void publish(int layout, uint32_t count, uint32_t selected)
{
	uiGameflowLibraryWindowSlot_t slots[25];
	size_t n;
	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.selection.generation = ++generation;
	snapshot.selection.itemCount = count;
	snapshot.selection.selectedIndex = selected;
	snapshot.layout = (u8)layout;
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		snapshot.columns = 5;
		n = UIGameflowLibrary_BuildGridWindow(count, selected, 5u,
			UI_GAMEFLOW_DIRECTION_NONE, slots);
	}
	else {
		n = UIGameflowLibrary_BuildWindow(count, selected,
			UI_GAMEFLOW_DIRECTION_NONE, slots);
	}
	snapshot.recordCount = (u32)n;
	for(size_t i = 0; i < n; ++i) {
		uiGameflowCardSnapshot_t *record = &snapshot.records[i];
		record->flags = UI_GAMEFLOW_CARD_VALID;
		record->libraryIndex = slots[i].index;
		record->relativeSlot = slots[i].relativeSlot;
		record->column = slots[i].column;
		snprintf(record->gameId, sizeof(record->gameId), "G%03uE0", slots[i].index);
		snprintf(record->title, sizeof(record->title), "Game number %u", slots[i].index);
		snprintf(record->company, sizeof(record->company), "Company %u", slots[i].index % 9u);
		if(apps) {
			record->flags |= UI_GAMEFLOW_CARD_APP;
			memset(record->gameId, 0, sizeof(record->gameId));
			snprintf(record->title, sizeof(record->title), "App %u", slots[i].index);
		}
	}
	CHECK(_GameflowSnapshotValid(&snapshot));
	memset(eventData, 0, sizeof(*eventData));
	UIGameflow_Init(&eventData->state);
	UIGameflow_SetColumns(&eventData->state, snapshot.columns);
	CHECK(UIGameflow_ApplySnapshot(&eventData->state, &snapshot.selection, motionMode));
	drawGameflowSnapshotPresentation_t presentation;
	_GameflowPrepareSnapshot(&snapshot, &presentation);
	_GameflowCopySnapshot(eventData, &snapshot, &presentation);
}

/* Detail's snapshot for the focused game, as DrawUpdateGameflowDetail
 * publishes it. */
static void detail(void)
{
	const uiGameflowFrame_t *frame = UIGameflow_Frame(&eventData->state);
	const uiGameflowCardSnapshot_t *record = _GameflowFindRecord(
		&eventData->snapshot, frame->focusIndex, NULL);
	/* The demonstration save in two places: one file, its blocks once,
	 * and its own line after the date. */
	uiSavesGameStats_t stats = {
		.saves = 2u, .files = 1u, .blocks = 2u, .latestUpdated = 762525240u,
		.sourceSaves = {0u, 0u, 2u}, .checkedSources = 4u, .updatedKnown = true
	};
	uiGameflowDetailCheatSource_t cheat = {"Infinite energy", true};
	char title[96];
	uiGameflowDetailSource_t source = {
		.generation = frame->generation, .focusIndex = frame->focusIndex,
		.company = "Detail publisher", .cheats = &cheat, .cheatCount = 1u,
		.flags = UI_GAMEFLOW_DETAIL_CAN_SETTINGS | UI_GAMEFLOW_DETAIL_CAN_CHEATS |
			UI_GAMEFLOW_DETAIL_CAN_LIBRARY | UI_GAMEFLOW_DETAIL_CHEATS_KNOWN
	};
	CHECK(record != NULL);
	snprintf(title, sizeof(title), "Detail title %s", record->gameId);
	source.title = title;
	source.gameId = record->gameId;
	source.saveLine = "SD card image";
	if(savesVariant == 2u) stats.partial = true;
	else if(savesVariant == 3u) stats.saves = stats.files = stats.blocks = UINT32_MAX;
	else if(savesVariant == 4u) stats.saves = stats.blocks = 0u;
	else if(savesVariant == 5u) stats.updatedKnown = false;
	else if(savesVariant == 6u) {
		stats.saves = stats.blocks = 0u;
		stats.partial = true;
	}
	else if(savesVariant == 7u) {
		stats.checkedSources = 0u;
		stats.updatedKnown = false;
	}
	else if(savesVariant == 8u) {
		stats.saves = 1u;
		stats.blocks = 2u;
	}
	source.saveStats = savesVariant == 0u ? NULL : &stats;
	if(savesVariant == 9u || savesVariant == 10u) {
		/* Left and Right chose the second of three: a Save Folder copy. */
		static uint8_t chosen[UI_SAVES_ENTRY_SIZE];
		chosen[0x28] = 0x2d; chosen[0x29] = 0x73; chosen[0x2a] = 0x36; chosen[0x2b] = 0x38;
		source.saveCopies = 3u;
		source.saveChoice = 2u;
		source.saveChoiceEntry = chosen;
		source.saveChoiceWhere = "Save Folder";
		source.saveChoiceLine = "SD card image";
	}
	if(savesVariant == 10u) {
		/* Copies of two files: the chosen one's file and its place among
		 * that file's copies, in an image. */
		stats.saves = 3u;
		stats.files = 2u;
		source.saveChoiceFile = "DobutsunomoriP_MURA";
		source.saveChoiceFileCopy = 2u;
		source.saveChoiceFileCopies = 2u;
		source.saveChoiceWhere = "MemoryCardA.USA.raw";
	}
	CHECK(UIGameflowDetail_Build(&eventData->detail, &source));
	/* The video event must never retain borrowed menu-thread storage. */
	memset(&stats, 0, sizeof(stats));

	_GameflowPrepareDetailPresentation(eventData);
}

static void unescape(char *text)
{
	char *out = text;
	while(*text) {
		if(text[0] == '\\' && text[1] == 'n') { *out++ = '\n'; text += 2; }
		else if(strncmp(text, "\\205", 4) == 0) { *out++ = '\205'; text += 4; }
		else *out++ = *text++;
	}
	*out = '\0';
}

int main(void)
{
	char line[256];
	out = stdout;
	eventData = calloc(1, sizeof(*eventData));
	CHECK(eventData != NULL);
	event.data = eventData;
	sceneFrame.scene = UI_SCENE_LIBRARY;
	sceneFrame.chromeProgress = 1.0f;
	sceneFrame.libraryReveal = 1.0f;
	while(fgets(line, sizeof(line), stdin)) {
		unsigned a, b; int c; float dt;
		line[strcspn(line, "\n")] = '\0';
		if(sscanf(line, "L %d %u %u", &c, &a, &b) == 3) publish(c, a, b);
		else if(strcmp(line, "K") == 0) detail();
		else if(sscanf(line, "Q %u", &a) == 1) savesVariant = a;
		else if(strcmp(line, "Y") == 0) apps = true;
		else if(sscanf(line, "W %d", &c) == 1) UIStage_SetWide(c != 0);
		else if(sscanf(line, "M %d", &c) == 1) motionMode = (uiMotionMode_t)c;
		else if(sscanf(line, "C %d", &c) == 1) packClosed = c != 0;
		else if(sscanf(line, "Z %d", &c) == 1) {
			/* DrawSetGameflowDetailFocus, without its lock. */
			eventData->detailFocus = (uiGameflowDetailFocus_t)c;
		}
		else if(sscanf(line, "D %d", &c) == 1) {
			/* DrawSetGameflowMode, without its lock. */
			UIGameflow_SetMode(&eventData->state, (uiGameflowMode_t)c, motionMode);
			_GameflowSetLaunch(eventData, UIGameflow_Frame(&eventData->state)->mode ==
				UI_GAMEFLOW_MODE_LAUNCH);
			sceneFrame.scene = c ? UI_SCENE_GAME_DETAIL : UI_SCENE_LIBRARY;
		}
		else if(strncmp(line, "S ", 2) == 0) {
			unescape(line + 2);
			/* DrawLaunchStep, without its lock. */
			if(launchActive) _GameflowLaunchMessage(line + 2);
		}
		else if(strcmp(line, "E") == 0) UILaunch_Finish(&launchState);
		else if(sscanf(line, "N %u %f", &a, &dt) == 2) {
			for(b = 0; b < a; ++b) {
				animDelta = dt; animSeconds += dt;
				fprintf(out, "F\n");
				_DrawGameflow(&event);
			}
		}
		else { fprintf(stderr, "bad command: %s\n", line); return 64; }
	}
	return 0;
}
"""

CENTRE = (320.0, 188.0)
RADIUS = 136.0
FRAME = 1 / 60
# Game Detail for game 18 of 40, then A.
LAUNCH = ["K", "D 1", "N 40 0.0167", "D 2"]


def primitives(frame: str) -> list:
    """Each primitive in a frame: its kind, texture, points and alphas."""
    found, texture = [], None
    for line in frame.splitlines():
        if line.startswith("X "):
            texture = line[2:]
        elif line.startswith("B "):
            fields = line.split()
            found.append({"kind": "strip" if fields[-1] == "strip" else "quads",
                          "count": int(fields[1]), "texture": texture,
                          "points": [], "alphas": []})
            texture = None
        elif line.startswith("P "):
            found[-1]["points"].append(tuple(map(float, line.split()[1:])))
        elif line.startswith("C "):
            found[-1]["alphas"].append(int(line.split()[4]))
    return found


def strings(frame: str) -> list:
    """(x, y, text) of every line of text a frame draws."""
    return [(int(l.split()[1]), int(l.split()[2]), l.split(" ", 6)[6])
            for l in frame.splitlines() if l.startswith("S ")]


def text_cells(frame: str) -> list:
    """Full medium-font cells, including its one-pixel coverage pass.

    The western IPL cell is 24px high; its y anchor is the cell centre.
    The checked font stand-in advances 11px, and the final glyph also
    extends one font pixel beyond its advance, as IPLFontWrite.c does.
    """
    cells = []
    for line in frame.splitlines():
        if not line.startswith("S "):
            continue
        _, x, y, scale, align, alpha, text = line.split(" ", 6)
        x, y, scale, align = float(x), float(y), float(scale), int(align)
        left = x - align * 11 * len(text) * scale / 2
        cells.append({"text": text, "scale": scale, "align": align,
                      "alpha": int(alpha),
                      "box": (left, y - 12 * scale,
                              left + (11 * len(text) + 1) * scale + 1,
                              y + 12 * scale)})
    return cells


def text_colors(frame: str) -> dict:
    return {(int(f[1]), int(f[2])): tuple(map(int, f[3:]))
            for line in frame.splitlines() if line.startswith("I ")
            for f in [line.split()]}


def turn(point) -> float:
    """Where a point sits on the ring: 0 at twelve o'clock, clockwise."""
    angle = math.atan2(point[1] - CENTRE[1], point[0] - CENTRE[0])
    return (angle / (2 * math.pi) + 0.25) % 1.0


def dim(frame: str) -> int:
    """The alpha of the dim, the frame's first quad when it spans the stage."""
    first = primitives(frame)[0]
    xs = [p[0] for p in first["points"]]
    return first["alphas"][0] if first["kind"] == "quads" and xs and \
        min(xs) <= 0.0 and max(xs) >= 640.0 else 0


def ring(frame: str) -> tuple:
    """The ring's strips, and how far its fill reaches (0 when empty)."""
    strips = [p for p in primitives(frame) if p["kind"] == "strip"]
    fill = turn(strips[3]["points"][-1]) if len(strips) > 3 else 0.0
    return strips, fill


class LaunchGxStream(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="swiss-launch-gx-")
        cls.frame_c = (GUI / "FrameBufferMagic.c").read_text()
        cls.frame_h = (GUI / "FrameBufferMagic.h").read_text()
        cls.binary = build(Path(cls.tmp.name), GUI, cls.frame_c, cls.frame_h, True,
                           "launch", DRIVER)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_script(self, script):
        # Detail's text holds IPL font bytes such as its "\267" dot.
        result = subprocess.run([str(self.binary)],
                                input=("\n".join(script) + "\n").encode(),
                                capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr[-2000:])
        return frames(result.stdout.decode("latin-1"))

    def check_launch_frames(self):
        """A launch frame in 4:3 and in 16:9."""
        rests = {}
        for wide, edges in ((0, (0.0, 640.0)), (1, (-320 / 3, 640 + 320 / 3))):
            rest = self.run_script([f"W {wide}", "L 0 40 18"] + LAUNCH + [
                "S Checking Game\\205", "N 60 0.0167"])[-1]
            rests[wide] = rest
            drawn = primitives(rest)
            # First the dim, one quad from edge to edge of the frame.
            veil = drawn[0]
            self.assertEqual((veil["kind"], veil["count"]), ("quads", 4))
            xs, ys = [p[0] for p in veil["points"]], [p[1] for p in veil["points"]]
            self.assertAlmostEqual(min(xs), edges[0], places=2)
            self.assertAlmostEqual(max(xs), edges[1], places=2)
            self.assertEqual((min(ys), max(ys)), (0.0, 480.0))
            self.assertEqual(set(veil["alphas"]), {240})
            # Then the ring: a track and a fill, three bands each, within
            # the budget and on the circle.
            strips, fill = ring(rest)
            self.assertEqual(len(strips), 6)
            self.assertLessEqual(sum(s["count"] for s in strips), 6 * 98)
            for strip in strips:
                for point in strip["points"]:
                    self.assertLess(abs(math.dist(point, CENTRE) - RADIUS), 3.0 + 0.01)
            self.assertAlmostEqual(turn(strips[0]["points"][0]), 0.0, places=3)
            self.assertGreater(fill, 0.14 - 0.01)
            self.assertLess(fill, 0.24)
            # The cover, centred inside the ring, clear of it.
            cover = covers(rest)["G018E0"]
            self.assertEqual(len(cover), 1)
            x0, y0, x1, y1 = cover[0]
            self.assertAlmostEqual((x0 + x1) / 2, 320.0, delta=0.5)
            self.assertAlmostEqual((y0 + y1) / 2, 188.0, delta=0.5)
            self.assertLess(math.dist((x0, y0), CENTRE), RADIUS - 3.0)
            # The title and publisher, and one caption: no Detail text
            # and no hint line are left.
            self.assertEqual(strings(rest), [
                (320, 360, "Detail title G018E0"), (320, 384, "Detail publisher"),
                (320, 414, "Checking game")])
            self.assertNotIn("\nH ", "\n" + rest)
        # The same launch screen in both shapes, but for the dim's reach: its
        # "B 4" and four vertices of three lines are the first 13 lines.
        self.assertNotEqual(rests[0], rests[1])
        self.assertEqual(rests[0].split("\n", 13)[13], rests[1].split("\n", 13)[13])

    def test_a_launch_frame_in_both_screen_shapes(self):
        self.check_launch_frames()

    def check_glides(self):
        """From every layout, Detail's cover glides to the centre."""
        for layout in (0, 1, 2, 3):
            shots = self.run_script([f"L {layout} 40 18"] + LAUNCH + ["N 60 0.0167"])
            boxes = [covers(f)["G018E0"][0] for f in shots[39:]]
            # Detail's pose, then a glide that never jumps.
            self.assertAlmostEqual(boxes[0][0], 48 + 6, delta=1.0)
            for a, b in zip(boxes, boxes[1:]):
                self.assertLess(max(abs(p - q) for p, q in zip(a, b)), 40.0)
            self.assertAlmostEqual((boxes[-1][0] + boxes[-1][2]) / 2, 320.0, delta=0.5)

    def test_the_cover_glides_to_the_centre_from_every_layout(self):
        self.check_glides()

    def test_autoboot_launches_from_the_library(self):
        # Boot without prompts: no Detail, and the Library's own title.
        rest = self.run_script(["L 0 40 18", "N 10 0.0167", "D 2", "N 60 0.0167"])[-1]
        self.assertAlmostEqual((covers(rest)["G018E0"][0][0] +
                                covers(rest)["G018E0"][0][2]) / 2, 320.0, delta=0.5)
        self.assertEqual(strings(rest), [(320, 360, "Game number 18"),
                                         (320, 384, "Company 0"),
                                         (320, 414, "Starting game")])

    def test_autoboot_from_spotlight_glides_from_its_picture(self):
        # Boot without prompts in Spotlight: the cover leaves the middle of
        # the picture panel and glides to the centre without a jump.
        shots = self.run_script(["L 3 40 18", "N 10 0.0167", "D 2", "N 60 0.0167"])
        boxes = [covers(f)["G018E0"][0] for f in shots[10:] if "G018E0" in covers(f)]
        self.assertAlmostEqual(boxes[0][0], 106 + 6, delta=12.0)
        for a, b in zip(boxes, boxes[1:]):
            self.assertLess(max(abs(p - q) for p, q in zip(a, b)), 40.0)
        self.assertAlmostEqual((boxes[-1][0] + boxes[-1][2]) / 2, 320.0, delta=0.5)

    def test_the_fill_never_falls_and_the_caption_follows(self):
        messages = ["Saving recent list\\205", "Video Mode: NTSC 480p", "Applied 2 cheats",
                    "Checking Game\\205", "Reading File 1/2\\nmain.dol [3012KB]",
                    "Patching File 1/2\\nmain.dol [3012KB]",
                    "Writing File 1/2\\nmain.dol [3012KB]",
                    "Reading File 1/2\\nmain.dol [3012KB]",
                    "Reading File 2/2\\nstage.rel [220KB]",
                    "One moment, setting up audio streaming.",
                    "Loading DOL\\nDo not remove SD Card - SD2SP2", "Loading DOL"]
        script = ["L 0 40 18"] + LAUNCH + ["N 30 0.0167"]
        for message in messages:
            script += [f"S {message}", "N 8 0.0167"]
        shots = self.run_script(script + ["N 30 0.0167"])[40:]
        fills = [ring(f)[1] for f in shots]
        for a, b in zip(fills, fills[1:]):
            self.assertGreaterEqual(b, a - 1e-4)
        self.assertGreater(fills[-1], 0.78)
        captions = [s[2] for f in shots for s in strings(f) if s[1] == 414]
        self.assertEqual(list(dict.fromkeys(captions)), [
            "Starting game", "Saving recent games", "Setting the video mode",
            "Applying cheats", "Checking game", "Preparing game files",
            "Setting up audio streaming", "Loading game"])
        for text in ("main.dol", "KB", "Reading File"):
            self.assertNotIn(text, "".join(shots))
        # "Do not remove" stays up to the hand-off, under the caption.
        self.assertIn((320, 438, "Do not remove SD Card - SD2SP2"), strings(shots[-1]))

    def check_the_cover_outlasts_the_poster_pack(self):
        # The hand-off closes the pack; Detail's cover stays until the end.
        rest = self.run_script(["L 0 40 18"] + LAUNCH + ["N 30 0.0167", "C 1",
                                                         "S Loading DOL", "E", "N 20 0.0167"])
        self.assertEqual(set(covers(rest[-1])), {"G018E0"})
        self.assertGreater(ring(rest[-1])[1], 0.99)

    def test_the_cover_outlasts_the_poster_pack(self):
        self.check_the_cover_outlasts_the_poster_pack()

    def test_animations_off_still_ring_that_steps(self):
        shots = self.run_script(["M 2", "L 0 40 18"] + LAUNCH + [
            "N 1 0.0167", "S Checking Game\\205", "N 1 0.0167", "N 30 0.0167"])
        # No glide, no glint: the screen is the same from the first frame.
        self.assertEqual(shots[-31], shots[-1])
        self.assertAlmostEqual(ring(shots[-1])[1], 0.14, delta=0.005)
        self.assertAlmostEqual((covers(shots[-1])["G018E0"][0][0] +
                                covers(shots[-1])["G018E0"][0][2]) / 2, 320.0, delta=0.5)

    def test_an_app_launches_from_apps(self):
        """A on an app launches it from the cards, with no Detail: its own
        poster flies to the centre, the others fade, the launch says app,
        and boot_dol's "Loading DOL" moves the ring on."""
        shots = self.run_script(["Y", "L 0 12 4", "N 20 0.0167", "D 2", "N 40 0.0167",
                                 "S Loading DOL", "N 40 0.0167"])
        start, loading = shots[55], shots[-1]
        self.assertTrue(any(text == "Starting app" for _, _, text in strings(start)))
        self.assertTrue(any(text == "Loading app" for _, _, text in strings(loading)))
        for frame in (start, loading):
            self.assertFalse([text for _, _, text in strings(frame) if "game" in text.lower()])
        self.assertEqual(dim(loading), 240)
        self.assertEqual(set(covers(loading)), {"ART004"})
        self.assertGreater(ring(loading)[1], ring(start)[1])

    def test_a_failed_launch_goes_back_to_the_library(self):
        shots = self.run_script(["L 0 40 18"] + LAUNCH + [
            "N 30 0.0167", "S Checking Game\\205", "N 30 0.0167", "D 0", "N 120 0.0167"])
        before = self.run_script(["L 0 40 18", "N 60 0.0167"])[-1]
        # The launch screen fades out, and the Library is as it was.
        fades = [dim(f) for f in shots[99:]]
        self.assertEqual(fades[0], 240)
        self.assertTrue(all(a >= b for a, b in zip(fades, fades[1:])))
        self.assertEqual(fades[-1], 0)
        self.assertEqual(shots[-1], before)

    @staticmethod
    def lit_frame(frame: str) -> tuple:
        """Detail's bright frame round the focused row: its top and bottom."""
        ys, y = [], None
        for line in frame.splitlines():
            if line.startswith("P "):
                y = float(line.split()[2])
            elif line.startswith("C ") and line.split()[1:4] == ["244", "239", "255"]:
                ys.append(y)
        return (min(ys), max(ys)) if ys else None

    def test_detail_focus_slides_between_rows(self):
        # Launch's row, then Cheats' (301 down, 59 tall): the frame slides
        # there over several frames rather than jumping, and rests on it.
        log = self.run_script(["L 0 40 18", "K", "D 1", "N 40 0.0167", "Z 1",
                               "N 40 0.0167"])
        self.assertEqual(self.lit_frame(log[39]), (367.0, 410.0))
        path = [self.lit_frame(f) for f in log[40:]]
        self.assertEqual(path[-1], (301.0, 360.0))
        tops = [top for top, _ in path]
        self.assertEqual(tops, sorted(tops, reverse=True))
        self.assertTrue(all(a - b < 66 * 0.3 for a, b in zip(tops, tops[1:])), tops)
        self.assertGreater(sum(1 for a, b in zip(tops, tops[1:]) if a != b), 5)
        # Off moves it at once; Detail opening again finds it on its row.
        log = self.run_script(["M 2", "L 0 40 18", "K", "D 1", "N 5 0.0167", "Z 2",
                               "N 1 0.0167"])
        self.assertEqual(self.lit_frame(log[-1]), (252.0, 294.0))
        log = self.run_script(["L 0 40 18", "K", "D 1", "N 40 0.0167", "Z 1",
                               "N 40 0.0167", "D 0", "N 60 0.0167", "Z 0", "D 1",
                               "N 1 0.0167"])
        self.assertEqual(self.lit_frame(log[-1]), (367.0, 410.0))

    def detail_rest(self, wide=0, variant=1, focus=0):
        return self.run_script([f"W {wide}", "M 2", "L 0 40 18", f"Q {variant}",
                                "K", "D 1", f"Z {focus}", "N 1 0.0167"])[-1]

    def test_save_inset_draws_copied_stats_above_actions(self):
        for wide in (0, 1):
            with self.subTest(wide=wide):
                rest = self.detail_rest(wide)
                drawn = strings(rest)
                self.assertIn((576, 214, "SAVES"), drawn)
                self.assertIn((274, 214, "2 copies | 2 blocks"), drawn)
                self.assertIn((274, 232, "Updated 2024-02-29 12:34 | SD card image"), drawn)
                self.assertIn((274, 264, "SETTINGS"), drawn)
                self.assertIn((274, 313, "CHEATS"), drawn)
                self.assertIn((264, 122, "Detail title G018E0"), drawn)
                self.assertFalse(any(t == "SAVE DATA" for _, _, t in drawn))
                # Actual GX panel vertices enclose the inset and stay separate
                # from focus; its label is not a fourth navigable action.
                quads = [p["points"][i:i + 4] for p in primitives(rest)
                         if p["kind"] == "quads" for i in range(0, p["count"], 4)]
                self.assertIn([(260.0, 202.0), (590.0, 202.0),
                               (590.0, 245.0), (260.0, 245.0)], quads)
                for row, expected in ((0, (367.0, 410.0)), (1, (301.0, 360.0)),
                                      (2, (252.0, 294.0))):
                    self.assertEqual(self.lit_frame(self.detail_rest(wide, focus=row)), expected)
                partial = strings(self.detail_rest(wide, 2))
                self.assertIn((274, 232, "Partial scan | Updated 2024-02-29 12:34 | SD card image"),
                              partial)
                unknown_date = strings(self.detail_rest(wide, 5))
                self.assertIn((274, 232, "Update date unavailable | SD card image"), unknown_date)
                partial_date = strings(self.detail_rest(wide, 7))
                self.assertIn((274, 232, "Partial scan | Update date unavailable | SD card image"),
                              partial_date)

    def test_save_choice_draws_the_chosen_copy(self):
        # With Left and Right choosing among the copies, the label says so,
        # and the inset names the chosen copy and that it goes on at launch,
        # in the inset's own places.
        for wide in (0, 1):
            with self.subTest(wide=wide):
                drawn = strings(self.detail_rest(wide, 9))
                self.assertIn((576, 214, "\xab SAVES \xbb"), drawn)
                self.assertIn((274, 214, "Copy 2 of 3 | Save Folder"), drawn)
                self.assertIn((274, 232, "Loads at launch | 2024-02-29 12:34 | SD card image"), drawn)
                self.assertIn((274, 264, "SETTINGS"), drawn)
                # With copies of more than one file, the chosen copy's file
                # and its place among that file's copies, still in the inset.
                files = strings(self.detail_rest(wide, 10))
                self.assertIn((274, 214, "DobutsunomoriP_MURA 2 of 2 | MemoryCardA.USA.raw"), files)
                for variant in (9, 10):
                    cells = [c for c in text_cells(self.detail_rest(wide, variant))
                             if 202 <= c["box"][1] and c["box"][3] <= 245]
                    self.assertEqual(len(cells), 3)
                    tag, summary, _ = cells
                    self.assertGreater(tag["box"][0] - summary["box"][2], 12)
                    self.assertGreaterEqual(summary["scale"], 0.46)
                    self.assertTrue(all(c["box"][2] < 580 for c in cells))

    def test_fewer_than_two_copies_leave_saves_out(self):
        # Saves on Details off (no statistics), no copies, a partial scan
        # that found none, or one copy: no SAVES inset, and Detail keeps the
        # places it had before the inset.
        for wide in (0, 1):
            for variant in (0, 4, 6, 8):
                with self.subTest(wide=wide, variant=variant):
                    rest = self.detail_rest(wide, variant)
                    drawn = strings(rest)
                    self.assertFalse([t for _, _, t in drawn if t == "SAVES" or "save cop" in t])
                    self.assertIn((264, 148, "Detail publisher"), drawn)
                    self.assertIn((264, 202, "LAST PLAYED"), drawn)
                    self.assertIn((274, 244, "SETTINGS"), drawn)
                    self.assertIn((274, 293, "CHEATS"), drawn)
                    quads = [p["points"][i:i + 4] for p in primitives(rest)
                             if p["kind"] == "quads" for i in range(0, p["count"], 4)]
                    self.assertIn([(246.0, 76.0), (604.0, 76.0),
                                   (604.0, 404.0), (246.0, 404.0)], quads)
                    self.assertFalse([q for q in quads if q[0] == (260.0, 202.0)])
                    for row, expected in ((0, (348.0, 391.0)), (1, (281.0, 340.0)),
                                          (2, (232.0, 274.0))):
                        self.assertEqual(
                            self.lit_frame(self.detail_rest(wide, variant, focus=row)), expected)

    def test_save_inset_hierarchy_and_full_cells_fit(self):
        # Both lines occupy full font cells, not just baseline anchors.
        # Max totals and partial/date status share the same fixed inset;
        # none may touch its border, the trailing label or the next row.
        for wide in (0, 1):
            for variant in (1, 2, 3, 5, 7):
                with self.subTest(wide=wide, variant=variant):
                    rest = self.detail_rest(wide, variant)
                    cells = [c for c in text_cells(rest)
                             if 202 <= c["box"][1] and c["box"][3] <= 245]
                    self.assertEqual(len(cells), 3)
                    tag, summary, updated = cells
                    self.assertEqual(tag["text"], "SAVES")
                    self.assertEqual(tag["align"], 2)
                    self.assertGreaterEqual(tag["scale"], 0.38)
                    self.assertEqual(summary["align"], 0)
                    self.assertGreaterEqual(summary["scale"], 0.46)
                    self.assertGreater(summary["scale"], updated["scale"])
                    self.assertEqual(updated["scale"], 0.46)
                    self.assertEqual(updated["align"], 0)
                    for cell in cells:
                        left, top, right, bottom = cell["box"]
                        self.assertGreaterEqual(left, 274)
                        self.assertGreater(top, 202)
                        self.assertLess(right, 580)
                        self.assertLess(bottom, 245)
                    self.assertGreater(tag["box"][0] - summary["box"][2], 12)
                    self.assertGreater(updated["box"][1] - summary["box"][3], 5)
                    self.assertGreater(252 - updated["box"][3], 14)
                    if variant == 3:
                        self.assertEqual(summary["text"],
                                         "4294967295 files | 4294967295 blocks")
                    colors = text_colors(rest)
                    self.assertEqual(colors[(274, 214)], (246, 243, 255, 255))
                    self.assertEqual(colors[(274, 232)], (202, 192, 244, 235))
                    # The date/status has the same secondary weight as the
                    # section labels; muted preview text remains subordinate.
                    self.assertEqual(colors[(274, 232)], colors[(274, 264)])
                    self.assertGreater(min(colors[(274, 232)][:3]), 185)
                    self.assertGreater(colors[(274, 232)][3], 218)

    def test_save_inset_text_fades_with_detail(self):
        for wide in (0, 1):
            log = self.run_script([f"W {wide}", "L 0 40 18", "K", "D 1",
                                   "N 100 0.0167"])
            colors = [text_colors(frame) for frame in log]
            visible = [c for c in colors if (274, 214) in c]
            self.assertTrue(visible)
            alphas = [c[(274, 214)][3] for c in visible]
            self.assertLess(min(alphas), 255)
            self.assertEqual(alphas[-1], 255)
            self.assertEqual(alphas, sorted(alphas))
            for c in visible:
                self.assertEqual(c[(274, 232)], c[(576, 214)])
                self.assertLessEqual(c[(274, 232)][3], c[(274, 214)][3])

    def test_detail_frame_budget(self):
        budget = json.loads((Path(__file__).with_name("detail_frame_budget.json")).read_text())
        for wide in (0, 1):
            for variant, name in enumerate(("unavailable", "recorded", "partial", "max-count", "empty")):
                scene = f"detail-{name}-{'wide' if wide else 'native'}"
                drawn = primitives(self.detail_rest(wide, variant))
                actual = {"geometry_vertices": sum(p["count"] for p in drawn),
                          "geometry_begins": len(drawn),
                          # IPL medium text emits two four-vertex passes per
                          # glyph. Hint icons have their own registered suite.
                          "medium_glyphs": sum(len(t) for _, _, t in strings(self.detail_rest(wide, variant)))}
                with self.subTest(scene=scene):
                    self.assertEqual(actual, budget[scene])

    def test_mutants_fail(self):
        mutants = {
            "the dim stops at the stage": ("{{{UIStage_Left(), 0.0f},\n\t\t{UIStage_Right(), 0.0f}, "
                                           "{UIStage_Right(), 480.0f},\n\t\t{UIStage_Left(), 480.0f}}}",
                                           "{{{0.0f, 0.0f},\n\t\t{640.0f, 0.0f}, "
                                           "{640.0f, 480.0f},\n\t\t{0.0f, 480.0f}}}"),
            "the pack's closing takes the cover": ("posterTexture = launchActive && launchHasPoster &&",
                                                   "posterTexture = false && launchHasPoster &&"),
            "Detail stays under the launch screen": ("if(frame->launchProgress < 0.999f) {",
                                                     "if(frame->launchProgress < 2.0f) {"),
            "the cover stays at Detail's pose": ("gameflowLaunchPose.point[vertex],\n",
                                                 "card->quad.point[vertex],\n"),
        }
        tests = (self.check_launch_frames, self.check_the_cover_outlasts_the_poster_pack,
                 self.check_glides)
        original = self.binary
        try:
            for index, (name, (old, new)) in enumerate(mutants.items()):
                with self.subTest(mutant=name):
                    self.assertEqual(self.frame_c.count(old), 1, name)
                    self.binary = build(Path(self.tmp.name), GUI,
                                        self.frame_c.replace(old, new), self.frame_h,
                                        True, f"mutant{index}", DRIVER)
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
