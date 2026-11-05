#!/usr/bin/env python3
"""Menu Language's Ukrainian file (gui/lang/uk.po) against the code.

The console reads it with UILang_Parse, which drops an entry it can't trust,
so every entry here must come through: the real parser, built for the host,
reads the file and must keep them all and look each one up. Beyond what the
parser can tell, each entry's English must still be a string the interface
draws (buildtools/ui/lang_strings.py), and its Ukrainian must keep what the
code relies on: its trailing colon (settings labels lose it to the layout),
Swiss's English button prompt at its end (drawn as icons), a hint line's
controls with their spacing, capitals where the English is in capitals, a
settings help's title and blank line, and nothing the Cyrillic sheet and
the IPL font can't draw. A control's own name is never translated.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"
PO = GUI / "lang/uk.po"
sys.path.insert(0, str(ROOT / "buildtools/ui"))
import lang_strings  # noqa: E402

UKRAINIAN = set("АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя")
# The bytes Ukrainian's table may hold on the console: its strings, entries and
# index, about 140 KB in all, with room for the strings still in English.
RAM_BUDGET = 192 * 1024
PROMPTS = ("Press L + A to continue, or B to cancel.", "Press A to continue.",
           "Press A to continue", "Press A.", "Press A")


def entries():
    """(line, msgid, msgstr) for each entry, as Latin-1 and UTF-8 text."""
    found, key, parts, start = [], None, {}, 0
    lines = PO.read_text(encoding="utf-8").split("\n") + [""]
    for number, raw in enumerate(lines, 1):
        line = raw.strip()
        m = re.fullmatch(r'(msgid|msgstr) "(.*)"', line)
        if m:
            if m.group(1) == "msgid":
                if "msgstr" in parts:
                    found.append((start, parts["msgid"], parts["msgstr"]))
                parts, start = {}, number
            key = m.group(1)
            parts[key] = m.group(2)
        elif line.startswith('"') and key:
            parts[key] += line[1:-1]
        else:
            key = None
    if "msgstr" in parts:
        found.append((start, parts["msgid"], parts["msgstr"]))
    out = []
    for number, msgid, msgstr in found:
        # An escape is an IPL-font byte (a character up to U+00FF here); a
        # translation's letters are its UTF-8, read as they are.
        english = lang_strings.decode(msgid)
        text = lang_strings.decode(msgstr)
        if english:
            out.append((number, english, text))
    return out


def problems(english, text):
    found = []
    for ch in text:
        if ch in "\n\t" or 0x20 <= ord(ch) <= 0x7e or ch in UKRAINIAN:
            continue
        if 0x80 <= ord(ch) <= 0xff and ch in english:
            continue
        found.append(f"draws {ch!r}, which neither font has here")
        break
    for prompt in PROMPTS:
        if english.rstrip().endswith(prompt):
            if not text.rstrip().endswith(prompt):
                found.append(f'must end with "{prompt}", drawn as buttons')
            break
    if english.rstrip(" ").endswith(":") != text.rstrip(" ").endswith(":"):
        found.append("keeps or drops a trailing colon the English hasn't")
    if english[:len(english) - len(english.lstrip(" \n"))] != text[:len(text) - len(text.lstrip(" \n"))] or \
            english[len(english.rstrip(" \n")):] != text[len(text.rstrip(" \n")):]:
        found.append("leading or trailing spaces and newlines differ")
    letters = [c for c in english if c.isalpha()]
    if len(letters) >= 2 and all(c.isupper() for c in letters) and \
            any(c.islower() for c in text if c.isalpha()):
        found.append("the English is in capitals")
    structure = [next((n for n in lang_strings.CONTROLS if i.strip().startswith(n + "  ")), None)
                 for i in re.split(r" {3,}|\xb7", english)]
    if any(structure):
        theirs = [next((n for n in lang_strings.CONTROLS if i.strip().startswith(n + "  ")), None)
                  for i in re.split(r" {3,}|\xb7", text)]
        if structure != theirs:
            found.append("a hint line's controls and their spacing change")
    lines = english.split("\n")
    if len(lines) > 2 and lines[0].endswith(":") and lines[1] == "":
        mine = text.split("\n")
        if len(mine) < 2 or mine[1] != "":
            found.append("a settings help keeps its title, then a blank line")
        if len(mine) - 2 > 16:
            found.append("a settings help has at most 16 lines")
    if english.strip() in lang_strings.CONTROLS:
        found.append("a control's name is drawn as its icon")
    return found


def parse_on_host(count):
    """The console's parser keeps every entry and finds each one."""
    driver = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_lang.h"
int main(int argc, char **argv) {
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    static uiLangEntry_t entries[UI_LANG_MAX_ENTRIES];
    char *data, *copy;
    long size;
    size_t count;
    if(argc < 2 || !f) return 2;
    fseek(f, 0, SEEK_END); size = ftell(f); rewind(f);
    data = malloc((size_t)size + 1); copy = malloc((size_t)size + 1);
    if(fread(data, 1, (size_t)size, f) != (size_t)size) return 2;
    memcpy(copy, data, (size_t)size);
    count = UILang_Parse(copy, (size_t)size, entries, UI_LANG_MAX_ENTRIES);
    if(!UILang_Load(UI_LANG_UKRAINIAN, data, (size_t)size)) return 3;
    UILang_Select(UI_LANG_UKRAINIAN);
    for(size_t i = 0; i < count; i++) {
        const char *found = UILang_Text(entries[i].english);
        /* The first of two equal strings wins; the test forbids them. */
        if(strcmp(found, entries[i].text)) { printf("lookup %zu\n", i); return 4; }
    }
    if(strcmp(UILang_Text("No such string"), "No such string")) return 5;
    printf("%zu %zu %zu\n", count, UILang_MemoryFootprint(), sizeof(uiLangEntry_t));
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "driver.c"
        source.write_text(driver)
        binary = Path(tmp) / "driver"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(GUI), str(source),
            str(GUI / "ui_lang.c"), "-o", str(binary)]
        if "--sanitize" in sys.argv:
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
            if os.uname().sysname == "Linux":
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        out = subprocess.run([str(binary), str(PO)], capture_output=True, text=True, timeout=30)
        assert out.returncode == 0, f"parser driver failed ({out.returncode}): {out.stdout}"
        kept, held, entry = (int(n) for n in out.stdout.split())
        assert kept == count, f"the console's parser keeps {kept} of {count} entries"
    # What the console keeps in RAM with the language on: an entry there is two
    # 4-byte pointers. The file itself is let go once it is read.
    held -= kept * (entry - 8)
    assert held <= RAM_BUDGET, f"uk.po's table holds {held} bytes, over {RAM_BUDGET}"
    return held


def main():
    found = entries()
    assert len(found) > 500, "uk.po looks empty"
    drawn = lang_strings.strings()
    failures = []
    seen = set()
    for line, english, text in found:
        where = f"uk.po:{line}"
        if english in seen:
            failures.append(f"{where}: {english!r} twice")
        seen.add(english)
        if english not in drawn:
            failures.append(f"{where}: {english!r} is no longer a string the interface draws")
        failures += [f"{where}: {english[:40]!r}: {p}" for p in problems(english, text)]
    for failure in failures:
        print(failure)
    assert not failures, f"{len(failures)} problems in uk.po"
    held = parse_on_host(len(found))
    missing = [t for t in drawn if t not in seen]
    print(f"Menu Language: uk.po's {len(found)} entries hold, and the console's parser keeps "
          f"them all ({len(missing)} drawn strings stay English) in {held:,} bytes of RAM PASS")


if __name__ == "__main__":
    main()
