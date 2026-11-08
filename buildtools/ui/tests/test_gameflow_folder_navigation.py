#!/usr/bin/env python3
"""Execute the real folder filter, navigation branches and menu dispatch.

Only hardware, scene publication and the device's directory data are mocked.
--revision can replay a historical implementation against the same invariants.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def function(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)


def build_source(read):
    swiss = read("cube/swiss/source/swiss.c")
    files = read("cube/swiss/source/files.c")
    util = read("cube/swiss/source/util.c")
    carousel = function(swiss, "uiDrawObj_t* renderFileCarousel(")
    initial_focus = function(carousel, "if(curSelection == 0 && num_files > 1 && directory[0]->fileType==IS_SPECIAL)")
    # These are the actual activation and parent/back branches, not a policy
    # facsimile. The surrounding retained game-detail path is not entered by
    # a parent card. Its separate existing tests continue to cover game launch.
    actions = carousel
    if "static void filesActivate(" in swiss:
        # The lists' one copy of A, which the Library calls as its own.
        assert "filesActivate(directory, true);" in carousel
        actions = function(swiss, "static void filesActivate(")
    parent = function(actions, "else if(directory[curSelection]->fileType==IS_SPECIAL)")
    parent = parent[parent.index("{") + 1:parent.rindex("}")]
    x = function(carousel, "if(browserButtons & BUTTON_X)")
    b = carousel[carousel.index("if((browserButtons & BUTTON_B) && gameflowInsideFolder()) {"):]
    b = b[:b.index("if((browserButtons & BUTTON_START)")]
    menu = function(swiss, "void menu_loop()")
    home_effects = function(swiss, "static void homeDispatchEffect(")
    library_effect = home_effects[home_effects.index("case UI_HOME_EFFECT_OPEN_LIBRARY:"):
                                 home_effects.index("case UI_HOME_EFFECT_CHANGE_SOURCE:")]
    dispatch = function(menu, "if(devices[DEVICE_CUR] != NULL && curMenuLocation==ON_FILLIST)")
    refresh = function(menu, "if(devices[DEVICE_CUR] != NULL && needsRefresh)")
    helper = ""
    if "static void gameflowNavigateParent(" in swiss:
        helper = function(swiss, "static void gameflowNavigateParent(")
    snapshot = function(swiss, "static void gameflowSnapshotRecord(")
    folder_record = function(snapshot, "if(mode == UI_GAMEFLOW_LIBRARY_NONE && file->fileType == IS_DIR)")
    model = read("cube/swiss/source/gui/FrameBufferMagic.h")
    record_end = model.index("} uiGameflowCardSnapshot_t;") + len("} uiGameflowCardSnapshot_t;")
    record_start = model.rfind("typedef struct {", 0, record_end)
    record_sizes = "\n".join(re.findall(
        r"^#define UI_GAMEFLOW_(?:TITLE|COMPANY|FACTS)_LENGTH .*", model, re.MULTILINE))
    parts = [STUBS.replace("@CAROUSEL_FOCUS@", initial_focus),
             record_sizes, model[record_start:record_end]]
    for marker in ("char *endsWith(", "bool canLoadFileType(", "bool checkExtension(",
                   "char *getRelativeName(", "bool getParentPath(", "char *getDevicePath("):
        parts.append(function(util, marker))
    for marker in ("int fileComparator(", "int sortFiles(", "void scanFiles(",
                   "file_handle** getSortedDirEntries(", "file_handle* getCurrentDirEntries(",
                   "int getSortedDirEntryCount(", "int getCurrentDirEntryCount("):
        parts.append(function(files, marker))
    for marker in ("static bool gameflowCustomRootForDevice(",
                   "static void gameflowGamesRoot(",
                   "static bool gameflowPathPrefixMatches(",
                   "static bool gameflowResolveDirOnDevice("):
        if marker in swiss:
            parts.append(function(swiss, marker))
    for marker in ("bool upToParent(", "static uiGameflowLibraryEntryType_t gameflowEntryType(",
                   "static uiGameflowLibraryMode_t gameflowLibraryMode(",
                   "static int gameflowLibraryEntries(", "static bool gameflowInsideFolder("):
        parts.append(function(swiss, marker))
    for marker in ("static void filesUp(", "static void filesHome(",
                   "static DEVICEHANDLER_INTERFACE *homeSourceFromPathIncludingUnavailable(",
                   "static bool homeLibraryDevice(", "static bool homeLibraryOn(",
                   "static bool homeLibraryMount(",
                   "static DEVICEHANDLER_INTERFACE *homeFindLibrary(",
                   "static void homeSwitchSource(",
                   "static void homeApplyStartupSource(",
                   "static bool gameflowTryOtherDevicesForLibrary("):
        if marker in swiss:
            parts.append(function(swiss, marker))
    parts += [helper, function(swiss, "static bool gameflowEnterLibraryFromHome("),
              function(swiss, "static void homePublishBrowserTransition("),
              function(swiss, "static file_handle *folderArtPicture("),
              function(swiss, "static void gameflowCopyText("),
              "static void snapshotFolder(uiGameflowCardSnapshot_t *record, file_handle *file, uiGameflowLibraryMode_t mode) {",
              "const char *relativeName=getRelativeName(file->name);", folder_record, "}",
              "static void navigate(unsigned browserButtons, bool useGameflow) { file_handle **directory=sortedDirEntries; while(1) {",
              "if(browserButtons & BUTTON_A) {", parent, "break; }", x, b, "break; } }",
              "static void dispatch(void) {", dispatch, "}",
              "static void openLibraryFromHome(void) { switch(0) {",
              library_effect.replace("case UI_HOME_EFFECT_OPEN_LIBRARY:", "case 0:", 1),
              "} }",
              "static void nextIteration(void) { while(1) { homePublishBrowserTransition(&filePanel);",
              refresh, "dispatch(); break; } }", TESTS]
    return "\n".join(parts)


STUBS = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fnmatch.h>
#include "ui_gameflow_library.h"
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif
#ifndef FNM_LEADING_DIR
#define FNM_LEADING_DIR 0
#endif
#define PATHNAME_MAX 1024
#define ATTRIB_HIDDEN 2
#define FEAT_BOOT_GCM 4
#define DEVICE_CUR 0
#define DEVICE_PREV 1
#define IS_FILE 0
#define IS_DIR 1
#define IS_SPECIAL 2
#define ON_OPTIONS 0
#define ON_FILLIST 1
#define BROWSER_CAROUSEL 2
#define BROWSER_FULLWIDTH 1
#define BUTTON_A 1
#define BUTTON_B 2
#define BUTTON_X 4
#define BUTTON_RIGHT 8
#define BUTTON_LEFT 16
#define BUTTON_START 32
#define B_SELECTED 1
#define B_NOSELECT 0
#define UI_SCENE_LIBRARY 1
#define UI_HOME_SOURCE_MOUNT_UNMOUNTED 0
#define UI_HOME_SOURCE_MOUNT_ABSENT 1
#define UI_HOME_SOURCE_MOUNT_MOUNTED 2
#define MAX_DEVICES 28
#define ISO9660_GAMECUBE_DISC 4
#define GAMECUBE_DISC 5
#define MULTIDISC_DISC 6
#define DISC_SIZE 1459978240u
typedef int uiDrawObj_t;
typedef uint8_t u8;
typedef int8_t s8;
typedef uint32_t u32;
typedef uint64_t u64;
#define BNR_PIXELDATA_LEN (96u * 32u * 2u)
typedef struct { char name[PATHNAME_MAX]; int fileType, fileAttrib; uint32_t size;
    uint64_t fileBase; void *uiObj; } file_handle;
typedef struct device { file_handle *initial; unsigned features; char **extraExtensions;
    int (*init)(file_handle *);
    int (*readDir)(file_handle *, file_handle **, unsigned);
    int (*closeFile)(file_handle *);
    void (*deinit)(file_handle *); } DEVICEHANDLER_INTERFACE;
#define DEVICE_CONFIG 2
static DEVICEHANDLER_INTERFACE device, bootDevice, __device_dvd, __device_smb, __device_ftp, __device_fsp, __device_wode, __device_usbgecko;
static file_handle fspInitial = {.name = "fsp:/", .fileType = IS_DIR};
static file_handle wodeInitial = {.name = "wode:/", .fileType = IS_DIR};
static file_handle usbInitial = {.name = "usb:/", .fileType = IS_DIR};
static DEVICEHANDLER_INTERFACE *allDevices[MAX_DEVICES] = {&bootDevice, &__device_fsp, &__device_wode, &__device_usbgecko, &device};
static DEVICEHANDLER_INTERFACE *devices[] = {&device, NULL, &bootDevice};
static file_handle initial = {.name = "sdc:/", .fileType = IS_DIR};
static file_handle bootInitial = {.name = "gcldr:/", .fileType = IS_DIR};
static file_handle discInitial = {.name = "dvd:/", .fileType = IS_DIR};
static bool deviceAvailable;
static unsigned inits;
static int initFailDevice;
static bool deviceHandler_getDeviceAvailable(DEVICEHANDLER_INTERFACE *d) {
    return d == &bootDevice || (d == &device && deviceAvailable) ||
        d == &__device_fsp || d == &__device_wode || d == &__device_usbgecko;
}
static void deviceHandler_setDeviceAvailable(DEVICEHANDLER_INTERFACE *d, bool a) {
    if(d == &device) deviceAvailable = a;
}
static void deviceHandler_setStatEnabled(int enable) { (void)enable; }
static char *getDevicePath(char *path);
static DEVICEHANDLER_INTERFACE *getDeviceFromPath(char *path) {
    char *devpos = getDevicePath(path);
    if(devpos == path) return NULL;
    for(int i = 0; i < MAX_DEVICES; i++) {
        if(allDevices[i] && allDevices[i]->initial &&
           !strncmp(allDevices[i]->initial->name, path, (size_t)(devpos - path)))
            return allDevices[i];
    }
    return NULL;
}
static size_t nav_strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if(size) { size_t n = len < size - 1 ? len : size - 1; memcpy(dst, src, n); dst[n] = '\0'; }
    return len;
}
#undef strlcpy
#define strlcpy nav_strlcpy
static file_handle curDir, curFile, *curDirEntries;
static int curDirEntryCount, sortedDirEntryCount, curSelection;
static int curMenuLocation, needsRefresh, needsDeviceChange, dvdDiscTypeInt;
/* Two storages on one connector clash; here, only a test's pair. */
static DEVICEHANDLER_INTERFACE *clashA, *clashB;
static bool filesClash(DEVICEHANDLER_INTERFACE *a, DEVICEHANDLER_INTERFACE *b, char *r, size_t n) {
    (void)r; (void)n; return (a == clashA && b == clashB) || (a == clashB && b == clashA);
}
static file_handle **sortedDirEntries;
static struct { bool libraryFolders, showHiddenFiles, hideUnknownFileTypes;
    int fileBrowserType, gameBrowserType, appsBrowserType; char flattenDir[1024];
    char libraryPath[PATHNAME_MAX];
} swissSettings;
static char *knownExtensions[] = {".iso", ".gcm", ".tgc", ".dol", NULL};
static unsigned listPublishes, libraryPublishes, homePublishes, disposals;
static uiDrawObj_t panelObject;
static uiDrawObj_t *filePanel = &panelObject;
/* The File Browser's page pointer, cleared when that page goes. */
static uiDrawObj_t *filesPage;
static bool homeLibraryEntryPending;
static bool homeFileBrowser;
static void DrawGameflowCancelPosters(void) {}
static void homeSourceRecord(DEVICEHANDLER_INTERFACE *d, int state) { (void)d; (void)state; }
static int fakeInit(file_handle *f) { ++inits; return (initFailDevice && f == &initial) ? -1 : 0; }
static void fakeDeinit(file_handle *f) { (void)f; }
static bool is_rom_name(const char *name) { (void)name; return false; }
static void print_debug(const char *fmt, ...) { (void)fmt; }
static void *checked_reallocarray(void *p, size_t n, size_t size) {
    assert(size == 0 || n <= SIZE_MAX / size); return realloc(p, n * size);
}
#define reallocarray checked_reallocarray
static size_t concat_path(char *dst, const char *dir, const char *leaf) {
    int n = snprintf(dst, PATHNAME_MAX, "%s%s%s", dir,
        dir[strlen(dir)-1] == '/' ? "" : "/", leaf);
    assert(n >= 0 && n < PATHNAME_MAX); return (size_t)n;
}
static char *getExternalPath(char *name) { return strdup(name); }
static void freeFiles(void) {
    free(curDirEntries); free(sortedDirEntries); curDirEntries = NULL;
    sortedDirEntries = NULL; curDirEntryCount = sortedDirEntryCount = 0;
}
static void lockFile(file_handle *f) { (void)f; }
static void unlockFile(file_handle *f) { (void)f; }
static unsigned padsButtonsHeld(void) { return 0; }
static void VIDEO_WaitVSync(void) {}
static __attribute__((unused)) void DrawUpdateFileBrowserButton(void *f, int state) { (void)f; (void)state; }
static void DrawDispose(uiDrawObj_t *p) { assert(p == &panelObject); ++disposals; }
static void homePublish(bool visible) {
    if(visible) { assert(filePanel == NULL); ++homePublishes; }
}
static void folderArtClose(void) {}
static void UIScene_RequestLibraryLayout(int layout) { (void)layout; }
static void UIScene_Request(int scene) { (void)scene; }
static __attribute__((unused)) int gameflowLayout(void) { return 0; }
static __attribute__((unused)) int gameflowSceneLayout(void) { return 0; }
/* The File Browser, and before it Swiss's lists (for --revision). */
static __attribute__((unused)) uiDrawObj_t *renderFileList(file_handle **d, int n, uiDrawObj_t *p) {
    (void)d; (void)n; ++listPublishes; return p;
}
static __attribute__((unused)) uiDrawObj_t *renderFileBrowser(file_handle **d, int n, uiDrawObj_t *p) {
    return renderFileList(d, n, p);
}
static __attribute__((unused)) uiDrawObj_t *renderFileFullwidth(file_handle **d, int n, uiDrawObj_t *p) {
    return renderFileList(d, n, p);
}
static __attribute__((unused)) bool gameflowListFallback, filesKeepPresses;
static unsigned otherReleases;
static __attribute__((unused)) void filesOtherRelease(void) { ++otherReleases; }
enum { UI_FILES_LEFT, UI_FILES_RIGHT };
static __attribute__((unused)) bool DrawUpdateFilesReading(uiDrawObj_t *p, int pane) { (void)p; (void)pane; return false; }
static uiDrawObj_t *renderFileCarousel(file_handle **directory, int num_files, uiDrawObj_t *p) {
    @CAROUSEL_FOCUS@
    (void)directory; assert(num_files > 0); ++libraryPublishes; return p;
}
'''

TESTS = r'''
static unsigned rootEmpty;
static bool failRead;
static bool discMultiGame;
static int fakeReadDir(file_handle *dir, file_handle **out, unsigned type) {
    (void)type;
    if(failRead) { *out=NULL; return 0; }
    static const char *rootNames[] = {"..", "Racing.v1", "Nintendo.GC",
        "Zelda..Collection [GZLE01]", "Racing.v1.png", "readme.txt",
        ".Secret", "._Racing.v1", "Flag.Hidden", "game.nkit.iso", "._game.iso"};
    const char *names[16] = {".."};
    int types[16] = {IS_SPECIAL}, attrib[16] = {0};
    int n = 1;
    if(!strcmp(dir->name, "dvd:/")) {
        /* read_fst exposes the whole retail disc as a virtual .gcm beside
         * its data files/folders. Multi-game media exposes several images. */
        n=discMultiGame ? 2 : 4;
        names[0]="Demo Game [GDEM01].gcm"; types[0]=IS_FILE;
        if(discMultiGame) { names[1]="Second Game.gcm"; types[1]=IS_FILE; }
        else {
            names[1]="data"; types[1]=IS_DIR;
            names[2]="Folder [GDIR01]"; types[2]=IS_DIR;
            names[3]="notes.txt"; types[3]=IS_FILE;
        }
    } else if(!strcmp(dir->name, "sdc:/games") && !rootEmpty) {
        n = 11;
        for(int i=0; i<n; ++i) names[i] = rootNames[i];
        for(int i=1; i<=3; ++i) types[i] = IS_DIR;
        types[4] = types[5] = types[9] = types[10] = IS_FILE;
        types[6] = types[8] = IS_DIR; types[7]=IS_FILE; attrib[8] = ATTRIB_HIDDEN;
    } else if(!strcmp(dir->name, "sdc:/games") && rootEmpty==2) {
        n=3; names[1]="notes.txt"; types[1]=IS_FILE;
        names[2]="Racing.v1.png"; types[2]=IS_FILE;
    } else if(!strcmp(dir->name, "sdc:/games/Racing.v1")) {
        n=4; names[1]="Classics.Set"; types[1]=IS_DIR;
        names[2]="Classics.Set.png"; types[2]=IS_FILE;
        names[3]="demo.iso"; types[3]=IS_FILE;
    } else if(!strcmp(dir->name, "sdc:/games/Racing.v1/Classics.Set")) {
        n=3; names[1]="Old.Saves"; types[1]=IS_DIR;
        names[2]="demo.iso"; types[2]=IS_FILE;
    } else if(!strcmp(dir->name, "sdc:/games/Racing.v1/Classics.Set/Old.Saves")) {
        n=2; names[1]="nested.game.iso"; types[1]=IS_FILE;
    } else if(!strcmp(dir->name, "sdc:/")) {
        n=3; names[1]="games"; types[1]=IS_DIR; names[2]="ipl.dol"; types[2]=IS_FILE;
    } else if(!strcmp(dir->name, "gcldr:/")) {
        n=3; names[1]="apps"; types[1]=IS_DIR; names[2]="boot.iso"; types[2]=IS_FILE;
    } else if(!strcmp(dir->name, "gcldr:/apps")) {
        n=2; names[1]="gbi.dol"; types[1]=IS_FILE;
    }
    *out = calloc((size_t)n, sizeof(**out)); assert(*out);
    for(int i=0;i<n;++i) {
        concat_path((*out)[i].name, dir->name, names[i]);
        (*out)[i].fileType=types[i]; (*out)[i].fileAttrib=attrib[i];
        (*out)[i].fileBase=(uint64_t)i+10;
    }
    if(!strcmp(dir->name,"dvd:/") && !discMultiGame) {
        (*out)[0].fileBase=0; (*out)[0].size=DISC_SIZE;
    }
    return n;
}
static void reset(const char *path, bool folders, unsigned empty) {
    freeFiles(); memset(&curFile, 0, sizeof(curFile));
    devices[DEVICE_CUR]=&device; failRead=false; discMultiGame=false;
    memset(&swissSettings, 0, sizeof(swissSettings));
    swissSettings.libraryFolders=folders; swissSettings.hideUnknownFileTypes=true;
    swissSettings.fileBrowserType=swissSettings.gameBrowserType=BROWSER_FULLWIDTH;
    strcpy(swissSettings.flattenDir, folders ? "*/games/*/*" : "*/games");
    device.initial=&initial; device.features=FEAT_BOOT_GCM; device.init=fakeInit;
    device.readDir=fakeReadDir; device.deinit=fakeDeinit;
    bootDevice.initial=&bootInitial; bootDevice.features=FEAT_BOOT_GCM;
    bootDevice.init=fakeInit; bootDevice.readDir=fakeReadDir; bootDevice.deinit=fakeDeinit;
    __device_dvd.initial=&discInitial; __device_dvd.features=FEAT_BOOT_GCM;
    __device_dvd.init=fakeInit; __device_dvd.readDir=fakeReadDir; __device_dvd.deinit=fakeDeinit;
    __device_fsp.initial=&fspInitial; __device_fsp.features=FEAT_BOOT_GCM;
    __device_fsp.init=fakeInit; __device_fsp.readDir=fakeReadDir; __device_fsp.deinit=fakeDeinit;
    __device_wode.initial=&wodeInitial; __device_wode.features=FEAT_BOOT_GCM;
    __device_wode.init=fakeInit; __device_wode.readDir=fakeReadDir; __device_wode.deinit=fakeDeinit;
    __device_usbgecko.initial=&usbInitial; __device_usbgecko.features=FEAT_BOOT_GCM;
    __device_usbgecko.init=fakeInit; __device_usbgecko.readDir=fakeReadDir; __device_usbgecko.deinit=fakeDeinit;
    deviceAvailable=false; inits=0; initFailDevice=0; devices[DEVICE_CONFIG]=&bootDevice;
    swissSettings.libraryPath[0]='\0'; clashA=clashB=NULL;
    strcpy(curDir.name,path); curDir.fileType=IS_DIR; rootEmpty=empty;
    curSelection=0; curMenuLocation=ON_FILLIST; needsRefresh=needsDeviceChange=0;
    listPublishes=libraryPublishes=homePublishes=disposals=0; filePanel=&panelObject;
    scanFiles();
}
static bool listed(const char *path) {
    for(int i=0;i<sortedDirEntryCount;++i)
        if(!strcmp(sortedDirEntries[i]->name,path)) return true;
    return false;
}
static void rootNavigation(void) {
    const unsigned buttons[] = {BUTTON_A,BUTTON_X,BUTTON_B};
    for(unsigned empty=0;empty<3;++empty) for(unsigned k=0;k<3;++k) {
        reset("sdc:/games",true,empty);
        if(empty==2) { swissSettings.hideUnknownFileTypes=false; scanFiles(); }
        curSelection=k!=0 && empty==0 ? 2 : 0;
        int selected=curSelection;
        char selectedPath[PATHNAME_MAX];
        strcpy(selectedPath,sortedDirEntries[curSelection]->name);
        bool retained=gameflowLibraryMode(sortedDirEntries,sortedDirEntryCount)!=
            UI_GAMEFLOW_LIBRARY_NONE;
        navigate(buttons[k],retained); nextIteration();
        assert(listPublishes==0); assert(homePublishes==1); assert(disposals==1);
        assert(curMenuLocation==ON_OPTIONS); assert(!strcmp(curDir.name,"sdc:/games"));
        assert(curSelection==selected);
        assert(!needsDeviceChange && !needsRefresh);
        assert(gameflowEnterLibraryFromHome()); assert(!needsRefresh);
        curMenuLocation=ON_FILLIST; nextIteration();
        assert(libraryPublishes==1 && listPublishes==0);
        if(k==0 && empty==0) {
            /* The real renderer starts past the parent when a card exists.
             * Keep this intentional first-card focus distinct from restoring
             * a game/folder after B or X. Empty roots retain their sole parent. */
            assert(curSelection==1);
            assert(!strcmp(sortedDirEntries[curSelection]->name,"sdc:/games/Nintendo.GC"));
        } else assert(!strcmp(sortedDirEntries[curSelection]->name,selectedPath));
    }
}
static void nestedNavigation(void) {
    const char *paths[]={"sdc:/games/Racing.v1",
        "sdc:/games/Racing.v1/Classics.Set", "sdc:/games/Nintendo.GC"};
    const char *parents[]={"sdc:/games","sdc:/games/Racing.v1","sdc:/games"};
    const unsigned buttons[]={BUTTON_A,BUTTON_X,BUTTON_B};
    for(unsigned p=0;p<3;++p) for(unsigned b=0;b<3;++b) {
        reset(paths[p],true,false);
        assert(gameflowLibraryMode(sortedDirEntries,sortedDirEntryCount)!=
            UI_GAMEFLOW_LIBRARY_NONE);
        navigate(buttons[b],true);
        assert(curMenuLocation==ON_FILLIST && needsRefresh && !needsDeviceChange);
        assert(!strcmp(curDir.name,parents[p])); assert(!strcmp(curFile.name,paths[p]));
        nextIteration(); assert(listPublishes==0 && libraryPublishes==1);
        assert(homePublishes==0 && disposals==0);
        assert(!strcmp(sortedDirEntries[curSelection]->name,paths[p]));
    }
}
static void dottedFilter(void) {
    reset("sdc:/games",true,false);
    assert(listed("sdc:/games/Racing.v1") && listed("sdc:/games/Nintendo.GC"));
    assert(!listed("sdc:/games/.Secret") && !listed("sdc:/games/._Racing.v1"));
    assert(!listed("sdc:/games/Flag.Hidden") && !listed("sdc:/games/readme.txt"));
    assert(!listed("sdc:/games/Racing.v1.png") && !listed("sdc:/games/._game.iso"));
    int count=gameflowLibraryEntries(sortedDirEntries,sortedDirEntryCount);
    assert(count==5 && sortedDirEntries[0]->fileType==IS_SPECIAL);
    for(int i=1;i<count;++i) if(!strcmp(sortedDirEntries[i]->name,"sdc:/games/Racing.v1")) {
        file_handle *picture=folderArtPicture(i); assert(picture);
        assert(!strcmp(picture->name,"sdc:/games/Racing.v1.png"));
        uiGameflowCardSnapshot_t record={0};
        snapshotFolder(&record,sortedDirEntries[i],UI_GAMEFLOW_LIBRARY_NONE);
        assert(record.subfolder && !strcmp(record.title,"Racing.v1"));
    }
    assert(UIGameflowLibrary_ParseGameFolderName("Zelda..Collection [GZLE01]",NULL,NULL,0));
    assert(UIGameflowLibrary_IsFolderPicture("sdc:/games/Nintendo.GC",
        "sdc:/games/Nintendo.GC.png"));
    assert(!UIGameflowLibrary_IsFolderPicture("sdc:/games/Nintendo.GC",
        "sdc:/games/Nintendo.png"));
    reset("sdc:/games/Racing.v1/Classics.Set",true,false);
    char heading[96];
    assert(UIGameflowLibrary_FolderHeading("sdc:/games",curDir.name,heading,sizeof(heading)));
    assert(!strcmp(heading,"RACING.V1 / CLASSICS.SET"));
    /* Actual scanFiles flattens the last visible level, preserving its full
     * dotted ancestry and deeper game path while omitting Old.Saves as a card. */
    assert(listed("sdc:/games/Racing.v1/Classics.Set/Old.Saves/nested.game.iso"));
    assert(!listed("sdc:/games/Racing.v1/Classics.Set/Old.Saves"));
    dispatch(); assert(libraryPublishes==1 && !listPublishes);
    reset("sdc:/games",true,false); swissSettings.showHiddenFiles=true; scanFiles();
    assert(listed("sdc:/games/.Secret") && listed("sdc:/games/Flag.Hidden"));
    count=gameflowLibraryEntries(sortedDirEntries,sortedDirEntryCount);
    for(int i=0;i<count;++i) assert(strcmp(sortedDirEntries[i]->name,"sdc:/games/._game.iso"));
}
static void legacyAndEmptyPolicy(void) {
    reset("sdc:/games/Nintendo.GC",false,false);
    dispatch(); assert(listPublishes==1 && libraryPublishes==0);
    navigate(BUTTON_X,false); nextIteration(); assert(!strcmp(curDir.name,"sdc:/games"));
    reset("sdc:/games",false,true); dispatch(); assert(listPublishes==1);
    reset("sdc:/games",false,false); dispatch(); assert(libraryPublishes==1);
    /* System > File Browser: the File Browser where the Library would show,
     * and the Library again once it is over. */
    reset("sdc:/games",false,false); homeFileBrowser=true; dispatch();
    assert(listPublishes==1 && !libraryPublishes);
    assert(gameflowLibraryMode(sortedDirEntries,sortedDirEntryCount)==UI_GAMEFLOW_LIBRARY_NONE);
    homeFileBrowser=false; dispatch(); assert(listPublishes==1 && libraryPublishes==1);
    reset("sdc:/games",false,false); dispatch(); assert(libraryPublishes==1);
    curSelection=0; /* Explicitly select the parent before activating it. */
    navigate(BUTTON_A,true); nextIteration(); assert(homePublishes==1 && !listPublishes);
    reset("sdc:/other.v1",true,false); dispatch(); assert(listPublishes==1);
    reset("sdc:/games",true,true); device.features=0; dispatch(); assert(listPublishes==1);
    assert(gameflowLibraryMode(NULL,0)==UI_GAMEFLOW_LIBRARY_NONE);
    assert(gameflowLibraryMode(sortedDirEntries,0)==UI_GAMEFLOW_LIBRARY_NONE);
    reset("sdc:/games",true,true); failRead=true; needsRefresh=1; otherReleases=0;
    nextIteration();
    assert(devices[DEVICE_CUR]==NULL && needsDeviceChange);
    assert(!listPublishes && !libraryPublishes);
    /* The right pane's listing goes before the Source does. */
    assert(otherReleases==1);
}
static void listFallback(void) {
    /* The Library couldn't draw here: the File Browser shows the folder,
     * and the Library takes it again once the flag is cleared. */
    reset("sdc:/games",false,false); gameflowListFallback=true; dispatch();
    assert(listPublishes==1 && !libraryPublishes);
    gameflowListFallback=false; otherReleases=0; dispatch();
    assert(listPublishes==1 && libraryPublishes==1);
    /* Going to the Library lets the right pane's listing go. */
    assert(otherReleases==1);
}
static void crossDeviceLibraryFallback(void) {
    /* 1. Library on gcldr:/, which has no /games: sdc:/games becomes the
     *    Source and the Library opens on it. Nothing asks, nothing is saved. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    assert(gameflowEnterLibraryFromHome());
    assert(devices[DEVICE_CUR]==&device);
    assert(devices[DEVICE_PREV]==&bootDevice);
    assert(!strcmp(curDir.name,"sdc:/games"));
    assert(needsRefresh==1 && swissSettings.libraryPath[0]=='\0');
    curMenuLocation=ON_FILLIST; nextIteration();
    assert(libraryPublishes==1 && listPublishes==0);

    /* 2. The network adapter, WODE and USB Gecko are never searched, and a
     *    storage on the Source's own connector isn't mounted beside it. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    clashA=&device; clashB=&bootDevice;
    assert(!gameflowEnterLibraryFromHome());
    assert(devices[DEVICE_CUR]==&bootDevice && inits==0);

    /* 3. A Library Path names the folder the Library opens, on its device. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    strcpy(swissSettings.libraryPath,"sdc:/games/Racing.v1");
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&device);
    memcpy(&curDir, devices[DEVICE_CUR]->initial, sizeof(file_handle));
    scanFiles();
    assert(gameflowEnterLibraryFromHome());
    assert(!strcmp(curDir.name,"sdc:/games/Racing.v1"));

    /* 4. Startup on a Library Path's device: it becomes the Source before
     *    the root is read, and the Source picker isn't needed. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    needsDeviceChange=1;
    strcpy(swissSettings.libraryPath,"sdc:/");
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&device && devices[DEVICE_PREV]==&bootDevice);
    assert(deviceAvailable && needsDeviceChange==0);

    /* 5. Auto: the boot device has no /games, so startup finds sdc:/games. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    needsDeviceChange=1;
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&device && devices[DEVICE_PREV]==&bootDevice);
    assert(needsDeviceChange==0 && swissSettings.libraryPath[0]=='\0');

    /* 6. Auto with /games on the boot device: it stays, nothing else mounts. */
    reset("sdc:/",true,false);
    devices[DEVICE_CUR]=&device;
    deviceAvailable=true;
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&device && inits==0);

    /* 6b. Started from the disc drive: it isn't read, and nothing mounts. */
    reset("dvd:/",true,false);
    devices[DEVICE_CUR]=&__device_dvd;
    deviceAvailable=true;
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&__device_dvd && inits==0);

    /* 7. The Library Path's card fails to mount (taken out at startup):
     *    gcldr:/ stays the Source, and Library finds nothing else. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    strcpy(swissSettings.libraryPath,"sdc:/");
    initFailDevice=1;
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&bootDevice);
    assert(!gameflowEnterLibraryFromHome());
    assert(devices[DEVICE_CUR]==&bootDevice);

    /* 8. Its card not there at all: gcldr:/ stays, and nothing is mounted. */
    reset("gcldr:/",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=false;
    strcpy(swissSettings.libraryPath,"sdc:/");
    homeApplyStartupSource();
    assert(devices[DEVICE_CUR]==&bootDevice && inits==0);

    /* 9. From a subfolder of the boot device (gcldr:/apps after Apps):
     *    Library reads gcldr:/ first, then finds sdc:/games and opens it. */
    reset("gcldr:/apps",true,false);
    devices[DEVICE_CUR]=&bootDevice;
    deviceAvailable=true;
    assert(!gameflowEnterLibraryFromHome());
    assert(devices[DEVICE_CUR]==&bootDevice);
    memcpy(&curDir, devices[DEVICE_CUR]->initial, sizeof(file_handle));
    curSelection=0; needsRefresh=1; homeLibraryEntryPending=true; curMenuLocation=ON_FILLIST;
    nextIteration();
    assert(devices[DEVICE_CUR]==&device && !strcmp(curDir.name,"sdc:/games"));
    assert(libraryPublishes==1 && listPublishes==0);
}
static void discLibrary(void) {
    for(unsigned folders=0;folders<2;++folders)
    for(unsigned multi=0;multi<2;++multi)
    for(unsigned sd=0;sd<2;++sd) {
        reset("dvd:/",folders,false);
        devices[DEVICE_CUR]=&__device_dvd; deviceAvailable=sd;
        discMultiGame=multi; dvdDiscTypeInt=multi ? 0 : GAMECUBE_DISC;
        scanFiles();
        /* A usable SD library must not steal an explicitly selected disc. */
        openLibraryFromHome();
        assert(curMenuLocation==ON_FILLIST);
        assert(devices[DEVICE_CUR]==&__device_dvd);
        assert(!strcmp(curDir.name,"dvd:/") && !needsRefresh && !inits);
        assert(gameflowLibraryMode(sortedDirEntries,sortedDirEntryCount)==
            UI_GAMEFLOW_LIBRARY_IMAGE_FILES);
        int count=gameflowLibraryEntries(sortedDirEntries,sortedDirEntryCount);
        assert(count==(multi ? 2 : 1));
        assert(!strcmp(sortedDirEntries[0]->name,"dvd:/Demo Game [GDEM01].gcm"));
        dispatch(); assert(libraryPublishes==1 && !listPublishes);
        /* Home and re-entry keep the disc and the selected game. */
        curSelection=multi ? 1 : 0;
        navigate(BUTTON_B,true); nextIteration();
        assert(curMenuLocation==ON_OPTIONS && !needsRefresh);
        assert(gameflowEnterLibraryFromHome());
        assert(devices[DEVICE_CUR]==&__device_dvd && curSelection==(multi ? 1 : 0));
        /* The explicit File Browser still lists the disc's data. */
        homeFileBrowser=true;
        assert(gameflowLibraryMode(sortedDirEntries,sortedDirEntryCount)==UI_GAMEFLOW_LIBRARY_NONE);
        curMenuLocation=ON_FILLIST; dispatch(); assert(listPublishes==1);
        homeFileBrowser=false;
    }
    /* Games from a browsed disc subdirectory returns to its root, even with
     * Library Folders on or a saved path naming a directory on the disc. */
    reset("dvd:/data",true,false);
    devices[DEVICE_CUR]=&__device_dvd; deviceAvailable=true;
    strcpy(swissSettings.libraryPath,"dvd:/data");
    scanFiles();
    openLibraryFromHome();
    assert(devices[DEVICE_CUR]==&__device_dvd && !strcmp(curDir.name,"dvd:/"));
    assert(needsRefresh && !inits);
    nextIteration(); assert(libraryPublishes==1 && !listPublishes);
    /* An unreadable disc follows the existing failure path to the picker. */
    reset("dvd:/",true,false); devices[DEVICE_CUR]=&__device_dvd;
    failRead=true; needsRefresh=1;
    nextIteration();
    assert(devices[DEVICE_CUR]==NULL && needsDeviceChange);
    assert(!libraryPublishes && !listPublishes);
}
int main(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"dots")) dottedFilter();
    else if(argc==2 && !strcmp(argv[1],"disc")) discLibrary();
    else { rootNavigation(); nestedNavigation(); dottedFilter(); legacyAndEmptyPolicy();
        listFallback(); crossDeviceLibraryFallback(); discLibrary(); }
    freeFiles();
    puts("real folder filter/navigation/dispatch: PASS"); return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--revision", help="replay source from a git revision")
    parser.add_argument("--case", choices=("all", "dots", "disc"), default="all")
    args = parser.parse_args()
    def read(path):
        if args.revision:
            return subprocess.check_output(["git", "show", f"{args.revision}:{path}"],
                                           cwd=ROOT, text=True)
        return (ROOT / path).read_text()
    with tempfile.TemporaryDirectory(prefix="indigo-folder-navigation-") as tmp:
        tmp = Path(tmp)
        source = tmp / "navigation.c"
        source.write_text(build_source(read))
        library = tmp / "ui_gameflow_library.c"
        library.write_text(read("cube/swiss/source/gui/ui_gameflow_library.c"))
        cmd = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-Wno-implicit-fallthrough", "-I", str(ROOT / "cube/swiss/source/gui")]
        if args.sanitize:
            cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                cmd += ["-fno-pie", "-no-pie"]
        subprocess.run(cmd + [str(source), str(library), "-o", str(tmp / "navigation")], check=True)
        subprocess.run([str(tmp / "navigation"), args.case], check=True)


if __name__ == "__main__":
    main()
