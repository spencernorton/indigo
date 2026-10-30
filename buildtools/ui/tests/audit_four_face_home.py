#!/usr/bin/env python3
"""Phase 4H structural acceptance gate for semantic four-face Home.

The host C tests prove reducer and spring behavior. This audit protects the
cross-module wiring that those pure tests cannot observe: menu-thread routing,
source lifetime, retained-video publication, renderer ownership, and the
destructive restart boundary. Inputs are normalized so CRLF production files
and LF host modules are checked identically.
"""

from pathlib import Path
import re


if not __debug__:
    raise SystemExit(
        "audit_four_face_home.py requires Python assertions; rerun without -O/PYTHONOPTIMIZE"
    )


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")


def extract_block(source: str, marker: str) -> str:
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
    raise AssertionError(f"unterminated block: {marker}")


def extract_function(source: str, marker: str) -> str:
    return extract_block(source, marker)


def ordered(source: str, *needles: str) -> None:
    position = -1
    for needle in needles:
        position = source.index(needle, position + 1)


HOME_H = read(GUI / "ui_home.h")
HOME_C = read(GUI / "ui_home.c")
LAYOUT_H = read(GUI / "ui_home_layout.h")
LAYOUT_C = read(GUI / "ui_home_layout.c")
TEXT_H = read(GUI / "ui_home_text.h")
TEXT_C = read(GUI / "ui_home_text.c")
SAFETY_H = read(GUI / "ui_home_safety.h")
SAFETY_C = read(GUI / "ui_home_safety.c")
SCENE_H = read(GUI / "ui_scene.h")
SCENE_C = read(GUI / "ui_scene.c")
FRAME_H = read(GUI / "FrameBufferMagic.h")
FRAME_C = read(GUI / "FrameBufferMagic.c")
INDIGO = read(GUI / "indigo_background.c")
INFO = read(GUI / "info.c")
SYSTEM_INFO = read(GUI / "ui_system_info.c")
SWISS = read(ROOT / "cube/swiss/source/swiss.c")

apply_ring = extract_function(HOME_C, "static uiHomeEffect_t applyRing(")
apply_classic = extract_function(HOME_C, "static uiHomeEffect_t applyClassic(")
home_apply = extract_function(HOME_C, "uiHomeEffect_t UIHome_Apply(")
apply_source = extract_function(HOME_C, "static uiHomeEffect_t applySource(")
apply_system = extract_function(HOME_C, "static uiHomeEffect_t applySystem(")
apply_confirm = extract_function(HOME_C, "static uiHomeEffect_t applyRestartConfirm(")
row_labels = extract_function(HOME_C, "const char *UIHome_RowLabel(")
capabilities = extract_function(SWISS, "static uiHomeCapabilities_t homeCapabilities(")
home_publish = extract_function(SWISS, "static void homePublish(")
source_record = extract_function(SWISS, "static void homeSourceRecord(")
source_mounted = extract_function(SWISS, "static bool homeSourceLifecycleMounted(")
source_ready = extract_function(SWISS, "static bool homeSourceIsReady(")
source_startup = extract_function(SWISS, "static void homeSourceObserveStartup(")
dispatch = extract_function(SWISS, "static void homeDispatchEffect(")
refresh_helper = extract_function(SWISS, "static void homeRefreshLibrary(")
restart_helper = extract_function(SWISS, "static void homeRestartSwiss(void)\n{")
restart_drain = extract_function(SWISS, "static bool homeDrainRestartInput(")
selector_drain = extract_function(SWISS, "static void homeDrainSelectorInput(void)\n{")
restart_confirm = extract_function(SWISS, "static void homeConfirmRestartEffect(")
selector = extract_function(SWISS, "static bool select_device_internal(")
menu = extract_function(SWISS, "void menu_loop()")
home_input = extract_block(menu, "else if (curMenuLocation==ON_OPTIONS)")
device_change = extract_block(menu, "if(needsDeviceChange)")
scan = extract_block(menu, "if(devices[DEVICE_CUR] != NULL && needsRefresh)")
draw_root = extract_function(FRAME_C, "static void _DrawHomeRoot(")
draw_rows = extract_function(FRAME_C, "static void _DrawHomeRows(")
draw_context = extract_function(FRAME_C, "static void _DrawHomeContext(")
draw_modal = extract_function(FRAME_C, "static void _DrawHomeModalDepth(")
draw_confirm = extract_function(FRAME_C, "static void _DrawHomeRestartConfirm(")
draw_home = extract_function(FRAME_C, "static void _DrawHome(uiDrawObj_t *evt)")
draw_update = extract_function(FRAME_C, "void DrawUpdateHome(")
prepare_text = extract_function(FRAME_C, "static void _PrepareHomeText(")
video_loop = extract_function(FRAME_C, "static void *videoUpdate(")
setup_cube = extract_function(INDIGO, "static void setupCubePipeline(")
scene_apply_home = extract_function(SCENE_C, "static void applyHomeRequest(")
scene_retarget = extract_function(SCENE_C, "static void retargetPose(")
scene_update = extract_function(SCENE_C, "void UIScene_Update(")
info_page = extract_function(INFO, "uiDrawObj_t * info_draw_page(")
show_info = extract_function(INFO, "void show_info()")


# --- Canonical semantic model: four clockwise faces, and Apps. ---
# Apps is the fifth face only while the source has an app (hasApps) and
# Setup > Console puts it on a side: by default the ring is otherwise exactly
# the four, and Apps comes last so their order and numbers never change
# (2026-09-29, the maintainer's choice).
face_enum = re.search(
    r"typedef\s+enum\s*\{\s*"
    r"UI_HOME_FACE_LIBRARY\s*=\s*0\s*,\s*"
    r"UI_HOME_FACE_SOURCE\s*,\s*"
    r"UI_HOME_FACE_SETTINGS\s*,\s*"
    r"UI_HOME_FACE_SYSTEM\s*,\s*"
    r"(?:/\*.*?\*/\s*)?UI_HOME_FACE_APPS\s*,\s*"
    r"(?:/\*.*?\*/\s*)?UI_HOME_FACE_SAVES\s*,\s*"
    r"UI_HOME_FACE_EMULATORS\s*,\s*"
    r"(?:/\*.*?\*/\s*)?UI_HOME_FACE_FILES\s*,\s*"
    r"(?:/\*.*?\*/\s*)?UI_HOME_FACE_WEB\s*,\s*"
    r"UI_HOME_FACE_COUNT\s*\}\s*uiHomeFace_t\s*;",
    HOME_H,
    re.S,
)
assert face_enum, ("Home faces are not Library/Source/Settings/System, then Apps, "
                   "Memory Cards, Emulators, File Browser and Web")
assert re.search(
    r"faceLabels\s*\[UI_HOME_FACE_COUNT\]\s*=\s*\{\s*"
    r'"LIBRARY"\s*,\s*"SOURCE"\s*,\s*"SETTINGS"\s*,\s*"SYSTEM"\s*,\s*"APPS"\s*,\s*'
    r'"MEMORY CARDS"\s*,\s*"EMULATORS"\s*,\s*"FILE BROWSER"\s*,\s*"WEB"\s*\}',
    HOME_C,
    re.S,
), "visible Home labels no longer match the semantic enum order"
# The faces sit on Library's four sides; unless Settings names others they
# are Source, Settings, System and Apps up, left, right and down, and Apps
# keeps its side but leaves the ring while the source has no app.
assert re.search(
    r"defaultSides\[UI_HOME_SIDE_COUNT\]\s*=\s*\{\s*"
    r"UI_HOME_FACE_SOURCE\s*,\s*UI_HOME_FACE_SETTINGS\s*,\s*"
    r"UI_HOME_FACE_SYSTEM\s*,\s*UI_HOME_FACE_APPS\s*\}",
    HOME_C,
), "the default cube is no longer Source, Settings, System and Apps"
resolve_layout = extract_function(HOME_C, "static void resolveLayout(")
assert "face == (int)UI_HOME_FACE_APPS && !capabilities.hasApps" in resolve_layout, (
    "the ring is no longer without Apps while the source has no app"
)
assert "reconcileFaces(state, capabilities);" in HOME_C
assert "case UI_HOME_FACE_APPS:\n\t\t\t\treturn UI_HOME_EFFECT_OPEN_APPS;" in apply_ring
assert re.search(
    r"capabilities\.hasApps = capabilities\.hasSource && appsPlaced &&\s*homeAppsFound;",
    capabilities,
), "Apps on no side no longer keeps the Apps face off Home"
assert re.search(
    r"if\(capabilities\.hasSource && appsPlaced && !homeAppsKnown\)",
    capabilities,
), "Apps on no side no longer keeps Home from reading /apps"
assert ".customSides = true," in capabilities, "Home no longer takes its sides from Settings"
ordered(dispatch, "case UI_HOME_EFFECT_OPEN_APPS:", "show_apps();",
        "UIScene_Request(UI_SCENE_HOME);")
ordered(dispatch, "case UI_HOME_EFFECT_OPEN_EMULATORS:", "show_emulators();",
        "UIScene_Request(UI_SCENE_HOME);")
assert "#define UI_HOME_QUARTER_TURN_RADIANS 1.57079632679f" in HOME_H

legacy_scope = "\n".join((HOME_H, HOME_C, SCENE_H, SCENE_C, FRAME_H, home_input))
for legacy in (
    "MENU_MAX",
    "MENU_DEVICE",
    "MENU_SETTINGS",
    "MENU_INFO",
    "MENU_REFRESH",
    "MENU_EXIT",
    "MENU_NOSELECT",
    "UI_SCENE_HOME_SELECTION_COUNT",
    "UI_SCENE_HOME_TURN_RADIANS",
    "UIScene_RequestHomeSelection",
    "DrawMenuButtons",
    "DrawUpdateMenuButtons",
    "1.256637",
):
    assert legacy not in legacy_scope, f"legacy five-sector Home token returned: {legacy}"
assert "B  LIBRARY" not in draw_root + draw_context + draw_confirm + home_input


# --- Signed quarter-turn scene ownership and Source suppression. ---
assert "UI_SCENE_SOURCE" in SCENE_H
assert "UIScene_Request(type == DEVICE_CUR ? UI_SCENE_SOURCE : UI_SCENE_LIBRARY);" in selector, (
    "current-source selector does not own the distinct Source scene"
)
assert "scene == UI_SCENE_HOME || scene == UI_SCENE_SOURCE" in SCENE_C
assert "scene->scene != UI_SCENE_HOME" in draw_home, (
    "root Home composition can leak behind the Source selector"
)
assert "UIScene_RequestHome(const uiHomeState_t *home)" in SCENE_H
assert "uiHomeOrientation_t orientation;" in HOME_H
ordered(scene_apply_home, "request.orientation", "state.home = request;",
        "state.homeTarget = request.orientation;", "state.homeTurnAxis = request.turnAxis;",
        "state.homeTurnDirection = request.turnDirection;")
scene_request_home = extract_function(SCENE_C, "void UIScene_RequestHome(")
scene_load_home = extract_function(SCENE_C, "static uiSceneHomeRequest_t loadHomeRequest(")
for field in ("requestedHomeOrientation", "requestedHomeTurnAxis", "requestedHomeTurnDirection",
              "requestedHomeStyle"):
    assert field in scene_request_home and field in scene_load_home
assert "__ATOMIC_ACQ_REL" in scene_request_home
assert "__atomic_thread_fence(__ATOMIC_ACQUIRE)" in scene_load_home
assert "homeYawTarget" not in SCENE_C
for field in ("homeOrientation[3][3]", "homeTargetOrientation[3][3]",
              "homeMotifBasis[UI_HOME_FACE_COUNT][3][3]", "homeMotifAlpha"):
    assert field in SCENE_H
orientation_update = extract_function(SCENE_C, "static void updateOrientation(")
assert "UIHome_OrientationMatrix(&state.navigationTarget" in orientation_update
assert "state.frame.homeOrientation" in orientation_update
assert "updateOrientation(deltaSeconds, motionMode);" in scene_update
assert "request.face -" not in scene_apply_home
assert "UI_HOME_FACE_COUNT / 2" not in scene_apply_home
for field in ("homeFace", "homeTurnDirection", "homeTurnAxis", "homeFocusProgress", "homeRevision"):
    assert field in SCENE_H, f"video frame lost {field}"
    assert f"state.frame.{field} =" in scene_update, f"video thread does not publish {field}"


# --- Root B is inert on the Infinite ring; all Home actions flow through one
# reducer effect. Setup > Console > Cube > Classic lays the faces out as the
# GameCube's menu does, Library the way between them, and there B turns back
# to Library from a side face, as the GameCube's does (2026-10-02, the
# maintainer's choice). That B lives in Classic's own reducer, never in the
# ring's, and the ring surface picks one of the two by the state's style. ---
assert "UI_HOME_INPUT_BACK" not in apply_ring, "Infinite's root B is no longer inert"
assert "UI_HOME_INPUT_BACK" in apply_classic, "Classic's B no longer turns back to Library"
assert "return applyRing(state, input, capabilities);" in apply_classic, (
    "Classic no longer leaves A and Start to the ring"
)
ordered(home_apply, "case UI_HOME_SURFACE_RING:",
        "state->style == UI_HOME_CUBE_CLASSIC ?",
        "applyClassic(state, input, capabilities) :",
        "applyRing(state, input, capabilities);",
        "case UI_HOME_SURFACE_SOURCE:")
ordered(
    home_input,
    "if(btns & BUTTON_B)",
    "command = UI_HOME_INPUT_BACK;",
    "else if(btns & BUTTON_A)",
    "command = UI_HOME_INPUT_ACTIVATE;",
    "homeDispatchEffect(effect);",
)
assert "gameflowEnterLibraryFromHome(" not in home_input
assert "case UI_HOME_EFFECT_OPEN_LIBRARY:" in dispatch
assert "gameflowEnterLibraryFromHome()" in dispatch
assert SWISS.count("gameflowEnterLibraryFromHome(") == 3, (
    "Library entry bypasses or no longer reaches the effect dispatcher"
)
assert "if(homeLibraryEntryPending)" in scan
assert "if(gameflowEnterLibraryFromHome())" in scan


# --- Refresh/restart are contextual effects; reset remains confirmation-only. ---
assert HOME_C.count("UI_HOME_EFFECT_REFRESH") == 1
assert HOME_C.count("UI_HOME_EFFECT_RESTART") == 1
ordered(
    apply_source,
    "state->selection == 1 && capabilities.hasSource",
    "return UI_HOME_EFFECT_REFRESH;",
)
assert "UI_HOME_EFFECT_REFRESH" not in apply_ring + apply_system + apply_confirm
ordered(
    dispatch,
    "case UI_HOME_EFFECT_REFRESH:",
    "homeRefreshLibrary();",
    "case UI_HOME_EFFECT_RESTART:",
    "homeConfirmRestartEffect();",
)
assert SWISS.count("homeRefreshLibrary();") == 1
assert SWISS.count("homeRestartSwiss();") == 1
assert "homeRefreshLibrary();" not in home_input
assert "homeRestartSwiss();" not in home_input
assert "SYS_ResetSystem(" not in home_input + dispatch
assert "SYS_ResetSystem(SYS_HOTRESET, 0, !swissSettings.hasFlippyDrive);" in restart_helper
assert "SYS_POWEROFF" not in restart_helper + dispatch + home_input

# System's rows: Information, Memory Cards (row one, its own screen), File
# Browser (row two, Swiss's list at the source's root) and Restart, last.
# Confirmation opens on Cancel (row zero), only Restart's row opens it, and
# only its row one can emit Restart.
ordered(
    apply_system,
    "state->selection == 1",
    "return UI_HOME_EFFECT_OPEN_SAVES;",
    "state->selection == 2",
    "return openFiles(capabilities);",
    "state->selection == 3",
    "enterSurface(state, UI_HOME_SURFACE_RESTART_CONFIRM, 0);",
)
# The File Browser face opens it the same way, through the same helper:
# Swiss's list with a source, the source picker without one.
ordered(apply_ring, "case UI_HOME_FACE_FILES:", "return openFiles(capabilities);")
open_files_helper = extract_function(HOME_C, "static uiHomeEffect_t openFiles(")
assert re.search(r"return capabilities\.hasSource \? UI_HOME_EFFECT_OPEN_FILES :\s*"
                 r"UI_HOME_EFFECT_CHANGE_SOURCE;", open_files_helper)
assert HOME_C.count("UI_HOME_EFFECT_OPEN_FILES") == 1
assert HOME_C.count("openFiles(capabilities)") == 2
ordered(dispatch, "case UI_HOME_EFFECT_OPEN_SAVES:", "show_saves();")
# File Browser lists the source's root in Swiss's own list: the flag that
# keeps the Library away is set only there and dropped by Home.
open_files = dispatch[dispatch.index("case UI_HOME_EFFECT_OPEN_FILES:"):]
open_files = open_files[: open_files.index("break;")]
ordered(open_files, "homeFileBrowser = true;", "devices[DEVICE_CUR]->initial",
    "curMenuLocation = ON_FILLIST;")
assert SWISS.count("homeFileBrowser = true;") == 1
# B from Swiss's list comes back to the face that opened it, System's rows
# or File Browser's own face: opening it leaves Home's state alone.
assert "homeState" not in open_files
assert "homeFileBrowser = false;" in home_input
assert "swissSettings.enableFileManagement =" not in SWISS
# Web reads the browser's DOL from the config device, which is mounted only
# while something reads it unless it is the Source too: mount it to look, let
# it go when the DOL isn't there, and keep it through boot_dol, which reads
# the DOL from it. The browser gets lab=1, its own console lab, only while
# Indigo's is on.
ordered(dispatch, "case UI_HOME_EFFECT_OPEN_WEB:", "launchWebBrowser();")
web_launch = extract_function(SWISS, "static void launchWebBrowser(void)\n{")
ordered(web_launch, "mounted = config_set_device();", "device = devices[DEVICE_CONFIG];",
    "if(mounted && ", "device->readDir(", "if(found < 0) {", "if(mounted) {",
    "config_unset_device();", "return;", ".lab = swissSettings.labServer",
    "UIBrowserLaunch_Arguments(&browserArgs, &context);",
    "boot_dol(&curFile, browserArgs.argc, browserArgs.argv);",
    "device->closeFile(&curFile);", "config_unset_device();")
assert web_launch.count("config_unset_device();") == 2
assert web_launch.count("boot_dol(") == 1
assert SWISS.count("show_saves();") == 1
# Memory Cards hands the Home cube over where it stands, System side to the
# front: any scene but Home's would turn it while it goes and comes back.
open_saves = dispatch[dispatch.index("case UI_HOME_EFFECT_OPEN_SAVES:"):]
open_saves = open_saves[: open_saves.index("break;")]
assert set(re.findall(r"UIScene_Request\((\w+)\)", open_saves)) <= {"UI_SCENE_HOME"}, (
    "Memory Cards turns the Home cube away from its System side as it hands it over"
)
assert 'return row == 0 ? "CANCEL" : "RESTART";' in row_labels
ordered(
    apply_confirm,
    "state->selection == 0",
    "enterSurface(state, UI_HOME_SURFACE_SYSTEM, 3);",
    "state->selection == 1",
    "return UI_HOME_EFFECT_RESTART;",
)
assert home_input.index("if(btns & BUTTON_B)") < home_input.index(
    "else if(btns & BUTTON_A)"
), "Back no longer wins simultaneous destructive command presses"
assert "if(command != UI_HOME_INPUT_NONE && !navigated)" in home_input, (
	"navigation and activation can execute in the same input sample"
)
release_drain = home_input.rindex("padsMenuInputPoll(&homeMenuInput,")
assert home_input.index("homeDispatchEffect(effect);") < release_drain
assert "allowedAxes, true);" in home_input[release_drain:], (
	"opening/confirmation buttons are not release-drained between samples"
)
assert "padsStickX" not in home_input and "padsStickY" not in home_input
assert re.search(
	r"#define HOME_CONFIRMATION_BUTTONS\s*\(BUTTON_A\s*\|\s*BUTTON_B\s*\|\s*"
	r"BUTTON_RIGHT\s*\|\s*\\?\s*BUTTON_LEFT\s*\|\s*BUTTON_UP\s*\|\s*"
	r"BUTTON_DOWN\s*\|\s*BUTTON_START\)",
	SWISS,
), "Restart release drain no longer owns the full Home button mask"
assert "UIHomeSafety_RestartReleasePending(" in restart_drain
assert "UIHomeSafety_AccumulateRestartCancel(" in restart_drain
assert "padsStickX" not in restart_drain and "padsStickY" not in restart_drain
ordered(
	restart_confirm,
	"homeDrainRestartInput(false)",
	"UIHome_Apply(&homeState, UI_HOME_INPUT_BACK",
	"homePublish(true);",
	"homeRestartSwiss();",
)
assert "homeState.surface" not in restart_confirm, (
	"Restart confirmation bypasses the reducer when a late B cancels"
)
assert "const u32 homeButtons = HOME_CONFIRMATION_BUTTONS | BUTTON_Y;" in home_input


# --- Source cancellation is lossless; teardown begins only after confirmed A. ---
cancel = extract_block(selector, "if(btns & BUTTON_B)")
destination_cancel = extract_block(cancel, "if(type == DEVICE_DEST)")
assert "devices[type] = NULL;" in destination_cancel
assert cancel.count("devices[type] = NULL;") == 1
assert "deinit(" not in cancel and "freeFiles(" not in cancel
assert "swissSettings.exiSpeed = savedExiSpeed;" in cancel
assert "homeDrainSelectorInput();" in cancel, "selector B can leak into Home"
assert "SELECTOR_RELEASE_BUTTONS" in selector_drain
assert "padsButtonsHeld() & SELECTOR_RELEASE_BUTTONS" in selector_drain
assert "UIMenuInput_Init(&homeMenuInput);" in selector_drain
assert "homeMenuInputRetrace = VIDEO_GetRetraceCount();" in selector_drain
assert "padsStick" not in selector_drain
assert "return false;" in cancel
assert selector.index("if(btns & BUTTON_B)") < selector.index("if(btns & BUTTON_A)"), (
	"selector A+B no longer gives cancellation precedence"
)

confirmed_marker = "bool deviceConfirmed = select_device_internal(DEVICE_CUR);"
before_selector = device_change[: device_change.index(confirmed_marker)]
assert "devices[DEVICE_PREV] = previousDevice;" in before_selector
assert "needsRefresh = 1;" not in before_selector, (
	"selector cancellation schedules a destructive rescan"
)
for destructive in ("freeFiles();", "DrawGameflowCancelPosters();", "deinit(",
                    "devices[DEVICE_CUR] = NULL;"):
    assert destructive not in before_selector, (
        f"current source is destroyed before selector confirmation: {destructive}"
    )

# A confirmed Source replaces the old one in sourceCommit (the File
# Browser's L and Y use it too) and mounts in sourceMount.
current_replace = extract_block(selector, "if(type == DEVICE_CUR) {")
assert "sourceCommit(selectedDevice);" in current_replace
commit = extract_function(SWISS, "static void sourceCommit(DEVICEHANDLER_INTERFACE *device)\n{")
ordered(
    commit,
	"devices[DEVICE_CUR] != NULL",
    "freeFiles();",
    "DrawGameflowCancelPosters();",
    "devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);",
	"homeSourceRecord(devices[DEVICE_CUR], UI_HOME_SOURCE_MOUNT_UNMOUNTED);",
    "devices[DEVICE_CUR] = device;",
	"homeSourceRecord(device, UI_HOME_SOURCE_MOUNT_UNMOUNTED);",
)
mount = extract_function(SWISS, "static bool sourceMount(void)\n{")
assert "if(deviceConfirmed && devices[DEVICE_CUR] != NULL && !sourceMount())" in device_change
assert "ret = devices[DEVICE_CUR]->init(devices[DEVICE_CUR]->initial);" in mount
assert "devices[DEVICE_CUR] != previousDevice" not in device_change
assert "else if(!deviceConfirmed)" in device_change
assert "needsRefresh = refreshBeforeSelection;" in device_change
cancel_change = extract_block(device_change, "else if(!deviceConfirmed)")
ordered(
	cancel_change,
	"if(homeSourceLifecycleMounted())",
	"needsRefresh = refreshBeforeSelection;",
	"devices[DEVICE_CUR] = NULL;",
	"homeSourceRecord(NULL, UI_HOME_SOURCE_MOUNT_ABSENT);",
	"needsRefresh = 0;",
)
assert "deinit(" not in cancel_change and "freeFiles(" not in cancel_change


# --- Capabilities are truthful menu-thread snapshots. ---
assert ".hasSource = homeSourceIsReady()" in capabilities
assert "deviceHandler_getDeviceAvailable(devices[DEVICE_CUR])" in source_ready
assert "UIHomeSafety_SourceMounted(&homeSourceLifecycle" in source_mounted
assert "UIHomeSafety_SourceReady(&homeSourceLifecycle" in source_ready
assert "UIHomeSafety_RecordSource(&homeSourceLifecycle" in source_record
assert "lifecycle->state == UI_HOME_SOURCE_MOUNT_MOUNTED" in SAFETY_C
assert "lifecycle->handler == currentHandler" in SAFETY_C
assert "handler != NULL ? state :" in SAFETY_C
assert "UI_HOME_SOURCE_MOUNT_ABSENT" in SAFETY_C
ordered(
	source_startup,
	"homeSourceLifecycle.state != UI_HOME_SOURCE_MOUNT_UNKNOWN",
	"devices[DEVICE_CUR] != NULL && !needsDeviceChange",
	"UI_HOME_SOURCE_MOUNT_MOUNTED :",
	"UI_HOME_SOURCE_MOUNT_UNMOUNTED",
)
assert menu.index("homeSourceObserveStartup();") < menu.index("homePublish(")
ordered(
	mount,
	"ret = devices[DEVICE_CUR]->init(devices[DEVICE_CUR]->initial);",
	"UI_HOME_SOURCE_MOUNT_UNMOUNTED",
	"devices[DEVICE_CUR] = NULL;",
	"UI_HOME_SOURCE_MOUNT_ABSENT",
)
success_tail = mount[mount.rindex("DrawDispose(msgBox);") :]
ordered(
	success_tail,
	"deviceHandler_setDeviceAvailable(devices[DEVICE_CUR], true);",
	"homeSourceRecord(devices[DEVICE_CUR]",
	"UI_HOME_SOURCE_MOUNT_MOUNTED",
)
ordered(
	scan,
	"devices[DEVICE_CUR]->deinit(devices[DEVICE_CUR]->initial);",
	"UI_HOME_SOURCE_MOUNT_UNMOUNTED",
	"devices[DEVICE_CUR] = NULL;",
	"UI_HOME_SOURCE_MOUNT_ABSENT",
)
assert re.search(
    r"\.hasRecent\s*=\s*swissSettings\.recentListLevel\s*>\s*0\s*&&\s*"
    r"swissSettings\.recent\[0\]\[0\]\s*!=\s*'\\0'",
    capabilities,
    re.S,
)
assert "capabilities.hasRecent ?" in apply_ring
assert "UI_HOME_EFFECT_OPEN_RECENT" in apply_ring
assert "if(capabilities.hasSource)" in home_publish
# Home names the current source as the UI calls it ("Game Disc", not "DVD").
assert "DeviceDisplayName(devices[DEVICE_CUR])" in home_publish
render_scope = draw_root + draw_rows + draw_context + draw_confirm + draw_home
for live_probe in ("devices[", "swissSettings", "deviceHandler_getDeviceAvailable"):
    assert live_probe not in render_scope, f"video renderer performs live capability I/O: {live_probe}"


# A library scan may reset file selection, never the retained Home face.
assert "scanFiles();" in scan and "curSelection=0;" in scan
assert "curMenuSelection" not in scan
assert "UIHome_Init(" not in scan


# --- One retained snapshot, one video transaction, one rail per surface. ---
ordered(
    draw_update,
    "LWP_MutexLock(_videomutex);",
    "data->state = *state;",
    "data->capabilities = capabilities;",
	"data->layoutValid = UIHomeLayout_Compute(&data->state,",
    "UIScene_RequestHome(state);",
    "LWP_MutexUnlock(_videomutex);",
)
assert video_loop.index("LWP_MutexLock(_videomutex);") < video_loop.index(
    "UIScene_Update(UIAnim_Delta(), _CurrentMotionMode());"
)
assert "uiHomeState_t state;" in FRAME_C and "uiHomeCapabilities_t capabilities;" in FRAME_C
assert '#include "ui_home_layout.h"' in FRAME_C
assert "uiHomeLayout_t layout;" in FRAME_C and "bool layoutValid;" in FRAME_C
assert "!data->layoutValid" in draw_home
assert "scene->homeFace != data->state.face" in draw_home
for field in ("scene->homeFace", "scene->homeTurnDirection", "scene->homeFocusProgress"):
    assert field in draw_root, f"Home renderer does not read video-owned {field}"
assert "data->state.face" not in draw_root

assert "layout->commandCenter.x, layout->commandCenter.y" in draw_root
assert "layout->commandCenter.x, layout->commandCenter.y" in draw_rows
assert "data->layout.commandCenter.x, data->layout.commandCenter.y" in draw_confirm
assert draw_context.count("_DrawHomeRows(data, reveal);") == 1
ordered(
    draw_home,
    "data->state.surface == UI_HOME_SURFACE_RING",
    "_DrawHomeRoot(data, scene, reveal);",
    "else if(data->state.surface == UI_HOME_SURFACE_RESTART_CONFIRM)",
    "_DrawHomeRestartConfirm(data, reveal);",
    "else",
    "_DrawHomeContext(data, reveal);",
)
assert "STICK / D-PAD  TURN" in prepare_text and "B  BACK" not in draw_root
assert "D-PAD  SELECT    A  OPEN    B  BACK" in FRAME_C
assert r"\213  \233  CHOOSE    A  SELECT    B  CANCEL" in FRAME_C
assert "RELOADS INDIGO AND ENDS THIS SESSION" in FRAME_C
ordered(
	draw_confirm,
	"_DrawHomeModalDepth(&data->layout, reveal);",
	"data->layout.consequenceCenter.x",
	"homeRestartConsequence",
)
# The scrim darkens the whole frame, widescreen margins too.
assert "_putFlatRect(UIStage_Left(), 0.0f, UIStage_Right() - UIStage_Left(), 480.0f, scrim);" in draw_modal
assert "layout->modalBounds" in draw_modal
assert "snprintf(" not in draw_modal and "GetTextScale" not in draw_modal
assert '"HOME   %d / %d"' not in FRAME_C, (
	"generic ordinal eyebrow returned instead of truthful source status"
)
# The ring names only the selected face, under the cube: no source eyebrow
# above it and no neighbour labels beside it (2026-09-23 review).
assert "eyebrow" not in FRAME_C
# The face's name as text, and the command line with button icons.
assert draw_root.count("_DrawHomeText(") == 1
assert draw_root.count("_DrawHintText(") == 1
assert "UIHome_FaceLabel(face)" in draw_root
# Setup > Console's Face Labels and On-screen Controls take away only the
# face's name and the hint line under the cube; the rows' hint line follows
# On-screen Controls, and Restart's question always keeps its own.
# The renderer reads them from the published capabilities, which swiss.c's
# homeCapabilities fills from the settings, as it does the cube's style.
assert ("if(!data->capabilities.hideFaceLabel) {\n\t\t_DrawHomeText(incomingX, incomingY,\n"
        "\t\t\tUIHome_FaceLabel(face)") in draw_root
assert ("if(!data->capabilities.hideCommands) {\n\t\t_DrawHintText(layout->commandCenter.x, "
        "layout->commandCenter.y,\n\t\t\tdata->command,") in draw_root
assert ("if(!data->capabilities.hideCommands) {\n\t\t_DrawHintText(layout->commandCenter.x, "
        "layout->commandCenter.y,\n\t\t\thomeContextCommand,") in draw_rows
assert "hideCommands" not in draw_confirm and "hideFaceLabel" not in draw_confirm
home_capabilities = extract_function(SWISS, "static uiHomeCapabilities_t homeCapabilities(")
assert ".hideFaceLabel = swissSettings.hideFaceLabels != 0," in home_capabilities
assert ".hideCommands = swissSettings.hideHomeControls != 0," in home_capabilities
assert prepare_text.count("UIHomeText_CopyFitted(") == 1
assert "GetTextSizeInPixels" in prepare_text
assert "GetTextScaleToFitInWidth" not in prepare_text
assert "#define UI_HOME_TEXT_SCALE_FLOOR 0.46f" in TEXT_H
assert "UI_HOME_TEXT_ELLIPSIS_BYTE" in TEXT_H
assert "(float)width * UI_HOME_TEXT_SCALE_FLOOR" in TEXT_C
for allocation in ("malloc(", "calloc(", "realloc(", "strdup("):
	assert allocation not in TEXT_C, "Home text fitting introduced allocation"
for render_function in (draw_root, draw_rows, draw_context, draw_confirm, draw_home):
	assert "snprintf(" not in render_function and "GetTextScale" not in render_function, (
		"Home renderer formats or measures text every frame"
	)

# The pure native layout is the renderer's geometry source, not dead test code.
for token in (
	"#define UI_HOME_LAYOUT_SAFE_LEFT 24",
	"#define UI_HOME_LAYOUT_SAFE_TOP 62",
	"#define UI_HOME_LAYOUT_SAFE_RIGHT 616",
	"#define UI_HOME_LAYOUT_SAFE_BOTTOM 438",
	"#define UI_HOME_LAYOUT_CUBE_RAIL_BOTTOM 368",
	"#define UI_HOME_LAYOUT_CUBE_KEEP_OUT_LEFT 185",
	"#define UI_HOME_LAYOUT_CUBE_KEEP_OUT_RIGHT 455",
	"#define UI_HOME_LAYOUT_SELECTED_TRAVEL 34",
	"#define UI_HOME_LAYOUT_COMMAND_Y 433",
):
	assert token in LAYOUT_H
for field in ("modalBounds", "consequenceCenter", "consequenceBounds"):
	assert field in LAYOUT_H and field in LAYOUT_C
assert "modalBounds" in draw_modal
assert "consequenceCenter" in draw_confirm
assert "consequenceBounds" in prepare_text
assert "item->glowBounds.top > rail" in LAYOUT_C
# Rows clear the cube's rail; only System's four, under its raised cube,
# clear the higher one.
assert re.search(r"int rail = count == 4 \? UI_HOME_LAYOUT_SYSTEM_RAIL_BOTTOM :\s*UI_HOME_LAYOUT_CUBE_RAIL_BOTTOM;", LAYOUT_C)
assert "#define UI_HOME_LAYOUT_SYSTEM_RAIL_BOTTOM 349" in LAYOUT_H
assert "UIHomeLayout_RectsDisjoint(items[first].glowBounds, command)" in LAYOUT_C
assert "UIHomeLayout_RectIsSafe(selectedTravelBounds)" in LAYOUT_C
assert "UIHomeLayout_RectsDisjoint(selectedTravelBounds, cubeKeepOut)" in LAYOUT_C
assert "UIMotion_Amplitude((float)UI_HOME_LAYOUT_SELECTED_TRAVEL," in draw_root


# --- Ambient cube motion is bounded sway, never semantic continuous yaw. ---
sway = re.search(r"#define CUBE_IDLE_SWAY_RADIANS\s+([0-9.]+)f", INDIGO)
assert sway and 0.0 < float(sway.group(1)) <= 0.08
# Idle Animation > Sway turns from Home's pose to its mirror image and back:
# its reach is Home's own turn and no more, so the front face stays in front
# and the outline is never wider than Home's. Only while Home itself rests
# (not the Source picker), with motion on, from a clock that waits while the
# cube leaves its rest and starts over once the idle blend is all but gone.
# test_cube_render_pose.py runs it through a turn and a shallow dip.
home_pose = re.search(r"\[UI_SCENE_HOME\] = \{([^}]*)\}", SCENE_C)
home_yaw = float(home_pose.group(1).split(",")[4].strip().rstrip("f"))
sway_reach = re.search(r"#define CUBE_SWAY_RADIANS\s+([0-9.]+)f", INDIGO)
assert sway_reach and 0.0 < float(sway_reach.group(1)) <= home_yaw
assert "if(idleSway) {" in setup_cube
assert "float sway = swayClock(seconds, animated ? idleBlend : 0.0f);" in setup_cube
assert ("idleYaw = animated ? -CUBE_SWAY_RADIANS *\n"
        "\t\t\t(1.0f - cosf(sway * CUBE_SWAY_RATE)) * idleBlend : 0.0f;") in setup_cube
sway_clock = extract_function(INDIGO, "static float swayClock(")
assert "#define CUBE_SWAY_RESTART_BLEND 0.01f" in INDIGO
assert "if(idleBlend < CUBE_SWAY_RESTART_BLEND) {\n\t\tswaySeconds = 0.0f;" in sway_clock
assert "else if(idleBlend >= swayLastBlend) {" in sway_clock
assert "swayLastBlend = idleBlend;" in sway_clock
assert ("idleTarget = motionMode == UI_MOTION_FULL &&\n"
        "\t\tstate.appliedScene == UI_SCENE_HOME &&") in scene_update
# The frame sets Waves and Idle Animation from Setup > Console before it
# draws the backdrop; test_background_gx_stream.py runs the waves' gate.
draw_background = extract_function(FRAME_C, "static void _DrawBackground(")
ordered(draw_background, "IndigoBackground_SetWaves(!swissSettings.hideWaves);",
        "IndigoBackground_SetIdleSway(swissSettings.idleAnimation != 0);",
        "IndigoBackground_Draw(")
assert draw_background.count("IndigoBackground_SetWaves(") == 1
assert draw_background.count("IndigoBackground_SetIdleSway(") == 1
assert "fminf(step, 0.1f)" in sway_clock
assert "CUBE_IDLE_TURN_RATE" not in INDIGO
assert "fmodf(seconds *" not in setup_cube
assert "sinf(seconds * CUBE_IDLE_SWAY_RATE)" in setup_cube
assert "CUBE_IDLE_SWAY_RADIANS * idleBlend" in setup_cube
assert "float idleBlend = scene->homeIdleBlend;" in setup_cube
assert "uiMotionSpring_t homeIdleBlend;" in SCENE_C
assert "state.frame.homeIdleBlend = UIMotion_SpringUpdate" in scene_update
assert "#define UI_SCENE_HOME_TURN_RESPONSE 10.0f" in SCENE_C
assert "UI_SCENE_HOME_TURN_RESPONSE" in scene_retarget
assert "retargetPose(state.appliedScene, motionMode);" in scene_apply_home

# Each semantic lateral face has a pane and shows the icon Settings chose
# for it; every icon draws on whichever face it is given. The quad icons
# emit a solid quad and four one-pixel coverage quads per shape in one fixed
# primitive: Hub 9, Sliders 6, Clock 11 and Books 13 shapes.
# test_stroke_gx_stream.py executes every icon on every face.
assert "drawFaceIcons(seconds, animated, clock, pad, icons, &raster);" in INDIGO
for icon, vertices in (("Hub", 180), ("Sliders", 120), ("Clock", 220), ("Books", 260)):
	assert f"GX_Begin(GX_QUADS, GX_VTXFMT0, {vertices});" in extract_function(
		INDIGO, f"static void draw{icon}Icon("
	)
# The little rails that framed the cube on four sides are gone everywhere.
assert "_DrawSpatialRails" not in FRAME_C
dispatch = extract_function(INDIGO, "static void drawOneFaceIcon(")
# Four icons per face, in face order; a face's choice picks one of its own.
# drawFaceIcons and the turned faces' pictures both draw through it.
assert "drawOneFaceIcon(raster, face, choices[face]" in extract_function(INDIGO, "static void drawFaceIcons(")
assert "drawOneFaceIcon(&fine, face, icons[face]" in extract_function(INDIGO, "static void renderFacePictures(")
assert "switch(face * UI_HOME_ICON_CHOICES + choice) {" in dispatch
for icon, name in (("CONTROLLER", "Controller"), ("BOOKS", "Books"), ("COVERS", "Covers"),
		("PLAY", "Play"), ("HUB", "Hub"), ("DISC", "Disc"), ("SD_CARD", "SdCard"),
		("FOLDER", "Folder"), ("SLIDERS", "Sliders"), ("GEAR", "Gear"), ("TOGGLES", "Toggles"),
		("DIAL", "Dial"), ("CLOCK", "Clock"), ("INFO", "Info"), ("POWER", "Power"),
		("CHIP", "Chip"), ("APPS", "Apps"), ("SAVES", "Saves"), ("EMULATORS", "Emulators"),
		("FILES", "Folder"), ("WEB", "Web")):
	assert f"case UI_HOME_ICON_{icon}:" in dispatch
	helper = extract_function(INDIGO, f"static void draw{name}Icon(")
	assert "int face" in helper and "UI_HOME_FACE_" not in helper
for helper in ("drawFaceCircle", "drawFaceRing", "drawControllerRect"):
	assert "UI_HOME_FACE_" not in extract_function(INDIGO, f"static void {helper}(")
decorative_strength = re.search(
	r"#define HOME_DECORATIVE_STRENGTH\s+([0-9.]+)f", INDIGO
)
assert decorative_strength and 0.0 < float(decorative_strength.group(1)) <= 0.8
assert "orbitStrength * HOME_DECORATIVE_STRENGTH" in INDIGO
# The quieter Home waves ease in and out with the scene's own spring; a test
# of the scene id here steps them on the frame the scene changes.
background_draw = extract_function(INDIGO, "void IndigoBackground_Draw(")
assert "scene->homeDecorativeBlend" in background_draw
assert "scene->scene == UI_SCENE_HOME" not in background_draw
system_motif = extract_function(INDIGO, "static void drawClockIcon(")
for frame_edge in (
	"-0.55f, 0.55f",
	"-0.55f, -0.61f",
	"-0.61f, -0.55f",
	"0.55f, -0.55f",
):
	assert frame_edge in system_motif


# --- System Information has one conventional exit: B. ---
assert "UISystem_FormatPageStatus(" in info_page
assert '"L/R  PAGE %d OF %d    B  BACK"' in SYSTEM_INFO
assert "Press A or B" not in info_page
interactive_info = show_info[show_info.index("while(1)") :]
assert "BUTTON_A" not in interactive_info
assert "if(btns & PAD_BUTTON_B)" in interactive_info
assert interactive_info.count("break;") == 1
assert "while (padsButtonsHeld() & BUTTON_B)" in interactive_info


print("four-face Home structural audit passed")
