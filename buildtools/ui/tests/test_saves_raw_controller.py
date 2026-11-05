#!/usr/bin/env python3
"""Exercise actual RAW wrappers and Memory Cards controller routes on host.

The SD model accepts only physical device paths, so a synthetic GCI reaching a
handler fails immediately. Actual controller functions load/reload RAW places,
read virtual headers and artwork ranges, export verified GCI bytes, move and
erase saves in and out of images through the image writer (modelled here with
the real planner) only after a verified copy, keep a read-only image's actions
dimmed, and route the two independent SD columns, each on either SD device
(the settings device and a Source that is another FatFs device): every
device call goes to the device whose path it names. No device write or
delete may name an image. --source-root supports reviewing an uncommitted
integration worktree.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui_saves.h"
#include "ui_saves_raw.h"
#include "ui_save_cubes.h"
#include "ui_saves_metadata.h"
#include "ui_saves_details.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#define PATHNAME_MAX 1024
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
#define SAVES_LIST_MAX 256
#define SAVES_TABS 4
#define SAVES_TAB_FOLDER 2
#define SAVES_SLOTS 48
#define SAVES_SYSTEM_BLOCKS 5
#define SAVES_MAX_BYTES (0x150u + 2043u * UI_SAVES_BLOCK_SIZE)
#define IS_FILE 0
#define IS_DIR 1
#define IS_SPECIAL 2
#define FEAT_WRITE 1
#define DEVICE_HANDLER_SEEK_SET 0
#define CARD_FILENAMELEN 32
#define CARD_ATTRIB_NOCOPY 8
#define CARD_ATTRIB_NOMOVE 16
#define BUTTON_L 1
#define BUTTON_R 2
#define BUTTON_A 4
#define BUTTON_B 8
#define BUTTON_UP 16
#define BUTTON_DOWN 32
#define D_WARN 1
#define D_FAIL 2
#define ALIGN_LEFT 0
#define CARD_ERROR_READY 0
#define DCFlushRange(p,n) ((void)0)
typedef struct {
    s32 chn, fileno;
    u32 filelen;
    u8 permissions;
    char filename[32];
    u8 gamecode[4], company[2];
    bool showall;
} card_dir;
typedef struct file_handle file_handle;
typedef struct {
    file_handle *initial;
    u32 features;
    s32 (*readDir)(file_handle *, file_handle **, u32);
    s32 (*readFile)(file_handle *, void *, u32);
    s32 (*writeFile)(file_handle *, const void *, u32);
    s32 (*deleteFile)(file_handle *);
    s32 (*closeFile)(file_handle *);
    long long (*seekFile)(file_handle *, long long, u32);
    s32 (*statFile)(file_handle *);
} DEVICEHANDLER_INTERFACE;
struct file_handle {
    char name[PATHNAME_MAX];
    u32 size, offset;
    u8 other[128];
    int fileType;
    DEVICEHANDLER_INTERFACE *device;
};
/* Two FatFs devices with the same handlers, as on a console: the settings
 * device (sda:) and a Source on another card (sdc:). No devices[] here, so
 * an extracted function that reaches for the settings device won't build. */
static DEVICEHANDLER_INTERFACE __device_card_a, __device_card_b, sd, sd2;
static file_handle sdRoot = {.name = "sda:/", .fileType = IS_DIR};
static file_handle sdRoot2 = {.name = "sdc:/", .fileType = IS_DIR};
static DEVICEHANDLER_INTERFACE *sdDevice[2];
static int sdDevices;
static char folderHome[2][PATHNAME_MAX];
static DEVICEHANDLER_INTERFACE *owner(const char *name) {return strncmp(name,"sdc:",4) ? &sd : &sd2;}
static const char *DeviceDisplayName(const DEVICEHANDLER_INTERFACE *device) {
    return device==&sd ? "GC Loader" : device==&sd2 ? "SD Card - SD2SP2" : "?";
}
static const char *saves_folder(void) {return "swiss/saves";}
static bool foldersReady = true, artFresh;
static int wantCount, screenFocus, screenStacks[2] = {2,3};
static u32 listings, placeIds[SAVES_TABS][SAVES_LIST_MAX];
static int placeIdCount[SAVES_TABS];
static int reads, writes, deletes, closes, cardCopies, starts, ends, tells, says;
static bool expectInvalidMessage, invalidMessageSeen;
static bool lastResult, copyMode;
static struct {
    s8 storageStack;
    struct {char title[48],item[4][40];u8 count,dim;u16 width;} menu;
    u8 menuOpen,menuFocus,ghost;
    u16 menuSerial;
    s16 ghostCell;
    char reason[96];
} over;
static struct {int cell,slot,toCell;} plan;
static int storageChoice=-1;
static int savesMenu(const char *title,const char *const *items,int count,int initial,
    unsigned dim,const char *const *reasons,unsigned ghosts);
typedef int savesInput_t;
static u32 inputs[32];
static unsigned inputCount,inputAt,menusShown,detailsShown;
static unsigned seenDim,menuSeen;
static int menuOpenedOn=-1;
static uiSaveDetailsSnapshot_t detailSnapshot;
static void inputInit(savesInput_t *input) {*input=0;}
static unsigned downs;
static u32 inputNext(savesInput_t *input) {
    (void)input;
    if(inputAt<inputCount) return inputs[inputAt++];
    if(storageChoice>=0 && over.menuFocus!=storageChoice && ++downs<=8) return BUTTON_DOWN;
    /* Pressed, or an item the menu doesn't have: B, not DOWN for ever. */
    u32 pressed=storageChoice>=0 && over.menuFocus==storageChoice ? BUTTON_A : BUTTON_B;
    downs=0;return pressed;
}
static void script(const u32 *presses,unsigned count) {
    assert(count<=32);memcpy(inputs,presses,count*sizeof(*presses));inputCount=count;inputAt=0;
}
static void menuaudio_select(void) {}
static void menuaudio_blip(void) {}
static int GetTextSizeInPixels(const char *text) {return (int)strlen(text)*8;}
static void UICheats_Fit(char *out,size_t size,const char *text,int width,float scale,
    int (*measure)(const char *)) {
    (void)width;(void)scale;(void)measure;snprintf(out,size,"%s",text);
}
static void screenRedraw(void) {
    if(over.menuOpen) {menusShown++;seenDim=over.menu.dim;assert(over.menuSerial);}
    if(over.menuOpen && over.menuSerial!=menuSeen) {menuSeen=over.menuSerial;menuOpenedOn=over.menuFocus;}
}
typedef struct {int unused;} uiDrawObj_t;
static uiDrawObj_t *DrawSaveDetails(const uiSaveDetailsSnapshot_t *snapshot) {
    assert(UISaveDetails_Valid(snapshot));detailSnapshot=*snapshot;detailsShown++;
    return calloc(1,sizeof(uiDrawObj_t));
}
static uiDrawObj_t *DrawPublish(uiDrawObj_t *box) {assert(box);return box;}
static void DrawDispose(uiDrawObj_t *box) {assert(box);free(box);}
typedef struct {char gamecode[4],company[2],filename[32];u32 time,len,icon_addr,comment_addr;u16 icon_fmt,icon_speed;u8 banner_fmt;} card_stat;
static struct {bool disableUIAnimations;} swissSettings;
static card_stat status;
static int statusResult,statusReads,expectedStatusChannel;
static int CARD_GetStatus(int channel,int file,card_stat *out) {
    assert(channel==expectedStatusChannel && file==7);statusReads++;*out=status;return statusResult;
}
static void eraseBegin(int cell) {(void)cell;starts++;}
static void artReturn(void) {}
static int failReadAt = -1;
static char chosen[PATHNAME_MAX], told[160];
static void *memalign(size_t alignment, size_t size) {(void)alignment; return malloc(size);}
static char *getRelativeName(const char *path) {
    const char *slash = strrchr(path, '/'); return (char *)(slash ? slash+1 : path);
}
static char *getDevicePath(const char *path) {
    const char *colon = strchr(path, ':'); assert(colon); return (char *)(colon+1);
}
static void concat_path(char *out, const char *root, const char *leaf) {
    snprintf(out, PATHNAME_MAX, "%s%s%s", root,
        root[strlen(root)-1]=='/' ? "" : "/", leaf[0]=='/' ? leaf+1 : leaf);
}
static void setCopyGCIMode(bool enabled) {copyMode=enabled;}
static void savesTell(int kind, const char *why) {(void)kind; tells++; snprintf(told,sizeof(told),"%s",why);}
static void savesSay(const char *why);
static void artRoom(u32 length) {(void)length;}
static DEVICEHANDLER_INTERFACE *ensured;
static void folderEnsure(DEVICEHANDLER_INTERFACE *device, const char *path) {(void)path;ensured=device;}
static void opBegin(bool move, const char *folder) {(void)move;(void)folder; starts++;}
static void placesReload(void);
static void opEnd(bool ok) {ends++; lastResult=ok; placesReload();}
static bool cardWrite(int slot, const u8 *entry, const u8 *blocks, u32 size,
    char *why, size_t capacity) {
    (void)why;(void)capacity;
    assert(slot==0 || slot==1); assert(!memcmp(entry,"GALP01",6));
    assert(size==2*8192 && blocks[0]=='A' && blocks[8192]=='B');
    cardCopies++; return true;
}
static void loadCard(struct savesPlace *unused, int slot);
'''

MODEL = r'''
#define IMAGE_BYTES (64u*8192u)
static u8 imageA[IMAGE_BYTES], imageB[IMAGE_BYTES];
static u8 frozenA[IMAGE_BYTES], frozenB[IMAGE_BYTES];
static struct {char name[PATHNAME_MAX]; u8 bytes[2*8192+64]; u32 size;} exports[8];
static int exportCount;
static void put16(u8 *p,unsigned value) {p[0]=(u8)(value>>8);p[1]=(u8)value;}
static void seal(u8 *bytes,size_t length,u8 *out) {
    unsigned sum=0,inverse=0;
    for(size_t at=0;at<length;at+=2) {
        unsigned value=((unsigned)bytes[at]<<8)|bytes[at+1];
        sum=(sum+value)&65535;inverse=(inverse+(value^65535))&65535;
    }
    put16(out,sum==65535?0:sum);put16(out+2,inverse==65535?0:inverse);
}
static void fixture(u8 *image,const char *game,const char *maker,char first,char second) {
    memset(image,0,IMAGE_BYTES);
    put16(image+0x22,4);seal(image,0x1fc,image+0x1fc);
    for(unsigned copy=0;copy<2;copy++) {
        u8 *directory=image+(1+copy)*8192;
        memset(directory,255,8192);memset(directory,0,64);
        memcpy(directory,game,4);memcpy(directory+4,maker,2);
        memcpy(directory+8,"raw-save",8);put16(directory+0x30,2);put16(directory+0x32,1);
        put16(directory+0x36,5);put16(directory+0x38,2);directory[0x3e]=0x10;
        put16(directory+0x1ffa,0);seal(directory,8188,directory+8188);
        u8 *map=image+(3+copy)*8192;
        put16(map+6,57);put16(map+10,7);put16(map+14,65535);
        seal(map+4,8188,map);
    }
    memset(image+5*8192,first,8192);memset(image+7*8192,second,8192);
}
/* The Source's card image, where Swiss's Emulate Memory Card keeps it. */
#define SOURCE_IMAGE "sdc:/swiss/saves/MemoryCardA.USA.raw"
static bool sourceSavesMissing;
static const u8 *physical(file_handle *file,u32 *size) {
    assert(strchr(file->name,':') && "synthetic save reached physical device");
    assert(file->device==owner(file->name) && "a device call names another device's path");
    if(!strcmp(file->name,"sda:/images/A.raw")) {*size=IMAGE_BYTES;return imageA;}
    if(!strcmp(file->name,"sda:/images/B.raw") || !strcmp(file->name,SOURCE_IMAGE)) {*size=IMAGE_BYTES;return imageB;}
    for(int i=0;i<exportCount;i++) if(!strcmp(file->name,exports[i].name)) {
        *size=exports[i].size;return exports[i].bytes;
    }
    *size=0;return NULL;
}
static long long sdSeek(file_handle *file,long long offset,u32 type) {
    assert(strchr(file->name,':') && file->device==owner(file->name));
    assert(type==0 && offset>=0);file->offset=(u32)offset;return offset;
}
static s32 sdRead(file_handle *file,void *out,u32 size) {
    u32 available;const u8 *data=physical(file,&available);reads++;
    if(failReadAt==reads || !data || file->offset>available || size>available-file->offset) return -1;
    memcpy(out,data+file->offset,size);file->offset+=size;return (s32)size;
}
static s32 sdClose(file_handle *file) {assert(strchr(file->name,':') && file->device==owner(file->name));closes++;return 0;}
static s32 sdStat(file_handle *file) {u32 size;return physical(file,&size) ? 0 : -1;}
static bool failSdWrite;
static s32 sdWrite(file_handle *file,const void *data,u32 size) {
    assert(strchr(file->name,':') && !SavesRaw_IsImageName(file->name));
    assert(file->device==owner(file->name) && (file->device->features&FEAT_WRITE));
    assert(exportCount<8 && size<=sizeof(exports[0].bytes));
    if(failSdWrite) return -1;
    writes++;snprintf(exports[exportCount].name,PATHNAME_MAX,"%s",file->name);
    memcpy(exports[exportCount].bytes,data,size);exports[exportCount].size=size;exportCount++;
    return (s32)size;
}
static s32 sdDelete(file_handle *file) {
    assert(strchr(file->name,':') && !SavesRaw_IsImageName(file->name));
    assert(file->device==owner(file->name));deletes++;return 0;
}
static s32 sdDir(file_handle *directory,file_handle **out,u32 type) {
    (void)type;int count=0;
    assert(directory->device==owner(directory->name));
    if(sourceSavesMissing && !strcmp(directory->name,"sdc:/swiss/saves")) {*out=NULL;return -1;}
    *out=calloc(10,sizeof(**out));assert(*out);
    if(!strcmp(directory->name,"sdc:/swiss/saves")) {
        file_handle *file=&(*out)[count++];snprintf(file->name,PATHNAME_MAX,"%s",SOURCE_IMAGE);
        file->size=IMAGE_BYTES;file->device=&sd2;file->fileType=IS_FILE;
    }
    if(!strcmp(directory->name,"sda:/images")) {
        for(int i=0;i<2;i++) {
            file_handle *file=&(*out)[count++];
            snprintf(file->name,PATHNAME_MAX,"sda:/images/%c.raw",'A'+i);
            file->size=IMAGE_BYTES;file->device=&sd;file->fileType=IS_FILE;
        }
    }
    for(int i=0;i<exportCount;i++) {
        const char *slash=strrchr(exports[i].name,'/');
        if((size_t)(slash-exports[i].name)!=strlen(directory->name) ||
            strncmp(exports[i].name,directory->name,strlen(directory->name))) continue;
        file_handle *file=&(*out)[count++];snprintf(file->name,PATHNAME_MAX,"%s",exports[i].name);
        file->size=exports[i].size;file->device=owner(file->name);file->fileType=IS_FILE;
    }
    return count;
}
static void loadCard(savesPlace_t *place,int slot) {(void)slot;placeClear(place);place->ready=true;}
/* saves_raw.c's writer, modelled with the real planner on the images: the
 * planned system blocks and the save's blocks land, nothing else. */
static bool rawWritable=true;
static int rawAdds,rawErases;
static u8 *imageBytes(const file_handle *image) {
    assert(image->device==owner(image->name));
    if(!strcmp(image->name,"sda:/images/A.raw")) return imageA;
    if(!strcmp(image->name,"sda:/images/B.raw") || !strcmp(image->name,SOURCE_IMAGE)) return imageB;
    assert(!"an image the model doesn't have");return NULL;
}
static bool SavesRaw_Writable(const file_handle *image) {
    return rawWritable && image->device==owner(image->name) &&
        (image->device->features&FEAT_WRITE) && image->fileType==IS_FILE;
}
/* Another folder: the chooser opens on the other stack's device, at its
 * folder, and answers with a path under that device's root. */
static bool chooseFolder(DEVICEHANDLER_INTERFACE *device,const char *start,char *out,size_t size) {
    savesPlace_t *dest=&places[screenStacks[!screenFocus]];
    assert(device==dest->device && !strcmp(start,getDevicePath(dest->dir.name)+1));
    if(!chosen[0]) return false;
    snprintf(out,size,"%s",chosen); return true;
}
static u8 rawMetadata[UI_SAVES_RAW_METADATA_SIZE];
static uiSavesRawCard_t rawPlanned;
static uiSavesRawStatus_t SavesRaw_Add(file_handle *image,const uint8_t *gci,uint32_t length) {
    uiSavesRawChange_t change;u8 *bytes;uiSavesRawStatus_t status;unsigned at,blocks;
    if(!SavesRaw_Writable(image)) return UI_SAVES_RAW_READ_ONLY;
    bytes=imageBytes(image);blocks=UISaves_Blocks(gci);assert(length==64+blocks*8192u);
    memcpy(rawMetadata,bytes,sizeof(rawMetadata));
    status=UISavesRaw_PlanAdd(rawMetadata,IMAGE_BYTES,gci,&rawPlanned,&change);
    if(status!=UI_SAVES_RAW_OK) return status;
    at=change.first;
    for(unsigned i=0;i<blocks;i++,at=rawPlanned.next[at]) memcpy(bytes+at*8192u,gci+64+i*8192u,8192);
    memcpy(bytes,rawMetadata,sizeof(rawMetadata));rawAdds++;
    return UI_SAVES_RAW_OK;
}
static uiSavesRawStatus_t SavesRaw_Erase(file_handle *image,const uint8_t entry[UI_SAVES_ENTRY_SIZE]) {
    uiSavesRawChange_t change;u8 *bytes;uiSavesRawStatus_t status;
    if(!SavesRaw_Writable(image)) return UI_SAVES_RAW_READ_ONLY;
    bytes=imageBytes(image);memcpy(rawMetadata,bytes,sizeof(rawMetadata));
    status=UISavesRaw_PlanErase(rawMetadata,IMAGE_BYTES,entry,&rawPlanned,&change);
    if(status!=UI_SAVES_RAW_OK) return status;
    memcpy(bytes,rawMetadata,sizeof(rawMetadata));rawErases++;
    return UI_SAVES_RAW_OK;
}
/* An image's saves as the console would read them. */
static unsigned imageSaves(const u8 *bytes,uiSavesRawCard_t *card) {
    assert(UISavesRaw_Parse(bytes,UI_SAVES_RAW_METADATA_SIZE,IMAGE_BYTES,card)==UI_SAVES_RAW_OK);
    return card->count;
}
static void setup(void) {
    memset(places,0,sizeof(places));memset(placeIdCount,0,sizeof(placeIdCount));
    sd=(DEVICEHANDLER_INTERFACE){&sdRoot,FEAT_WRITE,sdDir,sdRead,sdWrite,sdDelete,sdClose,sdSeek,sdStat};
    sdRoot.device=&sd;sd2=sd;sd2.initial=&sdRoot2;sdRoot2.device=&sd2;
    sdDevice[0]=&sd;sdDevice[1]=NULL;sdDevices=1;sourceSavesMissing=false;ensured=NULL;
    for(int i=2;i<4;i++) {
        places[i].device=&sd;places[i].dir.device=&sd;places[i].dir.fileType=IS_DIR;
        snprintf(places[i].dir.name,PATHNAME_MAX,"sda:/%s",i==2?"left":"right");places[i].ready=true;
        snprintf(folderHome[i-2],PATHNAME_MAX,"%s",places[i].dir.name);
    }
    reads=writes=deletes=closes=cardCopies=starts=ends=tells=says=exportCount=0;
    rawWritable=true;rawAdds=rawErases=0;failSdWrite=false;
    expectInvalidMessage=invalidMessageSeen=false;
    pool=calloc(1,48*SAVES_SLOT_BYTES);assert(pool);memset(slots,0,sizeof(slots));
    screenFocus=0;screenStacks[0]=2;screenStacks[1]=3;failReadAt=-1;chosen[0]=0;
    memset(&over,0,sizeof(over));over.storageStack=-1;
    inputCount=inputAt=menusShown=detailsShown=seenDim=menuSeen=0;storageChoice=-1;menuOpenedOn=-1;
    memset(&detailSnapshot,0,sizeof(detailSnapshot));
    memset(&status,0,sizeof(status));statusResult=CARD_ERROR_READY;statusReads=0;expectedStatusChannel=0;swissSettings.disableUIAnimations=false;
    fixture(imageA,"GALP","01",'A','B');fixture(imageB,"GZLP","02",'C','D');
    memcpy(frozenA,imageA,IMAGE_BYTES);memcpy(frozenB,imageB,IMAGE_BYTES);
}
static void openImage(int tab,const char *name) {
    file_handle image={.size=IMAGE_BYTES,.fileType=IS_FILE,.device=&sd};
    snprintf(image.name,PATHNAME_MAX,"sda:/images/%s.raw",name);loadRaw(&places[tab],&image);
    assert(places[tab].rawOpen && places[tab].ready && places[tab].count==1);
}
static void unchanged(void) {assert(!memcmp(imageA,frozenA,IMAGE_BYTES));assert(!memcmp(imageB,frozenB,IMAGE_BYTES));assert(deletes==0);}
static void clean(void) {for(int i=0;i<4;i++)placeClear(&places[i]);free(pool);pool=NULL;}
static void readExport(void) {
    setup();openImage(2,"A");file_handle *virtual=places[2].list[0];
    assert(strchr(virtual->name,':')==NULL);unsigned ordinal=99;
    assert(rawSource(virtual,&ordinal)==&places[2] && ordinal==0);
    artRead(virtual,0,8172);assert(!slots[0].failed && slots[0].art.frames==1);
    assert(strspn(slots[0].line[0],"A")==32 && strspn(slots[0].line[1],"A")==32);
    for(int i=0;i<2048;i++)assert(pool[i]=='A');
    assert(slotTags[0]==8172 && !copyMode);
    int before=reads;u8 entry[64];assert(saveEntry(virtual,entry));assert(reads==before);
    assert(!memcmp(entry,"GALP01",6));
    u8 artwork[32];assert(readSaveAt(virtual,64+8192-10,artwork,sizeof(artwork)));
    for(int i=0;i<32;i++)assert(artwork[i]==(i<10?'A':'B'));
    u32 length=0;u8 *data=saveRead(virtual,&length);assert(data && length==64+2*8192);
    assert(!memcmp(data,entry,64));for(unsigned i=64;i<length;i++)assert(data[i]==(i<64+8192?'A':'B'));free(data);
    /* Copy exports it and leaves the image as it was. */
    placesRemember();saveTransfer(virtual,UI_SAVES_PLACE_FOLDER,false);
    assert(starts==1 && ends==1 && lastResult && writes==1 && exportCount==1);
    assert(exports[0].size==64+2*8192 && !memcmp(exports[0].bytes,entry,64));
    for(int i=64;i<64+2*8192;i++)assert(exports[0].bytes[i]==(i<64+8192?'A':'B'));
    assert(!strncmp(exports[0].name,"sda:/right/",11) && strstr(exports[0].name,".gci"));
    assert(places[2].rawOpen && places[2].count==1 && !strcmp(places[2].rawImage.name,"sda:/images/A.raw"));
    assert(!places[3].rawOpen && places[3].count==1);
    unchanged();
    /* Move: a copy that doesn't go in leaves the save on the image. */
    failSdWrite=true;placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,true);
    /* folderWrite takes its own partial file away (never an image). */
    assert(!lastResult && rawErases==0 && exportCount==1 && tells==1 && deletes==1);
    assert(!memcmp(imageA,frozenA,IMAGE_BYTES));deletes=0;
    /* One that does: the image lets it go, as listed, and reads again. */
    failSdWrite=false;placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,true);
    assert(lastResult && exportCount==2 && rawErases==1 && deletes==0);
    assert(strstr(exports[1].name,"_2.gci") && !memcmp(exports[1].bytes,entry,64));
    assert(places[2].rawOpen && places[2].ready && places[2].count==0 && places[2].freeBlocks==59);
    uiSavesRawCard_t card;assert(imageSaves(imageA,&card)==0 && card.freeBlocks==59);
    assert(!memcmp(imageB,frozenB,IMAGE_BYTES));clean();
    /* To a card: Copy leaves the image; Move erases it there after. */
    for(int slot=0;slot<2;slot++) {
        setup();openImage(2,"A");screenStacks[1]=slot;placesRemember();
        saveTransfer(places[2].list[0],slot ? UI_SAVES_PLACE_SLOT_B : UI_SAVES_PLACE_SLOT_A,false);
        assert(lastResult && cardCopies==1 && writes==0 && rawErases==0);
        unchanged();
        placesRemember();saveTransfer(places[2].list[0],slot ? UI_SAVES_PLACE_SLOT_B : UI_SAVES_PLACE_SLOT_A,true);
        assert(lastResult && cardCopies==2 && rawErases==1 && places[2].count==0);
        clean();
    }
    /* A save marked no-move doesn't move off an image either. */
    setup();
    for(int i=0;i<2;i++) {u8 *directory=imageA+(1+i)*8192;directory[0x34]=0x10;seal(directory,8188,directory+8188);}
    memcpy(frozenA,imageA,IMAGE_BYTES);openImage(2,"A");placesRemember();
    saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,true);
    assert(tells==1 && strstr(told,"doesn't let its save move") && starts==0 && writes==0);
    unchanged();clean();
}
static void twoColumns(void) {
    setup();openImage(2,"A");openImage(3,"B");
    u8 a[64],b[64];assert(saveEntry(places[2].list[0],a));assert(saveEntry(places[3].list[0],b));
    assert(!memcmp(a,"GALP01",6) && !memcmp(b,"GZLP02",6));
    /* The other column's image is a card: room, free blocks, its saves. */
    uiSavesRoom_t room;saveRoom(3,a,true,&room);
    assert(room.ready && room.writable && room.card && room.image && !room.hasIt);
    assert(room.saves==1 && room.freeBlocks==57 && !strcmp(room.name,"The card image"));
    saveRoom(3,b,true,&room);assert(room.hasIt);
    rawWritable=false;saveRoom(3,a,true,&room);assert(!room.writable);rawWritable=true;
    char path[PATHNAME_MAX];assert(!destinationFolder(UI_SAVES_PLACE_FOLDER,path,sizeof(path)));
    saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,false);assert(writes==0 && starts==0);
    placesRemember();placesReload();assert(places[2].rawOpen && places[3].rawOpen);
    assert(saveEntry(places[2].list[0],a) && saveEntry(places[3].list[0],b));
    assert(!memcmp(a,"GALP01",6) && !memcmp(b,"GZLP02",6));unchanged();
    /* Copy into the other image: both images read, A as it was. */
    placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_IMAGE,false);
    assert(lastResult && rawAdds==1 && rawErases==0 && writes==0 && deletes==0);
    assert(!memcmp(imageA,frozenA,IMAGE_BYTES) && places[3].count==2);
    uiSavesRawCard_t card;assert(imageSaves(imageB,&card)==2);
    unsigned at=UISavesRaw_Find(&card,a);assert(at<card.count);
    u8 copied[64+2*8192];
    for(unsigned i=0,block=card.entry[at][0x36]*256u+card.entry[at][0x37];i<2;i++,block=card.next[block])
        memcpy(copied+64+i*8192,imageB+block*8192,8192);
    for(int i=0;i<2*8192;i++)assert(copied[64+i]==(i<8192?'A':'B'));
    /* Again: it's there, so nothing is written. */
    placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_IMAGE,false);
    assert(!lastResult && rawAdds==1 && tells==1 && strstr(told,"already has this save"));
    /* Move the other way: from B into A, then off B. */
    screenFocus=1;places[3].selection=0;placesRemember();
    file_handle *moving=NULL;
    for(int i=0;i<places[3].count;i++) {u8 e[64];assert(saveEntry(places[3].list[i],e));if(!memcmp(e,"GZLP02",6))moving=places[3].list[i];}
    assert(moving);saveTransfer(moving,UI_SAVES_PLACE_IMAGE,true);
    assert(lastResult && rawAdds==2 && rawErases==1);
    assert(imageSaves(imageA,&card)==2 && UISavesRaw_Find(&card,b)<card.count);
    assert(imageSaves(imageB,&card)==1 && UISavesRaw_Find(&card,b)==card.count);
    assert(places[2].count==2 && places[3].count==1 && deletes==0 && writes==0);
    clean();
    setup();screenFocus=1;openImage(3,"A");placesRemember();
    saveTransfer(places[3].list[0],UI_SAVES_PLACE_FOLDER,false);
    assert(lastResult && writes==1 && !strncmp(exports[0].name,"sda:/left/",10));
    unchanged();clean();
}
static void destinationsAndFailures(void) {
    setup();char path[PATHNAME_MAX];
    assert(destinationFolder(UI_SAVES_PLACE_FOLDER,path,sizeof(path)) && !strcmp(path,"sda:/right"));
    snprintf(chosen,sizeof(chosen),"chosen-right");
    assert(destinationFolder(UI_SAVES_PLACE_CHOOSE,path,sizeof(path)) && !strcmp(path,"sda:/chosen-right"));
    sd.features=0;assert(!destinationFolder(UI_SAVES_PLACE_FOLDER,path,sizeof(path)));
    assert(!destinationFolder(UI_SAVES_PLACE_CHOOSE,path,sizeof(path)));sd.features=FEAT_WRITE;
    screenFocus=1;snprintf(chosen,sizeof(chosen),"chosen-left");
    assert(destinationFolder(UI_SAVES_PLACE_CHOOSE,path,sizeof(path)) && !strcmp(path,"sda:/chosen-left"));
    file_handle raw={.device=&sd,.fileType=IS_FILE};strcpy(raw.name,"sda:/images/A.raw");
    int before=closes;assert(!saveDelete(&raw) && closes==before && deletes==0);
    screenFocus=0;openImage(2,"A");placesRemember();
    failReadAt=reads+1;saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,false);
    assert(!lastResult && writes==0 && deletes==0);unchanged();
    /* An image its device can't change: no erase, a Move's copy stays a copy. */
    failReadAt=-1;rawWritable=false;tells=0;
    assert(!saveDelete(places[2].list[0]) && rawErases==0);
    placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,true);
    assert(writes==1 && rawErases==0 && tells==1 && strstr(told,"couldn't be removed"));
    assert(!memcmp(imageA,frozenA,IMAGE_BYTES));clean();
}
static void openingFailures(void) {
    setup();strcpy(places[2].dir.name,"sda:/images");loadFolder(&places[2],false);
    assert(places[2].count==2);places[2].selection=1;
    memset(imageB,0,5*8192);memcpy(frozenB,imageB,IMAGE_BYTES);
    expectInvalidMessage=true;openSelected(&places[2],places[2].list[1]);
    assert(invalidMessageSeen && says==1 && screenFocus==0);
    assert(!places[2].rawOpen && places[2].ready && places[2].selection==1 && places[2].count==2);
    expectInvalidMessage=false;
    /* Two saves keep inherited folder selection 1 in range: only the
     * controller's first-open policy should select save 0. */
    for(int i=0;i<2;i++) {
        u8 *directory=imageA+(1+i)*8192;u8 *second=directory+64;
        memcpy(second,directory,64);memcpy(second,"GX2P02",6);
        memset(second+8,0,32);memcpy(second+8,"raw-two",7);
        put16(second+0x36,6);put16(second+0x38,1);seal(directory,8188,directory+8188);
        u8 *map=imageA+(3+i)*8192;put16(map+6,56);put16(map+12,65535);seal(map+4,8188,map);
    }
    memset(imageA+6*8192,'C',8192);memcpy(frozenA,imageA,IMAGE_BYTES);
    openSelected(&places[2],places[2].list[0]);
    assert(places[2].rawOpen && places[2].ready && places[2].selection==0 && places[2].rawReturn==1);
    places[2].selection=0;placesRemember();placesReload();
    assert(places[2].selection==0 && places[2].rawReturn==1);
    unchanged();clean();
}
static void emptyImage(void) {
    setup();
    for(int i=0;i<2;i++) {
        u8 *directory=imageA+(1+i)*8192;memset(directory,255,8188);
        put16(directory+0x1ffa,0);seal(directory,8188,directory+8188);
        u8 *map=imageA+(3+i)*8192;memset(map,0,8192);put16(map+6,59);seal(map+4,8188,map);
    }
    memcpy(frozenA,imageA,IMAGE_BYTES);places[2].selection=5;
    file_handle image={.size=IMAGE_BYTES,.fileType=IS_FILE,.device=&sd};strcpy(image.name,"sda:/images/A.raw");
    loadRaw(&places[2],&image);
    assert(places[2].rawOpen && places[2].ready && places[2].count==0 && places[2].freeBlocks==59);
    assert(places[2].selection==0 && places[2].rawReturn==5);
    placesRemember();placesReload();assert(places[2].rawOpen && places[2].ready && places[2].count==0);
    unchanged();clean();
}
static void storageFocus(void) {
    for(int original=0;original<2;original++) {
        setup();places[0].ready=places[1].ready=true;int stacks[2]={0,1};
        storageChoice=2;int focus=storagePress(stacks,original,original ? BUTTON_L : BUTTON_R);
        assert(focus==original && stacks[!original]==2+!original && stacks[original]==original);
        storageChoice=-1;focus=storagePress(stacks,original,original ? BUTTON_L : BUTTON_R);
        assert(focus==original && stacks[!original]==2+!original);
        /* One SD device: the menu calls it the SD card. */
        assert(over.menu.count==3 && !strcmp(over.menu.item[2],"SD card"));
        unchanged();clean();
    }
}
/* Settings on one card (sda:), games on another (sdc:), where Swiss's
 * Emulate Memory Card keeps their card images: each SD column chooses
 * either device by name, opens at its Save Folder, and lists, reads and
 * writes there only; the device's handlers see only its own paths. */
static void twoDevices(void) {
    setup();sdDevice[1]=&sd2;sdDevices=2;places[0].ready=places[1].ready=true;
    int stacks[2]={0,3};
    /* R: Slot A, Slot B and both devices by name, on the one it shows. */
    storageChoice=3;int focus=storagePress(stacks,0,BUTTON_R);
    assert(menuOpenedOn==2 && over.menu.count==4 && over.menu.dim==1u);
    assert(!strcmp(over.menu.item[2],"GC Loader") && !strcmp(over.menu.item[3],"SD Card - SD2SP2"));
    assert(focus==0 && stacks[0]==0 && stacks[1]==3 && places[3].device==&sd2);
    assert(!strcmp(places[3].dir.name,"sdc:/swiss/saves") && !strcmp(folderHome[1],"sdc:/swiss/saves"));
    assert(places[3].ready && places[3].count==1 && !strcmp(places[3].list[0]->name,SOURCE_IMAGE));
    /* Again: the menu starts on the Source, and choosing it keeps the folder. */
    storageChoice=3;storagePress(stacks,0,BUTTON_R);
    assert(menuOpenedOn==3 && places[3].device==&sd2 && !strcmp(places[3].dir.name,"sdc:/swiss/saves"));
    /* The left column on the settings device, the right in the Source's image. */
    storageChoice=2;storagePress(stacks,0,BUTTON_L);
    assert(stacks[0]==2 && places[2].device==&sd && !strcmp(places[2].dir.name,"sda:/left"));
    openSelected(&places[3],places[3].list[0]);
    assert(places[3].rawOpen && places[3].ready && places[3].count==1 && places[3].rawImage.device==&sd2);
    assert(SavesRaw_Writable(&places[3].rawImage));
    /* The image takes a save from an image on the settings device, and
     * lets its own one go. */
    openImage(2,"A");screenStacks[0]=2;screenStacks[1]=3;screenFocus=0;
    placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_IMAGE,false);
    assert(lastResult && rawAdds==1 && places[3].count==2 && !memcmp(imageA,frozenA,IMAGE_BYTES));
    uiSavesRawCard_t card;assert(imageSaves(imageB,&card)==2);
    u8 a[64],b[64],gone[64];assert(saveEntry(places[2].list[0],a) && UISavesRaw_Find(&card,a)<card.count);
    file_handle *own=NULL;
    for(int i=0;i<places[3].count;i++) {
        assert(saveEntry(places[3].list[i],b));
        if(!memcmp(b,"GZLP02",6)) {own=places[3].list[i];memcpy(gone,b,64);}
    }
    assert(own && saveDelete(own) && rawErases==1);
    placesRemember();placesReload();assert(places[3].rawOpen && places[3].count==1);
    assert(imageSaves(imageB,&card)==1 && UISavesRaw_Find(&card,a)<card.count && UISavesRaw_Find(&card,gone)==card.count);
    /* B on the left: its folder. A Copy out of the Source's image lands there. */
    loadFolder(&places[2],false);
    assert(!places[2].rawOpen && places[2].device==&sd && !strcmp(places[2].dir.name,"sda:/left"));
    screenFocus=1;placesRemember();saveTransfer(places[3].list[0],UI_SAVES_PLACE_FOLDER,false);
    assert(lastResult && exportCount==1 && ensured==&sd && !strncmp(exports[0].name,"sda:/left/",10));
    assert(places[2].count==1 && places[2].list[0]->device==&sd);
    /* B on the right: the Source's folder, where a Copy from the left lands. */
    loadFolder(&places[3],false);
    assert(!places[3].rawOpen && places[3].device==&sd2 && places[3].count==1);
    screenFocus=0;placesRemember();saveTransfer(places[2].list[0],UI_SAVES_PLACE_FOLDER,false);
    assert(lastResult && exportCount==2 && ensured==&sd2 && !strncmp(exports[1].name,"sdc:/swiss/saves/",17));
    assert(places[3].count==2 && places[3].list[0]->device==&sd2 && places[3].list[1]->device==&sd2);
    char path[PATHNAME_MAX];snprintf(chosen,sizeof(chosen),"backup");
    assert(destinationFolder(UI_SAVES_PLACE_CHOOSE,path,sizeof(path)) && !strcmp(path,"sdc:/backup"));
    sd2.features=0;assert(!destinationFolder(UI_SAVES_PLACE_FOLDER,path,sizeof(path)));sd2.features=FEAT_WRITE;
    /* Back to the settings device: its own Save Folder, its own home. */
    storageChoice=2;storagePress(stacks,0,BUTTON_R);
    assert(menuOpenedOn==3 && places[3].device==&sd && !strcmp(places[3].dir.name,"sda:/swiss/saves"));
    assert(!strcmp(folderHome[1],"sda:/swiss/saves") && !strcmp(folderHome[0],"sda:/left"));
    /* A Source without the Save Folder opens at its root. */
    sourceSavesMissing=true;storageChoice=3;storagePress(stacks,0,BUTTON_R);
    assert(places[3].device==&sd2 && !strcmp(places[3].dir.name,"sdc:/") && !strcmp(folderHome[1],"sdc:/"));
    assert(deletes==0 && writes==2);clean();
}
static void detailsInput(void) {
    const u32 back[]={BUTTON_B};
    setup();openImage(2,"A");openImage(3,"B");
    /* Exercise the full production A branch, not a test's save-dispatch
     * approximation. Neither RAW column can accept a write. */
    script(back,1);assert(selectPress(0,BUTTON_A)==0);
    assert(detailsShown==1 && menusShown==0);
    assert(!strcmp(detailSnapshot.name,"raw-save"));
    assert(detailSnapshot.blocks==2 && !detailSnapshot.estimated);
    assert(!strcmp(detailSnapshot.source,"Card image"));
    assert(!strcmp(detailSnapshot.updated,"Unknown"));
    assert(!strcmp(detailSnapshot.icon,"Static"));
    assert(starts==0 && writes==0 && deletes==0);
    script(back,1);selectPress(0,BUTTON_A);assert(detailsShown==2);
    selectPress(0,BUTTON_B);assert(detailsShown==2);
    places[2].selection=1;selectPress(0,BUTTON_A);assert(detailsShown==2);
    rawWritable=false;script(back,1);places[2].selection=0;selectPress(0,BUTTON_A);
    assert(detailsShown==3 && !strcmp(detailSnapshot.source,"Read-only card image"));
    unchanged();clean();

    /* Images whose device can't change them: every action is dimmed and
     * stays unselectable, while the dialog and action menu were rendered. */
    const u32 allDim[]={BUTTON_A,BUTTON_A,BUTTON_UP,BUTTON_A,
        BUTTON_DOWN,BUTTON_DOWN,BUTTON_A,BUTTON_B};
    setup();rawWritable=false;openImage(2,"A");openImage(3,"B");script(allDim,8);selectPress(0,BUTTON_A);
    assert(inputAt==8 && detailsShown==1 && menusShown>0 && seenDim==7 && over.menuSerial==1);
    assert(starts==0 && writes==0 && deletes==0 && rawAdds==0 && rawErases==0);unchanged();clean();

    /* Writable images: the menu starts on Copy, which goes into the other
     * image after its question; Erase asks first, No first. */
    const u32 copyIn[]={BUTTON_A,BUTTON_A,BUTTON_A};
    setup();openImage(2,"A");openImage(3,"B");script(copyIn,3);selectPress(0,BUTTON_A);
    assert(inputAt==3 && seenDim==0 && rawAdds==1 && rawErases==0 && lastResult);
    assert(!memcmp(imageA,frozenA,IMAGE_BYTES) && places[3].count==2);clean();
    const u32 eraseNo[]={BUTTON_A,BUTTON_DOWN,BUTTON_A,BUTTON_A};
    setup();openImage(2,"A");script(eraseNo,4);selectPress(0,BUTTON_A);
    assert(inputAt==4 && rawErases==0);unchanged();clean();
    const u32 eraseYes[]={BUTTON_A,BUTTON_DOWN,BUTTON_A,BUTTON_UP,BUTTON_A};
    setup();openImage(2,"A");script(eraseYes,5);selectPress(0,BUTTON_A);
    assert(inputAt==5 && rawErases==1 && starts==1 && ends==1 && lastResult);
    assert(places[2].rawOpen && places[2].count==0 && deletes==0);clean();

    /* A real RAW-to-independent-SD export still needs action selection
     * and its confirmation; opening details alone never performs it. */
    const u32 copy[]={BUTTON_A,BUTTON_A,BUTTON_A};
    setup();openImage(2,"A");script(copy,3);selectPress(0,BUTTON_A);
    assert(detailsShown==1 && inputAt==3 && writes==1 && exportCount==1);
    assert(lastResult && !strncmp(exports[0].name,"sda:/right/",11));
    unchanged();clean();
    setup();openImage(2,"A");sd.features=0;script(allDim,8);selectPress(0,BUTTON_A);
    assert(detailsShown==1 && seenDim==7 && starts==0 && writes==0);unchanged();clean();
}
static void detailsIconStates(void) {
    const u32 back[]={BUTTON_B};
    const char *expected[]={"Animated","Paused: UI Motion Off","None stored","Preview unavailable","Preview unavailable"};
    for(int mode=0;mode<5;mode++) {
        setup();openImage(2,"A");
        u8 *entry=places[2].rawCard->entry[0];
        entry[0x31]=0x0a;entry[0x33]=0x0e;
        swissSettings.disableUIAnimations=mode==1;
        if(mode==2)entry[0x31]=0;
        if(mode==3) {
            slotTags[0]=saveTag(2,places[2].list[0]);slots[0].failed=true;
        }
        if(mode==4) {free(pool);pool=NULL;}
        script(back,1);selectPress(0,BUTTON_A);
        assert(!strcmp(detailSnapshot.icon,expected[mode]));
        assert(detailsShown==1 && writes==0 && deletes==0);
        unchanged();clean();
    }
}
static void detailsMetadata(void) {
    const u32 back[]={BUTTON_B};
    setup();openImage(2,"A");
    int before=reads;
    u8 *entry=places[2].rawCard->entry[0];
    unsigned seconds=86400+120;
    entry[0x28]=(u8)(seconds>>24);entry[0x29]=(u8)(seconds>>16);
    entry[0x2a]=(u8)(seconds>>8);entry[0x2b]=(u8)seconds;
    entry[11]='\n';script(back,1);selectPress(0,BUTTON_A);
    assert(!strcmp(detailSnapshot.updated,"2000-01-02 00:02"));
    assert(!strcmp(detailSnapshot.name,"raw save"));
    assert(reads==before && writes==0 && deletes==0);unchanged();clean();

    /* An unreadable SD header still opens a visible details panel and
     * identifies the metadata failure instead of inventing a date. */
    setup();file_handle save={.size=2*8192+64,.fileType=IS_FILE,.device=&sd};
    strcpy(save.name,"sda:/right/unreadable.gci");
    places[2].list[0]=&save;places[2].count=1;script(back,1);selectPress(0,BUTTON_A);
    assert(detailSnapshot.estimated && !strcmp(detailSnapshot.updated,"Unable to read metadata"));
    assert(!strcmp(detailSnapshot.icon,"Unavailable"));
    assert(detailsShown==1 && writes==0 && deletes==0);unchanged();clean();

    /* A physical card's real status date is used only on a successful
     * status read for that exact game, maker and filename. */
    for(int mode=0;mode<12;mode++) {
        int slot=mode==11 ? 1 : 0;
        setup();file_handle card={.size=2*8192,.fileType=IS_FILE,
            .device=slot ? &__device_card_b : &__device_card_a};
        strcpy(card.name,slot ? "cardb:/raw-save" : "carda:/raw-save");card_dir *dir=(card_dir *)card.other;
        dir->chn=slot;dir->fileno=7;dir->filelen=card.size;expectedStatusChannel=slot;
        memcpy(dir->gamecode,"GALP",4);memcpy(dir->company,"01",2);strcpy(dir->filename,"raw-save");
        memcpy(status.gamecode,"GALP",4);memcpy(status.company,"01",2);strcpy(status.filename,"raw-save");status.time=86400+120;status.len=card.size;
        status.icon_addr=0x123;status.icon_fmt=0x15;status.icon_speed=0x2a;
        status.banner_fmt=0;status.comment_addr=UINT32_MAX;
        if(mode==1)statusResult=-1;
        if(mode==2)memcpy(status.gamecode,"GZLP",4);
        if(mode==3)memcpy(status.company,"02",2);
        if(mode==4)strcpy(status.filename,"another-save");
        if(mode==5)status.time=0;
        if(mode==6)status.time=UINT32_MAX;
        if(mode==7)status.len+=8192;
        if(mode==8)dir->filelen+=8192;
        if(mode==9)dir->chn=1;
        if(mode==10)card.size+=8192;
        places[slot].ready=true;places[slot].list[0]=&card;places[slot].count=1;screenStacks[0]=slot;
        script(back,1);selectPress(0,BUTTON_A);
        assert(statusReads==((mode==8 || mode==9 || mode==10) ? 0 : 1));
        assert(!strcmp(detailSnapshot.updated,(mode==0 || mode==11) ?
            "2000-01-02 00:02" : (mode==5 || mode==6) ?
            "Unknown" : "Unable to read metadata"));
        assert(!strcmp(detailSnapshot.icon,(mode==0 || mode==5 || mode==6 || mode==11) ? "Animated" : "Unavailable"));
        assert(detailsShown==1 && reads==0 && writes==0 && deletes==0);unchanged();clean();
    }
}
int main(void) {
    readExport();twoColumns();destinationsAndFailures();openingFailures();emptyImage();storageFocus();twoDevices();detailsInput();detailsIconStates();detailsMetadata();
    puts("RAW controller: load, virtual entry/art/read, details A/B, export, move and erase through the image writer, read-only images, reload, two SD columns and two SD devices PASS");
    return 0;
}
'''


def harness(root):
    gui = root / 'cube/swiss/source/gui'
    saves = (gui / 'saves.c').read_text()
    raw = (gui / 'saves_raw.c').read_text()
    cube = (gui / 'ui_save_cubes.c').read_text()
    start = saves.index('typedef struct {')
    end = saves.index('} savesPlace_t;', start) + len('} savesPlace_t;')
    prelude = PRELUDE.replace('static void loadCard(struct savesPlace *unused, int slot);', '')
    slot_start = saves.index('typedef struct {\n\tbool failed;')
    slot_end = saves.index('} savesSlot_t;', slot_start) + len('} savesSlot_t;')
    art_state = r"""
#define SAVES_SLOT_BYTES (UI_SAVES_ICON_FRAMES*UI_SAVES_ICON_BYTES+UI_SAVES_BANNER_BYTES)
static savesSlot_t slots[48];
static u8 *pool, scratchBytes[UI_SAVES_HEAD_SIZE+UI_SAVES_ART_MAX_END];
static u8 *scratch=scratchBytes;
static u32 slotTags[48];
static void savesSay(const char *why) {
    (void)why; says++;
    if(expectInvalidMessage) {
        assert(places[screenStacks[screenFocus]].rawOpen && !places[screenStacks[screenFocus]].ready);
        invalidMessageSeen=true;
    }
}
"""
    pieces = [prelude, saves[start:end], 'static savesPlace_t places[SAVES_TABS];', saves[slot_start:slot_end], art_state]
    pieces += [extract_function(saves, marker) for marker in ('static bool isCard(', 'static const char *slotName(')]
    pieces += [extract_function(raw, marker) for marker in ('bool SavesRaw_IsImageName(', 'static bool imageReader(', 'static bool canRead(', 'uiSavesRawStatus_t SavesRaw_Load(', 'bool SavesRaw_ReadGci(')]
    pieces += [extract_function(saves, marker) for marker in ('static savesPlace_t *rawSource(', 'static bool readSaveAt(', 'static u32 saveTag(', 'static int placeSkip(', 'static void placeClear(', 'static void artRead(')]
    model, tests = MODEL.split('static void setup(void)', 1)
    pieces += [model, extract_function(saves, 'static bool isSaveName(')]
    pieces += [extract_function(saves, marker) for marker in ('static int placeOrder(', 'static bool folderIsRoot(', 'static void loadFolder(', 'static void loadRaw(', 'static void loadTab(', 'static void folderSet(', 'static void folderOpenHome(', 'static void folderPath(', 'static void placeRemember(')]
    pieces += [extract_function(cube, marker) for marker in ('uiSaveCubesChange_t UISaveCubes_Change(', 'int UISaveCubes_Cells(', 'int UISaveCubes_Home(')]
    pieces += [extract_function(saves, marker) for marker in ('static void placesRemember(', 'static void placesReload(', 'static u8 *saveRead(', 'static bool folderWrite(', 'static bool saveDelete(', 'static bool destinationFolder(uiSavesPlace_t to, char *path, size_t size)\n{', 'static void saveTransfer(', 'static unsigned saveBlocks(', 'static bool saveEntry(', 'static void saveRoom(', 'static int placeCells(', 'static int placeCell(', 'static file_handle *placeAt(', 'static int stackFocus(', 'static int artSlot(', 'static void saveLine(', 'static void saveTitle(', 'static void saveHeading(', 'static void saveEntry32(', 'static bool saveUpdated(', 'static bool saveDetails(', 'static int savesMenu(', 'static void saveOptions(', 'static void chooseStorage(')]
    show = extract_function(saves, 'void show_saves(')
    opening = extract_function(show, 'if(SavesRaw_IsImageName(chosen->name))')
    pieces += ['static void openSelected(savesPlace_t *place,file_handle *chosen) {' + opening + '}']
    storage = extract_function(show, 'else if(pressed & (BUTTON_L | BUTTON_R))')
    body = storage[storage.index('{')+1:storage.rindex('}')]
    pieces += ['static int storagePress(int stacks[2],int focus,u32 pressed) {int input=0;' + body + 'return focus;}']
    selection = extract_function(show, 'else if((pressed & BUTTON_A) && focus >= 0)')
    pieces += ['static int selectPress(int focus,u32 pressed) {int stacks[2]={screenStacks[0],screenStacks[1]};int input=0;do {' + selection.removeprefix('else ') + '}while(0);return focus;}']
    return '\n'.join(pieces + ['static void setup(void)' + tests])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = args.source_root.resolve()
    with tempfile.TemporaryDirectory(prefix='indigo-raw-controller-') as temp:
        source, binary = Path(temp) / 'controller.c', Path(temp) / 'controller'
        source.write_text(harness(root))
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
            if sys.platform.startswith('linux'):
                flags += ['-fno-pie', '-no-pie']
        gui = root / 'cube/swiss/source/gui'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags + ['-I', str(gui), str(source), str(gui / 'ui_saves.c'), str(gui / 'ui_lang.c'), str(gui / 'ui_saves_raw.c'), str(gui / 'ui_saves_metadata.c'), str(gui / 'ui_saves_details.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
