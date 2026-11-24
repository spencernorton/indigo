"""Exercise the GameCube's one-retry policy with a scripted transport and card."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class StartupUpdateRetry(unittest.TestCase):
    def test_download_slot(self):
        path = Path(__file__).resolve().parents[2] / 'cube/swiss/source/gui/update.c'
        source = path.read_text()
        function = source[source.index('static update_result_t downloadSlot('):
                          source.index('static update_result_t installSlot(')]
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef enum { UPDATE_OK, UPDATE_HASH_MISMATCH, UPDATE_CANCELLED,
               UPDATE_WRITE_FAIL, UPDATE_TIMEOUT } update_result_t;
typedef struct { const char *manifest; } update_check_t;
typedef struct file file_handle;
typedef struct { int (*deleteFile)(file_handle *); } device;
struct file { device *device; int offset; };
static update_check_t release = {"one authenticated manifest"};
static file_handle inactive;
static update_result_t responses[2];
static unsigned requests, deletions, opens;
static bool cancelled, card_error;
static int discard(file_handle *file) {
    assert(file == &inactive && requests == 1 && file->offset > 0);
    ++deletions;
    return card_error;
}
static device card = {discard};
static void slotFile(file_handle *file, int slot, const char *suffix) {
    assert(file == &inactive && slot == 1 && strcmp(suffix, "dol") == 0);
    assert(opens == 0 || (requests == 1 && deletions == 1 && !card_error));
    ++opens;
    file->device = &card;
    file->offset = 0;
}
static update_result_t downloadSlotPayload(update_check_t *check, file_handle *file, bool retry) {
    assert(check == &release && file == &inactive && file->offset == 0);
    assert(requests < 2 && retry == (requests == 1) && opens == requests + 1);
    file->offset = 32768;
    return responses[requests++];
}
static bool updateCancelPressed(void) { return cancelled; }
static void print_debug(const char *line) { (void)line; }
'''
        cases = r'''
static void expect(bool automatic, update_result_t first, update_result_t second,
                   bool press_b, bool delete_error, update_result_t result,
                   unsigned expected_requests, unsigned expected_deletions) {
    responses[0] = first; responses[1] = second;
    requests = deletions = opens = 0;
    cancelled = press_b; card_error = delete_error;
    assert(downloadSlot(&release, &inactive, 1, automatic) == result);
    assert(requests == expected_requests && opens == expected_requests);
    assert(deletions == expected_deletions);
}
int main(void) {
    expect(true, UPDATE_OK, UPDATE_OK, false, false, UPDATE_OK, 1, 0);
    expect(true, UPDATE_HASH_MISMATCH, UPDATE_OK, false, false, UPDATE_OK, 2, 1);
    expect(true, UPDATE_HASH_MISMATCH, UPDATE_HASH_MISMATCH, false, false, UPDATE_HASH_MISMATCH, 2, 1);
    expect(true, UPDATE_HASH_MISMATCH, UPDATE_CANCELLED, false, false, UPDATE_CANCELLED, 2, 1);
    expect(true, UPDATE_HASH_MISMATCH, UPDATE_OK, true, false, UPDATE_CANCELLED, 1, 0);
    expect(true, UPDATE_HASH_MISMATCH, UPDATE_OK, false, true, UPDATE_WRITE_FAIL, 1, 1);
    expect(true, UPDATE_WRITE_FAIL, UPDATE_OK, false, false, UPDATE_WRITE_FAIL, 1, 0);
    expect(true, UPDATE_TIMEOUT, UPDATE_OK, false, false, UPDATE_TIMEOUT, 1, 0);
    expect(true, UPDATE_CANCELLED, UPDATE_OK, false, false, UPDATE_CANCELLED, 1, 0);
    expect(false, UPDATE_HASH_MISMATCH, UPDATE_OK, false, false, UPDATE_HASH_MISMATCH, 1, 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            test = Path(temp) / 'retry.c'
            executable = Path(temp) / 'retry'
            test.write_text('\n'.join((fixture, function, cases)))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            str(test), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
