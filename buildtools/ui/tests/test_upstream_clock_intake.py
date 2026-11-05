#!/usr/bin/env python3
"""Exercise the environment publisher and Sync Clock setup paths."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def function(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(marker)


class ClockIntake(unittest.TestCase):
    def test_real_environment_publisher_holds_lock(self):
        source = (ROOT / "cube/swiss/source/config/config.c").read_text()
        publisher = function(source, "void config_update_environ()")
        driver = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define GCDIGITAL_COMPAT 2
#define GCVIDEO_COMPAT 3
#define USBGECKO_MEMCARD_SLOT_A 1
#define CODESET 0
#define in_range(v,a,b) ((v)>=(a)&&(v)<=(b))
static struct {
 int aveCompat,sramLanguage,enableUSBGecko,waitForUSBGecko;
 bool rt4kOptim,disableRecalibration,disableRumble;
} swissSettings;
static char txtbuffer[256];
static const char *aveCompatStr[]={"a","b","c","d"};
static const char *sramLanguageEnvStr[]={"en","fr"};
static int lockDepth,locks,unlocks,operations;
static void __env_lock(void *unused) {
 (void)unused; assert(lockDepth==0);lockDepth++;locks++;
}
static void __env_unlock(void *unused) {
 (void)unused;assert(lockDepth==1);lockDepth--;unlocks++;
}
static int setenv(const char *key,const char *value,int replace) {
 assert(lockDepth==1&&replace==1&&key&&value);operations++;return 0;
}
static int unsetenv(const char *key) {
 assert(lockDepth==1&&key);operations++;return 0;
}
static int putenv(char *value) {
 assert(lockDepth==1&&value);operations++;return 0;
}
static const char *nl_langinfo(int item) {(void)item;return "UTF-8";}
static void __PADDisableRecalibration(bool v) {(void)v;assert(lockDepth==1);}
static void __PADDisableRumble(bool v) {(void)v;assert(lockDepth==1);}
/* ENVIRONMENT PUBLISHER */
int main(void) {
 config_update_environ();
 swissSettings.enableUSBGecko=2;swissSettings.rt4kOptim=true;swissSettings.aveCompat=2;
 config_update_environ();
 assert(locks==2&&unlocks==2&&lockDepth==0&&operations==12);
 puts("environment publication: complete lock scope across both settings branches");
}
'''
        driver = driver.replace("/* ENVIRONMENT PUBLISHER */", publisher)
        with tempfile.TemporaryDirectory(prefix="indigo-clock-intake-") as directory:
            path = Path(directory)
            (path / "clock.c").write_text(driver)
            command = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                           str(path / "clock.c"), "-o", str(path / "clock")], check=True)
            subprocess.run([str(path / "clock")], check=True)

    def test_sync_clock_setup_uses_the_timezone_resolver(self):
        gui = ROOT / "cube/swiss/source/gui"
        source = (gui / "settings.c").read_text()
        toggle = function(source, "void settings_toggle(")
        start = toggle.index("case SET_NTP_SYNC:")
        end = toggle.index("case SET_FSP_HOSTIP:", start)
        clock_arms = toggle[start:end]
        driver = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_time_sync.h"

enum { SET_NTP_SYNC, SET_NTP_HOSTIP, SET_TIME_ZONE };
enum { ENTRYMODE_ALPHA = 1, ENTRYMODE_NUMERIC = 2, ENTRYMODE_IP = 4, D_WARN };
typedef int uiDrawObj_t;
static struct { bool ntpSync; char timeZone[64], ntpHostIp[16]; } swissSettings;
static const char *entered;
static int edits, warnings;
static uiDrawObj_t dialog;

static void DrawGetTextEntry(int mode, const char *label, void *text, int size)
{
    assert(label && mode == (ENTRYMODE_NUMERIC | ENTRYMODE_ALPHA));
    assert(size == (int)sizeof(swissSettings.timeZone) - 1);
    ++edits;
    if(entered != NULL) {
        assert(strlen(entered) <= (size_t)size);
        strcpy(text, entered);
    }
}
static uiDrawObj_t *DrawMessageBox(int kind, const char *text)
{
    assert(kind == D_WARN && text);
    return &dialog;
}
static uiDrawObj_t *DrawPublish(uiDrawObj_t *box) { return box; }
static void wait_press_A(void) { ++warnings; }
static void DrawDispose(uiDrawObj_t *box) { assert(box == &dialog); }

/* TIME ZONE EDITOR */
static void change(int option)
{
    switch(option) {
        /* CLOCK ARMS */
    }
}

int main(void)
{
    const struct {
        int option;
        bool wasOn;
        const char *zone, *entry;
        bool nowOn;
        const char *expectedZone;
        int expectedEdits, expectedWarnings;
    } cases[] = {
        {SET_NTP_SYNC, false, "", "America/Denver", true, "America/Denver", 1, 0},
        {SET_NTP_SYNC, true, "America/Denver", NULL, false, "America/Denver", 0, 0},
        {SET_NTP_SYNC, false, "Europe/London", NULL, true, "Europe/London", 0, 0},
        {SET_NTP_SYNC, false, "Etc/Unknown", "Asia/Kolkata", true, "Asia/Kolkata", 1, 0},
        {SET_NTP_SYNC, false, "Etc/Unknown", NULL, true, "Etc/Unknown", 1, 0},
        {SET_NTP_SYNC, false, "", NULL, true, "", 1, 0},
        {SET_NTP_SYNC, true, "", NULL, false, "", 0, 0},
        {SET_TIME_ZONE, true, "America/Denver", "America/No_Such_City", true, "America/Denver", 1, 1},
        {SET_TIME_ZONE, true, "America/Denver", "MST7MDT,M3.2.0,M11.1.0", true, "MST7MDT,M3.2.0,M11.1.0", 1, 0},
        {SET_TIME_ZONE, true, "America/Denver", "", true, "", 1, 0},
        {SET_TIME_ZONE, true, "Unknown saved value", NULL, true, "Unknown saved value", 1, 0},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        strcpy(swissSettings.timeZone, cases[i].zone);
        swissSettings.ntpSync = cases[i].wasOn;
        entered = cases[i].entry;
        edits = warnings = 0;
        change(cases[i].option);
        assert(edits == cases[i].expectedEdits);
        assert(warnings == cases[i].expectedWarnings);
        assert(swissSettings.ntpSync == cases[i].nowOn);
        assert(strcmp(swissSettings.timeZone, cases[i].expectedZone) == 0);
    }
    puts("Sync Clock setup: 11 settings transitions passed");
}
'''
        driver = driver.replace("/* TIME ZONE EDITOR */", function(source, "static void settingsEditTimeZone("))
        driver = driver.replace("/* CLOCK ARMS */", clock_arms)
        with tempfile.TemporaryDirectory(prefix="indigo-clock-setup-") as directory:
            path = Path(directory)
            (path / "clock-setup.c").write_text(driver)
            command = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                           "-I", str(gui), "-I", str(gui.parent.parent / "include"),
                           str(path / "clock-setup.c"), str(gui / "ui_time_sync.c"),
                           str(Path(__file__).with_name("tzdata_host.c")),
                           "-o", str(path / "clock-setup")], check=True)
            subprocess.run([str(path / "clock-setup")], check=True)


if __name__ == "__main__":
    unittest.main()
