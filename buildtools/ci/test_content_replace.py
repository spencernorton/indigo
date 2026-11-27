"""Exercise the content installer's FAT rename and interruption states."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

GUI = Path(__file__).resolve().parents[2] / 'cube/swiss/source/gui'


class ContentReplace(unittest.TestCase):
    def test_preserve_old_file_or_publish_verified_replacement(self):
        source = (GUI / 'update.c').read_text()
        functions = source[source.index('static bool contentTarget('):
                           source.index('/* Whether the card already holds')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct { char name[64]; } file_handle;
typedef struct {
    int (*statFile)(file_handle *);
    int (*deleteFile)(file_handle *);
    int (*renameFile)(file_handle *, char *);
} DEVICEHANDLER_INTERFACE;
static int fail_publish, fail_rollback;
static int stat_file(file_handle *file) { return access(file->name, F_OK); }
static int erase(file_handle *file) { return unlink(file->name); }
static int move(file_handle *from, char *to) {
    if (access(to, F_OK) == 0) return -1; /* FAT refuses to overwrite. */
    if (fail_publish && strcmp(from->name, "test.part") == 0) {
        strcpy(from->name, to); /* Some FAT failures also change the handle. */
        return -1;
    }
    if (fail_rollback && strcmp(from->name, "test.indigo-old") == 0) return -1;
    if (rename(from->name, to)) return -1;
    strcpy(from->name, to);
    return 0;
}
static void contentFile(file_handle *f, DEVICEHANDLER_INTERFACE *device,
                        const char *path, const char *suffix) {
    (void)device;
    snprintf(f->name, sizeof(f->name), "%s%s", path, suffix);
}
static void write_byte(const char *name, char value) {
    FILE *stream = fopen(name, "wb");
    assert(stream && fputc(value, stream) == value && fclose(stream) == 0);
}
static int read_byte(const char *name) {
    FILE *stream = fopen(name, "rb");
    if (!stream) return EOF;
    int byte = fgetc(stream);
    assert(fclose(stream) == 0);
    return byte;
}
'''
        cases = r'''
int main(void) {
    DEVICEHANDLER_INTERFACE device = { stat_file, erase, move };
    file_handle part, target, previous;
    const char *path = "test", *old = "test.indigo-old";
    contentFile(&part, &device, path, ".part");
    write_byte(part.name, 'a');
    assert(contentPublish(&device, path, &part));
    assert(read_byte(path) == 'a' && read_byte(old) == EOF);

    for (int interrupted = 0; interrupted <= 1; interrupted++) {
        contentFile(&part, &device, path, ".part");
        write_byte(part.name, 'b');
        fail_publish = 1; fail_rollback = interrupted;
        assert(!contentPublish(&device, path, &part));
        assert(read_byte(interrupted ? old : path) == 'a');
        fail_publish = fail_rollback = 0;
        assert(contentTarget(&device, path, &target, &previous));
        assert(read_byte(path) == 'a' && read_byte(old) == EOF);
    }

    /* On a retry, discard an older backup only after locating the live file. */
    write_byte(old, 'z');
    contentFile(&part, &device, path, ".part");
    assert(contentPublish(&device, path, &part));
    assert(read_byte(path) == 'b' && read_byte(old) == EOF);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            test = root / 'replace.c'
            test.write_text('\n'.join((harness, functions, cases)))
            binary = root / 'replace'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            str(test), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=root, check=True)
