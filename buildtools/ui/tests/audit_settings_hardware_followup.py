#!/usr/bin/env python3
"""Fail-closed structural audit for hardware-driven Settings corrections."""

from __future__ import annotations

import ast
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"


def read(path: Path) -> str:
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")
    # What is said, not the language it is drawn in: Menu Language's
    # lookups (UI_T(...)) left out.
    return re.sub(r'\bUI_T\(((?:"(?:\\.|[^"\\])*"|[^()"])*)\)', r"\1", text)


def extract_function(source: str, marker: str) -> str:
    try:
        start = source.index(marker)
        opening = source.index("{", start)
    except ValueError as error:
        raise ValueError(f"missing function {marker!r}: {error}") from error
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise ValueError(f"unterminated function {marker!r}")


def tooltip_inventory(settings: str) -> tuple[int, int, int]:
    assignment = re.compile(
        r"\[(SET_[A-Z0-9_]+)\]\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+),?"
    )
    literal = re.compile(r'\"(?:[^\"\\]|\\.)*\"')
    count = 0
    maximum_lines = 0
    maximum_line_length = 0
    for match in assignment.finditer(settings):
        parts = literal.findall(match.group(2))
        try:
            text = "".join(ast.literal_eval(part) for part in parts)
        except (SyntaxError, ValueError) as error:
            raise ValueError(f"cannot parse tooltip {match.group(1)}: {error}") from error
        lines = text.splitlines() or [""]
        count += 1
        maximum_lines = max(maximum_lines, len(lines))
        maximum_line_length = max(maximum_line_length, *(len(line) for line in lines))
    return count, maximum_lines, maximum_line_length


def validate(files: dict[str, str]) -> list[str]:
    errors: list[str] = []

    def need(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    def ordered(text: str, *tokens: str) -> bool:
        positions = [text.find(token) for token in tokens]
        return -1 not in positions and positions == sorted(positions)

    settings = files["settings"]
    layout_c = files["layout_c"]
    layout_h = files["layout_h"]
    makefile = files["makefile"]
    layout_test = files["layout_test"]
    menu_test = files["menu_test"]
    semantics = files["semantics"]
    framebuffer = files["framebuffer"]

    try:
        show = extract_function(settings, "int show_settings_view(")
        wait_input = extract_function(settings, "static u32 settingsWaitForInput(")
        drain = extract_function(
            settings, "static void settingsInhibitThroughDigitalRelease("
        )
        direction_map = extract_function(
            settings, "static u32 settingsButtonForMenuDirection("
        )
        blocking = extract_function(settings, "static bool settingsInputMayBlock(")
        repeat = extract_function(settings, "static bool settingsHoldToRepeat(")
        changed = extract_function(settings, "static bool settingsChanged(")
        live_rows = extract_function(
            settings, "static bool settingsIsLiveVideoRow(int page, int option)\n{")
        keep_video = extract_function(settings, "static bool settingsKeepVideoMode(")
        change_value = extract_function(settings, "static void settingsChangeValue(")
        apply_video = extract_function(settings, "static void settingsApplyVideoMode(")
        restore_video = extract_function(settings, "static void settingsVideoRestore(")
        pick = extract_function(settings, "static bool settingsPick(")
        pick_draw = extract_function(settings, "static void settingsDrawPicker(")
        chrome = extract_function(settings, "static void drawSettingsChrome(")
        row = extract_function(settings, "static void drawSettingRow(")
        describe_focus = extract_function(settings, "static void settingsDescribeFocus(")
        page_draw = extract_function(settings, "uiDrawObj_t* settings_draw_page(")
        prepare = extract_function(layout_c, "int UISetLayout_PrepareText(")
        page_render = extract_function(framebuffer, "static void _DrawSettingsPage(")
        list_render = extract_function(framebuffer, "static void _DrawSettingsList(")
        page_update = extract_function(framebuffer, "void DrawUpdateSettingsPage(")
        draw_mode = extract_function(framebuffer, "void DrawVideoMode(GXRModeObj *videoMode)")
    except ValueError as error:
        errors.append(str(error))
        return errors

    # Input: one reusable per-port policy, digital precedence, controlled
    # repeat, 50/60 Hz timing, and no return to aggregate raw axes.
    need("UI_MENU_INPUT_AXIS_BOTH" in settings, "Settings does not enable both axes")
    need(re.search(r"\bUI_MENU_INPUT_REPEAT\b", settings) is not None,
         "Settings lost controlled analog repeat")
    mask = re.search(
        r"#define\s+SETTINGS_DIGITAL_INPUT_MASK\s+\((.*?)\)\s*#define",
        settings,
        re.DOTALL,
    )
    expected_mask = [
        "BUTTON_RIGHT", "BUTTON_LEFT", "BUTTON_UP", "BUTTON_DOWN",
        "BUTTON_B", "BUTTON_A", "BUTTON_Y", "BUTTON_R", "BUTTON_L", "BUTTON_X",
    ]
    need(mask is not None, "Settings digital input mask is not extractable")
    if mask is not None:
        normalized_mask = re.sub(r"\\\s*\n", "", mask.group(1))
        normalized_mask = re.sub(r"\s+", "", normalized_mask)
        need(normalized_mask == "|".join(expected_mask),
             "Settings digital input mask is not the exact control set")
    expected_directions = [
        ("UI_MENU_INPUT_LEFT", "BUTTON_LEFT"),
        ("UI_MENU_INPUT_RIGHT", "BUTTON_RIGHT"),
        ("UI_MENU_INPUT_UP", "BUTTON_UP"),
        ("UI_MENU_INPUT_DOWN", "BUTTON_DOWN"),
    ]
    actual_directions = re.findall(
        r"case\s+(UI_MENU_INPUT_(?:LEFT|RIGHT|UP|DOWN)):\s*"
        r"return\s+(BUTTON_(?:LEFT|RIGHT|UP|DOWN));",
        direction_map,
    )
    need(actual_directions == expected_directions,
         "analog directions do not map one-for-one onto legacy actions")
    need(direction_map.count("case UI_MENU_INPUT_NONE:") == 1 and
         direction_map.count("return 0u;") == 1,
         "neutral analog mapping is not fail-closed")
    need("padsStickX(" not in settings and "padsStickY(" not in settings,
         "Settings consumes raw aggregate stick axes")
    need("padsMenuInputPoll(menuInput" in wait_input,
         "Settings wait loop bypasses the shared mapper")
    need("settingsMenuInputElapsedMicroseconds(lastRetrace)" in wait_input,
         "Settings wait loop charges nominal time instead of elapsed retraces")
    need("buttons != 0u" in wait_input,
         "digital input does not inhibit analog in the wait loop")
    for token in (
        "VIDEO_GetRetraceCount()",
        "VIDEO_GetRetraceRate()",
        "elapsedRetraces = currentRetrace - *lastRetrace",
        "if(elapsedRetraces == 0u)",
        "UI_MENU_INPUT_MAX_ELAPSED_US",
    ):
        need(token in settings, f"Settings elapsed-retrace timing omits {token}")
    need("UIMenuInput_Init(&menuInput);" in show,
         "Settings entry does not initialize analog ownership")
    need("if(padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK) {\n"
         "\t\tsettingsInhibitThroughDigitalRelease(&menuInput," in show,
         "the press that opened Settings (X on Game Detail) also acts in it")
    need("SETTINGS_MENU_INPUT_POLICY, true" in drain,
         "digital release drain does not keep analog inhibited")
    need("settingsMenuInputElapsedMicroseconds(lastRetrace)" in drain,
         "release drain charges nominal time instead of elapsed retraces")
    need(drain.index("padsMenuInputPoll") < drain.index("if(buttons == 0u)"),
         "release edge is checked before its inhibited analog sample")
    need(show.count("settingsInhibitThroughDigitalRelease(&menuInput") >= 4,
         "entry/Save/Discard/release drains are incomplete")
    need(show.count("SETTINGS_MENU_INPUT_POLICY, true") >= 3,
         "tooltip/modal waits do not consume held analog input")
    need(show.count("padsMenuInputPoll") ==
         show.count("settingsMenuInputElapsedMicroseconds("),
         "a Settings modal poll does not use the elapsed-retrace clock")
    for token in (
        "SET_RT4K_OPTIM",
        "SET_FLATTEN_DIR",
        "SET_BBA_LOCALIP",
        "SET_RT4K_PORT",
        "SET_DEFAULT_AUDIO_STREAM",
        "SET_DEFAULT_EMULATE_ETHERNET",
    ):
        need(token in blocking, f"blocking child reset omits {token}")
    # Settings redesign, phase 1b: A on a long choice opens a list. It drains the A that
    # opened it and the press that closes it, blocks like any child prompt,
    # and changes the value only through Right's own code.
    need("else if(settingsPickerFor(ref->page, ref->option) != NULL) {" in show and
         "settingsPick(settingsPickerFor(ref->page, ref->option), config," in show,
         "A on a long choice does not open the list")
    need("if(activate && settingsPickerFor(page, option) != NULL) {" in blocking,
         "the list does not reset analog ownership when it closes")
    need(pick.index("settingsInhibitThroughDigitalRelease(menuInput, lastRetrace);") <
         pick.index("while(1) {") and
         pick.count("settingsInhibitThroughDigitalRelease(menuInput, lastRetrace);") >= 3,
         "the list reads the A that opened it, or leaks the press that closed it")
    need("UISetLayout_ComputeList(list->count, focus, &out->layout);" in pick_draw and
         "_SettingsFocusCard(l->focusRect.x, l->focusRect.y," in list_render and
         "UISettingsFocus_" not in list_render and "UIMotion_Spring" not in list_render,
         "the list borrows the page's animated focus, which then sits between rows")
    need("settings_toggle(pick->page, pick->option, 1, config);" in pick and
         "if(!row.enabled ||" in pick,
         "the list sets values outside Right's code, or opens on a disabled row")
    need("settingsInputMayBlock(ref->page, ref->option, btns)" in show,
         "blocking child loops do not reset analog ownership")
    need("if(inputMayBlock || (wasDigital && !settingsHoldToRepeat(" in show,
         "analog-opened child prompts can leak their dismissing A press")

    # Settings redesign, phase 1: the D-pad repeats like the stick, B leaves, A advances a
    # choice, L/R wrap, and a live video change must be confirmed or undone.
    for token in ("UI_MENU_INPUT_INITIAL_REPEAT_US", "UI_MENU_INPUT_REPEAT_US",
                  "(padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK) == held",
                  "(held & ~directions) != 0u",
                  "SETTINGS_MENU_INPUT_POLICY, true"):
        need(token in repeat, f"D-pad repeat omits {token}")
    need("digitalRepeat = false;" in show,
         "a blocking or analog action does not restart the D-pad repeat delay")
    need("settingsChanged(config) ?" in show and "btns |= BUTTON_A;" in show,
         "B no longer leaves through Save & Exit or Discard & Exit")
    need(changed.count("memcmp(") == 2 and "tempSettings" in changed and
         "tempConfig" in changed,
         "B's change test does not compare both snapshots")
    need("view = (tab + 1) % UI_SETLAYOUT_TAB_COUNT;" in show and
         "view = (tab + UI_SETLAYOUT_TAB_COUNT - 1) % UI_SETLAYOUT_TAB_COUNT;" in show,
         "L/R do not wrap around the tabs")
    need("view == inputView && option == inputOption" in show and
         "settingsRowIsAction(ref->page, ref->option)" in show,
         "A can change a row it did not start on, or an action row")
    # Settings redesign, phase 2: B backs out of a Setup section before it leaves, and A
    # on a Setup row opens its section.
    need("option = view - VIEW_DISPLAY;" in show and "view = VIEW_SETUP;" in show and
         "btns &= ~BUTTON_A;" in show,
         "B in a Setup section does not return to that section's Setup row")
    need("view = ref->option; option = 0;" in show,
         "A on a Setup row does not open its section")
    need("UISetLayout_PageDesc(view)->tab != UI_SETLAYOUT_NO_TAB" in show,
         "L/R can leave a game's own settings")
    # Settings redesign, phase 3: X puts an enabled game row back to its default, and a
    # screen reset asks first.
    need("(btns & BUTTON_X) && view == VIEW_GAME" in show and
         "if(row.enabled)" in show and "settingsUseDefault(config, ref->option);" in show,
         "X can reset a row outside a game's settings, or a disabled one")
    need("!settingsRowIsReset(ref->page, ref->option) ||" in show and
         "settingsConfirmReset()" in show,
         "a screen reset runs without asking")
    for token in ("SET_SYS_VIDEO", "SET_SWISS_VIDEOMODE", "SET_AVE_COMPAT",
                  "SET_FORCE_DTVSTATUS", "SET_RT4K_OPTIM"):
        need(token in live_rows, f"live video rows omit {token}")
    # A opens the video prompt, so it resets analog ownership; Left and Right
    # only step the value, so holding them scrolls through the values.
    need("(activate && settingsIsLiveVideoRow(page, option))" in blocking,
         "the video confirmation does not reset analog ownership, or holding "
         "Left/Right cannot scroll a video row's values")
    need("#define SETTINGS_VIDEO_KEEP_US 10000000u" in settings and
         "SETTINGS_VIDEO_KEEP_US" in keep_video and
         "released = held == 0u;" in keep_video and "keep = true;" in keep_video,
         "the video confirmation is not a released, timed A-to-keep prompt")
    # Browsing never switches: Left and Right step a video row with
    # DrawVideoMode held back (settings_toggle, upstream's, switches itself),
    # and DrawVideoMode honours it.
    need("DrawVideoModeDefer(true);\n\tsettings_toggle(page, option, direction, config);\n"
         "\tDrawVideoModeDefer(false);" in change_value and
         "DrawVideoMode(" not in change_value.replace("DrawVideoModeDefer(", ""),
         "Left/Right on a video row switches the mode")
    need("if(videoModeDeferred) {\n\t\treturn;\n\t}" in draw_mode and
         draw_mode.index("if(videoModeDeferred)") < draw_mode.index("setVideoMode("),
         "DrawVideoMode switches while Settings holds it back")
    # AVE Compatibility and RetroTINK-4K often keep the mode object and change
    # the signal in place: DrawVideoMode must re-apply an unchanged object
    # (updateVideoMode reconfigures the VI), or A would ask about a change
    # that isn't on screen and B would not bring the old signal back.
    need(ordered(draw_mode, "if(getVideoMode() != videoMode) {", "setVideoMode(videoMode);",
                 "else {", "updateVideoMode(videoMode);"),
         "DrawVideoMode drops a change that keeps the mode object (AVE, RetroTINK-4K)")
    for field in ("sramVideo", "uiVMode", "aveCompat", "forceDTVStatus", "rt4kOptim"):
        need(f"settingsVideo.{field} = swissSettings.{field};" in change_value,
             f"a video step does not record {field}")
        need(f"swissSettings.{field} = settingsVideo.{field};" in restore_video,
             f"a video value not kept does not restore {field}")
    # A only applies: one switch to the stepped value, the prompt, and on a
    # no the settings and the old mode back.
    need(apply_video.count("DrawVideoMode(") == 2 and
         "settings_toggle(" not in apply_video and "settingsChangeValue(" not in apply_video and
         ordered(apply_video,
                 "DrawVideoMode(getVideoModeFromSwissSetting(swissSettings.uiVMode));",
                 "settingsKeepVideoMode(&row)", "settingsVideoRestore();",
                 "DrawVideoMode(before);"),
         "A on a video row does not switch once, ask, and change back when refused")
    need("else if(settingsIsLiveVideoRow(ref->page, ref->option)) {\n"
         "\t\t\t\t\tsettingsApplyVideoMode(ref->page, ref->option, config);" in show,
         "A on a video row changes its value instead of applying the stepped one")
    # Cancel runs before actions: any button but Left, Right, A and Y, or any
    # button off a video row, puts a stepped value back before B, L/R, Save
    # or Discard can act, so a mode never on screen is never saved.
    cancel = ("if(settingsVideo.pending && (!onSetting ||\n"
              "\t\t\t!settingsIsLiveVideoRow(ref->page, ref->option) ||\n"
              "\t\t\t(btns & ~(BUTTON_LEFT | BUTTON_RIGHT | BUTTON_A | BUTTON_Y)) != 0u)) {\n"
              "\t\t\tsettingsVideoRestore();")
    need(ordered(show, "settingsWaitForInput(", cancel, "if(btns & BUTTON_Y)"),
         "a stepped video value outlives another button, so it can be saved unseen")
    need('hints[count++] = "A  Apply";' in describe_focus and
         "settingsVideoStepped()" in describe_focus,
         "the footer does not say A applies a stepped video value")
    need("settingsDescribeRow(page, option, config, &row);" in apply_video and
         '"Keep %s %s?' in keep_video and "row->label, row->value" in keep_video,
         "the video prompt does not name the new value")
    need("settings_toggle(ref->page, ref->option, 1, config)" not in show and
         "settings_toggle(ref->page, ref->option, -1, config)" not in show,
         "a value change bypasses the video confirmation")
    need("config_defaults_from(&tempConfig, &tempSettings);" in show,
         "saving from a game compares it with the changed defaults")
    # Settings redesign, phase 4: SRAM names a new Configuration Device only once the
    # settings are saved there, and a failed save keeps the old device, so
    # the reset hook's updateSRAM cannot point the console at an empty one.
    need("swissSettings.configDeviceId = tempSettings.configDeviceId;\n"
         "\t\t\t\tupdateSRAM(&swissSettings, true);\n"
         "\t\t\t\tswissSettings.configDeviceId = chosenConfigDevice;" in show,
         "SRAM takes a new Configuration Device before the settings reach it")
    need("if(config_update_global(true)) {\n\t\t\t\t\t// Saved on the chosen device: SRAM may name it now.\n"
         "\t\t\t\t\tupdateSRAM(&swissSettings, true);" in show,
         "a good save leaves SRAM on the old Configuration Device")
    saved = show[show.index("if(config_update_global(true)) {"):
                 show.index("Failed to save configuration!")]
    need("swissSettings.configDeviceId = tempSettings.configDeviceId;" in
         saved[saved.index("else {"):],
         "a failed save keeps the new Configuration Device")
    need(show.index("settingsInputMayBlock(ref->page, ref->option, btns)") <
         show.index("if(btns & BUTTON_Y)"),
         "blocking-child status is latched after Settings mutations begin")

    # Presentation (the cheat browser's language): every text family
    # consumes a measured owner, and the page's bands keep the cheat
    # browser's own metrics so title, tabs, rows and footer never overlap.
    for token, declaration in (
        ("titleRegion", "uiSetLayoutRect_t titleRegion;"),
        ("subtitleRegion", "uiSetLayoutRect_t subtitleRegion;"),
        ("badgeRegion", "uiSetLayoutRect_t badgeRegion;"),
        ("tabTrack", "uiSetLayoutRect_t tabTrack;"),
        ("titleMaxWidth", "int titleMaxWidth;"),
        ("subtitleMaxWidth", "int subtitleMaxWidth;"),
        ("badgeMaxWidth", "int badgeMaxWidth;"),
        ("sectionMaxWidth", "int sectionMaxWidth;"),
        ("descriptionMaxWidth", "int descriptionMaxWidth;"),
        ("hintMaxWidth", "int hintMaxWidth;"),
        ("rowTextY", "int rowTextY[UI_SETLAYOUT_VISIBLE_ROWS];"),
    ):
        need(declaration in layout_h and token in layout_c,
             f"layout does not publish {token}")
    for token in (
        "TITLE_Y 63",
        "SUBTITLE_Y 98",
        "TOP_DIVIDER_Y 115",
        "SECTION_Y 132",
        "ROWS_Y0 148",
        "ROW_PITCH 40",
        "ROW_H 34",
        "DESCRIPTION_Y 400",
        "BOTTOM_DIVIDER_Y 413",
        "FOOTER_Y 435",
    ):
        need(token in layout_c, f"native vertical ownership drifted: {token}")
    need("#define UI_SETLAYOUT_VISIBLE_ROWS 6" in layout_h,
         "the page no longer shows the cheat browser's six cards")
    need(chrome.count("prepareSettingText(") >= 3 and
         "prepareHintText(hints[i]," in describe_focus,
         "title/subtitle/tabs/footer hints are not all measured")
    # The page covers the whole screen, opaque, like the cheat browser: the
    # title bar's clock can't show through the tabs.
    for token in (
        "UI_SETLAYOUT_PAGE_X (-6)",
        "UI_SETLAYOUT_PAGE_Y (-6)",
        "UI_SETLAYOUT_PAGE_W 652",
        "UI_SETLAYOUT_PAGE_H 492",
        "UI_SETLAYOUT_PAGE_ALPHA 255",
    ):
        need(token in layout_h, f"the page no longer covers the screen: {token}")
    need("back.a = UI_SETLAYOUT_PAGE_ALPHA;" in page_render and
         page_render.index("_PagePanel(UI_SETLAYOUT_PAGE_X, UI_SETLAYOUT_PAGE_Y,") <
         page_render.index("drawStringMedium("),
         "the page is not drawn opaque beneath everything else")
    need(row.count("prepareSettingText(") == 2,
         "row label and value are not each measured")
    need("UISetLayout_Label(row->label, label, sizeof(label));" in row,
         "row labels keep the colon the row tables hold for prompts")
    need("if(UISetLayout_HelpSummary(help, row.value, summary," in describe_focus,
         "the focused row's one-line description is not drawn from its help")
    # Stability: one page event for the whole session, updated in place, so
    # no frame is ever drawn without a page. Only Save and Discard dispose it.
    loop = show[show.index("while(1) {"):]
    need("settingsPageEvent = NULL;" in show[:show.index("while(1) {")],
         "a new Settings session reuses the last session's page")
    need(show.count("DrawDispose(settingsPage);") == 2 and
         loop.index("DrawDispose(settingsPage);") >
         loop.index("if(option == UISetLayout_RowCount(view)) {"),
         "the page is disposed outside Save and Discard, so it flickers")
    # No bare frame between screens: the caller moves the scene to Settings
    # before this runs and back after it returns, so the page goes up before
    # the press that opened it is let go, and comes down only after the press
    # that closes it is, right before returning.
    need(ordered(show[:show.index("while(1) {")],
                 "settings_draw_page(view, option, config);",
                 "if(padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK) {"),
         "the page waits for the opening press's release, so the screen behind shows bare")
    for result in ("1", "0"):
        need("\t\t\t\tsettingsInhibitThroughDigitalRelease(&menuInput,\n"
             "\t\t\t\t\t&menuInputRetrace);\n"
             "\t\t\t\tDrawDispose(settingsPage);\n"
             f"\t\t\t\treturn {result};" in loop,
             f"the page leaves before the closing press's release (return {result}), "
             "so the screen behind shows bare")
    need("if(settingsPageEvent == NULL &&" in page_draw and
         "DrawPublish(settingsPageEvent);" in page_draw and
         "DrawUpdateSettingsPage(settingsPageEvent, &page, (const int[UI_COLOR_LAYERS]) {\n"
         "\t\tswissSettings.uiColor, swissSettings.uiBackdropColor, swissSettings.uiWaveColor});"
         in page_draw and
         "DrawContainer(" not in page_draw,
         "the page is rebuilt and republished instead of updated in place")
    locked = page_update[page_update.index("LWP_MutexLock(_videomutex);"):
                         page_update.index("LWP_MutexUnlock(_videomutex);")]
    need("->snapshot = *snapshot;" in locked and "menuColorPinned[i] = colors[i];" in locked,
         "the page's text and its colors are not replaced in one step")
    need("DrawFadingLabel" not in settings,
         "Help discoverability returned to a continuously fading label")
    need("UI_SETLAYOUT_ROW_TEXT_FLOOR 0.60f" in layout_h,
         "readable native-grid floor regressed")
    need("preferredScale < floorScale" in prepare,
         "measured-copy policy accepts an unreadable requested scale")
    need("out[0] = '\\0'" in prepare,
         "measured-copy failures do not clear presentation output")

    # Compile/sanitizer coverage and real-English extrema are mandatory.
    # One list builds every test plain and sanitized, and the runner runs it.
    for target in ("test_ui_settings_layout", "test_ui_menu_input"):
        need(target in makefile, f"Makefile omits {target}")
    for token in (
        "test_explicit_text_and_vertical_ownership",
        "test_measured_copies_cover_real_extremes",
        "CPU Temperature Calibration:",
        "Hide unknown file types:",
        "Emulate Broadband Adapter:",
        "test_help_card_holds_sixteen_lines",
        "test_help_summary_reads_the_current_value",
    ):
        need(token in layout_test, f"layout extrema coverage omits {token}")
    need("firstRepeatAt(20000u)" in menu_test,
         "menu input tests omit the 50 Hz trace")
    need("firstRepeatAt(16667u)" in menu_test,
         "menu input tests omit the 60 Hz trace")
    need("UI_MENU_INPUT_INITIAL_REPEAT_US" in menu_test and
         "UI_MENU_INPUT_REPEAT_US" in menu_test,
         "menu input tests do not bind repeat timing")
    need("show actions" in semantics and "normalize_show_actions" in semantics and
         "SHOW_ACTIONS_SHA256" in semantics,
         "Save/Discard/page/value semantic comparison is missing")

    try:
        tooltip_count, tooltip_lines, tooltip_width = tooltip_inventory(settings)
    except ValueError as error:
        errors.append(str(error))
    else:
        need(tooltip_count >= 30, "tooltip inventory unexpectedly incomplete")
        help_lines = re.search(r"#define UI_SETLAYOUT_HELP_LINES (\d+)", layout_h)
        need(help_lines is not None and tooltip_lines <= int(help_lines.group(1)),
             "tooltip exceeds the help card")
        need(tooltip_width <= 68, "tooltip line exceeds the tested width owner")
    return errors


FILES = {
    "settings": read(GUI / "settings.c"),
    "layout_c": read(GUI / "ui_settings_layout.c"),
    "layout_h": read(GUI / "ui_settings_layout.h"),
    "makefile": read(ROOT / "buildtools/ui/tests/Makefile"),
    "layout_test": read(ROOT / "buildtools/ui/tests/test_ui_settings_layout.c"),
    "menu_test": read(ROOT / "buildtools/ui/tests/test_ui_menu_input.c"),
    "semantics": read(ROOT / "buildtools/ui/tests/audit_settings_semantics.sh"),
    "framebuffer": read(GUI / "FrameBufferMagic.c"),
}

live_errors = validate(FILES)
if live_errors:
    raise SystemExit("Settings hardware audit failed:\n- " + "\n- ".join(live_errors))

print("Settings hardware audit OK")
