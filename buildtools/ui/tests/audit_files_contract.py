#!/usr/bin/env python3
"""The File Browser keeps Swiss's list contract (swiss.c's renderFileList).

Its left pane is the shared listing menu_loop scans, so it does what Swiss's
lists did with it: stops the meta thread before anything acts on a file,
locks an entry while it reads it, keeps current_view_start/end on the left
pane's visible rows whichever pane is focused, reads the visible rows'
banners a row a frame (and the focused entry's before A acts on it) and
never in a draw, and takes the presses a busy moment would lose, but not
those the Source picker had. Its right pane reads its own listing and never touches
what the shared one feeds.

The right pane may hold a storage of its own, mounted outside devices[]. It
lets it go before control leaves the screen and before anything that mounts
or unmounts storage itself (menu_loop's Library and its empty-folder branch,
starting a file, Autoload, Recent, a Source change, either Other devices...,
B), a reset shuts it down, a read that fails mounts it once more, and Swiss's
box on its entries borrows the Source's slot and gives it back. Swiss's box
on either side keeps the other side's storage mounted.

Its Actions run Swiss's own file operations (manage_file_ex) with the
choice made and the other pane's folder as the destination: then Swiss's
destination picker, its mount and its folder chooser never run, and without
a preset they are exactly Swiss's. The landing name is built in one place;
a stopped or failed copy deletes its partial file at each of the four places
it can end early, on the destination only, never a folder of that name nor
anything on a memory card; a stopped Move keeps its original; Replace goes
on only once the file there is gone; a Move that can't take the original
off never runs, and one whose delete fails says so; the copy goes to the
other pane's folder; Delete still needs L held with A; Swiss's questions
keep their words; a result is said once both panes are read again. Every
rule has a mutant that must fail it.
"""

import hashlib

import re
import sys
from pathlib import Path

if not __debug__:
    raise SystemExit("audit_files_contract.py needs Python assertions; run it without -O")

ROOT = Path(__file__).resolve().parents[3]


def unlooked(text: str) -> str:
    """text with its strings as written, without Menu Language's lookups
    (UI_T("...") and UILang_Text("...")): the messages' meaning is checked,
    not the language they are drawn in."""
    return re.sub(r'(?:UI_T|UILang_Text)\(("(?:\\.|[^"\\])*")\)', r"\1", text)


SWISS = unlooked((ROOT / "cube/swiss/source/swiss.c").read_text(encoding="latin-1"))
FRAME = unlooked((ROOT / "cube/swiss/source/gui/FrameBufferMagic.c").read_text(encoding="latin-1"))
FILES = unlooked((ROOT / "cube/swiss/source/gui/ui_files.c").read_text(encoding="latin-1"))
TEST = (ROOT / "buildtools/ui/tests/test_ui_files.c").read_text()
# Swiss's lines kept under the presets, one tab in and without trailing
# blanks: its Z box (option = 0 where it declared it) and its destination
# picker, mount and folder chooser (destFile assigned where it declared it).
SWISS_BOX_SHA256 = "63ca2ad0f785996016ffce191f798465a1b00e7bc9cb3b7931fd935b00212783"
# Swiss's picker but for one line: its handle is the static destStore, so none
# of manage_file_ex's many ways out leaks it (2.4 vetting, M2).
SWISS_DESTINATION_SHA256 = "cd1125c5f3606832f516a8ef58a7b5f66c0056f990db6068d2726651113e91a0"


def function(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(marker)


def before(text: str, first: str, second: str, start: int = 0) -> None:
    a = text.index(first, start)
    assert a < text.index(second, a), f"{first!r} must come before {second!r}"


def check(swiss: str, frame: str, files: str = FILES) -> None:
    screen = function(swiss, "static uiDrawObj_t* renderFileList(")
    menu = function(swiss, "void menu_loop()")
    dispatch = function(menu, "if(devices[DEVICE_CUR] != NULL && curMenuLocation==ON_FILLIST)")
    manage = function(swiss, "static bool filesManageFrom(")
    step = function(swiss, "static bool filesMetaStep(")
    panes = function(swiss, "static bool filesPaneSnapshot(")
    info = function(swiss, "static bool filesInfoSnapshot(")
    focus = function(swiss, "static void filesLeftFocusName(")
    carousel = function(swiss, "uiDrawObj_t* renderFileCarousel(")

    # It browses the shared listing, the one scanFiles keeps.
    assert re.search(r"renderFileList\(getSortedDirEntries\(\),\s*getSortedDirEntryCount\(\), filePanel\)",
                     dispatch), "the File Browser reads another listing"
    # The meta thread: started once the page is up, stopped on every way out
    # and before any action on a file (filesActivate and filesManage stop it
    # themselves; filesManageFrom is the screen's own).
    before(screen, "filesPublish(directory, &filePanel, true)", "meta_thread_start(loadingBox);")
    assert screen.rstrip().endswith("return filePanel;\n}") and \
        "meta_thread_stop();\n\tDrawDispose(loadingBox);" in screen, "the meta thread outlives the screen"
    before(manage, "meta_thread_stop();", "manage_file_ex(")
    # Before an operation the left pane's open files are closed: FatFs won't
    # replace, delete or rename a file another handle holds.
    before(manage, "meta_thread_stop();", "filesCloseLeft();")
    before(manage, "filesCloseLeft();", "manage_file_ex(")
    # Every place that lets a page go clears filesPage first, or a later page
    # at the same address would pass for the File Browser's own.
    for let_go in re.finditer(r"(?:DrawRepublish|DrawDispose)\(\*?filePanel\b", swiss):
        start = swiss.rindex("\n", 0, let_go.start()) + 1
        line = swiss[start:swiss.index("\n", let_go.start())]
        above = swiss[swiss.rindex("\n", 0, start - 1) + 1:start - 1].strip()
        assert "filesPage = DrawRepublish(" in line or re.fullmatch(
            r"if\(\*?filePanel == filesPage\) filesPage = NULL;", above), \
            f"a page goes with filesPage still on it: {line.strip()}"
    # Z on "..": Autoload goes on with File Management and off always, from
    # either pane, as both panes' hints say.
    assert "(fileManagementAllowed() || filesAutoloadFolder(curDir.name))" in screen
    assert "if(fileManagementAllowed() || filesAutoloadFolder(filesOther.dir.name)) {" in screen
    # A memory card's rename is a stub: the File Browser never offers it.
    device_fn = function(swiss, "static void filesDevice(")
    assert "out->canRename = device->renameFile != NULL && !out->card;" in device_fn, \
        "a memory card offers a Rename that does nothing"
    # The meta thread's last step writes to the wheel: every way the screen
    # lets the wheel go (Detail from a game included) stops the thread first.
    for found in re.finditer(r"DrawDispose\(loadingBox\);", screen):
        assert re.search(r"meta_thread_stop\(\);\s*$", screen[:found.start()]), \
            "the wheel goes while the meta thread may still write to it"
    for action in ("static void filesActivate(", "static bool filesManage("):
        body = function(swiss, action)
        assert "manage_file()" not in body, f"{action} runs Swiss's box past filesManageFile"
        for call in ("load_file();", "filesManageFile("):
            if call in body:
                before(body, "meta_thread_stop();", call)
    # After an action on the right pane, scanFiles finds the left pane's
    # focus again, not the right entry's path.
    actions = function(swiss, "static bool filesActions(")
    right = actions[actions.index("\telse {\n\t\tstrlcpy(filesOther.focusName, name"):]
    before(actions, "filesManageFrom(", "filesLeftFocusName(directory, curFile.name, sizeof(curFile.name));")
    assert "filesLeftFocusName(directory, curFile.name, sizeof(curFile.name));" in right
    # Entries the meta thread may be filling are read under their lock.
    for body, name in ((panes, "rows"), (info, "info bar")):
        # Locked, or (refreshing while idle) given up when the entry is busy.
        assert re.search(r"if\(!wait && !trylockFile\(entry\)\) \{\s*return false;", body) and \
            re.search(r"if\(wait\) \{\s*lockFile\(entry\);", body) and \
            "unlockFile(entry);" in body, f"{name} reads unlocked"
    for body, name in ((focus, "focus name"), (step, "banner read")):
        assert re.search(r"(?<!un)lockFile\(", body) and "unlockFile(" in body, f"{name} reads unlocked"
    before(step, "lockFile(directory[i]);", "populate_meta(directory[i]);")
    # current_view_* is the left pane's window, whichever pane is focused.
    section = swiss[swiss.index(" * The File Browser: two panes side by side"):
                    swiss.index("static u32 gameflowSnapshotGeneration;")]
    assert not re.search(r"current_view_(?:start|end)\s*=", section), \
        "current_view_* set other than from UIFiles_LeftView"
    assert "&current_view_" not in section.replace(
        "UIFiles_LeftView(&filesState, &current_view_start, &current_view_end);", "")
    assert screen.count("UIFiles_LeftView(&filesState, &current_view_start, &current_view_end);") == 2
    # Banners: one visible row a frame from this thread, with a meta thread
    # or without (it keeps the meta cache's eviction going), the focused
    # entry's before A, and never while drawing.
    assert "FEAT_THREAD_SAFE" not in step and step.count("populate_meta(") == 1
    before(step, "populate_meta(directory[i]);", "return true;")
    press = function(screen, "if(buttons & BUTTON_A)")
    before(press, "populate_meta(directory[curSelection]);", "filesActivate(directory, false);")
    for draw in ("static void _DrawFiles(", "static void _FilesShapes(", "static void _FilesWords(",
                 "static void _FilesCube("):
        body = function(frame, draw)
        for banned in ("populate_meta", "lockFile", "GetTextSizeInPixels", "alloc(", "free("):
            assert banned not in body, f"{draw} {banned}"
    # The right pane's listing is its own.
    for reader in ("static void filesOtherRead(", "static void filesOtherOpen(",
                   "static void filesOtherRelease(", "static void filesOtherAcquire(",
                   "static void filesOtherFree(", "static bool filesOtherMount(",
                   "static void filesOtherChoose(", "static void filesOtherPick("):
        body = function(swiss, reader)
        for banned in ("scanFiles", "populate_meta", "meta_thread_", "current_view_",
                       "curSelection", "curFile", "curDir"):
            assert banned not in body, f"{reader} {banned}"
    assert "->readDir(&filesOther.dir, &filesOther.entries, -1)" in function(
        swiss, "static void filesOtherRead(")
    # Presses: taken from the scans; the old ones dropped on opening and
    # after a box, those made while a left folder is read kept.
    wait = screen[screen.index("\t\twhile(1) {\n\t\t\tu32 now"):screen.index("filesNote = NULL;\n\t\tactive")]
    assert "padsButtonsTaken(waitButtons);" in wait and "(void)padsButtonsTaken" not in wait
    before(screen, "if(!filesKeepPresses) {\n\t\t(void)padsButtonsTaken(waitButtons);",
           "\twhile(1) {")
    assert screen.count("filesKeepPresses = ") == 3, "presses kept for more than a folder change"
    # The Source picker reads held buttons: what it was given is not kept.
    change = function(menu, "if(needsDeviceChange) {\n\t\tDEVICEHANDLER_INTERFACE")
    before(change, "filesKeepPresses = false;", "select_device_internal(DEVICE_CUR)")
    before(screen, "/* A box (Z, Autoload, a storage menu) may have left presses of its\n\t\t * own. */",
           "(void)padsButtonsTaken(waitButtons);",
           screen.index("if(buttons & BUTTON_B)"))
    # B: the page leaves before Home is published.
    back = function(screen, "if(buttons & BUTTON_B)")
    before(back, "filesSnapshot.leaving = 1;", "filesWait(")
    before(back, "filesWait(", "filesHome();")
    # A folder being read says so, then the right listing goes before the
    # Source can.
    refresh = function(menu, "if(devices[DEVICE_CUR] != NULL && needsRefresh)")
    before(refresh, "DrawUpdateFilesReading(filePanel, UI_FILES_LEFT);", "scanFiles();")
    for opening in ("filesOtherOpen(NULL, NULL);", "filesOtherOpen(entry, \"\");"):
        at = screen.index(opening)
        assert "DrawUpdateFilesReading(filePanel, UI_FILES_RIGHT);" in screen[at - 120:at], opening
    before(refresh, "filesOtherRelease();", "->deinit(devices[DEVICE_CUR]->initial);")
    assert "if(curMenuLocation != ON_FILLIST || needsDeviceChange) {\n\t\tfilesOtherRelease();" in screen
    # The Library that can't draw a folder leaves it to the File Browser,
    # read again in fileComparator's order; the flag lasts one listing.
    fallback = carousel[carousel.index("if(!drawn) {\n\t\t\t/* No Library"):]
    fallback = fallback[:fallback.index("break;")]
    for token in ("memcpy(curFile.name, directory[curSelection]->name",
                  "gameflowListFallback = true;", "needsRefresh = 1;"):
        assert token in fallback, token
    # Swiss's lists are gone for good: no renderer, no row to draw them, and
    # the Library has no list of its own to fall back to.
    for gone in ("renderFileBrowser(", "renderFileFullwidth(", "drawFiles(", "drawFilesCarousel(",
                 "drawFilesFullwidth(", "drawCurrentDevice(", "drawCurrentDeviceCarousel(",
                 "FILES_PER_PAGE_FULLWIDTH", "DrawFileBrowserButton", "DrawFileCarouselEntry",
                 "EV_FILEBROWSERBUTTON", "drawFileBrowserButtonEvent_t"):
        assert gone not in swiss and gone not in frame, gone
    assert "useGameflow" not in carousel, "the Library keeps a branch for Swiss's carousel"
    assert screen.index("gameflowListFallback = false;") < screen.index("if(num_files<=0)")
    # The page covers the frame, so nothing behind it is drawn.
    assert "event->type == EV_FILES" in function(frame, "static bool _FrameCovered(")
    check_second_device(swiss, screen, menu, dispatch, refresh, manage)
    check_operations(swiss, screen, manage, actions, frame, files)
    check_detail(swiss, screen, frame)


def check_second_device(swiss: str, screen: str, menu: str, dispatch: str, refresh: str,
                        manage: str) -> None:
    release = "filesOtherRelease();"
    acquire = function(swiss, "static void filesOtherAcquire(")
    letgo = function(swiss, "static void filesOtherRelease(")
    read = function(swiss, "static void filesOtherRead(")
    pick = function(swiss, "static void filesOtherPick(")
    change = function(swiss, "static void filesSourceChange(")
    swap = function(swiss, "static bool filesSwapSides(")
    reset = function(swiss, "static s32 filesOtherOnReset(")
    # menu_loop: before the Library, which mounts and unmounts the
    # Configuration Device by itself, and before the empty folder's Source
    # goes. The scene requests are the Library's alone.
    library = dispatch[:dispatch.index("else {")]
    before(library, release, "renderFileCarousel(")
    assert library.index(release) < library.index("UIScene_Request("), "the Library is asked for first"
    assert "UIScene_Request" not in dispatch[dispatch.index("else {"):], "the File Browser asks for a scene"
    before(refresh, release, "->deinit(devices[DEVICE_CUR]->initial);")
    # It mounts its own and lets only its own go.
    assert re.search(r"if\(filesOther\.mount == UI_FILES_OWN\) \{\s*filesOther\.device->deinit\(", letgo)
    before(letgo, "filesOtherFree();", "->deinit(")
    assert "filesOther.mount = UI_FILES_UNMOUNTED;" in letgo
    mount = function(swiss, "static bool filesOtherMount(")
    assert re.search(r"if\(device == devices\[DEVICE_CUR\]\) \{\s*filesOther\.mount = UI_FILES_SHARED;\s*return true;", mount)
    before(mount, "deviceHandler_setStatEnabled(0);", "->init(device->initial);")
    before(mount, "->init(device->initial);", "deviceHandler_setStatEnabled(1);")
    # The read is the mount check: a failed read on its own mount mounts once more.
    assert re.search(r"if\(filesOther\.read < 0 && filesOther\.mount == UI_FILES_OWN\) \{"
                     r"[^}]*if\(filesOtherMount\(\)\) \{\s*filesOther\.read = device->readDir\(", read), \
        "a failed read isn't the mount check"
    for rule in ("filesOther.mount == UI_FILES_SHARED && filesOther.device != source",
                 "filesOther.mount == UI_FILES_UNMOUNTED"):
        assert rule in acquire, rule
    # In renderFileList: before starting a file, Autoload (a settings save),
    # Recent, either Other devices... and B; a Source change and Y let it go
    # in their helpers.
    arm = screen[screen.index("if(type == IS_SPECIAL && filesAtRoot(&curDir))"):]
    before(arm, "if(type == IS_FILE && loads) {\n\t\t\t\t\t\tfilesOtherRelease();", "filesActivate(directory, false);")
    toggles = [m.start() for m in re.finditer(r"filesToggleAutoload\(", screen)]
    assert len(toggles) == 2, "Autoload from somewhere else"
    for at in toggles:
        assert screen[:at].rstrip().endswith(release), "Autoload with the right pane's storage held"
        assert screen[at:].split("\n", 2)[1].strip() == "filesOtherAcquire();", "Autoload doesn't bring it back"
    recent = screen[screen.index("if((buttons & BUTTON_START)"):]
    before(recent, release, "filesRecent(false)")
    other = screen[screen.index("if(choice == filesMenu.devices) {"):]
    before(other, release, "needsDeviceChange = 1;")
    back = function(screen, "if(buttons & BUTTON_B)")
    before(back, release, "filesHome();")
    before(change, release, "sourceCommit(device);")
    before(swap, "UIFiles_CanSwap(", "sourceCommit(")
    before(swap, release, "sourceCommit(right);")
    assert re.search(r"if\(!UIFiles_CanSwap\([^)]*\)\) \{[^}]*return false;", swap), "Y onto a pane that isn't ready"
    assert "if(curMenuLocation != ON_FILLIST || needsDeviceChange) {\n\t\tfilesOtherRelease();" in screen
    # R, Other devices...: Swiss's destination picker with an empty slot,
    # the slot and the scene put back after.
    before(pick, release, "devices[DEVICE_DEST] = NULL;")
    before(pick, "devices[DEVICE_DEST] = NULL;", "select_device(DEVICE_DEST);")
    before(pick, "while(padsButtonsHeld() & PAD_BUTTON_A) VIDEO_WaitVSync();", "select_device(DEVICE_DEST);")
    before(pick, "select_device(DEVICE_DEST);", "devices[DEVICE_DEST] = dest;")
    before(pick, "select_device(DEVICE_DEST);", "UIScene_Request(filesScene);")
    assert "devices[DEVICE_DEST] = chosen" not in pick, "the pane's storage kept in DEVICE_DEST"
    # A reset shuts down the pane's own storage: registered from swiss.c,
    # after the interface stops reading (priority 1, as deviceHandler.c's).
    assert re.search(r"__attribute__\(\(constructor\)\)\s*static void \w+\(void\)\s*\{\s*"
                     r"SYS_RegisterResetFunc\(&filesOtherResetInfo\);", swiss), "no reset function"
    assert re.search(r"static sys_resetinfo filesOtherResetInfo = \{\s*\{NULL, NULL\}, filesOtherOnReset, 1\s*\};",
                     swiss), "the reset function's priority"
    assert re.search(r"if\(!final && filesOther\.mount == UI_FILES_OWN && filesOther\.device != NULL &&\s*"
                     r"!\(filesOther\.device->quirks & QUIRK_NO_DEINIT\)\) \{\s*filesOther\.device->deinit\(",
                     reset), "the reset shuts down what it shouldn't, or nothing"
    # An action on a right-pane entry borrows the Source's slot for the
    # pane's storage and the destination's for the Source, and gives both
    # back on its one way out; nothing is mounted or unmounted meanwhile.
    before(manage, "devices[DEVICE_CUR] = fromRight ? filesOther.device : cur;", "manage_file_ex(")
    before(manage, "devices[DEVICE_DEST] = fromRight ? cur : filesOther.device;", "manage_file_ex(")
    before(manage, "manage_file_ex(", "devices[DEVICE_CUR] = cur;")
    before(manage, "manage_file_ex(", "devices[DEVICE_DEST] = dest;")
    assert "manage_file()" not in manage and "->init(" not in manage and "->deinit(" not in manage
    # Swiss's box keeps the other side's storage: the Source for a right
    # entry, else the right pane's own mount; never in DEVICE_DEST, where the
    # destination picker unmounts it.
    keeper = function(swiss, "static bool filesManageFile(DEVICEHANDLER_INTERFACE *keep)\n{")
    assert re.search(r"if\(keep == NULL && filesOther\.mount == UI_FILES_OWN\) \{\s*keep = filesOther\.device;",
                     keeper), "the right pane's own storage isn't kept"
    assert re.search(r"if\(keep != NULL && devices\[DEVICE_DEST\] == keep\) \{\s*devices\[DEVICE_DEST\] = NULL;",
                     keeper), "the kept storage left in DEVICE_DEST"
    before(keeper, "devices[DEVICE_DEST] = NULL;", "manage_file();")
    before(keeper, "manageKeep = keep;", "manage_file();")
    before(keeper, "manage_file();", "manageKeep = NULL;")
    assert len(re.findall(r"(?<![\w])manage_file\(\)", swiss[swiss.index(" * The File Browser: two panes side by side"):
                                                     swiss.index("static u32 gameflowSnapshotGeneration;")])) == 1
    # Choosing another storage lets the old one go, including a clash reset.
    choose = function(swiss, "static void filesOtherChoose(")
    before(choose, release, "filesOther.device = device;")
    assert re.search(r"if\(filesOther\.device == NULL \|\| filesClash\([^)]*\)\) \{\s*filesOtherRelease\(\);",
                     acquire), "a clash reset keeps the old storage mounted"
    # The first device: the Source without File Management.
    assert "UIFiles_RightOnConfig(fileManagementAllowed()," in function(
        swiss, "static DEVICEHANDLER_INTERFACE *filesOtherDefault("), "a second mount with nothing to do"
    # Swiss's picker greys nothing: a clash keeps the pane and says why.
    before(pick, "filesClash(chosen, devices[DEVICE_CUR], filesNoteText", "filesOtherChoose(chosen);")
    assert re.search(r"filesNote = filesNoteText;\s*chosen = NULL;", pick), "the picker's clash is silent"
    # A greyed storage can't be chosen.
    assert "filesBox(UI_FILES_HINTS_BOX, BUTTON_L | BUTTON_R, false);" in function(
        swiss, "static int filesStorageMenu("), "the storage menu isn't a box"
    assert "if(pick >= 0 && ((box->dim >> pick) & 1u)) {" in function(swiss, "static int filesBox("), \
        "A chooses a greyed item"
    # L's device becomes the Source in place, mounted, and X at the top
    # opens the left menu rather than Swiss's Source picker.
    before(change, "sourceCommit(device);", "(void)sourceMount();")
    assert re.search(r"if\(filesMenuDevices\[choice\] != devices\[DEVICE_CUR\]\) \{\s*"
                     r"filesSourceChange\(filesMenuDevices\[choice\]\);\s*break;", screen), "L's choice does nothing"
    before(screen, "else if((buttons & PAD_BUTTON_X) && filesAtRoot(&curDir)) {\n\t\t\t\tstorage = UI_FILES_LEFT;",
           "else if(buttons & PAD_BUTTON_X) {\n\t\t\t\tfilesUp(directory[0]);")
    assert manage.count("return") == 1 and manage.index("devices[DEVICE_CUR] = cur;") < manage.index("return")
    assert manage.index("devices[DEVICE_DEST] = dest;") < manage.index("return")
    manager = function(swiss, "bool manage_file_ex(int preset, const char *destDir) {")
    assert manager.count("devices[DEVICE_DEST] != devices[DEVICE_CUR] && devices[DEVICE_DEST] != manageKeep") == 2
    # L and R choose storage; only the C-stick pages.
    for page in ("UI_FILES_INPUT_PAGE_UP", "UI_FILES_INPUT_PAGE_DOWN"):
        at = screen.index(page)
        assert "BUTTON_L" not in screen[at - 80:at] and "BUTTON_R" not in screen[at - 80:at], page
    # The Source changes through the picker's own two steps.
    commit = function(swiss, "static void sourceCommit(DEVICEHANDLER_INTERFACE *device)\n{")
    for step in ("freeFiles();", "DrawGameflowCancelPosters();", "->deinit(devices[DEVICE_CUR]->initial);",
                 "devices[DEVICE_CUR] = device;"):
        assert step in commit, step
    assert "sourceCommit(selectedDevice);" in function(swiss, "static bool select_device_internal(")
    assert "!sourceMount()" in menu


def kept(block: str) -> str:
    """Swiss's lines as they were: one tab less, no trailing blanks, and its
    strings as written, without Menu Language's lookups (UI_T("..."))."""
    block = re.sub(r'UI_T\(("(?:\\.|[^"\\])*")\)', r"\1", block)
    return "\n".join((line[1:] if line.startswith("\t") else line).rstrip()
                     for line in block.split("\n"))


def check_operations(swiss: str, screen: str, manage: str, actions: str, frame: str,
                     files: str) -> None:
    ex = function(swiss, "bool manage_file_ex(int preset, const char *destDir) {")
    # Swiss's callers ask; the File Browser presets.
    assert re.search(r"bool manage_file\(\) \{\s*return manage_file_ex\(MANAGE_ASK, NULL\);\s*\}", swiss)
    assert "int option = preset;\n\tif(preset == MANAGE_ASK) {\n" in ex, "Swiss's box asks over a preset"
    box = ex[ex.index("\tif(preset == MANAGE_ASK) {\n") + len("\tif(preset == MANAGE_ASK) {\n"):]
    box = box[:box.index("\n\t}\n")]
    assert hashlib.sha256(kept(box).encode()).hexdigest() == SWISS_BOX_SHA256, "Swiss's box changed"
    # With a destination, Swiss's picker, its mount and its folder chooser
    # never run; without one they are Swiss's, unchanged.
    opening = "\t\tif(destDir == NULL) {\n"
    picker = ex[ex.index(opening) + len(opening):]
    picker = picker[:picker.index("\n\t\t}\n\t\telse {\n\t\t\tdestFile = memset(&destStore")]
    assert hashlib.sha256(kept(picker).encode()).hexdigest() == SWISS_DESTINATION_SHA256, \
        "Swiss's destination picker, mount or folder chooser changed"
    rest = ex.replace(picker, "")
    for step in ("select_device(", "->init(", "select_dest_dir("):
        assert step in picker and step not in rest, f"{step} runs with a preset destination"
    assert re.search(r"else \{\s*destFile = memset\(&destStore, 0, sizeof\(destStore\)\);\s*"
                     r"strlcpy\(destFile->name, destDir, PATHNAME_MAX\);", ex), "the preset folder isn't used"
    # One handle, never allocated: no way out of manage_file_ex leaks it.
    assert "static file_handle destStore;" in ex and "destFile = calloc" not in ex and "free(destFile)" not in ex, \
        "manage_file_ex allocates its destination handle again"
    # The landing name is built in one place, which the screen predicts with.
    assert swiss.count("stripInvalidChars(getRelativeName(") == 1
    namer = function(swiss, "DEVICEHANDLER_INTERFACE *srcDev, DEVICEHANDLER_INTERFACE *destDev)\n{")
    assert "stripInvalidChars(getRelativeName(" in namer and 'strlcat(out, ".gci", PATHNAME_MAX);' in namer
    assert "manageDestName(destFile->name, destFile->name, curFile.name," in ex
    assert "manageDestName(landing, thereDir, entry->name, here, there);" in actions
    # A stopped or failed copy deletes its partial file, on the destination,
    # at each of the four places it ends early, and only those.
    drop = function(swiss, "static bool manageDropPartial(file_handle *destFile)")
    assert "DEVICEHANDLER_INTERFACE *dest = devices[DEVICE_DEST];" in drop
    assert "DEVICE_CUR" not in drop
    before(drop, "dest->closeFile(destFile);", "dest->deleteFile(destFile)")
    last = "return dest->deleteFile != NULL && dest->deleteFile(destFile) == 0;"
    assert drop.count("deleteFile(") == 1 and last in drop, "the partial file isn't deleted"
    # A card's delete goes by the save's own name, and its writes report
    # short counts for saves that are whole: a card keeps what it has.
    assert re.search(r"if\(dest == &__device_card_a \|\| dest == &__device_card_b\) \{\s*return false;", drop)
    before(drop, "dest == &__device_card_a", last)
    # A folder of that name is no partial file.
    assert re.search(r"dest->statFile\(&there\) == 0 && there\.fileType == IS_DIR\) \{\s*return true;", drop)
    before(drop, "there.fileType == IS_DIR", last)
    assert ex.count("manageDropPartial(destFile)") == 4 and swiss.count("manageDropPartial(") == 5
    for message in ('"Failed to Read! (%d %d)', '"Failed to Write! (%d %d)', '"Failed to Write! (%d)\\n'):
        at = ex.index(message)
        site = ex[:ex.rfind("\n", 0, at)].rstrip().rsplit("\n", 1)[1].strip()
        # A read that fails before the first write made no file: nothing
        # to delete. A failed check is never proof that nothing is there.
        drops = "bool removed = !written || manageDropPartial(destFile);" \
            if message.startswith('"Failed to Read!') else "bool removed = manageDropPartial(destFile);"
        assert site == drops, f"{message}: the partial file stays"
        assert "return true;" in ex[at:at + 400]
    assert "cancelled = 0, written = 0, chunkSize" in ex and ex.count("written = 1;") == 1
    assert "\t\t\t\twritten = 1;\n\t\t\t\tret = devices[DEVICE_DEST]->writeFile(destFile, readBuffer, amountToCopy);" \
        in ex, "a write goes out unrecorded"
    assert re.search(r"devices\[DEVICE_DEST\]->closeFile\(destFile\);\n(\t+/\*[^\n]*\*/\n)?"
                     r"\t+bool removed = !written \|\| manageDropPartial\(destFile\);\n"
                     r"\t+sprintf\(txtbuffer, \"Failed to Read!", ex), "a failed read leaves its file open"
    stop = "bool removed = cancelled && manageDropPartial(destFile);"
    assert stop in ex, "a stopped copy keeps its partial file, or a finished one loses its file"
    before(ex, "ret = devices[DEVICE_DEST]->writeFile(destFile, NULL, 0);", stop)
    # B tapped while the last piece was read or written, or the file closed,
    # still stops it: looked at again before the partial file and the Move's
    # original are decided.
    late = ("\t\t\tif((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) {\n"
            "\t\t\t\tcancelled = 1;\n\t\t\t}\n")
    assert late in ex, "B in the last piece of a copy is missed"
    before(ex, "ret = devices[DEVICE_DEST]->writeFile(destFile, NULL, 0);", late)
    before(ex, late, stop)
    # A file there that can't be read is there all the same: the question
    # comes from the existence check Keep both uses (statFile, else open).
    assert "\t\tif(manageDestExists(destFile)) {\n" in ex and "readFile(destFile, NULL, 0)" not in ex, \
        "a file there that can't be read is written over without the question"
    before(ex, "if(manageDestExists(destFile)) {", "filesAskExists(destFile->name)")
    before(ex, stop, "manageCopied(result, canDelete || cancelled ? option : COPY_OPTION,")
    # A stopped Move keeps its original: it is deleted only past a copy
    # that wasn't stopped.
    assert re.search(r"if\(!cancelled\) \{\s*// If cut, delete from source device\s*"
                     r"if\(canDelete && option == MOVE_OPTION\) \{\s*"
                     r"kept = devices\[DEVICE_CUR\]->deleteFile\(&curFile\) != 0;", ex), \
        "a stopped Move deletes its original"
    assert ex.count("devices[DEVICE_CUR]->deleteFile(") == 1
    # Replace it: the copy goes on only once the file there is gone.
    assert re.search(r"if\(devices\[DEVICE_DEST\]->deleteFile\(destFile\) != 0\) \{\s*"
                     r"DrawDispose\(dupeBox\);\s*manageTell\(D_FAIL,[^;]*;\s*"
                     r"return false;\s*\}\s*replaced = true;", ex), "Replace copies over a file it couldn't remove"
    assert ex.count("devices[DEVICE_DEST]->deleteFile(") == 1
    assert ex.count(", removed,\n") == 3 and ex.count("replaced, D_FAIL, txtbuffer);") == 3
    assert "removed, replaced, D_INFO, message);" in ex, "a stopped Replace doesn't say the old file is gone"
    # A Move only runs when the box allowed it (the box greys one whose
    # original can't be taken off), and one whose delete fails says so.
    assert re.search(r"if\(action < 0 \|\| !avail\.enabled\[action\]\) \{\s*return false;", actions), \
        "a greyed action runs"
    assert "move && !(from->canWrite && (renames || from->canDelete))" in function(
        files, "void UIFiles_Availability("), "a Move offered where the original stays"
    assert "kept = devices[DEVICE_CUR]->deleteFile(&curFile) != 0;" in ex
    assert "result = kept ? UI_FILES_RESULT_KEPT : UI_FILES_RESULT_DONE;" in ex
    assert "manageCopied(result, canDelete || cancelled ? option : COPY_OPTION," in ex, \
        "a Move that couldn't take the original off says it moved"
    # manage_file_ex's permissions are the ones the availability table test
    # checks UIFiles_Availability against, line for line.
    flags = re.findall(r"^\tbool can\w+ = .*;$", ex, re.M)
    assert len(flags) == 6 and all(line in TEST for line in flags), "manage_file's permissions moved"
    # Delete: Swiss's words, and only L held with A deletes; A alone on
    # Delete doesn't; L, A and B are let go before Swiss goes on.
    asks = function(swiss, "static bool filesAskDelete(")
    assert "filesBox(UI_FILES_HINTS_DELETE, 0u, true) == 0" in asks
    runner = function(swiss, "static int filesBox(")
    assert "chorded = chord && (padsButtonsHeld() & (BUTTON_A | BUTTON_L)) == (BUTTON_A | BUTTON_L);" in runner
    assert "(pressed & BUTTON_A) && !(chord && focus == 0)" in runner, "A alone deletes"
    assert re.search(r"if\(chord\) \{\s*buttons \|= BUTTON_L;", runner)
    assert "while(padsButtonsHeld() & (buttons & ~(BUTTON_UP | BUTTON_DOWN)));" in runner
    assert "((box->dim >> pick) & 1u)" in runner, "a greyed item is chosen"
    for words in ('"Delete this file?\\n \\nPress L + A to continue, or B to cancel."',
                  '"Delete this folder and all it holds?\\n \\nPress L + A to continue, or B to cancel."'):
        assert swiss.count(words) == 2, f"Delete's words differ: {words}"
    assert re.search(r"if\(option == DELETE_OPTION && filesBoxes && !filesAskDelete\(", ex)
    assert "if(option == DELETE_OPTION && !filesBoxes) {" in ex
    # Swiss's questions keep their words; the File Browser only draws them.
    for words in ('"Move this file?\\nIt is removed from here once copied.\\nA  MOVE    B  CANCEL"',
                  '"Hide this file?\\nIt shows only with Show hidden files on.\\nA  HIDE    B  CANCEL"',
                  '"Hide this folder?\\nIt shows only with Show hidden files on.\\nA  HIDE    B  CANCEL"',
                  '"Open this folder at every start?\\nHome is skipped until you turn it off.\\nA  AUTOLOAD    B  CANCEL"',
                  '"Update the FlippyDrive with this file?\\nA  UPDATE    B  CANCEL"',
                  '"Write this file to the WiiKey\'s flash?\\nA  FLASH    B  CANCEL"'):
        assert swiss.count(words) == 1, words
    confirm = function(swiss, "static bool confirmAction(const char *text)")
    assert "int asked = filesBoxes ? filesAsk(text) : -1;" in confirm
    # filesBoxes holds only while the screen runs Swiss's operation.
    before(manage, "filesBoxes = true;", "manage_file_ex(")
    before(manage, "manage_file_ex(", "filesBoxes = false;")
    # File exists: the box's choice is Swiss's button; Keep both greyed when
    # only replacing makes room.
    assert "u32 buttons = chosen ? chosen : padsButtonsHeld();" in ex
    assert "filesFitsBoth = !avail.replaceOnly[action];" in actions
    assert "UIFiles_ExistsChoices(filesFitsBoth, &choices);" in function(swiss, "static u32 filesAskExists(")
    # The screen's operations go through filesManageFrom alone; A on a file
    # that doesn't start here opens Actions rather than Swiss's box.
    assert "filesManageEntry" not in swiss
    assert screen.count("filesActions(directory, UI_FILES_LEFT)") == 2
    assert screen.count("filesActions(directory, UI_FILES_RIGHT)") == 1
    assert actions.count("filesManageFrom(") == 1
    # Copy and Move go to the other pane's folder, and a same-named folder
    # there, or a memory card, greys them (UIFiles_Availability).
    assert "changed = filesManageFrom(entry, pane, options[action], thereDir);" in actions
    assert "const char *thereDir = left ? filesOther.dir.name : curDir.name;" in actions
    assert "what.existsFolder = thereList[i]->fileType == IS_DIR;" in actions
    assert "out->card = device == &__device_card_a || device == &__device_card_b;" in \
        function(swiss, "static void filesDevice(")
    # A result waits until both panes are read again: Swiss's operation
    # only keeps it, the page says it once it is published, and at once
    # when nothing changed.
    tell = function(swiss, "static void manageTell(")
    copied = function(swiss, "static void filesSayCopy(")
    assert "filesSayLater(title, detail, failed);" in tell and "filesSay(" not in tell
    assert "filesSayLater(lines[0], lines[1]," in copied and "filesSay(" not in copied
    assert swiss.count("filesSayPending();") == 2
    before(screen, "if(!filesPublish(directory, &filePanel, true)) {", "filesSayPending();")
    before(screen, "filesSayPending();", "meta_thread_start(loadingBox);")
    assert re.search(r"if\(!changed\) \{\s*filesSayPending\(\);", actions)
    # The progress card says what goes where and B Stop.
    assert "filesBoxes ? filesProgress(option, destFile->name) :" in ex
    assert "if(data->files) {\n\t\t_DrawHintText(x1 + 16, y2 - 12, \"B  Stop\"" in frame
    # The message and the ghost row are drawn, never measured, in a draw.
    for draw in ("static void _FilesMessage(", "static void _FilesMenu("):
        body = function(frame, draw)
        assert "GetTextSizeInPixels" not in body and "alloc(" not in body, draw


def check_detail(swiss: str, screen: str, frame: str) -> None:
    """A on a game in the left pane opens Indigo's Game Detail through the
    Library's own image path, so the MemCard PRO's GameID goes out where it
    always did, before Detail shows. The File Browser's page stays up until
    Detail is ready, the event then takes its place already in Detail (no
    card flies in), and the page comes back settled over it before it turns
    back to the Library; B comes back to the same row and the scene the File
    Browser opened over. A poster still on its way when Detail shows lands
    while it is up."""
    opener = function(swiss, "static void filesOpenDetail(file_handle **directory, uiDrawObj_t **filePanel)\n{")
    opens = function(swiss, "static bool filesOpensDetail(const file_handle *entry)\n{")
    show = function(swiss, "static void gameflowShowFromFiles(")
    back = function(swiss, "static void gameflowBackToFiles(")
    image = function(swiss, "static bool gameflowLoadImageWithContext(")
    detail = function(swiss[swiss.rindex("static int gameflow_info_game"):], "static int gameflow_info_game")
    publish = function(swiss, "static bool gameflowPublishDetail(")
    load = function(swiss, "static void load_game_with_context(gameflowLaunchContext_t *context) {")
    # Which entries: a disc image on a Source that starts them, decided under
    # the entry's lock; the right pane's games still start on the left.
    assert "UIGameflowLibrary_IsGameImageName(entry->name)" in opens and "FEAT_BOOT_GCM" in opens
    assert "entry->fileType == IS_FILE" in opens
    press = function(screen, "if(buttons & BUTTON_A)")
    before(press, "lockFile(directory[curSelection]);", "detail = loads && filesOpensDetail(directory[curSelection]);")
    before(press, "detail = loads && filesOpensDetail(directory[curSelection]);", "unlockFile(directory[curSelection]);")
    assert re.search(r"else if\(detail\) \{[^}]*filesOpenDetail\(directory, &filePanel\);\s*break;", press), \
        "A on a game doesn't open its Detail"
    assert swiss.count("filesOpenDetail(directory, &filePanel);") == 1, "Detail from somewhere else"
    # Nothing else mounted, the meta thread stopped, before anything reads.
    before(opener, "meta_thread_stop();", "filesOtherRelease();")
    before(opener, "filesOtherRelease();", "gameflowFreshHandle(entry);")
    # A valid disc header first, as the Library proves before its posters.
    before(opener, "if(gameflowReadResolverHeader(entry, &header)) {", "memalign(")
    before(opener, "filesDetailSnapshot(snapshot, entry, &header);", "DrawGameflowRequestPosters(")
    before(opener, "DrawGameflowRequestPosters(", "event = DrawGameflow(snapshot);")
    before(opener, "DrawGameflowRequestPosters(", "DrawGameflowPrimePoster();")
    before(opener, "DrawGameflowPrimePoster();", "event = DrawGameflow(snapshot);")
    # A poster that one read can't land (a slot just let go, the pack only
    # now opened) is read on Detail's idle retraces, as the Library's, and
    # fades in: the banner standing in for it never stays.
    # (The memory card slots are followed in the same idle branch, after it.)
    assert re.search(r"if\(buttons == 0u\) \{\s*DrawGameflowPollPosters\(\);"
                     r"(?:[^{}]|\{[^{}]*\})*\}\s*"
                     r"\} while\(buttons == 0u\);", detail), "Detail's poster never lands"
    # The Library's image path, flagged; it never shows the event itself.
    before(opener, "gameflowFilesPage = *filePanel;", "gameflowLoadImageWithContext(")
    before(opener, "gameflowFromFiles = true;", "gameflowLoadImageWithContext(")
    before(opener, "gameflowLoadImageWithContext(", "gameflowFromFiles = false;")
    for banned in ("DrawPublish(", "DrawRepublish(", "DrawSetGameflowMode", "gameID_", "load_game"):
        assert banned not in opener, f"filesOpenDetail {banned}"
    assert re.search(r"if\(gameflowFilesShown\) \{\s*DrawDispose\(event\);\s*\}\s*else \{\s*"
                     r"DrawDiscard\(event\);", opener), "the event outlives Detail"
    before(opener, "gameflowFromFiles = false;", "*filePanel = filesPage = gameflowFilesPage;")
    assert re.search(r"if\(!handled\) \{\s*filesActivate\(directory, false\);", opener), "no way to Swiss's own"
    assert opener.rstrip()[:-1].rstrip().endswith("UIScene_Request(filesScene);"), "the File Browser's scene"
    # In: the mode is there already, then the event takes the page's place.
    assert re.search(r"if\(!gameflowFromFiles \|\| gameflowFilesShown\) \{\s*"
                     r"DrawSetGameflowMode\(event, mode\);\s*return;", show)
    before(show, "DrawSetGameflowModeNow(event, mode);", "DrawRepublish(gameflowFilesPage, event);")
    # ... once the cube is behind it: Detail draws nothing before, and the
    # Home cube would show between the page and Detail.
    assert re.search(r"vsync < 60 && UIScene_Frame\(\)->libraryReveal < 1\.0f;\s*vsync\+\+\) \{\s*"
                     r"VIDEO_WaitVSync\(\);", show), "the Home cube between the page and Detail"
    before(show, "UIScene_Frame()->libraryReveal", "DrawRepublish(gameflowFilesPage, event);")
    assert "gameflowFilesShown = true;" in show
    assert "gameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_DETAIL);" in detail
    assert "UI_GAMEFLOW_MODE_DETAIL" not in detail.replace(
        "gameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_DETAIL);", ""), "Detail shown another way"
    assert re.search(r"if\(context != NULL\) \{\s*gameflowShowFromFiles\(context->event, "
                     r"UI_GAMEFLOW_MODE_LAUNCH\);", load), "Boot without prompts' launch flies in"
    assert swiss.count("gameflowShowFromFiles(") == 3
    # The GameID: sent before Detail or the launch screen first shows, as
    # from the Library, through card_slots.c (indigo#102).
    before(load, "CardSlots_RequestGame(&GCMDisk);", "gameflow_info_game(config, context)")
    before(load, "CardSlots_RequestGame(&GCMDisk);", "gameflowShowFromFiles(")
    assert swiss.count("CardSlots_RequestGame(") == 1
    assert "gameID_early_set(" not in swiss
    # Out: the page, settled and on top, before each turn back to the
    # Library while the event is on screen.
    assert re.search(r"if\(gameflowFromFiles && gameflowFilesPage == NULL\) \{\s*"
                     r"gameflowFilesPage = DrawFilesSettled\(&filesSnapshot\);", back), "no settled page"
    assert "DrawPublish(gameflowFilesPage);" in back, "the page isn't put on top"
    for body, mode in ((detail, "DrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LIBRARY);"),
                       (image, "DrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);")):
        assert body.count("UI_GAMEFLOW_MODE_LIBRARY") == 1
        assert body[:body.index(mode)].rstrip().endswith("gameflowBackToFiles();"), "the Library seen leaving"
    # Detail says where B goes.
    assert re.search(r"if\(gameflowFromFiles\) \{\s*flags \|= UI_GAMEFLOW_DETAIL_BACK_FILES;", publish)
    # Z turns Autoload on only after Swiss's question, as on Swiss's game
    # screen, which the File Browser opened before.
    assert re.search(r"action == UI_GAMEFLOW_DETAIL_ACTION_AUTOLOAD &&\s*\(!gameflowFromFiles \|\|\s*"
                     r"autoloadToggleConfirmed\(curFile\.name, false\)\)\) \{", detail), "Autoload asks nothing"
    # A folder's second disc as Swiss chose it: the one named as disc 2.
    opposite = function(swiss, "static file_handle *gameflowFindOppositeImage(")
    assert "&oppositeHeader, gameflowFromFiles);" in image, "the Library's rule for the File Browser's disc 2"
    assert re.search(r"if\(match != NULL && !swissChoice\) \{", opposite)
    assert re.search(r"if\(swissChoice && UIGameflowResolver_NamedAsOppositeDisc\(image->name,\s*"
                     r"primaryHeader->discNumber, candidate->name,\s*candidateHeader\.discNumber\)\) \{\s*"
                     r"break;", opposite), "no name tie-break"
    # The hint says Details only where A opens them.
    info = function(swiss, "static bool filesInfoSnapshot(")
    assert re.search(r"if\(kind == UI_FILES_KIND_DISC && pane == UI_FILES_LEFT &&\s*!filesOpensDetail\(entry\)\) \{"
                     r"\s*kind = UI_FILES_KIND_OTHER;", info), "A  Details where A doesn't open them"
    before(info, "kind = UI_FILES_KIND_OTHER;", "UIFiles_Hints(")
    # The drawing side: a mode set with no motion, a page with no opening.
    now = function(frame, "bool DrawSetGameflowModeNow(")
    assert "_GameflowSetMode(evt, mode, UI_MOTION_OFF)" in now
    settled = function(frame, "uiDrawObj_t* DrawFilesSettled(")
    assert re.search(r"->seconds = 60\.0f;", settled), "the page opens again"
    assert "clearNestedEvent(evt);" in function(frame, "void DrawDiscard(")
    # A box or the message has the info bar only while it is up: the focused
    # entry's lines come back when it goes, so none stays under the next
    # question or the progress card.
    box = function(swiss, "static int filesBox(")
    before(box, "filesInfoKeep(&entry);", "box->open = 1;")
    assert re.search(r"box->open = 0;\s*filesInfoPut\(&entry\);\s*filesMenuShow\(\);", box), \
        "a box's lines stay after it closes"
    say = function(swiss, "static void filesSay(")
    before(say, "filesInfoKeep(&entry);", "filesSnapshot.line[0][0] = filesSnapshot.line[1][0] = '\\0';")
    assert re.search(r"message\[1\]\[0\] = '\\0';\s*filesInfoPut\(&entry\);\s*"
                     r"\(void\)DrawUpdateFiles\(filesPage, &filesSnapshot\);", say), "the message's lines stay"
    # The message fades out too, unless UI Motion is Off.
    assert re.search(r"!= UI_MOTION_OFF\) \{\s*filesSnapshot\.messageLeaving = 1;", say), "the message doesn't fade out"
    # A copy that landed flashes under its message.
    pending = function(swiss, "static void filesSayPending(")
    before(pending, "|= UI_FILES_ROW_FLASH;", "filesSay(filesLater.title")
    # Y: the panes' contents go once the swap is allowed, and come back with
    # the screen's next page.
    swap = function(swiss, "static bool filesSwapSides(")
    before(swap, "if(!UIFiles_CanSwap(", "filesSnapshot.swapping = 1;")
    before(swap, "filesSnapshot.swapping = 1;", "filesLeftFocusName(")
    before(screen, "filesSnapshot.swapping = 0;", "if(!filesPublish(directory, &filePanel, true)) {")
    draw = function(frame, "static void _DrawFiles(")
    assert "UIFiles_SwapStep(data->content, s->swapping != 0u," in draw, "Y doesn't fade the panes"
    # A read after Y (a disc spinning up, a share) says so while the
    # swapped-out rows are gone: "Reading..." and its cells go with the
    # chrome, not the contents.
    shapes = function(frame, "static void _FilesShapes(")
    reading = shapes[shapes.index("if(pane->reading) {"):]
    assert "{18, 27, 91, 230}, alpha)" in reading and "alpha * (i == lit ? 1.0f : 0.35f)" in reading, \
        "the loading cells fade out with a swap"
    assert "float said = pane->reading ? alpha : inner;" in function(frame, "static void _FilesWords("), \
        "Reading... fades out with a swap"
    # The loading cells step as UI Motion says: 5 Hz, 2 Hz, still.
    assert re.search(r"lit = motion == UI_MOTION_OFF \? 0 :\s*\(int\)\(data->seconds \* "
                     r"\(motion == UI_MOTION_REDUCED \? 2\.0f : 5\.0f\)\) % 3;", draw), \
        "the loading cells ignore UI Motion"
    # The pane without the focus says its words in the light weight.
    words = function(frame, "static void _FilesWords(")
    assert "active >= 0.5f ? drawStringMedium : drawString;" in words


check(SWISS, FRAME)
# Nor do their declarations, their one font helper or their pictures.
for path, gone in (("cube/swiss/include/swiss.h", ("renderFileBrowser(", "drawFiles(", "FILES_PER_PAGE_FULLWIDTH")),
                   ("cube/swiss/source/gui/FrameBufferMagic.h", ("DrawFileBrowserButton", "DrawFileCarouselEntry",
                                                                 "TEX_STAR")),
                   ("cube/swiss/source/gui/IPLFontWrite.c", ("drawStringEllipsis", "GetCharsThatFitInWidth")),
                   ("cube/swiss/source/images/images.scf", ("banner_mask", "dirimg", "gcmimg")),
                   ("cube/swiss/source/images/buttons/buttons.scf", ("star_16",))):
    text = (ROOT / path).read_text()
    for name in gone:
        assert name not in text, f"{path}: {name}"

MUTANTS = (
    ("left files stay open for an operation", SWISS,
     "\tmeta_thread_stop();\n\tfilesCloseLeft();\n", "\tmeta_thread_stop();\n"),
    ("a memory card offers Rename", SWISS,
     "out->canRename = device->renameFile != NULL && !out->card;",
     "out->canRename = device->renameFile != NULL;"),
    ("the wheel goes before the meta thread stops", SWISS,
     "\t\t\t\t\tmeta_thread_stop();\n\t\t\t\t\tDrawDispose(loadingBox);\n\t\t\t\t\tloadingBox = NULL;",
     "\t\t\t\t\tDrawDispose(loadingBox);\n\t\t\t\t\tloadingBox = NULL;"),
    ("the meta thread runs into manage_file", SWISS,
     "\tmeta_thread_stop();\n\tfilesCloseLeft();\n\tdevices[DEVICE_CUR] = fromRight ? filesOther.device : cur;",
     "\tfilesCloseLeft();\n\tdevices[DEVICE_CUR] = fromRight ? filesOther.device : cur;"),
    ("the left focus is not put back", SWISS,
     "\t\tfilesLeftFocusName(directory, curFile.name, sizeof(curFile.name));\n", ""),
    ("rows read unlocked", SWISS, "\t\t\tif(wait) {\n\t\t\t\tlockFile(entry);\n\t\t\t}\n\t\t}\n\t\trow->kind",
     "\t\t}\n\t\trow->kind"),
    ("the view follows the right pane", SWISS,
     "\tUIFiles_LeftView(&filesState, &current_view_start, &current_view_end);\n\tfilesLeftListing++;",
     "\tcurrent_view_start = 0;\n\tfilesLeftListing++;"),
    ("banners left to a meta thread", SWISS,
     "static bool filesMetaStep(file_handle **directory)\n{\n",
     "static bool filesMetaStep(file_handle **directory)\n{\n\tif(devices[DEVICE_CUR]->features & FEAT_THREAD_SAFE) return false;\n"),
    ("A before the focused entry's meta", SWISS,
     "\t\t\t\tpopulate_meta(directory[curSelection]);\n\t\t\t\ttype", "\t\t\t\ttype"),
    ("the picker's presses kept", SWISS,
     "\t\tfilesKeepPresses = false;\n\t\thomePublish(", "\t\thomePublish("),
    ("no word while the right pane reads", SWISS,
     "\t\t\t\t(void)DrawUpdateFilesReading(filePanel, UI_FILES_RIGHT);\n\t\t\t\tfilesOtherOpen(entry",
     "\t\t\t\tfilesOtherOpen(entry"),
    ("the right pane reads banners", SWISS,
     "\tfilesOther.listed = true;\n\tfilesOther.listing++;",
     "\tpopulate_meta(filesOther.sorted[0]);\n\tfilesOther.listed = true;\n\tfilesOther.listing++;"),
    ("presses before opening kept", SWISS,
     "\tif(!filesKeepPresses) {\n\t\t(void)padsButtonsTaken(waitButtons);\n\t}\n", ""),
    ("a press during a read lost", SWISS,
     "\t\t\tbuttons = padsButtonsTaken(waitButtons);\n", "\t\t\tbuttons = held;\n"),
    ("B goes Home before the page leaves", SWISS,
     "\t\t\tfilesSnapshot.leaving = 1;\n", ""),
    ("no word while a folder is read", SWISS,
     "\t\t\t(void)DrawUpdateFilesReading(filePanel, UI_FILES_LEFT);\n", ""),
    ("the Source goes under the right pane", SWISS,
     "\t\t\t\thomeLibraryEntryPending = false;\n\t\t\t\tfilesOtherRelease();\n",
     "\t\t\t\thomeLibraryEntryPending = false;\n"),
    ("the fallback keeps the Library's order", SWISS,
     "\t\t\tgameflowListFallback = true;\n\t\t\tneedsRefresh = 1;\n", "\t\t\tgameflowListFallback = true;\n"),
    ("the Library under a mounted right pane", SWISS,
     "\t\t\t\tfilesOtherRelease();\n\t\t\t\tfilesKeepPresses = false;\n", "\t\t\t\tfilesKeepPresses = false;\n"),
    ("a file starts with the right pane mounted", SWISS,
     "\t\t\t\t\tif(type == IS_FILE && loads) {\n\t\t\t\t\t\tfilesOtherRelease();\n\t\t\t\t\t}\n", ""),
    ("left Autoload under a mounted right pane", SWISS,
     "\t\t\t\tfilesOtherRelease();\n\t\t\t\tfilesToggleAutoload(&curDir.name[0]);",
     "\t\t\t\tfilesToggleAutoload(&curDir.name[0]);"),
    ("right Autoload under a mounted right pane", SWISS,
     "\t\t\t\t\tfilesOtherRelease();\n\t\t\t\t\tfilesToggleAutoload(filesOther.dir.name);",
     "\t\t\t\t\tfilesToggleAutoload(filesOther.dir.name);"),
    ("Autoload doesn't bring the right pane back", SWISS,
     "\t\t\t\tfilesToggleAutoload(&curDir.name[0]);\n\t\t\t\tfilesOtherAcquire();\n",
     "\t\t\t\tfilesToggleAutoload(&curDir.name[0]);\n"),
    ("Recent under a mounted right pane", SWISS,
     "\t\t\tfilesOtherRelease();\n\t\t\t(void)filesRecent(false);", "\t\t\t(void)filesRecent(false);"),
    ("the left Other devices... under a mounted right pane", SWISS,
     "\t\t\t\t\tfilesOtherRelease();\n\t\t\t\t\tneedsDeviceChange = 1;", "\t\t\t\t\tneedsDeviceChange = 1;"),
    ("B leaves the right pane mounted", SWISS,
     "\t\t\tfilesOtherRelease();\n\t\t\t/* No Swiss row to dim on the way out. */",
     "\t\t\t/* No Swiss row to dim on the way out. */"),
    ("a Source change under a mounted right pane", SWISS,
     "\tfilesOtherRelease();\n\tmeta_thread_stop();\n\tsourceCommit(device);", "\tmeta_thread_stop();\n\tsourceCommit(device);"),
    ("Y under a mounted right pane", SWISS,
     "\t\tfilesOtherRelease();\n\t\tmeta_thread_stop();\n\t\tfilesOther.device = left;",
     "\t\tmeta_thread_stop();\n\t\tfilesOther.device = left;"),
    ("Y onto a pane that isn't ready", SWISS,
     "\tif(!UIFiles_CanSwap(filesOther.mount, !filesOther.readFailed)) {", "\tif(false) {"),
    ("the right Other devices... under a mounted right pane", SWISS,
     "\tfilesOtherRelease();\n\tdevices[DEVICE_DEST] = NULL;", "\tdevices[DEVICE_DEST] = NULL;"),
    ("the right picker unmounts DEVICE_DEST", SWISS,
     "\tdevices[DEVICE_DEST] = NULL;\n\t/* The picker", "\t/* The picker"),
    ("the right picker takes the A that chose it", SWISS,
     "\twhile(padsButtonsHeld() & PAD_BUTTON_A) VIDEO_WaitVSync();\n\tselect_device(DEVICE_DEST);",
     "\tselect_device(DEVICE_DEST);"),
    ("the right picker keeps the pane in DEVICE_DEST", SWISS,
     "\tdevices[DEVICE_DEST] = dest;\n\tUIScene_Request(filesScene);", "\tUIScene_Request(filesScene);"),
    ("the right picker leaves the Library's scene", SWISS,
     "\tdevices[DEVICE_DEST] = dest;\n\tUIScene_Request(filesScene);", "\tdevices[DEVICE_DEST] = dest;"),
    ("the pane unmounts the Source's storage", SWISS,
     "\tif(filesOther.mount == UI_FILES_OWN) {\n\t\tfilesOther.device->deinit(",
     "\tif(filesOther.mount != UI_FILES_UNMOUNTED) {\n\t\tfilesOther.device->deinit("),
    ("a failed read isn't the mount check", SWISS,
     "\t\tif(filesOtherMount()) {\n\t\t\tfilesOther.read = device->readDir(",
     "\t\tif(false) {\n\t\t\tfilesOther.read = device->readDir("),
    ("no reset function", SWISS,
     "\tSYS_RegisterResetFunc(&filesOtherResetInfo);\n", "\n"),
    ("the reset runs before the interface stops", SWISS,
     "{NULL, NULL}, filesOtherOnReset, 1\n", "{NULL, NULL}, filesOtherOnReset, 0\n"),
    ("the reset unmounts the Source's storage", SWISS,
     "\tif(!final && filesOther.mount == UI_FILES_OWN && filesOther.device != NULL &&",
     "\tif(!final && filesOther.device != NULL &&"),
    ("the Source's slot isn't given back", SWISS,
     "\tmemcpy(entry, &curFile, sizeof(file_handle));\n\tdevices[DEVICE_CUR] = cur;\n",
     "\tmemcpy(entry, &curFile, sizeof(file_handle));\n"),
    ("Swiss's box unmounts the Source it borrowed from", SWISS,
     "\t\t\tif(devices[DEVICE_DEST] != devices[DEVICE_CUR] && devices[DEVICE_DEST] != manageKeep) {\n\t\t\t\tdevices[DEVICE_DEST]->deinit( devices[DEVICE_DEST]->initial );\n",
     "\t\t\tif(devices[DEVICE_DEST] != devices[DEVICE_CUR]) {\n\t\t\t\tdevices[DEVICE_DEST]->deinit( devices[DEVICE_DEST]->initial );\n"),
    ("a left box unmounts the right pane's storage", SWISS,
     "\tif(keep == NULL && filesOther.mount == UI_FILES_OWN) {\n\t\tkeep = filesOther.device;\n\t}\n", ""),
    ("the picker unmounts the kept storage", SWISS,
     "\tif(keep != NULL && devices[DEVICE_DEST] == keep) {\n\t\tdevices[DEVICE_DEST] = NULL;\n\t}\n", ""),
    ("manage_file doesn't know what to keep", SWISS,
     "\tmanageKeep = keep;\n", ""),
    ("the destination slot isn't the other pane's", SWISS,
     "\tdevices[DEVICE_DEST] = fromRight ? cur : filesOther.device;\n", ""),
    ("the destination slot isn't given back", SWISS,
     "\tdevices[DEVICE_DEST] = dest;\n\treturn changed;", "\treturn changed;"),
    ("A's box keeps nothing", SWISS,
     "\t\t\tneedsRefresh = filesManageFile(NULL) ? 1:0;\n\t\t}\n\t\tmemcpy(directory[curSelection]",
     "\t\t\tneedsRefresh = manage_file() ? 1:0;\n\t\t}\n\t\tmemcpy(directory[curSelection]"),
    ("choosing storage keeps the old one mounted", SWISS,
     "\tfilesOtherRelease();\n\tfilesOther.device = device;", "\tfilesOther.device = device;"),
    ("a clash reset keeps the old one mounted", SWISS,
     "source, NULL, 0)) {\n\t\tfilesOtherRelease();\n", "source, NULL, 0)) {\n"),
    ("the right pane on its own storage without File Management", SWISS,
     "UIFiles_RightOnConfig(fileManagementAllowed(),", "UIFiles_RightOnConfig(true,"),
    ("the picker's clash replaced silently", SWISS,
     "\t\tfilesNote = filesNoteText;\n\t\tchosen = NULL;\n", ""),
    ("A chooses a greyed item", SWISS,
     "\t\tif(pick >= 0 && ((box->dim >> pick) & 1u)) {", "\t\tif(false) {"),
    ("L's Source isn't mounted", SWISS,
     "\tsourceCommit(device);\n\t(void)sourceMount();\n", "\tsourceCommit(device);\n"),
    ("L's choice does nothing", SWISS,
     "\t\t\t\t\tfilesSourceChange(filesMenuDevices[choice]);\n", ""),
    ("X at the top opens Swiss's Source picker", SWISS,
     "\t\t\telse if((buttons & PAD_BUTTON_X) && filesAtRoot(&curDir)) {\n\t\t\t\tstorage = UI_FILES_LEFT;\n\t\t\t}\n", ""),
    ("L still pages", SWISS,
     "\t\tif(page == UI_MENU_INPUT_UP) {", "\t\tif((buttons & BUTTON_L) || page == UI_MENU_INPUT_UP) {"),
    # Operations to the other side.
    ("Swiss's box asks over a preset", SWISS,
     "\tif(preset == MANAGE_ASK) {\n", "\tif(true) {\n"),
    ("Swiss's box changed", SWISS,
     "\t\t\tif(canRename && (buttons & BUTTON_L)) {", "\t\t\tif(canHide && (buttons & BUTTON_L)) {"),
    ("the folder chooser runs with a preset destination", SWISS,
     "\t\t\tstrlcpy(destFile->name, destDir, PATHNAME_MAX);\n",
     "\t\t\tstrlcpy(destFile->name, destDir, PATHNAME_MAX);\n"
     "\t\t\tselect_dest_dir(devices[DEVICE_DEST]->initial, destFile->name);\n"),
    ("the preset folder is ignored", SWISS,
     "\t\t\tstrlcpy(destFile->name, destDir, PATHNAME_MAX);\n", ""),
    ("the screen builds the landing name itself", SWISS,
     "\tmanageDestName(landing, thereDir, entry->name, here, there);",
     "\tconcat_path(landing, thereDir, stripInvalidChars(getRelativeName(entry->name)));"),
    ("a failed read keeps its partial file", SWISS,
     "\t\t\t\t\t\tbool removed = !written || manageDropPartial(destFile);\n\t\t\t\t\t\tsprintf(txtbuffer, \"Failed to Read!",
     "\t\t\t\t\t\tbool removed = false;\n\t\t\t\t\t\tsprintf(txtbuffer, \"Failed to Read!"),
    ("a failed read says nothing is left after a write", SWISS,
     "\t\t\t\twritten = 1;\n", ""),
    ("a failed read never deletes", SWISS,
     "bool removed = !written || manageDropPartial(destFile);", "bool removed = true;"),
    ("the Library takes the File Browser's page without clearing filesPage", SWISS,
     "\t\t\t\t\tif(filePanel == filesPage) filesPage = NULL;\n\t\t\t\t\tfilePanel = DrawRepublish(filePanel, newPanel);",
     "\t\t\t\t\tfilePanel = DrawRepublish(filePanel, newPanel);"),
    ("the right pane's Autoload off needs File Management", SWISS,
     "if(fileManagementAllowed() || filesAutoloadFolder(filesOther.dir.name)) {",
     "if(fileManagementAllowed()) {"),
    ("the left pane's Autoload off needs File Management", SWISS,
     "(fileManagementAllowed() || filesAutoloadFolder(curDir.name))", "fileManagementAllowed()"),
    ("a failed write keeps its partial file", SWISS,
     "\t\t\t\t\tbool removed = manageDropPartial(destFile);\n\t\t\t\t\tsprintf(txtbuffer, \"Failed to Write! (%d %d)",
     "\t\t\t\t\tbool removed = false;\n\t\t\t\t\tsprintf(txtbuffer, \"Failed to Write! (%d %d)"),
    ("a failed last write keeps its partial file", SWISS,
     "\t\t\t\tbool removed = manageDropPartial(destFile);\n\t\t\t\tsprintf(txtbuffer, \"Failed to Write! (%d)\\n",
     "\t\t\t\tbool removed = false;\n\t\t\t\tsprintf(txtbuffer, \"Failed to Write! (%d)\\n"),
    ("B in the last piece of a copy is missed", SWISS,
     "\t\t\tif((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) {\n\t\t\t\tcancelled = 1;\n\t\t\t}\n", ""),
    ("a file there that can't be read is written over without the question", SWISS,
     "\t\tif(manageDestExists(destFile)) {\n\t\t\t/* The File Browser asks",
     "\t\tif(devices[DEVICE_DEST]->readFile(destFile, NULL, 0) == 0) {\n\t\t\t/* The File Browser asks"),
    ("a stopped copy keeps its partial file", SWISS,
     "bool removed = cancelled && manageDropPartial(destFile);", "bool removed = false;"),
    ("a finished copy is deleted", SWISS,
     "bool removed = cancelled && manageDropPartial(destFile);", "bool removed = manageDropPartial(destFile);"),
    ("the partial file is looked for on the Source", SWISS,
     "\tDEVICEHANDLER_INTERFACE *dest = devices[DEVICE_DEST];\n\n\tdest->closeFile(destFile);",
     "\tDEVICEHANDLER_INTERFACE *dest = devices[DEVICE_CUR];\n\n\tdest->closeFile(destFile);"),
    ("the partial file is never deleted", SWISS,
     "return dest->deleteFile != NULL && dest->deleteFile(destFile) == 0;",
     "return dest->deleteFile == NULL && dest->deleteFile(destFile) == 0;"),
    ("a card's whole save is deleted after a short write", SWISS,
     "\tif(dest == &__device_card_a || dest == &__device_card_b) {\n\t\treturn false;\n\t}\n", ""),
    ("a folder of the same name is deleted", SWISS,
     "dest->statFile(&there) == 0 && there.fileType == IS_DIR) {", "false) {"),
    ("a stopped Move deletes its original", SWISS,
     "\t\t\tif(!cancelled) {\n\t\t\t\t// If cut, delete from source device",
     "\t\t\tif(1) {\n\t\t\t\t// If cut, delete from source device"),
    ("Replace copies over a file it couldn't remove", SWISS,
     "\t\t\t\t\t\tif(devices[DEVICE_DEST]->deleteFile(destFile) != 0) {",
     "\t\t\t\t\t\tif(devices[DEVICE_DEST]->deleteFile(destFile) != 0 && false) {"),
    ("a stopped Replace doesn't say the old file is gone", SWISS,
     "removed, replaced, D_INFO, message);", "removed, false, D_INFO, message);"),
    ("the copy goes to this pane's own folder", SWISS,
     "changed = filesManageFrom(entry, pane, options[action], thereDir);",
     "changed = filesManageFrom(entry, pane, options[action], hereDir);"),
    ("a same-named folder isn't seen", SWISS,
     "\t\t\twhat.existsFolder = thereList[i]->fileType == IS_DIR;\n", ""),
    ("a memory card isn't known as one", SWISS,
     "\tout->card = device == &__device_card_a || device == &__device_card_b;\n", ""),
    ("the result is said over the panes it changed", SWISS,
     "\t\tfilesSayLater(title, detail, failed);\n\t\treturn;", "\t\tfilesSay(title, detail, failed);\n\t\treturn;"),
    ("the kept result is never said", SWISS,
     "\t/* What an operation said, over both panes as it left them. */\n\tfilesSayPending();\n", ""),
    ("nothing changed and nothing said", SWISS,
     "\tif(!changed) {\n\t\tfilesSayPending();\n\t}\n", ""),
    ("a greyed action runs", SWISS,
     "if(action < 0 || !avail.enabled[action]) {", "if(action < 0) {"),
    ("a Move is offered where the original can't be taken off", FILES,
     "move && !(from->canWrite && (renames || from->canDelete))", "move && !(from->canWrite)"),
    ("a failed delete of the original says nothing", SWISS,
     "kept = devices[DEVICE_CUR]->deleteFile(&curFile) != 0;", "devices[DEVICE_CUR]->deleteFile(&curFile);"),
    ("a Move that couldn't take the original off says it moved", SWISS,
     "manageCopied(result, canDelete || cancelled ? option : COPY_OPTION,", "manageCopied(result, option,"),
    ("A alone deletes", SWISS,
     "(pressed & BUTTON_A) && !(chord && focus == 0)", "(pressed & BUTTON_A)"),
    ("L and A no longer delete", SWISS,
     "chorded = chord && (padsButtonsHeld() & (BUTTON_A | BUTTON_L)) == (BUTTON_A | BUTTON_L);",
     "chorded = false;"),
    ("Delete asks without the chord", SWISS,
     "filesBox(UI_FILES_HINTS_DELETE, 0u, true) == 0", "filesBox(UI_FILES_HINTS_DELETE, 0u, false) == 0"),
    ("L still held as Swiss deletes", SWISS,
     "\tif(chord) {\n\t\tbuttons |= BUTTON_L;\n\t}\n", ""),
    ("Delete's words change in the File Browser", SWISS,
     "filesAskDelete(isFile ?\n\t\t\t\"Delete this file?", "filesAskDelete(isFile ?\n\t\t\t\"Delete it?"),
    ("Delete doesn't ask in the File Browser", SWISS,
     "if(option == DELETE_OPTION && filesBoxes && !filesAskDelete(", "if(option == DELETE_OPTION && false && !filesAskDelete("),
    ("Move's words change", SWISS,
     "\"Move this file?\\nIt is removed from here once copied.\\nA  MOVE    B  CANCEL\"",
     "\"Move this file?\\nA  MOVE    B  CANCEL\""),
    ("Swiss's boxes stay the File Browser's", SWISS,
     "\tfilesBoxes = false;\n\tmemcpy(entry, &curFile", "\tmemcpy(entry, &curFile"),
    ("Keep both offered when only Replace makes room", SWISS,
     "filesFitsBoth = !avail.replaceOnly[action];", "filesFitsBoth = true;"),
    ("the page leaves the background drawn", FRAME,
     " || event->type == EV_FILES", ""),
    # A game's Detail from the File Browser.
    ("A on a game starts it", SWISS,
     "\t\t\t\telse if(detail) {", "\t\t\t\telse if(false) {"),
    ("Detail decided unlocked", SWISS,
     "\t\t\t\tdetail = loads && filesOpensDetail(directory[curSelection]);\n\t\t\t\tunlockFile(",
     "\t\t\t\tunlockFile(directory[curSelection]);\n\t\t\t\tdetail = loads && filesOpensDetail(directory[curSelection]);\n\t\t\t\t(void)("),
    ("Detail on a Source that can't start a disc", SWISS,
     "\t\t(devices[DEVICE_CUR]->features & FEAT_BOOT_GCM);", "\t\ttrue;"),
    ("Detail with the right pane's storage held", SWISS,
     "\tmeta_thread_stop();\n\tfilesOtherRelease();\n\tlockFile(entry);", "\tmeta_thread_stop();\n\tlockFile(entry);"),
    ("posters before the header proves a disc", SWISS,
     "\tif(gameflowReadResolverHeader(entry, &header)) {\n\t\tsnapshot = memalign(32, sizeof(*snapshot));\n\t}",
     "\t(void)gameflowReadResolverHeader(entry, &header);\n\tsnapshot = memalign(32, sizeof(*snapshot));"),
    ("Detail not flagged as the File Browser's", SWISS,
     "\t\tgameflowFromFiles = true;\n", ""),
    ("the flag outlives Detail", SWISS,
     "\t\tgameflowFromFiles = false;\n", ""),
    ("the event shown before Detail is ready", SWISS,
     "\t\tgameflowFromFiles = true;\n", "\t\tgameflowFromFiles = true;\n\t\tDrawRepublish(*filePanel, event);\n"),
    ("an event never shown is left behind", SWISS,
     "\t\telse {\n\t\t\tDrawDiscard(event);\n\t\t}\n", ""),
    ("no way to Swiss's own when Detail can't open", SWISS,
     "\tif(!handled) {\n\t\tfilesActivate(directory, false);\n\t}\n", ""),
    ("the scene stays Detail's", SWISS,
     "\t/* Detail turned the cube to its own scene and the Library's. */\n\tUIScene_Request(filesScene);\n", ""),
    ("Detail flies in", SWISS,
     "\tDrawSetGameflowModeNow(event, mode);\n\tDrawRepublish(", "\tDrawSetGameflowMode(event, mode);\n\tDrawRepublish("),
    ("the event published before its mode", SWISS,
     "\tDrawSetGameflowModeNow(event, mode);\n\tDrawRepublish(gameflowFilesPage, event);\n",
     "\tDrawRepublish(gameflowFilesPage, event);\n\tDrawSetGameflowModeNow(event, mode);\n"),
    ("Detail shown the Library's way", SWISS,
     "\tgameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_DETAIL);",
     "\tDrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_DETAIL);"),
    ("Boot without prompts' launch flies in", SWISS,
     "\t\tgameflowShowFromFiles(context->event, UI_GAMEFLOW_MODE_LAUNCH);",
     "\t\tDrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LAUNCH);"),
    ("the GameID sent after Detail opens", SWISS,
     "\tCardSlots_RequestGame(&GCMDisk);\n\tDrawDispose(msgBox);\n",
     "\tDrawDispose(msgBox);\n"),
    ("B shows the Library leaving", SWISS,
     "\t\t\tgameflowBackToFiles();\n\t\t\tDrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LIBRARY);",
     "\t\t\tDrawSetGameflowMode(context->event, UI_GAMEFLOW_MODE_LIBRARY);"),
    ("Detail's own exit shows the Library leaving", SWISS,
     "\tgameflowBackToFiles();\n\tDrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);",
     "\tDrawSetGameflowMode(context.event, UI_GAMEFLOW_MODE_LIBRARY);"),
    ("the page comes back opening again", SWISS,
     "\t\tgameflowFilesPage = DrawFilesSettled(&filesSnapshot);", "\t\tgameflowFilesPage = DrawFiles(&filesSnapshot);"),
    ("B in Detail says Library", SWISS,
     "\tif(gameflowFromFiles) {\n\t\tflags |= UI_GAMEFLOW_DETAIL_BACK_FILES;\n\t}\n", ""),
    ("Detail before the cube is behind it", SWISS,
     "vsync < 60 && UIScene_Frame()->libraryReveal < 1.0f;", "vsync < 0;"),
    ("Detail's Autoload asks nothing from the File Browser", SWISS,
     "(!gameflowFromFiles ||\n\t\t\tautoloadToggleConfirmed(curFile.name, false))", "true"),
    ("the File Browser's disc 2 by the Library's rule", SWISS,
     "&oppositeHeader, gameflowFromFiles);", "&oppositeHeader, false);"),
    ("two possible discs 2 give none", SWISS,
     "\t\tif(match != NULL && !swissChoice) {", "\t\tif(match != NULL) {"),
    ("disc 2 not chosen by its name", SWISS,
     "\t\tif(swissChoice && UIGameflowResolver_NamedAsOppositeDisc(", "\t\tif(false && UIGameflowResolver_NamedAsOppositeDisc("),
    ("Detail's poster never lands", SWISS,
     "\t\t\t\tif(buttons == 0u) {\n\t\t\t\t\tDrawGameflowPollPosters();\n\t\t\t\t\t/* A card",
     "\t\t\t\tif(buttons == 0u) {\n\t\t\t\t\t/* A card"),
    ("Detail opens with a partial poster", SWISS,
     "snapshot);\n\t\tDrawGameflowPrimePoster();",
     "snapshot);\n\t\tDrawGameflowPollPosters();"),
    ("A  Details on storage that doesn't start games", SWISS,
     "\t\t!filesOpensDetail(entry)) {", "\t\tfalse) {"),
    ("a mode set now moves", FRAME,
     "\treturn _GameflowSetMode(evt, mode, UI_MOTION_OFF);", "\treturn _GameflowSetMode(evt, mode, _CurrentMotionMode());"),
    ("the settled page opens", FRAME,
     "\t\t((drawFilesEvent_t*)page->data)->seconds = 60.0f;\n", ""),
    ("Swiss's list rows come back", FRAME,
     "// Internal\nstatic void _DrawEmptyBox(uiDrawObj_t *evt) {",
     "static void _DrawFileBrowserButton(uiDrawObj_t *evt) { (void)EV_FILEBROWSERBUTTON; }\n"
     "// Internal\nstatic void _DrawEmptyBox(uiDrawObj_t *evt) {"),
    ("the Library draws a list of its own", SWISS,
     "\t\tif(!drawn) {\n\t\t\t/* No Library", "\t\tif(false) {\n\t\t\t/* No Library"),
    ("the Library keeps a legacy input branch", SWISS,
     "\t\tif((browserButtons & BUTTON_B) && gameflowInsideFolder()) {",
     "\t\tif((browserButtons & BUTTON_B) && useGameflow && gameflowInsideFolder()) {"),
    # Finishing touches.
    ("a box's lines stay under the next", SWISS,
     "\tbox->open = 0;\n\tfilesInfoPut(&entry);\n", "\tbox->open = 0;\n"),
    ("the message's lines stay", SWISS,
     "\tfilesInfoPut(&entry);\n\t(void)DrawUpdateFiles(filesPage, &filesSnapshot);\n\twhile(padsButtonsHeld()",
     "\t(void)DrawUpdateFiles(filesPage, &filesSnapshot);\n\twhile(padsButtonsHeld()"),
    ("the message only fades in", SWISS,
     "\t\tfilesSnapshot.messageLeaving = 1;\n\t\tfilesSnapshot.messageSerial++;\n", "\t\tfilesSnapshot.messageSerial++;\n"),
    ("a landed copy doesn't flash", SWISS,
     "\t\tpane->row[pane->focusRow].flags |= UI_FILES_ROW_FLASH;\n", ""),
    ("Y swaps with the panes as they were", SWISS,
     "\tfilesSnapshot.swapping = 1;\n", ""),
    ("the panes stay gone after Y", SWISS,
     "\tfilesSnapshot.swapping = 0;\n", ""),
    ("the swap fade isn't drawn", FRAME,
     "UIFiles_SwapStep(data->content, s->swapping != 0u,", "UIFiles_SwapStep(1.0f, false,"),
    ("the loading cells go with a swap", FRAME,
     "\t\t\t\t_SaveCubesFaded((GXColor) {18, 27, 91, 230}, alpha));",
     "\t\t\t\t_SaveCubesFaded((GXColor) {18, 27, 91, 230}, inner));"),
    ("Reading... goes with a swap", FRAME,
     "float said = pane->reading ? alpha : inner;", "float said = inner;"),
    ("the loading cells move with UI Motion Off", FRAME,
     "lit = motion == UI_MOTION_OFF ? 0 :\n", "lit =\n"),
    ("the loading cells at 5 Hz on Reduced", FRAME,
     "(motion == UI_MOTION_REDUCED ? 2.0f : 5.0f)", "(5.0f)"),
    ("both panes' words at full weight", FRAME,
     "active >= 0.5f ? drawStringMedium : drawString;", "drawStringMedium;"),
    ("a draw reads banners", FRAME,
     "\tUIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);\n\t_SaveCubesBackdrop(",
     "\tUIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);\n\tpopulate_meta(NULL);\n\t_SaveCubesBackdrop("),
)
for label, source, old, new in MUTANTS:
    assert source.count(old) == 1, f"mutation anchor missing: {label}"
    mutated = source.replace(old, new, 1)
    try:
        check(mutated if source is SWISS else SWISS, mutated if source is FRAME else FRAME,
              mutated if source is FILES else FILES)
    except (AssertionError, ValueError):
        continue
    raise AssertionError(f"mutant escaped the File Browser contract audit: {label}")

print(f"File Browser contract audit OK ({len(MUTANTS)} mutants rejected)")
sys.exit(0)
