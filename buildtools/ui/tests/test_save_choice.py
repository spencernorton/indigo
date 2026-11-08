#!/usr/bin/env python3
"""Game Detail's save choice, run from swiss.c itself: which copy Left and
Right step to, when there is a choice at all, and what Launch does with it.

gameflowSaveSlot, gameflowSaveChoosable, gameflowSaveWhere and
gameflowLoadChosenSave are extracted unchanged, with the Left/Right step from
the Detail controller; the boxes answer as told and Saves_LoadCopy only
counts. A Detail with no card, or with Emulate Memory Card on, must never be
left unable to launch (2.4 vetting, S1)."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"
SWISS = (ROOT / "cube/swiss/source/swiss.c").read_text()

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "saves_stats.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef int uiDrawObj_t;
struct DEVICEHANDLER_STRUCT { const char *deviceName; };
typedef struct DEVICEHANDLER_STRUCT DEVICEHANDLER_INTERFACE;
#define DEVICE_CUR 0
#define DEVICE_CONFIG 1
/* Settings on a GC Loader, the games (the Source) on an SD2SP2. */
static DEVICEHANDLER_INTERFACE settingsCard = {"GC Loader"}, sourceCard = {"SD Card - SD2SP2"};
static DEVICEHANDLER_INTERFACE *devices[2] = {&sourceCard, &settingsCard};
static const char *DeviceDisplayName(const DEVICEHANDLER_INTERFACE *device) { return device->deviceName; }
static struct { int emulateMemoryCard; } swissSettings;
static unsigned gameflowSaveEmulators;
typedef struct {
	bool savesScanned; uiSavesGameStats_t saveStats; int saveSlot; int saveChoice;
	bool savesWaiting; char gameId[7];
} gameflowLaunchContext_t;
static savesCopies_t gameflowSaveCopies;
static bool answer = true;
static int asks, loads;
static char asked[512];
/* gameflowSaveAsk follows the memory card slots while its box is up and
 * sets this when one changed; changeDuringAsk plays that for the question. */
static bool gameflowSlotsChanged, changeDuringAsk;
static bool gameflowSaveAsk(const char *text)
{
	snprintf(asked, sizeof(asked), "%s", text); ++asks;
	if(changeDuringAsk && strstr(text, "Start with")) gameflowSlotsChanged = true;
	return answer;
}
static uiDrawObj_t *DrawProgressBar(bool a, int b, const char *c) { (void)a; (void)b; (void)c; return NULL; }
static uiDrawObj_t *DrawPublish(uiDrawObj_t *o) { return o; }
static void DrawDispose(uiDrawObj_t *o) { (void)o; }
static char *getRelativeName(char *p) { char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static const savesCopy_t *loaded;
static bool Saves_LoadCopy(int slot, const savesCopy_t *copy, char *why, size_t n)
{
	(void)slot; (void)why; (void)n; ++loads; loaded = copy; return true;
}
/* A memory card emulator's switch (card_slots.c): whether the slot may be
 * written, before the question and once it has been answered. */
static bool writable, writableAfter;
static bool CardSlots_WritableFor(int slot, const char *id)
{
	(void)slot; (void)id; return asks == 0 ? writable : writableAfter;
}
static bool pollChanged;
static bool CardSlots_Poll(void) { return pollChanged; }
'''

MAIN = r'''
static void press(gameflowLaunchContext_t *context, bool right)
{
	u32 buttons = right ? 2u : 1u;
	enum { BUTTON_LEFT = 1u, BUTTON_RIGHT = 2u };
	STEP
}
/* Copies: one on the card in slot when slot >= 0, then two in the Save Folder. */
static void detail(gameflowLaunchContext_t *context, int emulate, bool card, int slot)
{
	memset(&gameflowSaveCopies, 0, sizeof(gameflowSaveCopies));
	if(slot >= 0) gameflowSaveCopies.copy[gameflowSaveCopies.count++].source = (savesCopySource_t)slot;
	gameflowSaveCopies.copy[gameflowSaveCopies.count].source = SAVES_COPY_FILE;
	gameflowSaveCopies.copy[gameflowSaveCopies.count].device = &settingsCard;
	strcpy(gameflowSaveCopies.copy[gameflowSaveCopies.count++].path, "gcldr:/swiss/saves/a.gci");
	gameflowSaveCopies.copy[gameflowSaveCopies.count].source = SAVES_COPY_IMAGE;
	gameflowSaveCopies.copy[gameflowSaveCopies.count].device = &settingsCard;
	strcpy(gameflowSaveCopies.copy[gameflowSaveCopies.count++].path, "gcldr:/swiss/saves/b.raw");
	gameflowSaveCopies.cards[0] = card;
	swissSettings.emulateMemoryCard = emulate;
	gameflowSaveEmulators = 0u;
	memset(context, 0, sizeof(*context));
	context->savesScanned = true;
	context->saveSlot = gameflowSaveSlot(&gameflowSaveCopies);
	context->saveChoice = -1;	/* the totals first, as gameflowPublishDetail opens */
	asks = loads = 0;
	answer = true;
	writable = writableAfter = true;
	changeDuringAsk = pollChanged = false;
}
int main(void)
{
	gameflowLaunchContext_t c;
	int i;

	/* No card: no choice. Left and Right change nothing, Launch starts the
	 * game without asking (it used to refuse, every time). */
	detail(&c, 0, false, -1);
	assert(c.saveSlot < 0 && !gameflowSaveChoosable(&c));
	press(&c, true); press(&c, false);
	assert(c.saveChoice == -1);
	assert(gameflowLoadChosenSave(&c) && asks == 0 && loads == 0);

	/* Emulate Memory Card on, a card in Slot A holding a copy: the game reads
	 * its card image, so no choice either. */
	detail(&c, 1, true, SAVES_COPY_SLOT_A);
	assert(c.saveSlot == 0 && !gameflowSaveChoosable(&c));
	press(&c, true);
	assert(c.saveChoice == -1 && gameflowLoadChosenSave(&c) && asks == 0);

	/* A memory card emulator in Slot A (its details found it changing cards):
	 * only read, as the MemCard PRO build tested on a real card did. No
	 * choice, and Launch leaves its card as it is. One in Slot B leaves Slot
	 * A's card its choice. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	gameflowSaveEmulators = 1u;
	assert(c.saveSlot == 0 && !gameflowSaveChoosable(&c));
	press(&c, true); assert(c.saveChoice == -1);
	c.saveChoice = 1;
	assert(gameflowLoadChosenSave(&c) && asks == 0 && loads == 0);
	gameflowSaveEmulators = 2u;
	assert(gameflowSaveChoosable(&c));

	/* A card in Slot A holding a copy: each copy, then the totals again. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	assert(gameflowSaveChoosable(&c));
	press(&c, true); assert(c.saveChoice == 0);
	press(&c, true); assert(c.saveChoice == 1);
	press(&c, true); assert(c.saveChoice == 2);
	press(&c, true); assert(c.saveChoice == -1);
	press(&c, false); assert(c.saveChoice == 2);
	press(&c, false); press(&c, false); press(&c, false); assert(c.saveChoice == -1);
	/* The card's own copy launches as it is. */
	c.saveChoice = 0;
	assert(gameflowLoadChosenSave(&c) && asks == 0 && loads == 0);
	/* Another copy asks first; B keeps the card's copy and goes back to the
	 * totals, so the next Launch starts the game as it is. */
	c.saveChoice = 1; answer = false;
	assert(!gameflowLoadChosenSave(&c) && asks == 1 && loads == 0 && c.saveChoice == -1);
	assert(gameflowLoadChosenSave(&c) && asks == 1 && loads == 0);
	/* A puts it on the card, its own copy kept first, as the question says. */
	c.saveChoice = 2; answer = true;
	assert(gameflowLoadChosenSave(&c) && asks == 2 && loads == 1);
	assert(strstr(asked, "Slot A's own copy of it goes to the Save Folder first."));

	/* Copies in the Source's swiss/saves: a .gci there goes by the Source's
	 * name, not the Save Folder's, and a card image by its own, as anywhere.
	 * The one chosen is the one loaded; the card's own copy still goes to
	 * the Save Folder. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	assert(!strcmp(gameflowSaveWhere(&gameflowSaveCopies.copy[1]), "Save Folder"));
	assert(!strcmp(gameflowSaveWhere(&gameflowSaveCopies.copy[2]), "b.raw"));
	gameflowSaveCopies.copy[1].device = gameflowSaveCopies.copy[2].device = &sourceCard;
	strcpy(gameflowSaveCopies.copy[1].path, "sdc:/swiss/saves/a.gci");
	strcpy(gameflowSaveCopies.copy[2].path, "sdc:/swiss/saves/MemoryCardA.USA.raw");
	assert(!strcmp(gameflowSaveWhere(&gameflowSaveCopies.copy[1]), "SD Card - SD2SP2"));
	assert(!strcmp(gameflowSaveWhere(&gameflowSaveCopies.copy[2]), "MemoryCardA.USA.raw"));
	c.saveChoice = 1;
	assert(gameflowLoadChosenSave(&c) && asks == 1 && loads == 1 &&
		loaded == &gameflowSaveCopies.copy[1]);
	assert(strstr(asked, "Start with the save from SD Card - SD2SP2?\n"
		"Slot A's own copy of it goes to the Save Folder first."));

	/* Different files of the game: Slot A holds one (Town), the Save Folder
	 * another (Letters). The card has no copy of that file to keep, so none
	 * goes to the Save Folder, and the question names the file. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	for(i = 0; i < 3; i++) memcpy(gameflowSaveCopies.copy[i].entry, "GACZ01", 6);
	strcpy((char *)gameflowSaveCopies.copy[0].entry + 8, "Town");
	strcpy((char *)gameflowSaveCopies.copy[1].entry + 8, "Letters");
	strcpy((char *)gameflowSaveCopies.copy[2].entry + 8, "Town");
	c.saveChoice = 1;
	assert(gameflowLoadChosenSave(&c) && asks == 1 && loads == 1);
	assert(strstr(asked, "Start with Letters from Save Folder?\n"
		"It goes on the memory card in Slot A."));
	/* The card's own file, in an image: its own copy of it goes first. */
	c.saveChoice = 2;
	assert(gameflowLoadChosenSave(&c) && asks == 2 && loads == 2);
	assert(strstr(asked, "Start with Town from b.raw?\n"
		"Slot A's own copy of it goes to the Save Folder first."));

	/* A card without the game's save: a copy goes onto it, and a choice can
	 * always be undone (the totals are one of the steps). */
	detail(&c, 0, true, -1);
	assert(c.saveSlot == 0 && gameflowSaveChoosable(&c));
	for(i = 0; i < 3; i++) press(&c, true);
	assert(c.saveChoice == -1);
	assert(gameflowLoadChosenSave(&c) && asks == 0 && loads == 0);
	/* A copy chosen there: the question promises no own copy to keep. */
	c.saveChoice = 0;
	assert(gameflowLoadChosenSave(&c) && asks == 1 && loads == 1);
	assert(!strstr(asked, "own copy") && strstr(asked, "It goes on the memory card in Slot A."));

	/* A MemCard PRO still changing to this game's card: Left and Right wait,
	 * and nothing goes on the card it is changing from, before the question
	 * or after it. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	c.savesWaiting = true;
	press(&c, true); assert(c.saveChoice == -1);
	c.savesWaiting = false;
	c.saveChoice = 1; writable = false;
	assert(!gameflowLoadChosenSave(&c) && asks == 1 && loads == 0);
	assert(strstr(asked, "still changing"));
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	c.saveChoice = 1; writableAfter = false;
	assert(!gameflowLoadChosenSave(&c) && asks == 2 && loads == 0);
	assert(strstr(asked, "still changing"));

	/* A card that changed while the question was up, seen then or by the
	 * poll right after it: nothing goes on, and Detail reads the cards again
	 * before another launch. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	c.saveChoice = 1; changeDuringAsk = true;
	assert(!gameflowLoadChosenSave(&c) && loads == 0 && !c.savesScanned);
	assert(strstr(asked, "changed while the question was up"));
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	c.saveChoice = 1; pollChanged = true;
	assert(!gameflowLoadChosenSave(&c) && loads == 0 && !c.savesScanned);

	/* Fewer than two copies: no choice. */
	detail(&c, 0, true, SAVES_COPY_SLOT_A);
	gameflowSaveCopies.count = 1u;
	assert(!gameflowSaveChoosable(&c));
	press(&c, true); assert(c.saveChoice == -1);

	puts("save choice: none without a card, with Emulate Memory Card on or on a memory card emulator, each copy then the totals, B keeps the card's, the question as the card is and by file, a copy on the Source by its name, nothing while a card changes or after one changed during the question PASS");
	return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    controller = SWISS[SWISS.index("\t\t\t/* Left and Right choose the save copy to start with, while there\n"):]
    step = controller[controller.index("\t\t\t\tif(gameflowSaveChoosable(context) && !context->savesWaiting) {"):]
    step = step[:step.index("\t\t\t\t\tgameflowPublishDetail(config, context);")]
    step += "\t\t\t\t}\n"
    # The controller's own lines, with the redraw and blip left out.
    assert step.count("context->saveChoice = (context->saveChoice + 1 + step + span) %") == 1
    source = HARNESS + "\n".join(extract_function(SWISS, marker) for marker in (
        "static int gameflowSaveSlot(const savesCopies_t *copies)",
        "static bool gameflowSaveChoosable(const gameflowLaunchContext_t *context)",
        "static const char *gameflowSaveWhere(const savesCopy_t *copy)",
        "static bool gameflowSaveFiles(void)",
        "static bool gameflowLoadChosenSave(gameflowLaunchContext_t *context)",
    )) + MAIN.replace("STEP", step)
    with tempfile.TemporaryDirectory(prefix="save-choice-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(source)
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200112L",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-g", "-I", str(GUI),
                   str(path / "test.c"), str(GUI / "ui_saves_metadata.c"), str(GUI / "ui_saves.c"), str(GUI / "ui_lang.c"),
                   "-o", str(path / "test")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
