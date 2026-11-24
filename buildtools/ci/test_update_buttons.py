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
