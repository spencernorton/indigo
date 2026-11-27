"""A button that dismisses an update dialogue must not operate Home."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


GUI = Path(__file__).resolve().parents[2] / 'cube/swiss/source/gui'


class UpdateButtons(unittest.TestCase):
    def test_held_and_latched_answers_are_consumed(self):
        code = (GUI / 'update.c').read_text()
        dialogs = code[code.index('static void releaseUpdateButtons('):code.index('/* Progress with')]
        wait = next(line for line in (GUI / 'IPLFontWrite.h').read_text().splitlines()
                    if line.startswith('#define wait_press_A()'))
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef unsigned u32;
typedef int uiDrawObj_t;
enum { BUTTON_A = 1, BUTTON_B = 2, OTHER = 4 };
static const u32 *frames;
static size_t frame, count;
static u32 presses;
static uiDrawObj_t box;
static u32 padsButtonsHeld(void) { return frames[frame]; }
static u32 padsButtonsTaken(u32 mask) { u32 hit = presses & mask; presses &= ~mask; return hit; }
static void VIDEO_WaitVSync(void) {
    assert(frame + 1 < count);
    presses |= frames[frame + 1] & ~frames[frame];
    ++frame;
}
static uiDrawObj_t *DrawMessageBox(int type, const char *text) { (void)type; (void)text; return &box; }
static uiDrawObj_t *DrawPublish(uiDrawObj_t *item) { return item; }
static void DrawDispose(uiDrawObj_t *item) {
    assert(item == &box && padsButtonsHeld() == 0 && presses == OTHER);
}
static void start(const u32 *sequence, size_t n) {
    frames = sequence; count = n; frame = 0; presses = frames[0] | OTHER;
}
'''
        cases = r'''
int main(void) {
    const u32 warning[] = {BUTTON_B, 0, BUTTON_A, BUTTON_A, 0};
    const u32 cancel[] = {BUTTON_A, 0, BUTTON_B, 0};
    const u32 install[] = {0, BUTTON_A, BUTTON_A, 0};
    bool answer = true;
    start(warning, 5); updateTell(0, "cancelled");
    start(cancel, 4); assert(!updateAsk(0, "install?", &answer) && !answer);
    start(install, 4); assert(updateAsk(0, "install?", &answer) && answer);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / 'input.c'
            binary = Path(td) / 'input'
            source.write_text('\n'.join((fixture, wait, dialogs, cases)))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_manual_check_starts_with_fresh_cancel_input(self):
        code = (GUI / 'update.c').read_text()
        constructor = code[code.index('static update_check_t *newCheck('):
                           code.index('static void fetchCheck(')]
        cancel = code[code.index('static bool updateCancelPressed('):
                      code.index('static bool updateDeadlineHit(')]
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { BUTTON_A = 1, BUTTON_B = 2 };
enum { UPDATE_NOT_CONFIGURED = 1, UPDATE_BAD_SERVER, UPDATE_NO_SPACE, UPDATE_HEAD_MAX = 64 };
enum update_source { UPDATE_SOURCE_HTTP, UPDATE_SOURCE_SMB };
typedef int ui_update_base_err_t;
typedef struct { char host[20]; } base_t;
typedef struct {
    bool quiet;
    void (*fetchManifest)(void), (*fetchFile)(void);
    base_t base;
    const char *hostHeader;
    char *scratch;
} update_source_state_t;
typedef struct {
    update_source_state_t source;
    char channel[16], server[64];
    int result;
} update_check_t;
static struct { char smbShare[8], smbServerIp[16], updateServer[64]; int updateChannel; } swissSettings;
#define UPDATE_DEFAULT_SERVER "192.0.2.1"
#define UPDATE_DEFAULT_HOST "example.invalid"
#define UIUpdate_ChannelName(channel) "stable"
#define UIUpdate_ParseBase(text,base,why) ((void)(why), true)
#undef strlcpy
#define strlcpy(dst,src,size) snprintf(dst, size, "%s", src)
static void smbFetchManifest(void) {}
static void smbFetchFile(void) {}
static void httpFetchManifest(void) {}
static void httpFetchFile(void) {}
static unsigned held, latched;
static unsigned padsButtonsHeld(void) { return held; }
static unsigned padsButtonsTaken(unsigned mask) {
    unsigned taken = latched & mask;
    latched &= ~mask;
    return taken;
}
'''
        cases = r'''
int main(void) {
    for (unsigned source = UPDATE_SOURCE_HTTP; source <= UPDATE_SOURCE_SMB; source++) {
        latched = BUTTON_A | BUTTON_B; held = 0;
        update_check_t *check = newCheck(source, false);
        assert(check && !updateCancelPressed() && latched == BUTTON_A);
        held = BUTTON_B;
        assert(updateCancelPressed());
        held = 0; latched |= BUTTON_B;
        assert(updateCancelPressed() && !updateCancelPressed());
        free(check->source.scratch); free(check);
    }
    latched = BUTTON_A | BUTTON_B;
    update_check_t *background = newCheck(UPDATE_SOURCE_HTTP, true);
    assert(background && latched == (BUTTON_A | BUTTON_B));
    free(background->source.scratch); free(background);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'start_check.c'
            binary = Path(td) / 'start_check'
            src.write_text('\n'.join((fixture, constructor, cancel, cases)))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
