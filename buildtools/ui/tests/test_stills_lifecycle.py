#!/usr/bin/env python3
"""Run the renderer's pack lifecycle against counted device/cache stand-ins.

Non-Spotlight requests must not touch stills, including a missing poster
pack, and only the Jewel Case opens discs. Switching layout, switching device, failure and cancellation exercise
the actual open/request/cancel functions from FrameBufferMagic.c.
"""

import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "cube/swiss/source/gui/FrameBufferMagic.c").read_text()

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef struct Device DEVICEHANDLER_INTERFACE;
typedef struct File { char name[256]; DEVICEHANDLER_INTERFACE *device; } file_handle;
struct Device { file_handle *initial; void (*closeFile)(file_handle *); };
typedef struct { int unused; } uiAssetsSource_t;
typedef struct { int unused; } uiAssetsSync_t;
typedef struct { int relativeSlot; char gameId[8]; } uiGameflowCardSnapshot_t;
typedef struct { u32 recordCount; int layout; uiGameflowCardSnapshot_t records[25]; } uiGameflowRenderSnapshot_t;
#define UI_ASSETS_OK 0
#define UI_ASSETS_SLOTS 25
#define UI_STILLS_SLOTS 3
#define UI_DISCS_SLOTS 3
#define UI_ASSETS_WINDOW 9
#define UI_ASSETS_ID_LEN 6
#define UI_GAMEFLOW_LAYOUT_GRID 2
#define UI_GAMEFLOW_LAYOUT_SPOTLIGHT 3
#define UI_GAMEFLOW_LAYOUT_JEWEL_CASE 4
#define LWP_MUTEX_NULL 0
static int _videomutex = 1;
static DEVICEHANDLER_INTERFACE *posterPackDevice;
static bool posterPackAttempted, posterPackFileOwned, stillsPackFileOwned, stillsPackAttempted;
static bool discsPackFileOwned, discsPackAttempted, discsReady;
static bool descriptionsAttempted, postersReady, stillsReady, noPosters, noStills;
static file_handle posterPackFile, stillsPackFile, discsPackFile;
static unsigned posterOpens, stillOpens, discOpens, closes, stillRequests, discRequests;
static unsigned fullPosterReads, visiblePolls;
static u32 gameflowArtworkLayout;
static bool pollStills, pollDiscs;
static unsigned manualForgets;
static bool manualReady;
static void Manual_Forget(void) { manualReady=false;manualForgets++; }
static bool UIAssets_Poll(void) {fullPosterReads++;return true;}
static bool UIAssets_PollVisible(bool stills, bool discs) {
    visiblePolls++;pollStills=stills;pollDiscs=discs;return true;
}
static bool UIDiscs_Ready(void) { return discsReady; }
static void UIDiscs_CancelForDeviceChange(void) { discsReady = false; }
static int UIDiscs_Init(const uiAssetsSource_t *s, const uiAssetsSync_t *l) {
    (void)s;(void)l;discsReady = true;return 0;
}
static void UIDiscs_RequestWindow(const char ids[][8], int count, int selected) {
    (void)ids;(void)count;(void)selected;discRequests++;
}
static void closeFile(file_handle *f) { (void)f; closes++; }
static void concat_path(char *out, const char *root, const char *leaf) {
    size_t a = strlen(root), b = strlen(leaf);
    assert(a + b + 2 <= 256);
    memcpy(out, root, a); out[a] = '/';
    memcpy(out + a + 1, leaf, b + 1);
}
static bool UIAssets_Ready(void) { return postersReady; }
static bool UIStills_Ready(void) { return stillsReady; }
static void UIAssets_CancelForDeviceChange(void) { postersReady = false; }
static void UIStills_CancelForDeviceChange(void) { stillsReady = false; }
static void UIAssets_SyncFromMutex(int mutex, uiAssetsSync_t *s) { (void)mutex;(void)s; }
static int UIAssets_SourceFromFileHandle(file_handle *f, uiAssetsSource_t *s) {
    (void)s;
    if(strstr(f->name, "stills.pak")) { stillOpens++; return noStills ? -1 : 0; }
    if(strstr(f->name, "discs.pak")) { discOpens++; return 0; }
    posterOpens++; return noPosters ? -1 : 0;
}
static int UIAssets_Init(const uiAssetsSource_t *s, const uiAssetsSync_t *l) {
    (void)s;(void)l;postersReady = true;return 0;
}
static int UIStills_Init(const uiAssetsSource_t *s, const uiAssetsSync_t *l) {
    (void)s;(void)l;stillsReady = true;return 0;
}
static bool _GameflowSnapshotValid(const uiGameflowRenderSnapshot_t *s) { return s != NULL; }
static void UIAssets_RequestWindow(const char ids[][8], int count, int selected) {
    (void)ids;(void)count;(void)selected;
}
static void UIStills_RequestWindow(const char ids[][8], int count, int selected) {
    (void)ids;(void)count;(void)selected;stillRequests++;
}
'''

DRIVER = r'''
int main(void) {
    file_handle aRoot = {.name="a:"}, bRoot = {.name="b:"};
    DEVICEHANDLER_INTERFACE a = {&aRoot, closeFile}, b = {&bRoot, closeFile};
    uiGameflowRenderSnapshot_t snapshot = {.recordCount=1};
    memcpy(snapshot.records[0].gameId, "GTEST1", 7);
    /* A manual may be open without any artwork pack: cancel it even on
     * the early-return path, before a device is released. */
    manualReady=true;
    DrawGameflowCancelPosters();
    assert(!manualReady && manualForgets==1 && closes==0);
    assert(DrawGameflowPrimePoster());
    assert(fullPosterReads==1 && visiblePolls==0);
    assert(DrawGameflowPollPosters());
    assert(fullPosterReads==1 && visiblePolls==1 && !pollStills && !pollDiscs);
    for(int layout=0;layout<3;layout++) {
        snapshot.layout=layout;
        DrawGameflowRequestPosters(&a,&snapshot);
        DrawGameflowPollPosters();
        assert(!pollStills && !pollDiscs);
    }
    assert(posterOpens==1 && stillOpens==0 && !stillsReady && discOpens==0);
    snapshot.layout=UI_GAMEFLOW_LAYOUT_SPOTLIGHT;
    DrawGameflowRequestPosters(&a,&snapshot);
    DrawGameflowRequestPosters(&a,&snapshot);
    assert(stillOpens==1 && stillRequests==2 && stillsReady && discOpens==0);
    DrawGameflowPollPosters();
    assert(pollStills && !pollDiscs);
    /* Discs: only the Jewel Case opens them, once per device. */
    snapshot.layout=UI_GAMEFLOW_LAYOUT_JEWEL_CASE;
    DrawGameflowRequestPosters(&a,&snapshot);
    DrawGameflowRequestPosters(&a,&snapshot);
    assert(discOpens==1 && discRequests==2 && discsReady && stillRequests==2);
    DrawGameflowPollPosters();
    assert(!pollStills && pollDiscs);
    snapshot.layout=0;
    manualReady=true;
    DrawGameflowRequestPosters(&b,&snapshot);
    assert(!manualReady);
    DrawGameflowPollPosters();
    assert(!pollStills && !pollDiscs);
    assert(posterOpens==2 && stillOpens==1 && !stillsReady && !discsReady && closes==3);
    snapshot.layout=UI_GAMEFLOW_LAYOUT_SPOTLIGHT;
    noStills=true;
    DrawGameflowRequestPosters(&b,&snapshot);
    DrawGameflowRequestPosters(&b,&snapshot);
    assert(stillOpens==2 && !stillsReady && stillRequests==2);
    DrawGameflowCancelPosters();
    assert(!stillsPackAttempted && !posterPackAttempted);
    noStills=false;noPosters=true;
    snapshot.layout=0;
    DrawGameflowRequestPosters(&a,&snapshot);
    assert(stillOpens==2);
    snapshot.layout=UI_GAMEFLOW_LAYOUT_SPOTLIGHT;
    DrawGameflowRequestPosters(&a,&snapshot);
    assert(stillOpens==3 && stillsReady && !postersReady);
    DrawGameflowCancelPosters();
    assert(!postersReady && !stillsReady && !stillsPackFileOwned);
    assert(!discsPackFileOwned && !discsPackAttempted);
    _videomutex=LWP_MUTEX_NULL;
    DrawGameflowRequestPosters(&a,&snapshot);
    assert(stillOpens==3 && !stillsPackAttempted);
    puts("stills and discs lifecycle: lazy layouts, late Spotlight, Jewel Case, device changes, failure and cancellation PASS");
    return 0;
}
'''


def main():
    functions = "\n".join(extract_function(SOURCE, signature) for signature in (
        "static void _GameflowClosePosterPackFile(",
        "static void _GameflowCloseStillsPackFile(",
        "static void _GameflowCloseDiscsPackFile(",
        "void DrawGameflowCancelPosters(",
        "static bool _GameflowOpenPosterPack(",
        "static bool _GameflowOpenStillsPack(",
        "static bool _GameflowOpenDiscsPack(",
        "void DrawGameflowRequestPosters(",
        "bool DrawGameflowPrimePoster(",
        "bool DrawGameflowPollPosters(",
    ))
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / "lifecycle.c").write_text(STUBS + functions + DRIVER)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1"]
        if "--sanitize" in sys.argv:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if sys.platform != "darwin":
                command += ["-no-pie"]
        subprocess.run(command + [str(path / "lifecycle.c"), "-o", str(path / "lifecycle")], check=True)
        subprocess.run([str(path / "lifecycle")], check=True, timeout=10)


if __name__ == "__main__":
    main()
