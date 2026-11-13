#!/usr/bin/env python3
"""Settings views (Settings redesign, phase 2) cover every setting exactly once.

The views in settings.c (Quick, Game Defaults, Setup and its six sections,
and one game's own settings) are tables of (page, option) rows over the
legacy PAGE_/SET_ value layer. A setting missing from every table would be
unreachable on the console; one listed twice would be confusing. This test
parses the tables and the enums and checks the whole mapping.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"
SETTINGS_C = (GUI / "settings.c").read_text()
SETTINGS_H = (GUI / "settings.h").read_text()
LAYOUT_H = (GUI / "ui_settings_layout.h").read_text()
SWISS_C = (ROOT / "cube/swiss/source/swiss.c").read_text()
MAIN_C = (ROOT / "cube/swiss/source/main.c").read_text()

PAGE_ENUMS = {
    "PAGE_GLOBAL": "SETTINGS_GLOBAL",
    "PAGE_INTERFACE": "SETTINGS_INTERFACE",
    "PAGE_NETWORK": "SETTINGS_NETWORK",
    "PAGE_GAME_GLOBAL": "SETTINGS_GAME_GLOBAL",
    "PAGE_GAME_DEFAULTS": "SETTINGS_GAME_DEFAULTS",
    "PAGE_GAME": "SETTINGS_GAME",
}
# Its rows now live in Quick and three Setup sections, so a single reset
# across them has no home.
# Game Defaults doesn't show the vertical offset: every game starts at
# global.ini's value (+0 unless set there), and each game's own settings
# still show it.
DROPPED = {("PAGE_GAME_GLOBAL", "SET_GLOBAL_DEFAULTS"),
           ("PAGE_GAME_DEFAULTS", "SET_DEFAULT_VERT_OFFSET")}
QUICK = [
    ("PAGE_INTERFACE", "SET_MENU_MUSIC"),
    ("PAGE_INTERFACE", "SET_MENU_SFX"),
    ("PAGE_INTERFACE", "SET_UI_ANIMS"),
    ("PAGE_GLOBAL", "SET_DISABLE_RUMBLE"),
    ("PAGE_GAME_GLOBAL", "SET_IGR"),
    ("PAGE_GAME_GLOBAL", "SET_BS2BOOT"),
    ("PAGE_GAME_GLOBAL", "SET_EMULATE_MEMCARD"),
    ("PAGE_GAME_GLOBAL", "SET_ALL_CHEATS"),
    ("PAGE_INTERFACE", "SET_AUTOBOOT"),
]
SECTIONS = ["display", "console", "storage", "network", "library", "developer"]


HEADERS = "\n".join(path.read_text() for path in (ROOT / "cube/swiss").rglob("*.h"))


def constant(name: str) -> int:
    """A numeric define or enum value, including aliases and derived counts."""
    define = re.search(r"#define " + name + r"\s+(\d+)", HEADERS)
    if define:
        return int(define.group(1))
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", HEADERS, flags=re.S)
    for body in re.findall(r"enum\s*\w*\s*\{(.*?)\}", text, re.S):
        members = [m.strip() for m in body.split(",") if m.strip()]
        if name not in [m.split("=")[0].strip() for m in members]:
            continue
        value = -1
        for member in members:
            key, equals, expression = member.partition("=")
            if equals:
                expression = expression.strip()
                try:
                    value = int(expression, 0)
                except ValueError:
                    alias = re.fullmatch(r"([A-Z][A-Z0-9_]*)(?:\s*\+\s*(\d+))?", expression)
                    if not alias:
                        raise ValueError(f"unsupported enum expression: {expression}")
                    value = constant(alias[1]) + int(alias[2] or 0)
            else:
                value += 1
            if key.strip() == name:
                return value
    raise KeyError(name)


def toggle_arms() -> dict:
    body = SETTINGS_C[SETTINGS_C.index("void settings_toggle(int page"):]
    body = body[:body.index("\n}\n")]
    return dict(re.findall(r"case (SET_\w+):\n(.*?)\n\t+break;", body, re.S))


def enum_rows(enum_name: str) -> list:
    body = re.search(r"enum " + enum_name + r" \{(.*?)\};", SETTINGS_H, re.S).group(1)
    members = [m.split("=")[0].strip() for m in body.split(",") if m.strip()]
    return [m for m in members if not m.startswith("SET_PAGE_")]


def table(name: str) -> list:
    body = re.search(r"static const settingsRowRef_t " + name + r"Rows\[\] = \{(.*?)\n\};",
                     SETTINGS_C, re.S).group(1)
    return re.findall(r"\{(\w+), (\w+)\}", body)


class SettingsViewsTest(unittest.TestCase):
    def test_public_layout_count_excludes_the_internal_flat_renderer(self):
        self.assertEqual(constant("UI_GAMEFLOW_LIBRARY_LAYOUT_COUNT"), 5)
        self.assertEqual(constant("UI_GAMEFLOW_LAYOUT_COUNT"), 6)

    def test_every_setting_has_exactly_one_home(self):
        placed = []
        for name in ["quick", "gameDefaults", *SECTIONS]:
            placed += table(name)
        legacy = [(page, row) for page, enum in PAGE_ENUMS.items() if page != "PAGE_GAME"
                  for row in enum_rows(enum)]
        self.assertEqual(len(placed), len(set(placed)), "a setting is listed twice")
        self.assertEqual(set(legacy) - set(placed), DROPPED)
        self.assertEqual(set(placed) - set(legacy), set())

    def test_game_views_share_one_order(self):
        game = table("game")
        defaults = table("gameDefaults")
        self.assertEqual(sorted(game), sorted(("PAGE_GAME", row) for row in enum_rows("SETTINGS_GAME")))
        self.assertEqual(sorted(defaults),
                         sorted(("PAGE_GAME_DEFAULTS", row) for row in enum_rows("SETTINGS_GAME_DEFAULTS")
                                if ("PAGE_GAME_DEFAULTS", row) not in DROPPED))
        # Everyday rows lead, the reset comes last, and both screens agree.
        self.assertEqual([row for _, row in game[:7]],
                         ["SET_FORCE_VIDEOMODE", "SET_WIDESCREEN", "SET_GAME_LANG", "SET_POLL_RATE",
                          "SET_INVERT_CAMERA", "SET_SWAP_CAMERA", "SET_TRIGGER_LEVEL"])
        self.assertEqual(game[-1], ("PAGE_GAME", "SET_DEFAULTS"))
        self.assertEqual(defaults[-1], ("PAGE_GAME_DEFAULTS", "SET_DEFAULT_DEFAULTS"))
        shared = [row.replace("SET_DEFAULT_", "SET_") for _, row in defaults
                  if row not in ("SET_DEFAULT_NTSC_VIDEOMODE", "SET_DEFAULT_PAL_VIDEOMODE", "SET_DEFAULT_DEFAULTS")]
        per_game = [row for _, row in game
                    if row not in ("SET_FORCE_VIDEOMODE", "SET_GAME_LANG", "SET_DEFAULTS",
                                   "SET_VERT_OFFSET")]
        self.assertEqual(shared, per_game)

    def test_game_fields_are_the_game_file_keys(self):
        writer = (ROOT / "cube/swiss/source/config/config.c").read_text()
        writer = writer[writer.index("int config_update_game("):]
        writer = writer[:writer.index("fclose(fp);")]
        stored = set(re.findall(r"if\(entry->(\w+) != defaults->\1\)", writer))
        fields = re.search(r"gameRowFields\[\] = \{(.*?)\n\};", SETTINGS_C, re.S).group(1)
        mapped = dict(re.findall(r"\[(SET_\w+)\] = GAME_FIELD\((\w+)\)", fields))
        self.assertEqual(set(mapped.values()), stored)
        self.assertEqual(set(mapped), set(enum_rows("SETTINGS_GAME")) - {"SET_DEFAULTS"})

    def test_quick_is_the_planned_nine(self):
        self.assertEqual(table("quick"), QUICK)

    def test_setup_links_each_section_in_order(self):
        links = table("setup")
        self.assertEqual(links, [("SETTINGS_ROW_LINK", "VIEW_" + s.upper()) for s in SECTIONS])
        summaries = re.findall(r'"([^"]+)"', re.search(
            r"static const char \*setupSummaries\[\] = \{(.*?)\};", SETTINGS_C, re.S).group(1))
        limit = int(re.search(r"#define UI_SETLAYOUT_VALUE_TEXT_MAX (\d+)u", LAYOUT_H).group(1))
        self.assertEqual(len(summaries), len(SECTIONS))
        for summary in summaries:
            self.assertLessEqual(len(summary), limit, summary)

    def test_row_counts_match_the_layout(self):
        for name, macro in [("quick", "QUICK"), ("gameDefaults", "GAME_DEFAULTS"),
                            ("setup", "SETUP"), ("game", "GAME"),
                            *[(s, s.upper()) for s in SECTIONS]]:
            count = int(re.search(r"#define UI_SETLAYOUT_ROWS_" + macro + r" (\d+)", LAYOUT_H).group(1))
            self.assertEqual(len(table(name)), count, name)

    def test_every_listed_setting_is_described(self):
        describe = SETTINGS_C[SETTINGS_C.index("static void settingsDescribeRow("):]
        describe = describe[:describe.index("\nuiDrawObj_t* settings_draw_page(")]
        branches = re.split(r"\n\t(?:else )?if\(page == (PAGE_\w+)\) \{", describe)
        cases = {branches[i]: set(re.findall(r"case (SET_\w+):", branches[i + 1]))
                 for i in range(1, len(branches), 2)}
        for page, enum in PAGE_ENUMS.items():
            self.assertEqual(set(enum_rows(enum)), cases[page], page)

    def test_disable_settings_read_the_positive_way_round(self):
        describe = SETTINGS_C[SETTINGS_C.index("static void settingsDescribeRow("):]
        describe = describe[:describe.index("\nuiDrawObj_t* settings_draw_page(")]
        # A stored "Disable X" shows as "X: On/Off", flipped, never "Disable X: No".
        self.assertNotRegex(describe, r'rowYesNo\(row, "Disable ')
        for label, field, count in [("Controller Rumble:", "disableRumble", 1),
                                    ("Controller Recalibration:", "disableRecalibration", 1),
                                    ("Alpha Dithering:", "disableDithering", 2),
                                    ("Hypervisor:", "disableHypervisor", 2)]:
            shown = re.findall(r'rowOnOff\(row, "' + label + r'", (!?)[\w>.-]+?' + field + r',', describe)
            self.assertEqual(shown, ["!"] * count, label)

    def test_the_picker_lists_every_short_choice(self):
        # A opens a list on each choice row that cycles 4 to 16 values; live
        # video rows keep their keep-or-revert prompt and numbers keep Right.
        arms = toggle_arms()
        live = {"SET_SYS_VIDEO", "SET_SWISS_VIDEOMODE", "SET_AVE_COMPAT",
                "SET_FORCE_DTVSTATUS", "SET_RT4K_OPTIM"}
        cycles = {}
        for name, arm in arms.items():
            # % COUNT; or, for Backdrop and Wave Color, % (COUNT + 1);
            modulo = re.search(r"% \(?(\w+)(?: \+ (\d+)\))?;", arm)
            if "+= direction" in arm and modulo:
                size, extra = modulo.group(1), int(modulo.group(2) or 0)
                cycles[name] = (int(size) if size.isdigit() else constant(size)) + extra
        expected = {name for name, size in cycles.items() if 4 <= size <= 16 and name not in live}
        table_text = re.search(r"settingsPickerRows\[\] = \{(.*?)\n\};", SETTINGS_C, re.S).group(1)
        picker = re.findall(r"PICK_(SETTING|GAME)\((?:(PAGE_\w+), )?(SET_\w+), (\w+)\)", table_text)
        self.assertEqual({option for _, _, option, _ in picker}, expected)
        placed = set()
        for name in ["quick", "gameDefaults", "game", *SECTIONS]:
            placed |= set(table(name))
        for kind, page, option, field in picker:
            page = page or "PAGE_GAME"
            arm = arms[option]
            target = ("gameConfig->" if kind == "GAME" else "swissSettings.") + field
            # The listed field is the one Right steps, and nothing else moves.
            self.assertIn(target + " += direction;", arm, option)
            self.assertEqual(set(re.findall(r"([\w.>-]+) \+?= ", arm)), {target}, option)
            # Stepping only reads state, so listing the values is safe.
            calls = set(re.findall(r"\b(\w+)\(", arm)) - {"if", "while", "in_range",
                "getDTVStatus", "is_rt4k_alive", "SYS_GetPhysicalMemSize"}
            self.assertEqual(calls, set(), option)
            self.assertIn(option, enum_rows(PAGE_ENUMS[page]), option)
            self.assertIn((page, option), placed, option)

    def test_every_row_has_help(self):
        def keys(table):
            body = re.search(r"static char \*" + table + r"\[[^\]]*\] = \{(.*?)\n\};", SETTINGS_C, re.S).group(1)
            return set(re.findall(r"\[(SET_\w+)\] =", body))
        game = keys("tooltips_game")
        game_rows = enum_rows("SETTINGS_GAME")
        defaults_rows = enum_rows("SETTINGS_GAME_DEFAULTS")
        help_for = {
            "PAGE_GLOBAL": keys("tooltips_global"),
            "PAGE_INTERFACE": keys("tooltips_interface"),
            "PAGE_NETWORK": keys("tooltips_network"),
            "PAGE_GAME_GLOBAL": keys("tooltips_game_global"),
            "PAGE_GAME": game,
            # Rows 0-1 have their own table; the rest share the game table by index.
            "PAGE_GAME_DEFAULTS": keys("tooltips_game_defaults_video") |
                {row for i, row in enumerate(defaults_rows) if i > 1 and game_rows[i] in game},
        }
        for name in ["quick", "gameDefaults", "game", *SECTIONS]:
            for page, option in table(name):
                self.assertIn(option, help_for[page], f"{name}: {option} has no help")

    def test_console_value_lines_fit_the_help_line(self):
        # Under the rows, the help line shows the help's line for the
        # row's value, with the lines continuing it (they start lower
        # case). About 100 characters fit across the page: Cube's Classic
        # line once ran to 121 and lost its end.
        tooltips = {}
        for table_name in ("tooltips_global", "tooltips_interface"):
            body = re.search(r"static char \*" + table_name + r"\[[^\]]*\] = \{(.*?)\n\};",
                             SETTINGS_C, re.S).group(1)
            for option, text in re.findall(r'\[(SET_\w+)\] = ((?:"(?:[^"\\]|\\.)*"\s*)+)', body):
                tooltips[option] = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text)).replace("\\n", "\n")
        for _, option in table("console"):
            value_line = None
            for line in tooltips[option].split("\n")[1:]:
                if " - " in line and line[0].isupper():
                    value_line = line.split(" - ", 1)[1]
                elif value_line is not None and line and line[0].islower():
                    value_line += " " + line
                else:
                    value_line = None
                    continue
                self.assertLessEqual(len(value_line), 100, f"{option}: {value_line}")

    def test_storage_says_whether_the_settings_file_loaded(self):
        chrome = SETTINGS_C[SETTINGS_C.index("static void drawSettingsChrome("):]
        chrome = chrome[:chrome.index("\n}\n")]
        self.assertIn("if(page_num == VIEW_STORAGE) {", chrome)
        for state in ("NO_DEVICE", "SAVED", "MISSING"):
            self.assertIn("UI_SETLAYOUT_SETTINGS_FILE_" + state, chrome)
        self.assertIn("devices[DEVICE_CONFIG] == NULL", chrome)
        self.assertIn("config_global_file_loaded()", chrome)
        self.assertIn("prepareSettingText(subtitleText,", chrome)
        config = (ROOT / "cube/swiss/source/config/config.c").read_text()
        init = config[config.index("int config_init("):]
        init = init[:init.index("\n}\n")]
        self.assertIn("globalFileLoaded = configData != NULL;", init)
        self.assertLess(init.index("SWISS_SETTINGS_FILENAME);"), init.index("globalFileLoaded ="))
        save = config[config.index("int config_update_global("):]
        save = save[:save.index("\n}\n")]
        self.assertIn("if(res) {\n\t\tglobalFileLoaded = true;", save)

    def test_a_text_row_offers_edit_only_where_a_acts(self):
        # A dimmed network row (no adapter yet) still opens the text editor;
        # a dimmed Save Folder has no Configuration Device to list, so A does
        # nothing there and its footer must not offer "A  Edit".
        focus = SETTINGS_C[SETTINGS_C.index("static void settingsDescribeFocus("):]
        focus = focus[:focus.index("\n}\n")]
        self.assertRegex(focus, r'if\(row\.enabled \|\| ref->page == PAGE_NETWORK\) \{\s*'
                                r'hints\[count\+\+\] = "A  Edit";')
        # Only these can dim a text row; a new one needs its hint decided.
        self.assertEqual(set(re.findall(r'rowText(?:Number)?\(row, "[^"]*", [^;]*, ([^;]*?)\);',
                                        SETTINGS_C)),
                         {"true", "netEnable", "devices[DEVICE_CONFIG] != NULL",
                          "!swissSettings.libraryFolders"})
        arms = toggle_arms()
        network = re.findall(r'case (SET_\w+): rowText(?:Number)?\(row, "[^"]*", [^;]*, netEnable\);',
                             SETTINGS_C)
        self.assertGreater(len(network), 10)
        for option in network:
            if option == "SET_TIME_ZONE":
                self.assertEqual(arms[option].strip(), "settingsEditTimeZone();")
                editor = SETTINGS_C[SETTINGS_C.index("static void settingsEditTimeZone("):]
                self.assertIn("DrawGetTextEntry(", editor[:editor.index("\n}\n")])
            else:
                self.assertTrue(arms[option].strip().startswith("DrawGetTextEntry("), option)
        self.assertIn("devices[DEVICE_CONFIG] != NULL &&", arms["SET_SAVE_FOLDER"])
        # Flatten directory dims while Library Folders sets it, and then
        # neither A nor Left and Right reach its editor.
        locked = SETTINGS_C[SETTINGS_C.index("static bool settingsRowLocked("):]
        locked = locked[:locked.index("\n}\n")]
        self.assertIn("option == SET_FLATTEN_DIR &&\n\t\tswissSettings.libraryFolders", locked)
        change = SETTINGS_C[SETTINGS_C.index("static void settingsChangeValue("):]
        self.assertIn("if(settingsRowLocked(page, option)) {\n\t\treturn;", change[:200])
        self.assertRegex(SETTINGS_C, r"else if\(settingsRowLocked\(ref->page, ref->option\)\) \{"
                                     r"[^}]*\}\s*else if\(settingsRowIsAction\(ref->page, ref->option\)\)")

    def test_entry_points_land_where_they_should(self):
        self.assertIn("show_settings_view(VIEW_QUICK, 0, NULL)", SWISS_C)
        self.assertNotIn("show_settings(PAGE_GLOBAL, 0, NULL)", SWISS_C)
        # Game Detail opens a game's settings at their first row.
        self.assertEqual(SWISS_C.count("show_settings_view(VIEW_GAME, 0, config)"), 2)
        self.assertNotIn("show_settings(PAGE_GAME", SWISS_C)
        # The legacy entry names a setting, never a row: a game's rows are
        # ordered everyday-first, so their index isn't their SET_ value.
        wrapper = SETTINGS_C[SETTINGS_C.index("int show_settings(int page"):
                             SETTINGS_C.index("int show_settings_view(int view")]
        self.assertIn("ref->page == page && ref->option == option", wrapper)
        self.assertNotIn(", option, config)", wrapper)
        # First boot and a cable change open the rows they name.
        self.assertIn("show_settings(PAGE_GLOBAL, SET_CONFIG_DEV, NULL)", MAIN_C)
        self.assertIn("show_settings(PAGE_GLOBAL, SET_AVE_COMPAT, NULL)", MAIN_C)
        self.assertIn(("PAGE_GLOBAL", "SET_CONFIG_DEV"), table("storage"))
        self.assertIn(("PAGE_GLOBAL", "SET_AVE_COMPAT"), table("display"))


if __name__ == "__main__":
    unittest.main()
