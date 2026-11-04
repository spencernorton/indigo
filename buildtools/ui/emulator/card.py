#!/usr/bin/env python3
"""Build the demonstration disc the emulator test boots Indigo with.

Dolphin inserts it as a GameCube disc and Swiss reads its ISO 9660 file
system as a device. /games holds small images of fictitious games: a disc
header, a file table with one file, opening.bnr, and that banner (drawn
here), which is all the Library reads of a game. Two more images have a
missing or corrupt file table, which the Library must survive (DAMAGED).
A text file and an empty folder sit beside them, as they do on real cards:
the Library skips both (STRAYS), and the text file sorts first, so the
games move up past it. Three games sit in folders (FOLDERS), one of them three
levels down in Old.Saves.v2: Swiss's default flattening lists them with the rest, and Library
Folders shows the folders, two levels deep, with the pictures beside them
(FOLDER_PICTURES) as their posters: Racing.v1's, in a colour nothing else on the
disc has, and two that must never show, in another: Nintendo.GC's, a picture
past the 2 MB one may have, and Classics.Set's, damaged.
/swiss/ui/posters.pak holds posters drawn here from gradients and shapes, for
all but two of the games, so the Library shows both kinds of card, and
/swiss/ui/stills.pak gameplay stills drawn the same way for all but three, so
Spotlight shows a still, a cover and a banner card, /swiss/ui/discs.pak disc
art drawn the same way for all but three, so the Jewel Case shows a disc of
its own, the middle of a cover and a bare disc, and
/swiss/ui/descriptions.txt a line about all but one, which keeps its
banner's. /swiss/ui/manuals holds a manual for one game, made with
manual_pack.py from pages laid out here of Indigo's own guide pictures
(MANUAL_GAME), and a damaged one for another. /apps holds stand-in programs for Apps, beside pictures drawn here
in each shape Indigo fits to a card, one in a Homebrew Channel folder whose
boot.dol Apps must leave out. Nothing in it is anyone else's: no game, no
box art, no screenshot, no text, no font.

Given the probe (probe/probe.c, built with the toolchain), the disc also holds
it twice, as a real program Indigo can launch: a game image whose boot program
is the probe (PROBE_GAME) and an app (PROBE_APP).

With --card-zip it makes an SD card image instead (build_card): the release
zip unpacked onto a FAT32 card, the same games, packs and apps beside it.

usage: card.py OUT.iso [--no-posters] [--probe probe.dol]
       card.py OUT.img --card-zip Indigo-<version>.zip [--no-posters] [--probe probe.dol]
Needs genisoimage; the posters need gxtexconv (see buildtools/ui/poster_pack.py)
and are left out, with a notice, when it is missing.
"""

from __future__ import annotations

import argparse
import array
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

GAMES = (
    ("GACZ01", "Astral Circuit"), ("GCHZ01", "Cobalt Harrier"), ("GDRZ01", "Dune Rally"),
    ("GEIZ01", "Ember and Ivy"), ("GGPZ01", "Glacier Point"), ("GNTZ01", "Neon Tidepool"),
    ("GPLZ01", "Paper Lantern"), ("GRZZ01", "Rally Cross Zero"), ("GSSZ01", "Skyward Salvage"),
)
NO_POSTER = frozenset({"GPLZ01", "GSSZ01"})
STRAYS = ("About these games.txt", "Nintendo.GC/")
# Games in folders below /games, the last one past the second level.
FOLDERS = {"GRZZ01": "Racing.v1", "GNTZ01": "Racing.v1/Classics.Set", "GPLZ01": "Racing.v1/Classics.Set/Old.Saves.v2"}
# Pictures beside folders, each a poster-shaped field of one colour: the one
# Library Folders shows, and the ones it must refuse (run.py looks for both).
FOLDER_PICTURES = {"Racing.v1": "shown", "Nintendo.GC": "too big", "Racing.v1/Classics.Set": "damaged"}
SHOWN_PICTURE, REFUSED_PICTURE = (255, 0, 255), (0, 255, 255)
NO_STILL = frozenset({"GDRZ01", "GPLZ01", "GSSZ01"})
# The Jewel Case's discs: one with a poster and no disc prints its cover's
# middle, and the two with neither are bare.
NO_DISC = frozenset({"GEIZ01", "GPLZ01", "GSSZ01"})
# Spotlight's descriptions; Rally Cross Zero keeps its banner's.
DESCRIPTIONS = {
    "GACZ01": "Race through orbiting circuits where every lap rewires the track, and the fastest line is the one nobody has drawn yet.",
    "GCHZ01": "Fly a patched-up harrier over a cobalt sea, trading cargo between islands that move a little further apart every night.",
    "GDRZ01": "Cross an ever-shifting desert in a rally car held together by tape and optimism. Dunes remember every wheel that crossed them.",
    "GEIZ01": "Two gardeners, one of fire and one of vines, tend a greenhouse at the end of the world. Neither can finish it alone.",
    "GGPZ01": "Guide a team of climbers to a summit that is never where the map says. Weather, rope and trust run short in turn.",
    "GNTZ01": "Dive among the glowing tidepools of a city that sank long ago, and bring its lights back to the surface one by one.",
    "GPLZ01": "Carry a paper lantern through a town of folded houses, where every door opens onto another story.",
    "GSSZ01": "Salvage what fell from the sky cities, then decide whether to sell it, keep it, or send it back up.",
}
# Cobalt Harrier's manual: two of Indigo's guide pictures a page, on paper,
# under a band of a colour of the page's own, which run.py finds the page by.
# Only pictures of this disc's own games and saves: none names a real game.
# The last page is taller than a page may be, so its texture is the tallest
# there is. Ember and Ivy's manual is cut short: Indigo must say it can't be
# read.
MANUAL_GAME, BROKEN_MANUAL_GAME = "GCHZ01", "GEIZ01"
MANUAL_BANDS = ((220, 0, 0), (0, 190, 0), (0, 0, 220), (225, 225, 0), (0, 200, 200), (220, 0, 220),
                (240, 120, 0))
MANUAL_PICTURES = ("home-library-face", "home-files-face", "game-details", "settings-quick", "settings-setup",
                   "home-sides", "memory-cards", "memory-cards-details", "memory-cards-options", "files",
                   "files-actions", "apps", "system-info", "source-picker-still")
MANUAL_PAPER = (238, 234, 224)
GUIDE_PICTURES = Path(__file__).resolve().parents[3] / "docs/guide/images"
DISC = ("QIDC00", "Indigo demonstration disc")
MAGIC = 0xC2339F3D
STUB_BYTES = 64 * 1024
FST_OFFSET = 0x5000       # the outer disc's (empty) file table
GAME_FST = 0x4000         # a game's file table, and its banner after it
GAME_BANNER = 0x5000
BANNER_BYTES = 0x1960     # BNR1: magic, padding, 96x32 RGB5A3 pixels, one description
DESCRIPTION = "A fictitious game on the disc Indigo's emulator test boots with."
SYSTEM_AREA = 0x8000  # ISO 9660 leaves the first 32 KiB to the platform


# A disc's region code (0x458, in bi2 after the header), which Swiss takes a
# game's region from: 0 Japan, 1 the Americas, 2 PAL.
REGION_CODES = {"J": 0, "E": 1, "P": 2}
REGION_CODE_AT = 0x458


def disc_header(game_id: str, title: str) -> bytearray:
    """The start of a GameCube disc: ID, magic, and the game's name."""
    if len(game_id) != 6 or not game_id.isalnum() or not game_id.isupper():
        raise ValueError(f"not a disc ID: {game_id!r}")
    header = bytearray(0x440)
    header[0:6] = game_id.encode("ascii")
    struct.pack_into(">I", header, 0x1C, MAGIC)
    name = title.encode("ascii")
    header[0x20:0x20 + len(name)] = name
    return header


def banner(index: int, title: str) -> bytes:
    """An opening.bnr: a gradient in the game's colour, and its names."""
    data = bytearray(BANNER_BYTES)
    data[0:4] = b"BNR1"
    hue = (index * 67) % 360
    left, right = _rgb(hue, 0.6, 0.35), _rgb((hue + 40) % 360, 0.8, 0.85)
    offset = 0x20
    for tile_y in range(8):          # 96x32 in 4x4 tiles, row by row
        for tile_x in range(24):
            for y in range(4):
                for x in range(4):
                    t = (tile_x * 4 + x) / 95
                    r, g, b = (round(a + (c - a) * t) >> 3 for a, c in zip(left, right))
                    struct.pack_into(">H", data, offset, 0x8000 | r << 10 | g << 5 | b)
                    offset += 2
    # Swiss's BNRDesc: a short name and publisher, then the full ones, then
    # the description, each field after the last (include/bnr.h).
    for field, text, size in ((0x1820, title, 0x20), (0x1840, "Indigo test disc", 0x20),
                              (0x1860, title, 0x40), (0x18A0, "Indigo demonstration disc", 0x40),
                              (0x18E0, DESCRIPTION, 0x80)):
        encoded = text.encode("ascii")[:size - 1]
        data[field:field + len(encoded)] = encoded
    return bytes(data)


def game_image(index: int, game_id: str, title: str, region: int = 0) -> bytes:
    """A game as the Library reads it: its header, a one-file table, its banner."""
    image = bytearray(STUB_BYTES)
    image[:0x440] = disc_header(game_id, title)
    struct.pack_into(">I", image, REGION_CODE_AT, region)
    fst = struct.pack(">BBHII", 1, 0, 0, 0, 2) + struct.pack(">BBHII", 0, 0, 0, GAME_BANNER, BANNER_BYTES)
    fst += b"opening.bnr\0"
    struct.pack_into(">III", image, 0x424, GAME_FST, len(fst), len(fst))
    image[GAME_FST:GAME_FST + len(fst)] = fst
    image[GAME_BANNER:GAME_BANNER + BANNER_BYTES] = banner(index, title)
    return bytes(image)


def header_only(game_id: str, title: str) -> bytes:
    """A header-only dump: a disc header that declares no file table."""
    return bytes(disc_header(game_id, title))


RUNAWAY_COUNT = 0x0AAAAAAA


def runaway_table(game_id: str, title: str) -> bytes:
    """A game whose file table counts far more entries than it holds.

    RUNAWAY_COUNT puts the string table 0x7FFFFFF8 bytes past the table: an
    unchecked lookup wraps below 0x80000000, where nothing is mapped, and faults
    on its first name instead of reading on through memory."""
    image = bytearray(STUB_BYTES)
    image[:0x440] = disc_header(game_id, title)
    fst = struct.pack(">BBHII", 1, 0, 0, 0, RUNAWAY_COUNT)
    fst += struct.pack(">BBHII", 0, 0, 0, GAME_BANNER, BANNER_BYTES)
    struct.pack_into(">III", image, 0x424, GAME_FST, len(fst), len(fst))
    image[GAME_FST:GAME_FST + len(fst)] = fst
    return bytes(image)


# Images the Library lists and must survive. It reads the file table of every
# image it lists, to find the banner, and an unchecked read of a missing or
# corrupt table crashed it. Their titles sort among the first games, which the
# emulator test browses.
DAMAGED = (("GBHZ01", "Broken Header", header_only), ("GCTZ01", "Corrupt Table", runaway_table))


# The probe, as a game: an ID with a real region letter, and a title that
# sorts among the games the route does not browse.
PROBE_GAME = ("GPRE01", "Indigo Probe")
PROBE_APP = "Probe.dol"
PROBE_DOL_OFFSET = 0x10000
# A GC Loader boots boot.iso from its card: on a card for one, the release's
# ipl.dol made into a disc.
BOOT_ISO = ("GSWE01", "Indigo")


def probe_image(dol: bytes, game_id: str = PROBE_GAME[0], title: str = PROBE_GAME[1],
                optical_data: bool = False) -> bytes:
    """A disc image whose boot program is the probe, laid out as Swiss reads a
    game: header, an apploader header at 0x2440 (Swiss patches the apploader
    too, but boots the DOL itself), the DOL, and a file table with the banner."""
    header = disc_header(game_id, title)
    apploader = bytearray(0x40)
    apploader[0:10] = b"2026/10/01"
    struct.pack_into(">IIII", apploader, 0x10, 0x81200000, 0x20, 0, 0)
    struct.pack_into(">I", apploader, 0x20, 0x4E800020)  # blr: never run
    bnr = banner(len(GAMES), title)
    bnr_offset = (PROBE_DOL_OFFSET + len(dol) + 0xFFF) & ~0xFFF
    fst_offset = (bnr_offset + len(bnr) + 0xFFF) & ~0xFFF
    names = bytearray(b"opening.bnr\0")
    entries = [struct.pack(">BBHII", 0, 0, 0, bnr_offset, len(bnr))]
    if optical_data:
        # Ordinary game data, including a folder whose name resembles a
        # strict Library game folder: neither may become a Library card.
        for name in ("assets", "Data [GDIR01]"):
            index = len(entries) + 1
            entries.append(struct.pack(">III", 0x01000000 | len(names), 0, index + 1))
            names.extend(name.encode() + b"\0")
    fst = struct.pack(">BBHII", 1, 0, 0, 0, len(entries) + 1) + b"".join(entries) + names
    struct.pack_into(">IIII", header, 0x420, PROBE_DOL_OFFSET, fst_offset, len(fst), len(fst))
    image = bytearray((fst_offset + len(fst) + 0x7FFF) & ~0x7FFF)
    image[:0x440] = header
    struct.pack_into(">I", image, REGION_CODE_AT, REGION_CODES[game_id[3]])
    image[0x2440:0x2440 + len(apploader)] = apploader
    image[PROBE_DOL_OFFSET:PROBE_DOL_OFFSET + len(dol)] = dol
    image[bnr_offset:bnr_offset + len(bnr)] = bnr
    image[fst_offset:fst_offset + len(fst)] = fst
    return bytes(image)


def game_file(game_id: str, title: str) -> str:
    return f"{title} [{game_id}].iso"


def game_path(game_id: str, title: str) -> str:
    """A game's image below /games: in its folder (FOLDERS), if it has one."""
    folder = FOLDERS.get(game_id)
    return f"{folder}/{game_file(game_id, title)}" if folder else game_file(game_id, title)


def library_order(probe: bool) -> list[str]:
    """The titles in the order the Library lists them. Swiss flattens the
    folders in /games into one list and sorts it by path, case aside
    (files.c), so a game in a folder sorts by its folder's name."""
    games = [(game_id, title) for game_id, title in GAMES] + [(i, t) for i, t, _ in DAMAGED]
    if probe:
        games.append(PROBE_GAME)
    return [title for game_id, title in sorted(games, key=lambda g: game_path(*g).lower())]


def outer_header() -> bytes:
    """The disc itself: a header Dolphin accepts, with an empty file table."""
    area = bytearray(FST_OFFSET + 12)
    area[:0x440] = disc_header(*DISC)
    struct.pack_into(">III", area, 0x424, FST_OFFSET, 12, 12)
    struct.pack_into(">BBHII", area, FST_OFFSET, 1, 0, 0, 0, 1)  # root directory, one entry
    return bytes(area)


def folder_picture(kind: str) -> bytes:
    """A folder's picture: shown, or a picture Indigo can read but must not
    (too big: 3 MB, padded with a chunk a reader skips), or one it can't
    (damaged: a bit of its image data flipped, so its CRC fails). The one
    shown carries a 1.5 MB chunk a reader skips as well: a chunk that long
    once overran the poster thread's stack in zlib-ng's CRC."""
    import io
    import zlib
    from PIL import Image

    out = io.BytesIO()
    Image.new("RGB", (192, 256), SHOWN_PICTURE if kind == "shown" else REFUSED_PICTURE).save(out, "PNG")
    data = out.getvalue()
    if kind in ("shown", "too big"):
        pad = bytes(3 << 20 if kind == "too big" else 3 << 19)
        data = (data[:-12] + struct.pack(">I", len(pad)) + b"paDd" + pad +
                struct.pack(">I", zlib.crc32(b"paDd" + pad)) + data[-12:])
    elif kind == "damaged":
        at = data.index(b"IDAT") + 8
        data = data[:at] + bytes([data[at] ^ 1]) + data[at + 1:]
    return data


def poster(index: int):
    """Poster art for one fictitious game: a gradient and a few shapes."""
    from PIL import Image, ImageDraw

    width, height = 384, 512
    hue = (index * 67) % 360
    top, bottom = _rgb(hue, 0.55, 0.22), _rgb((hue + 40) % 360, 0.75, 0.62)
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / (height - 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    light = poster_light(index)
    cx, cy, r = width // 2, 200, 70 + index * 6
    if index % 3 == 0:
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=light, width=14)
    elif index % 3 == 1:
        draw.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], outline=light, width=14)
    else:
        draw.rectangle([cx - r, cy - r, cx + r, cy + r], outline=light, width=14)
    for n in range(4):
        y = 360 + n * 26
        draw.rectangle([48 + n * 22, y, width - 48 - n * 22, y + 10], fill=light)
    return image


def poster_light(index: int) -> tuple[int, int, int]:
    """The colour of a poster's shapes, which its game's banner doesn't have."""
    return _rgb((index * 67 + 180) % 360, 0.35, 0.92)


def still(index: int):
    """A gameplay still for one fictitious game: a sky, hills and a sun."""
    from PIL import Image, ImageDraw

    width, height = 640, 480
    hue = (index * 67) % 360
    sky, horizon = _rgb(hue, 0.45, 0.35), _rgb((hue + 30) % 360, 0.35, 0.9)
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / (height - 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(sky, horizon)))
    sun = _rgb((hue + 180) % 360, 0.3, 0.98)
    draw.ellipse([420 - index * 12, 70, 500 - index * 12, 150], fill=sun)
    for n, (base, rise) in enumerate(((330, 90), (370, 60), (410, 40))):
        ground = _rgb((hue + 90 + n * 20) % 360, 0.6, 0.25 + n * 0.12)
        points = [(x, base - rise * abs(((x + index * 40 + n * 90) % 320) - 160) / 160)
                  for x in range(0, width + 1, 32)]
        draw.polygon([(0, height)] + points + [(width, height)], fill=ground)
    return image


def disc_art(index: int):
    """A disc's print for one fictitious game, as GameTDB draws one: the disc
    filling the square, clear outside it and in its hole, with rings, a
    shape for a logo and a strip of small marks."""
    from PIL import Image, ImageDraw

    size = 160
    hue = (index * 67 + 20) % 360
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse([0, 0, size - 1, size - 1], fill=_rgb(hue, 0.6, 0.55) + (255,))
    for n, radius in enumerate((74, 62, 50)):
        ring = _rgb((hue + 30 * (n + 1)) % 360, 0.5, 0.8)
        draw.ellipse([80 - radius, 80 - radius, 80 + radius, 80 + radius], outline=ring + (255,), width=3)
    light = _rgb((hue + 180) % 360, 0.3, 0.97) + (255,)
    if index % 2 == 0:
        draw.rectangle([46, 26, 114, 46], fill=light)
    else:
        draw.polygon([(80, 18), (110, 46), (50, 46)], fill=light)
    for n in range(5):
        draw.rectangle([44 + n * 15, 120, 52 + n * 15, 128], fill=light)
    draw.ellipse([58, 58, 102, 102], fill=_rgb(hue, 0.2, 0.85) + (255,))
    draw.ellipse([69, 69, 91, 91], fill=(0, 0, 0, 0))
    return image


def _rgb(hue: float, saturation: float, value: float) -> tuple[int, int, int]:
    import colorsys
    return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(hue / 360, saturation, value))


def build_pack(folder: Path, pak: Path, kind: str) -> bool:
    """posters.pak, stills.pak or discs.pak, from pictures drawn here."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    import poster_pack

    try:
        poster_pack.resolve_gxtexconv(None)
    except poster_pack.PackError as error:
        print(f"card.py: no {kind} ({error})", file=sys.stderr)
        return False
    draw, missing = {"posters": (poster, NO_POSTER), "stills": (still, NO_STILL),
                     "discs": (disc_art, NO_DISC)}[kind]
    art = folder / kind
    art.mkdir()
    records = []
    for index, (game_id, _) in enumerate(GAMES):
        if game_id in missing:
            continue
        path = art / f"{game_id}.png"
        draw(index).save(path)
        records.append({"game_id": game_id, "source": f"{kind}/{game_id}.png", "universal": False,
                        "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "note": "Drawn by buildtools/ui/emulator/card.py for the emulator test"})
    manifest = folder / f"{kind}.json"
    manifest.write_text(json.dumps({"version": 1, "kind": kind, "records": records}))
    poster_pack.generate(str(manifest), str(pak), art_root=str(folder))
    return True


def manual_page(n: int):
    """Page n of MANUAL_GAME's manual, as a 300 dpi scan of a GameCube
    manual's page would be: paper, the band, two guide pictures; the last
    page has lines standing for text below them, which make it taller."""
    from PIL import Image

    last = n == len(MANUAL_BANDS) - 1
    width, height, margin = 1350, 2500 if last else 2100, 90
    inner = width - 2 * margin
    page = Image.new("RGB", (width, height), MANUAL_PAPER)
    page.paste(MANUAL_BANDS[n], (margin, margin, width - margin, margin + 110))
    y = margin + 140
    for name in MANUAL_PICTURES[2 * n:2 * n + 2]:
        with Image.open(GUIDE_PICTURES / f"{name}.png") as picture:
            shot = picture.convert("RGB").resize((inner, inner * 3 // 4), Image.Resampling.LANCZOS)
        page.paste(shot, (margin, y))
        y += shot.size[1] + 30
    for line in range(12 if last else 0):
        page.paste((40, 40, 48), (margin, y + line * 30, margin + inner * (60 + line * 7 % 40) // 100, y + line * 30 + 12))
    return page


def build_manuals(manuals: Path) -> int:
    """swiss/ui/manuals: MANUAL_GAME's manual and BROKEN_MANUAL_GAME's, cut
    short; returns the pages of the first. Without the guide's pictures (a
    copy of buildtools/ui alone) there are none, with a notice."""
    if not GUIDE_PICTURES.is_dir():
        print(f"card.py: no manuals ({GUIDE_PICTURES} is missing)", file=sys.stderr)
        return 0
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    import manual_pack

    manuals.mkdir(parents=True, exist_ok=True)
    pak = manual_pack.build([manual_page(n) for n in range(len(MANUAL_BANDS))])
    (manuals / f"{MANUAL_GAME}.pak").write_bytes(pak)
    (manuals / f"{BROKEN_MANUAL_GAME}.pak").write_bytes(pak[:len(pak) // 2])
    return len(MANUAL_BANDS)


# Apps, sorted as Apps shows them. Arcade, first, has no picture.
APPS = ("Arcade", "Pixel Painter", "Starfield", "toolbox")
APP_STUB = b"a stand-in for a program: Apps lists it, nothing can run it"


def app_picture(kind: str):
    """An app's picture: a small pixel-art icon, a poster, or a Homebrew
    Channel banner, so Apps shows each shape Indigo fits to a card."""
    from PIL import Image, ImageDraw

    if kind == "painter":
        image = Image.new("RGB", (16, 16), _rgb(260, 0.5, 0.3))
        for y in range(16):
            for x in range(16):
                if (x - 7.5) ** 2 + (y - 7.5) ** 2 < 36:
                    image.putpixel((x, y), _rgb((x * 24 + y * 8) % 360, 0.7, 0.95))
        return image
    if kind == "starfield":
        width, height = 300, 400
        image = Image.new("RGB", (width, height))
        draw = ImageDraw.Draw(image)
        for y in range(height):
            draw.line([(0, y), (width, y)], fill=_rgb(230, 0.8, 0.12 + 0.3 * y / height))
        for n in range(60):
            x, y = (n * 97) % width, (n * 53) % (height - 60)
            draw.ellipse([x, y, x + 2 + n % 3, y + 2 + n % 3], fill=(255, 250, 220))
        draw.polygon([(150, 250), (190, 330), (110, 330)], fill=_rgb(20, 0.6, 0.9))
        return image
    image = Image.new("RGB", (128, 48), _rgb(150, 0.6, 0.45))
    draw = ImageDraw.Draw(image)
    for n in range(4):
        draw.rectangle([10 + n * 30, 12, 30 + n * 30, 36], outline=(240, 240, 240), width=3)
    return image


def app_order(probe: bool) -> list[str]:
    """The apps in the order Apps lists them: by name."""
    return sorted(APPS + ((PROBE_APP[:-4],) if probe else ()), key=str.lower)


def build_apps(apps: Path) -> int:
    """/apps, as Apps reads it; returns how many apps it should list."""
    apps.mkdir()
    (apps / "Arcade.dol").write_bytes(APP_STUB)
    for name, kind in (("Pixel Painter", "painter"), ("Starfield", "starfield")):
        (apps / f"{name}.dol").write_bytes(APP_STUB)
        app_picture(kind).save(apps / f"{name}.png")
    toolbox = apps / "Toolbox"
    toolbox.mkdir()
    (toolbox / "toolbox.dol").write_bytes(APP_STUB)
    (toolbox / "boot.dol").write_bytes(APP_STUB)  # the Wii's
    (toolbox / "meta.xml").write_text("<app><name>Toolbox</name></app>\n")
    app_picture("banner").save(toolbox / "icon.png")
    (apps / "readme.txt").write_text("Not a program: Apps leaves it out.\n")
    return len(APPS)


def populate(root: Path, work: Path, posters: bool = True, probe: Path | None = None,
             foreign: int = 0) -> dict[str, object]:
    """What the disc and the card both hold: /games, the packs and
    descriptions in /swiss/ui, and /apps. The first game, whose launch the
    route lets fail, has the region code foreign: a console's other region,
    so the menu must come back from that region's video mode."""
    (root / "games").mkdir(parents=True, exist_ok=True)
    (root / "swiss/ui").mkdir(parents=True, exist_ok=True)
    for index, (game_id, title) in enumerate(GAMES):
        path = root / "games" / game_path(game_id, title)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(game_image(index, game_id, title, foreign if index == 0 else 0))
    for game_id, title, image in DAMAGED:
        (root / "games" / game_file(game_id, title)).write_bytes(image(game_id, title))
    if probe:
        (root / "games" / game_file(*PROBE_GAME)).write_bytes(probe_image(probe.read_bytes()))
    for name in STRAYS:
        if name.endswith("/"):
            (root / "games" / name).mkdir()
        else:
            (root / "games" / name).write_text("Not a game.\n")
    for folder, kind in FOLDER_PICTURES.items():
        (root / "games" / f"{folder}.png").write_bytes(folder_picture(kind))
    with_posters = posters and build_pack(work, root / "swiss/ui/posters.pak", "posters")
    with_stills = posters and build_pack(work, root / "swiss/ui/stills.pak", "stills")
    with_discs = posters and build_pack(work, root / "swiss/ui/discs.pak", "discs")
    (root / "swiss/ui/descriptions.txt").write_text(
        "# Descriptions of the demonstration disc's fictitious games\n" +
        "".join(f"{game_id} {text}\n" for game_id, text in sorted(DESCRIPTIONS.items())))
    manual = build_manuals(root / "swiss/ui/manuals")
    apps = build_apps(root / "apps")
    if probe:
        (root / "apps" / PROBE_APP).write_bytes(probe.read_bytes())
        apps += 1
    return {"games": len(GAMES), "damaged": len(DAMAGED), "manual_pages": manual,
            "posters": len(GAMES) - len(NO_POSTER) if with_posters else 0,
            "stills": len(GAMES) - len(NO_STILL) if with_stills else 0,
            "discs": len(GAMES) - len(NO_DISC) if with_discs else 0, "apps": apps,
            "probe": bool(probe)}


def build(out: Path, posters: bool = True, probe: Path | None = None, foreign: int = 0) -> dict[str, object]:
    if not shutil.which("genisoimage"):
        raise SystemExit("card.py: genisoimage is missing")
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        root = folder / "root"
        info = populate(root, folder, posters, probe, foreign)
        for path in sorted(root.rglob("*")) + [root]:
            os.utime(path, (1000000000, 1000000000))
        subprocess.run(["genisoimage", "-quiet", "-R", "-J", "-V", "INDIGO_DEMO", "-o", str(out), str(root)],
                       check=True)
    image = bytearray(out.read_bytes())
    if any(image[:SYSTEM_AREA]) or image[SYSTEM_AREA:SYSTEM_AREA + 6] != b"\x01CD001":
        raise SystemExit("card.py: genisoimage did not leave the system area free")
    header = outer_header()
    image[:len(header)] = header
    out.write_bytes(image)
    return {**info, "bytes": len(image)}


# The SD card: as big as a small real one, formatted as the SD Association's
# formatter does an SDHC card (FAT32, 32 KiB clusters). The file is sparse.
CARD_BYTES = 8 << 30
CLUSTER_BYTES = 64 * 512  # mkfs.fat -s 64
# The most pieces a game file can be in for Swiss to launch it (MAX_FRAGS in
# deviceHandler.h), and for a GC Loader to serve it.
MAX_FRAGMENTS = 40
MTOOLS = dict(os.environ, MTOOLS_SKIP_CHECK="1")


def build_card(out: Path, card_zip: Path, posters: bool = True, probe: Path | None = None,
               foreign: int = 0, settings: str | None = None, boot_iso: bool = False,
               fragments: int = 0, virtual_cards: bool = False) -> dict[str, object]:
    """A FAT32 SD card image set up as someone would: the release zip
    unpacked onto it, then games, the packs and apps beside it. Without
    settings it has no swiss/settings/global.ini, so Indigo starts in
    Settings, as on a new card; with them that file holds them."""
    import zipfile
    for tool in ("mkfs.fat", "mcopy"):
        if not shutil.which(tool):
            raise SystemExit(f"card.py: {tool} is missing (dosfstools, mtools)")
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        root = folder / "root"
        root.mkdir()
        with zipfile.ZipFile(card_zip) as package:
            package.extractall(root)
        info = populate(root, folder, posters, probe, foreign)
        if virtual_cards:
            sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "qa"))
            import make_test_saves
            saves = root / make_test_saves.SAVE_FOLDER
            saves.mkdir(parents=True, exist_ok=True)
            raw, _ = make_test_saves.virtual_card()
            (saves / make_test_saves.RAW_CARD_NAME).write_bytes(raw)
            backup = saves / make_test_saves.MEMORY_FOLDER_NAME
            backup.mkdir()
            for name, data in make_test_saves.memory_folder_files().items():
                (backup / name).write_bytes(data)
            info["virtual_cards"] = {"images": 1, "saves": 2, "physical_slots": False,
                                     "folders": 1, "folder_saves": len(make_test_saves.memory_folder_files())}
        if settings is not None:  # a card that has been used: its settings folders made
            (root / "swiss/settings/game").mkdir(parents=True, exist_ok=True)
            (root / "swiss/settings/global.ini").write_text(settings.replace("\n", "\r\n"))
        if boot_iso:
            (root / "boot.iso").write_bytes(probe_image((root / "ipl.dol").read_bytes(), *BOOT_ISO))
        if fragments:  # the probe's game, with enough clusters for that many pieces
            game = root / "games" / game_file(*PROBE_GAME)
            game.write_bytes(game.read_bytes().ljust(fragments * CLUSTER_BYTES, b"\0"))
        with open(out, "wb") as image:
            image.truncate(CARD_BYTES)
        subprocess.run(["mkfs.fat", "-F", "32", "-s", "64", "-n", "INDIGO", str(out)], check=True,
                       capture_output=True)
        for entry in sorted(root.iterdir()):
            subprocess.run(["mcopy", "-s", "-i", str(out), str(entry), "::/"], check=True, env=MTOOLS,
                           capture_output=True)
    if fragments:
        info["fragments"] = fragment(out, f"games/{game_file(*PROBE_GAME)}", fragments)
    return {**info, "zip": card_zip.name, "bytes": CARD_BYTES}


def fragment(image: Path, path: str, pieces: int) -> int:
    """Move a file on a card image (FAT32, as build_card makes it) into that
    many runs of clusters, a free cluster between each, as a copy onto a card
    that has seen deletions can leave it. Returns how many runs it is in."""
    with open(image, "r+b") as disk:
        boot = disk.read(512)
        sector, per_cluster, reserved, fats = struct.unpack_from("<HBHB", boot, 11)
        total, fat_sectors = struct.unpack_from("<I", boot, 32)[0], struct.unpack_from("<I", boot, 36)[0]
        cluster = sector * per_cluster
        fat_at, data_at = reserved * sector, (reserved + fats * fat_sectors) * sector
        limit = (total - data_at // sector) // per_cluster + 2  # cluster numbers start at 2
        disk.seek(fat_at)
        fat = list(struct.unpack(f"<{fat_sectors * sector // 4}I", disk.read(fat_sectors * sector)))

        def chain(first: int) -> list[int]:
            clusters = []
            while 2 <= first < 0x0FFFFFF8:
                clusters.append(first)
                first = fat[first] & 0x0FFFFFFF
            return clusters

        def entries(first: int):
            """(name, where its entry is, the entry) for each file in a directory."""
            parts: dict[int, str] = {}
            for number in chain(first):
                at = data_at + (number - 2) * cluster
                disk.seek(at)
                block = disk.read(cluster)
                for i in range(0, cluster, 32):
                    entry = block[i:i + 32]
                    if entry[0] == 0:
                        return
                    if entry[0] == 0xE5:
                        parts = {}
                    elif entry[11] == 0x0F:  # a piece of a long name
                        parts[entry[0] & 0x1F] = (entry[1:11] + entry[14:26] + entry[28:32]).decode("utf-16-le")
                    else:
                        base, ext = entry[0:8].decode("latin-1").rstrip(), entry[8:11].decode("latin-1").rstrip()
                        name = "".join(parts[k] for k in sorted(parts)).split("\0")[0]
                        yield name or base + (f".{ext}" if ext else ""), at + i, entry
                        parts = {}

        first, where = struct.unpack_from("<I", boot, 44)[0], 0
        for part in path.split("/"):
            found = next(((at, entry) for name, at, entry in entries(first) if name.lower() == part.lower()), None)
            if found is None:
                raise FileNotFoundError(path)
            where, entry = found
            first = struct.unpack_from("<H", entry, 20)[0] << 16 | struct.unpack_from("<H", entry, 26)[0]
        old = chain(first)
        if len(old) < pieces:
            raise ValueError(f"{path} has {len(old)} clusters, too few for {pieces} pieces")
        new, number = [], limit - len(old) - pieces  # at the card's end, which build_card leaves free
        for piece in range(pieces):
            size = len(old) // pieces + (piece < len(old) % pieces)
            new += range(number, number + size)
            number += size + 1
        if any(fat[n] for n in new):
            raise ValueError("the card's end is not free")
        for source, target in zip(old, new):
            disk.seek(data_at + (source - 2) * cluster)
            data = disk.read(cluster)
            disk.seek(data_at + (target - 2) * cluster)
            disk.write(data)
        for source in old:
            fat[source] = 0
        for i, target in enumerate(new):
            fat[target] = new[i + 1] if i + 1 < len(new) else 0x0FFFFFFF
        table = struct.pack(f"<{len(fat)}I", *fat)
        for copy in range(fats):
            disk.seek(fat_at + copy * fat_sectors * sector)
            disk.write(table)
        disk.seek(where + 20)
        disk.write(struct.pack("<H", new[0] >> 16))
        disk.seek(where + 26)
        disk.write(struct.pack("<H", new[0] & 0xFFFF))
    return 1 + sum(b != a + 1 for a, b in zip(new, new[1:]))


# A second SD card for the File Browser's right pane (SD2SP2 beside a GC
# Loader): three small text files, a folder, and a file big enough that a copy
# of it is still running when the route stops it. Memory Cards later finds a
# card image on it where Swiss's Emulate Memory Card keeps Slot A's for a
# game started from it.
SECOND_BYTES = 256 << 20
SECOND_FILES = {"a-one.txt": b"one\r\n" * 300, "b-two.txt": b"two\r\n" * 700,
                "c-three.txt": b"three\r\n" * 50}
SECOND_BIG = ("big.bin", 160 << 20)
SECOND_IMAGE = "swiss/saves/MemoryCardA.USA.raw"


def second_card(out: Path) -> dict[str, object]:
    """A FAT32 card holding SECOND_FILES, an empty folder (backups),
    SECOND_BIG, a repeating pattern, and at SECOND_IMAGE the public
    two-save card image (make_test_saves.virtual_card)."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "qa"))
    import make_test_saves
    with open(out, "wb") as image:
        image.truncate(SECOND_BYTES)
    subprocess.run(["mkfs.fat", "-F", "32", "-n", "SECOND", str(out)], check=True, capture_output=True)
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        for name, data in SECOND_FILES.items():
            (folder / name).write_bytes(data)
        with open(folder / SECOND_BIG[0], "wb") as big:
            block = bytes(range(256)) * 4096
            for _ in range(SECOND_BIG[1] // len(block)):
                big.write(block)
        for entry in sorted(folder.iterdir()):
            subprocess.run(["mcopy", "-i", str(out), str(entry), "::/"], check=True, env=MTOOLS,
                           capture_output=True)
        raw = folder / Path(SECOND_IMAGE).name
        raw.write_bytes(make_test_saves.virtual_card()[0])
        for made in ("::/backups", "::/swiss", "::/swiss/saves"):
            subprocess.run(["mmd", "-i", str(out), made], check=True, env=MTOOLS, capture_output=True)
        subprocess.run(["mcopy", "-i", str(out), str(raw), f"::/{SECOND_IMAGE}"], check=True,
                       env=MTOOLS, capture_output=True)
    return {"files": sorted(SECOND_FILES) + [SECOND_BIG[0], "backups/", SECOND_IMAGE],
            "bytes": SECOND_BYTES}


def free_sectors(card: Path) -> list[tuple[int, int]]:
    """A FAT32 card's free clusters, as sector ranges (first, last): where a
    new file's data would go, and nothing that is there already."""
    with open(card, "rb") as image:
        boot = image.read(512)
        sector, per_cluster = struct.unpack_from("<HB", boot, 11)
        reserved, fats = struct.unpack_from("<HB", boot, 14)
        total, fat_sectors = struct.unpack_from("<II", boot, 32)
        image.seek(reserved * sector)
        fat = array.array("I", image.read(fat_sectors * sector))
    if sys.byteorder != "little":
        fat.byteswap()
    first_data = reserved + fats * fat_sectors
    clusters = min((total - first_data) // per_cluster, len(fat) - 2)
    ranges: list[tuple[int, int]] = []
    for cluster in range(2, clusters + 2):
        if fat[cluster] & 0x0FFFFFFF:
            continue
        start = first_data + (cluster - 2) * per_cluster
        if ranges and ranges[-1][1] + 1 == start:
            ranges[-1] = (ranges[-1][0], start + per_cluster - 1)
        else:
            ranges.append((start, start + per_cluster - 1))
    return ranges


def listing(card: Path, folder: str = "") -> list[str]:
    """A card's folder (its top by default) as the File Browser sorts it:
    folders first, then by name ignoring case (fileComparator), after its
    ".." row."""
    where = "::/" + (folder.strip("/") + "/" if folder.strip("/") else "")
    result = subprocess.run(["mdir", "-b", "-i", str(card), where], capture_output=True, text=True,
                            env=MTOOLS, check=True)
    names = [line.strip()[len(where):] for line in result.stdout.splitlines()
             if line.strip().startswith(where)]
    return [".."] + sorted(names, key=lambda name: (not name.endswith("/"), name.rstrip("/").lower()))


def read_card(card: Path, path: str) -> bytes | None:
    """A file from the card image, or None when it isn't there."""
    result = subprocess.run(["mcopy", "-n", "-i", str(card), f"::/{path}", "-"], capture_output=True, env=MTOOLS)
    return result.stdout if result.returncode == 0 else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("--no-posters", action="store_true")
    parser.add_argument("--probe", type=Path, help="the probe DOL, to add as a game and an app")
    parser.add_argument("--card-zip", type=Path, help="make an SD card image from this release zip instead")
    args = parser.parse_args(argv)
    if args.card_zip:
        print(json.dumps(build_card(args.out, args.card_zip, posters=not args.no_posters, probe=args.probe)))
    else:
        print(json.dumps(build(args.out, posters=not args.no_posters, probe=args.probe)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
