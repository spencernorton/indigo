#!/usr/bin/env python3
"""Run the real pack cache/reader over pthreads and controlled device reads.

No console or images are needed: CMPR transport is byte-exact. The device can
hold a read in flight while the menu pauses, replaces a window or cancels.
The ordinary cache suite separately covers the complete format/CRC contract.
"""

import argparse
import os
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

from test_cheats_gx_stream import extract_function

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GUI = ROOT / "cube/swiss/source/gui"

STUBS = {
    "gccore.h": """
#ifndef ARTWORK_GCCORE_STUB_H
#define ARTWORK_GCCORE_STUB_H
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
#endif
""",
    "ogc/lwp.h": """
#include <gccore.h>
typedef u32 lwp_t;
#define LWP_THREAD_NULL 0xffffffffu
#define LWP_PRIO_NORMAL 64
s32 LWP_CreateThread(lwp_t *, void *(*)(void *), void *, void *, u32, u8);
s32 LWP_JoinThread(lwp_t, void **);
""",
    "ogc/semaphore.h": """
#include <gccore.h>
#define sem_t artworkSem_t
typedef u32 sem_t;
s32 LWP_SemInit(sem_t *, u32, u32);
s32 LWP_SemWait(sem_t);
s32 LWP_SemPost(sem_t);
s32 LWP_SemDestroy(sem_t);
""",
}


def check_menu_boundaries():
    swiss = (GUI.parent / "swiss.c").read_text()
    library = extract_function(swiss, "uiDrawObj_t* renderFileCarousel(")
    start = library.index("(void)padsButtonsTaken(waitButtons);")
    idle = extract_function(library[start:], "while(1)")
    assert idle.index("padsButtonsTaken(waitButtons)") < idle.index("DrawGameflowPollPosters();")
    assert idle.index("CardArt_Poll();") < idle.index("VIDEO_WaitVSync();")
    actions = library[start + library[start:].index(idle) + len(idle):]
    assert actions.index("DrawGameflowPausePosters();") < actions.index("bool openSettings")
    assert "} while(buttons == 0u);\n\t\t\tDrawGameflowPausePosters();" in swiss
    renderer = (GUI / "FrameBufferMagic.c").read_text()
    pause = extract_function(renderer, "void DrawGameflowPausePosters(")
    assert "UIAssets_PauseReads();" in pause
    lab = (GUI / "lab.c").read_text()
    restart = extract_function(lab, "static void lab_restart(")
    assert restart.index("DrawGameflowBlockPosters();") < restart.index("DrawWithVideoLocked(")
    upload = (GUI.parent / "wiiload.c").read_text()
    handler = extract_function(upload, "static void wiiload_handler(")
    assert handler.index("DrawGameflowBlockPosters();") < handler.index("DrawWithVideoLocked(")
    assert handler.index("DrawGameflowUnblockPosters();") < handler.index("LWP_ResumeThread(")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--thread-sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    assert not (args.sanitize and args.thread_sanitize)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    check_menu_boundaries()
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        for name in ("gccore.h", "ogc/lwp.h", "ogc/semaphore.h"):
            target = path / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(STUBS[name])
        (path / "malloc.h").write_text(
            "#include <stdlib.h>\n"
            "void *artworkTestAlloc(size_t alignment, size_t size);\n"
            "void artworkTestFree(void *ptr);\n"
            "#define memalign artworkTestAlloc\n#define free artworkTestFree\n")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-D_DEFAULT_SOURCE", "-DUI_ASSETS_HOST_BUILD",
            "-DUI_ASSETS_HOST_THREADS", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
            "-I" + str(path), "-I" + str(GUI)]
        if args.sanitize or args.thread_sanitize:
            command += ["-fsanitize=" + ("thread" if args.thread_sanitize else "address,undefined"),
                        "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
            if os.uname().sysname == "Linux":
                command += ["-fno-pie", "-no-pie"]
        def compile_cache(cache):
            subprocess.run(command + [str(HERE / "test_artwork_reader.c"), str(cache),
                           str(GUI / "ui_artwork_reader.c"), "-pthread", "-lz",
                           "-o", str(path / "reader")], check=True)

        compile_cache(GUI / "ui_assets.c")
        subprocess.run([str(path / "reader")] + (["benchmark"] if args.benchmark else []),
                       check=True, timeout=30)
        if args.mutations:
            original = (GUI / "ui_assets.c").read_text()
            for marker in ("static void cacheRequestWindow(", "static void cacheCancel("):
                function = extract_function(original, marker)
                assert "\tUIArtworkReader_Stop();\n" in function
                changed = function.replace("\tUIArtworkReader_Stop();\n", "", 1)
                mutant = path / "mutant.c"
                mutant.write_text(original.replace(function, changed, 1))
                compile_cache(mutant)
                result = subprocess.run([str(path / "reader")], capture_output=True, timeout=15)
                assert result.returncode != 0, marker + " survived without its drain barrier"
                print(marker + " missing drain: rejected", flush=True)
            mutant = path / "mutant.c"
            assert "c->src.threadSafe &&" in original
            mutant.write_text(original.replace("c->src.threadSafe &&", "", 1))
            compile_cache(mutant)
            result = subprocess.run([str(path / "reader")], capture_output=True, timeout=15)
            assert result.returncode != 0, "unsafe device threading survived"
            print("unsafe source offload: rejected", flush=True)


if __name__ == "__main__":
    main()
