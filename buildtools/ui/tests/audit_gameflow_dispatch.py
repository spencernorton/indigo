#!/usr/bin/env python3
"""Mechanical contract for strict /games retained-Library dispatch."""

import re
import sys
from pathlib import Path


if not __debug__:
    raise SystemExit(
        "audit_gameflow_dispatch.py requires Python assertions; rerun without -O/PYTHONOPTIMIZE"
    )


ROOT = Path(__file__).resolve().parents[3]
SWISS = (ROOT / "cube/swiss/source/swiss.c").read_text()
MAIN = (ROOT / "cube/swiss/source/main.c").read_text()
FILES = (ROOT / "cube/swiss/source/files.c").read_text()
UTIL = (ROOT / "cube/swiss/source/util.c").read_text()
CONFIG = (ROOT / "cube/swiss/source/config/config.c").read_text()
SETTINGS = (ROOT / "cube/swiss/source/gui/settings.c").read_text()
PROFILE_PATH = Path(__file__).parent / "fixtures/phase4f_legacy_global.ini"
HOME_PROFILE_PATH = Path(__file__).parent / "fixtures/phase4f_home_global.ini"


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


menu = extract_function(SWISS, "void menu_loop()")
carousel = extract_function(SWISS, "uiDrawObj_t* renderFileCarousel(")
# A on an entry the Library doesn't open: the lists' one copy, which the
# Library calls as the Library.
LEGACY_ACTIVATE = "filesActivate(directory, true);"
activate_entry = extract_function(SWISS, "static void filesActivate(")
# The dispatch: the Library wherever it applies, the File Browser everywhere
# else. File Browser Type is no longer read, so an old GameBrowserType can't
# hide the Library; the scene requests belong to the Library's branch alone.
dispatch = extract_function(menu, "if(devices[DEVICE_CUR] != NULL && curMenuLocation==ON_FILLIST)")
strict_probe = dispatch.index("if(!gameflowListFallback && gameflowLibraryMode(")
release = dispatch.index("filesOtherRelease();", strict_probe)
layout_request = dispatch.index("UIScene_RequestLibraryLayout(gameflowLayout());", release)
scene_request = dispatch.index("UIScene_Request(UI_SCENE_LIBRARY);", layout_request)
retained_renderer = dispatch.index("renderFileCarousel(", scene_request)
file_list = dispatch.index("else {", retained_renderer)
list_renderer = dispatch.index("renderFileList(", file_list)
assert strict_probe < release < layout_request < scene_request < retained_renderer < \
    file_list < list_renderer
assert dispatch.count("UIScene_Request") == 2
for gone in ("BrowserType", "UIGameflowLibrary_SelectBrowser(", "renderFileBrowser(",
             "renderFileFullwidth(", "switch("):
    assert gone not in menu, gone
assert "UIGameflowLibrary_SelectBrowser" not in SWISS

# Production-upgrade contract: the preserved profile omits GameBrowserType,
# inherits Fullwidth, and keeps Swiss's default /games flattening. That scans
# strict folders as image files, so both modes must reach retained Detail.
profile = {}
for raw_line in PROFILE_PATH.read_text().splitlines():
    name, value = raw_line.split("=", 1)
    profile[name] = value
assert profile == {
    "FileBrowserType": "Fullwidth",
    "FlattenDir": "*/games",
    "Autoload": "gcldr:/games/Pokemon Colosseum [GC6E01].iso",
    "RecentListLevel": "Lazy",
}
assert "GameBrowserType" not in profile
assert 'swissSettings.gameBrowserType = BROWSER_FULLWIDTH;' in MAIN
assert 'strcpy(swissSettings.flattenDir, "*/games");' in MAIN
assert "fileBrowserTypeStr[] = {\"Standard\", \"Fullwidth\", \"Carousel\"}" in SETTINGS
assert 'else if(!strcmp("GameBrowserType", name))' in CONFIG
assert "fnmatch(swissSettings.flattenDir, curDir.name" in FILES

home_profile = {}
for raw_line in HOME_PROFILE_PATH.read_text().splitlines():
    name, value = raw_line.split("=", 1)
    home_profile[name] = value
assert home_profile == {
    "FileBrowserType": "Fullwidth",
    "FlattenDir": "*/games",
    "Autoload": "",
    "RecentListLevel": "Lazy",
}

home_route = extract_function(SWISS, "static bool gameflowEnterLibraryFromHome(")
assert "UIGameflowLibrary_IsGamesRootEntry(" in home_route
assert "memcpy(&curDir, directory[i], sizeof(file_handle));" in home_route
assert "needsRefresh = 1;" in home_route
retained_root = home_route.index("UIGameflowLibrary_Locate(gamesRoot, curDir.name)")
root_return = home_route.index("return true;", retained_root)
root_scan = home_route.index("directory = getSortedDirEntries();", root_return)
assert retained_root < root_return < root_scan
assert "curSelection" not in home_route[retained_root:root_scan]
assert "needsRefresh" not in home_route[retained_root:root_scan]
home_dispatch = extract_function(SWISS, "static void homeDispatchEffect(")
library_effect = home_dispatch.index("case UI_HOME_EFFECT_OPEN_LIBRARY:")
library_call = home_dispatch.index("gameflowEnterLibraryFromHome()", library_effect)
next_effect = home_dispatch.index("case UI_HOME_EFFECT_CHANGE_SOURCE:", library_call)
assert library_effect < library_call < next_effect
assert "homeLibraryEntryPending = true;" in home_dispatch
pending_scan = menu.index("if(homeLibraryEntryPending)")
pending_clear = menu.index("homeLibraryEntryPending = false;", pending_scan)
pending_promote = menu.index("if(gameflowEnterLibraryFromHome())", pending_clear)
pending_continue = menu.index("continue;", pending_promote)
assert pending_scan < pending_clear < pending_promote < pending_continue
assert menu.count("gameflowEnterLibraryFromHome(") == 1, (
    "Library promotion bypasses the reducer-owned pending scan"
)
assert "int postScanLocation = curMenuLocation;" in menu
assert menu.count("curMenuLocation = postScanLocation;") == 2
browser_owned_loop = menu[menu.index("uiDrawObj_t *filePanel = NULL;"):]
assert browser_owned_loop.count("homePublishBrowserTransition(&filePanel);") == 2
assert "homePublish(curMenuLocation == ON_OPTIONS);" not in browser_owned_loop
home_transition = extract_function(SWISS, "static void homePublishBrowserTransition(")
assert home_transition.index("DrawDispose(*filePanel);") < home_transition.index(
    "*filePanel = NULL;"
) < home_transition.index("homePublish(visible);")
assert "DrawUpdateMenuButtons" not in menu
startup_policy = menu.index("UIGameflowLibrary_ShouldStartHome(")
startup_root = menu.index(
    "memcpy(&curDir, devices[DEVICE_CUR]->initial,", startup_policy
)
startup_refresh = menu.index("needsRefresh = 1;", startup_root)
startup_home = menu.index("curMenuLocation = ON_OPTIONS;", startup_refresh)
startup_release = menu.index(
    "while(padsButtonsHeld() & BUTTON_B)", startup_home
)
home_wait = menu.index("UIScene_Request(UI_SCENE_HOME)", startup_release)
assert (
    startup_policy
    < startup_root
    < startup_refresh
    < startup_home
    < startup_release
    < home_wait
)

library_mode = extract_function(
    SWISS, "static uiGameflowLibraryMode_t gameflowLibraryMode("
)
assert "UIGameflowLibrary_Locate(gamesRoot, curDir.name)" in library_mode
# Library Folders locates by its own rule only on storage, never inside a
# disc's game data. Optical games are exposed at the device root.
assert ("location = swissSettings.libraryFolders &&\n"
        "\t\tdevices[DEVICE_CUR] != &__device_dvd ?\n"
        "\t\tUIGameflowLibrary_LocateFolders(gamesRoot, curDir.name) :\n"
        "\t\tUIGameflowLibrary_Locate(gamesRoot, curDir.name);") in library_mode
assert "UIGameflowLibrary_ClassifierInit(&classifier, location);" in library_mode
assert "UIGameflowLibrary_ClassifierAdd(&classifier, type, name)" in library_mode
assert "UIGameflowLibrary_ClassifierFinish(&classifier)" in library_mode

autoload_lookup = extract_function(UTIL, "int find_existing_entry(")
assert "return RECENT_ERR_ENT_MISSING;" in autoload_lookup
assert "curMenuLocation = ON_FILLIST;" in autoload_lookup
assert "needsRefresh = 0;" in autoload_lookup

detail_policy = carousel.index("UIGameflowLibrary_UsesRetainedDetail(")
folder_detail = carousel.index("gameflowResolveAndLoadFolder(", detail_policy)
image_detail = carousel.index("gameflowLoadImageWithContext(", folder_detail)
legacy_fallback = carousel.index(LEGACY_ACTIVATE, image_detail)
assert detail_policy < folder_detail < image_detail < legacy_fallback
assert "load_file();" in activate_entry[activate_entry.index("fileType==IS_FILE"):]

image_loader = extract_function(SWISS, "static bool gameflowLoadImageWithContext(")
assert "gameflowReadResolverHeader(" in image_loader
# A Library launch reopens the file its banner was read through and forgets a
# stale sector map, before the header read, and does the same for disc 2.
fresh = extract_function(SWISS, "static void gameflowFreshHandle(")
assert "file->device->closeFile(file);" in fresh
assert ("if(file->status == STATUS_HAS_MAPPING) {\n"
        "\t\tfile->status = STATUS_NOT_MAPPED;") in fresh
assert image_loader.index("gameflowFreshHandle(image);") < \
    image_loader.index("gameflowReadResolverHeader(image, &headerEntry)")
assert image_loader.index("gameflowFindOppositeImage(image, &headerEntry,") < \
    image_loader.index("gameflowFreshHandle(context.oppositeDisc);") < \
    image_loader.index("gameflowPopulateResolvedMeta(context.oppositeDisc,")
assert "gameflowFindOppositeImage(image, &headerEntry," in image_loader
assert "meta_find_disc2(" not in image_loader
assert "gameflowProtectMetaFile(image);" in image_loader
opposite_finder = extract_function(SWISS, "static file_handle *gameflowFindOppositeImage(")
assert "image->device->quirks & QUIRK_GCLOADER_NO_DISC_2" in opposite_finder
# A disc 2 lookup reads only the images its metadata cannot rule out.
assert opposite_finder.index("UIGameflowResolver_MayBeOppositeDisc(primaryHeader,") < \
    opposite_finder.index("gameflowReadResolverHeader(candidate,")
context_load = image_loader.index("load_file_with_context(&context);")
primary_close = image_loader.index(
    "devices[DEVICE_CUR]->closeFile(&curFile);", context_load
)
opposite_close = image_loader.index(
    "devices[DEVICE_CUR]->closeFile(context.oppositeDisc);", primary_close
)
copy_back = image_loader.index("memcpy(image, &curFile", opposite_close)
assert context_load < primary_close < opposite_close < copy_back
assert "DrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);" in image_loader
assert "DrawClearGameflowDetail(context.event);" in image_loader

folder_loader = extract_function(SWISS, "static bool gameflowResolveAndLoadFolder(")
assert "folder->device->quirks & QUIRK_GCLOADER_NO_DISC_2" in folder_loader
resolve_call = folder_loader.index("UIGameflowResolver_Resolve(")
primary_meta = folder_loader.index(
    "gameflowPopulateResolvedMeta(context.primary,", resolve_call
)
opposite_meta = folder_loader.index(
    "gameflowPopulateResolvedMeta(context.oppositeDisc,", primary_meta
)
resolver_free = folder_loader.index("free(resolverEntries);", opposite_meta)
assert resolve_call < primary_meta < opposite_meta < resolver_free

meta_population = extract_function(SWISS, "static bool gameflowPopulateResolvedMeta(")
assert "populate_meta(file);" in meta_population
assert "file->meta->diskId.gamename" in meta_population
assert "file->meta->diskId.company" in meta_population
assert "file->meta->diskId.disknum" in meta_population
assert "file->meta->diskId.gamever" in meta_population

meta_protection = extract_function(SWISS, "static void gameflowProtectMetaFile(")
assert "getSortedDirEntries()" in meta_protection
assert "getSortedDirEntryCount()" in meta_protection
assert "getCurrentDirEntries()" not in meta_protection
assert "current_view_end = i;" in meta_protection

snapshot_record = extract_function(SWISS, "static void gameflowSnapshotRecord(")
assert "if(!record->gameId[0])" in snapshot_record
assert "gameflowReadResolverHeader(file, &headerEntry)" in snapshot_record
assert "file->device == &__device_flippy" in snapshot_record
assert "file->device == &__device_flippyflash" in snapshot_record
assert "file->device->closeFile(file);" in snapshot_record
assert "memcpy(record->gameId, headerEntry.gameId" in snapshot_record
assert "sizeof(headerEntry.gameId)" in snapshot_record
assert "record->gameId[sizeof(record->gameId) - 1u] = '\\0';" in snapshot_record
assert "memcpy(file->meta->diskId.gamename, headerEntry.gameId, 4u);" in snapshot_record


# Library Layout: each layout's window, presses and stick; Y opens a game's
# settings through its retained Detail. Every rule below has a mutant that
# must fail it.
def check_layouts(swiss: str) -> None:
    carousel = extract_function(swiss, "uiDrawObj_t* renderFileCarousel(")
    build = extract_function(swiss, "static bool gameflowBuildSnapshot(")
    # Apps (gui/apps.c) lays itself out and moves as the Library does.
    layout_of = extract_function(swiss, "\nuiGameflowLayout_t gameflowLayout(")
    detail = extract_function(swiss[swiss.rindex("static int gameflow_info_game"):],
                              "static int gameflow_info_game")
    folder = extract_function(swiss, "static bool gameflowResolveAndLoadFolder(")
    image = extract_function(swiss, "static bool gameflowLoadImageWithContext(")

    # Public setting zero replaces Horizontal with Boxes. The renderer's
    # flat mode stays internal for Apps and the jewel-case memory fallback.
    assert "swissSettings.libraryLayout > UI_GAMEFLOW_LAYOUT_HORIZONTAL &&" in layout_of
    assert "swissSettings.libraryLayout < UI_GAMEFLOW_LIBRARY_LAYOUT_COUNT ?" in layout_of
    assert "UI_GAMEFLOW_LAYOUT_BOXES;" in layout_of
    # The grid window only for the grid; its off-screen rows never read.
    grid_at = build.index("if(layout == UI_GAMEFLOW_LAYOUT_GRID) {")
    ring_at = build.index("UIGameflowLibrary_BuildWindow(", grid_at)
    filter_at = build.index("unread = file->fileType == IS_FILE && file->meta == NULL;")
    assert grid_at < build.index("UIGameflowLibrary_BuildGridWindow(", grid_at) < \
        filter_at < ring_at
    assert ("if(!unread || (slots[i].relativeSlot >= -1 &&\n"
            "\t\t\t\tslots[i].relativeSlot <= 1)) {") in build
    # Each layout's presses; the carousel's are the ones it always had.
    ring = carousel[carousel.index("/* The carousel: left and right step, up and down page. */"):]
    ring = ring[:ring.index("for(int move = 0;")]
    for line in ("if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;",
                 "if(right) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;",
                 "if(up || (browserButtons & BUTTON_L))\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;",
                 "if(down || (browserButtons & BUTTON_R))\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;"):
        assert line in ring, line
    column = carousel[carousel.index("/* The column: up and down step, left and right page. */"):]
    column = column[:column.index("else if(layout == UI_GAMEFLOW_LAYOUT_GRID)")]
    assert "if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;" in column
    assert "if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_NEXT;" in column
    assert "if(left || (browserButtons & BUTTON_L))" in column
    grid = carousel[carousel.index("/* The grid: every direction steps, L and R page. */"):]
    grid = grid[:grid.index("/* The carousel:")]
    assert "if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_UP;" in grid
    assert "if(down) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_DOWN;" in grid
    assert ("columns ?\n\t\t\t\t\tUI_GAMEFLOW_LIBRARY_GRID_ROWS : FILES_PER_PAGE_CAROUSEL,"
            in carousel)
    # Y: physical Y only, never on the parent card, A wins; the loaders
    # carry it into Detail, which opens the settings once and never boots.
    entry = carousel[carousel.index("bool openSettings ="):]
    entry = entry[:entry.index(";")]
    assert "!(browserButtons & BUTTON_A)" in entry
    assert "(browserButtons & PAD_BUTTON_Y)" in entry
    assert "UIGameflowLibrary_UsesRetainedDetail(" in entry
    # Library Folders: each entry answers for itself, so a folder of games
    # never opens Detail and a game beside it always does.
    assert "gameflowEntryMode(gameflowMode, directory[curSelection])" in entry
    activate = carousel[carousel.index("if((browserButtons & BUTTON_A) || openSettings) {"):]
    activate = activate[:activate.index(LEGACY_ACTIVATE)]
    assert "gameflowEntryMode(gameflowMode, directory[curSelection]);" in activate
    assert "if(entryMode == UI_GAMEFLOW_LIBRARY_GAME_FOLDERS) {" in activate
    # Inside a folder, B goes up it before it can reach Home.
    up = carousel.index("if((browserButtons & BUTTON_B) && gameflowInsideFolder()) {")
    home = carousel.index("filesHome();", up)
    assert "curMenuLocation = ON_OPTIONS;" in extract_function(swiss, "static void filesHome(")
    assert "gameflowNavigateParent(true, directory[0]);" in carousel[up:home]
    branch = carousel.index("if((browserButtons & BUTTON_A) || openSettings) {")
    loaders = carousel.index("gameflowSnapshot, openSettings);", branch)
    loaders = carousel.index("gameflowSnapshot, openSettings);", loaders + 1)
    stop = carousel.index("if(openSettings) {", loaders)
    stop_break = carousel.index("break;", stop)
    legacy = carousel.index(LEGACY_ACTIVATE, stop)
    assert branch < loaders < stop < stop_break < legacy
    for loader in (folder, image):
        assert "bool openSettings)" in loader
        assert "context.openSettings = openSettings;" in loader
    assert "(swissSettings.autoBoot && !context->openSettings)) {" in detail
    assert "openSettings = context->openSettings;\n\tcontext->openSettings = false;" in detail
    assert "return openSettings ? 0 : info_game(config);" in detail
    once = detail.index("if(openSettings) {")
    assert detail.index("openSettings = false;", once) < \
        detail.index("action = UI_GAMEFLOW_DETAIL_ACTION_SETTINGS;", once) < \
        detail.index("buttons = UIMenuAction_Update(", once)
    assert detail.count("show_settings_view(VIEW_GAME, 0, config)") == 1


check_layouts(SWISS)
# Every retained parent activation uses the root boundary guard before it
# can refresh a listing. B at the root still selects Home directly.
parent_navigation = extract_function(SWISS, "static void gameflowNavigateParent(")
assert parent_navigation.index("UI_GAMEFLOW_LIBRARY_LOCATION_ROOT") < \
    parent_navigation.index("curMenuLocation = ON_OPTIONS;") < \
    parent_navigation.index("return;") < parent_navigation.index("filesUp(parent);")
assert "upToParent(&curDir)" in extract_function(SWISS, "static void filesUp(")
# Its three: X, B inside a folder, and A on ".." through filesActivate.
assert carousel.count("gameflowNavigateParent(true,") == 2
assert carousel.count(LEGACY_ACTIVATE) == 1
# The shared actions take the caller's place as flags: only the Library is
# the Library (".." and B) and shows folder pictures (stopped before Z/START).
# Swiss's lists, which called them as lists, are gone.
assert carousel.count("filesHome();") == 1
assert carousel.count("filesManage(directory)") == 1
assert carousel.count("filesRecent(true)") == 1
assert "useGameflow" not in carousel
for gone in ("uiDrawObj_t* renderFileBrowser(", "uiDrawObj_t* renderFileFullwidth("):
    assert gone not in SWISS, gone
manage_shared = extract_function(SWISS, "static bool filesManage(")
assert manage_shared.index("meta_thread_stop();") < manage_shared.index("CardArt_Pause();")
recent_shared = extract_function(SWISS, "static bool filesRecent(")
assert recent_shared.index("meta_thread_stop();") < \
    recent_shared.index("if(cardArt) {\n\t\t\tCardArt_Pause();\n\t\t}")
special = activate_entry.index("else if(directory[curSelection]->fileType==IS_SPECIAL) {")
assert activate_entry.index("gameflowNavigateParent(useGameflow, directory[curSelection]);",
                            special) < activate_entry.index("else if", special + 1)
layout_mutants = (
    ("an unknown layout is kept", "swissSettings.libraryLayout < UI_GAMEFLOW_LIBRARY_LAYOUT_COUNT ?",
     "swissSettings.libraryLayout < 99 ?"),
    ("the carousel gets the grid window", "if(layout == UI_GAMEFLOW_LAYOUT_GRID) {\n\t\tsize_t kept = 0u;",
     "if(layout != UI_GAMEFLOW_LAYOUT_HORIZONTAL) {\n\t\tsize_t kept = 0u;"),
    ("visible grid rows are skipped", "(slots[i].relativeSlot >= -1 &&\n\t\t\t\tslots[i].relativeSlot <= 1)",
     "(slots[i].relativeSlot == 0)"),
    ("the carousel loses its up page", "if(up || (browserButtons & BUTTON_L))\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;\n\t\t\t\tif(down || (browserButtons & BUTTON_R))\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;\n\t\t\t}\n\t\t\tfor",
     "if(browserButtons & BUTTON_L)\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_BACK;\n\t\t\t\tif(down || (browserButtons & BUTTON_R))\n\t\t\t\t\tmoves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PAGE_ON;\n\t\t\t}\n\t\t\tfor"),
    ("the column steps on left", "if(up) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;\n\t\t\t\tif(down)",
     "if(left) moves[moveCount++] = UI_GAMEFLOW_LIBRARY_MOVE_PREVIOUS;\n\t\t\t\tif(down)"),
    ("the grid pages by cards", "UI_GAMEFLOW_LIBRARY_GRID_ROWS : FILES_PER_PAGE_CAROUSEL,",
     "FILES_PER_PAGE_CAROUSEL : FILES_PER_PAGE_CAROUSEL,"),
    ("the C-stick opens settings", "(browserButtons & PAD_BUTTON_Y) &&\n\t\t\tUIGameflowLibrary_UsesRetainedDetail(",
     "(browserButtons & BUTTON_Y) &&\n\t\t\tUIGameflowLibrary_UsesRetainedDetail("),
    ("Y on the parent card", "(browserButtons & PAD_BUTTON_Y) &&\n\t\t\tUIGameflowLibrary_UsesRetainedDetail(\n\t\t\t\tgameflowEntryMode(gameflowMode, directory[curSelection]),\n\t\t\t\tgameflowEntryType(directory[curSelection]));",
     "(browserButtons & PAD_BUTTON_Y);"),
    ("Y on a folder of games", "UIGameflowLibrary_UsesRetainedDetail(\n\t\t\t\tgameflowEntryMode(gameflowMode, directory[curSelection]),",
     "UIGameflowLibrary_UsesRetainedDetail(\n\t\t\t\tgameflowMode,"),
    ("A on a folder of games opens Detail", "uiGameflowLibraryMode_t entryMode =\n\t\t\t\tgameflowEntryMode(gameflowMode, directory[curSelection]);",
     "uiGameflowLibraryMode_t entryMode =\n\t\t\t\tgameflowMode;"),
    ("B inside a folder goes Home", "if((browserButtons & BUTTON_B) && gameflowInsideFolder()) {",
     "if((browserButtons & BUTTON_B) && false) {"),
    ("Y falls into the legacy file path", "\t\t\tif(openSettings) {\n\t\t\t\t/* Y has no legacy meaning: it never opens or boots a file. */\n\t\t\t\twhile(padsButtonsHeld() & PAD_BUTTON_Y) VIDEO_WaitVSync();\n\t\t\t\tbreak;\n\t\t\t}\n",
     ""),
    ("a loader drops Y", "\tcontext.openSettings = openSettings;\n\tmemcpy(context.gameId, headerEntry.gameId",
     "\tmemcpy(context.gameId, headerEntry.gameId"),
    ("Y boots without prompts", "(swissSettings.autoBoot && !context->openSettings)) {",
     "swissSettings.autoBoot) {"),
    ("Y opens the settings twice", "openSettings = context->openSettings;\n\tcontext->openSettings = false;",
     "openSettings = context->openSettings;"),
    ("a failed Detail boots from Y", "return openSettings ? 0 : info_game(config);",
     "return info_game(config);"),
)
for label, old_text, new_text in layout_mutants:
    assert SWISS.count(old_text) == 1, f"mutation anchor missing: {label}"
    try:
        check_layouts(SWISS.replace(old_text, new_text, 1))
    except (AssertionError, ValueError):
        continue
    raise AssertionError(f"layout mutant escaped the dispatch audit: {label}")



# A loop that reads the pad as held buttons and does I/O between those reads
# (card art from a source that is not thread safe, about a second from DVD; a
# file copied or verified a chunk at a time) also takes the presses the
# retrace scans latched (padsButtonsTaken), or a quick tap made during the I/O
# is lost. Just ahead of the loop it drops the presses from before, so the A
# or B that led there is not acted on twice, and it never drops them inside
# the loop. Loops are found, not listed: a new one that reads held buttons
# around I/O fails here. Indigo's own sources only; upstream's keep upstream's
# code (UPSTREAM).
sys.path.insert(0, str(ROOT / "buildtools/ci"))
from check_upstream import OWN  # noqa: E402

SOURCE = ROOT / "cube/swiss/source"
WAITERS = {path: path.read_text() for path in sorted(SOURCE.rglob("*.c"))
           if OWN.match(path.relative_to(ROOT).as_posix())}
IO = ("CardArt_Poll(", "artLoad(", "->readFile(", "->writeFile(", "->readDir(",
      "populate_meta(", "filesMetaStep(")
# Memory Cards' inputNext takes its presses, and inputInit drops the old ones
# at the start of each screen (audit_saves_safety.py checks both). The File
# Browser drops them on opening and after each box, but keeps those made
# while a folder is read (audit_files_contract.py checks both).
CLEARED_BY_CALLER = {"inputNext", "renderFileList"}
LATCHED = {"showPrograms", "renderFileCarousel", "manage_file_ex", "verify_game",
           "inputNext", "renderFileList"}
CLEAR = re.compile(r"\(void\)padsButtonsTaken\((\w+)\);")
TAKE = re.compile(r"(?<!\(void\))padsButtonsTaken\((\w+)\)")


def blank(text: str) -> str:
    """Comments and literals as spaces, so offsets stay the source's."""
    return re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
                  lambda m: re.sub(r"[^\n]", " ", m.group()), text, flags=re.S)


def closing(text: str, at: int) -> int:
    pair = {"(": ")", "{": "}"}[text[at]]
    depth = 0
    for index in range(at, len(text)):
        depth += (text[index] == text[at]) - (text[index] == pair)
        if depth == 0:
            return index
    raise AssertionError("unbalanced source")


def loops(code: str) -> list:
    """(start, condition span, body span) of every while, for and do loop."""
    found = []
    for match in re.finditer(r"\b(?:while|for)\s*\(|\bdo\s*\{", code):
        if match.group().startswith("do"):
            body = (match.end() - 1, closing(code, match.end() - 1) + 1)
            tail = code.index(";", body[1])
            found.append((match.start(), (body[1], tail), body))
            continue
        condition = (match.end() - 1, closing(code, match.end() - 1) + 1)
        rest = condition[1] + len(code[condition[1]:]) - len(code[condition[1]:].lstrip())
        end = closing(code, rest) + 1 if code[rest] == "{" else code.index(";", rest) + 1
        found.append((match.start(), condition, (condition[1], end)))
    return found


def wait_sites(sources: dict) -> list:
    sites = []
    for path, text in sources.items():
        code = blank(text)
        every = loops(code)
        for start, condition, (body, end) in every:
            direct = list(code[body:end])
            for inner, _, (_, inner_end) in every:
                if body <= inner < end:
                    direct[inner - body:inner_end - body] = " " * (inner_end - inner)
            direct = "".join(direct)
            reads = code[slice(*condition)] + direct
            if "padsButtonsHeld()" not in reads or not any(io in direct for io in IO):
                continue
            function = re.findall(r"^\w[^\n;]*?\b(\w+)\([^;{}]*\)\s*\{", code[:start], re.M)
            sites.append((path, function[-1], start, body, direct, code))
    return sites


def check_waits(sources: dict) -> None:
    found = set()
    for path, function, start, body, direct, code in wait_sites(sources):
        where = f"{path.name} {function}"
        taken = TAKE.findall(direct)
        assert taken, f"{where}: a press made during the loop's I/O is lost"
        assert not CLEAR.search(direct), f"{where}: the loop drops a press made during it"
        if function not in CLEARED_BY_CALLER:
            cleared = re.search(CLEAR.pattern + "$", code[:start].rstrip())
            assert cleared, f"{where}: the loop takes a press made before it"
            assert cleared.group(1) == taken[0], f"{where}: it drops other buttons than it takes"
        found.add(function)
    assert LATCHED <= found, f"loops not found: {sorted(LATCHED - found)}"


check_waits(WAITERS)
wait_mutants = []
for path, function, start, body, direct, code in wait_sites(WAITERS):
    text = WAITERS[path]
    take = TAKE.search(code, body)
    wait_mutants.append((f"{path.name} {function} reads only held buttons", path,
                         text[:take.start()] + "0u" + text[take.end():]))
    wait_mutants.append((f"{path.name} {function} drops presses made during it", path,
                         text[:body + 1] + f"(void)padsButtonsTaken({take.group(1)});" +
                         text[body + 1:]))
    if function not in CLEARED_BY_CALLER:
        clear = list(CLEAR.finditer(code, 0, start))[-1]
        wait_mutants.append((f"{path.name} {function} keeps the presses from before", path,
                             text[:clear.start()] + text[clear.end():]))
settings = SOURCE / "gui/settings.c"
anchor = "SETTINGS_MENU_INPUT_POLICY, buttons != 0u);\n"
assert WAITERS[settings].count(anchor) == 1, "mutation anchor missing: settings wait"
wait_mutants.append(("a Settings wait starts reading card art", settings,
                     WAITERS[settings].replace(anchor, anchor + "\t\tCardArt_Poll();\n", 1)))
for label, path, mutated in wait_mutants:
    try:
        check_waits({**WAITERS, path: mutated})
    except AssertionError:
        continue
    raise AssertionError(f"wait mutant escaped the dispatch audit: {label}")

print(f"gameflow dispatch audit OK ({len(layout_mutants)} layout mutants, "
      f"{len(wait_mutants)} wait input mutants rejected)")
