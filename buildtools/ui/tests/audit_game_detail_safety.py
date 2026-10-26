#!/usr/bin/env python3
"""Mechanical guardrails for Game Detail controller and read-only seams."""

from pathlib import Path
import re


if not __debug__:
    raise SystemExit(
        "audit_game_detail_safety.py requires Python assertions; rerun without -O/PYTHONOPTIMIZE"
    )


ROOT = Path(__file__).resolve().parents[3]


def extract_function(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {marker}")


cheats_source = (ROOT / "cube/swiss/source/cheats/cheats.c").read_text()
swiss_source = (ROOT / "cube/swiss/source/swiss.c").read_text()
framebuffer_source = (
    ROOT / "cube/swiss/source/gui/FrameBufferMagic.c"
).read_text()

probe = extract_function(cheats_source, "static bool probeCheatsFile")
device_probe = extract_function(
    cheats_source, "static bool probeCheatsOnDevice"
)
internal = extract_function(cheats_source, "static int findCheatsInternal")
legacy = extract_function(cheats_source, "int findCheats(bool silent)")
read_only = extract_function(cheats_source, "int findCheatsReadOnly(void)")
selection_internal = extract_function(
    cheats_source, "static bool loadCheatsSelectionInternal"
)
selection_legacy = extract_function(
    cheats_source, "bool loadCheatsSelection(void)"
)
selection_read_only = extract_function(
    cheats_source, "bool loadCheatsSelectionReadOnly(void)"
)
selection_file = extract_function(
    cheats_source, "static CheatSelectionLoadResult loadCheatSelectionFile"
)

assert internal.count("ensure_path(") == 2
cursor = 0
while True:
    cursor = internal.find("ensure_path(", cursor)
    if cursor < 0:
        break
    assert "if(allowPathMutation)" in internal[max(0, cursor - 180) : cursor]
    cursor += 1

fallback_gate = internal.index("searchCount = allowFallbackInit ? 4u : 1u")
first_fallback_init = internal.index("->init(")
fallback_miss = internal.index("if(!found)", first_fallback_init)
assert fallback_gate < first_fallback_init < fallback_miss
probe_read = probe.index("->readFile(cheatsFile, testBuffer, 8)")
probe_close = probe.index("->closeFile(cheatsFile)", probe_read)
probe_zero = probe.index("cheatsFile->size = 0", probe_close)
probe_failure = probe.index("return false;", probe_zero)
assert probe_read < probe_close < probe_zero < probe_failure
assert device_probe.count("probeCheatsFile(") == 1
assert internal.count("probeCheatsOnDevice(") == 1
assert "readFile(cheatsFile, &testBuffer, 8)" not in internal
short_read_null = internal.index("devices[DEVICE_CHEATS] = NULL", fallback_miss)
assert fallback_miss < short_read_null
early_failure = internal.index(
    "if(devices[DEVICE_CHEATS] == NULL || cheatsFile->size == 0)"
)
assert "if(cheatsFileOpen && devices[DEVICE_CHEATS] != NULL" in internal
early_close = internal.index("->closeFile(cheatsFile)", early_failure)
early_free = internal.index("free(cheatsFile);", early_close)
assert early_failure < early_close < early_free
parse_start = internal.index("bool readComplete = false")
parse_read = internal.index("bytesRead == (s32)cheatsFile->size", parse_start)
parse_guard = internal.index(
    "if(!readComplete)", parse_read
)
parse_zero = internal.index("return 0;", parse_guard)
assert parse_start < parse_read < parse_guard < parse_zero
assert "findCheatsInternal(silent, true, true)" in legacy
assert "findCheatsInternal(true, false, false)" in read_only
assert "ensure_path(" not in read_only and "->init(" not in read_only
assert "if(cheatsFile == NULL)" in internal
delete_guard = selection_file.index("else if(deleteMismatch)")
delete_call = selection_file.index("->deleteFile(", delete_guard)
assert delete_guard < delete_call
assert "loadCheatsSelectionInternal(true)" in selection_legacy
assert "loadCheatsSelectionInternal(false)" in selection_read_only
assert "deleteFile" not in selection_read_only
assert "selectionFile == NULL || enabledFlags == NULL" in selection_file
assert "selectionFile->size == expectedSize" in selection_file
assert "enabledFlags[i] > 1u" in selection_file

launch_marker = "static void load_game_with_context"
launch = extract_function(
    swiss_source[swiss_source.rindex(launch_marker) :], launch_marker
)
assert "bool cheatsFound = findCheats(true) > 0;" in launch
assert "if(cheatsFound)" in launch
assert "loadCheatsSelection();" in launch

detail_marker = "static int gameflow_info_game"
detail = extract_function(
    swiss_source[swiss_source.rindex(detail_marker) :], detail_marker
)
assert detail.count("findCheatsReadOnly()") == 1
assert "findCheats(" not in detail
assert detail.count("loadCheatsSelectionReadOnly()") == 1
assert "loadCheatsSelection();" not in detail

def check_save_publication(source: str) -> None:
    publication = extract_function(source, "static bool gameflowPublishDetail(")
    assert publication.count("Saves_CollectGameStats(") == 1
    collection = extract_function(publication, "context->primary != NULL")
    # Saves on Details gates the read: off, a game's details never touch the
    # memory cards.
    assert publication[:publication.index(collection)].endswith(
        "if(!swissSettings.hideDetailSaves &&\n\t\t")
    assert "context->primary->fileType == IS_FILE" in collection
    assert "valid_gcm_magic(&GCMDisk)" in collection
    assert "memcmp(context->gameId, &GCMDisk, UI_GAMEFLOW_DETAIL_ID_LENGTH) == 0" in collection
    # Read once while Detail is open; Left and Right only republish it.
    assert "if(!context->savesScanned) {" in collection
    # Only the cards that hold this game's card now (card_slots.c): never one
    # a memory card emulator is still changing (indigo#102).
    assert ("(CardSlots_ReadableFor(0, context->gameId) ? 1u : 0u) |") in collection
    assert ("(CardSlots_ReadableFor(1, context->gameId) ? 2u : 0u);") in collection
    assert ("Saves_CollectGameStats(context->gameId, &context->saveStats,\n"
            "\t\t\t\t&gameflowSaveCopies, slots);") in collection
    assert "context->savesWaiting = CardSlots_WaitingFor(context->gameId);" in collection
    assert "source.savesWaiting = context->savesWaiting;" in collection
    assert ("context->savesCardFailed = CardSlots_FailedFor(0, context->gameId) ||\n"
            "\t\t\t\tCardSlots_FailedFor(1, context->gameId);") in collection
    assert "if(context->savesCardFailed) context->saveStats.partial = true;" in collection
    assert "source.savesCardFailed = context->savesCardFailed;" in collection
    assert "source.saveStats = &context->saveStats;" in collection
    assert source.count("Saves_CollectGameStats(") == 1
    assert "source.saveStats" not in publication[:publication.index(collection)]
    assert "UIGameflowDetail_Build(snapshot, &source)" in publication

check_save_publication(swiss_source)
for old, new in (
    ("if(!swissSettings.hideDetailSaves &&", "if(true &&"),
    ("context->primary->fileType == IS_FILE", "true"),
    ("valid_gcm_magic(&GCMDisk)", "true"),
    ("memcmp(context->gameId, &GCMDisk, UI_GAMEFLOW_DETAIL_ID_LENGTH) == 0", "true"),
    ("source.saveStats = &context->saveStats;", "source.saveStats = NULL;"),
    ("if(!context->savesScanned) {", "if(true) {"),
    ("CardSlots_ReadableFor(0, context->gameId) ? 1u : 0u", "1u"),
    ("CardSlots_ReadableFor(1, context->gameId) ? 2u : 0u", "2u"),
    ("source.savesWaiting = context->savesWaiting;", "source.savesWaiting = false;"),
    ("source.savesCardFailed = context->savesCardFailed;", "source.savesCardFailed = false;"),
    ("if(context->savesCardFailed) context->saveStats.partial = true;", "(void)0;"),
):
    try:
        publication = extract_function(swiss_source, "static bool gameflowPublishDetail(")
        check_save_publication(swiss_source.replace(publication, publication.replace(old, new, 1), 1))
    except AssertionError:
        pass
    else:
        raise AssertionError(f"save publication mutant escaped: {old}")

def check_detail_input(controller: str, mapping: str) -> None:
    # Host policy tests exercise the edges. Bind the actual controller to that
    # policy, including the entry/modal quarantine and physical face buttons.
    assert "const u32 detailButtons = PAD_BUTTON_X | BUTTON_B | BUTTON_A |" in controller
    assert ("PAD_BUTTON_Y | BUTTON_Z | BUTTON_R | BUTTON_UP | BUTTON_DOWN |\n"
            "\t\tBUTTON_LEFT | BUTTON_RIGHT;") in controller
    # Held or taken from the scans: a press made and let go while an idle
    # frame read a poster still counts; each re-arm drops older presses.
    assert ("buttons = UIMenuAction_Update(&detailInput,\n"
            "\t\t\t\t\tpadsButtonsHeld() | padsButtonsTaken(detailButtons),") in controller
    assert "detailButtons, BUTTON_L, BUTTON_B);" in controller
    assert controller.count("UIMenuAction_Init(&detailInput, padsButtonsHeld());\n"
                            "\t\t(void)padsButtonsTaken(detailButtons);") == 1
    assert controller.count("UIMenuAction_Init(&detailInput, padsButtonsHeld());\n"
                            "\t(void)padsButtonsTaken(detailButtons);") == 1
    # The stick steps like the D-pad, on both axes (Left and Right choose a
    # save copy), once a push (no repeat), quiet while a button is down, and
    # re-armed with the buttons after a modal.
    assert ("padsMenuInputPoll(&detailStick, 0u,\n\t\t\t\t\tUI_MENU_INPUT_AXIS_BOTH,\n"
            "\t\t\t\t\t(padsButtonsHeld() & detailButtons) != 0u);") in controller
    assert controller.count("UIMenuInput_Init(&detailStick);") == 2
    assert "gameflowWaitDetailButtonsReleased" not in controller
    assert "while(!(padsButtonsHeld() & detailButtons))" not in controller
    assert "if(buttons & PAD_BUTTON_X) input |= UI_GAMEFLOW_DETAIL_INPUT_X;" in mapping
    assert "if(buttons & PAD_BUTTON_Y) input |= UI_GAMEFLOW_DETAIL_INPUT_Y;" in mapping
    assert "if(buttons & BUTTON_X)" not in mapping
    assert "if(buttons & BUTTON_Y)" not in mapping
    assert "if(buttons & BUTTON_L) input |= UI_GAMEFLOW_DETAIL_INPUT_L;" in mapping
    assert "if(buttons & BUTTON_UP) input |= UI_GAMEFLOW_DETAIL_INPUT_UP;" in mapping
    assert "if(buttons & BUTTON_DOWN) input |= UI_GAMEFLOW_DETAIL_INPUT_DOWN;" in mapping
    library_exit = extract_function(
        controller, "if(action == UI_GAMEFLOW_DETAIL_ACTION_LIBRARY)"
    )
    assert "return 0;" in library_exit and "while(" not in library_exit


detail_mapping = extract_function(swiss_source, "static u32 gameflowDetailInput")
check_detail_input(detail, detail_mapping)
detail_input_mutants = (
    (detail.replace("UIMenuAction_Update", "oldHeldAction", 1), detail_mapping),
    (detail.replace("padsButtonsHeld() | padsButtonsTaken(detailButtons),",
                    "padsButtonsHeld(),", 1), detail_mapping),
    (detail.replace("(void)padsButtonsTaken(detailButtons);", "", 1), detail_mapping),
    (detail.replace("(void)padsButtonsTaken(detailButtons);", "", 2), detail_mapping),
    (detail.replace("detailButtons, BUTTON_L, BUTTON_B);",
                    "detailButtons, BUTTON_L, 0u);", 1), detail_mapping),
    (detail.replace("UIMenuAction_Init(&detailInput, padsButtonsHeld());", "", 1), detail_mapping),
    (detail.replace("UIMenuAction_Init(&detailInput, padsButtonsHeld());", "", 2), detail_mapping),
    (detail.replace("PAD_BUTTON_X", "BUTTON_X", 1), detail_mapping),
    (detail, detail_mapping.replace("PAD_BUTTON_Y", "BUTTON_Y", 1)),
    (detail, detail_mapping.replace("if(buttons & BUTTON_L)", "if(false)", 1)),
    (detail.replace(" | BUTTON_UP | BUTTON_DOWN |", " |", 1), detail_mapping),
    (detail.replace("\t\tBUTTON_LEFT | BUTTON_RIGHT;", "\t\t0u;", 1), detail_mapping),
    (detail, detail_mapping.replace("if(buttons & BUTTON_DOWN)", "if(false)", 1)),
    (detail.replace("UI_MENU_INPUT_AXIS_BOTH,",
                    "UI_MENU_INPUT_AXIS_BOTH | UI_MENU_INPUT_REPEAT,", 1), detail_mapping),
    (detail.replace("(padsButtonsHeld() & detailButtons) != 0u);", "false);", 1), detail_mapping),
    (detail.replace("UIMenuInput_Init(&detailStick);", "", 1), detail_mapping),
)
for mutant_controller, mutant_mapping in detail_input_mutants:
    try:
        check_detail_input(mutant_controller, mutant_mapping)
    except AssertionError:
        pass
    else:
        raise AssertionError("Detail input regression escaped wiring audit")

B_KEEPS = "if(!gameflowSaveAsk(text)) {\n\t\tcontext->saveChoice = -1;\n\t\treturn false;\n\t}"
STEPS = ("\t\t\tif(buttons & (BUTTON_LEFT | BUTTON_RIGHT)) {\n"
         "\t\t\t\t/* Not while a card changes to this game's card: its copy\n"
         "\t\t\t\t * isn't on the list yet. */\n"
         "\t\t\t\tif(gameflowSaveChoosable(context) && !context->savesWaiting) {")


def check_save_load(controller: str, load: str, choosable: str, choice: str) -> None:
    # A save copy chosen with Left and Right goes on the card only after A in
    # its own box, and before the launch screen takes over: B, or a load that
    # didn't go on, keeps Detail open, B with the card as it is again.
    boot = controller.index("if((action == UI_GAMEFLOW_DETAIL_ACTION_BOOT ||")
    load_at = controller.index("!gameflowLoadChosenSave(context)) {", boot)
    assert load_at < controller.index(
        "DrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LAUNCH);", boot)
    ask = load.index(B_KEEPS)
    assert load.count("Saves_LoadCopy(") == 1 and ask < load.index("Saves_LoadCopy(")
    # There is a choice only where a launch can put it on the card the game
    # reads: a card in a slot, Emulate Memory Card off. Without one, Left and
    # Right choose nothing, the box shows no « SAVES », and Launch leaves the
    # card as it is (it never asks, never refuses).
    assert "context->saveSlot >= 0" in choosable and \
        "!swissSettings.emulateMemoryCard" in choosable
    # Nor on a memory card emulator: the MemCard PRO build tested on a real
    # card (indigo#102) only read one.
    assert "!(gameflowSaveEmulators & (1u << context->saveSlot))" in choosable
    assert "!gameflowSaveChoosable(context)" in load[:ask]
    assert STEPS in controller
    assert choice.index("if(!gameflowSaveChoosable(context)) {\n\t\treturn;") < \
        choice.index("source->saveCopies = gameflowSaveCopies.count;")
    # The question says the card's own copy goes to the Save Folder only when
    # the card holds that same file, not just another file of the game.
    assert OWN in load


OWN = ("own |= gameflowSaveCopies.copy[i].source == (savesCopySource_t)context->saveSlot &&\n"
       "\t\t\tUISaves_SameFile(gameflowSaveCopies.copy[i].entry, copy->entry);")
save_load = extract_function(swiss_source, "static bool gameflowLoadChosenSave(")
save_choosable = extract_function(swiss_source, "static bool gameflowSaveChoosable(")
save_choice = extract_function(swiss_source, "static void gameflowSaveChoiceSource(")
check_save_load(detail, save_load, save_choosable, save_choice)
for mutant_controller, mutant_load, mutant_choosable, mutant_choice in (
    (detail.replace("!gameflowLoadChosenSave(context)) {", "false) {", 1), save_load,
     save_choosable, save_choice),
    (detail, save_load.replace(B_KEEPS, "", 1), save_choosable, save_choice),
    (detail, save_load.replace("context->saveChoice = -1;\n\t\treturn false;", "return false;", 1),
     save_choosable, save_choice),
    (detail, save_load.replace("!gameflowSaveChoosable(context)", "!context->savesScanned", 1),
     save_choosable, save_choice),
    (detail, save_load, save_choosable.replace("!swissSettings.emulateMemoryCard", "true", 1),
     save_choice),
    (detail, save_load, save_choosable.replace("context->saveSlot >= 0", "true", 1), save_choice),
    (detail, save_load, save_choosable.replace("!(gameflowSaveEmulators & (1u << context->saveSlot))", "true", 1), save_choice),
    (detail.replace(STEPS, STEPS.replace("gameflowSaveChoosable(context)",
                                         "context->savesScanned"), 1),
     save_load, save_choosable, save_choice),
    (detail, save_load, save_choosable,
     save_choice.replace("if(!gameflowSaveChoosable(context)) {\n\t\treturn;\n\t}\n", "", 1)),
    (detail, save_load.replace(OWN, OWN.split(" &&")[0] + ";", 1), save_choosable, save_choice),
):
    try:
        check_save_load(mutant_controller, mutant_load, mutant_choosable, mutant_choice)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("Detail save load regression escaped its audit")

carousel = extract_function(swiss_source, "uiDrawObj_t* renderFileCarousel")
declaration = carousel.index("u32 browserButtons;")
sample = carousel.index("browserButtons = padsButtonsHeld() |", declaration)
activation = carousel.index("bool retainedActivation", sample)
nav_guard = carousel.index("if(!retainedActivation)", activation)
first_nav = carousel.index("browserButtons & BUTTON_LEFT", nav_guard)
activate_branch = carousel.index("if((browserButtons & BUTTON_A) || openSettings)",
                                 first_nav)
assert declaration < sample < activation < nav_guard < first_nav < activate_branch
# A and an eligible game's Y Settings own their frame. Y on an ordinary
# folder has no retained action and must not activate a folder page.
settings = carousel.index("bool openSettings", sample)
assert sample < settings < activation
assert "UIGameflowLibrary_UsesRetainedDetail(" in carousel[settings:activation]
assert "(browserButtons & PAD_BUTTON_Y)" in carousel[settings:activation]
assert "= (browserButtons & BUTTON_A) || openSettings;" in carousel[activation:nav_guard]
assert "(BUTTON_A | PAD_BUTTON_Y)" not in carousel[activation:nav_guard]

cheats_renderer = extract_function(framebuffer_source, "static void _DrawCheats")
scroll_block = extract_function(
    cheats_renderer, "if(s->count > UI_CHEATS_VISIBLE_ROWS)"
)
cheats_header = (ROOT / "cube/swiss/source/gui/ui_cheats.h").read_text()
visible_rows = re.search(r"#define UI_CHEATS_VISIBLE_ROWS\s+(\d+)\b", cheats_header)
assert visible_rows and int(visible_rows.group(1)) > 0
# Both denominators must stay under the strict count > visible-rows guard:
# empty, single-entry, and exactly-one-page catalogs must not divide by zero.
assert "/ s->count" in scroll_block
assert re.search(r"/\s*\(s->count - UI_CHEATS_VISIBLE_ROWS\)", scroll_block)
assert not re.search(r"/\s*\(?\s*s->count", cheats_renderer.replace(scroll_block, "", 1))
assert "if(s->rowCount == 0)" in cheats_renderer

print("game detail safety audit OK")


def check_card_slots(source: str) -> None:
    """indigo#102: the GameID goes through card_slots.c, Detail follows the
    switch on idle retraces, no copy is chosen or written while a card is
    still changing, and a launch sends an ID that waited."""
    def definition(marker: str) -> str:
        return extract_function(source[source.rindex(marker):], marker)

    loader = definition("static void load_game_with_context(")
    assert "gameID_early_set(" not in loader
    assert loader.count("CardSlots_RequestGame(&GCMDisk);") == 1
    assert loader.index("CardSlots_RequestGame(&GCMDisk);") < loader.index("config_find(config);")
    shown = loader.index("info_game(config))) {")
    assert loader.index("gameflowWaitForCardIds();", shown) < loader.index("config_load_current(config);")
    detail = definition("static int gameflow_info_game(")
    assert ("if(CardSlots_Poll() && context->savesScanned) {\n"
            "\t\t\t\t\t\tcontext->savesScanned = false;\n"
            "\t\t\t\t\t\tgameflowPublishDetail(config, context);") in detail
    assert "if(gameflowSaveChoosable(context) && !context->savesWaiting) {" in detail
    chosen = definition("static bool gameflowLoadChosenSave(")
    gate = chosen.index("/* Never onto the card a memory card emulator is still changing from. */\n"
                        "\tif(!CardSlots_WritableFor(context->saveSlot, context->gameId)) {")
    assert gate < chosen.index("Saves_LoadCopy(")
    assert "return false;" in chosen[gate:chosen.index("}", gate) + 1]
    # Asked again after the question box: the card may change while it is up.
    asked = chosen.index("if(!gameflowSaveAsk(text)) {")
    assert gate < asked
    again = chosen.index("if(!CardSlots_WritableFor(context->saveSlot, context->gameId)) {", asked)
    changed = chosen.index("if(CardSlots_Poll() || gameflowSlotsChanged) {", asked)
    assert asked < changed < again
    assert "return false;" in chosen[changed:chosen.index("}", changed) + 1]
    # The question box follows the slots while it is up, from a clean start.
    assert chosen.index("gameflowSlotsChanged = false;") < asked
    # Not now after a box that saw a card change: Detail reads the cards again.
    assert ("!gameflowLoadChosenSave(context)) {\n"
            "\t\t\t/* Not now: the save stays as it was, and so does Detail, but for\n"
            "\t\t\t * a card that changed while one of its boxes was up. */\n"
            "\t\t\tif(gameflowSlotsChanged) context->savesScanned = false;\n"
            "\t\t\tgameflowPublishDetail(config, context);") in detail
    box = definition("static bool gameflowSaveAsk(")
    assert box.index("if(CardSlots_Poll()) gameflowSlotsChanged = true;") < box.index("VIDEO_WaitVSync();")
    assert again < chosen.index("Saves_LoadCopy(")
    # A card given up on while switching makes the scan incomplete.
    publication = definition("static bool gameflowPublishDetail(")
    # A slot the game can't read at a scan is an emulator changing cards:
    # marked before the cards are read, so it never gets a save choice.
    assert publication.index("gameflowSaveEmulators |= ~slots & 3u;") < \
        publication.index("Saves_CollectGameStats(")
    assert ("context->savesCardFailed = CardSlots_FailedFor(0, context->gameId) ||\n"
            "\t\t\t\tCardSlots_FailedFor(1, context->gameId);\n"
            "\t\t\tif(context->savesCardFailed) context->saveStats.partial = true;") in publication
    wait = definition("static void gameflowWaitForCardIds(")
    # Sync Clock may adjust the console clock during a card switch. The
    # executable launch-wait regression also exercises both step directions.
    assert "u64 start = SYS_GetSystemTime();" in wait
    assert "gettime()" not in wait
    assert "while(CardSlots_IdWaiting() && diff_msec(start, SYS_GetSystemTime()) < 65000u) {" in wait
    assert "(void)CardSlots_Poll();" in wait


check_card_slots(swiss_source)
for old, new in (
    ("CardSlots_RequestGame(&GCMDisk);", "gameID_early_set(&GCMDisk);"),
    ("\tgameflowWaitForCardIds();\n", "\n"),
    ("if(CardSlots_Poll() && context->savesScanned) {", "if(false && context->savesScanned) {"),
    ("!context->savesWaiting) {", "true) {"),
    ("/* Never onto the card a memory card emulator is still changing from. */\n"
     "\tif(!CardSlots_WritableFor(context->saveSlot, context->gameId)) {",
     "/* Never onto the card a memory card emulator is still changing from. */\n"
     "\tif(false) {"),
    ("diff_msec(start, SYS_GetSystemTime()) < 65000u", "true"),
    ("u64 start = SYS_GetSystemTime();\n\n\twhile(CardSlots_IdWaiting()", "u64 start = gettime();\n\n\twhile(CardSlots_IdWaiting()"),
    ("\tif(CardSlots_Poll() || gameflowSlotsChanged) {", "\tif(false) {"),
    ("\tif(CardSlots_Poll() || gameflowSlotsChanged) {", "\tif(CardSlots_Poll()) {"),
    ("\t\tif(CardSlots_Poll()) gameflowSlotsChanged = true;\n", ""),
    ("\tgameflowSlotsChanged = false;\n\tif(!gameflowSaveChoosable(context) ||", "\tif(!gameflowSaveChoosable(context) ||"),
    ("\t\t\tif(gameflowSlotsChanged) context->savesScanned = false;\n\t\t\tgameflowPublishDetail(config, context);",
     "\t\t\tgameflowPublishDetail(config, context);"),
    ("\t\t\tif(context->savesCardFailed) context->saveStats.partial = true;", "\t\t\t(void)0;"),
    ("\t\t\tgameflowSaveEmulators |= ~slots & 3u;\n", ""),
):
    try:
        check_card_slots(swiss_source.replace(old, new, 1))
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError(f"card slots mutant escaped: {old}")
