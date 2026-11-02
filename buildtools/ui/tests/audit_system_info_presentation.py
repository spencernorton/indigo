#!/usr/bin/env python3
"""Fail-closed structural guardrails for the retained System Information UI."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"


def fail(message: str) -> None:
    raise SystemExit(f"System Information presentation audit failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")


def extract_function(source: str, marker: str) -> str:
    try:
        start = source.index(marker)
        opening = source.index("{", start)
    except ValueError as error:
        fail(f"missing function marker {marker!r}: {error}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    fail(f"unterminated function {marker!r}")


info_path = GUI / "info.c"
info_bytes = info_path.read_bytes()
info = read(info_path)
policy = read(GUI / "ui_system_info.c")
header = read(GUI / "ui_system_info.h")
makefile = read(ROOT / "buildtools/ui/tests/Makefile")

draw_page = extract_function(info, "uiDrawObj_t * info_draw_page(")
show_info = extract_function(info, "void show_info()")
overview = extract_function(info, "static void infoDrawOverview(")
console = extract_function(info, "static void infoDrawConsole(")
connections = extract_function(info, "static void infoDrawConnections(")
input_output = extract_function(info, "static void infoDrawInputOutput(")
about = extract_function(info, "static void infoDrawAbout(")
credits = extract_function(info, "static void infoDrawCredits(")
copy_fitted = extract_function(policy, "float UISystem_CopyFitted(")

# The repository was normalised to LF throughout when the public-release
# export gate refused carriage returns as control characters. The contract is
# unchanged in intent: one coherent convention, no mixed endings, no stray CRs.
require(info_bytes.count(b"\n") > 0, "info.c has no line endings at all")
require(b"\r" not in info_bytes,
        "info.c contains carriage returns; this tree is LF-only")

# Six authored pages retain all legacy discovery while adding the missing
# overview. Page identity and geometry live in a pure host-tested policy.
require("#define UI_SYSTEM_PAGE_COUNT 6" in header, "page count is not six")
for page in (
    "UI_SYSTEM_PAGE_OVERVIEW",
    "UI_SYSTEM_PAGE_CONSOLE",
    "UI_SYSTEM_PAGE_CONNECTIONS",
    "UI_SYSTEM_PAGE_IO",
    "UI_SYSTEM_PAGE_ABOUT",
    "UI_SYSTEM_PAGE_CREDITS",
):
    require(page in draw_page, f"page renderer omits {page}")
require("UISystem_ComputeLayout(page_num, &layout);" in draw_page,
        "renderer bypasses pure layout")
require("UISystem_FormatPageStatus(" in draw_page,
        "single command rail is not policy-owned")
require("L/R  PAGE %d OF %d    B  BACK" in policy,
        "command rail does not expose page navigation and B-only exit")

# Every boxed surface is validated with its implicit border expansion by the
# pure test; essential dynamic values retain a 0.60 native-grid floor.
require("#define UI_SYSTEM_TEXT_SCALE_FLOOR 0.60f" in header,
        "native-grid text floor regressed")
require("UI_SYSTEM_SAFE_X0 32" in header and "UI_SYSTEM_SAFE_X1 608" in header,
        "horizontal safe area is not explicit")
require("UI_SYSTEM_SAFE_Y0 34" in header and "UI_SYSTEM_SAFE_Y1 438" in header,
        "vertical safe area is not explicit")
require("UI_SYSTEM_ELLIPSIS_BYTE" in copy_fitted,
        "long runtime values do not ellipsize")
# One list builds every test plain and sanitized, and the runner runs it.
require("test_ui_system_info" in makefile, "missing host target test_ui_system_info")

# Runtime truth is captured on the menu thread before DrawRepublish. There are
# no video-time callbacks, formatters, or hardware probes hidden in labels.
require("DrawDynamicLabel" not in info,
        "per-frame dynamic label returned to System Information")
for forbidden in ("sprintf(", "strcpy(", "strcat("):
    require(forbidden not in info, f"unbounded formatter {forbidden} returned")
for forbidden in ("snprintf(", "sprintf(", "strftime(", "DrawDynamicLabel"):
    require(forbidden not in show_info,
            f"input/render loop contains {forbidden}")
require("DrawRepublish(pagePanel, info_draw_page(page))" in show_info,
        "page snapshot is not retained")
require("DrawDispose(pagePanel);" in show_info,
        "retained page is not disposed")
require("pollFrames >= 50u" in show_info and "infoCurrentMinute()" in show_info,
        "overview does not cache minute formatting off the frame renderer")

# Overview and detail pages expose the P5 facts without inventing health or
# temperature data. Missing thermal/source states remain explicit.
for token in (
    "UISystem_FormatClock",
    "UISystem_FormatDate",
    "CoreTemperature()",
    "UISystem_FormatTemperature",
    "UISystem_FormatCalibration",
    "deviceHandler_getDeviceAvailable",
    "UISystem_SourceHealth",
    "infoGetVideoMode",
    "UISystem_RegionName",
    "GIT_REVISION",
    "GIT_COMMIT",
):
    require(token in overview, f"overview omits {token}")
require('"UNAVAILABLE"' in policy and '"NOT MOUNTED"' in policy,
        "unavailable facts are not truthful")
require('"CPU THERMAL / MINUTE SAMPLE"' in overview,
        "Overview thermal timing is not disclosed")

# The redesign must remain additive: mechanically retain representative facts
# from every one of the five legacy domains, not merely the new Overview.
for token in (
    "infoGetConsoleModel",
    "infoGetIplVersion",
    "infoGetCpu",
    "infoGetSystemOnChip",
    "mfspr(ECID0)",
    "mfspr(ECID1)",
    "mfspr(ECID2)",
    "mfspr(ECID3)",
):
    require(token in console, f"Console page omits legacy fact {token}")
for token in (
    "LOC_MEMCARD_SLOT_A",
    "LOC_MEMCARD_SLOT_B",
    "LOC_SERIAL_PORT_1",
    "LOC_SERIAL_PORT_2",
    "LOC_DVD_CONNECTOR",
    "LOC_HSP",
    "devices[DEVICE_CUR]",
    "devices[DEVICE_CONFIG]",
    "getExiTypeByLocation(LOC_MEMCARD_SLOT_A)",
    "getExiTypeByLocation(LOC_MEMCARD_SLOT_B)",
):
    require(token in connections, f"Connections page omits legacy fact {token}")
require('"REOPEN PAGE TO REFRESH"' in connections,
        "captured peripheral snapshot is not labeled")
for socket in ("PAD_CHAN0", "PAD_CHAN1", "PAD_CHAN2", "PAD_CHAN3"):
    require(socket in input_output, f"input page omits {socket}")
for token in (
    "getDTVStatus()",
    "getRawDTVStatus()",
    "swissSettings.sramStereo",
    "sramLanguageStr[swissSettings.sramLanguage]",
):
    require(token in input_output, f"Input / Output page omits legacy fact {token}")
require('"REOPEN PAGE TO REFRESH"' in input_output,
        "Input / Output page-entry snapshot is not disclosed")
for identity in ("SWISS 0.6", "INDIGO - UNOFFICIAL SWISS FORK", "GIT_COMMIT",
                 "GIT_REVISION", "_V_STRING", "GITHUB.COM/SPENCERNORTON/INDIGO",
                 "UI_SYSTEM_ABOUT_HELP_LABEL", "UI_SYSTEM_ABOUT_HELP_TEXT"):
    require(identity in about, f"About page omits {identity}")
# Indigo's problems are Indigo's: About sends nobody to upstream's community.
require("GC-FOREVER" not in info and "EFNET" not in info,
        "About page points Indigo users to upstream Swiss's community")
require('#define UI_SYSTEM_ABOUT_HELP_TEXT "GITHUB ISSUES  /  DISCUSSIONS"'
        in header, "About's help line does not name Indigo's GitHub")
for credit in (
    "CURRENT PATREON SUPPORTERS",
    "HISTORICAL PATREON SUPPORTERS",
    "BORG NUMBER ONE",
    "RAMBLINGOKIE",
    "EXTRA GREETZ",
    "THANKS TO YOU",
):
    require(credit in credits, f"Credits page omits legacy content {credit}")
# The Spotlight layout's design comes from Gameplay Spotlight, with its
# author's permission, and the Jewel Case layout is portlandhodl's jewel case
# view, after Ann Nguyen's CD Case: the credits stay.
for credit in ('"LIBRARY DESIGNS"', '"GAMEPLAY SPOTLIGHT BY MVIZENSK"'):
    require(credit in credits, "Credits page omits Gameplay Spotlight's author")
require('"JEWEL CASE BY PORTLANDHODL, AFTER CD CASE BY ANN NGUYEN"' in credits,
        "Credits page omits the jewel case's authors")

# Input remains exactly the established B-only, one-level return contract.
interactive = show_info[show_info.index("while(1)") :]
require("BUTTON_A" not in interactive, "A exits or acts inside System Information")
# Swiss 45af0e6 uses the common controller mask for the exit too.
require("if(btns & BUTTON_B)" in interactive, "common-controller B exit is missing")
require(interactive.count("break;") == 1, "more than one exit path exists")
require("while (padsButtonsHeld() & BUTTON_B)" in interactive,
        "B release is not drained")
require("UI_SYSTEM_PAGE_COUNT - 1" in interactive,
        "navigation is not bounded to the authored page count")

print("System Information presentation audit OK")
