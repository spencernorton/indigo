#!/usr/bin/env python3
"""Structural and mutation-backed gate for Home routing safety.

Pure lifecycle/input predicates are exercised by test_ui_home_safety.c. This
gate binds those predicates to the real Swiss startup, Recent, refresh,
selector, scan-failure, and guarded-Restart paths without executing libogc on
the host.
"""

from __future__ import annotations

from pathlib import Path
import re
import subprocess


if not __debug__:
    raise SystemExit(
        "audit_routing_safety.py requires Python assertions; rerun without -O/PYTHONOPTIMIZE"
    )


ROOT = Path(__file__).resolve().parents[3]
# The accepted base main.c, before Indigo's startup edits.
BASE_MAIN = Path(__file__).resolve().parent / "fixtures/main.base.c"
SWISS_PATH = ROOT / "cube/swiss/source/swiss.c"
MAIN_PATH = ROOT / "cube/swiss/source/main.c"
SAFETY_PATH = ROOT / "cube/swiss/source/gui/ui_home_safety.c"
FRAMEBUFFER_PATH = ROOT / "cube/swiss/source/gui/FrameBufferMagic.c"
DEVICE_HANDLER_PATH = ROOT / "cube/swiss/source/devices/deviceHandler.c"
UTIL_PATH = ROOT / "cube/swiss/source/util.c"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")


def base_file(path: str) -> str:
    assert path == "cube/swiss/source/main.c", path
    return read(BASE_MAIN)


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


def ordered(source: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        cursor = source.index(token, cursor) + len(token)


def check_helper(source: str) -> None:
    mounted = extract_block(source, "bool UIHomeSafety_SourceMounted(")
    ready = extract_block(source, "bool UIHomeSafety_SourceReady(")
    recent = extract_block(source, "bool UIHomeSafety_ShouldForceRecentInit(")
    selector = extract_block(source, "bool UIHomeSafety_SelectorReleasePending(")
    restart = extract_block(source, "bool UIHomeSafety_RestartReleasePending(")
    cancel = extract_block(source, "bool UIHomeSafety_AccumulateRestartCancel(")

    assert re.search(
        r"return\s+lifecycle\s*!=\s*NULL\s*&&\s*"
        r"lifecycle->state\s*==\s*UI_HOME_SOURCE_MOUNT_MOUNTED\s*&&\s*"
        r"lifecycle->handler\s*!=\s*NULL\s*&&\s*"
        r"lifecycle->handler\s*==\s*currentHandler\s*;",
        mounted,
        re.S,
    )
    assert re.search(
        r"return\s+available\s*&&\s*UIHomeSafety_SourceMounted\(",
        ready,
        re.S,
    )
    assert re.search(
        r"return\s+targetHandler\s*!=\s*NULL\s*&&\s*"
        r"targetHandler\s*==\s*currentHandler\s*&&\s*"
        r"!UIHomeSafety_SourceReady\(",
        recent,
        re.S,
    )
    assert "heldButtons & selectorButtons" in selector
    for axis in ("stickX <= -deadzone", "stickX >= deadzone",
                 "stickY <= -deadzone", "stickY >= deadzone"):
        assert axis in selector
    assert "return (heldButtons & confirmationButtons) != 0u;" in restart
    assert re.search(
        r"return\s+cancelRequested\s*\|\|\s*"
        r"\(heldButtons\s*&\s*backButton\)\s*!=\s*0u\s*;",
        cancel,
        re.S,
    )


# Upstream's startup autoload boots any official Swiss in the root of the card
# (z.dol, a.dol, boot.dol...) in place of Indigo, so Indigo's main.c drops it.
UPSTREAM_AUTOLOAD = (
    "\t\t\tif((devices[DEVICE_CUR]->features & FEAT_AUTOLOAD_DOL) && getenv(\"WIILOAD\") == NULL) {\n"
    "\t\t\t\tload_auto_dol(argc, argv);\n"
    "\t\t\t}\n"
)
NO_AUTOLOAD = (
    "\t\t\t/* No startup DOL autoload: upstream's boots any official Swiss in the\n"
    "\t\t\t * root (z.dol, a.dol, boot.dol...) in place of Indigo. */\n"
)


def expected_main(base: str) -> str:
    assert base.count(UPSTREAM_AUTOLOAD) == 1
    base = base.replace(UPSTREAM_AUTOLOAD, NO_AUTOLOAD, 1)
    anchor = "\t\t\tDrawLoadBackdrop(devices[DEVICE_CUR]);\n\t\t}\n\t}\n"
    replacement = (
        "\t\t\tDrawLoadBackdrop(devices[DEVICE_CUR]);\n\t\t}\n"
        "\t\telse {\n"
        "\t\t\t/* A detected handler is not a mounted source when boot init fails.\n"
        "\t\t\t * Clear only the pointer so same-device Autoload/Recent must retry init. */\n"
        "\t\t\tdevices[DEVICE_CUR] = NULL;\n"
        "\t\t}\n\t}\n"
    )
    assert base.count(anchor) == 1
    base = base.replace(anchor, replacement, 1)
    include = '#include "gui/FrameBufferMagic.h"\n'
    assert base.count(include) == 1
    base = base.replace(include, include + '#include "gui/ui_540p.h"\n', 1)
    # The experiment starts only after normal startup and the Cubeboot return,
    # is absent from ordinary builds, and cannot intercept source discovery.
    anchor = '\tif(swissSettings.cubebootInvoked) {\n\t\treturn 0;\n\t}\n\twhile(1) {'
    replacement = ('\tif(swissSettings.cubebootInvoked) {\n\t\treturn 0;\n\t}\n'
                   '#if UI_EXPERIMENTAL_540P\n\tUI540_Request(argc, argv);\n#endif\n\twhile(1) {')
    assert base.count(anchor) == 1
    return base.replace(anchor, replacement, 1)


def trailing_blank_free(text: str) -> str:
    """The vendored base has no trailing blanks (the whitespace gate refuses them in
    new files), so main.c is compared with trailing blanks ignored on both sides."""
    return re.sub(r"[ \t]+$", "", text, flags=re.M)


def check_runtime(swiss: str, main: str) -> None:
    assert trailing_blank_free(main) == trailing_blank_free(
        expected_main(base_file("cube/swiss/source/main.c"))), (
        "main.c changed outside its audited startup edits and opt-in experiment"
    )
    assert '#include "gui/ui_home_safety.h"' in swiss

    lifecycle_mounted = extract_block(swiss, "static bool homeSourceLifecycleMounted(")
    source_ready = extract_block(swiss, "static bool homeSourceIsReady(")
    source_from_path = extract_block(
        swiss, "static DEVICEHANDLER_INTERFACE *homeSourceFromPathIncludingUnavailable("
    )
    startup = extract_block(swiss, "static void homeSourceObserveStartup(")
    capabilities = extract_block(swiss, "static uiHomeCapabilities_t homeCapabilities(")
    recent = extract_block(swiss, "void select_recent_entry()")
    refresh = extract_block(swiss, "static void homeRefreshLibrary(")
    flippy_update = extract_block(swiss, "static void homeRunFlippyUpdate(")
    restart_drain = extract_block(swiss, "static bool homeDrainRestartInput(")
    selector_drain = extract_block(swiss, "static void homeDrainSelectorInput(void)\n{")
    restart_confirm = extract_block(swiss, "static void homeConfirmRestartEffect(")
    selector = extract_block(swiss, "static bool select_device_internal(")
    load_file = extract_block(
        swiss,
        "static void load_file_with_context(gameflowLaunchContext_t *context)\n{",
    )
    menu = extract_block(swiss, "void menu_loop()")

    assert "UIHomeSafety_SourceMounted(&homeSourceLifecycle" in lifecycle_mounted
    assert "UIHomeSafety_SourceReady(&homeSourceLifecycle" in source_ready
    assert "getDeviceFromPath(path)" in source_from_path
    assert "allDevices[i]" in source_from_path
    assert "strncmp(handler->initial->name, path, prefixLength)" in source_from_path
    assert "deviceHandler_getDeviceAvailable" not in source_from_path
    assert ".hasSource = homeSourceIsReady()" in capabilities
    ordered(
        startup,
        "devices[DEVICE_CUR] != NULL && !needsDeviceChange",
        "UI_HOME_SOURCE_MOUNT_MOUNTED",
        "UI_HOME_SOURCE_MOUNT_UNMOUNTED",
    )
    assert menu.index("homeSourceObserveStartup();") < menu.index("homePublish(")

    ordered(
        recent,
        "homeSourceFromPathIncludingUnavailable(",
        "previousSourceMounted = homeSourceLifecycleMounted()",
        "targetWasAvailable",
        "UIHomeSafety_ShouldForceRecentInit(",
        "if(forceReinit)",
        "freeFiles();",
        "DrawGameflowCancelPosters();",
        "if(previousSourceMounted)",
        "previousSource->deinit(previousSource->initial);",
        "UI_HOME_SOURCE_MOUNT_UNMOUNTED",
        "devices[DEVICE_CUR] = NULL;",
        "deviceHandler_setDeviceAvailable(targetSource, true);",
        "find_existing_entry(",
        "deviceHandler_setDeviceAvailable(targetSource, false);",
        "res != RECENT_ERR_DEV_MISSING",
        "UI_HOME_SOURCE_MOUNT_MOUNTED",
        "previousSourceMounted",
        "UI_HOME_SOURCE_MOUNT_MOUNTED",
    )

    source_switch = extract_block(
        recent,
        "if(devices[DEVICE_CUR] != NULL &&\n\t\t\tdevices[DEVICE_CUR] != targetSource)",
    )
    assert "DrawGameflowCancelPosters();" in source_switch
    assert recent.index(source_switch) < recent.index("find_existing_entry(")

    ordered(
        refresh,
        "freeFiles();",
        "DrawGameflowCancelPosters();",
        "wkfReinit();",
        "source->deinit(source->initial);",
        "UI_HOME_SOURCE_MOUNT_UNMOUNTED",
        "ret = source->init(source->initial);",
        "devices[DEVICE_PREV] = source;",
        "devices[DEVICE_CUR] = NULL;",
        "UI_HOME_SOURCE_MOUNT_ABSENT",
        "needsRefresh = 0;",
        "needsDeviceChange = 1;",
        "deviceHandler_setDeviceAvailable(source, true);",
        "UI_HOME_SOURCE_MOUNT_MOUNTED",
        "needsRefresh = 1;",
    )
    flippy = extract_block(load_file, 'else if(endsWith(fileName,".fpkg"))')
    assert "homeFlippyUpdatePending = true;" in flippy
    # A firmware update asks first: only A on the question queues it.
    ordered(flippy, "if(confirmAction(", "homeFlippyUpdatePending = true;")
    for unsafe_inline in ("freeFiles();", "flippy_closefrom(1);", "flippy_reset();"):
        assert unsafe_inline not in flippy
    ordered(
        flippy_update,
        "freeFiles();",
        "DrawGameflowCancelPosters();",
        "flippy_closefrom(1);",
        "flippy_reset();",
        "UI_HOME_SOURCE_MOUNT_UNMOUNTED",
        "needsDeviceChange = 1;",
        "needsRefresh = 0;",
        "refreshDeviceCode(false);",
        "flippy_boot(FLIPPY_MODE_UPDATE);",
        "deviceHandler_Flippy_test()",
        "needsDeviceChange = 1;",
        "needsRefresh = 0;",
    )
    ordered(
        menu,
        "renderFile",
        "if(homeFlippyUpdatePending)",
        "DrawDispose(filePanel);",
        "filePanel = NULL;",
        "homeRunFlippyUpdate();",
    )
    service = menu.rindex("if(homeFlippyUpdatePending)")
    assert menu.index("homeDispatchEffect(effect);") < service
    assert service < menu.rindex("if(needsDeviceChange)")

    assert "UIHomeSafety_RestartReleasePending(" in restart_drain
    assert "UIHomeSafety_AccumulateRestartCancel(" in restart_drain
    assert "padsStick" not in restart_drain
    ordered(
        restart_confirm,
        "homeDrainRestartInput(false)",
        "UIHome_Apply(&homeState, UI_HOME_INPUT_BACK",
        "homePublish(true);",
        "return;",
        "homeRestartSwiss();",
    )

    for button in (
        "BUTTON_A", "BUTTON_B", "BUTTON_X", "BUTTON_Y", "BUTTON_START",
        "BUTTON_Z", "BUTTON_L", "BUTTON_R", "BUTTON_LEFT", "BUTTON_RIGHT",
        "BUTTON_UP", "BUTTON_DOWN",
    ):
        assert button in swiss[swiss.index("#define HOME_CONFIRMATION_BUTTONS"):
                               swiss.index("static bool homeDrainRestartInput")]
    assert "UIMenuInput_Init(&homeMenuInput);" in selector_drain
    assert "homeMenuInputRetrace = VIDEO_GetRetraceCount();" in selector_drain
    assert "padsStick" not in selector_drain
    assert "homeFilterQuarantinedAnalog" not in swiss
    home_input = extract_block(menu, "else if (curMenuLocation==ON_OPTIONS)")
    assert home_input.count("padsMenuInputPoll(&homeMenuInput,") == 2
    assert "allowedAxes, true);" in home_input
    assert "padsStickX" not in home_input and "padsStickY" not in home_input

    cancel = extract_block(selector, "if(btns & BUTTON_B)")
    assert "homeDrainSelectorInput();" in cancel
    assert "deinit(" not in cancel and "freeFiles(" not in cancel
    assert selector.index("if(btns & BUTTON_B)") < selector.index("if(btns & BUTTON_A)")

    scan = extract_block(menu, "if(devices[DEVICE_CUR] != NULL && needsRefresh)")
    failure = extract_block(scan, "if(getCurrentDirEntryCount()<=0)")
    ordered(
        failure,
        "deinit(",
        "UI_HOME_SOURCE_MOUNT_UNMOUNTED",
        "devices[DEVICE_CUR] = NULL;",
        "UI_HOME_SOURCE_MOUNT_ABSENT",
        "needsDeviceChange=1;",
    )


def check_lifecycle_isolation(
    framebuffer: str,
    device_handler: str,
    util: str,
) -> None:
    assert '#include "gui/FrameBufferMagic.h"' not in device_handler
    assert "DrawGameflowCancelPosters" not in device_handler
    assert '#include "gui/FrameBufferMagic.h"' not in util
    assert "DrawGameflowCancelPosters" not in util
    assert re.search(
        r"static\s+sys_resetinfo\s+resetinfo\s*=\s*\{\s*"
        r"\{NULL,\s*NULL\},\s*onreset,\s*1\s*\};",
        device_handler,
        re.S,
    )

    reset_handler = extract_block(
        framebuffer, "static s32 _GameflowOnReset(s32 final)\n{"
    )
    assert "if(!final)" in reset_handler
    assert "DrawGameflowCancelPosters();" in reset_handler
    assert re.search(
        r"static\s+sys_resetinfo\s+gameflowResetInfo\s*=\s*\{\s*"
        r"\{NULL,\s*NULL\},\s*_GameflowOnReset,\s*0\s*\};",
        framebuffer,
        re.S,
    )
    draw_init = extract_block(framebuffer, "void DrawInit(")
    ordered(
        draw_init,
        "LWP_MutexInit(&_videomutex, false);",
        "SYS_RegisterResetFunc(&gameflowResetInfo);",
        "gameflowResetRegistered = true;",
    )
    draw_shutdown = extract_block(framebuffer, "void DrawShutdown(")
    ordered(
        draw_shutdown,
        "DrawGameflowCancelPosters();",
        "SYS_UnregisterResetFunc(&gameflowResetInfo);",
        "gameflowResetRegistered = false;",
        "LWP_JoinThread(thread, NULL);",
        "UIAssets_DisposeAfterVideoStop();",
        "LWP_MutexDestroy(mutex);",
    )


def expect_rejected(label: str, check, source: str, *args: str) -> None:
    try:
        check(source, *args)
    except (AssertionError, ValueError):
        return
    raise AssertionError(f"unsafe mutation escaped audit: {label}")


SWISS = read(SWISS_PATH)
MAIN = read(MAIN_PATH)
SAFETY = read(SAFETY_PATH)
FRAMEBUFFER = read(FRAMEBUFFER_PATH)
DEVICE_HANDLER = read(DEVICE_HANDLER_PATH)
UTIL = read(UTIL_PATH)

check_helper(SAFETY)
check_runtime(SWISS, MAIN)
check_lifecycle_isolation(FRAMEBUFFER, DEVICE_HANDLER, UTIL)

helper_mutants = [
    ("mounted OR topology", SAFETY.replace(
        "lifecycle->state == UI_HOME_SOURCE_MOUNT_MOUNTED &&",
        "lifecycle->state == UI_HOME_SOURCE_MOUNT_MOUNTED ||", 1)),
    ("mounted identity inversion", SAFETY.replace(
        "lifecycle->handler == currentHandler", "lifecycle->handler != currentHandler", 1)),
    ("availability OR topology", SAFETY.replace(
        "return available &&", "return available ||", 1)),
    ("Recent skips readiness", SAFETY.replace(
        "!UIHomeSafety_SourceReady(lifecycle, currentHandler, available)",
        "available", 1)),
    ("Restart release inverted", SAFETY.replace(
        "(heldButtons & confirmationButtons) != 0u",
        "(heldButtons & confirmationButtons) == 0u", 1)),
    ("late-B latch AND topology", SAFETY.replace(
        "return cancelRequested ||", "return cancelRequested &&", 1)),
]
for label, mutant in helper_mutants:
    expect_rejected(label, check_helper, mutant)

runtime_mutants = [
	("540p experiment enabled in ordinary builds", MAIN.replace(
		"#if UI_EXPERIMENTAL_540P\n\tUI540_Request(argc, argv);\n#endif",
		"\tUI540_Request(argc, argv);", 1), SWISS),
	("540p experiment intercepts Cubeboot return", MAIN.replace(
		"\tif(swissSettings.cubebootInvoked) {\n\t\treturn 0;\n\t}",
		"\tif(swissSettings.cubebootInvoked) {\n\t\tUI540_Request(argc, argv);\n\t\treturn 0;\n\t}", 1), SWISS),
    ("startup failure pointer retained", MAIN.replace(
        "\t\t\tdevices[DEVICE_CUR] = NULL;\n", "", 1), SWISS),
    ("startup DOL autoload restored", MAIN.replace(
        NO_AUTOLOAD, UPSTREAM_AUTOLOAD, 1), SWISS),
    ("Recent same-handler retry removed", MAIN, SWISS.replace(
        "if(forceReinit) {", "if(false) {", 1)),
    ("unavailable Recent path fallback removed", MAIN, SWISS.replace(
        "\t\thandler = allDevices[i];\n", "\t\thandler = NULL;\n", 1)),
    ("Recent stale cache retained", MAIN, SWISS.replace(
        "\t\t\tfreeFiles();\n\t\t\tDrawGameflowCancelPosters();\n",
        "", 1)),
    ("WKF remount removed", MAIN, SWISS.replace(
        "\t\tret = source->init(source->initial);\n", "", 1)),
    ("WKF cache retained across remount", MAIN, SWISS.replace(
        "\t\tfreeFiles();\n\t\tDrawGameflowCancelPosters();\n",
        "\t\tDrawGameflowCancelPosters();\n", 1)),
    ("WKF selector identity lost", MAIN, SWISS.replace(
        "\t\t\tdevices[DEVICE_PREV] = source;\n", "", 1)),
    ("Flippy refreshes dead session", MAIN, SWISS.replace(
		"\tneedsRefresh = 0;\n}\n\nstatic void homeRestartSwiss",
		"\tneedsRefresh = 1;\n}\n\nstatic void homeRestartSwiss", 1)),
    ("Flippy cache retained across reset", MAIN, SWISS.replace(
		"\tfreeFiles();\n\tDrawGameflowCancelPosters();\n",
        "", 1)),
    ("Flippy reset runs under browser lock", MAIN, SWISS.replace(
        "\t\t\t\thomeFlippyUpdatePending = true;\n",
        "\t\t\t\tflippy_reset();\n", 1)),
    ("Restart waits on analog", MAIN, SWISS.replace(
        "while(UIHomeSafety_RestartReleasePending(",
        "while(abs(padsStickX()) >= 24 || UIHomeSafety_RestartReleasePending(", 1)),
    ("selector cancel teardown", MAIN, SWISS.replace(
        "\t\t\thomeDrainSelectorInput();\n",
        "\t\t\tfreeFiles();\n\t\t\thomeDrainSelectorInput();\n", 1)),
    ("neutral re-arm omitted from selector drain", MAIN, SWISS.replace(
        "\tUIMenuInput_Init(&homeMenuInput);\n"
        "\thomeMenuInputRetrace = VIDEO_GetRetraceCount();\n",
        "", 1)),
    ("scan failure remains mounted", MAIN, "".join(SWISS.rsplit(
        "\t\t\t\thomeSourceRecord(devices[DEVICE_CUR],\n"
        "\t\t\t\t\tUI_HOME_SOURCE_MOUNT_UNMOUNTED);\n", 1))),
]
for label, mutant_main, mutant_swiss in runtime_mutants:
    expect_rejected(label, check_runtime, mutant_swiss, mutant_main)

lifecycle_mutants = [
    ("reset poster teardown removed", FRAMEBUFFER.replace(
        "\t\tDrawGameflowCancelPosters();\n", "", 1), DEVICE_HANDLER, UTIL),
    ("device teardown priority restored", FRAMEBUFFER, DEVICE_HANDLER.replace(
        "\t{NULL, NULL}, onreset, 1\n", "\t{NULL, NULL}, onreset, 0\n", 1), UTIL),
    ("renderer dependency restored in device layer", FRAMEBUFFER,
        DEVICE_HANDLER.replace(
            '#include "flippy.h"\n',
            '#include "flippy.h"\n#include "gui/FrameBufferMagic.h"\n',
            1,
        ), UTIL),
    ("reset callback unregister removed", FRAMEBUFFER.replace(
        "\t\tSYS_UnregisterResetFunc(&gameflowResetInfo);\n", "", 1),
        DEVICE_HANDLER, UTIL),
]
for label, mutant_framebuffer, mutant_device, mutant_util in lifecycle_mutants:
    expect_rejected(
        label,
        check_lifecycle_isolation,
        mutant_framebuffer,
        mutant_device,
        mutant_util,
    )

print(
    "routing safety audit passed "
    f"({len(helper_mutants) + len(runtime_mutants) + len(lifecycle_mutants)} "
    "unsafe mutations rejected)"
)
