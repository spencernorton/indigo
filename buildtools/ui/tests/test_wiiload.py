#!/usr/bin/env python3
"""Upload stream, executable bounds, socket timeout and video-safe handoff.

The production functions run here. The fake scheduler holds the menu's
video lock at handoff, and rejects a suspend that could strand that owner.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function
from wiiload_source import SOURCE, stream_source

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <zlib.h>
typedef uint8_t u8;typedef uint32_t u32;
typedef int uiDrawObj_t;
typedef int wiiload_read_function_t(int,void *,size_t);
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ATTRIBUTE_PACKED __attribute__((packed))
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define LWP_THREAD_NULL 0
#define LWP_MUTEX_NULL 0
#define LWP_PRIO_NORMAL 64
#define PATCH_BIN 0
#define SYS_HOTRESET 0
#define FALSE 0
static int main_thread=1,upload_mutex=2;
static bool suspended,videoOwner,guarded,mutexHeld,idleSocket,artworkBlocked;
static unsigned suspends,launches,progresses,disposes,rawMode;
static u8 incoming[65536];static size_t incomingSize,cursor,chunk=4096;
static void menuStep(void) {assert(!suspended);videoOwner=false;}
static int reader(int fd,void *out,size_t n) {
    (void)fd;assert(!suspended);
    if(n>chunk)n=chunk;
    if(n>incomingSize-cursor)n=incomingSize-cursor;
    memcpy(out,incoming+cursor,n);cursor+=n;videoOwner=true;return (int)n;
}
static void DrawUpdateProgressBar(uiDrawObj_t *p,int percent) {
    (void)p;assert(!suspended&&percent>=0&&percent<=100);progresses++;
}
static uiDrawObj_t *DrawProgressBar(bool a,int b,const char *c) {
    (void)a;(void)b;(void)c;return (uiDrawObj_t *)1;
}
static uiDrawObj_t *DrawPublish(uiDrawObj_t *p) {if(videoOwner)menuStep();return p;}
static void DrawDispose(uiDrawObj_t *p) {(void)p;if(videoOwner)menuStep();disposes++;}
static void DrawWithVideoLocked(void (*fn)(void *),void *ctx) {
    if(videoOwner)menuStep();
    guarded=true;fn(ctx);guarded=false;
}
static void DrawGameflowBlockPosters(void) {assert(!suspended&&!artworkBlocked);artworkBlocked=true;}
static void DrawGameflowUnblockPosters(void) {assert(suspended&&artworkBlocked);artworkBlocked=false;}
static void LWP_SuspendThread(int t) {(void)t;assert(guarded&&!videoOwner&&artworkBlocked);suspended=true;suspends++;}
static void LWP_ResumeThread(int t) {(void)t;assert(suspended&&!artworkBlocked);suspended=false;}
static int LWP_SetThreadPriority(int t,int priority) {(void)t;(void)priority;return 64;}
static void LWP_MutexLock(int m) {(void)m;assert(!mutexHeld);mutexHeld=true;}
static void LWP_MutexUnlock(int m) {(void)m;assert(mutexHeld);mutexHeld=false;}
static bool mp3_player_initialized(void) {return false;}
static void MP3Player_Stop(void) {assert(suspended);}
static bool menuaudio_shutdown(void) {assert(suspended);return false;}
static void menuaudio_init(void) {}
static void DrawShutdown(void) {assert(0);}
static void SYS_ResetSystem(int a,int b,int c) {(void)a;(void)b;(void)c;assert(0);}
static u32 branchResolve(void *p,int type,int n) {(void)p;(void)type;(void)n;return rawMode;}
static int ELFtoARAM(void *p,void *a,size_t n) {(void)p;(void)a;(void)n;assert(suspended);launches++;return 0;}
static int DOLtoARAM(void *p,void *a,size_t n) {return ELFtoARAM(p,a,n);}
static int BINtoARAM(void *p,size_t n,u32 a,u32 b) {(void)a;(void)b;return ELFtoARAM(p,NULL,n);}
static int net_select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *t) {
    (void)n;(void)r;(void)w;(void)e;assert(t->tv_sec>0);t->tv_sec=0;return idleSocket?0:1;
}
static int net_read(int sd,void *out,size_t n) {return reader(sd,out,n);}
'''

DRIVER = r'''
static void be32(u8 *p,u32 n) {p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void be16(u8 *p,u32 n) {p[0]=n>>8;p[1]=n;}
static size_t compressed(const u8 *plain,size_t size) {
    uLongf n=sizeof(incoming);assert(compress(incoming,&n,plain,size)==Z_OK);
    incomingSize=n;cursor=0;chunk=4096;suspended=false;videoOwner=false;return n;
}
static void transferArgs(const u8 *image,size_t size,const char *args,size_t argsSize) {
    u8 packed[65536];size_t n=compressed(image,size);memcpy(packed,incoming,n);
    wiiload_header_t h={0};memcpy(&h.magic,"HAXX",4);h.version=5;
    h.deflate_size=(u32)n;h.inflate_size=(u32)size;h.args_size=argsSize;
    memcpy(incoming,&h,sizeof(h));memcpy(incoming+sizeof(h),packed,n);
    memcpy(incoming+sizeof(h)+n,args,argsSize);
    incomingSize=sizeof(h)+n+h.args_size;cursor=0;chunk=4096;videoOwner=true;
    wiiload_handler(0,reader,"Receiving","TCP:192.0.2.1");
    assert(!suspended&&!mutexHeld&&!artworkBlocked);
}
static void transfer(const u8 *image,size_t size,bool badArgs) {
    transferArgs(image,size,"xx",badArgs?2:0);
}
int main(void) {
    u8 plain[320]={0};memset(plain+256,0x55,64);
    be32(plain,256);be32(plain+0x48,0x80003100);be32(plain+0x90,64);
    be32(plain+0xe0,0x80003100);
    assert(wiiload_image_valid(plain,sizeof(plain),false));
    size_t n=compressed(plain,sizeof(plain));
    void *out=wiiload_read(0,n,sizeof(plain),reader,NULL);
    assert(out&&memcmp(out,plain,sizeof(plain))==0);free(out);
    n=compressed(plain,sizeof(plain));chunk=1;
    out=wiiload_read(0,n,sizeof(plain),reader,NULL);assert(out);free(out);
    n=compressed(plain,sizeof(plain));
    assert(!wiiload_read(0,n+1,sizeof(plain),reader,NULL)); /* EOF after stream */
    n=compressed(plain,sizeof(plain));incomingSize-=2;
    assert(!wiiload_read(0,n-2,sizeof(plain),reader,NULL)); /* no checksum */
    n=compressed(plain,sizeof(plain));assert(!wiiload_read(0,n,sizeof(plain)+1,reader,NULL));
    n=compressed(plain,sizeof(plain));assert(!wiiload_read(0,n,sizeof(plain)-1,reader,NULL));
    assert(!wiiload_read(0,0,320,reader,NULL));
    assert(!wiiload_read(0,WIILOAD_MAX_DEFLATE+1u,320,reader,NULL));
    assert(!wiiload_read(0,1,WIILOAD_MAX_IMAGE+1u,reader,NULL));
    assert(!wiiload_read(0,1,3,reader,NULL));
    n=compressed(plain,sizeof(plain));incoming[n++]=1;incomingSize=n;
    assert(!wiiload_read(0,n,sizeof(plain),reader,NULL)); /* trailing bytes */
    transfer(plain,sizeof(plain),false);assert(launches==1&&suspends==1);
    transfer(plain,sizeof(plain),true);assert(launches==1&&suspends==1);
    be32(plain+0x90,0xffffffffu);
    assert(!wiiload_image_valid(plain,sizeof(plain),false));
    transfer(plain,sizeof(plain),false);assert(launches==1&&suspends==1);
    memset(plain,0,sizeof(plain));memcpy(plain,ELFMAG,4);plain[4]=1;plain[5]=2;
    be16(plain+16,2);be16(plain+18,20);be32(plain+24,0x80003100);
    be32(plain+28,52);be16(plain+42,32);be16(plain+44,1);
    be32(plain+52,1);be32(plain+56,256);be32(plain+64,0x80003100);
    be32(plain+68,64);be32(plain+72,64);
    assert(wiiload_image_valid(plain,sizeof(plain),false));
    transfer(plain,sizeof(plain),false);assert(launches==2&&suspends==2);
    be32(plain+28,0xfffffff0u);assert(!wiiload_image_valid(plain,sizeof(plain),false));
    /* Named SDLOADER.BIN, an ELF still loads as an ELF: it is checked as one. */
    transferArgs(plain,sizeof(plain),"SDLOADER.BIN",13);assert(launches==2&&suspends==2);
    for(size_t size=0;size<256;size++)assert(!wiiload_image_valid(plain,size,false));
    memset(plain,0,4);rawMode=1;transfer(plain,4,false);assert(launches==3&&suspends==3);
    rawMode=0;transferArgs(plain,4,"SDLOADER.BIN",13);assert(launches==4&&suspends==4);
    n=compressed(plain,4);idleSocket=true;
    assert(tcp_read(3,plain,4)<0);idleSocket=false;chunk=1;
    assert(tcp_read(3,plain,4)==4);
    assert(disposes==7&&progresses>0);
    puts("uploads: bounded exact streams, executable ranges, EOF, timeouts, args and video-safe handoff PASS");
    return 0;
}
'''


def main():
    header = SOURCE[SOURCE.index("typedef struct {"):SOURCE.index("typedef int wiiload_read_function_t")]
    functions = stream_source() + "\n".join(extract_function(SOURCE, marker) for marker in (
        "static int tcp_read(", "static void wiiload_suspend_main(", "static void wiiload_handler("))
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / "upload.c").write_text(PREFIX + header + functions + DRIVER)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1"]
        if "--sanitize" in sys.argv:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if sys.platform != "darwin":
                command += ["-no-pie"]
        subprocess.run(command + [str(path / "upload.c"), "-lz", "-o", str(path / "upload")], check=True)
        subprocess.run([str(path / "upload")], check=True, timeout=10)


if __name__ == "__main__":
    main()
