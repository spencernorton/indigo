#!/usr/bin/env python3
"""Boot an Indigo DOL in Dolphin and drive its menus with a controller.

Dolphin (headless: software OpenGL on a virtual X server) boots the DOL with
the demonstration disc (card.py) in the drive. Once Home is up a controller
is plugged in (dsu_pad.py) and a route of button presses walks the cube and
opens each face. Every check compares the screen with itself earlier in the
same run, never with stored pictures, so a redesign does not break the test
while a crash, a hang, a black screen or a broken control does:

  - Home appears after boot, with a face's name under the cube;
  - turning the cube changes the name, five turns come back to the first
    (the disc has apps, so Apps is a fifth face), turning back undoes a
    turn, and the five names differ;
  - A on a face opens another screen, and B comes back to the same face;
  - in the Library, RIGHT and LEFT move between games and back again, and
    A opens a game's details; there UP and A open the game's settings, B
    comes back to the details, and B again to the game;
  - on the Source face, Change Source opens the device picker, RIGHT shows
    another device and B leaves it;
  - on the System face, Memory Cards opens with a memory card in each slot
    (GCI folders of made-up saves, ../qa/make_test_saves.py): RIGHT and LEFT
    move from save to save and across to Slot B and back, DOWN scrolls a
    stack, R and L choose a named storage place and back, A opens the box
    beside a save and B closes it, Copy puts the save on Slot B (its folder
    gains the file, the same blocks), Move is dimmed for it then, Erase
    removes it from Slot A's folder, and B leaves; File Browser opens its
    two panes, RIGHT and LEFT move between them, A opens a folder in either
    and X comes back, Z opens the Actions box and B closes it, R opens the right
    pane's storage menu and a memory card chosen there shows its saves, Y
    swaps the sides and back, L opens the left pane's menu, on the disc a
    device that isn't there says so in its pane, and B leaves;
  - on the Settings face, Setup > Console > Down Face None takes Apps off the
    cube (System's next face is Library), File Browser puts a face of its
    own there (A opens the File Browser, B comes back to that face) and
    Apps puts Apps back;
  - Setup > Console > Cube Classic lays the faces out as the GameCube's menu
    does, Library between them: from Settings, LEFT goes nowhere and RIGHT
    twice is Library, then System; there UP goes nowhere and B is Library;
    UP is Source, DOWN Library, DOWN Apps and UP Library. Cube goes back to
    Infinite after;
  - Setup > Library > Library Folders On shows the disc's folders in the
    Library: an empty folder opens with only its way back, A opens a folder
    and a folder in it, which lists the game a level further down too, and B
    goes back up a folder at a time to the same card, then Home; a folder's
    picture shows as its poster, and a picture too big or damaged never does;
  - on Apps, RIGHT and LEFT move between the disc's apps and back, and A
    starts the probe (probe/probe.c), which reports what the hand-off left
    it: the menu music stopped, nothing still writing to memory, and its own
    path to start from;
  - nothing crashes: Dolphin emulates the MMU, so an invalid memory access
    stops Indigo on its exception screen as it would on a console, and
    Dolphin's own log reports no exception or invalid access.

The game route launches the probe from the Library as a game instead: the
probe must see the game's ID, the full 24 MB and the music stopped.

With --storage sd2sp2 (or sdgecko-b) there is no disc: the release zip is
unpacked onto an SD card image (card.build_card) and Indigo boots from the
zip's own ipl.dol. A new card has no settings yet, so Indigo starts in
Settings and the route saves them first; afterwards it reads back from the
card what Indigo wrote there.

The files route boots from a GC Loader with a second SD card in SD2SP2 and
copies, keeps both, moves, renames, deletes, stops a copy and a move, and
copies into a folder between them, checking each on the card images, and
a game's Detail from there shows its poster. Then the SD2SP2 becomes the
Source, and Memory Cards, settings on the GC Loader, opens the card image
Swiss keeps on the SD2SP2, erases a save from it and moves the other to the
GC Loader. Then it boots again with that SD card failing its writes, and a
copy fails. Last, A on the probe's game in the File Browser opens its Game
Detail, B comes back to the same row, and Detail's Launch Game starts the
probe as that game.

The virtual-cards route leaves both physical slots empty, browses a public
synthetic RAW image on SD and exports a GCI into the other SD column, then
erases one of the image's saves, moves the other out to the SD column and
moves a .gci of it back in. The exported payloads and the image, read as the
console reads a card, are checked on the FAT image.

usage: run.py DOL --out DIR [--route smoke|tour|game|save|virtual-cards|folders|files] [--probe DOL]
              [--region pal|pal60|ntsc]
              [--cable composite|component]
              [--storage dvd|sd2sp2|sdgecko-b|gcloader --card-zip ZIP] [--disc ISO]
Writes DIR/report.json, DIR/summary.md, DIR/sheet.png (every checkpoint),
the checkpoint pictures and Dolphin's output. Exit 0: passed. 1: Indigo
failed a check. 2: the harness or the emulator could not run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(1, str(Path(__file__).resolve().parents[1] / "qa"))
import card  # noqa: E402
import dsu_pad  # noqa: E402
import make_test_saves  # noqa: E402

WIDTH, HEIGHT = 640, 480
# Where text the route reads sits (x0, y0, x1, y1): the face's name under the
# cube on Home, the selected game's title in the Library (the focused
# device's name in the Source picker sits there too), and its title on the
# game's details.
LABEL_BOX = (200, 372, 440, 396)
# Home's button hints under the face's name ("TURN  A OPEN"), dimmer than it:
# lit at HINT_LEVEL, where the backdrop and waves around them are not. Jet
# Black draws their words gray at the same luma, about 120 at the brightest,
# so that they are gone is read at HINT_GONE_LEVEL, which only the backdrop
# (56 at most, in any color) and nothing drawn stays under; non-default.ini,
# where that is checked, has no waves.
HOME_HINT_BOX, HINT_LEVEL, HINT_GONE_LEVEL = (240, 422, 400, 446), 120, 80
TITLE_BOX = (200, 338, 440, 362)
FOLDER_LAYOUTS = {
    "Horizontal": (TITLE_BOX, "LEFT", "RIGHT"),
    "Vertical": ((258, 190, 605, 224), "UP", "DOWN"),
    "Grid": ((180, 374, 460, 402), "LEFT", "RIGHT"),
    "Spotlight": ((378, 78, 607, 108), "LEFT", "RIGHT"),
    # The jewel case's title stands where the carousel's does.
    "Jewel Case": (TITLE_BOX, "LEFT", "RIGHT"),
}
# Settings' page title, including the "Storage" page a new card opens with.
SETTINGS_TITLE_BOX = (30, 46, 230, 80)
COUNTER_BOX = (560, 120, 610, 142)  # Settings' "row / rows", shown while a row has the focus
# Settings' six visible rows: the top of each, 40 lines apart, the box of a
# row's label, and the box between label and value that the focus lights.
SETTINGS_ROW_TOPS = tuple(148 + 40 * n for n in range(6))
SETTINGS_ROW_LABEL = (45, 4, 300, 30)
SETTINGS_ROW_FILL = (320, 2, 480, 30)
DETAIL_TITLE_BOX = (264, 106, 600, 134)
# The cover on the game's details from the File Browser, inside its frame, and
# how far (mean, 0-255) a frame of it may differ from where it settles.
DETAIL_CARD_BOX = (56, 104, 210, 300)
DETAIL_CARD_MOVED = 8.0
# A poster on Detail's card: this many pixels of its shapes' colour
# (card.poster_light) in DETAIL_CARD_BOX, which its banner never has, within
# this many captured frames of Detail's first. The first details after boot
# read their poster before they show, so it is there at once; a poster that
# came late would fade in about half a second on, and fail.
POSTER_PIXELS = 1000
POSTER_FRAMES = 2
# Memory Cards: the focused save's name in the info bar, each stack's header
# ("A  Open" and its free blocks), the buttons along the bottom, where the
# arrow above the left stack shows once it scrolls, and the maroon box that
# says an operation is done. Its words are bold and close together, so two
# saves' names can overlap by more than half: they are told apart by
# same_text, not overlap.
INFO_BOX = (164, 377, 590, 396)
LEFT_HEADER_BOX, RIGHT_HEADER_BOX = (66, 56, 222, 92), (354, 56, 510, 92)
MEMORY_LEFT_PATH_BOX = (128, 66, 280, 92)
FOOTER_BOX = (30, 442, 610, 464)
# The File Browser's panes: their path lines ("/", "/apps"), each from the
# pane's outer edge, which Menu Widescreen moves out to x -67 (stage_box
# places it on the capture); and the right pane's top rows.
FILES_PATH_BOX, FILES_WIDE_PATH_BOX = (40, 90, 220, 106), (-65, 90, 115, 106)
FILES_RIGHT_PATH_BOX = (330, 90, 510, 106)
FILES_RIGHT_ROWS_BOX = (366, 116, 560, 196)
FILES_LEFT_ROWS_BOX = (78, 116, 272, 330)
FILES_INFO_TITLE_BOX = (164, 371, 590, 392)  # the info bar's name
# Each pane's storage name, its first 88 px: clear of the SOURCE chip after a
# short name on the left, and 288 px apart, so a name moves between them. Only
# the focused pane's is white; the other's is quiet, grey under Jet Black.
FILES_NAME_BOXES = ((40, 62, 128, 88), (328, 62, 416, 88))
FILES_RIGHT_MESSAGE_BOX = (340, 236, 588, 282)  # why the right pane is not ready
SAVE_DETAILS_EYEBROW_BOX = (100, 108, 274, 132)
SAVE_DETAILS_TITLE_BOX = (100, 132, 540, 168)
SAVE_DETAILS_SIZE_BOX = (350, 178, 528, 212)
SAVE_DETAILS_BLOCKS_BOX = (122, 178, 302, 212)
SAVE_DETAILS_SOURCE_BOX = (246, 244, 540, 270)
LIBRARY_SAVES_SUMMARY_BOX = (266, 204, 538, 224)
LIBRARY_SAVES_UPDATED_BOX = (308, 224, 586, 241)
LIBRARY_SAVES_TAG_BOX = (540, 204, 586, 224)  # SAVES, right of the count
SAVE_DETAILS_ICON_BOX = (246, 304, 540, 330)
SAVE_DETAILS_UPDATED_BOX = (246, 274, 540, 300)
SAVE_DETAILS_ACTIONS_BOX = (104, 360, 536, 387)
RAW_ICON_BOX = (68, 100, 120, 161)  # selected cell 0; excludes banner/info bar
FOLDER_PAGE_TITLE_BOX = (48, 78, 300, 105)
FOLDER_PATH_BOX = (48, 130, 584, 259)
FOLDER_CONTENTS_BOX = (48, 280, 584, 339)
FOLDER_COLOR_BOX = (240, 341, 570, 373)
FOLDER_CUBE_BOX = (66, 100, 132, 164)
FOLDER_AZURE = (68, 170, 230)
FOLDER_HINT_BOX = (500, 442, 610, 466)
UP_ARROW_BOX = (169, 96, 184, 112)
MESSAGE_BOX = (160, 200, 480, 250)
MESSAGE_COLOUR = (120, 16, 36)  # the same under every Menu Color
MESSAGE_PIXELS = 9000  # of the box's 16000: its fill, not a red icon or two behind it
# A game's manual (card.MANUAL_GAME's): each page has a band of its own colour
# along its top, which at full width from the page's top fills this box...
MANUAL_BAND_BOX = (120, 28, 520, 58)
# ...and in the whole-page view, this one, the half-size page standing in
# the middle of the screen, the backdrop where the band was at full width.
MANUAL_WHOLE_BAND_BOX = (240, 24, 400, 36)
# Game Details' Manual row: its page count at the right, where without the
# row there is nothing; and the lines a message over Detail covers.
MANUAL_ROW_BOX = (470, 204, 580, 224)
MANUAL_ROW_PIXELS = 20  # "7 PAGES" lights about 50; nothing is drawn there without the row
MESSAGE_LINES_BOX = (60, 214, 580, 262)
MANUAL_SCROLL_SECONDS = 1.0
MANUAL_MEMORY = re.compile(r"manual: (\w{6}) open, (\d+) pages of (\d+) bytes, (\d+) bytes free \((\d+) before\)")
MANUAL_CLOSED = re.compile(r"manual: closed, (\d+) bytes free \((\d+) before\)")
ARROW_PIXELS = 20
# A save's art loads once nothing has been held for 15 VSyncs: a moment
# after a step, its comment replaces its file name in the info bar.
ART_SECONDS = 0.8
# Dolphin writes a GCI folder's files a second after the card's last write.
FLUSH_SECONDS = 10
TEXT_LEVEL = 160          # label text is bright; the waves behind it are not
SAME, DIFFERENT = 0.85, 0.5  # intersection over union of two label masks
# Waits count the console's own seconds (Emulator.emulated) when Dolphin
# reports them, so a busy machine slows a run instead of failing it; the
# machine's seconds still end a wait at WALL_FACTOR times as many, and never
# before WALL_FLOOR: a slow Dolphin must not cut a short press down to a frame.
BOOT_SECONDS = 120
SETTLE_SECONDS = 10
WALL_FACTOR = 5
WALL_FLOOR = 0.5
TICKS_PER_SECOND = 486_000_000  # the GameCube's CPU clock, which Dolphin's ticks count
# Of the console's time a button stays down, and up after. A press waits
# fresh (Route.press), so with Dolphin's reports a tenth of a second apart it
# lasts from this to about two reports more: 0.05 is at most about 0.2 s, well
# inside the 0.32 s a held direction takes to repeat (UI_MENU_INPUT_INITIAL_
# REPEAT_US), and still several frames.
PRESS_SECONDS = 0.05
# Library Folders: how long the route holds a press that opens or leaves a
# folder (library_folders), and how many pixels of a folder picture's colour
# make it on screen: a card in front is tens of thousands.
FOLDER_PRESS_SECONDS = 0.5
PICTURE_PIXELS = 3000
# A press the menu was too busy to see changes nothing on the screen; a person
# presses again, and press_until does, after MISSED_SECONDS, up to PRESSES times.
MISSED_SECONDS = 3
PRESSES = 3
FATAL = re.compile("|".join((
    r"(?:DSI|ISI|Program|Machine Check|Alignment) Exception", r"Unhandled exception",
    r"Segmentation fault", r"core dumped", r"\bPANIC\b", r"ASSERT(?:ION)? FAILED",
    r"Invalid (?:read|write) (?:from|to)", r"Unknown (?:opcode|instruction)",
    r"FIFO (?:is )?(?:overflowed|desync)", r"failed to compile shader", r"device lost",
    r"poster stack .*\(overrun\)",
    r"DABR: (?:write to|read of)",  # a thread's stack reached its guard (patch 0007)
)), re.I)
# card_art's poster thread reports how much of its stack it used each time it
# stops (Indigo's debug output, a development console's). An overrun stops at
# the stack's guard (DABR, above); this catches one that came close: a run
# that used more than this share of it fails.
POSTER_STACK_SHARE = 0.75
POSTER_STACK = re.compile(r"card_art: poster stack (\d+) of (\d+) bytes used")
# The DSP runs its real microcode (LLE): Dolphin's high-level stand-ins know
# libogc's audio library but not libogc2's, so the menu music's stop before a
# launch would never be answered. It runs in step with the CPU, not on a thread
# of its own, where it sometimes missed the mail that stops the music for a
# launch (AESND_Reset waits for it with interrupts off) on a busy machine.
DOLPHIN_INI = """[Core]
DSPHLE = False
DSPThread = False
MMU = True
GFXBackend = OGL
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Analytics]
Enabled = False
PermissionAsked = True
[Interface]
ConfirmStop = False
OnScreenDisplayMessages = False
[Input]
BackgroundInput = True
[Display]
Fullscreen = True
RenderToMain = True
"""


# A console's region: Dolphin's region for a program without one (a DOL), which
# the video hardware starts in, and the video format and 60 Hz flag in its
# SRAM, which Swiss reads its video mode from. Dolphin's own SRAM is an NTSC
# console's whatever the region, so a PAL run would turn NTSC once Settings
# saved; each run gets the SRAM of the console it is.
REGIONS = {"ntsc": (1, 0, False), "pal": (2, 1, False), "pal60": (2, 1, True)}
# The region of the game whose launch the route lets fail: the console's
# other one (card.REGION_CODES), so the menu must return from its video mode.
FOREIGN = {"ntsc": 2, "pal": 1, "pal60": 1}
# The video cable Dolphin reports: with a component cable (its "progressive
# scan"), Swiss's Auto video mode is 480p; without one, interlaced.
CABLES = {"composite": False, "component": True}
# Where the SD card goes: SD2SP2 in Serial Port 2, or an SD Gecko in Memory
# Card Slot B. 15 is the SD card adapter the emulator runner's Dolphin adds
# (buildtools/ci/runner/dolphin/). In a GC Loader, the card is the drive's,
# which serves its boot.iso as the disc (DOLPHIN_GCLOADER, patch 0006).
STORAGES = {"dvd": None, "sd2sp2": "SerialPort2", "sdgecko-b": "SlotB", "gcloader": None}
SD_CARD_DEVICE = 15
# Dolphin's memory card that is a folder of .gci files, one per save.
GCI_FOLDER_DEVICE = 8
# Rows on Settings' Storage page: DOWN past them reaches Save & Exit.
STORAGE_ROWS = 8
# Lit pixels in COUNTER_BOX that are its digits rather than nothing (19 for "1 / 8").
COUNTER_PIXELS = 6
# Settings to start with, put on the card as swiss/settings/global.ini (--settings).
SETTINGS = Path(__file__).resolve().parent / "settings"


def seeded(text: str) -> dict[str, str]:
    """The Key=Value lines of a settings file."""
    pairs = (line.split("=", 1) for line in text.splitlines() if "=" in line and not line.startswith("#"))
    return {key.strip(): value.strip() for key, value in pairs}

# The probe's report, in the order of its strip (probe.c).
PROBE_WORDS = ("magic", "version", "id0", "id1", "memsize", "console", "video", "bus", "core",
               "arena_lo", "arena_hi", "top", "ai_dma", "ai_cr", "stray_writes", "argc",
               "argv0", "argv1", "argv2", "argv3", "vi_dcr", "vi_clk_dtv", "crc")
PROBE_MAGIC = 0x1D160B0E
AI_DMA_ENABLE = 0x8000


class Failed(Exception):
    """Indigo did something the route says it must not."""


class Broken(Exception):
    """The harness or the emulator could not do its part."""


def legacy_folder_browser(rgb: np.ndarray) -> bool:
    """Swiss's default list: long 40-pixel rows beside its device panel.

    These are the actual drawFiles/DrawFileBrowserButton bounds, not a
    missing Indigo label: an Indigo fade or blank frame is allowed here.
    A line must contrast with both neighboring rows, so a pale background
    or a darkened legacy frame does not disguise its geometry.
    """
    if rgb.shape != (HEIGHT, WIDTH, 3):
        raise Broken(f"unexpected presented frame size: {rgb.shape}")
    gray = rgb.max(axis=2).astype(np.int16)

    def edge(x0: int, x1: int, y: int) -> bool:
        for row in range(y - 2, y + 3):
            contrast = gray[row, x0:x1] - np.maximum(gray[row - 4, x0:x1],
                                                   gray[row + 4, x0:x1])
            if np.count_nonzero(contrast > 12) >= (x1 - x0) * 0.90:
                return True
        return False

    rows = sum(edge(166, 592, y) for y in (105, 145, 185, 225, 265, 305, 345))
    # The device card at (20,90)-(143,210) is separate from the file rows.
    device = edge(36, 125, 90) and edge(36, 125, 210)
    return device and rows >= 1


def _edge(rgb: np.ndarray, x0: int, x1: int, y: int) -> int:
    """How lit a box's edge is along x0..x1 within two rows of y: its
    brightest row's mean, when nearly all of that row is bright (lilac, or
    grey under Jet Black, dimmed or not) and brighter than four rows off it
    on both sides; else 0."""
    best = 0
    for row in range(y - 2, y + 3):
        line = rgb[row, x0:x1].astype(np.int16)
        off = np.maximum(rgb[row - 4, x0:x1].max(axis=1), rgb[row + 4, x0:x1].max(axis=1))
        lit = (line.max(axis=1) >= 100) & (line.max(axis=1) - off > 30)
        if np.count_nonzero(lit) >= (x1 - x0) * 0.9:
            best = max(best, int(line.max(axis=1).mean()))
    return best


# The File Browser's boxes, by the spans of their edges that hold in both
# screen shapes (Menu Widescreen moves only the panes' outer edges): each
# pane box's top and bottom, the storage button above each pane, the gutter
# between the panes, and the info bar.
FILES_PANES = ((150, 300), (340, 490))
FILES_BUTTONS = ((80, 220), (440, 560))


def files_screen(rgb: np.ndarray, wide: bool = False) -> bool:
    """The File Browser's two panes side by side, with the info bar below."""
    if rgb.shape != (HEIGHT, WIDTH, 3):
        raise Broken(f"unexpected presented frame size: {rgb.shape}")
    rgb = detection_frame(rgb, wide)
    panes = all(_edge(rgb, x0, x1, y) for x0, x1 in FILES_PANES for y in (108, 340))
    buttons = all(_edge(rgb, x0, x1, y) for x0, x1 in FILES_BUTTONS for y in (28, 54))
    gutter = not _edge(rgb, 313, 327, 108)
    info = all(_edge(rgb, 150, 490, y) for y in (362, 431))
    return panes and buttons and gutter and info


def files_text(rgb: np.ndarray, wide: bool = False, right: bool = False,
               box: tuple[int, int, int, int] | None = None) -> np.ndarray:
    """A File Browser pane's text in a box, its path line by default. The
    left pane's outer edge moves with Menu Widescreen, outside the stage
    detection_frame keeps, so its path is read from the capture itself."""
    if box is None and not right:
        return text_mask(rgb.max(axis=2), stage_box(FILES_WIDE_PATH_BOX if wide else
                                                    FILES_PATH_BOX, wide))
    return text_mask(detection_frame(rgb, wide).max(axis=2),
                     box if box is not None else FILES_RIGHT_PATH_BOX)


# A storage menu (L or R) opens below its storage button, right-aligned 6 px
# in from its pane's right edge: its title box's top edge at y 114 and its
# items box's at y 148 run at least 124 px left from there (a row's focus
# outline has edges at 114 and 139, never 148). Menu Widescreen moves the
# right pane's right edge out with the stage, so the edges are read on the
# whole stage (files_stage) where UIFiles_Layout puts the pane.
def files_pane_x(pane: int, wide: bool = False) -> tuple[int, int]:
    """A File Browser pane's left and right edges in authored px, as
    UIFiles_Layout puts them: the outer edge 40 px in from the stage's
    (UIStage_Left/Right: 1/6 of 640 more each side in Menu Widescreen), the
    inner edges at 312 and 328 in both shapes."""
    margin = round(320 / 3) if wide else 0
    return (40 - margin, 312) if pane == 0 else (328, 600 + margin)


def files_menu_edges(pane: int, wide: bool = False) -> tuple[int, int]:
    """Where a box beside a row of pane surely has its top and bottom edges."""
    right = files_pane_x(pane, wide)[1]
    return right - 126, right - 12


FILES_MENU_EDGES = (files_menu_edges(0), files_menu_edges(1))


def files_stage(rgb: np.ndarray, wide: bool = False) -> tuple[np.ndarray, int]:
    """The capture in authored px over the whole stage, and the column of
    authored x 0. Like detection_frame, but keeping Menu Widescreen's sides:
    the letterbox's 640 columns span x -107 .. 747 there."""
    if not wide:
        return rgb, 0
    margin = round(320 / 3)
    return np.asarray(Image.fromarray(rgb[60:420]).resize(
        (WIDTH + 2 * margin, HEIGHT), Image.Resampling.NEAREST)), margin


def files_menu(rgb: np.ndarray, pane: int, wide: bool = False) -> bool:
    """A File Browser storage menu over pane's rows (0 left, 1 right)."""
    frame, at = files_stage(rgb, wide)
    x0, x1 = files_menu_edges(pane, wide)
    return bool(_edge(frame, x0 + at, x1 + at, 115) and _edge(frame, x0 + at, x1 + at, 149))


# A box beside a row ends short of its pane's left part, where a row's
# outline (the other pane's focus, a ghost row) goes on: 20 to 100 px in.
FILES_ROW_PROBES = tuple((files_pane_x(p)[0] + 20, files_pane_x(p)[0] + 100) for p in (0, 1))


def files_box(rgb: np.ndarray, pane: int, wide: bool = False) -> bool:
    """A box beside a row of pane's (Actions, a question, a storage menu):
    its items box's top and bottom edges across the right of the pane, not
    running on across the pane as a row's outline does."""
    frame, at = files_stage(rgb, wide)
    x0, x1 = (x + at for x in files_menu_edges(pane, wide))
    p0, p1 = (x + at for x in (files_pane_x(pane, wide)[0] + 20, files_pane_x(pane, wide)[0] + 100))
    edges = [y for y in range(116, 337) if _edge(frame, x0, x1, y) and not _edge(frame, p0, p1, y)]
    return sum(1 for a, b in zip([-99] + edges, edges) if b - a > 8) >= 2


def active_pane(rgb: np.ndarray, wide: bool = False) -> int:
    """Which of the File Browser's panes has the focus: 0 left, 1 right. Its
    box's edge is lit in full, the other's dimmed."""
    rgb = detection_frame(rgb, wide)
    left, right = (_edge(rgb, x0, x1, 108) for x0, x1 in FILES_PANES)
    return 0 if left > right else 1


class PresentedFrames:
    """Consume Dolphin's sequential PNG dumps, keeping bounded evidence.

    Dolphin finishes N before starting N+1. Read N only once N+1 exists;
    no screen polling interval can skip a rendered transition frame. Dumps
    outside spans are deleted too, so long waits do not fill the artifact.
    """
    LIMIT = 60000

    def __init__(self, folder: Path, out: Path) -> None:
        self.folder, self.out = folder, out
        self.spans: list[dict[str, object]] = []
        self.next = 1
        self.error: str | None = None
        self.done = False
        self.lock = threading.Lock()
        self.worker = threading.Thread(target=self._consume, daemon=True)
        self.worker.start()

    def latest(self) -> int:
        return max((int(p.stem.rsplit("_", 1)[1])
                    for p in self.folder.glob("framedump_*.png")), default=0)

    def begin(self, name: str) -> dict[str, object]:
        with self.lock:
            newest = self.latest()
            if newest == 0:
                raise Broken("Dolphin did not start native frame dumping")
            # Registration and the processed watermark share one lock. A
            # completed PNG cannot be deleted between choosing and starting
            # the span, even when directory enumeration sees an older file.
            span = {"transition": name, "first": max(newest, self.next), "last": None,
                    "frames": 0, "bad_frames": [], "digest": hashlib.sha256(),
                    "samples": []}
            self.spans.append(span)
        return span

    def end(self, span: dict[str, object]) -> None:
        with self.lock:
            span["end_requested"] = self.latest()
        until = time.monotonic() + 20
        # Presented XFB readback and PNG encoding are asynchronous. Two
        # advancing dump indices cover that pending tail before closing
        # the span; the successor file then proves its last PNG is complete.
        while self.latest() < span["end_requested"] + 2 and not self.error and time.monotonic() < until:
            time.sleep(0.02)
        with self.lock:
            span["last"] = self.latest()
        if span["last"] < span["end_requested"] + 2:
            raise Broken(self.error or "native frame-dump tail did not advance")
        while self.next <= span["last"] and not self.error and time.monotonic() < until:
            time.sleep(0.02)
        if self.error or self.next <= span["last"]:
            raise Broken(self.error or "native frame-dump reader did not keep up")
        if span["frames"] != span["last"] - span["first"] + 1:
            raise Broken(f"native frame-dump gap in {span['transition']}")

    def _consume(self) -> None:
        try:
            while True:
                path = self.folder / f"framedump_{self.next}.png"
                after = self.folder / f"framedump_{self.next + 1}.png"
                if not after.exists() and not self.done:
                    time.sleep(0.01)
                    continue
                if not path.exists():
                    if self.done and self.latest() < self.next:
                        break
                    raise Broken(f"native frame-dump gap at {self.next}")
                if self.next > self.LIMIT:
                    raise Broken("native frame-dump budget exceeded")
                with self.lock:
                    active = [s for s in self.spans if self.next >= s["first"] and
                              (s["last"] is None or self.next <= s["last"])]
                    if active:
                        with Image.open(path) as picture:
                            rgb = np.asarray(picture.convert("RGB"))
                    for span in active:
                        span["frames"] += 1
                        span["digest"].update(self.next.to_bytes(4, "big") + rgb.tobytes())
                        samples = span["samples"]
                        # First, middle candidate (powers of two), and last:
                        # three frame-sized buffers per span, never all frames.
                        if not samples:
                            samples.extend([(self.next, rgb.copy())] * 3)
                        if span["frames"] & (span["frames"] - 1) == 0:
                            samples[1] = (self.next, rgb.copy())
                        samples[2] = (self.next, rgb.copy())
                        if legacy_folder_browser(rgb):
                            self.out.mkdir(exist_ok=True)
                            name = f"legacy-{self.next}.png"
                            Image.fromarray(rgb).save(self.out / name)
                            span["bad_frames"].append(name)
                    path.unlink()
                    self.next += 1
        except Exception as error:
            self.error = f"{type(error).__name__}: {error}"

    def finish(self) -> None:
        self.done = True
        self.worker.join(20)
        if self.worker.is_alive() or self.error:
            raise Broken(self.error or "native frame-dump reader did not stop")

    def evidence(self, span: dict[str, object]) -> dict[str, object]:
        self.out.mkdir(exist_ok=True)
        samples = []
        for label, (index, rgb) in zip(("first", "middle", "last"), span["samples"]):
            name = f"{span['transition']}-{label}-{index}.png"
            Image.fromarray(rgb).save(self.out / name)
            samples.append(name)
        return {k: span[k] for k in ("transition", "first", "end_requested", "last", "frames", "bad_frames")} | {
            "sha256_rgb_sequence": span["digest"].hexdigest(), "samples": samples}


def dolphin_ini(storage: str = "dvd", card: Path | None = None, cards: Path | None = None,
                empty_slots: bool = False, second: Path | None = None) -> str:
    """Dolphin.ini: the SD card where the storage puts it, with cards a GCI
    folder memory card in each slot, cards/A and cards/B, and with second
    another SD card in SD2SP2 (beside a GC Loader's)."""
    core = ""
    if STORAGES[storage]:
        core += f"{STORAGES[storage]} = {SD_CARD_DEVICE}\nSP2SDCardImage = {card}\n"
    elif second:
        core += f"SerialPort2 = {SD_CARD_DEVICE}\nSP2SDCardImage = {second}\n"
    if cards:
        core += (f"SlotA = {GCI_FOLDER_DEVICE}\nSlotB = {GCI_FOLDER_DEVICE}\n"
                 f"GCIFolderAPathOverride = {cards / 'A'}\nGCIFolderBPathOverride = {cards / 'B'}\n")
    elif empty_slots:
        core += "SlotA = 0\nSlotB = 0\n"
    return DOLPHIN_INI.replace("[Core]\n", "[Core]\n" + core, 1)


def saves(folder: Path) -> set[str]:
    """The saves in a GCI folder memory card, by file name."""
    return {path.name for path in folder.glob("*.gci")}


def card_image_saves(image: bytes) -> dict[bytes, bytes]:
    """A card image's saves as the console reads them, by game, maker and
    name: the header's sums, the newer valid directory and block map (their
    counts compared signed, the first copy winning ties), the map's free
    count, and each save's chain, ending where it should. Independent of
    Indigo's own reader. Raises ValueError for an image a console would
    call broken."""
    block = 8192

    def sums(data: bytes) -> tuple[int, int]:
        words = struct.unpack(f">{len(data) // 2}H", data)
        value, inverse = sum(words) & 0xFFFF, sum(w ^ 0xFFFF for w in words) & 0xFFFF
        return (0 if value == 0xFFFF else value, 0 if inverse == 0xFFFF else inverse)

    def signed(value: int) -> int:
        return value - 0x10000 if value >= 0x8000 else value

    def newer(copies: list[bytes], valid: list[bool], at: int) -> bytes:
        if not any(valid):
            raise ValueError("neither copy sums")
        if not valid[0] or not valid[1]:
            return copies[valid.index(True)]
        counts = [signed(struct.unpack_from(">H", c, at)[0]) for c in copies]
        return copies[0] if counts[0] >= counts[1] else copies[1]

    if sums(image[:0x1FC]) != struct.unpack_from(">HH", image, 0x1FC):
        raise ValueError("the header doesn't sum")
    directories = [image[block:2 * block], image[2 * block:3 * block]]
    maps = [image[3 * block:4 * block], image[4 * block:5 * block]]
    directory = newer(directories, [sums(d[:0x1FFC]) == struct.unpack_from(">HH", d, 0x1FFC)
                                    for d in directories], 0x1FFA)
    block_map = newer(maps, [sums(m[4:]) == struct.unpack_from(">HH", m, 0) for m in maps], 4)
    blocks = len(image) // block
    links = [struct.unpack_from(">H", block_map, b * 2)[0] for b in range(blocks)]
    if sum(1 for b in range(5, blocks) if links[b] == 0) != struct.unpack_from(">H", block_map, 6)[0]:
        raise ValueError("the block map's free count is wrong")
    found: dict[bytes, bytes] = {}
    for place in range(127):
        entry = directory[place * 64:(place + 1) * 64]
        if entry[:4] == b"\xff" * 4:
            continue
        at, count = struct.unpack_from(">HH", entry, 0x36)
        body = bytearray()
        for _ in range(count):
            if not 5 <= at < blocks:
                raise ValueError("a save's chain leaves the card")
            body += image[at * block:(at + 1) * block]
            at = links[at]
        if at != 0xFFFF:
            raise ValueError("a save's chain doesn't end where its size says")
        found[entry[:6] + entry[8:40]] = entry + bytes(body)
    return found


def same_save(original: bytes, copy: bytes) -> bool:
    """A .gci and its copy on another card: the same game, maker and name in
    the entry, and the same blocks. The rest of the entry (where its blocks
    start on the card, when it was written) is the card's own."""
    return (len(copy) == len(original) > 64 and copy[0:6] == original[0:6] and
            copy[8:40] == original[8:40] and copy[64:] == original[64:])


# A copy's progress card in the File Browser: its "B Stop" glyph, red, at
# the card's left (nothing else on the page is red there), and the words of
# the File Browser's maroon message.
FILES_PROGRESS_STOP_BOX = (28, 278, 52, 300)
FILES_MESSAGE_TEXT_BOX = (170, 204, 470, 248)


def files_progress(rgb: np.ndarray, wide: bool = False) -> bool:
    """A copy's progress card over the File Browser."""
    x0, y0, x1, y1 = FILES_PROGRESS_STOP_BOX
    patch = detection_frame(rgb, wide)[y0:y1, x0:x1].astype(int)
    return int(((patch[..., 0] > 170) & (patch[..., 1] < 90) & (patch[..., 2] < 110)).sum()) >= 30


def message_up(rgb: np.ndarray, wide: bool = False) -> bool:
    """Memory Cards' maroon box over the middle of the screen."""
    rgb = detection_frame(rgb, wide)
    x0, y0, x1, y1 = MESSAGE_BOX
    return coloured(rgb[y0:y1, x0:x1], MESSAGE_COLOUR) >= MESSAGE_PIXELS


def manual_page(rgb: np.ndarray, wide: bool = False, whole: bool = False) -> int | None:
    """Which page of card.MANUAL_GAME's manual is on the screen from its top,
    by its band's colour: at full width, or as the whole page."""
    frame = detection_frame(rgb, wide)

    def band(box: tuple[int, int, int, int]) -> int | None:
        x0, y0, x1, y1 = box
        patch = frame[y0:y1, x0:x1]
        return next((n for n, colour in enumerate(card.MANUAL_BANDS)
                     if coloured(patch, colour) >= patch.shape[0] * patch.shape[1] * 0.8), None)

    full = band(MANUAL_BAND_BOX)
    if not whole:
        return full
    return band(MANUAL_WHOLE_BAND_BOX) if full is None else None


def manual_memory(log: Path) -> dict[str, int] | None:
    """What Indigo said of its memory with card.MANUAL_GAME's manual open (two
    pages of it held) and once it closed, or None when it never opened."""
    text = log.read_text(errors="replace") if log.exists() else ""
    opened = [m for m in MANUAL_MEMORY.finditer(text) if m.group(1) == card.MANUAL_GAME]
    closed = list(MANUAL_CLOSED.finditer(text))
    if not opened or not closed:
        return None
    pages, record, free, before = (int(g) for g in opened[0].groups()[1:])
    after, before_close = (int(g) for g in closed[0].groups())
    return {"pages": pages, "held": 2 * record, "free_open": free, "before": before,
            "after": after, "returned": after - before_close}


def arrow_up(gray: np.ndarray) -> bool:
    """The arrow above Memory Cards' left stack: rows lie above its window."""
    x0, y0, x1, y1 = UP_ARROW_BOX
    return int((gray[y0:y1, x0:x1] >= 200).sum()) >= ARROW_PIXELS


def text_mask(frame: np.ndarray, box: tuple[int, int, int, int] = LABEL_BOX,
              level: int = TEXT_LEVEL) -> np.ndarray:
    x0, y0, x1, y1 = box
    return frame[y0:y1, x0:x1] >= level


def stage_box(box: tuple[int, int, int, int], wide: bool = False) -> tuple[int, int, int, int]:
    """Authored coordinates in Dolphin's actual 640x480 capture.

    With Menu Widescreen, Dolphin auto-aspect letterboxes 16:9 at y60..420.
    Both axes scale 3/4 about the capture centre. Authored x0..640 occupies
    x80..560; the extended widescreen margins occupy the remaining columns.
    """
    x0, y0, x1, y1 = box
    return (round(320 + (x0 - 320) * .75), round(240 + (y0 - 240) * .75),
            round(320 + (x1 - 320) * .75), round(240 + (y1 - 240) * .75)) if wide else box


def detection_frame(rgb: np.ndarray, wide: bool = False) -> np.ndarray:
    """Restore authored coordinates solely for comparisons, never screenshots.

    Nearest-neighbor uses only captured pixel values. It removes Dolphin's
    wide letterbox and restores the central authored stage, so existing
    field bounds and minimum text coverage apply in either screen shape.
    The real native capture remains unchanged for pictures and review.
    """
    if not wide:
        return rgb
    return np.asarray(Image.fromarray(rgb[60:420, 80:560]).resize(
        (WIDTH, HEIGHT), Image.Resampling.NEAREST))


def raw_icon_frame(rgb: np.ndarray, wide: bool = False) -> int | None:
    """Which unique texture patch is on the selected RAW cube.

    A moving cube with a static texture cannot manufacture the other patch
    colour. Ignore colors elsewhere (banner, footer, backdrop and cubes).
    """
    x0, y0, x1, y1 = stage_box(RAW_ICON_BOX, wide)
    crop = rgb[y0:y1, x0:x1]
    counts = [coloured(crop, colour) for colour in make_test_saves.RAW_ICON_COLOURS]
    best = int(np.argmax(counts))
    return best if counts[best] >= 12 and counts[best] >= counts[1 - best] * 3 else None


def folder_cube_azure(rgb: np.ndarray, wide: bool = False) -> int:
    """Count authored Azure rim pixels only inside the selected folder cube."""
    x0, y0, x1, y1 = stage_box(FOLDER_CUBE_BOX, wide)
    return coloured(rgb[y0:y1, x0:x1], FOLDER_AZURE)


def backdrop(rgb: np.ndarray) -> np.ndarray:
    """The menu's backdrop colour, from the screen's bottom corners, where
    nothing else is drawn: settings/non-default.ini's Emerald, not Indigo's
    default purple."""
    return np.concatenate([rgb[-40:, :60].reshape(-1, 3), rgb[-40:, -60:].reshape(-1, 3)]).mean(axis=0)


def diagnose(rgb: np.ndarray) -> str:
    """What a failed step's screen shows, when it is not Indigo at all."""
    black = float((rgb.max(axis=2) < 8).mean())
    white = int((rgb.min(axis=2) > 200).sum())
    if black > 0.6 and white > 2000:
        return "Indigo crashed: the screen shows its exception handler (see the last picture)"
    if black > 0.95:
        return "the screen went black"
    return ""


def coloured(frame: np.ndarray, colour: tuple[int, int, int]) -> int:
    """How many pixels are within 55 of colour in every channel: card.py's
    folder pictures are pure colours nothing else on screen comes near."""
    return int((np.abs(frame.astype(np.int16) - np.array(colour, np.int16)) <= 55).all(axis=2).sum())


def overlap(a: np.ndarray, b: np.ndarray) -> float:
    union = np.logical_or(a, b).sum()
    return float(np.logical_and(a, b).sum() / union) if union else 1.0


def rows_moved(before: tuple[int, list[np.ndarray]], after: tuple[int, list[np.ndarray]]) -> int | None:
    """How many rows a Settings focus moved between two Route.settings_rows,
    down positive: how far its slot moved plus how far the list scrolled. The
    scroll is the shift that matches the rows' labels, leaving out the focused
    row in either picture (its label is drawn on the lit fill, so a little
    bolder). None when no shift matches at least two rows."""
    (was, old), (now, new) = before, after
    for shift in sorted(range(1 - len(old), len(old)), key=abs):
        pairs = [(new[n], old[n + shift]) for n in range(len(new))
                 if 0 <= n + shift < len(old) and now != n and was != n + shift]
        if len(pairs) >= 2 and all(same_text(a, b) for a, b in pairs):
            return now + shift - was
    return None


def same_text(a: np.ndarray, b: np.ndarray) -> bool:
    """The same words. A 480i picture can sit half a line higher after a slow
    frame (it is drawn for the other field), which moves an antialiased word's
    rows across TEXT_LEVEL, so the words are compared a pair of rows at a time."""
    def rows(mask: np.ndarray) -> np.ndarray:
        return mask[:mask.shape[0] // 2 * 2].reshape(-1, 2, mask.shape[1]).any(axis=1)
    return overlap(rows(a), rows(b)) >= SAME


def has_label(mask: np.ndarray) -> bool:
    """A word of text: enough lit pixels, spread across the band but not filling it."""
    lit = int(mask.sum())
    if not 60 <= lit <= mask.size // 3:
        return False
    columns = np.flatnonzero(mask.any(axis=0))
    return columns.size > 0 and 20 <= columns[-1] - columns[0] <= mask.shape[1] - 4


def has_folder_color_label(mask: np.ndarray) -> bool:
    """The folder page's small color value, within its dedicated text box.

    Native Azure has 50 bright pixels; the Library-title minimum is 60.
    Short names retain their bounded word span and exclude a filled panel.
    """
    lit = int(mask.sum())
    if not 30 <= lit <= mask.size // 3:
        return False
    columns = np.flatnonzero(mask.any(axis=0))
    rows = np.flatnonzero(mask.any(axis=1))
    if columns.size == 0 or rows.size == 0:
        return False
    word_area = (columns[-1] - columns[0] + 1) * (rows[-1] - rows[0] + 1)
    return (16 <= columns[-1] - columns[0] <= mask.shape[1] - 4 and
            rows[-1] - rows[0] >= 2 and lit * 10 < word_area * 9)


def has_save_number(mask: np.ndarray) -> bool:
    """A value-only metric may be a thin single digit, without its caption.

    Native captures at the unchanged bright-text threshold show 32 pixels
    for 1, 30 for wide 1 and 58 for wide 2. Require bounded ink, width and glyph height;
    a pixel or short bar cannot stand in for a number. Word checks retain
    their existing coverage and spread requirement.
    """
    lit = int(mask.sum())
    if not 24 <= lit <= mask.size // 3:
        return False
    columns = np.flatnonzero(mask.any(axis=0))
    rows = np.flatnonzero(mask.any(axis=1))
    return (columns.size > 0 and rows.size > 0 and
            3 <= columns[-1] - columns[0] <= mask.shape[1] - 4 and
            rows[-1] - rows[0] >= 8)


def save_details_panel(gray: np.ndarray) -> bool:
    """The presentation panel in authored coordinates, distinct from save cubes.

    Its four long border edges remain visible in both the normal and gray
    palettes. A cube may overlap the title band, but cannot supply this frame.
    """
    edges = (((70, 108, 78, 386), 1), ((564, 108, 572, 386), 1),
             ((92, 88, 548, 96), 0), ((92, 399, 548, 407), 0))
    for (x0, y0, x1, y1), axis in edges:
        if (gray[y0:y1, x0:x1] >= 64).any(axis=axis).mean() < .9:
            return False
    return has_save_details_eyebrow(text_mask(gray, SAVE_DETAILS_EYEBROW_BOX))


def has_save_details_eyebrow(mask: np.ndarray) -> bool:
    """The fixed small marker: Jet Black retains 49 bright pixels at 160.

    Its ink, word span and short glyph height are bounded separately from
    normal value fields. This marker is accepted only inside all four panel
    borders; a short save name never determines whether the dialog is open.
    """
    lit = int(mask.sum())
    if not 40 <= lit <= mask.size // 3:
        return False
    columns = np.flatnonzero(mask.any(axis=0))
    rows = np.flatnonzero(mask.any(axis=1))
    return (columns.size > 0 and rows.size > 0 and
            40 <= columns[-1] - columns[0] <= mask.shape[1] - 4 and
            4 <= rows[-1] - rows[0] <= 10)


def probe_field(rgb: np.ndarray) -> np.ndarray:
    """The probe's signature colour, an azure no screen of Indigo's uses."""
    r, g, b = (rgb[..., i].astype(np.int16) for i in range(3))
    return (b > 190) & (r < 80) & (g > 80) & (g < 180)


def probe_report(rgb: np.ndarray) -> dict[str, object] | None:
    """The probe's results, read from a picture of its screen, or None when
    the screen is not the probe's. The strip is found in the picture itself
    (the black and white rectangle inside the azure field), so the scale
    Dolphin or a capture card draws it at does not matter."""
    azure = probe_field(rgb)
    if azure.mean() < 0.1:
        return None
    rows = azure.mean(axis=1)
    field = np.flatnonzero(rows > 0.5)
    strip = [y for y in range(int(field[0]), rgb.shape[0]) if 0.05 < rows[y] < 0.45] if field.size else []
    if not strip:
        return None
    y0, y1 = strip[0], strip[-1] + 1
    edge = np.flatnonzero(azure[field[0]:y0].mean(axis=0) > 0.9)
    columns = azure[y0:y1].mean(axis=0)
    xs = [x for x in range(int(edge[0]), int(edge[-1]) + 1) if columns[x] < 0.1] if edge.size else []
    if not xs:
        return None
    x0, x1 = xs[0], xs[-1] + 1
    width, height = (x1 - x0) / 32, (y1 - y0) / len(PROBE_WORDS)
    words = []
    for row in range(len(PROBE_WORDS)):
        value = 0
        for bit in range(32):
            pixel = rgb[int(y0 + (row + 0.5) * height), int(x0 + (bit + 0.5) * width)]
            value = value << 1 | int(pixel.mean() > 128)
        words.append(value)
    report: dict[str, object] = dict(zip(PROBE_WORDS, words))
    packed = b"".join(w.to_bytes(4, "big") for w in words)
    report["valid"] = words[0] == PROBE_MAGIC and zlib.crc32(packed[:-4]) == words[-1]
    report["disc_id"] = printable(packed[8:14])
    report["path"] = printable(packed[64:80].rstrip(b"\0"))
    return report


def sram(video: int, sixty_hz: bool) -> bytes:
    """A GameCube's SRAM (as Dolphin keeps it in GC/SRAM.raw): Dolphin's own
    defaults, with the video format and PAL's 60 Hz flag set and the
    checksums over them made again."""
    data = bytearray(0x44)
    data[0x17] = 0x2C | video          # flags: stereo, set up; bits 0-1 the format
    data[0x15] = 0x40 if sixty_hz else 0  # ntd: bit 6, PAL at 60 Hz
    data[0x18:0x30] = b"DOLPHINSLOTADOLPHINSLOTB"
    data[0x3E:0x40] = b"\x6e\x6d"
    words = [int.from_bytes(data[i:i + 2], "big") for i in range(0x10, 0x18, 2)]
    data[0x04:0x06] = (sum(words) & 0xFFFF).to_bytes(2, "big")
    data[0x06:0x08] = (sum(~w & 0xFFFF for w in words) & 0xFFFF).to_bytes(2, "big")
    return bytes(data)


def video_mode(report: dict[str, object]) -> str:
    """The video mode the probe found the screen in, in words."""
    dcr, clock = int(report["vi_dcr"]), int(report["vi_clk_dtv"])
    scan = "progressive" if clock >> 16 & 1 else "double-strike" if dcr >> 2 & 1 else "interlaced"
    timing = ("NTSC", "PAL", "MPAL", "debug")[dcr >> 8 & 3]
    return f"{scan}, {timing} timing, {'a' if clock & 1 else 'no'} component cable"


def app_video_matches(mode: str, report: dict[str, object], region: str, cable: str) -> bool:
    """Check the VI registers at handoff, including a forced homebrew mode."""
    progressive = bool(int(report["vi_clk_dtv"]) >> 16 & 1)
    pal = int(report["vi_dcr"]) >> 8 & 3 == 1
    double_strike = not progressive and bool(int(report["vi_dcr"]) >> 2 & 1)
    expected_progressive = CABLES[cable] and (mode == "Auto" or mode in ("480p", "576p"))
    expected_pal = (region == "pal" and not CABLES[cable]) if mode == "Auto" else mode in ("576i", "576sf", "576p", "288p")
    return (progressive == expected_progressive and pal == expected_pal and
            double_strike == (mode in ("240p", "288p")))


def printable(data: bytes) -> str:
    """Bytes as text for a report: anything outside printable ASCII as a dot."""
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


class Deadline:
    """A wait of some seconds of the console's own time when Dolphin reports
    it (patch 0005, DOLPHIN_TICKS), else of the machine's.

    Dolphin reports ten times a second, so its last report can be most of a
    tenth of a second old when a wait begins. A fresh wait counts from the
    first report after it began instead: never less than its seconds."""

    def __init__(self, emulator: "Emulator", seconds: float, fresh: bool = False) -> None:
        self.emulator, self.seconds = emulator, seconds
        self.wall = time.monotonic()
        self.last = emulator.emulated()
        self.start = None if fresh else self.last

    def elapsed(self) -> float:
        now = self.emulator.emulated()
        if now is None:
            return time.monotonic() - self.wall
        if self.start is None and now != self.last:
            self.start = now  # a report after the wait began
        return 0.0 if self.start is None else now - self.start

    def expired(self) -> bool:
        wall = max(self.seconds * WALL_FACTOR, WALL_FLOOR)
        return self.elapsed() >= self.seconds or time.monotonic() - self.wall >= wall


class Emulator:
    def __init__(self, dol: Path, disc: Path | None, work: Path, out: Path, region: str = "pal",
                 storage: str = "dvd", card: Path | None = None, cable: str = "composite",
                 faults: str | None = None, cards: Path | None = None,
                 empty_slots: bool = False, folder_frames: bool = False,
                 second: Path | None = None, exact_textures: bool = False) -> None:
        self.out = out
        self.user = work / "dolphin"
        (self.user / "Config").mkdir(parents=True)
        config = dolphin_ini(storage, card, cards, empty_slots, second)
        if folder_frames:
            config += "[Movie]\nDumpFrames = True\nDumpFramesSilent = True\n"
        (self.user / "Config/Dolphin.ini").write_text(config)
        # Swiss's own debug output (its OSReport lines) goes to dolphin.log.
        (self.user / "Config/Logger.ini").write_text(
            "[Logs]\nOSREPORT = True\nPOWERPC = True\n[Options]\nVerbosity = 1\nWriteToConsole = True\nWriteToFile = False\n")
        (self.user / "GC").mkdir()
        (self.user / "GC/SRAM.raw").write_bytes(sram(*REGIONS[region][1:]))
        # Dolphin's texture cache sees a texture rewritten in place by 128
        # samples of it, evenly spaced: in a 640-texel-wide CMPR page each one
        # falls in its leftmost tile column, the page's paper margin, so a page
        # read into a slot over another looked unchanged and the old one
        # stayed. A console reads the new one (GX_InvalidateTexAll); Dolphin
        # does when it hashes all of every texture.
        (self.user / "Config/GFX.ini").write_text("[Settings]\nInternalResolution = 1\nShowFPS = False\n" +
            ("SafeTextureCacheColorSamples = 0\n" if exact_textures else "") +
            ("DumpFramesAsImages = True\nFrameDumpsResolutionType = 2\nPNGCompressionLevel = 1\n"
             if folder_frames else ""))
        self.folder_frames = (PresentedFrames(self.user / "Dump/Frames", out / "folder-transitions")
                              if folder_frames else None)
        (self.user / "Config/GCPadNew.ini").write_text(dsu_pad.GCPAD_INI)
        self.pad = dsu_pad.Pad(0)  # any free port; Dolphin is told which
        (self.user / "Config/DSUClient.ini").write_text(
            f"[Server]\nEnabled = True\nEntries = {dsu_pad.DESCRIPTION}:127.0.0.1:{self.pad.port};\n")
        # X keeps its sockets here; a container user cannot create it at a
        # system path otherwise.
        os.makedirs("/tmp/.X11-unix", mode=0o1777, exist_ok=True)
        read, write = os.pipe()
        self.xvfb = subprocess.Popen(
            ["Xvfb", "-displayfd", str(write), "-screen", "0", f"{WIDTH}x{HEIGHT}x24", "-nolisten", "tcp"],
            pass_fds=(write,), stdout=subprocess.DEVNULL, stderr=open(out / "xvfb.log", "w"))
        os.close(write)
        with os.fdopen(read) as display:
            number = display.readline().strip()
        if not number.isdigit():
            raise Broken("Xvfb did not start")
        self.display = f":{number}"
        env = dict(os.environ, DISPLAY=self.display, LIBGL_ALWAYS_SOFTWARE="1")
        env["DOLPHIN_TICKS"] = "1"  # the console's clock and PC in dolphin.log (patch 0005)
        env["DOLPHIN_DABR"] = "1"  # a thread's stack guarded, as on a console (patch 0007)
        env.pop("DOLPHIN_SD_FAULTS", None)
        env.pop("DOLPHIN_GCLOADER", None)
        if storage == "gcloader":
            env["DOLPHIN_GCLOADER"] = str(card)
        if faults:  # the SD card fails as asked (buildtools/ci/runner/dolphin/0004)
            env["DOLPHIN_SD_FAULTS"] = faults
        self.log = out / "dolphin.log"
        self.dolphin = subprocess.Popen(
            ["dolphin-emu-nogui", "-u", str(self.user), "-p", "x11", "-v", "OGL",
             *(["-C", f"Dolphin.Core.DefaultISO={disc}"] if disc else []),
             "-C", f"Dolphin.Core.FallbackRegion={REGIONS[region][0]}",
             "-C", f"SYSCONF.IPL.PGS={CABLES[cable]}", "-e", str(dol)],
            stdout=open(self.log, "w"), stderr=subprocess.STDOUT, env=env)
        self.started = time.monotonic()

    def frame(self) -> np.ndarray:
        if self.dolphin.poll() is not None:
            raise Failed(f"Dolphin exited ({self.dolphin.returncode}); see dolphin.log")
        raw = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-f", "x11grab", "-video_size", f"{WIDTH}x{HEIGHT}",
             "-draw_mouse", "0", "-i", f"{self.display}+0,0", "-frames:v", "1",
             "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
            capture_output=True, timeout=20)
        if raw.returncode or len(raw.stdout) != WIDTH * HEIGHT * 3:
            raise Broken(f"cannot read the screen: {raw.stderr.decode(errors='replace')[-200:]}")
        return np.frombuffer(raw.stdout, np.uint8).reshape(HEIGHT, WIDTH, 3)

    def burst(self, seconds: float, start) -> np.memmap:
        """The screen at 60 frames a second for a while, from one ffmpeg, so
        the frames come a vsync apart (frame() starts a process for each).
        start() runs once the first frame is in: a press seen from before it."""
        path = self.user.parent / "burst.rgb"
        grab = subprocess.Popen(
            ["ffmpeg", "-loglevel", "error", "-f", "x11grab", "-framerate", "60",
             "-video_size", f"{WIDTH}x{HEIGHT}", "-draw_mouse", "0", "-i", f"{self.display}+0,0",
             "-t", f"{seconds}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-y", str(path)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        size, until = WIDTH * HEIGHT * 3, time.monotonic() + 20
        while (not path.exists() or path.stat().st_size < size) and grab.poll() is None \
                and time.monotonic() < until:
            time.sleep(0.01)
        start()
        _, error = grab.communicate(timeout=seconds + 30)
        frames = path.stat().st_size // size if path.exists() else 0
        if grab.returncode or frames == 0:
            raise Broken(f"cannot read the screen: {error.decode(errors='replace')[-200:]}")
        return np.memmap(path, np.uint8, "r", shape=(frames, HEIGHT, WIDTH, 3))

    def _ticks(self) -> tuple[float, str] | None:
        """The last TICKS line in Dolphin's output: the console's seconds, and its PC and LR."""
        try:
            with open(self.log, "rb") as log:
                log.seek(0, os.SEEK_END)
                log.seek(max(0, log.tell() - 4096))
                tail = log.read().decode(errors="replace")
        except OSError:
            return None
        found = re.findall(r"^TICKS (\d+) (PC \S+ LR \S+)", tail, re.M)
        return (int(found[-1][0]) / TICKS_PER_SECOND, found[-1][1]) if found else None

    def emulated(self) -> float | None:
        """The console's own seconds since it started, or None from a Dolphin without patch 0005."""
        ticks = self._ticks()
        return ticks[0] if ticks else None

    def where(self) -> str | None:
        """Where the console's CPU was last seen, and when, for a check that failed."""
        ticks = self._ticks()
        return f"{ticks[1]} at {ticks[0]:.1f} s" if ticks else None

    def close(self) -> None:
        for process in (self.dolphin, self.xvfb):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()
        self.pad.close()
        if self.folder_frames:
            self.folder_frames.finish()


class Route:
    """Steps through the menus and records every checkpoint."""

    def __init__(self, emulator: Emulator, out: Path, probe: bool = False, fresh_card: bool = False,
                 cable: str = "composite", region: str = "pal", fragments: int = 0,
                 cards: Path | None = None, storage: str = "dvd", menu_wide: bool = False,
                 sd_image: Path | None = None, detail_saves: bool = True,
                 second_sd: Path | None = None, home_controls: bool = True,
                 apps_video: str = "Auto") -> None:
        self.emulator = emulator
        self.detail_saves = detail_saves  # Saves on Details is on (the default)
        self.home_controls = home_controls  # On-screen Controls is on (the default)
        self.apps_video = apps_video
        self.cards = cards  # the memory cards' GCI folders, cards/A and cards/B
        self.storage = storage
        self.menu_wide = menu_wide
        self.sd_image = sd_image
        self.second_sd = second_sd  # the files route's SD2SP2 card
        self.fragments = fragments  # the pieces the probe's game is in on the card
        self.cable = cable
        self.region = region
        self.out = out
        self.probe = probe
        self.fresh_card = fresh_card
        self.report: dict[str, object] | None = None
        self.folders_on = False  # the route turned Library Folders on and saved it
        self.pressed_again: list[str] = []  # presses the menu missed, pressed again
        self.checks: list[dict[str, object]] = []
        self.shots: list[tuple[str, Path]] = []
        self.pad = emulator.pad

    # -- evidence
    def shot(self, name: str, frame: np.ndarray) -> np.ndarray:
        path = self.out / f"{len(self.shots) + 1:02d}-{name}.png"
        Image.fromarray(frame).save(path)
        self.shots.append((name, path))
        return frame

    def check(self, name: str, passed: bool, **detail: object) -> None:
        probe_up = bool(self.report and self.report["valid"])  # the probe's screen is no crash
        if not passed and getattr(self, "last_rgb", None) is not None:
            self.shot("failed-check", self.last_rgb)
            detail["picture"] = self.shots[-1][1].name
        if not passed and getattr(self, "last_rgb", None) is not None and not probe_up:
            if why := diagnose(self.last_rgb):
                detail["screen"] = why
        if not passed and (where := self.emulator.where()):
            detail["console"] = where
        self.checks.append({"check": name, "passed": bool(passed), **detail})
        print(f"{'PASS' if passed else 'FAIL'} {name} {json.dumps(detail) if detail else ''}", flush=True)
        if not passed:
            raise Failed(f"{name}: {detail['screen']}" if "screen" in detail else name)

    # -- waiting for the screen
    def gray(self) -> np.ndarray:
        rgb = self.emulator.frame()
        self.last_rgb = rgb
        return detection_frame(rgb, self.menu_wide).max(axis=2)

    def settled_label(self, seconds: float = SETTLE_SECONDS, unlike: np.ndarray | None = None,
                      like: np.ndarray | None = None,
                      box: tuple[int, int, int, int] = LABEL_BOX,
                      level: int = TEXT_LEVEL,
                      numeric: bool = False) -> tuple[np.ndarray | None, float]:
        """Wait for steady text in a box (the face's name by default), optionally unlike or like a given one."""
        deadline = Deadline(self.emulator, seconds)
        previous, steady = None, 0
        while not deadline.expired():
            mask = text_mask(self.gray(), box, level)
            ok = has_save_number(mask) if numeric else has_label(mask)
            if ok and unlike is not None:
                ok = overlap(mask, unlike) < DIFFERENT
            if ok and like is not None:
                ok = same_text(mask, like)
            steady = steady + 1 if ok and previous is not None and overlap(mask, previous) >= 0.95 else 0
            previous = mask if ok else None
            if steady >= 2:
                return mask, time.monotonic()
            time.sleep(0.15)
        return None, time.monotonic()

    def press_until(self, button: str, seconds: float = SETTLE_SECONDS, unlike: np.ndarray | None = None,
                    like: np.ndarray | None = None,
                    box: tuple[int, int, int, int] = LABEL_BOX) -> tuple[np.ndarray | None, float]:
        """Press a button, then wait for steady text in a box as settled_label does.
        A press the menu was too busy to see leaves the box as it was, and a person
        presses again: so does this, noting each in the report. A press that
        changed the box, to anything, is never repeated."""
        before = text_mask(self.gray(), box)
        for attempt in range(1, PRESSES + 1):
            self.press(button)
            last = attempt == PRESSES
            mask, now = self.settled_label(seconds if last else MISSED_SECONDS, unlike, like, box)
            if mask is not None or last:
                return mask, now
            if overlap(text_mask(self.gray(), box), before) < SAME:
                return self.settled_label(seconds, unlike, like, box)  # it moved: let it arrive
            self.pressed_again.append(button)
        return None, time.monotonic()

    def press(self, button: str, seconds: float = PRESS_SECONDS) -> None:
        """Hold a button for some of the console's time, then leave it up for
        a little: a busy machine must not shorten a press below a frame. Both
        are fresh waits: from a stale report, a press begun just before the
        next one could end within a frame and never reach the console."""
        self.pad.hold(button)
        self.pause(seconds, fresh=True)
        self.pad.hold()
        self.pause(PRESS_SECONDS, fresh=True)

    def pause(self, seconds: float, fresh: bool = False) -> None:
        """Let some of the console's time pass: a menu's animation takes as
        much of it however busy the machine is."""
        wait = Deadline(self.emulator, seconds, fresh=fresh)
        while not wait.expired():
            time.sleep(0.02)

    # -- the route
    def boot(self) -> np.ndarray:
        if self.fresh_card:
            return self.first_run()
        mask, now = self.settled_label(BOOT_SECONDS)
        boot = round(now - self.emulator.started, 1)
        self.shot("home", self.last_rgb)
        self.check("Home appears after boot", mask is not None, seconds=boot)
        self.plug_in()
        return mask

    def first_run(self) -> np.ndarray:
        """A new card has no swiss/settings/global.ini: Indigo starts in
        Settings, on Storage, and says so. DOWN past the page's rows reaches
        Save & Exit, and A writes the settings to the card and goes Home.

        DOWN is pressed while the rows' counter shows, so one the menu was too
        busy to see (a new card's first moments) is pressed again; then LEFT
        twice, which ends on Save & Exit even from Discard & Exit, one row on."""
        title, now = self.settled_label(BOOT_SECONDS, box=SETTINGS_TITLE_BOX)
        self.shot("first-run", self.last_rgb)
        self.check("a new card opens Settings first", title is not None,
                   seconds=round(now - self.emulator.started, 1))
        self.plug_in()
        for _ in range(STORAGE_ROWS + 4):
            if text_mask(self.gray(), COUNTER_BOX).sum() < COUNTER_PIXELS:
                break
            self.press("DOWN")
            self.pause(0.4)
        self.check("DOWN takes the focus past the page's rows",
                   text_mask(self.gray(), COUNTER_BOX).sum() < COUNTER_PIXELS)
        for _ in range(2):
            self.press("LEFT")
            self.pause(0.3)
        self.shot("first-run-save", self.emulator.frame())
        self.press("A")
        mask, now = self.settled_label(BOOT_SECONDS)
        self.shot("home", self.last_rgb)
        self.check("Save & Exit writes the settings and goes Home", mask is not None,
                   seconds=round(now - self.emulator.started, 1))
        return mask

    def plug_in(self) -> None:
        self.pad.plug_in()
        deadline = time.monotonic() + 30  # Dolphin's own polling, in the machine's time
        while not self.pad.streaming and time.monotonic() < deadline:
            time.sleep(0.1)
        if not self.pad.streaming:
            raise Broken("Dolphin never asked for the controller")
        time.sleep(0.5)

    def card_checks(self, image: Path, route: str, start: dict[str, str] | None = None) -> None:
        """Read settings back from the SD image. A seeded card must retain
        its configured values, including on routes that do not save settings."""
        def text(path: str) -> str:
            return (card.read_card(image, path) or b"").decode("latin-1")

        settings = text("swiss/settings/global.ini")
        settings_check = ("the configured settings remain on the card" if start else
                          "the settings Indigo saved are on the card")
        self.check(settings_check,
                   "Swiss Video Mode=" in settings and "Down Face=" in settings, bytes=len(settings))
        if start:
            kept = seeded(settings)
            lost = {key: value for key, value in start.items() if kept.get(key) != value}
            self.check("the settings the card started with are all still there", not lost, lost=lost)
        if self.folders_on:
            # Library Folders, turned on by the route (library_folders): saved
            # on, and the card keeps its own FlattenDir, not the pattern Library
            # Folders uses while it is on, for when it goes off again.
            self.check("Library Folders is saved on, and the card keeps its own FlattenDir",
                       "\nLibrary Folders=Yes\r" in settings and "\nFlattenDir=*/games\r" in settings,
                       lines=[line.strip() for line in settings.splitlines()
                              if line.startswith(("Library Folders=", "FlattenDir="))])
        if route != "game" or not self.report:  # no game started
            return
        recent = text("swiss/settings/recent.ini").split("Recent_1=")[0]
        self.check("the launched game is first in the recent list",
                   card.game_file(*card.PROBE_GAME) in recent, recent=recent.strip().splitlines()[-1:])
        played = text("swiss/settings/play-history-0.ini") + text("swiss/settings/play-history-1.ini")
        self.check("Indigo's play history records the game", f"\n{card.PROBE_GAME[0]}=" in played)

    def turn(self, faces: list[np.ndarray], button: str, name: str) -> np.ndarray:
        mask, _ = self.press_until(button, unlike=faces[-1])
        self.shot(name, self.last_rgb)
        self.check(f"{button} turns the cube to another face", mask is not None, after=name)
        return mask

    def smoke(self) -> None:
        home = self.boot()
        faces = [home]
        # Setup > Console > On-screen Controls: the button hints under the
        # face's name, unless the card's settings turn them off.
        if self.home_controls:
            hints, _ = self.settled_label(box=HOME_HINT_BOX, level=HINT_LEVEL)
            self.check("Home shows its button hints", hints is not None)
        else:
            self.check("On-screen Controls Off: Home shows no button hints",
                       self.quiet(HOME_HINT_BOX, HINT_GONE_LEVEL))
        # The disc has apps: Library, Source, Settings, System and Apps.
        for n in range(1, 5):
            faces.append(self.turn(faces, "RIGHT", f"right-{n}"))
        back = self.turn(faces, "RIGHT", "right-5")
        self.check("five turns come back to the first face", same_text(back, home),
                   overlap=round(overlap(back, home), 3))
        distinct = max(overlap(a, b) for i, a in enumerate(faces) for b in faces[i + 1:])
        self.check("the five faces have five different names", distinct < DIFFERENT,
                   largest_overlap=round(distinct, 3))
        left = self.turn([back], "LEFT", "left-1")
        self.check("LEFT turns the other way", same_text(left, faces[-1]),
                   overlap=round(overlap(left, faces[-1]), 3))
        mask, _ = self.press_until("RIGHT", like=home)
        self.check("RIGHT undoes LEFT", mask is not None)
        for n, face in enumerate(faces[:4]):
            # Home starts on the Library face: browse it while it is open,
            # change the source on the Source face, and take Apps off Down
            # Face and put it back in Settings, then walk a Classic cube.
            if n == 2:
                self.apps_face_off_and_on(faces)
                self.classic_cube(faces)
                self.face_labels_off_and_on(faces)
                self.library_folders(faces)
            else:
                inside = {0: self.browse_library, 1: self.change_source,
                          3: self.system_rows}.get(n)
                self.open_and_close(face, n, inside)
            mask, _ = self.press_until("RIGHT", like=faces[n + 1])
            self.check("the cube turns on to the next face", mask is not None, face=n + 1)
        self.library_after_file_browser(faces)
        # Apps last: a launch never comes back.
        self.start_an_app(faces[4])

    def start_an_app(self, face: np.ndarray) -> None:
        """A on Apps shows the disc's apps as the Library shows games: RIGHT
        and LEFT move between them and back. A starts one: the Apps screen
        gives way to the launch screen, dimmed but for the app's card, its
        name and the ring. With the probe on the disc, the app started is the
        probe, and the hand-off must reach it (handoff); without it the
        route stops at the launch screen.

        The A that opens Apps is held longer than Apps takes to read /apps:
        on a console an SD card is read before a thumb lets go, and that A
        once started the first app with no chance to choose."""
        self.press("A", 2.5)
        opened = self.covered(face)
        self.shot("apps", self.last_rgb)
        self.check("A opens Apps", opened)
        first, _ = self.settled_label(box=TITLE_BOX)
        self.check("Apps shows an app's name", first is not None)
        other, _ = self.press_until("RIGHT", unlike=first, box=TITLE_BOX)
        self.shot("apps-right", self.last_rgb)
        self.check("RIGHT moves to the next app", other is not None)
        again, _ = self.press_until("LEFT", like=first, box=TITLE_BOX)
        self.check("LEFT goes back an app", again is not None)
        if self.probe:
            name = again
            for n in range(card.app_order(True).index(card.PROBE_APP[:-4])):
                name, _ = self.press_until("RIGHT", unlike=name, box=TITLE_BOX)
                self.check("RIGHT moves to the next app", name is not None, step=n + 1)
            self.shot("apps-probe", self.last_rgb)
        apps = float(self.emulator.frame().mean())
        self.press("A")
        # With the probe the launch must go on, and reading a program off an
        # emulated SD card can crawl on a busy machine: allow it a boot's
        # time. The probe's own checks follow.
        window = BOOT_SECONDS if self.probe else SETTLE_SECONDS
        launched, deadline = False, Deadline(self.emulator, window)
        while not launched and not deadline.expired():
            time.sleep(0.3)
            rgb = self.emulator.frame()
            self.last_rgb = rgb
            # The launch screen dims all but the app's card: the probe's bright
            # name card leaves 70 to 85% of the light, a dark card under 60%.
            # With the probe the launch goes on, so the hand-off's black frame
            # counts too (the probe then proves the launch); without it black
            # means a crash.
            mean = float(rgb.mean())
            launched = mean < (0.9 if self.probe else 0.6) * apps and (self.probe or mean > 0.02)
        self.shot("app-launch", self.last_rgb)
        self.check("A starts the app: the launch screen dims the Apps screen", launched,
                   apps=round(apps, 1), now=round(float(self.last_rgb.mean()), 1))
        if self.probe:
            report = self.handoff("app")
            self.check("the app starts with its own path", str(report["path"]).lower().endswith("probe.dol"),
                       path=report["path"], argc=report["argc"])
            self.check("the app inherits the selected video mode",
                       app_video_matches(self.apps_video, report, self.region, self.cable),
                       selected=self.apps_video, region=self.region, cable=self.cable, video=video_mode(report))

    def handoff(self, tag: str) -> dict[str, object]:
        """After a launch, wait for the probe's screen and check what the
        hand-off left it. The controller stays connected, as on a console."""
        report, deadline = None, Deadline(self.emulator, BOOT_SECONDS)
        while not (report and report["valid"]) and not deadline.expired():
            time.sleep(0.5)
            rgb = self.emulator.frame()
            self.last_rgb = rgb
            report = probe_report(rgb)
        self.shot(f"{tag}-probe", self.last_rgb)
        self.report = report
        self.check("the launch reaches the probe, and its report reads back", bool(report and report["valid"]),
                   seconds_waited=round(deadline.elapsed(), 1))
        self.check("the menu music is stopped before the hand-off",
                   not int(report["ai_dma"]) & AI_DMA_ENABLE, audio_dma=f"{report['ai_dma']:04X}")
        self.check("nothing writes to memory after the hand-off", report["stray_writes"] == 0,
                   words_changed=report["stray_writes"])
        return report

    def save(self) -> None:
        """Change one setting and save it: Settings > Setup > Console > Down
        Face None, then Save & Exit. Run on a card that fails (--sd-faults), the
        next boot of the same card (main) shows whether the settings survived."""
        home = self.boot()
        self.home_label, self.home_backdrop = home, backdrop(self.last_rgb)
        face = home
        for n in (1, 2):  # Library, Source, Settings
            face = self.turn([face], "RIGHT", f"right-{n}")
        self.flip_apps_face(face, "off")

    def boot_again(self) -> None:
        """The same card's next boot: its settings must still load, so Indigo
        starts at Home, not in Settings as on a card without them."""
        # Home is the face name the first boot showed; Settings' rows can put
        # text in that box too, but not that word.
        home, _ = self.settled_label(BOOT_SECONDS, like=self.home_label)
        self.shot("next-boot", self.last_rgb)
        self.check("the next boot still has the card's settings: Indigo starts at Home", home is not None)
        # A settings file that reads back broken boots Home too, on the defaults.
        drift = float(np.linalg.norm(backdrop(self.last_rgb) - self.home_backdrop))
        self.check("... in the colours the card's settings chose, not the defaults", drift < 15,
                   drift=round(drift, 1))

    def game(self) -> None:
        """Boot, open the Library, move to the probe's game, open its details
        and launch it. The probe must see the game's own disc ID and the
        24 MB a game is promised. On the way, the image whose file table runs
        away is launched too: it fails like any other launch here and comes
        back to the Library (starting it used to crash Indigo)."""
        home = self.boot()
        self.press("A")
        opened = self.covered(home)
        self.shot("library", self.last_rgb)
        self.check("A opens the Library", opened)
        title, _ = self.settled_label(box=TITLE_BOX)
        self.check("the Library shows a game's title", title is not None)
        order = card.library_order(True)
        runaway = next(name for _, name, image in card.DAMAGED if image is card.runaway_table)
        for n in range(order.index(card.PROBE_GAME[1])):
            title, _ = self.press_until("RIGHT", unlike=title, box=TITLE_BOX)
            self.check("RIGHT moves to the next game", title is not None, step=n + 1)
            if order[n + 1] == runaway:
                self.launch_fails_cleanly(title)
        self.shot("library-probe", self.last_rgb)
        self.press("A")
        self.check("A opens the game's details", self.covered(title, TITLE_BOX))
        self.shot("probe-details", self.last_rgb)
        self.press("A")
        if self.fragments > card.MAX_FRAGMENTS:
            self.check("a game in more pieces than can be served is refused, and the Library comes back",
                       self.back_to_library(title, "launch-refused"), pieces=self.fragments)
            return
        report = self.handoff("game")
        self.check("the game starts with its own disc ID", report["disc_id"] == card.PROBE_GAME[0],
                   disc_id=report["disc_id"])
        self.check("the game is given the full 24 MB", report["memsize"] == 0x01800000,
                   memory=f"{report['memsize']:08X}")

    def flip_apps_face(self, settings: np.ndarray, tag: str) -> None:
        """Down Face, twelve DOWNs into Console (flip_console): five RIGHTs
        from Apps pass Memory Cards, Emulators, File Browser and Web to None
        ("off"), two LEFTs from None reach File Browser ("files"), and three
        LEFTs more come back to Apps ("on")."""
        # The press counts follow the Down Face option order: None, Source,
        # Settings, System, Apps, Memory Cards, Emulators, File Browser, Web.
        # A face added to that list must update them.
        change, presses = {"off": ("RIGHT", 5), "files": ("LEFT", 2), "on": ("LEFT", 3)}[tag]
        self.flip_console(settings, 12, "apps-face", tag, change=change, presses=presses)

    def flip_console(self, settings: np.ndarray, downs: int, name: str, tag: str,
                     change: str = "RIGHT", presses: int = 1, named: bool = True) -> None:
        """From the Settings face: R and R to Setup, DOWN and A into Console,
        downs DOWNs to a row and change (a direction) to change it. B goes back to Setup and
        B again saves and exits (the demo disc can't keep the file; the
        setting holds until Indigo restarts), back to the Settings face, or,
        not named (Face Labels off), to a Home without the face's name."""
        detail = {name.replace("-", "_"): tag}
        self.press("A")
        opened = self.covered(settings)
        self.shot(f"settings-{name}-{tag}", self.last_rgb)
        self.check("A opens Settings", opened, **detail)
        for button in ("R", "R"):
            self.press(button)
            self.pause(1.0)
        self.walk_rows(1, "Setup's DOWN reaches Console", **detail)
        self.press("A")
        self.pause(1.5)
        self.walk_rows(downs, f"{downs} DOWNs reach the row", **detail)
        for _ in range(presses):
            self.press(change)
            self.pause(1.0)
        self.shot(f"{name}-{tag}", self.emulator.frame())
        self.press("B")
        self.pause(1.0)
        self.press("B")
        if not named:
            # No name is no proof of Home: Setup's page has none there either,
            # but it has the rows' counter, which Home never draws.
            gone = self.quiet() and text_mask(self.gray(), COUNTER_BOX).sum() < COUNTER_PIXELS
            self.shot(f"{name}-{tag}-home", self.last_rgb)
            self.check("Save & Exit comes back to Home, with no name under the cube", gone, **detail)
            return
        mask, _ = self.settled_label(like=settings)
        self.shot(f"{name}-{tag}-home", self.last_rgb)
        self.check("Save & Exit comes back to the Settings face", mask is not None, **detail)

    def settings_rows(self) -> tuple[int, list[np.ndarray]]:
        """Which of Settings' visible rows has the focus (the brightest fill),
        and every visible row's label."""
        gray = self.gray()
        fill = [gray[top + SETTINGS_ROW_FILL[1]:top + SETTINGS_ROW_FILL[3],
                     SETTINGS_ROW_FILL[0]:SETTINGS_ROW_FILL[2]].mean() for top in SETTINGS_ROW_TOPS]
        x0, y0, x1, y1 = SETTINGS_ROW_LABEL
        return int(np.argmax(fill)), [text_mask(gray, (x0, top + y0, x1, top + y1)) for top in SETTINGS_ROW_TOPS]

    def walk_rows(self, rows: int, name: str, **detail: object) -> None:
        """DOWN rows times in a Settings page, each step read back (rows_moved),
        so a press held into Settings' repeat (two rows) is taken back with UP
        and one the page missed is pressed again."""
        moved, steps = 0, []
        for _ in range(rows + PRESSES):
            if moved == rows:
                break
            before = self.settings_rows()
            button = "DOWN" if moved < rows else "UP"
            self.press(button)
            self.pause(0.4)
            step = rows_moved(before, self.settings_rows())
            if step is None:
                break
            steps.append(step)
            moved += step
        corrected = [step for step in steps if abs(step) != 1]
        self.check(name, moved == rows, moved=moved, **({"odd_steps": corrected} if corrected else {}), **detail)

    def classic_cube(self, faces: list[np.ndarray]) -> None:
        """From the Settings face: Setup > Console > Cube (thirteen DOWNs, just
        after the four sides) to Classic. The faces then sit as on the GameCube's
        menu, Library in front and the way between them: Settings is its
        left side, so LEFT goes nowhere, and RIGHT twice is Library and then
        System, not round. From System UP goes nowhere and B is Library; UP
        is Source, DOWN back, DOWN Apps (the disc has the probe app) and UP
        back. LEFT returns to Settings, where Cube goes back to Infinite.
        A press that should go nowhere has had a second of the console's
        time to turn the cube when its face is checked."""
        library, source, settings, system, apps = faces[:5]
        self.flip_console(settings, 13, "cube", "classic")
        walk = (("LEFT", "Settings", False), ("LEFT", "Settings", False), ("RIGHT", "Library", True),
                ("RIGHT", "System", True), ("UP", "System", False), ("B", "Library", True),
                ("UP", "Source", True), ("DOWN", "Library", True), ("DOWN", "Apps", True),
                ("UP", "Library", True), ("LEFT", "Settings", True))
        named = {"Library": library, "Source": source, "Settings": settings, "System": system,
                 "Apps": apps}
        for n, (button, name, turns) in enumerate(walk, 1):
            if turns:
                mask, _ = self.press_until(button, like=named[name])
            else:
                self.press(button)
                self.pause(1.0)
                mask, _ = self.settled_label(like=named[name])
            self.shot(f"classic-{n}-{button.lower()}", self.last_rgb)
            self.check(f"Cube Classic: {button} {'turns to' if turns else 'stays on'} {name}",
                       mask is not None, step=n)
        self.flip_console(settings, 13, "cube", "infinite")

    def face_labels_off_and_on(self, faces: list[np.ndarray]) -> None:
        """From the Settings face: Setup > Console > Face Labels (fifteen
        DOWNs, after Cube and Idle Animation) Off takes the face's name from
        under the cube; On puts the Settings face's name back."""
        settings = faces[2]
        self.flip_console(settings, 15, "face-labels", "off", named=False)
        self.flip_console(settings, 15, "face-labels", "on")

    def apps_face_off_and_on(self, faces: list[np.ndarray]) -> None:
        """Setup > Console > Down Face: None takes Apps off the cube, so the
        face after System is Library; File Browser puts its own face there
        (files_face); Apps puts Apps back after System."""
        library, settings, system, apps = faces[0], faces[2], faces[3], faces[4]
        for tag, after_system in (("off", library), ("files", None), ("on", apps)):
            self.flip_apps_face(settings, tag)
            self.check("RIGHT turns to System", self.press_until("RIGHT", like=system)[0] is not None,
                       apps_face=tag)
            if after_system is None:
                self.files_face(faces)
                for back in (system, settings):
                    self.check("LEFT turns back a face",
                               self.press_until("LEFT", like=back)[0] is not None, apps_face=tag)
                continue
            mask, _ = self.press_until("RIGHT", unlike=system)
            self.shot(f"after-system-apps-face-{tag}", self.last_rgb)
            self.check(f"Down Face {'None' if tag == 'off' else 'Apps'}: after System comes "
                       f"{'Library' if tag == 'off' else 'Apps'}",
                       mask is not None and same_text(mask, after_system),
                       overlap=round(overlap(mask, after_system), 3) if mask is not None else None)
            for back in (system, settings):
                self.check("LEFT turns back a face", self.press_until("LEFT", like=back)[0] is not None,
                           apps_face=tag)

    def pictures(self, seconds: float, until_shown: bool = False) -> tuple[int, int]:
        """Watches the screen for up to seconds of the console's time, or
        until the shown folder picture is up: its pixels in the last frame,
        and the most of a refused picture's in any."""
        deadline = Deadline(self.emulator, seconds)
        shown = refused = 0
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            shown = coloured(self.last_rgb, card.SHOWN_PICTURE)
            refused = max(refused, coloured(self.last_rgb, card.REFUSED_PICTURE))
            if until_shown and shown >= PICTURE_PIXELS:
                break
            time.sleep(0.25)
        return shown, refused

    def library_folders(self, faces: list[np.ndarray]) -> None:
        """From the Settings face: R and R to Setup, four DOWNs and A into
        Library, DOWN to Library Folders and RIGHT to turn it on; B and B save
        and exit. Then on Library, A opens the empty Nintendo.GC folder, which
        stays in the Library with only its way back, and B returns to it;
        RIGHT from there to Racing.v1, A into it and A into Classics.Set, which holds three cards (a game,
        the game in Old below it, and the way back): RIGHT twice is not back at
        the first, a third RIGHT is. B goes back to the same folder card each
        level up, and from /games to Home. Back on the Settings face after.
        Racing.v1's picture is its poster; Nintendo.GC's (too big) and Classics.Set's
        (damaged) never show, while their cards are in front."""
        library, source, settings = faces[0], faces[1], faces[2]
        self.press("A")
        self.check("A opens Settings", self.covered(settings), library_folders="on")
        for button, pause in (("R", 1.0), ("R", 1.0), ("DOWN", 0.4), ("DOWN", 0.4),
                              ("DOWN", 0.4), ("DOWN", 0.6), ("A", 1.5), ("DOWN", 0.6),
                              ("RIGHT", 1.0)):
            self.press(button)
            self.pause(pause)
        self.shot("library-folders-on", self.emulator.frame())
        self.press("B")
        self.pause(1.0)
        self.press("B")
        mask, _ = self.settled_label(like=settings)
        self.check("Save & Exit comes back to the Settings face", mask is not None,
                   library_folders="on")
        self.folders_on = True
        for back in (source, library):
            self.check("LEFT turns back a face", self.press_until("LEFT", like=back)[0] is not None,
                       library_folders="on")
        self.press("A")
        stray, _ = self.settled_label(box=TITLE_BOX)
        self.shot("folders-games", self.last_rgb)
        self.check("the Library shows a folder's name", stray is not None)
        _, refused = self.pictures(8.0)
        self.check("a folder's picture too big to read never shows", refused < PICTURE_PIXELS,
                   pixels=refused)
        # Nintendo.GC is empty: it opens in the Library with only the way back,
        # and B comes back to its card (Swiss's list would go Home). The
        # presses that open or leave a folder are held for half a second of
        # the console's time: the Library is still reading covers then, and a
        # tap can fall between two of its pad reads. Indigo waits for the
        # release after a folder changes, so a held press acts once.
        self.press("A", FOLDER_PRESS_SECONDS)
        empty, _ = self.settled_label(unlike=stray, box=TITLE_BOX)
        self.shot("folders-empty", self.last_rgb)
        self.check("an empty folder opens with only the way back", empty is not None)
        self.press("B", FOLDER_PRESS_SECONDS)
        self.check("B comes back to the empty folder's card",
                   self.settled_label(like=stray, box=TITLE_BOX)[0] is not None)
        racing, _ = self.press_until("RIGHT", unlike=stray, box=TITLE_BOX)
        self.check("RIGHT moves to the next folder", racing is not None)
        shown, refused = self.pictures(30.0, until_shown=True)
        self.shot("folders-picture", self.last_rgb)
        self.check("a folder's picture is its poster", shown >= PICTURE_PIXELS, pixels=shown)
        self.check("no refused picture shows beside it", refused < PICTURE_PIXELS, pixels=refused)
        self.press("A", FOLDER_PRESS_SECONDS)
        classics, _ = self.settled_label(unlike=racing, box=TITLE_BOX)
        self.shot("folders-racing", self.last_rgb)
        self.check("A opens a folder", classics is not None)
        _, refused = self.pictures(8.0)
        self.check("a damaged folder picture never shows", refused < PICTURE_PIXELS, pixels=refused)
        self.press("A", FOLDER_PRESS_SECONDS)
        first, _ = self.settled_label(unlike=classics, box=TITLE_BOX)
        self.shot("folders-classics", self.last_rgb)
        self.check("A opens a folder in it", first is not None)
        shown = first
        for n in (1, 2):
            shown, _ = self.press_until("RIGHT", unlike=shown, box=TITLE_BOX)
            self.shot(f"folders-classics-right-{n}", self.last_rgb)
            self.check("RIGHT moves to the next card", shown is not None, step=n)
        self.check("the folder lists the game a level down: RIGHT twice is not back at the first",
                   overlap(shown, first) < DIFFERENT, overlap=round(overlap(shown, first), 3))
        self.check("a third RIGHT is back at the first card",
                   self.press_until("RIGHT", like=first, box=TITLE_BOX)[0] is not None)
        for name, expected in (("Classics", classics), ("Racing", racing)):
            self.press("B", FOLDER_PRESS_SECONDS)
            back, _ = self.settled_label(like=expected, box=TITLE_BOX)
            self.shot(f"folders-back-to-{name.lower()}", self.last_rgb)
            self.check("B goes up a folder, to the same card", back is not None, card=name)
        self.press("B", FOLDER_PRESS_SECONDS)
        mask, _ = self.settled_label(like=library)
        self.shot("folders-home", self.last_rgb)
        self.check("B from /games goes Home", mask is not None, library_folders="on")
        for ahead in (source, settings):
            self.check("RIGHT turns on a face", self.press_until("RIGHT", like=ahead)[0] is not None,
                       library_folders="on")

    def folder_step(self, button: str, tag: str, box=TITLE_BOX,
                    like: np.ndarray | None = None,
                    unlike: np.ndarray | None = None) -> np.ndarray | None:
        """One folder action, including every native frame until it settles."""
        dump = self.emulator.folder_frames
        if dump is None:
            raise Broken("the folders route needs native presented-frame dumps")
        span = dump.begin(tag)
        self.press(button, FOLDER_PRESS_SECONDS)
        label, _ = self.settled_label(like=like, unlike=unlike, box=box)
        dump.end(span)
        proof = dump.evidence(span)
        self.folder_transitions.append(proof)
        (self.out / "folder-transitions.json").write_text(
            json.dumps(self.folder_transitions, indent=2) + "\n")
        self.shot(tag, self.last_rgb)
        self.check("every presented folder transition frame stays out of the legacy browser",
                   not proof["bad_frames"], **proof)
        return label

    def folder_color(self, like: np.ndarray | None = None,
                     unlike: np.ndarray | None = None) -> np.ndarray | None:
        """Steady color words, with the folder value's own small-text bounds.
        A changed value must differ from the preceding words; it cannot pass
        on a stale frame. Other screens retain their existing title bounds.
        """
        return self.text_until(FOLDER_COLOR_BOX,
                               lambda mask: has_folder_color_label(mask)
                               and (like is None or same_text(mask, like))
                               and (unlike is None or not same_text(mask, unlike)))

    def folder_layout(self, faces: list[np.ndarray], name: str, first: bool) -> None:
        """From Home Settings, set the next layout and folders on, then browse."""
        library, source, settings = faces
        self.press("A")
        self.check("A opens Settings", self.covered(settings), folder_layout=name)
        title, _ = self.settled_label(box=SETTINGS_TITLE_BOX)
        self.check("Settings shows a steady page title", title is not None, layout=name)
        for tab in ("Game Defaults", "Setup"):
            title, _ = self.press_until("R", unlike=title, box=SETTINGS_TITLE_BOX)
            self.check(f"R reaches {tab}", title is not None, layout=name)
        setup = title
        self.walk_rows(4, "Setup's DOWNs reach Library", layout=name)
        title, _ = self.press_until("A", unlike=setup, box=SETTINGS_TITLE_BOX)
        self.check("A opens Library settings", title is not None, layout=name)
        if first:
            self.walk_rows(1, "Library's DOWN reaches Library Folders", layout=name)
            self.press("RIGHT")
        else:  # Library Layout is the first row, cycling the five layouts.
            self.press("RIGHT")
        self.pause(1.0)
        self.shot(f"folders-{name.lower()}-setting", self.emulator.frame())
        self.check("B returns from Library settings to Setup",
                   self.press_until("B", like=setup, box=SETTINGS_TITLE_BOX)[0] is not None,
                   layout=name)
        self.press("B")
        self.check("Save & Exit returns Home with the folder layout",
                   self.settled_label(like=settings)[0] is not None, layout=name)
        self.folders_on = True
        for face in (source, library):
            self.check("LEFT returns toward Home Library",
                       self.press_until("LEFT", like=face)[0] is not None, layout=name)

    def folders(self) -> None:
        """Dotted folders and A/B/X parents in every layout, on a real FAT image."""
        home = self.boot()
        faces = [home]
        for n in (1, 2):
            faces.append(self.turn(faces, "RIGHT", f"folder-face-{n}"))
        self.folder_transitions: list[dict[str, object]] = []
        for index, (layout, (box, previous, following)) in enumerate(FOLDER_LAYOUTS.items()):
            prefix = layout.lower()
            self.folder_layout(faces, layout, first=index == 0)
            stray = self.folder_step("A", f"{prefix}-root-open", box)
            self.check("the dotted empty folder is a Library card", stray is not None, layout=layout)
            no_page = self.folder_step("Y", f"{prefix}-library-folder-y", box, like=stray)
            self.check("Library folders keep their normal navigation when Y is pressed",
                       no_page is not None, layout=layout)
            if index == 0:
                _, refused = self.pictures(8.0)
                self.check("the dotted empty folder's oversized PNG never shows",
                           refused < PICTURE_PIXELS, pixels=refused)
            parent = None
            for button in ("A", "B", "X"):
                empty = self.folder_step("A", f"{prefix}-empty-open-{button.lower()}",
                                         box, unlike=stray)
                self.check("the dotted empty folder contains only its parent card",
                           empty is not None, layout=layout, return_button=button)
                parent = empty
                if button == "A":
                    self.press(following)
                    self.check("the empty folder has no second card to browse",
                               self.settled_label(like=empty, box=box)[0] is not None, layout=layout)
                back = self.folder_step(button, f"{prefix}-empty-return-{button.lower()}",
                                        box, like=stray)
                self.check("A-parent, B and X return to the same empty folder card",
                           back is not None, layout=layout, button=button)
            racing, _ = self.press_until(following, unlike=stray, box=box)
            self.check("the dotted nonempty folder follows the empty folder", racing is not None,
                       layout=layout)
            if index == 0:
                shown, refused = self.pictures(30.0, until_shown=True)
                self.shot("dotted-folder-picture", self.last_rgb)
                self.check("the dotted folder's sibling PNG becomes its poster",
                           shown >= PICTURE_PIXELS, pixels=shown)
                self.check("a refused PNG never appears beside the dotted folder",
                           refused < PICTURE_PIXELS, pixels=refused)
            for button in ("A", "B", "X"):
                classics = self.folder_step("A", f"{prefix}-racing-open-{button.lower()}",
                                            box, unlike=racing)
                self.check("A opens a dotted ancestor folder", classics is not None,
                           layout=layout, return_button=button)
                if index == 0 and button == "A":
                    _, refused = self.pictures(8.0)
                    self.check("the dotted nested folder's damaged PNG never shows",
                               refused < PICTURE_PIXELS, pixels=refused)
                first = self.folder_step("A", f"{prefix}-nested-open-{button.lower()}",
                                         box, unlike=classics)
                self.check("A opens the dotted nested folder", first is not None,
                           layout=layout, return_button=button)
                if button == "A":
                    # Parent + direct game + flattened game in Old.Saves.v2:
                    # three cards. A fourth or missing card fails the wrap.
                    title = first
                    for step in (1, 2):
                        title, _ = self.press_until(following, unlike=title, box=box)
                        self.check("the dotted nested listing moves to another card",
                                   title is not None, layout=layout, step=step)
                    self.check("two steps include the game beneath the second dotted ancestor",
                               overlap(title, first) < DIFFERENT, layout=layout)
                    self.check("three steps wrap the exact nested listing",
                               self.press_until(following, like=first, box=box)[0] is not None,
                               layout=layout)
                    selected, _ = self.press_until(previous, like=parent, box=box)
                    self.check("the explicit nested parent card is reachable", selected is not None,
                               layout=layout)
                back = self.folder_step(button, f"{prefix}-nested-return-{button.lower()}",
                                        box, like=classics)
                self.check("A-parent, B and X restore the same nested folder card",
                           back is not None, layout=layout, button=button)
                if button == "A":
                    selected, _ = self.press_until(previous, like=parent, box=box)
                    self.check("the ancestor's explicit parent card is reachable",
                               selected is not None, layout=layout)
                back = self.folder_step(button, f"{prefix}-racing-return-{button.lower()}",
                                        box, like=racing)
                self.check("A-parent, B and X restore the same ancestor folder card",
                           back is not None, layout=layout, button=button)
            current_root = racing
            for button in ("A", "B", "X"):
                if button == "A":
                    # Parent is index zero, before the two folders. The
                    # returned Racing.v1 card is index two in every layout.
                    selected, _ = self.press_until(previous, unlike=racing, box=box)
                    self.check("the empty folder precedes the root parent", selected is not None,
                               layout=layout)
                    selected, _ = self.press_until(previous, like=parent, box=box)
                    self.check("the /games explicit parent card is reachable", selected is not None,
                               layout=layout)
                back = self.folder_step(button, f"{prefix}-root-return-{button.lower()}",
                                        LABEL_BOX, like=home)
                self.check("A-parent, B and X from /games return Home", back is not None,
                           layout=layout, button=button)
                # The parent card is skipped on entry when other cards
                # exist; B/X retain the actual non-parent selected card.
                expected = stray if button == "A" else current_root
                current_root = self.folder_step("A", f"{prefix}-root-reopen-{button.lower()}",
                                                box, like=expected)
                self.check("Home reopens /games on the expected folder card", current_root is not None,
                           layout=layout, after=button,
                           focus="first actual card" if button == "A" else "same selected card")
                if button == "A":
                    current_root, _ = self.press_until(following, like=racing, box=box)
                    self.check("root B and X use the non-first dotted folder",
                               current_root is not None, layout=layout)
            self.check("the next layout starts at the first actual folder",
                       self.press_until(previous, like=stray, box=box)[0] is not None, layout=layout)
            back = self.folder_step("B", f"{prefix}-root-finish", LABEL_BOX, like=home)
            self.check("B leaves the checked root on Home Library", back is not None, layout=layout)
            for face in faces[1:]:
                self.check("RIGHT returns toward Home Settings",
                           self.press_until("RIGHT", like=face)[0] is not None, layout=layout)

    def covered(self, reference: np.ndarray, box: tuple[int, int, int, int] = LABEL_BOX) -> bool:
        """Wait for the text in a box to stop matching reference: another screen opened over it."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            time.sleep(0.3)
            if overlap(text_mask(self.gray(), box), reference) < DIFFERENT:
                self.pause(1.0)  # let the screen finish arriving before the picture
                self.gray()
                return True
        return False

    def quiet(self, box: tuple[int, int, int, int] = LABEL_BOX, level: int = TEXT_LEVEL,
              seconds: float = SETTLE_SECONDS) -> bool:
        """Wait for a box to hold no word for two seconds of the console's time
        on end: longer than a screen takes to go, so a word drawn once it has
        gone still shows."""
        deadline = Deadline(self.emulator, seconds)
        empty = None
        while not deadline.expired():
            if has_label(text_mask(self.gray(), box, level)):
                empty = None
            elif empty is None:
                empty = Deadline(self.emulator, 2.0)
            elif empty.expired():
                return True
            time.sleep(0.15)
        return False

    def open_and_close(self, face: np.ndarray, n: int, inside=None) -> None:
        self.press("A")
        opened = self.covered(face)
        self.shot(f"face-{n + 1}-open", self.last_rgb)
        self.check("A opens the face", opened, face=n + 1)
        if inside:
            inside()
        self.press("B")
        mask, _ = self.settled_label(like=face)
        self.shot(f"face-{n + 1}-back", self.last_rgb)
        self.check("B comes back to the same face", mask is not None, face=n + 1)

    def browse_library(self) -> None:
        """RIGHT twice then LEFT twice: a new title each way, and back to the first.
        Then A opens that game's details, UP and A its settings, B comes back
        to the details and B again to the game; then a launch that fails comes
        back to it too."""
        titles = []
        first, _ = self.settled_label(box=TITLE_BOX)
        self.check("the Library shows a game's title", first is not None)
        titles.append(first)
        for n in (1, 2):
            title, _ = self.press_until("RIGHT", unlike=titles[-1], box=TITLE_BOX)
            self.shot(f"library-right-{n}", self.last_rgb)
            self.check("RIGHT moves to the next game", title is not None, step=n)
            titles.append(title)
        for n, expected in ((1, titles[1]), (2, titles[0])):
            title, _ = self.press_until("LEFT", like=expected, box=TITLE_BOX)
            self.shot(f"library-left-{n}", self.last_rgb)
            self.check("LEFT goes back a game", title is not None, step=n)
        self.press("A")
        opened = self.covered(titles[0], TITLE_BOX)
        self.shot("game-details", self.last_rgb)
        self.check("A opens the game's details", opened)
        details, _ = self.settled_label(box=DETAIL_TITLE_BOX)
        self.check("the details show the game's title", details is not None)
        # Two presses reach Settings whether or not a Cheats row sits between.
        for _ in range(2):
            self.press("UP")
            self.pause(0.3)
        self.press("A")
        opened = self.covered(details, DETAIL_TITLE_BOX)
        self.shot("game-settings", self.last_rgb)
        self.check("UP and A open the game's settings", opened)
        self.press("B")
        back, _ = self.settled_label(like=details, box=DETAIL_TITLE_BOX)
        self.shot("game-details-back", self.last_rgb)
        self.check("B comes back to the game's details", back is not None)
        self.press("B")
        title, _ = self.settled_label(like=titles[0], box=TITLE_BOX)
        self.shot("library-back", self.last_rgb)
        self.check("B comes back to the same game", title is not None)
        self.launch_fails_cleanly(titles[0])

    def launch_fails_cleanly(self, title: np.ndarray) -> None:
        """A and A launch the game. Dolphin has no IPL ROM and the demo disc no
        swiss/patches/ipl.bin, so the launch cannot read BS2: it must say so and,
        once A dismisses the message, come back to the same game in the Library.
        It used to free a pointer it had never set and crash. The game is from
        the console's other region, so the launch switches to that region's
        video mode first; the menu must switch back, which the probe checks at
        the end of the route (a launch that failed used to leave the menu at a
        PAL game's 50 Hz on an NTSC console)."""
        self.press("A")
        self.check("A opens the game's details again", self.covered(title, TITLE_BOX))
        self.press("A")
        self.check("a launch that cannot read BS2 comes back to the Library",
                   self.back_to_library(title, "launch-failure"))

    def back_to_library(self, title: np.ndarray, tag: str) -> bool:
        """After a launch that fails: A dismisses its message, and the Library
        must come back on the same game."""
        back, deadline = None, Deadline(self.emulator, BOOT_SECONDS / 2)
        while back is None and not deadline.expired():
            time.sleep(2.0)
            self.gray()
            self.shot(tag, self.last_rgb)
            if diagnose(self.last_rgb):
                break  # a crash or a black screen: check() reports which
            self.press("A")  # dismisses the failure message once it is up
            back, _ = self.settled_label(4, like=title, box=TITLE_BOX)
        self.shot(f"{tag}-back", self.last_rgb)
        return back is not None

    def change_source(self) -> None:
        """A on Change Source opens the device picker, RIGHT shows the next device
        (the disc drive, then Memory Card Slot A in Dolphin) and B leaves it."""
        before = text_mask(self.gray(), TITLE_BOX)
        self.press("A")
        first, _ = self.settled_label(unlike=before, box=TITLE_BOX)
        self.shot("source-picker", self.last_rgb)
        self.check("Change Source opens the device picker", first is not None)
        name, _ = self.press_until("RIGHT", unlike=first, box=TITLE_BOX)
        self.shot("source-picker-right", self.last_rgb)
        self.check("RIGHT shows the next device", name is not None)
        self.press("B")
        self.check("B leaves the device picker", self.covered(name, TITLE_BOX))

    def text_until(self, box: tuple[int, int, int, int], wanted) -> np.ndarray | None:
        """Steady text in a box, or none, that wanted(mask) accepts: None when
        it never comes."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        previous = None
        while not deadline.expired():
            mask = text_mask(self.gray(), box)
            if wanted(mask) and previous is not None and same_text(mask, previous):
                return mask
            previous = mask if wanted(mask) else None
            time.sleep(0.15)
        return None

    def info(self, button: str, like: np.ndarray | None = None,
             unlike: np.ndarray | None = None) -> np.ndarray | None:
        """A step in Memory Cards, and the focused save's name it comes to:
        its comment, once the save's art is read, rather than its file name."""
        self.press(button)
        self.pause(ART_SECONDS)
        return self.text_until(INFO_BOX, lambda mask: has_label(mask) and
                               (like is None or same_text(mask, like)) and
                               (unlike is None or not same_text(mask, unlike)))

    def differs(self, reference: np.ndarray, box: tuple[int, int, int, int]) -> bool:
        """Waits for the text in a box to be other words than reference, or none."""
        found = self.text_until(box, lambda mask: not same_text(mask, reference)) is not None
        self.pause(0.5)
        self.gray()
        return found

    def message(self, seconds: float = BOOT_SECONDS / 4) -> bool:
        """Waits for Memory Cards' maroon box: an operation is done."""
        deadline = Deadline(self.emulator, seconds)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            if message_up(self.last_rgb, self.menu_wide):
                return True
            time.sleep(0.2)
        return False

    def message_closes(self) -> bool:
        """Waits for the maroon box to close by itself (2 s)."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            if not message_up(self.last_rgb, self.menu_wide):
                return True
            time.sleep(0.2)
        return False

    def folder_changes(self, folder: Path, before: set[str]) -> set[str]:
        """A memory card's saves once Dolphin has written them to its folder."""
        deadline = Deadline(self.emulator, FLUSH_SECONDS)
        while not deadline.expired() and saves(folder) == before:
            time.sleep(0.25)
        self.pause(1.0)  # the file whole, not as it is being written
        return saves(folder)

    def steps(self, buttons: str, seconds: float = 0.6) -> None:
        for button in buttons.split():
            self.press(button)
            self.pause(seconds)

    def system_rows(self) -> None:
        """Memory Cards (row one) when the route has cards, then File Browser
        (row two)."""
        if self.cards:
            self.memory_cards()
        self.file_browser(1 if self.cards else 2)

    def files_list(self, shown: bool, pane: int | None = None) -> bool:
        """Waits for the File Browser to be on the screen (with pane focused,
        when one is given), or gone."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            up = files_screen(self.last_rgb, self.menu_wide)
            if up == shown and (pane is None or not up or
                                active_pane(self.last_rgb, self.menu_wide) == pane):
                return True
            time.sleep(0.3)
        return False

    def files_path(self, like: np.ndarray | None = None,
                   unlike: np.ndarray | None = None, right: bool = False,
                   box: tuple[int, int, int, int] | None = None) -> np.ndarray | None:
        """A pane's path line (or box) once it reads like (or unlike) a mask."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            mask = files_text(self.last_rgb, self.menu_wide, right, box)
            if mask.any() and (like is None or same_text(mask, like)) and \
                    (unlike is None or not same_text(mask, unlike)):
                return mask
            time.sleep(0.3)
        return None

    def files_boxed(self, opened: bool, pane: int = 0) -> bool:
        """Waits for a box beside a row of pane (opened), or for none with
        the File Browser still up."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            if files_box(self.last_rgb, pane, self.menu_wide) == opened and \
                    files_screen(self.last_rgb, self.menu_wide):
                return True
            time.sleep(0.3)
        return False

    def file_browser(self, downs: int) -> None:
        """DOWN to File Browser and A: the File Browser's two panes, the left
        one focused, though the disc has games the Library would show. RIGHT
        focuses the right pane, at the Source's root: DOWN and A open a folder
        and X comes back to the same rows, then LEFT focuses the left again.
        A on /games, which the left pane focuses as the folder the Library
        showed, reads it into the left pane and X comes back up; Z opens the
        Actions box beside its row and B closes it; B comes back to System's
        rows."""
        self.steps(" ".join(["DOWN"] * downs))
        system, _ = self.settled_label()
        self.press("A")
        shown = self.files_list(True, pane=0)
        self.shot("file-browser", self.last_rgb)
        self.check("File Browser opens its two panes, the left one focused", shown)
        self.press("RIGHT")
        self.check("RIGHT focuses the right pane", self.files_list(True, pane=1))
        self.shot("file-browser-right", self.last_rgb)
        # The right pane opens at the root, where a disc lists itself first:
        # DOWN is a folder on every route's Source (apps, or games).
        self.press("DOWN")
        self.pause(1.0)
        top = self.files_path(right=True)
        rows = self.files_path(right=True, box=FILES_RIGHT_ROWS_BOX)
        self.press("A")
        inside = self.files_path(unlike=top, right=True) if top is not None else None
        self.shot("file-browser-right-folder", self.last_rgb)
        self.check("A on a folder opens it in the right pane",
                   inside is not None and active_pane(self.last_rgb, self.menu_wide) == 1)
        self.press("X")
        back = self.files_path(like=top, right=True) if top is not None else None
        again = self.files_path(like=rows, right=True, box=FILES_RIGHT_ROWS_BOX) \
            if rows is not None else None
        self.check("X in the right pane comes back up to the same rows",
                   back is not None and again is not None)
        self.press("LEFT")
        self.check("LEFT focuses the left pane again", self.files_list(True, pane=0))
        # The left pane opens on the folder the Library last showed, /games:
        # with File Browser it is a list of files like any other.
        top = self.files_path()
        self.press("A")
        inside = self.files_path(unlike=top) if top is not None else None
        self.shot("file-browser-folder", self.last_rgb)
        self.check("A on a folder opens it in the left pane",
                   inside is not None and files_screen(self.last_rgb, self.menu_wide))
        self.press("X")
        back = self.files_path(like=top) if top is not None else None
        self.check("X comes back up a folder", back is not None)
        self.press("Z")
        opened = self.files_boxed(True)
        self.shot("file-browser-z", self.last_rgb)
        self.check("Z opens the Actions box beside the row", opened)
        self.press("B")
        self.check("B closes the Actions box",
                   self.files_boxed(False) and self.files_list(True, pane=0))
        if self.cards:
            self.files_storage()
        self.press("B")
        gone = self.files_list(False)
        label, _ = self.settled_label(like=system) if system is not None else (None, 0.0)
        self.shot("file-browser-back", self.last_rgb)
        self.check("B leaves the File Browser for System's rows", gone and label is not None)

    def files_storage_menu(self, pane: int, shown: bool) -> bool:
        """Waits for pane's storage menu to be open (shown), or closed with
        the File Browser still up."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        while not deadline.expired():
            self.last_rgb = self.emulator.frame()
            if files_menu(self.last_rgb, pane, self.menu_wide) == shown and \
                    files_screen(self.last_rgb, self.menu_wide):
                return True
            time.sleep(0.3)
        return False

    def files_rows(self, right: bool, like: np.ndarray | None = None,
                   unlike: np.ndarray | None = None) -> np.ndarray | None:
        """A pane's rows once they read like (or unlike) a mask."""
        return self.files_path(like=like, unlike=unlike, right=True,
                               box=FILES_RIGHT_ROWS_BOX if right else FILES_LEFT_ROWS_BOX)

    def files_storage(self) -> None:
        """The second device, with a memory card in each slot. R opens the
        right pane's storage menu beside its button and B closes it. R, DOWN
        (from the Source to Memory Card - Slot A, next in Swiss's order) and A
        show the card's saves in the right pane; RIGHT and DOWN browse them.
        Y swaps the sides, the card becoming the Source on the left, and Y
        again swaps back, the left pane as it was. L opens the left pane's
        menu and B closes it."""
        disc = self.files_rows(right=True)
        self.press("R")
        opened = self.files_storage_menu(1, True)
        self.shot("file-browser-storage-right", self.last_rgb)
        self.check("R opens the right pane's storage menu", opened)
        self.press("B")
        self.check("B closes the storage menu", self.files_storage_menu(1, False))
        self.press("R")
        self.files_storage_menu(1, True)
        # The GC Loader route lists the drive's disc between them, greyed.
        self.steps("DOWN" if self.storage == "dvd" else "DOWN DOWN")
        self.press("A")
        card = self.files_rows(right=True, unlike=disc) if disc is not None else None
        source = self.files_path(right=True, box=FILES_NAME_BOXES[0])  # focused: white
        self.shot("file-browser-second-device", self.last_rgb)
        self.check("R and A on Memory Card - Slot A show its saves in the right pane",
                   card is not None and not files_menu(self.last_rgb, 1, self.menu_wide))
        self.press("RIGHT")
        self.check("RIGHT focuses the second device's pane", self.files_list(True, pane=1))
        other = self.files_path(right=True, box=FILES_NAME_BOXES[1])
        # The info bar names the next save.
        name = self.files_path(right=True, box=FILES_INFO_TITLE_BOX)
        self.press("DOWN")
        moved = self.files_path(unlike=name, right=True, box=FILES_INFO_TITLE_BOX) \
            if name is not None else None
        self.check("DOWN browses the second device", moved is not None)
        left = self.files_rows(right=False)
        right = self.files_rows(right=True)
        self.press("Y")
        # The Source's name moves to the focused right pane once both sides
        # are mounted and read (a disc drive resets on the way).
        moved = self.files_path(like=source, right=True, box=FILES_NAME_BOXES[1]) \
            if source is not None else None
        swapped = self.files_rows(right=False, unlike=left) if left is not None else None
        changed = self.files_rows(right=True, unlike=right) if right is not None else None
        self.shot("file-browser-swapped", self.last_rgb)
        self.check("Y swaps the sides: the card is the Source, the disc on the right",
                   moved is not None and swapped is not None and changed is not None and
                   self.files_list(True, pane=1))
        self.press("Y")
        home = self.files_path(like=other, right=True, box=FILES_NAME_BOXES[1]) \
            if other is not None else None
        back = self.files_rows(right=False, like=left) if left is not None and home is not None \
            else None
        again = self.files_rows(right=True, like=right) if right is not None else None
        self.check("Y again swaps them back", back is not None and again is not None)
        self.press("L")
        opened = self.files_storage_menu(0, True)
        self.shot("file-browser-storage-left", self.last_rgb)
        self.check("L opens the left pane's storage menu", opened)
        self.press("B")
        self.check("B closes the left storage menu", self.files_storage_menu(0, False))
        if self.storage == "dvd":
            self.files_not_ready()

    def files_not_ready(self) -> None:
        """A second device that isn't there. R, UP twice (from Memory Card -
        Slot A, which the menu focuses, round to Other devices...) and A open
        Swiss's destination picker; Z lists every
        device and RIGHT twice goes from Memory Card - Slot A to KunaiGC,
        which A chooses. The right pane says it isn't ready instead of rows,
        Y leaves the sides as they are, and R and A put the disc back."""
        left = self.files_rows(right=False)
        self.press("R")
        self.files_storage_menu(1, True)
        self.steps("UP UP A")
        self.pause(2.0)
        self.steps("Z RIGHT RIGHT")
        self.press("A")
        message = self.files_path(right=True, box=FILES_RIGHT_MESSAGE_BOX)
        rows = files_text(self.last_rgb, self.menu_wide, box=FILES_RIGHT_ROWS_BOX)
        self.shot("file-browser-not-ready", self.last_rgb)
        self.check("a device that isn't there: the right pane says so, with no rows",
                   message is not None and not rows.any() and
                   files_screen(self.last_rgb, self.menu_wide))
        self.press("RIGHT")
        self.press("Y")
        self.pause(1.5)
        same = self.files_rows(right=False, like=left) if left is not None else None
        self.check("Y doesn't swap onto it",
                   same is not None and self.files_list(True, pane=1) and
                   self.files_path(like=message, right=True, box=FILES_RIGHT_MESSAGE_BOX) is not None)
        self.press("R")
        self.files_storage_menu(1, True)
        self.press("A")
        back = self.files_rows(right=True)
        self.check("R and A on the disc put it back on the right",
                   back is not None and not files_text(self.last_rgb, self.menu_wide,
                                                       box=FILES_RIGHT_MESSAGE_BOX).any())

    def files_goto(self, image: Path, focus: str, target: str, folder: str = "") -> None:
        """Moves the focused pane's focus from one entry to another, by their
        rows in the card's folder (its top by default) as the File Browser
        sorts it."""
        names = card.listing(image, folder)
        rows = names.index(target) - names.index(focus)
        self.steps(" ".join(["DOWN" if rows > 0 else "UP"] * abs(rows)), 0.4)

    def files_said(self, what: str, failed: bool = False) -> np.ndarray | None:
        """The maroon message comes, says an operation ended, and goes by
        itself (2 s); a failure's stays until A. Its words, as a mask."""
        said = self.message(SETTLE_SECONDS)
        self.pause(0.3)  # in whole, not fading in
        words = text_mask(detection_frame(self.emulator.frame(), self.menu_wide).max(axis=2),
                          FILES_MESSAGE_TEXT_BOX) if said else None
        self.shot(what.replace(" ", "-"), self.last_rgb)
        self.check(f"{what}: the message says so", said)
        if failed:
            self.pause(3.0)
            self.check(f"{what}: the failure's message waits for A",
                       message_up(self.emulator.frame(), self.menu_wide))
            self.press("A")
        self.check(f"{what}: the message goes", self.message_closes())
        self.pause(1.0)  # both panes read again
        return words

    def files_stop(self, what: str, finished: np.ndarray | None) -> None:
        """B on a copy's progress card: the card is up when B is pressed, and
        the message that comes isn't the one a finished copy has."""
        deadline = Deadline(self.emulator, SETTLE_SECONDS)
        up = False
        while not deadline.expired() and not up:
            self.last_rgb = self.emulator.frame()
            up = files_progress(self.last_rgb, self.menu_wide)
        self.shot(f"files-{what.replace(' ', '-')}-progress", self.last_rgb)
        self.check(f"{what}: the progress card is up when B is pressed", up)
        self.press("B")
        words = self.files_said(what)
        self.check(f"{what}: the message isn't a finished copy's",
                   words is not None and finished is not None and not same_text(words, finished))

    def files(self) -> None:
        """Operations to the other side, with a GC Loader as the Source and an
        SD card in SD2SP2 (card.second_card) beside it. System, File Browser,
        then R, DOWN and A put the SD card on the right. Every operation is
        checked on the card images themselves:
          - Z on Indigo-README.txt opens the Actions box beside it; X asks
            "Copy to" the SD card, with a ghost row where the copy lands, and
            A copies it: the SD card gains the same bytes;
          - the same again finds it there: Keep both writes a second copy;
          - RIGHT, then Z and Y on b-two.txt and A on Swiss's Move question,
            move it onto the GC Loader: gone from the SD card, there whole;
          - Z and R on a-one.txt, L four times, A and START name it a-one1.txt;
          - Z and Z on c-three.txt ask Delete's question: A alone deletes
            nothing, L held with A deletes it;
          - Z, X and A on big.bin start a copy that B stops: the message
            says so and nothing of it is left on the GC Loader;
          - Z, Y and A start a Move of it that B stops: it stays on the SD
            card whole and nothing of it is left on the GC Loader;
          - A on backups opens it on the right; LEFT, Z, X and A on b-two.txt
            copy it into that folder, not the SD card's top;
          - a game's Detail, the first since boot, shows its poster
            (files_poster);
          - L, DOWN and A make the SD card the Source, as for games on it
            with settings on the GC Loader;
        and B leaves, for Memory Cards (files_memory_cards).
        files_write_fails runs on the next boot."""
        gcl, sd2 = self.sd_image, self.second_sd
        faces = [self.boot()]
        for n in range(1, 4):
            faces.append(self.turn(faces, "RIGHT", f"files-right-{n}"))
        self.press("A")
        self.check("A opens System", self.covered(faces[3]))
        self.steps("DOWN DOWN")
        system, _ = self.settled_label()
        self.check("System shows its File Browser row before opening it", system is not None)
        self.press("A")
        self.check("File Browser opens on the GC Loader", self.files_list(True, pane=0))
        before = self.files_path(right=True, box=FILES_NAME_BOXES[1])
        self.press("R")
        self.files_storage_menu(1, True)
        self.steps("DOWN A")
        name = self.files_path(right=True, box=FILES_NAME_BOXES[1], unlike=before) \
            if before is not None else None
        self.shot("files-two-devices", self.last_rgb)
        self.check("R, DOWN and A put the SD card in SD2SP2 on the right", name is not None)

        readme, kept = "Indigo-README.txt", "Indigo-README_00.txt"
        original = card.read_card(gcl, readme)
        self.files_goto(gcl, "apps/", readme)
        self.press("Z")
        opened = self.files_boxed(True)
        self.pause(0.5)
        self.shot("files-actions", self.emulator.frame())
        self.check("Z opens the Actions box beside the file", opened)
        self.press("X")
        self.pause(0.8)
        self.shot("files-copy-question", self.emulator.frame())
        self.check("X asks Copy to the other side", self.files_boxed(True))
        self.press("A")
        finished = self.files_said("copy")
        self.check("the copy on the SD card is the file, byte for byte",
                   original is not None and card.read_card(sd2, readme) == original)
        self.steps("Z X A", 0.8)
        self.shot("files-exists", self.emulator.frame())
        self.press("A")
        self.files_said("keep both")
        self.check("Keep both writes a second copy beside the first",
                   card.read_card(sd2, kept) == original and card.read_card(sd2, readme) == original)

        self.press("RIGHT")
        self.check("RIGHT focuses the SD card's pane", self.files_list(True, pane=1))
        self.files_goto(sd2, kept, "b-two.txt")
        self.steps("Z Y", 0.8)
        self.shot("files-move-question", self.emulator.frame())
        self.press("A")
        self.files_said("move")
        self.check("Move takes it off the SD card and puts it on the GC Loader whole",
                   card.read_card(sd2, "b-two.txt") is None and
                   card.read_card(gcl, "b-two.txt") == card.SECOND_FILES["b-two.txt"])

        # Move focuses the row that took its place, big.bin.
        self.files_goto(sd2, "big.bin", "a-one.txt")
        self.steps("Z R", 0.8)
        self.steps("L L L L A", 0.3)
        self.shot("files-rename", self.emulator.frame())
        self.press("START")
        self.files_said("rename")
        self.check("Rename gives the file its new name",
                   card.read_card(sd2, "a-one.txt") is None and
                   card.read_card(sd2, "a-one1.txt") == card.SECOND_FILES["a-one.txt"])

        # Rename focuses the new name.
        self.files_goto(sd2, "a-one1.txt", "c-three.txt")
        self.steps("Z Z", 0.8)
        self.shot("files-delete-question", self.emulator.frame())
        self.press("A")
        self.pause(1.0)
        self.check("Delete's question: A alone deletes nothing",
                   card.read_card(sd2, "c-three.txt") is not None and
                   not message_up(self.emulator.frame(), self.menu_wide))
        self.pad.hold("L", "A")
        self.pause(0.3, fresh=True)
        self.pad.hold()
        self.files_said("delete")
        self.check("L held with A deletes it", card.read_card(sd2, "c-three.txt") is None)

        # Delete focuses the next row, Indigo-README.txt.
        big = card.SECOND_BIG[0]
        self.files_goto(sd2, readme, big)
        self.steps("Z X", 0.8)
        self.press("A")
        self.files_stop("stop", finished)
        self.check("a stopped copy leaves nothing of itself on the GC Loader",
                   card.read_card(gcl, big) is None)
        self.check("and the original stays on the SD card",
                   len(card.read_card(sd2, big) or b"") == card.SECOND_BIG[1])

        # A stopped Move: Swiss deletes the original only after a whole copy.
        self.steps("Z Y", 0.8)
        self.press("A")
        self.files_stop("stopped move", finished)
        self.check("a stopped Move leaves nothing of itself on the GC Loader",
                   card.read_card(gcl, big) is None)
        self.check("and keeps the original on the SD card, whole",
                   len(card.read_card(sd2, big) or b"") == card.SECOND_BIG[1])

        # The folder open on the other side is where a copy goes. The left
        # pane focuses b-two.txt, which the Move put there.
        self.files_goto(sd2, big, "backups/")
        self.press("A")
        self.pause(1.0)
        self.press("LEFT")
        self.check("LEFT focuses the GC Loader's pane", self.files_list(True, pane=0))
        self.steps("Z X", 0.8)
        self.press("A")
        self.files_said("copy into a folder")
        self.check("the copy lands in the folder open on the right, not the SD card's top",
                   card.read_card(sd2, "backups/b-two.txt") == card.SECOND_FILES["b-two.txt"] and
                   card.read_card(sd2, "b-two.txt") is None)
        self.files_poster(gcl, "b-two.txt")
        before = self.files_path(box=FILES_NAME_BOXES[0])
        self.press("L")
        self.files_storage_menu(0, True)
        self.steps("DOWN A")
        name = self.files_path(box=FILES_NAME_BOXES[0], unlike=before) if before is not None else None
        self.shot("files-source-sd2sp2", self.last_rgb)
        self.check("L, DOWN and A make the SD card in SD2SP2 the Source",
                   name is not None and self.files_list(True, pane=0))
        self.press("B")
        gone = self.files_list(False)
        # A fading File Browser can briefly fail its edge detector before
        # Home can take the next press. Require System's own stable words,
        # as file_browser() does, before UP and A open Memory Cards.
        label, _ = self.settled_label(like=system) if system is not None else (None, 0.0)
        self.check("B leaves the File Browser for System's rows", gone and label is not None)
        self.files_memory_cards(gcl, sd2)

    def files_memory_cards(self, gcl: Path, sd2: Path) -> None:
        """Settings on the GC Loader, the Source the SD card in SD2SP2,
        which holds a card image where Swiss's Emulate Memory Card keeps it
        (card.SECOND_IMAGE). With both slots empty, UP and A on System's
        rows open Memory Cards with both columns on the GC Loader's Save
        Folder, where an SD column starts. L lists
        both cards by name: DOWN and A put the SD2SP2's Save Folder on the
        left, and A opens the image. Erase takes its second save off and
        Move its first to the GC Loader's Save Folder on the right, and both
        cards are read back: the image is a card with no save left, the
        GC Loader has the first save whole, and the SD2SP2 no copy of it. B
        closes the image and B leaves Memory Cards."""
        self.steps("UP")
        self.press("A")
        self.pause(2.0)  # the screen opens, and its places are read
        gray = self.gray()
        left, right = text_mask(gray, LEFT_HEADER_BOX), text_mask(gray, RIGHT_HEADER_BOX)
        self.shot("files-memory-cards", self.last_rgb)
        self.check("Memory Cards opens both columns on SD, the slots being empty",
                   has_label(left) and has_label(right))
        self.press("L")
        self.pause(0.3)
        self.shot("files-memory-cards-storage-menu", self.emulator.frame())
        self.steps("DOWN A")
        moved = self.differs(left, LEFT_HEADER_BOX)
        source = text_mask(self.gray(), LEFT_HEADER_BOX)
        self.shot("files-memory-cards-sd2sp2", self.last_rgb)
        self.check("L, DOWN and A put the SD2SP2's Save Folder on the left, the GC Loader's on the right",
                   moved and has_label(source) and
                   same_text(text_mask(self.gray(), RIGHT_HEADER_BOX), right))
        image, _ = self.settled_label(box=INFO_BOX)
        self.check("the SD2SP2's card image has the focus", image is not None)
        self.press("A")
        self.check("A opens the SD2SP2's card image", self.differs(source, LEFT_HEADER_BOX))
        self.pause(ART_SECONDS)
        first, _ = self.settled_label(box=INFO_BOX)
        self.check("the image shows a save's comment", first is not None)
        self.shot("files-memory-cards-image", self.last_rgb)
        second = self.info("RIGHT", unlike=first)
        self.check("RIGHT comes to the image's second save", second is not None)
        self.save_details("files-memory-cards-details-to-erase")
        self.steps("A DOWN A")  # Actions (on Copy), Erase, then its question
        self.steps("UP")  # No is first: Yes
        self.press("A")
        self.check("Erase takes a save off the SD2SP2's image", self.message())
        self.check("the erase message closes by itself", self.message_closes())
        again = self.info("LEFT", like=first)
        self.check("LEFT comes back to the image's first save", again is not None)
        self.save_details("files-memory-cards-details-to-move")
        self.steps("A UP A")  # Actions (on Copy), Move, then its question
        self.shot("files-memory-cards-move-to", self.emulator.frame())
        self.press("A")  # Yes: "Move to the SD card?", the GC Loader's folder
        self.check("Move takes the image's save to the GC Loader", self.message())
        self.check("the move message closes by itself", self.message_closes())
        self.shot("files-memory-cards-moved", self.last_rgb)
        _, gcis = make_test_saves.virtual_card()
        raw = card.read_card(sd2, card.SECOND_IMAGE)
        export = f"{make_test_saves.SAVE_FOLDER}/{make_test_saves.RAW_EXPORT_NAME}"
        moved_save = card.read_card(gcl, export)
        try:
            left_saves = card_image_saves(raw) if raw is not None else None
        except ValueError as error:
            left_saves = None
            self.check("the SD2SP2's image reads as a card after Erase and Move", False, error=str(error))
        self.check("the SD2SP2's image is a card with no save left after Erase and Move",
                   left_saves == {}, saves=None if left_saves is None else len(left_saves))
        self.check("the GC Loader's Save Folder has the moved save, whole, and the SD2SP2 no copy",
                   moved_save is not None and same_save(gcis[0], moved_save) and
                   card.read_card(sd2, export) is None)
        self.press("B")
        back = self.text_until(LEFT_HEADER_BOX, lambda mask: same_text(mask, source))
        self.check("B closes the image to the SD2SP2's Save Folder", back is not None)
        self.press("B")
        self.check("B leaves Memory Cards", self.differs(source, LEFT_HEADER_BOX))

    def files_write_fails(self) -> None:
        """The next boot, with the SD card in SD2SP2 failing every write to
        a cluster it had free (card.free_sectors): R, DOWN and A put it on
        the right, A on backups opens it, and LEFT focuses the GC Loader. A
        game's Detail from the File Browser (files_detail), while the card
        still works, and X back up. Then Z, X and A copy Indigo-README.txt
        into backups. The write fails: the message says so and waits for A,
        and no whole copy is on the card. Once a write has failed, this card
        refuses every write after it (deleting another file fails too), so
        the unfinished file stays and the message says part of it is left;
        that the File Browser deletes it where the card allows is pinned by
        audit_files_contract.py. Last, the game's launch (files_launch)."""
        gcl, sd2 = self.sd_image, self.second_sd
        readme = "Indigo-README.txt"
        original = card.read_card(gcl, readme)
        faces = [self.boot()]
        for n in range(1, 4):
            faces.append(self.turn(faces, "RIGHT", f"files-fails-right-{n}"))
        self.press("A")
        self.steps("DOWN DOWN")
        self.press("A")
        self.check("File Browser opens on the GC Loader again", self.files_list(True, pane=0))
        self.press("R")
        self.files_storage_menu(1, True)
        self.steps("DOWN A")
        self.pause(1.0)
        self.press("RIGHT")  # on its first row past "..": backups
        self.press("A")
        self.pause(1.0)
        self.press("LEFT")
        self.check("LEFT focuses the GC Loader's pane", self.files_list(True, pane=0))
        top = self.files_detail(gcl, "apps/")
        self.press("X")
        self.check("X goes back up, to games/", self.files_path(like=top) is not None)
        self.files_goto(gcl, "games/", readme)
        self.steps("Z X", 0.8)
        self.press("A")
        self.files_said("failed write", failed=True)
        self.check("a copy whose write failed isn't on the SD card whole",
                   original is not None and card.read_card(sd2, f"backups/{readme}") != original)
        self.files_launch(gcl, readme)

    def files_game(self, image: Path, focus: str,
                   game: tuple[str, str] = card.PROBE_GAME) -> np.ndarray | None:
        """A on games/ opens it in the left pane, and DOWN goes to a game,
        the probe's by default. The path line at the top, as a mask."""
        self.files_goto(image, focus, "games/")
        top = self.files_path()
        self.press("A")
        inside = self.files_path(unlike=top) if top is not None else None
        self.check("A on games/ opens it in the left pane", inside is not None)
        # Flatten directory (on for /games) lists a folder's files in its
        # place: such a folder has no row, and they sort after the probe.
        names = [name for name in card.listing(image, "games")
                 if not (name.endswith("/") and len(card.listing(image, f"games/{name}")) > 1)]
        rows = names.index(card.game_file(*game)) - 1
        self.steps(" ".join(["DOWN"] * rows), 0.4)
        self.pause(1.0)
        return top

    def files_poster(self, image: Path, focus: str) -> None:
        """The first Detail since boot, opened from the File Browser on a
        game the poster pack has (files_game), so the pack opens only then:
        from Detail's first frame (POSTER_FRAMES) its card shows the game's
        poster, not the banner standing in for it. B comes back and
        X goes up again."""
        index = 0
        top = self.files_game(image, focus, card.GAMES[index])
        frames = self.emulator.burst(8.0, lambda: self.press("A"))
        shown = next((n for n, rgb in enumerate(frames) if not files_screen(rgb, self.menu_wide)), None)
        x0, y0, x1, y1 = DETAIL_CARD_BOX
        light = card.poster_light(index)
        poster = None
        for n in range(shown if shown is not None else len(frames), len(frames)):
            rgb = np.array(frames[n])
            if coloured(detection_frame(rgb, self.menu_wide)[y0:y1, x0:x1], light) >= POSTER_PIXELS:
                poster = n
                self.shot("files-detail-poster", rgb)
                break
        last = np.array(frames[len(frames) - 1])
        del frames
        if poster is None:
            self.shot("files-detail-no-poster", last)
        self.check("Detail from the File Browser shows the game's poster, not its banner",
                   poster is not None and poster - shown <= POSTER_FRAMES, first=shown, poster=poster)
        self.press("B")
        self.check("B comes back to the File Browser", self.files_list(True, pane=0))
        self.press("X")
        self.check("X goes back up", top is not None and self.files_path(like=top) is not None)

    def files_detail(self, image: Path, focus: str) -> np.ndarray | None:
        """The probe's game (files_game) and A open its Game Detail over the
        File Browser, already in place: every frame of its first 0.3 s, at 60
        a second, has the title Detail settles on (no card flies in). B comes
        back to the File Browser, the left pane on the same row and the right
        pane's storage, let go meanwhile, showing the same rows. Returns the
        top's path line."""
        top = self.files_game(image, focus)
        row = self.files_path(box=FILES_INFO_TITLE_BOX)
        rows = self.files_rows(right=False)
        right = self.files_rows(right=True)
        self.shot("files-game", self.last_rgb)
        frames = self.emulator.burst(8.0, lambda: self.press("A"))
        title, _ = self.settled_label(box=DETAIL_TITLE_BOX)
        self.shot("files-detail", self.last_rgb)
        self.check("A on a game in the File Browser opens its Game Detail",
                   title is not None and not files_screen(self.last_rgb, self.menu_wide))
        # The card where Detail settles it, in each of the first 0.3 s of
        # frames without the File Browser against the burst's last: a card
        # flying in, or nothing drawn yet (the Home cube), differs.
        x0, y0, x1, y1 = DETAIL_CARD_BOX
        card_at = lambda n: detection_frame(np.array(frames[n]), self.menu_wide)[y0:y1, x0:x1].astype(int)
        shown = next((n for n, rgb in enumerate(frames) if not files_screen(rgb, self.menu_wide)), None)
        moving = None
        if shown is not None:
            settled = card_at(len(frames) - 1)
            away = [round(float(np.abs(card_at(n) - settled).mean()), 1)
                    for n in range(shown, min(shown + 18, len(frames)))]
            moving = [shown + n for n, d in enumerate(away) if d > DETAIL_CARD_MOVED]
            self.shot("files-detail-first", np.array(frames[shown]))
        self.check("... already in place: its card doesn't move in its first frames",
                   moving is not None and len(moving) <= 1, first=shown, frames=len(frames), moving=moving,
                   away=away if shown is not None else None)
        del frames
        self.press("B")
        back = self.files_list(True, pane=0)
        same = self.files_path(like=row, box=FILES_INFO_TITLE_BOX) if row is not None else None
        again = self.files_rows(right=False, like=rows) if rows is not None else None
        other = self.files_rows(right=True, like=right) if right is not None else None
        self.shot("files-detail-back", self.last_rgb)
        self.check("B comes back to the File Browser, on the same row",
                   back and same is not None and again is not None)
        self.check("... with the right pane's storage back as it was", other is not None)
        return top

    def files_launch(self, image: Path, focus: str) -> None:
        """The probe's game (files_game), A and A again: Launch Game from its
        Detail must reach the probe as that game."""
        self.files_game(image, focus)
        self.press("A")
        detail, _ = self.settled_label(box=DETAIL_TITLE_BOX)
        self.check("A opens the game's Detail again", detail is not None)
        self.press("A")
        report = self.handoff("files-game")
        self.check("Launch Game from the File Browser starts the game with its own disc ID",
                   report["disc_id"] == card.PROBE_GAME[0], disc_id=report["disc_id"])

    def files_face(self, faces: list[np.ndarray]) -> None:
        """Down Face File Browser, from System: RIGHT turns to a face named
        unlike every other, A there opens the File Browser, and B comes back
        to that face, not System's."""
        mask, _ = self.press_until("RIGHT", unlike=faces[3])
        self.shot("after-system-apps-face-files", self.last_rgb)
        self.check("Down Face File Browser: after System comes a face of its own",
                   mask is not None and all(overlap(mask, face) < DIFFERENT for face in faces[:5]),
                   overlaps=[round(overlap(mask, face), 3) for face in faces[:5]]
                   if mask is not None else None)
        self.press("A")
        shown = self.files_list(True, pane=0)
        self.shot("files-face-list", self.last_rgb)
        self.check("A on the File Browser face opens the File Browser", shown)
        self.press("B")
        gone = self.files_list(False)
        back, _ = self.settled_label(like=mask) if mask is not None else (None, 0.0)
        self.shot("files-face-back", self.last_rgb)
        self.check("B from the File Browser comes back to the File Browser face",
                   gone and back is not None)

    def library_after_file_browser(self, faces: list[np.ndarray]) -> None:
        """From Apps, RIGHT to Library and A: after File Browser, the Library
        opens on a game again, not the File Browser. B and LEFT come back to Apps."""
        mask, _ = self.press_until("RIGHT", like=faces[0])
        self.check("RIGHT from Apps comes round to Library", mask is not None)
        self.press("A")
        title, _ = self.settled_label(box=TITLE_BOX)
        self.shot("library-after-file-browser", self.last_rgb)
        self.check("after File Browser, A on Library opens the Library",
                   title is not None and not files_screen(self.last_rgb, self.menu_wide) and
                   not legacy_folder_browser(self.last_rgb))
        self.press("B")
        mask, _ = self.settled_label(like=faces[0])
        self.check("B comes back to Library", mask is not None)
        mask, _ = self.press_until("LEFT", like=faces[4])
        self.check("LEFT turns back to Apps", mask is not None)

    def memory_cards(self) -> None:
        """On the System face, DOWN and A open Memory Cards, with a GCI folder
        memory card in each slot (self.cards: make_test_saves.py's Slot A and
        Slot B). Slot A's first save has the focus and its name is in the info
        bar. RIGHT moves along Slot A's first row, a new save each time, and a
        fourth RIGHT crosses to Slot B: LEFT then comes back to the row's last
        save, not the bump of a stack's edge, and on to the first. DOWN four
        times scrolls Slot A's stack (the arrow above it shows), and UP four
        times comes back. R swaps the right stack for another place and back.
        A opens save details, B returns without an action, and A Actions
        opens the box beside the save, which changes the bottom buttons. B
        closes that box. Move, Copy, Yes copies the
        save to Slot B: the maroon box says so and closes by itself, and Slot
        B's folder gains the save, its blocks the same. A on it again opens the
        box with Move dimmed, as Slot B has it now, and the buttons say why.
        Erase, then Yes over the No it starts on, erases it, and Slot A's
        folder no longer has it. L swaps the left stack and back, and B
        leaves, back to the System face."""
        slot_a, slot_b = self.cards / "A", self.cards / "B"
        self.press("DOWN")
        self.pause(0.6)
        self.press("A")
        self.pause(2.0)  # the screen opens, and the saves on it are read
        first, _ = self.settled_label(box=INFO_BOX)
        self.shot("memory-cards", self.last_rgb)
        self.check("A on Memory Cards opens the cube screen, a save's name below it",
                   first is not None)
        gray = self.gray()
        self.check("both stacks show a memory card: a letter, Open and the free blocks",
                   has_label(text_mask(gray, LEFT_HEADER_BOX)) and
                   has_label(text_mask(gray, RIGHT_HEADER_BOX)))
        browsing = text_mask(gray, FOOTER_BOX)
        seen = [first]
        for n in (1, 2, 3):
            mask = self.info("RIGHT", unlike=seen[-1])
            self.shot(f"memory-cards-right-{n}", self.last_rgb)
            self.check("RIGHT moves to the next save", mask is not None, step=n)
            seen.append(mask)
        crossed = self.info("RIGHT", unlike=seen[-1])
        self.shot("memory-cards-slot-b", self.last_rgb)
        self.check("RIGHT from the last column moves to another save",
                   crossed is not None and not any(same_text(crossed, mask) for mask in seen))
        self.check("LEFT from Slot B comes back to Slot A's last column, not a stack's edge",
                   self.info("LEFT", like=seen[3]) is not None)
        for n in (2, 1, 0):
            self.check("LEFT goes back a save", self.info("LEFT", like=seen[n]) is not None, to=n)
        for n in range(4):
            self.press("DOWN")
            self.pause(0.6)
        self.pause(ART_SECONDS)
        gray = self.gray()
        self.shot("memory-cards-scrolled", self.last_rgb)
        self.check("DOWN past the window's last row scrolls the stack: the arrow above it shows",
                   arrow_up(gray))
        for n in range(4):
            self.press("UP")
            self.pause(0.6)
        back = self.text_until(INFO_BOX, lambda mask: same_text(mask, first))
        self.check("UP comes back to the first save, the stack scrolled back", back is not None and
                   not arrow_up(self.gray()))
        self.swap("R", RIGHT_HEADER_BOX, "right")
        self.save_details("memory-cards-details")
        self.press("B")
        closed = self.text_until(FOOTER_BOX, lambda mask: same_text(mask, browsing))
        self.check("B closes details without opening an action", closed is not None)
        self.save_details("memory-cards-details-again")
        self.press("A")
        opened = self.differs(browsing, FOOTER_BOX)
        self.shot("memory-cards-box", self.last_rgb)
        self.check("A Actions opens the box beside the save", opened)
        menu = text_mask(self.gray(), FOOTER_BOX)
        self.press("B")
        closed = self.text_until(FOOTER_BOX, lambda mask: same_text(mask, browsing))
        self.check("B closes the box", closed is not None)
        before_b = saves(slot_b)
        self.steps("A A DOWN A")
        self.shot("memory-cards-copy-to", self.emulator.frame())
        self.press("A")
        self.check("Copy, Yes: the maroon box says the save is copied", self.message())
        self.shot("memory-cards-copied", self.last_rgb)
        gained = self.folder_changes(slot_b, before_b) - before_b
        name = next(iter(gained)) if len(gained) == 1 else ""
        self.check("Slot B's folder gains the save, its blocks the same as on Slot A",
                   bool(name) and (slot_a / name).exists() and
                   same_save((slot_a / name).read_bytes(), (slot_b / name).read_bytes()),
                   gained=sorted(gained))
        self.check("the maroon box closes by itself", self.message_closes())
        self.steps("A A", 0.3)
        self.differs(browsing, FOOTER_BOX)
        reason = text_mask(self.gray(), FOOTER_BOX)
        self.shot("memory-cards-dimmed", self.last_rgb)
        self.check("A on the copied save: Move is dimmed, and the buttons say why",
                   not same_text(reason, menu) and not same_text(reason, browsing))
        self.press("B")
        self.text_until(FOOTER_BOX, lambda mask: same_text(mask, browsing))
        before_a = saves(slot_a)
        self.steps("A A DOWN DOWN A")
        self.shot("memory-cards-erase", self.emulator.frame())
        self.steps("UP")
        self.press("A")
        self.check("Erase, Yes: the maroon box says the save is erased", self.message())
        self.shot("memory-cards-erased", self.last_rgb)
        gone = before_a - self.folder_changes(slot_a, before_a)
        self.check("the save is gone from Slot A's folder", bool(name) and gone == {name},
                   gone=sorted(gone))
        self.check("the maroon box closes by itself", self.message_closes())
        # The left storage menu also selects SD, then restores Slot A.
        self.swap("L", LEFT_HEADER_BOX, "left")
        left = text_mask(self.gray(), LEFT_HEADER_BOX)
        self.press("B")
        self.check("B leaves Memory Cards", self.differs(left, LEFT_HEADER_BOX))
        self.shot("memory-cards-left", self.last_rgb)

    def swap(self, button: str, box: tuple[int, int, int, int], side: str) -> None:
        """Choose SD by name with L or R, then restore that side's card.
        Each menu starts on its current place: A, B, SD in that order."""
        header = text_mask(self.gray(), box)
        self.press(button)
        self.pause(0.3)
        self.shot(f"memory-cards-{button.lower()}-storage", self.emulator.frame())
        if self.storage == "dvd":
            # This route has no configuration SD device. SD is named but
            # dimmed, with its reason; A must not replace a working card.
            self.steps("DOWN" if button == "R" else "DOWN DOWN")
            self.press("A")
            self.pause(0.3)
            self.check(f"{button}, unavailable SD leaves the {side} card in place",
                       same_text(text_mask(self.gray(), box), header))
            self.shot(f"memory-cards-{button.lower()}-sd-unavailable", self.last_rgb)
            self.press("B")
            self.check("B cancels the storage menu with its card still shown",
                       self.text_until(box, lambda mask: same_text(mask, header)) is not None)
            return
        self.steps("DOWN A" if button == "R" else "DOWN DOWN A")
        swapped = self.differs(header, box)
        self.shot(f"memory-cards-{button.lower()}", self.last_rgb)
        self.check(f"{button}, SD card chooses the {side} stack's storage", swapped)
        self.press(button)
        self.steps("UP A" if button == "R" else "UP UP A")
        again = self.text_until(box, lambda mask: same_text(mask, header))
        self.check(f"{button}, Slot {'B' if button == 'R' else 'A'} brings its memory card back",
                   again is not None)

    def open_save_details(self) -> bool:
        """Retry a missed A only while the browser's static context is unchanged.

        Once a panel or another screen change appears, A must not be repeated:
        on details it means Actions. Cube animation is not an input response,
        so compare the storage headers, info text and footer, not moving art.
        """
        boxes = (LEFT_HEADER_BOX, RIGHT_HEADER_BOX, INFO_BOX, FOOTER_BOX)
        before = self.gray()
        if save_details_panel(before):
            return True
        context = tuple(text_mask(before, box) for box in boxes)
        changed = False
        for attempt in range(PRESSES):
            self.press("A")
            deadline = Deadline(self.emulator, SETTLE_SECONDS)
            while not deadline.expired():
                gray = self.gray()
                if save_details_panel(gray):
                    return True
                changed = changed or any(not same_text(text_mask(gray, box), original)
                                         for box, original in zip(boxes, context))
                time.sleep(.15)
            if changed or attempt + 1 == PRESSES:
                return False
            self.pressed_again.append("A")
        return False

    def save_details(self, name: str) -> dict[str, np.ndarray]:
        """Open details and verify authored fields on the real screen.

        Host renderer contracts assert the exact words/date/block values;
        these masks prove the fields are actually visible in Dolphin. No
        action is selected until another A press.
        """
        self.check("A opens the save details panel", self.open_save_details())
        boxes = {"title": SAVE_DETAILS_TITLE_BOX, "blocks": SAVE_DETAILS_BLOCKS_BOX,
                 "size": SAVE_DETAILS_SIZE_BOX, "source": SAVE_DETAILS_SOURCE_BOX,
                 "icon": SAVE_DETAILS_ICON_BOX, "updated": SAVE_DETAILS_UPDATED_BOX,
                 "actions": SAVE_DETAILS_ACTIONS_BOX}
        fields = {}
        for field, box in boxes.items():
            mask, _ = self.settled_label(box=box, numeric=field in {"blocks", "size"})
            self.check(f"save details displays its {field} field", mask is not None,
                       aspect="16:9" if self.menu_wide else "4:3")
            fields[field] = mask
        self.shot(name, self.last_rgb)
        return fields

    def memory_folder_open(self, name: str, like: np.ndarray | None = None) -> np.ndarray:
        """Y opens the selected save-folder cube's retained identity page."""
        before = text_mask(self.gray(), FOLDER_PAGE_TITLE_BOX)
        self.press("Y")
        title = self.text_until(FOLDER_PAGE_TITLE_BOX,
                                lambda mask: has_label(mask) and not same_text(mask, before)
                                and (like is None or same_text(mask, like)))
        self.check("Y on the selected memory-card folder cube opens its identity page",
                   title is not None)
        self.check("the memory-card folder page displays its full path",
                   self.settled_label(box=FOLDER_PATH_BOX)[0] is not None)
        self.check("the memory-card folder page displays its contents and status",
                   self.settled_label(box=FOLDER_CONTENTS_BOX)[0] is not None)
        self.shot(name, self.last_rgb)
        return title

    def memory_folder_return(self, button: str, selected: np.ndarray, name: str) -> None:
        self.press(button)
        self.check("the folder page returns to the same selected memory-card cube",
                   self.text_until(INFO_BOX, lambda mask: same_text(mask, selected)) is not None,
                   button=button)
        self.shot(name, self.last_rgb)

    def memory_folder_saved(self, colour: int | None) -> None:
        """Bind native color selection to the actual device-prefixed FAT key."""
        settings = card.read_card(self.sd_image, "swiss/settings/global.ini") or b""
        key = rb"Memory Card Folder Colors=([^\r\n]*)"
        row = re.search(key, settings)
        path = rb"[a-z0-9]+:/swiss/saves/" + make_test_saves.MEMORY_FOLDER_NAME.encode() + rb"~"
        present = re.search(path + (str(colour).encode() if colour is not None else rb"[0-9]+") + rb"(?=;|$)",
                            row[1] if row else b"")
        self.check("memory-card folder color is saved under its device-prefixed path",
                   bool(present) if colour is not None else not present, colour=colour)
        self.check("memory-card color operations do not create Library folder colors",
                   b"Library Folder Colors=" not in settings)

    def memory_folder_identity(self, selected: np.ndarray) -> None:
        """Preview, save, cancel and reset; leave Azure for the next boot."""
        hint = self.settled_label(box=FOLDER_HINT_BOX)[0]
        self.check("the selected memory-card folder displays its contextual Y hint", hint is not None)
        self.shot("memory-folder-y-hint", self.last_rgb)
        normal = folder_cube_azure(self.last_rgb, self.menu_wide)
        title = self.memory_folder_open("memory-folder-path")
        path = self.settled_label(box=FOLDER_PATH_BOX)[0]
        contents = self.settled_label(box=FOLDER_CONTENTS_BOX)[0]
        self.press("Y")
        default = self.folder_color()
        self.check("Y resets the folder preview to Default", default is not None)
        self.press("RIGHT")
        indigo = self.folder_color(unlike=default)
        self.check("Right selects Indigo for the memory-card folder", indigo is not None)
        self.press("RIGHT")
        azure = self.folder_color(unlike=indigo)
        self.check("Right selects Azure for the memory-card folder", azure is not None)
        self.shot("memory-folder-azure-preview", self.last_rgb)
        self.memory_folder_return("A", selected, "memory-folder-azure-saved")
        self.memory_folder_saved(2)
        azure_pixels = folder_cube_azure(self.last_rgb, self.menu_wide)
        self.check("saving Azure colors the selected memory-card folder cube",
                   azure_pixels >= 12 and azure_pixels > normal + 8, pixels=azure_pixels)
        self.memory_folder_open("memory-folder-reopened", like=title)
        self.check("reopening keeps the selected folder's full path and contents",
                   self.settled_label(box=FOLDER_PATH_BOX, like=path)[0] is not None and
                   self.settled_label(box=FOLDER_CONTENTS_BOX, like=contents)[0] is not None)
        self.check("reopening selects the saved Azure value", self.folder_color(like=azure) is not None)
        self.press("RIGHT")
        self.check("another color can be previewed", self.folder_color(unlike=azure) is not None)
        self.memory_folder_return("B", selected, "memory-folder-cancel")
        self.memory_folder_saved(2)
        self.memory_folder_open("memory-folder-after-cancel", like=title)
        self.check("Cancel retains Azure", self.folder_color(like=azure) is not None)
        self.press("Y")
        self.check("Reset selects the original Default value",
                   self.folder_color(like=default, unlike=azure) is not None)
        self.memory_folder_return("A", selected, "memory-folder-default-saved")
        self.memory_folder_saved(None)
        self.memory_folder_open("memory-folder-before-restart", like=title)
        self.press("RIGHT")
        self.check("Indigo can be selected after reset", self.folder_color(like=indigo) is not None)
        self.press("RIGHT")
        self.check("Azure can be selected after reset", self.folder_color(like=azure) is not None)
        self.memory_folder_return("A", selected, "memory-folder-ready-for-restart")
        self.memory_folder_saved(2)
        self.memory_folder_reference = {"title": title, "path": path, "contents": contents,
                                        "azure": azure, "default": default, "selected": selected}

    def static_icon(self) -> None:
        """Watch a single authored texture; cube movement never counts as animation."""
        seen = 0
        animated = set()
        deadline = Deadline(self.emulator, 3.0)
        while not deadline.expired():
            rgb = self.emulator.frame()
            x0, y0, x1, y1 = stage_box(RAW_ICON_BOX, self.menu_wide)
            seen += coloured(rgb[y0:y1, x0:x1], make_test_saves.MEMORY_STATIC_COLOUR) >= 12
            frame = raw_icon_frame(rgb, self.menu_wide)
            if frame is not None:
                animated.add(frame)
            self.pause(.1)
        self.check("the static save displays its authored icon without invented animation frames",
                   seen >= 2 and not animated, samples=seen, unexpected_frames=sorted(animated))
        self.shot("memory-folder-static-icon", rgb)

    def memory_folder_saves(self, selected: np.ndarray) -> None:
        """Readable metadata and truthful static/missing icons inside the SD folder."""
        # The large SD glyph stays unchanged. Read only the actual folder path;
        # including that glyph can hide a shorter path change in widescreen.
        header = text_mask(self.gray(), MEMORY_LEFT_PATH_BOX)
        footer = text_mask(self.gray(), FOLDER_HINT_BOX)
        self.press("A")
        self.check("A opens the selected memory-card save folder",
                   self.text_until(MEMORY_LEFT_PATH_BOX, lambda mask: has_label(mask)
                                   and not same_text(mask, header)) is not None)
        self.pause(ART_SECONDS)
        first = self.settled_label(box=INFO_BOX)[0]
        self.check("the opaque save filename is replaced by readable comment metadata", first is not None)
        self.check("a save's footer differs from the folder's contextual Y controls",
                   not same_text(text_mask(self.gray(), FOLDER_HINT_BOX), footer))
        self.static_icon()
        static = self.save_details("memory-folder-static-details")
        self.press("B")
        self.check("B returns to the same static save", self.text_until(INFO_BOX,
                   lambda mask: same_text(mask, first)) is not None)
        second = self.info("RIGHT", unlike=first)
        self.check("the metadata-disabled racing save has a readable title", second is not None)
        absent = self.save_details("memory-folder-no-art-details")
        columns = np.flatnonzero(absent["title"].any(axis=0))
        self.check("the racing save displays its full public game title rather than its opaque filename",
                   columns.size > 0 and columns[-1] - columns[0] >= 180)
        self.check("static and absent stored icons have distinct status text",
                   not same_text(static["icon"], absent["icon"]))
        self.press("B")
        self.press("B")
        self.check("B returns from the save folder to its selected folder cube",
                   self.text_until(INFO_BOX, lambda mask: same_text(mask, selected)) is not None)
        self.shot("memory-folder-saves-return", self.last_rgb)

    def memory_folders_again(self) -> None:
        """A new emulator process loads the previous native FAT settings."""
        self.fresh_card = False
        home = self.boot()
        faces = [home]
        for n in range(1, 4):
            faces.append(self.turn(faces, "RIGHT", f"memory-folder-restart-home-{n}"))
        self.press("A")
        self.check("after restart A opens System", self.covered(faces[-1]))
        self.steps("DOWN A")
        self.pause(2.0)
        ref = self.memory_folder_reference
        self.check("after restart the same save-folder cube is selected",
                   self.text_until(INFO_BOX, lambda mask: same_text(mask, ref["selected"])) is not None)
        self.memory_folder_open("memory-folder-after-restart", like=ref["title"])
        self.check("restart preserves the folder's full path and contents",
                   self.settled_label(box=FOLDER_PATH_BOX, like=ref["path"])[0] is not None and
                   self.settled_label(box=FOLDER_CONTENTS_BOX, like=ref["contents"])[0] is not None)
        self.check("restart loads the saved Azure color", self.folder_color(like=ref["azure"]) is not None)
        self.memory_folder_saved(2)
        self.press("Y")
        self.check("Reset still selects Default after restart", self.folder_color(like=ref["default"]) is not None)
        self.memory_folder_return("A", ref["selected"], "memory-folder-reset-after-restart")
        self.memory_folder_saved(None)

    def raw_animation(self) -> None:
        """Capture both colors authored in RAW's animated icon, not cube motion."""
        seen = set()
        deadline = Deadline(self.emulator, 4.0)
        while not deadline.expired() and len(seen) < 2:
            rgb = self.emulator.frame()
            frame = raw_icon_frame(rgb, self.menu_wide)
            if frame is not None and frame not in seen:
                self.shot(f"virtual-cards-icon-frame-{frame}", rgb)
                seen.add(frame)
            self.pause(.05)
        self.check("RAW icon plays both distinct texture frames on its selected cube",
                   seen == {0, 1}, frames=sorted(seen))

    def library_save_stats(self, name: str, shown: bool) -> np.ndarray:
        """From Home's Library face, open the synthetic game's details: SAVES
        shows there only with Saves on Details on and two or more copies."""
        self.press("A")
        title, _ = self.settled_label(box=TITLE_BOX)
        self.check("Library opens the demonstration game", title is not None)
        self.press("A")
        self.check("A opens Library game details", self.covered(title, TITLE_BOX))
        if shown:
            for field, box in (("summary", LIBRARY_SAVES_SUMMARY_BOX),
                               ("updated", LIBRARY_SAVES_UPDATED_BOX)):
                mask, _ = self.settled_label(box=box)
                self.check(f"Library SAVES displays its {field}", mask is not None)
        else:
            detail, _ = self.settled_label(box=DETAIL_TITLE_BOX)
            self.check("Library game details settle", detail is not None)
            self.check("Library game details leave SAVES out",
                       not text_mask(self.gray(), LIBRARY_SAVES_TAG_BOX).any())
        self.shot(name, self.last_rgb)
        self.press("B")
        self.check("B returns from details to the same Library game",
                   self.settled_label(like=title, box=TITLE_BOX)[0] is not None)
        self.press("B")
        home, _ = self.settled_label()
        self.check("B returns to Home Library", home is not None)
        return home

    def virtual_cards(self) -> None:
        """No physical cards: both columns start on SD, one opens a RAW image
        and exports a save through Copy into the other's SD folder."""
        home = self.boot()
        # One save copy: no SAVES on its details, whether Saves on Details is on or off.
        home = self.library_save_stats("virtual-cards-library-before-export", shown=False)
        faces = [home]
        for n in range(1, 4):
            faces.append(self.turn(faces, "RIGHT", f"virtual-cards-home-right-{n}"))
        self.press("A")
        self.check("A opens System", self.covered(faces[-1]))
        self.steps("DOWN A")
        self.pause(2.0)
        left = text_mask(self.gray(), LEFT_HEADER_BOX)
        right = text_mask(self.gray(), RIGHT_HEADER_BOX)
        self.shot("virtual-cards-sd", self.last_rgb)
        self.check("empty physical slots open SD storage in both columns",
                   has_label(left) and has_label(right) and self.cards is None)
        self.press("R")
        self.pause(0.3)
        self.shot("virtual-cards-storage-menu", self.emulator.frame())
        self.press("A")  # keep the right column on its currently selected SD
        self.check("choosing right storage keeps both SD folders",
                   same_text(text_mask(self.gray(), LEFT_HEADER_BOX), left) and
                   same_text(text_mask(self.gray(), RIGHT_HEADER_BOX), right))
        # The source focus must stay on the left after choosing right storage.
        folder, _ = self.settled_label(box=INFO_BOX)
        self.check("the SD column selects its demonstration save folder", folder is not None)
        self.memory_folder_identity(folder)
        self.memory_folder_saves(folder)
        raw_title = self.info("RIGHT", unlike=folder)
        self.check("Right selects the RAW image beside the save folder", raw_title is not None)
        self.memory_folder_open("memory-image-path-and-read-only-status")
        self.memory_folder_return("B", raw_title, "memory-image-cancel")
        self.press("A")
        self.check("A opens the SD card's RAW image", self.differs(left, LEFT_HEADER_BOX))
        self.pause(ART_SECONDS)
        first, _ = self.settled_label(box=INFO_BOX)
        self.check("the RAW card shows a save's comment", first is not None)
        self.shot("virtual-cards-raw", self.last_rgb)
        self.check("opening RAW on the left keeps the right SD folder",
                   same_text(text_mask(self.gray(), RIGHT_HEADER_BOX), right))
        self.raw_animation()
        known = self.save_details("virtual-cards-details-known")
        self.press("B")
        self.check("B closes RAW save details to browsing",
                   self.text_until(INFO_BOX, lambda mask: same_text(mask, first)) is not None)
        second = self.info("RIGHT", unlike=first)
        self.check("RIGHT browses another save in RAW", second is not None)
        unknown = self.save_details("virtual-cards-details-unknown")
        self.check("known and unknown save dates have different text",
                   not same_text(known["updated"], unknown["updated"]))
        self.check("animated and missing save icons have different status text",
                   not same_text(known["icon"], unknown["icon"]))
        self.check("two-block and one-block save sizes have different text",
                   not same_text(known["blocks"], unknown["blocks"]))
        self.press("B")
        again = self.info("LEFT", like=first)
        self.check("LEFT returns to the RAW save to export", again is not None)
        # Open the same image independently on the right. Move and Copy are
        # dimmed (the other image has the save already), details still opens
        # and B always returns.
        self.steps("RIGHT RIGHT RIGHT RIGHT RIGHT")
        self.press("A")
        self.check("right SD independently opens RAW", self.differs(right, RIGHT_HEADER_BOX))
        self.pause(ART_SECONDS)
        both = self.save_details("virtual-cards-details-both-raw")
        self.check("details works with the same RAW image in both columns",
                   same_text(known["updated"], both["updated"]))
        self.press("A")
        self.pause(.3)
        self.shot("virtual-cards-copy-unavailable", self.emulator.frame())
        disabled = text_mask(self.gray(), FOOTER_BOX)
        self.press("A")  # dimmed Copy: the other image has this save
        self.pause(.3)
        self.check("an unavailable RAW Copy keeps its action menu",
                   same_text(text_mask(self.gray(), FOOTER_BOX), disabled) and
                   not message_up(self.last_rgb, self.menu_wide))
        self.press("B")  # action menu -> right RAW browsing
        self.press("B")  # right RAW -> independent writable folder
        self.check("right RAW closes to the independent SD folder",
                   self.text_until(RIGHT_HEADER_BOX, lambda mask: same_text(mask, right)) is not None)
        self.steps("LEFT LEFT LEFT LEFT LEFT")
        self.check("left source stays in its RAW image",
                   self.text_until(INFO_BOX, lambda mask: same_text(mask, first)) is not None)
        self.virtual_popup_checks()
        self.save_details("virtual-cards-details-to-export")
        self.press("A")  # Actions: a RAW image's save starts on Copy
        self.pause(0.3)
        self.shot("virtual-cards-copy-menu", self.emulator.frame())
        self.press("A")
        self.pause(0.3)
        self.shot("virtual-cards-copy-to-sd", self.emulator.frame())
        self.press("A")
        self.check("Copy exports RAW's save to the SD folder", self.message())
        self.shot("virtual-cards-exported", self.last_rgb)
        self.check("the export message closes by itself", self.message_closes())
        self.virtual_card_changes(first)
        self.press("B")
        back = self.text_until(LEFT_HEADER_BOX, lambda mask: same_text(mask, left))
        self.check("B closes RAW to its containing SD folder", back is not None)
        self.check("the other SD column keeps its folder after export",
                   same_text(text_mask(self.gray(), RIGHT_HEADER_BOX), right))
        self.shot("virtual-cards-back-to-sd", self.last_rgb)
        self.press("B")
        self.check("B leaves Memory Cards from the SD root", self.differs(left, LEFT_HEADER_BOX))
        self.press("B")  # System -> Home
        self.check("B returns to Home System", self.settled_label(like=faces[3])[0] is not None)
        for n in (2, 1, 0):
            mask, _ = self.press_until("LEFT", like=faces[n])
            self.check("LEFT returns toward Home Library", mask is not None, to=n)
        # Two copies after the export: SAVES shows, unless Saves on Details is off.
        self.library_save_stats("virtual-cards-library-after-export", shown=self.detail_saves)

    def virtual_card_changes(self, first: np.ndarray) -> None:
        """The image changes as a card does: Erase its other save, Move this
        one out to the SD column, then Move a .gci of it back in."""
        other = self.info("RIGHT", unlike=first)
        self.check("RIGHT comes to the image's other save", other is not None)
        self.save_details("virtual-cards-details-to-erase")
        self.steps("A DOWN A")  # Actions (on Copy), Erase, then its question
        self.shot("virtual-cards-erase-question", self.emulator.frame())
        self.steps("UP")  # No is first: Yes
        self.press("A")
        self.check("Erase takes a save off the RAW image", self.message())
        self.shot("virtual-cards-erased", self.last_rgb)
        self.check("the erase message closes by itself", self.message_closes())
        again = self.info("LEFT", like=first)
        self.check("LEFT comes back to the image's first save", again is not None)
        self.save_details("virtual-cards-details-to-move-out")
        self.steps("A UP A")  # Actions (on Copy), Move, then where to
        self.shot("virtual-cards-move-to", self.emulator.frame())
        self.press("A")  # the folder the other stack has open
        self.check("Move takes the RAW image's save to the SD folder", self.message())
        self.check("the move-out message closes by itself", self.message_closes())
        self.shot("virtual-cards-moved-out", self.last_rgb)
        # The image is empty; its save and the earlier export are in the SD
        # column, by the same name: either goes back.
        mask, _ = self.press_until("RIGHT", like=first, box=INFO_BOX)
        for _ in range(6):
            if mask is not None:
                break
            mask, _ = self.press_until("RIGHT", like=first, box=INFO_BOX)
        self.check("RIGHT reaches a .gci of the save in the SD column", mask is not None)
        self.pause(ART_SECONDS)
        self.save_details("virtual-cards-details-to-move-in")
        self.steps("A A")  # Actions start on Move for a file; Move
        self.shot("virtual-cards-move-in-question", self.emulator.frame())
        self.press("A")  # Yes: "Move to the card image?"
        self.check("Move puts the .gci into the RAW image", self.message())
        self.check("the move-in message closes by itself", self.message_closes())
        self.shot("virtual-cards-moved-in", self.last_rgb)
        back = self.info("LEFT", like=first)
        for _ in range(6):
            if back is not None:
                break
            back = self.info("LEFT", like=first)
        self.check("the moved save shows in the RAW image", back is not None)

    def virtual_popup_checks(self) -> None:
        """The private emulator image after dialogs, before authorizing Copy."""
        if self.sd_image is None:
            raise Broken("virtual-card proof needs its emulator SD image")
        folder = make_test_saves.SAVE_FOLDER
        original, _ = make_test_saves.virtual_card()
        raw = card.read_card(self.sd_image, f"{folder}/{make_test_saves.RAW_CARD_NAME}")
        exported = card.read_card(self.sd_image, f"{folder}/{make_test_saves.RAW_EXPORT_NAME}")
        self.check("details, B Back and unusable RAW actions leave the source image unchanged",
                   raw == original)
        self.check("details and unusable actions create no exported GCI", exported is None)

    def virtual_card_checks(self, image: Path) -> None:
        """The actual FAT bytes, after navigating and exporting in Indigo."""
        original, gcis = make_test_saves.virtual_card()
        folder = make_test_saves.SAVE_FOLDER
        for name, data in make_test_saves.memory_folder_files().items():
            unchanged = card.read_card(image, f"{folder}/{make_test_saves.MEMORY_FOLDER_NAME}/{name}")
            self.check("folder identity, colors and native save browsing preserve the save bytes",
                       unchanged == data, file=name, bytes=len(data))
        raw = card.read_card(image, f"{folder}/{make_test_saves.RAW_CARD_NAME}")
        numbered = make_test_saves.RAW_EXPORT_NAME.replace(".gci", "_2.gci")
        exports = {name: card.read_card(image, f"{folder}/{name}")
                   for name in (make_test_saves.RAW_EXPORT_NAME, numbered)}
        left = [data for data in exports.values() if data is not None]
        # The export and the Move out made two .gci files; one went back in.
        self.check("one exported GCI is left in the SD folder, with the save's exact payload",
                   len(left) == 1 and same_save(gcis[0], left[0]),
                   files=sorted(name for name, data in exports.items() if data is not None))
        if left:
            (self.out / "virtual-card-export.gci").write_bytes(left[0])
        try:
            saves = card_image_saves(raw) if raw is not None else None
        except ValueError as error:
            saves = None
            self.check("the RAW image reads as a card after Erase and both Moves", False,
                       error=str(error))
        if saves is not None:
            back = saves.get(gcis[0][:6] + gcis[0][8:40])
            self.check("the RAW image reads as a card after Erase and both Moves", True)
            self.check("the erased save is gone and the moved save is back, whole",
                       len(saves) == 1 and back is not None and same_save(gcis[0], back) and
                       back[0x38:0x40] == gcis[0][0x38:0x40] and back[8:0x36] == gcis[0][8:0x36],
                       saves=len(saves))
            # Only the system blocks and the save's new blocks were written.
            changed = [n for n in range(len(original) // 8192)
                       if raw[n * 8192:(n + 1) * 8192] != original[n * 8192:(n + 1) * 8192]]
            self.check("the image changed only in its system blocks and the save's new blocks",
                       len(raw) == len(original) and set(changed) <= {1, 2, 3, 4, 10, 11},
                       changed=changed)
            (self.out / "virtual-card-after.raw").write_bytes(raw)

    def manual(self) -> None:
        """A game's manual, card.MANUAL_GAME's: boot, open the Library and the
        details of a game without one, which have no Manual row, then of the
        game with one, where the row sits at the top with its page count. UP
        to it and A opens the manual at full width from page 1's top. DOWN
        scrolls the page up out of sight and UP back; RIGHT turns to page 2,
        read ahead while page 1 showed; Y shows the whole page, Y again full
        width; R the last page, read then; L the first; B goes back to the
        details. Then a game whose manual is cut short: its row is there, A
        says the manual can't be read, and A leaves Detail as it was."""
        home = self.boot()
        self.press("A")
        self.check("A opens the Library", self.covered(home))
        order = card.library_order(self.probe)
        names = dict(card.GAMES)
        at, title = 0, self.settled_label(box=TITLE_BOX)[0]
        self.check("the Library shows a game's title", title is not None)

        def go(game_id: str) -> np.ndarray:
            nonlocal at, title
            while at < order.index(names[game_id]):
                title, _ = self.press_until("RIGHT", unlike=title, box=TITLE_BOX)
                self.check("RIGHT moves to the next game", title is not None, to=order[at + 1])
                at += 1
            self.press("A")
            self.check("A opens the game's details", self.covered(title, TITLE_BOX), game=game_id)
            details, _ = self.settled_label(box=DETAIL_TITLE_BOX)
            self.check("the details show the game's title", details is not None, game=game_id)
            return details

        def back(details: np.ndarray) -> None:
            self.press("B")
            again, _ = self.settled_label(like=title, box=TITLE_BOX)
            self.check("B comes back to the Library, on the same game", again is not None)

        def row_shown() -> bool:
            return int(text_mask(self.gray(), MANUAL_ROW_BOX).sum()) >= MANUAL_ROW_PIXELS

        def page_shown(whole: bool = False, seconds: float = SETTLE_SECONDS) -> int | None:
            deadline = Deadline(self.emulator, seconds)
            while not deadline.expired():
                self.last_rgb = self.emulator.frame()
                shown = manual_page(self.last_rgb, self.menu_wide, whole)
                if shown is not None:
                    return shown
                time.sleep(0.15)
            return None

        def turn(button: str, page: int, name: str) -> int | None:
            """The press, and how many frames (60 a second) until the page is there."""
            frames = self.emulator.burst(3.0, lambda: self.press(button))
            arrived = next((n for n in range(len(frames))
                            if manual_page(np.array(frames[n]), self.menu_wide) == page), None)
            del frames
            shown = page_shown()
            self.shot(name, self.last_rgb)
            self.check(f"{button} shows page {page + 1}", shown == page, shown=shown, frames=arrived)
            return arrived

        plain = go("GACZ01")
        self.shot("manual-none", self.last_rgb)
        self.check("a game without a manual has no Manual row", not row_shown())
        back(plain)

        details = go(card.MANUAL_GAME)
        self.shot("manual-row", self.last_rgb)
        self.check("a game with a manual has a Manual row, with its page count", row_shown())
        for _ in range(3):  # Settings, then Manual (no Cheats row), then the top
            self.press("UP")
            self.pause(0.3)
        self.gray()
        self.shot("manual-row-focused", self.last_rgb)
        self.press("A")
        shown = page_shown()
        self.shot("manual-page-1", self.last_rgb)
        self.check("A on the Manual row opens the manual at full width, on page 1's top", shown == 0,
                   shown=shown)
        self.pad.hold("DOWN")
        self.pause(MANUAL_SCROLL_SECONDS)
        self.pad.hold()
        self.pause(0.3)
        self.last_rgb = self.emulator.frame()
        self.shot("manual-scrolled", self.last_rgb)
        self.check("DOWN scrolls the page up", manual_page(self.last_rgb, self.menu_wide) is None)
        self.pad.hold("UP")
        self.pause(MANUAL_SCROLL_SECONDS * 1.5)
        self.pad.hold()
        self.check("UP scrolls back to the page's top", page_shown() == 0)
        ahead = turn("RIGHT", 1, "manual-page-2")
        self.press("Y")
        shown = page_shown(whole=True)
        self.shot("manual-whole-page", self.last_rgb)
        self.check("Y shows the whole page", shown == 1, shown=shown)
        self.press("Y")
        self.check("Y again shows it at full width", page_shown() == 1)
        cold = turn("R", len(card.MANUAL_BANDS) - 1, "manual-last-page")
        turn("L", 0, "manual-first-page")
        self.checks.append({"check": "page turns: frames until the page shows", "passed": True,
                            "read_ahead": ahead, "read_then": cold})
        self.press("B")
        again, _ = self.settled_label(like=details, box=DETAIL_TITLE_BOX)
        self.shot("manual-closed", self.last_rgb)
        self.check("B closes the manual, back on the game's details", again is not None)
        back(details)

        details = go(card.BROKEN_MANUAL_GAME)
        self.shot("manual-broken-row", self.last_rgb)
        self.check("a game whose manual is damaged still has its Manual row", row_shown())
        for _ in range(3):
            self.press("UP")
            self.pause(0.3)
        lines = text_mask(self.gray(), MESSAGE_LINES_BOX)
        self.press("A")
        said = self.covered(lines, MESSAGE_LINES_BOX)
        self.shot("manual-broken-message", self.last_rgb)
        self.check("A on it says the manual can't be read", said and manual_page(self.last_rgb) is None)
        self.press("A")
        deadline, closed = Deadline(self.emulator, SETTLE_SECONDS), False
        while not closed and not deadline.expired():
            time.sleep(0.3)
            closed = same_text(text_mask(self.gray(), MESSAGE_LINES_BOX), lines)
        self.shot("manual-broken-back", self.last_rgb)
        self.check("A closes the message, and Detail is as it was", closed)

    def tour(self) -> None:
        self.smoke()


def sheet(shots: list[tuple[str, Path]], path: Path) -> None:
    if not shots:
        return
    columns, width, height = 4, 320, 240
    rows = (len(shots) + columns - 1) // columns
    canvas = Image.new("RGB", (columns * width, rows * (height + 18)), "black")
    draw = ImageDraw.Draw(canvas)
    for index, (name, picture) in enumerate(shots):
        x, y = index % columns * width, index // columns * (height + 18)
        canvas.paste(Image.open(picture).convert("RGB").resize((width, height)), (x, y + 18))
        draw.text((x + 4, y + 3), f"{index + 1}. {name}", fill=(255, 220, 90))
    canvas.save(path)


def fatal_lines(log: Path) -> list[str]:
    text = log.read_text(errors="replace") if log.exists() else ""
    return [line.strip() for line in text.splitlines() if FATAL.search(line)][:20]


def poster_stack(log: Path) -> dict[str, int] | None:
    """The most of its stack the poster thread used in a run, or None when
    it never ran."""
    text = log.read_text(errors="replace") if log.exists() else ""
    used = [(int(m.group(1)), int(m.group(2))) for m in POSTER_STACK.finditer(text)]
    return {"most": max(u for u, _ in used), "of": used[0][1], "stops": len(used)} if used else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dol", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--route", choices=("smoke", "tour", "game", "save", "virtual-cards", "folders", "files",
                                            "manual"),
                        default="smoke")
    parser.add_argument("--probe", type=Path, help="the probe DOL (probe/), launched as an app and a game")
    parser.add_argument("--region", choices=tuple(REGIONS), default="pal")
    parser.add_argument("--storage", choices=tuple(STORAGES), default="dvd")
    parser.add_argument("--cable", choices=tuple(CABLES), default="composite")
    parser.add_argument("--card-zip", type=Path, help="the release zip, unpacked onto the SD card")
    parser.add_argument("--settings", help="start the card with settings/<name>.ini as its global.ini")
    parser.add_argument("--sd-faults", help="the SD card's faults (DOLPHIN_SD_FAULTS), such as write-error-after=4")
    parser.add_argument("--fragments", type=int, default=0,
                        help=f"the probe's game in that many pieces on the card; over {card.MAX_FRAGMENTS} "
                             "its launch must be refused")
    parser.add_argument("--disc", type=Path, help="a disc to use instead of building card.py's")
    args = parser.parse_args(argv)
    if args.route == "game" and not args.probe:
        parser.error("the game route launches the probe: give --probe")
    if (args.storage != "dvd") != bool(args.card_zip):
        parser.error("an SD card (--storage) is made from the release zip (--card-zip), and only then")
    if args.settings and not args.card_zip:
        parser.error("settings to start with go on the SD card (--storage)")
    if args.route == "save" and not args.settings:
        parser.error("the save route changes settings a card starts with: give --settings")
    if args.fragments and not (args.card_zip and args.probe and args.route == "game"):
        parser.error("--fragments splits the probe's game on the SD card for the game route")
    if args.route == "virtual-cards" and args.storage not in ("gcloader", "sd2sp2"):
        parser.error("virtual-cards needs an SD card in GC Loader or SD2SP2, with both slots empty")
    if args.route == "manual" and args.storage == "dvd":
        parser.error("manual reads its pages on a thread, as from an SD card: give --storage")
    if args.route == "files" and (args.storage != "gcloader" or args.settings):
        parser.error("files needs a fresh card in a GC Loader (the route puts a second SD card in SD2SP2)")
    if args.route == "folders" and (args.storage != "sd2sp2" or args.region != "ntsc" or
                                     args.cable != "component" or args.settings):
        parser.error("folders needs a fresh SD2SP2 card, NTSC and component video")
    start = (SETTINGS / f"{args.settings}.ini").read_text() if args.settings else None
    args.out.mkdir(parents=True, exist_ok=True)
    report: dict[str, object] = {"schema": "indigo.emulator-test.v1", "route": args.route,
                                 "region": args.region, "storage": args.storage, "cable": args.cable,
                                 "settings": args.settings, "sd_faults": args.sd_faults,
                                 "fragments": args.fragments,
                                 "dol": str(args.dol),
                                 "dolphin": _version()}
    status, emulator, route = 0, None, None
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        try:
            dol, disc, sd = args.dol.resolve(), args.disc, None
            if args.card_zip:
                sd = work / "card.img"
                report["card"] = card.build_card(sd, args.card_zip, probe=args.probe,
                                                 foreign=FOREIGN[args.region], settings=start,
                                                 boot_iso=args.storage == "gcloader", fragments=args.fragments,
                                                 virtual_cards=args.route == "virtual-cards")
                if args.storage == "gcloader":  # the drive's disc until a game's is set
                    disc = work / "boot.iso"
                    disc.write_bytes(card.read_card(sd, "boot.iso"))
                dol = work / "ipl.dol"  # what a loader starts: the zip's own
                with zipfile.ZipFile(args.card_zip) as package:
                    dol.write_bytes(package.read("ipl.dol"))
                report["dol"] = f"{args.card_zip.name}:ipl.dol"
            elif disc is None:
                disc = work / "demo.iso"
                report["disc"] = card.build(disc, probe=args.probe, foreign=FOREIGN[args.region])
            # The smoke route opens Memory Cards with a memory card in each
            # slot, unless an SD Gecko has Slot B.
            cards = None
            if args.route in ("smoke", "tour") and args.storage != "sdgecko-b":
                cards = work / "cards"
                make_test_saves.write(str(cards))
            second = None
            if args.route == "files":
                second = work / "second.img"
                report["second_card"] = card.second_card(second)
            emulator = Emulator(dol, disc.resolve() if disc else None, work, args.out, args.region,
                                args.storage, sd, args.cable, args.sd_faults, cards,
                                empty_slots=args.route in ("virtual-cards", "files"),
                                folder_frames=args.route == "folders",
                                second=second, exact_textures=args.route == "manual")
            route = Route(emulator, args.out, probe=bool(args.probe), fresh_card=bool(sd) and start is None,
                          cable=args.cable, region=args.region, fragments=args.fragments, cards=cards,
                          storage=args.storage, menu_wide=bool(start and "Menu Widescreen=Yes" in start),
                          sd_image=sd, second_sd=second,
                          detail_saves=not start or seeded(start).get("Hide Saves on Details") != "Yes",
                          home_controls=not start or seeded(start).get("Hide On-screen Controls") != "Yes",
                          apps_video=seeded(start).get("Apps Video Mode", "Auto") if start else "Auto")
            getattr(route, args.route.replace("-", "_"))()
            if args.route == "files":
                # Again, with the SD card in SD2SP2 failing writes where a new
                # file's data would go: a copy that fails partway.
                emulator.close()
                (work / "again").mkdir()
                (args.out / "next-boot").mkdir(exist_ok=True)
                faults = ",".join(f"write-error={a}-{b}" for a, b in card.free_sectors(second))
                emulator = Emulator(dol, disc.resolve() if disc else None, work / "again",
                                    args.out / "next-boot", args.region, args.storage, sd,
                                    args.cable, faults, second=second)
                route.emulator, route.pad, route.fresh_card = emulator, emulator.pad, False
                route.files_write_fails()
            if args.route == "save":
                # Power off and on again, with a card that works.
                emulator.close()
                (work / "again").mkdir()
                (args.out / "next-boot").mkdir(exist_ok=True)  # Dolphin's output of the second boot
                emulator = Emulator(dol, disc.resolve() if disc else None, work / "again",
                                    args.out / "next-boot", args.region, args.storage, sd, args.cable)
                route.emulator, route.pad = emulator, emulator.pad
                route.boot_again()
            elif sd:
                if args.route == "virtual-cards":
                    # Real power cycle: a fresh Dolphin user/process, the same FAT image.
                    emulator.close()
                    (work / "again").mkdir()
                    (args.out / "next-boot").mkdir(exist_ok=True)
                    emulator = Emulator(dol, disc.resolve() if disc else None, work / "again",
                                        args.out / "next-boot", args.region, args.storage, sd,
                                        args.cable, empty_slots=True)
                    route.emulator, route.pad = emulator, emulator.pad
                    route.memory_folders_again()
                    route.virtual_card_checks(sd)
                route.card_checks(sd, args.route, seeded(start) if start else None)
        except Failed as failure:
            status = 1
            report["failure"] = str(failure)
        except (Broken, OSError, subprocess.SubprocessError) as error:
            status = 2
            report["error"] = f"{type(error).__name__}: {error}"
        finally:
            if emulator:
                try:
                    emulator.close()
                except Broken as error:
                    status = 2
                    report["error"] = f"native frame proof: {error}"
    fatal = fatal_lines(args.out / "dolphin.log")
    fatal += fatal_lines(args.out / "next-boot/dolphin.log")
    if fatal and status == 0:
        status = 1
        report["failure"] = "Dolphin reported a crash or an invalid access"
    stack = poster_stack(args.out / "dolphin.log")
    if stack and stack["most"] > POSTER_STACK_SHARE * stack["of"] and status == 0:
        status = 1
        report["failure"] = (f"the poster thread used {stack['most']} of its {stack['of']} bytes of "
                             f"stack, more than {POSTER_STACK_SHARE:.0%}")
    report["poster_stack"] = stack
    if args.route == "manual":
        memory = manual_memory(args.out / "dolphin.log")
        report["manual_memory"] = memory
        if status == 0 and (memory is None or memory["returned"] != 0):
            status = 1
            report["failure"] = ("Indigo did not report the manual's memory" if memory is None else
                                 f"closing the manual left {-memory['returned']} bytes held")
    report.update({
        "passed": status == 0,
        "checks": route.checks if route else [],
        "fatal_log_lines": fatal,
        "probe": route.report if route else None,
        "pressed_again": route.pressed_again if route else [],
        "pictures": [path.name for _, path in (route.shots if route else [])],
        "folder_transitions": getattr(route, "folder_transitions", []) if route else [],
    })
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    sheet(route.shots if route else [], args.out / "sheet.png")
    (args.out / "summary.md").write_text(summary(report))
    print(f"emulator test {'passed' if status == 0 else 'FAILED'}: {args.out / 'report.json'}")
    return status


def summary(report: dict[str, object]) -> str:
    where = ("the demonstration disc" if report["storage"] == "dvd" else
             f"an SD card in {report['storage'].upper()} made from the release zip")
    lines = [f"### Emulator ({report['route']}, {report['region'].upper()}"
             f"{'' if report['storage'] == 'dvd' else ', ' + report['storage'].upper()}): "
             f"{'passed' if report['passed'] else 'failed'}", "",
             f"{report['dolphin']}, {where}, a controller plugged in once Home is up.", "",
             "| Check | Result | Detail |", "| --- | --- | --- |"]
    for check in report["checks"]:
        detail = ", ".join(f"{k} {v}" for k, v in check.items() if k not in ("check", "passed"))
        lines.append(f"| {check['check']} | {'✅' if check['passed'] else '❌'} | {detail} |")
    for key in ("failure", "error"):
        if key in report:
            lines.append(f"\n**{key.capitalize()}:** {report[key]}")
    if report["fatal_log_lines"]:
        lines.append("\n**Dolphin reported:**\n```\n" + "\n".join(report["fatal_log_lines"]) + "\n```")
    if report.get("pressed_again"):
        lines.append(f"\n**Pressed again:** {len(report['pressed_again'])} press(es) the menu was too busy "
                     f"to see ({', '.join(report['pressed_again'])}): the screen had not changed, so the "
                     f"route pressed again, as a person would.")
    probe = report.get("probe")
    if probe:
        lines.append(f"\n**The probe reported:** disc `{probe['disc_id']}`, memory `{probe['memsize']:08X}`, "
                     f"the screen {video_mode(probe)}, audio DMA `{probe['ai_dma']:04X}`, "
                     f"{probe['stray_writes']} stray writes, started as `{probe['path'] or '(no path)'}`.")
    lines.append("\nEvery checkpoint is in the `sheet.png` of this run's `emulator` artifact.")
    return "\n".join(lines) + "\n"


def _version() -> str:
    try:
        return subprocess.run(["dolphin-emu-nogui", "--version"], capture_output=True, text=True,
                              timeout=20).stdout.strip() or "Dolphin (unknown version)"
    except (OSError, subprocess.SubprocessError):
        return "Dolphin (not found)"


if __name__ == "__main__":
    sys.exit(main())
