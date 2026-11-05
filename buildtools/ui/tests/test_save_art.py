#!/usr/bin/env python3
"""Memory Cards' test saves through the save art decoder.

buildtools/ui/qa/make_test_saves.py writes the saves Memory Cards is tried
with, in Dolphin's GCI folders and an SD card's Save Folder. This writes them
afresh and reads every one back with ui_saves.c, as the console does: the
entry found in each wrapper, the banner and icon laid out from it, every
frame and the banner turned into RGB5A3 texels, and the timeline. Each must
match what the generator drew, and the saves must cover every kind of icon:
the three frame forms, both banner forms and none, all three speeds, a
bounce, a frame with no pixels in the middle and at the end, eight frames,
no icon, no art at all, and NOMOVE and NOCOPY saves, with enough on each
card and in the folder to scroll a grid of 16.

usage: test_save_art.py   (CC picks the compiler; needs Pillow)
"""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
GUI = HERE.parents[2] / "cube" / "swiss" / "source" / "gui"
sys.path.insert(0, str(HERE.parent / "qa"))
import make_test_saves as gen  # noqa: E402

# Reads one save file as Memory Cards does and prints what it found; the
# decoded frames (8 x 2048 bytes, unused ones zero) and banner (6144) go to
# the second file.
DRIVER = r"""
#include "ui_saves.h"
#include "ui_saves_raw.h"
#include <stdio.h>
#include <string.h>

static uint8_t file[1u << 20], image[1u << 20], texels[8u * 2048u + 6144u];
static size_t imageLength;

static bool imageRead(void *opaque, uint32_t offset, void *out, uint32_t length)
{
    (void)opaque;
    if(offset > imageLength || length > imageLength - offset) return false;
    memcpy(out, image + offset, length);
    return true;
}

int main(int argc, char **argv)
{
	uint8_t entry[UI_SAVES_ENTRY_SIZE];
	uiSavesArt_t art;
	FILE *in = argc == 3 || argc == 4 ? fopen(argv[1], "rb") : NULL, *out;
	size_t length, start;
	unsigned i;

	if(in == NULL) return 2;
	length = fread(file, 1u, sizeof(file), in);
	fclose(in);
	if(argc == 4) {
		uiSavesRawCard_t card;
		imageLength = length;
		memcpy(image, file, length);
		if(UISavesRaw_Parse(image, UI_SAVES_RAW_METADATA_SIZE, length, &card) != UI_SAVES_RAW_OK)
			return 4;
		length = UISavesRaw_GciSize(&card, 0u);
		if(!UISavesRaw_ReadGci(&card, 0u, 0u, file, length, imageRead, NULL)) return 5;
	}
	start = UISaves_FindEntry(file, length, entry);
	printf("{\"start\": %u", (unsigned)start);
	if(start != 0u) {
		const uint8_t *data = file + start;
		printf(", \"art\": %d", UISaves_ArtLayout(entry, length - start, &art));
		printf(", \"end\": %u, \"banner\": %u, \"frames\": [", (unsigned)art.end,
			art.bannerFormat);
		for(i = 0u; i < art.frames; i++) {
			printf("%s%u", i ? ", " : "", art.frameFormat[i]);
			if(art.frameFormat[i] != UI_SAVES_ART_NONE &&
				!UISaves_ToRgb5a3(data, art.end, &art, (int)i, texels + i * 2048u))
				return 3;
		}
		printf("], \"steps\": [");
		for(i = 0u; i < art.steps; i++) {
			printf("%s[%d, %u]", i ? ", " : "",
				art.stepFrame[i] == UI_SAVES_ART_BLANK ? -1 : art.stepFrame[i],
				art.stepHold[i]);
		}
		printf("], \"shown\": [");
		for(i = 0u; i < art.period; i++) {
			printf("%s%d", i ? ", " : "", UISaves_ArtStep(&art, i));
		}
		printf("], \"comment\": \"");
		for(i = 0u; art.comment && i < 64u; i++) {
			printf("%02x", data[art.commentAt + i]);
		}
		printf("\"");
		if(art.bannerFormat != UI_SAVES_ART_NONE &&
			!UISaves_ToRgb5a3(data, art.end, &art, UI_SAVES_ART_BANNER,
				texels + 8u * 2048u))
			return 3;
	}
	printf("}\n");
	out = fopen(argv[2], "wb");
	if(out == NULL || fwrite(texels, 1u, sizeof(texels), out) != sizeof(texels)) return 2;
	fclose(out);
	return 0;
}
"""

WRAPPER_START = {"gci": 64, "sav": 0x80 + 64, "gcs": 0x110 + 64}


def timeline(save: gen.Save) -> list[list[int]]:
    """The steps Dolphin plays: each frame for its speed, a frame with no
    pixels showing the next that has some (or nothing), and a bounce coming
    back through the middle frames."""
    frames = list(save.frames) if save.icon_addr == gen.ICON_ADDR else []
    if not frames or frames[0][0] == 0:
        return []
    order = list(range(len(frames)))
    if save.bounce and len(frames) >= 3:
        order += list(range(len(frames) - 2, 0, -1))
    shown = [next((j for j in range(i, len(frames)) if frames[j][0]), -1) for i in range(len(frames))]
    return [[shown[i], frames[i][1]] for i in order]


class SaveArt(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = pathlib.Path(cls.tmp.name)
        (tmp / "driver.c").write_text(DRIVER)
        cc = os.environ.get("CC", "cc")
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-I", str(GUI),
                        "-o", str(tmp / "driver"), str(tmp / "driver.c"), str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"),
                        str(GUI / "ui_saves_raw.c")],
                       check=True)
        cls.out = tmp / "saves"
        subprocess.run([sys.executable, str(HERE.parent / "qa" / "make_test_saves.py"), str(cls.out)],
                       check=True)
        cls.read = {}
        for path, save, wrapper in gen.catalog():
            texels = tmp / "texels"
            result = subprocess.run([str(tmp / "driver"), str(cls.out / path), str(texels)],
                                    capture_output=True, text=True, check=True)
            cls.read[path] = (save, wrapper, json.loads(result.stdout), texels.read_bytes())

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_every_file_is_one_listed_save(self) -> None:
        written = sorted(str(p.relative_to(self.out)) for p in self.out.rglob("*") if p.is_file())
        self.assertEqual(written, sorted(path for path, _, _ in gen.catalog()))

    def test_every_save_decodes_to_what_was_drawn(self) -> None:
        for path, (save, wrapper, found, texels) in self.read.items():
            with self.subTest(path=path):
                self.assertEqual(found["start"], WRAPPER_START[wrapper])
                _, expected = gen.art(save)
                drawn = [f for f in save.frames] if save.icon_addr == gen.ICON_ADDR else []
                if drawn and drawn[0][0] == 0:
                    drawn = []
                self.assertEqual(found["frames"], [form for form, _ in drawn])
                for i, pixels in enumerate(expected["frames"][:len(drawn)]):
                    if pixels is not None:
                        self.assertEqual(texels[i * 2048:(i + 1) * 2048], pixels, f"frame {i}")
                banner = {None: 0, gen.BANNER_RGB5A3: 2, gen.BANNER_CI8: 3}
                self.assertEqual(found["banner"], banner[None if expected["banner"] is None
                                                         else save.banner])
                if expected["banner"] is not None:
                    self.assertEqual(texels[8 * 2048:], expected["banner"])
                self.assertEqual(found["steps"], timeline(save))
                comment = b"".join(line.encode().ljust(32, b"\0") for line in save.comment)
                self.assertEqual(bytes.fromhex(found["comment"]), comment)

    def test_fragmented_raw_icon_pixels_and_timeline_reach_the_real_decoder(self) -> None:
        tmp = pathlib.Path(self.tmp.name)
        raw, _ = gen.virtual_card()
        source, pixels = tmp / "animated.raw", tmp / "raw-texels"
        source.write_bytes(raw)
        result = subprocess.run([str(tmp / "driver"), str(source), str(pixels), "raw"],
                                capture_output=True, text=True, check=True)
        found, decoded = json.loads(result.stdout), pixels.read_bytes()
        first, _ = gen.virtual_saves()
        _, expected = gen.art(first)
        self.assertEqual(found["frames"], [gen.RGB5A3, gen.RGB5A3])
        self.assertEqual(found["steps"], [[0, 2], [1, 3]])
        self.assertEqual(found["shown"], [0, 0, 1, 1, 1])
        self.assertNotEqual(expected["frames"][0], expected["frames"][1])
        for index, frame in enumerate(expected["frames"]):
            self.assertEqual(decoded[index * 2048:(index + 1) * 2048], frame)
        self.assertEqual(decoded[8 * 2048:], expected["banner"])
        self.assertEqual(source.read_bytes(), raw, "parsing and decoding remain read-only")

    def test_the_timelines_play_as_dolphin_plays_them(self) -> None:
        def shown(path: str) -> list[int]:
            return self.read[path][2]["shown"]
        # Speeds 1, 2, 3 and 2 hold each frame that many ticks.
        self.assertEqual(shown("A/ZZ-ZIQE-IndigoQuest_Save01.gci"), [0, 1, 1, 2, 2, 2, 3, 3])
        # A bounce of three frames comes back through the middle one.
        self.assertEqual(shown("A/ZZ-ZSRE-StarRacerGhosts.gci"), [0, 1, 1, 2, 2, 2, 1, 1])
        # The frame with no pixels shows the next; the last shows nothing.
        self.assertEqual(shown("A/ZZ-ZLKE-LanternKeep.gci"), [0, 0, 2, 2, 2, 2, 3])
        self.assertEqual(shown("A/ZZ-ZPCE-PaperComets.gci"), [0, 0, 1, 1, -1, -1, -1])

    def test_the_saves_cover_every_kind_of_icon(self) -> None:
        slot_a = [found for path, (_, _, found, _) in self.read.items() if path.startswith("A/")]
        saves_a = [save for path, (save, _, _, _) in self.read.items() if path.startswith("A/")]
        forms = {form for found in slot_a for form in found["frames"]}
        self.assertEqual(forms, {0, 1, 2, 3})
        self.assertEqual({found["banner"] for found in slot_a}, {0, 2, 3})
        self.assertEqual({hold for found in slot_a for _, hold in found["steps"]}, {1, 2, 3})
        self.assertIn(8, [len(found["frames"]) for found in slot_a])
        self.assertTrue(any(s.bounce and len(s.frames) == 8 for s in saves_a))
        middle = [f["frames"] for f in slot_a if 0 in f["frames"][:-1]]
        trailing = [f["frames"] for f in slot_a if f["frames"] and f["frames"][-1] == 0]
        self.assertTrue(middle and trailing)
        self.assertTrue(any(f["banner"] and not f["frames"] for f in slot_a), "a banner, no icon")
        self.assertTrue(any(f["art"] == 1 and not f["banner"] and not f["frames"] and f["end"] == 64
                            for f in slot_a), "no banner and no icon")
        self.assertTrue(any(f["frames"] and not f["banner"] for f in slot_a), "an icon, no banner")
        permissions = {save.permissions for save in saves_a}
        self.assertTrue({gen.PUBLIC | gen.NOMOVE, gen.PUBLIC | gen.NOCOPY} <= permissions)
        self.assertIn("sav", {wrapper for _, wrapper, _, _ in self.read.values()})
        self.assertIn("gcs", {wrapper for _, wrapper, _, _ in self.read.values()})

    def test_every_grid_scrolls(self) -> None:
        # The GameCube shows a card as 16 cells, more in rows of four as saves
        # come; each place here holds more than 16, so it scrolls.
        paths = [path for path, _, _ in gen.catalog()]
        folder = f"SD/{gen.SAVE_FOLDER}/"
        loose = [p for p in paths if p.startswith(folder) and "/" not in p[len(folder):]]
        folders = {p[len(folder):].split("/")[0] for p in paths
                   if p.startswith(folder) and "/" in p[len(folder):]}
        self.assertEqual(len([p for p in paths if p.startswith("A/")]), 21)
        self.assertGreater(len([p for p in paths if p.startswith("B/")]), 16)
        self.assertGreater(len(loose) + len(folders), 16)
        # Slot B shares exactly one save with Slot A, so a copy can collide.
        names = lambda card: {p.split("/")[1] for p in paths if p.startswith(card + "/")}
        self.assertEqual(len(names("A") & names("B")), 1)


if __name__ == "__main__":
    unittest.main()
