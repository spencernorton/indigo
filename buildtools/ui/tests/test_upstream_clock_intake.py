#!/usr/bin/env python3
"""Exercise the actual clock aliases and environment publisher from config.c."""
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
    def test_real_aliases_keep_sync_opt_in_and_publisher_holds_lock(self):
        source = (ROOT / "cube/swiss/source/config/config.c").read_text()
        start = source.index('else if(!strcmp("NTPSync", name))')
        end = source.index('else if(!strcmp("LabServer", name))', start)
        aliases = source[start:end]
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
 bool rt4kOptim,disableRecalibration,disableRumble,ntpSync;
 char ntpHostIp[16],timeZone[64];
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
#undef strlcpy
#define strlcpy testCopy
static size_t strlcpy(char *to,const char *from,size_t size) {
 size_t n=strlen(from);if(size){size_t copied=n<size?n:size-1;memcpy(to,from,copied);to[copied]=0;}return n;
}
static void parse(const char *name,const char *value) {
 if(false) {} /* CLOCK ALIASES */
}
/* ENVIRONMENT PUBLISHER */
int main(void) {
 parse("SNTPHostIP","192.0.2.123");
 parse("System Time Zone","America/Denver");
 assert(!swissSettings.ntpSync);
 assert(!strcmp(swissSettings.ntpHostIp,"192.0.2.123"));
 assert(!strcmp(swissSettings.timeZone,"America/Denver"));
 parse("NTPHostIP","192.0.2.1");parse("TimeZone","IST-5:30");
 assert(!strcmp(swissSettings.ntpHostIp,"192.0.2.1"));
 assert(!strcmp(swissSettings.timeZone,"IST-5:30"));
 parse("System Time Zone","Etc/Unknown");
 assert(!strcmp(swissSettings.timeZone,"Etc/Unknown"));
 char longValue[128];memset(longValue,'x',127);longValue[127]=0;
 parse("TimeZone",longValue);parse("SNTPHostIP",longValue);
 assert(strlen(swissSettings.timeZone)==63&&strlen(swissSettings.ntpHostIp)==15);
 parse("NTPSync","Yes");assert(swissSettings.ntpSync);
 parse("NTPSync","No");assert(!swissSettings.ntpSync);
 config_update_environ();
 swissSettings.enableUSBGecko=2;swissSettings.rt4kOptim=true;swissSettings.aveCompat=2;
 config_update_environ();
 assert(locks==2&&unlocks==2&&lockDepth==0&&operations==12);
 puts("clock aliases: bounded, opt-in; environment publication: complete lock scope");
}
'''
        driver = driver.replace("/* CLOCK ALIASES */", aliases)
        driver = driver.replace("/* ENVIRONMENT PUBLISHER */", publisher)
        with tempfile.TemporaryDirectory(prefix="indigo-clock-intake-") as directory:
            path = Path(directory)
            (path / "clock.c").write_text(driver)
            command = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                           str(path / "clock.c"), "-o", str(path / "clock")], check=True)
            subprocess.run([str(path / "clock")], check=True)


if __name__ == "__main__":
    unittest.main()
