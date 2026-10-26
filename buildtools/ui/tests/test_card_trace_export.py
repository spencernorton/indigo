#!/usr/bin/env python3
"""The diagnostic exporter must never write through a memory-card slot."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MARKER = "static void gameflowWriteCardTrace(void)"

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define DEVICE_CONFIG 0
#define LOC_MEMCARD_SLOT_A 1u
#define LOC_MEMCARD_SLOT_B 2u
typedef struct { unsigned location; } Device;
static Device settings;
static Device *devices[1];
static const char *text;
static bool mounted, succeeds;
static unsigned mounts, unmounts, writes;
static const char *CardSlots_Trace(void) { return text; }
static bool config_set_device(void) { mounts++; return mounted; }
static void config_unset_device(void) { unmounts++; }
static int config_file_write(char *name, char *contents)
{
    assert(strcmp(name, "swiss/settings/card-trace.txt") == 0);
    assert(contents == text);
    writes++;
    return succeeds;
}
'''

MAIN = r'''
int main(void)
{
    text = "";
    gameflowWriteCardTrace();
    text = "first trace";
    gameflowWriteCardTrace();
    assert(mounts == 0u && writes == 0u);
    devices[0] = &settings;
    /* Even a card-slot SD device must see no diagnostic I/O. */
    for(unsigned slot = 1u; slot <= 3u; slot++) {
        settings.location = slot;
        gameflowWriteCardTrace();
        assert(mounts == 0u && unmounts == 0u && writes == 0u);
    }
    settings.location = 4u; /* settings on the loader */
    gameflowWriteCardTrace();
    assert(mounts == 1u && writes == 0u && unmounts == 0u);
    mounted = true;
    gameflowWriteCardTrace();
    assert(writes == 1u && unmounts == 1u);
    /* A failed export remains retryable; a successful one isn't repeated. */
    succeeds = true;
    gameflowWriteCardTrace();
    assert(writes == 2u && unmounts == 2u);
    gameflowWriteCardTrace();
    assert(mounts == 3u && writes == 2u && unmounts == 2u);
    settings.location = 16u; /* settings on SD2SP2 */
    text = "a longer second trace";
    gameflowWriteCardTrace();
    assert(writes == 3u && unmounts == 3u);
    puts("card trace export: card-slot I/O isolation, failed writes retryable, unchanged trace skipped PASS");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = (ROOT / "cube/swiss/source/swiss.c").read_text()
    start = source.rindex(MARKER)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            break
    with tempfile.TemporaryDirectory(prefix="card-trace-") as temp:
        path = Path(temp)
        (path / "test.c").write_text(HARNESS + source[start:end + 1] + MAIN)
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                   "-Werror", "-g", str(path / "test.c"), "-o", str(path / "test")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
