#!/usr/bin/env python3
"""The production GameID launch wait stays bounded across clock corrections."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MARKER = "static void gameflowWaitForCardIds(void)"
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef struct { int unused; } uiDrawObj_t;
static uiDrawObj_t box;
static u64 elapsed, readyAt;
static int64_t wallOffset, jump;
static unsigned polls, shown, disposed;
static u64 SYS_GetSystemTime(void) { return elapsed; }
static u64 gettime(void) { return (u64)((int64_t)elapsed + wallOffset); }
static u32 diff_msec(u64 start, u64 end) { return (u32)(end - start); }
static bool CardSlots_IdWaiting(void) { return elapsed < readyAt; }
static bool CardSlots_Poll(void) { polls++; return false; }
static void VIDEO_WaitVSync(void) {
    elapsed += 16u;
    if(elapsed == 208u) wallOffset += jump;
}
static uiDrawObj_t *DrawProgressBar(bool busy, int progress, const char *text) {
    assert(busy && progress == 0 && text != NULL); shown++; return &box;
}
static uiDrawObj_t *DrawPublish(uiDrawObj_t *value) { return value; }
static void DrawDispose(uiDrawObj_t *value) { assert(value == &box); disposed++; }
'''
MAIN = r'''
int main(void)
{
    for(int direction = -1; direction <= 1; direction++) {
        elapsed = 0u; readyAt = 800u; wallOffset = 1000000;
        jump = (int64_t)direction * 120000; polls = shown = disposed = 0u;
        gameflowWaitForCardIds();
        assert(!CardSlots_IdWaiting() && elapsed == 800u && polls == 50u);
        assert(shown == 1u && disposed == 1u);
        elapsed = 0u; readyAt = UINT64_MAX; wallOffset = 1000000;
        polls = shown = disposed = 0u;
        gameflowWaitForCardIds();
        assert(elapsed >= 65000u && elapsed <= 65016u);
        assert(shown == 1u && disposed == 1u);
    }
    elapsed = readyAt = 0u; polls = shown = disposed = 0u;
    gameflowWaitForCardIds();
    assert(polls == 0u && shown == 0u && disposed == 0u);
    puts("GameID launch wait: forward/backward clock steps, successful completion,65s bound and no pending ID PASS");
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
    with tempfile.TemporaryDirectory(prefix="card-launch-wait-") as temp:
        path = Path(temp)
        (path / "test.c").write_text(HARNESS + source[start:end + 1] + MAIN)
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                   "-Werror", "-Wno-unused-function", "-g", str(path / "test.c"),
                   "-o", str(path / "test")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
