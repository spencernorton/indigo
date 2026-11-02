#!/usr/bin/env python3
"""Fail-closed structural guardrails for the retained Detail presentation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def fail(message: str) -> None:
    raise SystemExit(f"detail presentation audit failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


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


framebuffer = (
    ROOT / "cube/swiss/source/gui/FrameBufferMagic.c"
).read_text()
detail_source = (
    ROOT / "cube/swiss/source/gui/ui_gameflow_detail.c"
).read_text()
detail_header = (
    ROOT / "cube/swiss/source/gui/ui_gameflow_detail.h"
).read_text()

renderer = extract_function(
    framebuffer, "static void _GameflowDrawDetailDashboard("
)
metadata = extract_function(
    framebuffer, "static void _GameflowDrawMetadata("
)
for forbidden in (
    "snprintf(",
    "sprintf(",
    "strcpy(",
    "strcat(",
    "_GameflowAppendAction(",
    "GetTextScaleToFitInWidth",
    "UIAssets_DominantColor(",
):
    require(forbidden not in renderer, f"render path contains {forbidden}")
    require(forbidden not in metadata,
            f"Library metadata render path contains {forbidden}")

for retained_field in (
    "detail->statusText",
    "detail->lastPlayedText",
    "detail->savesSummary",
    "detail->savesUpdated",
    "detail->cheatSummary",
    "detail->cheatPreview",
    "detail->settingsSummary",
    "detail->settingsPreview",
    "detail->launchLabel",
    "detail->primaryActions",
    "detail->advancedLineOne",
    "detail->advancedLineTwo",
):
    require(retained_field in renderer, f"renderer ignores {retained_field}")

require("_GameflowDrawDetailPlanes(" in renderer, "selected CTA planes missing")
require('"STARTING GAME..."' in renderer, "launch feedback missing")
# One line, drawn with button icons for the button names.
require("_DrawHintText(320, 433, detail->primaryActions" in renderer,
        "Detail command rail is not single-owner/single-line")

prepare = extract_function(
    framebuffer, "static void _GameflowPrepareDetailPresentation("
)
require("UIAssets_DominantColor(" in prepare,
        "cover accent is not cached at publication")
require(prepare.count("_GameflowPrepareDetailText(") >= 10,
        "dynamic typography is not fully cached")

card_prepare = extract_function(
    framebuffer, "static void _GameflowPrepareCardPresentation("
)
require(card_prepare.count("_GameflowPrepareCardText(") == 3,
        "Library metadata typography is not cached")
copy_snapshot = extract_function(
    framebuffer, "static void _GameflowCopySnapshot("
)
prepare_snapshot = extract_function(framebuffer, "static void _GameflowPrepareSnapshot(")
require("_GameflowPrepareCardPresentation(snapshot, i," in prepare_snapshot and
        "_GameflowPrepareSpotlight(snapshot," in prepare_snapshot,
        "Library metadata is not prepared from the immutable snapshot")
require("_GameflowPrepare" not in copy_snapshot and
        "presentation->cards" in copy_snapshot and "presentation->spotlightLines" in copy_snapshot,
        "Library publication must copy prepared text without fitting under the video lock")
update_snapshot = extract_function(framebuffer, "bool DrawUpdateGameflow(")
require(update_snapshot.index("_GameflowPrepareSnapshot(snapshot,") <
        update_snapshot.index("LWP_MutexLock(_videomutex)"),
        "Library text preparation blocks the video thread")

publish = extract_function(framebuffer, "bool DrawUpdateGameflowDetail(")
copy_at = publish.find("memcpy(&data->detail, snapshot")
prepare_at = publish.find("_GameflowPrepareDetailPresentation(data)")
publish_at = publish.find("updated = true")
require(-1 not in (copy_at, prepare_at, publish_at),
        "publication transaction is incomplete")
require(copy_at < prepare_at < publish_at,
        "presentation must be prepared inside the publication transaction")

build = extract_function(detail_source, "bool UIGameflowDetail_Build(")
clear_capability = build.find("UI_GAMEFLOW_DETAIL_CAN_CHEATS")
format_at = build.find("buildPresentation(snapshot)")
require(clear_capability >= 0 and format_at > clear_capability,
        "presentation was formatted before final capabilities were known")

for field in (
    "statusText",
    "lastPlayedText",
    "savesSummary",
    "savesUpdated",
    "cheatSummary",
    "cheatPreview",
    "settingsSummary",
    "settingsPreview",
    "launchLabel",
    "primaryActions",
    "advancedLineOne",
    "advancedLineTwo",
):
    require(f"char {field}[" in detail_header,
            f"pointer-free snapshot field {field} missing")

# A game's own settings get their own inset, formatted with the rest of the
# presentation once capabilities are final; the X action keeps a plain name,
# so the count is said once.
settings_line = extract_function(detail_source,
                                 "static void buildSettingsPresentation(")
require(build.find("buildSettingsPresentation(snapshot, source->firstCustomSetting)") >
        format_at, "the settings line is formatted before the presentation")
require("UI_GAMEFLOW_DETAIL_CAN_SETTINGS" in settings_line and
        '"Game Defaults"' in settings_line and '"%lu custom"' in settings_line,
        "the settings line does not say whether the game has its own settings")
require('"SETTINGS"' in renderer and
        renderer.index('"SETTINGS"') < renderer.index('"CHEATS"'),
        "the dashboard does not show SETTINGS beside CHEATS")
require("hasSettings" in extract_function(framebuffer,
        "static void _GameflowDrawDetailPlanes("),
        "the settings inset has no panel")

presentation = extract_function(detail_source, "static void buildPresentation(")
require("CUSTOM)" not in presentation,
        "the X action repeats the custom count the settings inset shows")
for primary in ('"D-PAD  MOVE"', '"A  SELECT"', '"B  LIBRARY"', '"X  SETTINGS"',
                '"Y  CHEATS"'):
    require(primary in presentation, f"primary action {primary} missing")

# The focused row carries the highlight, and Launch's dot only when focused.
planes = extract_function(framebuffer, "static void _GameflowDrawDetailPlanes(")
require("_GameflowPutBorder(&litRow, &litInner, litEdgeColor);" in planes and
        "rowTop[focusRow]" in planes, "the focused row has no bright frame")
require("_GameflowDrawDetailPlanes(detail, presentation, frame, alpha, focusRow,\n"
        "\t\tlit);" in renderer, "the planes do not know the focus")
require("if(focusRow == UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH) {\n"
        "\t\tdrawStringMedium(278, layout->launch, \"\\267\"" in renderer,
        "Launch's dot does not follow the focus")
for advanced in ('"Z  AUTOLOAD"', '"R  VERIFY"', '"L+A  CLEAN BOOT"'):
    require(advanced in presentation, f"advanced shortcut {advanced} missing")

saves_line = extract_function(detail_source, "static void buildSavesPresentation(")
require("UISaves_FormatUpdated(" in saves_line and "snapshot->saveStats = *stats;" in saves_line,
        "save snapshot or shared date formatter missing")
# SAVES needs a scan, and below two copies needs Always or card news. The
# behavioral tests cover each state; keep the retained presentation wired to
# that preference without moving work into the renderer.
require("if(stats == NULL || (stats->saves < 2u && !always && !waiting && !cardFailed)) {\n\t\treturn;" in saves_line and
        "snapshot->flags |= UI_GAMEFLOW_DETAIL_HAS_SAVES;" in saves_line,
        "SAVES ignores its scan, visibility preference or card state")
require("buildSavesPresentation(snapshot, source->saveStats, source->savesAlways," in build,
        "the retained SAVES presentation ignores the Always preference")
require('waiting ? "Memory card loading" : "Memory card didn\'t load"' in saves_line,
        "fewer than two copies hide a card that is loading or was given up on")
require("stats->partial" in saves_line and "stats->checkedSources == 0u" in saves_line and
        '"Partial scan | "' in saves_line,
        "a partial save scan would look complete")
require("UI_GAMEFLOW_DETAIL_HAS_SAVES | UI_GAMEFLOW_DETAIL_SAVE_CHOICE)) |" in build,
        "a caller's flags could show SAVES or its choice without a scan")
require(renderer.index('"SAVES"') < renderer.index('"SETTINGS"') < renderer.index('"CHEATS"'),
        "save copies are not above the settings and cheats insets")
require("if(hasSaves) {\n\t\t\t_GameflowPutDetailPanel(260, layout->saves, 330,\n"
        "\t\t\t\tlayout->savesHeight, 2," in planes,
        "read-only save inset has no panel, or one without SAVES")
require("if(detail->flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) {\n" in renderer and
        "drawStringMedium(576, layout->savesLine[0],\n"
        "\t\t\t(detail->flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) ?\n"
        "\t\t\t\"\\253 SAVES \\273\" : \"SAVES\"" in renderer,
        "the SAVES inset ignores its visibility flag, or hides its choice")
# The Manual row: only for a game with a manual (Build sets the flag from the
# pages it is given), above Settings, and a panel only then.
require("UI_GAMEFLOW_DETAIL_HAS_MANUAL |\n" in build and
        "buildManualPresentation(snapshot, source->manualPages);" in build,
        "a caller's flags could show Manual without a manual")
require("if(detail->flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) {\n"
        "\t\tdrawStringMedium(274, layout->manual, \"MANUAL\"" in renderer and
        renderer.index('"SAVES"') < renderer.index('"MANUAL"') < renderer.index('"SETTINGS"'),
        "the Manual row draws without a manual, or not between SAVES and SETTINGS")
require("(row == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL && !hasManual)) {\n\t\t\t\tcontinue;" in planes and
        "if(focusRow == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL && !hasManual) {" in planes,
        "the Manual row's panel or focus frame draws without a manual")
for forbidden in ("Saves_CollectGameStats(", "UISaves_FormatUpdated(", "->readFile(", "->readDir("):
    require(forbidden not in renderer and forbidden not in planes,
            f"Detail draw performs menu-thread save work: {forbidden}")

print("detail presentation audit OK")
