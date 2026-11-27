"""A failed parent listing must never be treated as a writable missing file."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FTP = Path(__file__).resolve().parents[2] / 'cube/swiss/source/devices/ftp/ftp_devoptab.c'


class FtpLookup(unittest.TestCase):
    def test_lookup_stat_and_create(self):
        src = FTP.read_text()
        lookup = src[src.index('static int FTP_PathInfo('):src.index('//assumption: offset+length')]
        stat = src[src.index('static int __ftp_stat('):src.index('static int __ftp_fstat(')]
        stub = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define FTP_MAXPATH 128
#define NET_ASSERT(x) assert(x)
#define NET_PRINTF(...) ((void)0)
#define FTP_FixPath(p, scratch) ((void)(scratch))
#define ReplaceForwardSlash(p) ((void)(p))
#define _FTP_lock() ((void)0)
#define _FTP_unlock() ((void)0)
typedef struct { char name[FTP_MAXPATH]; int size; bool isDirectory; } FTPDIRENTRY;
typedef struct node { FTPDIRENTRY item; struct node *next; } FTPDIRENTRYLISTITEM;
typedef struct { FTPDIRENTRYLISTITEM *list; } FTPDIRSTATESTRUCT;
typedef struct { int envIndex; } ftp_env;
typedef struct { int envIndex, offset, size; char filename[FTP_MAXPATH]; } FTPFILESTRUCT;
struct _reent { int _errno; };
static bool available;
static ftp_env env;
static FTPDIRENTRYLISTITEM existing = {{"existing", 17, false}, NULL};
static bool FTP_FindFirst(const char *dir, FTPDIRSTATESTRUCT *out, ftp_env *e) {
    (void)dir; (void)e; out->list = &existing; return available;
}
static void FTP_FindClose(FTPDIRSTATESTRUCT *d) { (void)d; }
static ftp_env *FindFTPEnv(const char *p) { (void)p; return &env; }
static void ExtractDevice(const char *src, char *dest) { if(src != dest) strcpy(dest, src); }
static void ftp_absolute_path_no_device(const char *src, char *dest, int unused) {
    (void)unused; strcpy(dest, src);
}
static void dentry_to_stat(FTPDIRENTRY *e, struct stat *st) { st->st_size = e->size; }
'''
        cases = r'''
int main(void) {
    struct stat st; struct _reent r = {0}; FTPFILESTRUCT f;
    available = false;
    assert(__ftp_stat(&r, "/existing", &st) == -1 && r._errno == EIO);
    assert(!FTP_OpenFile("/existing", &f, &env, O_WRONLY));
    assert(!FTP_OpenFile("/absent", &f, &env, O_RDWR));
    available = true;
    assert(__ftp_stat(&r, "/existing", &st) == 0 && st.st_size == 17);
    assert(FTP_OpenFile("/existing", &f, &env, O_RDONLY) && f.size == 17);
    assert(__ftp_stat(&r, "/absent", &st) == -1 && r._errno == ENOENT);
    assert(!FTP_OpenFile("/absent", &f, &env, O_RDONLY));
    assert(FTP_OpenFile("/absent", &f, &env, O_WRONLY) && f.size == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / 'lookup.c'
            binary = test.with_suffix('')
            test.write_text('\n'.join((stub, lookup, stat, cases)))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            str(test), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
