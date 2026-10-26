#!/usr/bin/env python3
"""Run Swiss's own settings-file code on the host.

docs/SETTINGS.md tells people how to write swiss/settings/global.ini and a
game's settings file by hand before they boot. Rather than trust the prose,
this test compiles config_parse_global, config_parse_game, config_defaults and
both writers straight out of config.c, with main.c's start-up defaults and
settings.c's value tables. It then checks the reference and its examples
against what that code does.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from test_cheats_gx_stream import extract_function


ROOT = Path(__file__).resolve().parents[3]
SWISS = ROOT / "cube/swiss"
CONFIG_C = (SWISS / "source/config/config.c").read_text()
CONFIG_H = (SWISS / "source/config/config.h").read_text()
SETTINGS_C = (SWISS / "source/gui/settings.c").read_text()
SETTINGS_H = (SWISS / "source/gui/settings.h").read_text()
SWISS_H = (SWISS / "include/swiss.h").read_text()
UI_HOME_H = (SWISS / "source/gui/ui_home.h").read_text()
UI_GAMEFLOW_H = (SWISS / "source/gui/ui_gameflow.h").read_text()
UI_GAMEFLOW_LIBRARY_H = (SWISS / "source/gui/ui_gameflow_library.h").read_text()
MAIN_H = (SWISS / "include/main.h").read_text()
MAIN_C = (SWISS / "source/main.c").read_text()
SWISS_C = (SWISS / "source/swiss.c").read_text()
DOC = ROOT / "docs/SETTINGS.md"
EXAMPLES = ROOT / "docs/examples"

GLOBAL_WRITER = extract_function(CONFIG_C, "int config_update_global(")
GAME_WRITER = extract_function(CONFIG_C, "int config_update_game(")
GLOBAL_PARSER = extract_function(CONFIG_C, "void config_parse_global(")
GAME_PARSER = extract_function(CONFIG_C, "void config_parse_game(")


def between(source: str, start: str, end: str) -> str:
    first = source.index(start)
    return source[first:source.index(end, first)]


def typedef(source: str, name: str) -> str:
    return re.search(r"typedef struct \{.*?\} " + name + ";", source, re.S).group(0)


def dump(struct: str, prefix: str) -> str:
    """printf every scalar and one-dimensional string field of a struct."""
    lines = []
    for line in struct.splitlines():
        scalar = re.match(r"\s*(?:int|short|s8|u8|u16|bool)\s+(\w+);", line)
        text = re.match(r"\s*char\s+(\w+)\[[^\]]+\];", line)
        if scalar:
            lines.append(f'\tfprintf(out, "{scalar[1]}=%d\\n", (int){prefix}{scalar[1]});')
        elif text:
            lines.append(f'\tfprintf(out, "{text[1]}=%s\\n", {prefix}{text[1]});')
    return "\n".join(lines)


STRING_ARRAYS = re.findall(r"^char \*\w+Str\[\] = \{.*?\};$", SETTINGS_C, re.M)
VALUES = {
    re.match(r"char \*(\w+)", array)[1]: re.findall(r'"([^"]*)"', array)
    for array in STRING_ARRAYS
}
SWISS_SETTINGS = typedef(SWISS_H, "SwissSettings")
CONFIG_ENTRY = typedef(CONFIG_H, "ConfigEntry")
DEFAULTS = between(MAIN_C, "// Sane defaults", "config_init_environ();")
assert 'strcpy(swissSettings.flattenDir, "*/games");' in MAIN_C
DETAIL_SAVES_TOGGLE = re.search(
    r"case SET_DETAIL_SAVES:\n(.*?)\n\t+break;", SETTINGS_C, re.S).group(1)


HARNESS = "\n".join([
    r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
#define PATHNAME_MAX 1024
#define RECENT_MAX 8
#define SRAM_LANGUAGE_MAX 8
#define in_range(x, a, b) (((x) >= (a)) && ((x) <= (b)))
/* libogc2 gamecube/include/ogc/system.h */
#define SYS_BOOT_DEVELOPMENT 0x00
#define SYS_BOOT_PRODUCTION 0x80
#define SYS_SOUND_MONO 0
#define SYS_SOUND_STEREO 1

static size_t harness_strlcpy(char *dst, const char *src, size_t size)
{
	size_t length = strlen(src);
	if(size) {
		size_t copy = length < size - 1 ? length : size - 1;
		memcpy(dst, src, copy);
		dst[copy] = '\0';
	}
	return length;
}
#define strlcpy harness_strlcpy
/* Upstream counts its tables with C2y's countof (<stdcountof.h>), which a
 * host compiler may not have yet. */
#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
/* A retail GameCube: 24 MiB, no digital-AV cable detected. */
static u32 SYS_GetPhysicalMemSize(void) { return 24u << 20; }
static bool getRawDTVStatus(void) { return false; }
/* SRAM-backed values start at zero; each console's own SRAM differs. */
#define refreshSRAM(settings) ((void)(settings))
""",
    # swiss.h reaches the Home face icons through FrameBufferMagic.h.
    re.search(r"^#define UI_HOME_ICON_CHOICES \d+$", UI_HOME_H, re.M).group(0),
    # And the faces the cube's sides name.
    re.search(r"^typedef enum \{[^}]*\} uiHomeFace_t;", UI_HOME_H, re.M).group(0),
    # And the Library layouts, through the same header.
    re.search(r"^typedef enum \{[^}]*\} uiGameflowLayout_t;", UI_GAMEFLOW_H, re.M).group(0),
    "\n".join(re.findall(r"^enum \w+\s*\{.*?\};", SWISS_H, re.S | re.M)),
    re.search(r"^enum setupStream\s*\{.*?\};", MAIN_H, re.S | re.M).group(0),
    SWISS_SETTINGS,
    "SwissSettings swissSettings;",
    (SWISS / "source/gui/ui_folder.h").read_text(),
    (SWISS / "source/gui/ui_folder.c").read_text().replace('#include "ui_folder.h"', ""),
    "static uiFolderColors_t folderColors;",
    CONFIG_ENTRY,
    "\n".join(STRING_ARRAYS),
    re.search(r"^const int simulatedMemSizeInt\[\] = \{.*?\};", SETTINGS_C, re.S | re.M).group(0),
    "\n".join(re.findall(r"^static char \w+Entries\[\]\[[45]\] = \{.*?\};$", CONFIG_C, re.M)),
    extract_function(CONFIG_C, "void config_defaults_from("),
    extract_function(CONFIG_C, "void config_defaults("),
    "#include <stddef.h>",
    re.search(r"^enum SETTINGS_GAME \{.*?\};", SETTINGS_H, re.S | re.M).group(0),
    between(SETTINGS_C, "#define GAME_FIELD", "#undef GAME_FIELD") + "#undef GAME_FIELD",
    extract_function(SETTINGS_C, "static void settingsGameDefaults("),
    extract_function(SETTINGS_C, "static void settingsUseDefault("),
    extract_function(SETTINGS_C, "static void settingsResetGame("),
    extract_function(SETTINGS_C, "static bool settingsGameRowCustom("),
    extract_function(SETTINGS_C, "int settings_game_custom_count("),
    # The Library's custom mark, with the device scan stubbed out: the
    # harness hands each file in the way the scan would.
    "void config_parse_game(char *configData, ConfigEntry *entry);\n"
    "typedef unsigned long long u64;\n"
    "static u64 harness_ms;\n"
    "static u64 SYS_GetSystemTime(void) { return harness_ms; }\n"
    "#define ticks_to_millisecs(t) (t)\n"
    "static int harness_scan_result, harness_scans;\n"
    "static int config_each_game_file(void (*visit)(const char *, char *, void *), void *context)\n"
    "{\n\t(void)visit;\n\t(void)context;\n\tharness_scans++;\n\treturn harness_scan_result;\n}\n"
    "static int harness_parses;\n"
    "static void harness_parse_game(char *data, ConfigEntry *entry)\n"
    "{\n\tharness_parses++;\n\tconfig_parse_game(data, entry);\n}\n"
    "#define config_parse_game harness_parse_game",
    between(SETTINGS_C, "typedef struct {\n\tchar gameId[4];",
            "/* X in a game's settings: this row follows Game Defaults again. */"),
    "#undef config_parse_game",
    # Library Folders sets FlattenDir to its own pattern while it is on.
    re.search(r"^#define UI_GAMEFLOW_LIBRARY_FOLDERS_FLATTEN .*$",
              UI_GAMEFLOW_LIBRARY_H, re.M).group(0),
    extract_function(CONFIG_C, "void config_set_library_folders("),
    GLOBAL_PARSER,
    GAME_PARSER,
    # The legacy swiss.ini migration, counting the files it would write.
    "static int harness_migrated, harness_legacy_deleted, harness_no_memory;\n"
    "static void *harness_calloc(size_t count, size_t size)\n"
    "{\n\treturn harness_no_memory ? NULL : calloc(count, size);\n}\n"
    "static int config_update_game(ConfigEntry *entry, ConfigEntry *defaults, bool check)\n"
    "{\n\t(void)entry;\n\t(void)defaults;\n\t(void)check;\n\treturn ++harness_migrated;\n}\n"
    "static int config_update_global(bool check) { (void)check; return 1; }\n"
    "static int config_update_recent(bool check) { (void)check; return 1; }\n"
    "static void config_file_delete(char *name) { harness_legacy_deleted = !strcmp(name, \"swiss.ini\"); }\n"
    "static void harness_progress(char *text, int a, int b) { (void)text; (void)a; (void)b; }\n"
    "#define SWISS_SETTINGS_FILENAME_LEGACY \"swiss.ini\"\n"
    "#define print_debug(...) ((void)0)\n"
    + re.search(r"^#define LEGACY_ENTRIES .*$", CONFIG_C, re.M).group(0) + "\n"
    + extract_function(CONFIG_C, "void config_parse_legacy(").replace("calloc(", "harness_calloc("),
    between(CONFIG_C, "/* Keys a global.ini may still carry", "int config_update_global("),
    extract_function(CONFIG_C, "static char *config_merge_autoload("),
    "static void write_global(FILE *fp)\n{\n"
    + between(GLOBAL_WRITER, "// Write out Swiss settings", "fclose(fp);") + "}",
    "static void write_game(FILE *fp, ConfigEntry *entry, ConfigEntry *defaults)\n{\n"
    + between(GAME_WRITER, 'fprintf(fp, "# Game specific', "fclose(fp);") + "}",
    "static void set_defaults(void)\n{\n"
    "\tmemset(&folderColors, 0, sizeof(folderColors));\n"
    "\tmemset(&swissSettings, 0, sizeof(SwissSettings));\n"
    '\tstrcpy(swissSettings.flattenDir, "*/games");\n'
    + DEFAULTS + "}",
    "static void dump_settings(FILE *out)\n{\n" + dump(SWISS_SETTINGS, "swissSettings.") + "\n}",
    "static void dump_entry(FILE *out, const ConfigEntry *entry)\n{\n" + dump(CONFIG_ENTRY, "entry->") + "\n}",
    "static void cycle_detail_saves(int direction)\n{\n" + DETAIL_SAVES_TOGGLE + "\n}",
    r"""
static char *read_stream(FILE *in)
{
	size_t length = 0, capacity = 4096;
	char *data = malloc(capacity);
	size_t got;
	while(data && (got = fread(data + length, 1, capacity - length - 1, in)) > 0) {
		length += got;
		if(capacity - length < 2) {
			capacity *= 2;
			data = realloc(data, capacity);
		}
	}
	if(!data) exit(70);
	data[length] = '\0';
	return data;
}

static void parse_global_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	if(!file) exit(66);
	char *global = read_stream(file);
	fclose(file);
	config_parse_global(global, true);
	free(global);
}

static char *read_path(const char *path)
{
	FILE *file = fopen(path, "rb");
	if(!file) exit(66);
	char *data = read_stream(file);
	fclose(file);
	return data;
}

/* global|global-fields < ini
 * game|game-fields ID4 REGION [global.ini] < game ini
 * game-save ID4 REGION opened.ini changed.ini < game ini: what Settings writes
 *   for a game when defaults changed while it was open from that game.
 * game-reset ID4 REGION < game ini: the file after Reset to defaults.
 * autoload-merge existing.ini < path: what an autoload toggle saves over it,
 *   or NULL when the caller must save everything instead.
 * merge-global existing.ini < changes: what a save writes over a global.ini
 *   Swiss booted with, after the changes were made on the console.
 * merge-game ID4 REGION existing.ini < changes: the same for a game's file.
 * legacy [no-memory] < swiss.ini: the games a legacy file migrates, and
 *   whether it then deletes swiss.ini.
 * custom-mark ID4 REGION [global.ini] < game ini: whether the Library marks
 *   that game's cover as having settings of its own.
 * scan-retry: how many scans the Library makes around a failed one.
 * args|args-fields booted.ini < arguments: the settings after a loader starts
 *   Swiss with these arguments on a card holding booted.ini. */
int main(int argc, char **argv)
{
	if(argc < 2) return 64;
	char *input = read_stream(stdin);
	set_defaults();
	if(argc >= 3 && !strcmp(argv[1], "merge-global")) {
		char *existing = read_path(argv[2]);
		char *booted = strdup(existing);
		char *generated = NULL;
		size_t length = 0;
		FILE *fp;
		config_parse_global(booted, true);
		config_parse_global(input, true);
		fp = open_memstream(&generated, &length);
		write_global(fp);
		fclose(fp);
		char *merged = config_merge_file(existing[0] ? existing : NULL, generated, globalOldKeys);
		fputs(merged != NULL ? merged : generated, stdout);
		free(merged);
		free(generated);
		free(booted);
		free(existing);
	}
	else if(argc >= 5 && !strcmp(argv[1], "merge-game")) {
		char *existing = read_path(argv[4]);
		char *booted = strdup(existing);
		char *generated = NULL;
		size_t length = 0;
		ConfigEntry entry, defaults;
		FILE *fp;
		memset(&entry, 0, sizeof(entry));
		strncpy(entry.game_id, argv[2], 4);
		entry.region = argv[3][0];
		config_defaults(&entry);
		defaults = entry;
		config_parse_game(booted, &entry);
		config_parse_game(input, &entry);
		fp = open_memstream(&generated, &length);
		write_game(fp, &entry, &defaults);
		fclose(fp);
		char *merged = config_merge_file(existing[0] ? existing : NULL, generated, gameFileKeys);
		fputs(merged != NULL ? merged : generated, stdout);
		free(merged);
		free(generated);
		free(booted);
		free(existing);
	}
	else if(argc >= 6 && !strcmp(argv[1], "game-save")) {
		static SwissSettings opened;
		ConfigEntry entry, defaults;
		parse_global_file(argv[4]);
		memcpy(&opened, &swissSettings, sizeof(SwissSettings));
		memset(&entry, 0, sizeof(entry));
		strncpy(entry.game_id, argv[2], 4);
		entry.region = argv[3][0];
		config_defaults(&entry);
		config_parse_game(input, &entry);
		parse_global_file(argv[5]);
		defaults = entry;
		config_defaults_from(&defaults, &opened);
		write_game(stdout, &entry, &defaults);
	}
	else if(argc >= 3 && !strcmp(argv[1], "autoload-merge")) {
		char *existing = read_path(argv[2]);
		char *merged;
		input[strcspn(input, "\r\n")] = '\0';
		strncpy(swissSettings.autoload, input, sizeof(swissSettings.autoload) - 1);
		/* config_file_read gives NULL for a missing or empty file. */
		merged = config_merge_autoload(existing[0] ? existing : NULL);
		fputs(merged != NULL ? merged : "NULL\n", stdout);
		free(merged);
		free(existing);
	}
	else if(argc >= 4 && !strcmp(argv[1], "game-reset")) {
		ConfigEntry entry, defaults;
		memset(&entry, 0, sizeof(entry));
		strncpy(entry.game_id, argv[2], 4);
		entry.region = argv[3][0];
		config_defaults(&entry);
		defaults = entry;
		config_parse_game(input, &entry);
		settingsResetGame(&entry);
		write_game(stdout, &entry, &defaults);
	}
	else if(argc >= 4 && !strcmp(argv[1], "custom-mark")) {
		if(argc >= 5) {
			parse_global_file(argv[4]);
		}
		settingsKeepGameFile(argv[2], input, NULL);
		puts(settings_game_has_custom(argv[2], argv[3][0]) ? "custom" : "none");
		puts(settings_game_has_custom(argv[2], argv[3][0]) ? "custom" : "none");
		printf("%d\n", harness_parses);
		settings_game_files_forget();
		puts(settings_game_has_custom(argv[2], argv[3][0]) ? "custom" : "none");
	}
	else if(!strcmp(argv[1], "legacy")) {
		harness_no_memory = argc >= 3 && !strcmp(argv[2], "no-memory");
		config_parse_legacy(input, harness_progress);
		printf("%d %d\n", harness_migrated, harness_legacy_deleted);
	}
	else if(!strcmp(argv[1], "scan-retry")) {
		harness_scan_result = -1;
		settings_game_files_load();
		settings_game_files_load();
		printf("%d ", harness_scans);
		harness_ms += 30001u;
		harness_scan_result = 0;
		settings_game_files_load();
		settings_game_files_load();
		printf("%d ", harness_scans);
		settings_game_files_forget();
		settings_game_files_load();
		printf("%d\n", harness_scans);
	}
	else if(argc >= 3 && (!strcmp(argv[1], "args") || !strcmp(argv[1], "args-fields"))) {
		parse_global_file(argv[2]);
		config_parse_global(input, false);
		if(!strcmp(argv[1], "args")) write_global(stdout);
		else dump_settings(stdout);
	}
	else if(argc >= 3 && !strcmp(argv[1], "detail-saves-cycle")) {
		config_parse_global(input, true);
		cycle_detail_saves(atoi(argv[2]));
		write_global(stdout);
	}
	else if(!strcmp(argv[1], "global") || !strcmp(argv[1], "global-fields")) {
		config_parse_global(input, true);
		if(!strcmp(argv[1], "global")) write_global(stdout);
		else dump_settings(stdout);
	}
	else if(argc >= 4 && (!strcmp(argv[1], "game") || !strcmp(argv[1], "game-fields"))) {
		if(argc >= 5) {
			parse_global_file(argv[4]);
		}
		ConfigEntry entry, defaults;
		memset(&entry, 0, sizeof(entry));
		strncpy(entry.game_id, argv[2], 4);
		entry.region = argv[3][0];
		config_defaults(&entry);
		defaults = entry;
		config_parse_game(input, &entry);
		if(!strcmp(argv[1], "game")) write_game(stdout, &entry, &defaults);
		else dump_entry(stdout, &entry);
	}
	else return 64;
	free(input);
	return 0;
}
""",
])


def pairs(text: str) -> dict:
    """Key=Value lines the way the parsers split them."""
    result = {}
    for line in re.split(r"[\r\n]+", text):
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def writer_keys(writer: str, subject: str) -> dict:
    """Key -> how the writer renders it: an array name, 'yes-no', or 'raw'."""
    keys = {}
    for key, rest in re.findall(r'fprintf\(fp, "([^"=]+)=%[^"]*\\r\\n", ([^;]*)\);', writer):
        array = re.match(r"(\w+Str)\[" + re.escape(subject), rest)
        if array:
            keys[key] = array[1]
        elif re.search(r'\? "Yes":"No"', rest):
            keys[key] = "yes-no"
        elif re.search(r'\? "([^"]+)":"([^"]+)"', rest):
            keys[key] = re.search(r'\? "([^"]+)":"([^"]+)"', rest).groups()
        else:
            keys[key] = "raw"
    for key in re.findall(r'fputs\("([^"=]+)=", fp\)', writer):
        keys[key] = "raw"
    return keys


GLOBAL_KEYS = writer_keys(GLOBAL_WRITER, "swissSettings.")
GAME_KEYS = writer_keys(GAME_WRITER, "entry->")
READ_KEYS = set(re.findall(r'strcmp\("([^"]+)", name\)', GLOBAL_PARSER + GAME_PARSER))
# Written by Swiss but never read back (the ID comes from the file name).
WRITE_ONLY = {"ID"}
# Earlier names the parser still accepts.
OLD_NAMES = {"Enable Debug", "USB Gecko debug output", "Stop DVD Motor on startup",
             "Hide Apps Face"}
# Listed values global.ini does not accept, all explained in SETTINGS.md: the
# NTSC default only takes NTSC modes, "Default" is a game's language, and
# 32 MiB and up exceed a retail GameCube's memory.
REJECTED = {
    *(("Force NTSC Video Mode", mode) for mode in VALUES["gameVModeStr"][8:]),
    ("System Language", "Default"),
    ("Simulated MRAM Size", "32 MiB"),
    ("Simulated MRAM Size", "48 MiB"),
    ("Simulated MRAM Size", "64 MiB"),
}


class SettingsFileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        work = Path(cls.tmp.name)
        source = work / "settings_file.c"
        source.write_text(HARNESS)
        cls.binary = work / "settings_file"
        flags = ["-std=gnu11", "-g", "-O1", "-Wall", "-Wno-sign-compare",
                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                 "-fno-omit-frame-pointer"]
        if os.uname().sysname == "Linux":
            flags += ["-fno-pie", "-no-pie"]
        result = subprocess.run(
            shlex.split(os.environ.get("CC", "cc")) + flags + ["-o", str(cls.binary), str(source)],
            capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stderr[-4000:])

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_harness(self, *args, stdin=""):
        result = subprocess.run([str(self.binary), *args], input=stdin.encode(),
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode()[-2000:])
        return result.stdout.decode()

    def global_file(self, text=""):
        return pairs(self.run_harness("global", stdin=text))

    def test_writer_and_parser_agree_on_keys(self):
        self.assertEqual(set(GLOBAL_KEYS) - READ_KEYS, set())
        self.assertEqual(set(GAME_KEYS) - READ_KEYS - WRITE_ONLY, set())
        self.assertEqual(READ_KEYS - set(GLOBAL_KEYS) - set(GAME_KEYS) - OLD_NAMES, set())

    def test_folder_colors_survive_the_real_global_parser_and_writer(self):
        encoded = "sda:/games/Spaces%20#%3B%7E%25%3D%0A%09%0D~3"
        second = "sdb:/games/Spaces%20#%3B%7E%25%3D%0A%09%0D~2"
        written = self.global_file(f"Memory Card Folder Colors={encoded};{second}\nMenu Color=Gold\n")
        self.assertEqual(written["Memory Card Folder Colors"], f"{encoded};{second}")
        self.assertEqual(written["Menu Color"], "Gold")
        again = self.global_file("\n".join(f"{key}={value}" for key, value in written.items()))
        self.assertEqual(written, again)
        self.assertEqual(self.global_file("Memory Card Folder Colors=bad%00path~1;wrong~9;good~8\n")
                         ["Memory Card Folder Colors"], "good~8")
        self.assertEqual(self.global_file("Memory Card Folder Colors=\n")["Memory Card Folder Colors"], "")

    def test_defaults_survive_a_round_trip(self):
        first = self.run_harness("global")
        self.assertEqual(first, self.run_harness("global", stdin=first))
        self.assertEqual(self.run_harness("global-fields"),
                         self.run_harness("global-fields", stdin=first))
        written = pairs(first)
        self.assertEqual(set(written), set(GLOBAL_KEYS))
        self.assertEqual(written["SD/IDE Speed"], "32MHz")
        self.assertEqual(written["FlattenDir"], "*/games")
        self.assertEqual(written["AVECompat"], "GCVideo")
        # A game's details show two or more save copies unless this is Yes.
        self.assertEqual(written["Hide Saves on Details"], "No")
        self.assertEqual(written["Always Show Saves on Details"], "No")

    def test_detail_saves_cycles_and_persists_all_three_modes(self):
        keys = ("Hide Saves on Details", "Always Show Saves on Details")
        modes = (("No", "No"), ("No", "Yes"), ("Yes", "No"))
        for index, mode in enumerate(modes):
            initial = "\n".join(f"{key}={value}" for key, value in zip(keys, mode))
            for direction in (-1, 1):
                with self.subTest(mode=mode, direction=direction):
                    written = pairs(self.run_harness("detail-saves-cycle", str(direction), stdin=initial))
                    self.assertEqual(tuple(written[key] for key in keys), modes[(index + direction) % 3])
                    saved = "\n".join(f"{key}={value}" for key, value in written.items())
                    self.assertEqual(self.global_file(saved), written)
        # Old settings files retain their existing On/Off behavior.
        for hide in ("No", "Yes"):
            written = self.global_file(f"Hide Saves on Details={hide}\n")
            self.assertEqual(tuple(written[key] for key in keys), (hide, "No"))

    def test_every_listed_value_round_trips(self):
        known_exceptions = set()
        for key, kind in GLOBAL_KEYS.items():
            if kind not in VALUES:
                continue
            for value in dict.fromkeys(VALUES[kind]):
                if self.global_file(f"{key}={value}\r\n")[key] != value:
                    known_exceptions.add((key, value))
        self.assertEqual(known_exceptions, REJECTED)

    def test_only_an_exact_yes_turns_a_setting_on(self):
        for key, kind in GLOBAL_KEYS.items():
            if kind != "yes-no" or key == "Last DTV Status":
                continue
            self.assertEqual(self.global_file(f"{key}=Yes")[key], "Yes", key)
            for off in ("No", "yes", "YES", "1", "true", "Yes "):
                self.assertEqual(self.global_file(f"{key}={off}")[key], "No", (key, off))
        for key, kind in GLOBAL_KEYS.items():
            if isinstance(kind, tuple):
                on, off = kind
                self.assertEqual(self.global_file(f"{key}={on}")[key], on, key)
                self.assertEqual(self.global_file(f"{key}={on.lower()}x")[key], off, key)
        # The on-screen speed is not the file's: 27MHz selects the slow one.
        self.assertEqual(self.global_file("SD/IDE Speed=27MHz")["SD/IDE Speed"], "16MHz")

    def test_the_cube_is_infinite_unless_the_file_says_classic(self):
        # Setup > Console > Cube: a file without the key, or with any other
        # value, keeps the cube Indigo always had.
        self.assertEqual(self.global_file()["Cube"], "Infinite")
        self.assertIn("\ncubeStyle=0\n", self.run_harness("global-fields"))
        self.assertEqual(self.global_file("Cube=Classic\r\n")["Cube"], "Classic")
        self.assertIn("\ncubeStyle=1\n", self.run_harness("global-fields", stdin="Cube=Classic\r\n"))
        for other in ("Infinite", "classic", "Ring", ""):
            self.assertEqual(self.global_file(f"Cube=Classic\r\nCube={other}\r\n")["Cube"],
                             "Infinite", other)

    def test_the_sides_default_to_the_cube_of_before(self):
        # Setup > Console > Up, Left, Right and Down Face. Without them the
        # cube is Source up, Settings left, System right and Apps down; an
        # older Hide Apps Face=Yes empties Apps' side unless the file names
        # the sides, and is never written again.
        sides = ("Up Face", "Left Face", "Right Face", "Down Face")
        self.assertEqual([self.global_file()[key] for key in sides],
                         ["Source", "Settings", "System", "Apps"])
        moved = self.global_file("Down Face=Settings\r\nLeft Face=Apps\r\n")
        self.assertEqual([moved[key] for key in sides], ["Source", "Apps", "System", "Settings"])
        self.assertEqual(self.global_file("Up Face=None")["Up Face"], "None")
        self.assertEqual(self.global_file("Up Face=Library")["Up Face"], "Source")
        # File Browser on any side, written back as read.
        for key in sides:
            self.assertEqual(self.global_file(f"{key}=File Browser\r\n")[key], "File Browser", key)
        old = self.global_file("Hide Apps Face=Yes\r\n")
        self.assertEqual(old["Down Face"], "None")
        self.assertNotIn("Hide Apps Face", old)
        self.assertEqual(self.global_file("Hide Apps Face=No\r\n")["Down Face"], "Apps")
        named = self.global_file("Hide Apps Face=Yes\r\nDown Face=Apps\r\n")
        self.assertEqual(named["Down Face"], "Apps")
        # Each side's choices start at its default and go round the faces,
        # None among them (UIHome_SideFace): zeroed settings are that cube.
        faces = ["None", "Source", "Settings", "System", "Apps", "Memory Cards", "Emulators",
                 "File Browser", "Web"]
        for side, default in (("up", 1), ("left", 2), ("right", 3), ("down", 4)):
            self.assertEqual(VALUES[f"{side}FaceStr"],
                             [faces[(default + i) % len(faces)] for i in range(len(faces))], side)

    def test_a_face_icon_only_takes_that_faces_own(self):
        # Each face has its own four icons; a name from another face's list
        # (a v1.19.0 file could hold one) leaves the face on its default.
        self.assertEqual(self.global_file("Source Icon=SD Card")["Source Icon"], "SD Card")
        self.assertEqual(self.global_file("Library Icon=Gear")["Library Icon"], "Controller")
        self.assertEqual(self.global_file("System Icon=None")["System Icon"], "Clock")
        faces = {"Library": "libraryIconStr", "Source": "sourceIconStr",
                 "Settings": "settingsIconStr", "System": "systemIconStr"}
        seen = [name for array in faces.values() for name in VALUES[array]]
        self.assertEqual(len(seen), len(set(seen)), "two faces offer the same icon")
        for face, array in faces.items():
            self.assertEqual(len(VALUES[array]), 4, face)

    def test_library_folders_keeps_the_flatten_dir_it_replaces(self):
        # While Library Folders is on, Swiss flattens by its own pattern, but
        # global.ini keeps the FlattenDir it replaced, ready for when it goes off.
        text = "Library Folders=Yes\r\nFlattenDir=*/isos\r\n"
        written = self.global_file(text)
        self.assertEqual(written["Library Folders"], "Yes")
        self.assertEqual(written["FlattenDir"], "*/isos")
        fields = self.run_harness("global-fields", stdin=text)
        self.assertIn("\nflattenDir=*/games/*/*\n", fields)
        self.assertIn("\nlibraryFoldersFlattenDir=*/isos\n", fields)
        # The order of the keys doesn't matter.
        self.assertEqual(self.global_file("FlattenDir=*/isos\r\nLibrary Folders=Yes\r\n"),
                         written)
        # Off, FlattenDir is the file's own.
        off = self.run_harness("global-fields", stdin="Library Folders=No\r\nFlattenDir=*/isos\r\n")
        self.assertIn("\nflattenDir=*/isos\n", off)

    def test_library_folders_never_keeps_its_own_pattern(self):
        # Off is one list of every game, so a FlattenDir that is empty or already
        # Library Folders' own pattern comes back as the default.
        for flatten in ("*/games/*/*", ""):
            text = f"FlattenDir={flatten}\r\nLibrary Folders=Yes\r\n"
            self.assertIn("\nlibraryFoldersFlattenDir=*/games\n",
                          self.run_harness("global-fields", stdin=text), flatten)
            self.assertEqual(self.global_file(text)["FlattenDir"], "*/games", flatten)

    def boot_with_arguments(self, booted, arguments, fields=False):
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write(booted)
        path = Path(handle.name)
        try:
            return self.run_harness("args-fields" if fields else "args", str(path), stdin=arguments)
        finally:
            path.unlink()

    def test_boot_arguments_change_only_what_they_name(self):
        # A loader can start Indigo with settings as arguments. Library Folders,
        # its FlattenDir and the temperature dial stay as global.ini has them
        # unless an argument names them.
        booted = ("Library Folders=Yes\r\nFlattenDir=*/isos\r\n"
                  "Clock=Left\r\nTemperature=Right\r\n")
        fields = self.boot_with_arguments(booted, "IGRType=Reboot\nClock=Right\n", fields=True)
        self.assertIn("\nlibraryFolders=1\n", fields)
        self.assertIn("\nflattenDir=*/games/*/*\n", fields)
        self.assertIn("\nlibraryFoldersFlattenDir=*/isos\n", fields)
        written = pairs(self.boot_with_arguments(booted, "IGRType=Reboot\nClock=Right\n"))
        self.assertEqual((written["Library Folders"], written["FlattenDir"]), ("Yes", "*/isos"))
        self.assertEqual((written["Clock"], written["Temperature"]), ("Right", "Right"))
        self.assertEqual(written["IGRType"], "Reboot")
        # An argument that names Library Folders still turns it off, and the
        # FlattenDir it replaced comes back.
        off = self.boot_with_arguments(booted, "Library Folders=No\n", fields=True)
        self.assertIn("\nlibraryFolders=0\n", off)
        self.assertIn("\nflattenDir=*/isos\n", off)

    def test_apps_video_mode_is_independent_of_the_menu(self):
        for mode in VALUES["uiVModeStr"]:
            written = self.global_file(f"Swiss Video Mode=480p\nApps Video Mode={mode}\n")
            self.assertEqual(written["Apps Video Mode"], mode)
            self.assertEqual(written["Swiss Video Mode"], "480p")
        self.assertEqual(self.global_file("Apps Video Mode=Unknown")["Apps Video Mode"], "Auto")

    def test_an_unknown_value_keeps_the_previous_one(self):
        defaults = self.global_file()
        for key, kind in GLOBAL_KEYS.items():
            if kind in VALUES:
                self.assertEqual(self.global_file(f"{key}=Nonsense")[key], defaults[key], key)
        self.assertEqual(self.global_file("Swiss Video Mode=480P")["Swiss Video Mode"], "Auto")
        self.assertEqual(self.global_file("Swiss Video Mode = 480p")["Swiss Video Mode"], "Auto")
        self.assertEqual(self.global_file("Unknown Key=Yes"), defaults)

    def test_line_endings_and_comments(self):
        for text in ("IGRType=Reboot\nAutoCheats=Yes\n",
                     "IGRType=Reboot\r\nAutoCheats=Yes\r\n",
                     "# comment\r\n#AutoCheats=No\r\nIGRType=Reboot\r\nAutoCheats=Yes"):
            written = self.global_file(text)
            self.assertEqual((written["IGRType"], written["AutoCheats"]), ("Reboot", "Yes"))

    def test_old_names_are_still_read(self):
        self.assertEqual(self.global_file("Stop DVD Motor on startup=Yes")["Stop DVD Drive motor"], "Yes")
        self.assertEqual(self.global_file("USB Gecko debug output=Slot B")["Enable USB Gecko"], "Slot B")
        self.assertEqual(self.global_file("Enable Debug=Yes")["Enable USB Gecko"], "Slot B")

    def test_last_dtv_status_comes_from_the_cable(self):
        self.assertEqual(self.global_file("Last DTV Status=Yes")["Last DTV Status"], "No")

    def test_a_game_file_holds_only_what_differs(self):
        empty = pairs(self.run_harness("game", "GALE", "E"))
        self.assertEqual(set(empty), {"ID", "Name", "Comment", "Status"})
        custom = pairs(self.run_harness(
            "game", "GALE", "E", stdin="Force Video Mode=480p\nForce Polling Rate=1000Hz\n"))
        self.assertEqual(set(custom) - set(empty), {"Force Video Mode", "Force Polling Rate"})
        for key, kind in GAME_KEYS.items():
            if kind in VALUES:
                for value in dict.fromkeys(VALUES[kind]):
                    written = pairs(self.run_harness("game", "GALE", "E", stdin=f"{key}={value}"))
                    self.assertEqual(written.get(key, value), value, (key, value))

    def test_vertical_offset_default_reaches_every_game(self):
        # As upstream since r2092: no -3 for GCVideo or GCDigital.
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write("Force Vertical Offset=+5\r\n")
        try:
            fields = pairs(self.run_harness("game-fields", "GALE", "E", handle.name))
            self.assertEqual(fields["forceVOffset"], "5")
            with open(handle.name, "a") as more:
                more.write("AVECompat=AVE N-DOL\r\n")
            fields = pairs(self.run_harness("game-fields", "GALE", "E", handle.name))
            self.assertEqual(fields["forceVOffset"], "5")
        finally:
            os.unlink(handle.name)

    def test_the_library_marks_only_games_that_differ_from_game_defaults(self):
        def mark(game_ini, game_id="GALE", region="E", global_ini=None):
            args = ["custom-mark", game_id, region] + ([global_ini] if global_ini else [])
            first, again, parses, after_a_save = self.run_harness(*args, stdin=game_ini).split()
            # The next step asks again: the same answer, without parsing again.
            self.assertEqual((again, parses), (first, "1"))
            # A save forgets the files: nothing is marked until they're read again.
            self.assertEqual(after_a_save, "none")
            return first

        self.assertEqual(mark("Force Widescreen=3D\r\n"), "custom")
        # What Reset to defaults leaves behind: Comment and Status only.
        self.assertEqual(mark("Comment=Plays fine\r\nStatus=Working\r\n"), "none")
        # A value that equals the default isn't custom.
        self.assertEqual(mark("Force Widescreen=No\r\n"), "none")
        # The file is found by the game's first four characters.
        self.assertEqual(mark("Force Widescreen=3D\r\n", game_id="GALP", region="P"), "custom")
        # Region picks the video mode default, as on Game Detail.
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write("Force PAL Video Mode=576i\r\n")
        try:
            self.assertEqual(mark("Force Video Mode=576i\r\n", "GALP", "P", handle.name), "none")
            self.assertEqual(mark("Force Video Mode=576i\r\n", "GALE", "E", handle.name), "custom")
        finally:
            os.unlink(handle.name)

    def test_a_legacy_swiss_ini_migrates_at_most_2047_games(self):
        # Under AddressSanitizer: 2,100 games stay inside the 2,048 entries
        # (the last is Game Defaults), and swiss.ini goes once they're out.
        games = "".join(f"ID=G{i:03X}\r\nName=Game {i}\r\nForce Video Mode=480p\r\n"
                        for i in range(2100))
        self.assertEqual(self.run_harness("legacy", stdin="Force Video Mode=480i\r\n" + games).split(),
                         ["2047", "1"])
        self.assertEqual(self.run_harness("legacy", stdin=games[:games.index("ID=G00A")]).split(),
                         ["10", "1"])
        # No memory for the entries: nothing is written, swiss.ini stays.
        self.assertEqual(self.run_harness("legacy", "no-memory", stdin=games).split(), ["0", "0"])

    def test_a_failed_scan_is_retried_but_not_on_every_step(self):
        # Fails, then waits (no second scan), retries after 30 s and keeps
        # the result, then reads again after a save.
        self.assertEqual(self.run_harness("scan-retry").split(), ["1", "2", "3"])

    def test_game_defaults_follow_global_ini_and_region(self):
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write("Force NTSC Video Mode=480p\r\nForce PAL Video Mode=576i\r\n"
                         "Force Widescreen=3D\r\n")
        try:
            ntsc = pairs(self.run_harness("game-fields", "GALE", "E", handle.name))
            pal = pairs(self.run_harness("game-fields", "GALP", "P", handle.name))
            self.assertEqual(VALUES["gameVModeStr"][int(ntsc["gameVMode"])], "480p")
            self.assertEqual(VALUES["gameVModeStr"][int(pal["gameVMode"])], "576i")
            self.assertEqual(VALUES["forceWidescreenStr"][int(ntsc["forceWidescreen"])], "3D")
            self.assertEqual(ntsc["gameLanguage"], "8")
            # Animal Crossing Deluxe widens itself, so it starts with Force
            # Widescreen off; its own file still turns it on, and keeps it.
            deluxe = pairs(self.run_harness("game-fields", "GADE", "E", handle.name))
            self.assertEqual(VALUES["forceWidescreenStr"][int(deluxe["forceWidescreen"])], "No")
            own = pairs(self.run_harness("game", "GADE", "E", handle.name,
                                         stdin="Force Widescreen=3D\r\n"))
            self.assertEqual(own.get("Force Widescreen"), "3D")
        finally:
            os.unlink(handle.name)

    def test_changing_a_default_from_a_game_leaves_the_game_following_it(self):
        work = Path(self.tmp.name)
        opened = work / "opened.ini"
        changed = work / "changed.ini"
        opened.write_text("Force Widescreen=No\r\n")
        changed.write_text("Force Widescreen=3D\r\n")
        untouched = pairs(self.run_harness("game-save", "GALE", "E", str(opened), str(changed)))
        self.assertNotIn("Force Widescreen", untouched)
        custom = pairs(self.run_harness("game-save", "GALE", "E", str(opened), str(changed),
                                        stdin="Force Polling Rate=1000Hz\r\n"))
        self.assertEqual(custom.get("Force Polling Rate"), "1000Hz")
        self.assertNotIn("Force Widescreen", custom)
        # Settings compares against the snapshot it opened with.
        settings = (SWISS / "source/gui/settings.c").read_text()
        self.assertIn("config_defaults_from(&tempConfig, &tempSettings);", settings)
        self.assertNotIn("config_defaults(&tempConfig);", settings)

    def test_reset_keeps_a_games_comment_and_status(self):
        raw = {"Force Vertical Offset": "+5", "Digital Trigger Level": "100", "RetroTINK-4K Profile": "3"}
        custom = "Comment=Needs the 480p fix\r\nStatus=Works\r\n"
        for key, kind in GAME_KEYS.items():
            if key in ("ID", "Name", "Comment", "Status"):
                continue
            value = (VALUES[kind][-1] if kind in VALUES else "Yes" if kind == "yes-no"
                     else kind[0] if isinstance(kind, tuple) else raw[key])
            custom += f"{key}={value}\r\n"
        self.assertGreater(len(pairs(self.run_harness("game", "GALE", "E", stdin=custom))), 20)
        reset = pairs(self.run_harness("game-reset", "GALE", "E", stdin=custom))
        self.assertEqual(reset, {"ID": "GALE", "Name": "", "Comment": "Needs the 480p fix",
                                 "Status": "Works"})

    def test_toggling_autoload_saves_only_its_own_key(self):
        path = Path(self.tmp.name) / "autoload.ini"
        # AutoBoot=Yes stays although B at launch may have turned it off in
        # memory; old names and unknown keys stay too.
        existing = ("# My console\r\n\r\n#!!Swiss Settings Start!!\r\nAutoBoot=Yes\r\n"
                    "Autoload=sd:/games/old.iso\r\nEnable Debug=Yes\r\nMy Note=keep\r\n"
                    "#!!Swiss Settings End!!\r\n\r\n")
        path.write_text(existing)
        self.assertEqual(self.run_harness("autoload-merge", str(path), stdin="dvd:/*.gcm\n"),
                         existing.replace("sd:/games/old.iso", "dvd:/*.gcm"))
        self.assertEqual(self.run_harness("autoload-merge", str(path)),
                         existing.replace("sd:/games/old.iso", ""))
        without = existing.replace("Autoload=sd:/games/old.iso\r\n", "")
        path.write_text(without)
        self.assertEqual(self.run_harness("autoload-merge", str(path), stdin="sd:/a.iso\n"),
                         without.replace("#!!Swiss Settings End!!",
                                         "Autoload=sd:/a.iso\r\n#!!Swiss Settings End!!"))
        # No file (or none that reads whole) means a full save, as before.
        path.write_text("")
        self.assertEqual(self.run_harness("autoload-merge", str(path), stdin="sd:/a.iso\n"), "NULL\n")
        self.assertIn("!= (s32)configFile->size", extract_function(CONFIG_C, "char* config_file_read("))
        self.assertEqual(SWISS_C.count("config_update_autoload(true);"),
                         SWISS_C.count("Saving autoload\\205"))
        self.assertEqual(SWISS_C.count("config_update_autoload(true);"), 3)
        self.assertNotIn("config_update_global(", SWISS_C)
        # Z in the Jewel Case saves only Spin Discs, the same way.
        self.assertEqual(SWISS_C.count("config_update_spin_discs(true);"), 1)

    def saved(self, existing: str, changes: str = "", game: bool = False) -> str:
        path = Path(self.tmp.name) / ("existing-game.ini" if game else "existing.ini")
        path.write_text(existing)
        if game:
            return self.run_harness("merge-game", "GALE", "E", str(path), stdin=changes)
        return self.run_harness("merge-global", str(path), stdin=changes)

    def test_a_save_keeps_what_you_wrote(self):
        example = (EXAMPLES / "global.ini").read_text()
        existing = example + "Future Setting=1\nStop DVD Motor on startup=Yes\n"
        merged = self.saved(existing, "IGRType=Apploader\n")
        lines = [line for line in re.split(r"\r\n", merged)]
        comments = [line for line in example.splitlines() if line.startswith("#")]
        self.assertEqual([line for line in lines if line.startswith("#")][:len(comments)], comments)
        self.assertIn("Future Setting=1", lines)
        self.assertNotIn("Stop DVD Motor on startup=Yes", lines)
        # The changed value stays where the file had it.
        ordered = [line.split("=")[0] for line in lines if "=" in line and not line.startswith("#")]
        self.assertEqual(ordered.index("IGRType"),
                         [l.split("=")[0] for l in example.splitlines() if "=" in l and not l.startswith("#")].index("IGRType"))
        self.assertEqual(pairs(merged)["IGRType"], "Apploader")
        # Every key Swiss writes is there once, and the file reads back as
        # what Swiss had in memory (the old name's value lives on in the new).
        keys = [line.split("=")[0] for line in lines if "=" in line and not line.startswith("#")]
        self.assertEqual(sorted(k for k in keys if k != "Future Setting"), sorted(GLOBAL_KEYS))
        self.assertEqual(pairs(merged)["Stop DVD Drive motor"], "Yes")
        self.assertEqual(self.run_harness("global-fields", stdin=merged),
                         self.run_harness("global-fields", stdin=existing + "IGRType=Apploader\n"))

    def test_saving_twice_changes_nothing_more(self):
        once = self.saved((EXAMPLES / "global.ini").read_text() + "Future Setting=1\n")
        self.assertEqual(self.saved(once), once)

    def test_a_new_file_says_comments_are_kept(self):
        fresh = self.saved("")
        self.assertIn("# Indigo keeps comments and unknown keys when it saves.", fresh)
        self.assertNotIn("will be lost", fresh)
        upgraded = self.saved("# Swiss Configuration File!\r\n# Anything written in here will be lost!\r\n"
                              "#!!Swiss Settings Start!!\r\nAutoCheats=Yes\r\n#!!Swiss Settings End!!\r\n")
        self.assertNotIn("will be lost", upgraded)
        # Keys the old file lacked go before its End marker.
        self.assertTrue(upgraded.rstrip().endswith("#!!Swiss Settings End!!"))

    def test_every_old_warning_line_is_replaced_in_bounds(self):
        # Each replacement is longer than the line it replaces; ASan checks the buffer.
        merged = self.saved("# Anything written in here will be lost!\n" * 150)
        self.assertEqual(merged.count("# Indigo keeps comments and unknown keys when it saves."), 150)

    def test_a_long_commented_file_keeps_its_notes(self):
        # Three notes above every key: several times the lines Swiss writes.
        example = self.saved("")
        long = "".join(f"# note {i} about {line.split('=')[0]}\r\n" * 3 + line + "\r\n"
                       for i, line in enumerate(re.split(r"\r\n", example)) if "=" in line)
        self.assertGreater(long.count("\n"), 300)
        merged = self.saved(long, "IGRType=Apploader\n")
        self.assertEqual(merged, long.replace("IGRType=Disabled", "IGRType=Apploader"))

    def test_a_game_file_drops_a_setting_back_at_its_default(self):
        example = (EXAMPLES / "game/GALE.ini").read_text()
        merged = self.saved(example, "Force Polling Rate=No\n", game=True)
        for line in example.splitlines():
            if line.startswith("#"):
                self.assertIn(line, merged)
        self.assertIn("Force Video Mode=480p", merged)
        self.assertNotIn("Force Polling Rate", merged)

    def test_polling_rate_lists_two_values_twice(self):
        rates = VALUES["forcePollRateStr"]
        self.assertEqual((rates.count("150Hz"), rates.count("120Hz")), (2, 2))
        fields = pairs(self.run_harness("game-fields", "GALE", "E", stdin="Force Polling Rate=150Hz"))
        self.assertEqual(fields["forcePollRate"], str(rates.index("150Hz")))

    def reference_rows(self, heading: str) -> dict:
        text = DOC.read_text()
        section = text[text.index(heading):]
        following = section.find("\n## ", len(heading))
        section = section if following < 0 else section[:following]
        return dict(re.findall(r"^\| `([^`]+)` \|(.*)$", section, re.M))

    def check_reference(self, rows: dict, keys: dict):
        for key, kind in keys.items():
            self.assertIn(key, rows, f"SETTINGS.md does not list {key}")
            if kind in VALUES:
                for value in dict.fromkeys(VALUES[kind]):
                    if (key, value) not in REJECTED:
                        self.assertIn(f"`{value}`", rows[key], (key, value))
            elif kind == "yes-no":
                self.assertIn("`Yes`", rows[key], key)
                self.assertIn("`No`", rows[key], key)
            elif isinstance(kind, tuple):
                for value in kind:
                    self.assertIn(f"`{value}`", rows[key], key)

    def test_reference_lists_every_key_and_value(self):
        global_rows = self.reference_rows("## Keys in global.ini")
        game_rows = self.reference_rows("## Keys in a game's file")
        self.check_reference(global_rows, GLOBAL_KEYS)
        self.check_reference(game_rows, GAME_KEYS)
        self.assertEqual(set(global_rows) - set(GLOBAL_KEYS) - OLD_NAMES, set())
        self.assertEqual(set(game_rows) - set(GAME_KEYS), set())
        for name in OLD_NAMES:
            self.assertIn(name, global_rows)

    def test_examples_take_effect_as_written(self):
        example = (EXAMPLES / "global.ini").read_text()
        wanted = pairs(example)
        self.assertGreater(len(wanted), 3)
        written = self.global_file(example)
        for key, value in wanted.items():
            self.assertEqual(written.get(key), value, key)
        game = (EXAMPLES / "game/GALE.ini").read_text()
        wanted = pairs(game)
        written = pairs(self.run_harness("game", "GALE", "E", str(EXAMPLES / "global.ini"), stdin=game))
        for key, value in wanted.items():
            self.assertEqual(written.get(key), value, key)


if __name__ == "__main__":
    unittest.main()
