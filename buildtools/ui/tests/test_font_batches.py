#!/usr/bin/env python3
"""Compare the real font emitter with the release's per-glyph stream.

Texture-sheet changes, bounded runs, both weight passes, alignment, scale,
newlines and extended characters must preserve every emitted vertex.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function
from test_frame_budget import renderer_definition

ROOT = Path(__file__).resolve().parents[3]
FONT = "cube/swiss/source/gui/IPLFontWrite.c"

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef float Mtx[3][4];
typedef struct {u8 r,g,b,a;} GXColor;
typedef struct {void *data;} GXTexObj;
static struct {int cell_height;} fontStorage={24}, *font=&fontStorage;
static GXTexObj fontTexObj;
#define MEM_VIRTUAL_TO_PHYSICAL(p) (p)
#define GX_QUADS 1
#define GX_VTXFMT1 1
#define GX_TEXMAP0 0
#define GX_PNMTX0 0
typedef struct {int x,y,s,t;GXColor color;uintptr_t sheet;Mtx matrix;} vertex_t;
static vertex_t stream[2][8192], pending;
static Mtx matrix;
static unsigned which, counts[2], begins[2], remaining;
static char sheets[2];
static void drawFontInit(void) {assert(!remaining);fontTexObj.data=NULL;}
static int SYS_GetFontWidth(unsigned char c) {return 7+c%13;}
static void SYS_GetFontTexture(unsigned char c, void **image,int *s,int *t,int *w) {
    assert(!remaining);*image=&sheets[(c>='A'&&c<='Z')||c>=128];
    *s=c%16*24;*t=c/16*24;*w=SYS_GetFontWidth(c);
}
static void guMtxTrans(Mtx m,float x,float y,float z) {
    memset(m,0,sizeof(Mtx));m[0][0]=m[1][1]=m[2][2]=1;
    m[0][3]=x;m[1][3]=y;m[2][3]=z;
}
static void guMtxScaleApply(Mtx in,Mtx out,float x,float y,float z) {
    float factor[3]={x,y,z};
    for(int r=0;r<3;r++)for(int c=0;c<4;c++)out[r][c]=in[r][c]*factor[r];
}
static void guMtxTransApply(Mtx in,Mtx out,float x,float y,float z) {
    for(int r=0;r<3;r++)for(int c=0;c<4;c++)out[r][c]=in[r][c];
    out[0][3]+=x;out[1][3]+=y;out[2][3]+=z;
}
static void GX_LoadPosMtxImm(Mtx m,int slot) {(void)slot;memcpy(matrix,m,sizeof(matrix));}
static void *GX_GetTexObjData(GXTexObj *obj) {return obj->data;}
static void GX_InitTexObjData(GXTexObj *obj,void *p) {assert(!remaining);obj->data=p;}
static void GX_LoadTexObj(GXTexObj *obj,int slot) {(void)obj;(void)slot;assert(!remaining);}
static void GX_Begin(int kind,int format,unsigned n) {
    (void)kind;(void)format;assert(!remaining&&n&&n%4==0&&n<=65535);
    remaining=n;begins[which]++;
}
static void GX_End(void) {assert(!remaining);}
static void GX_Position2s16(int x,int y) {
    memset(&pending,0,sizeof(pending));pending.x=(int16_t)x;pending.y=(int16_t)y;
}
static void GX_Color4u8(u8 r,u8 g,u8 b,u8 a) {pending.color=(GXColor){r,g,b,a};}
static void GX_TexCoord2s16(int s,int t) {
    assert(remaining&&counts[which]<8192);remaining--;
    pending.s=(int16_t)s;pending.t=(int16_t)t;pending.sheet=(uintptr_t)fontTexObj.data;
    memcpy(pending.matrix,matrix,sizeof(matrix));stream[which][counts[which]++]=pending;
}
'''

DRIVER = r'''
int main(void) {
    char longText[600];memset(longText,'m',599);longText[599]=0;
    const char *texts[]={NULL,"","hello world","aaaBBBcccDDD","a\nnot drawn",
        "\xE9\x81" "aaBbb",longText};
    unsigned totalBefore=0,totalAfter=0;
    for(unsigned text=0;text<sizeof(texts)/sizeof(texts[0]);text++)
    for(int align=0;align<3;align++)for(int medium=0;medium<2;medium++)
    for(int alpha=0;alpha<3;alpha++) {
        GXColor color={103,56,233,(u8)(alpha==2?255:alpha*123)};
        memset(counts,0,sizeof(counts));memset(begins,0,sizeof(begins));
        which=0;reference(113,71,texts[text],0.5f+align*0.25f,align,color,medium);
        which=1;drawStringWeighted(113,71,texts[text],0.5f+align*0.25f,align,color,medium);
        assert(counts[0]==counts[1]);
        assert(memcmp(stream[0],stream[1],counts[0]*sizeof(vertex_t))==0);
        assert(begins[1]<=begins[0]);
        if(text==6) assert(begins[1]==10u*(medium?2u:1u));
        totalBefore+=begins[0];totalAfter+=begins[1];
    }
    printf("font batches: identical glyph/matrix/texture streams; %u -> %u batches PASS\n",
        totalBefore,totalAfter);
    return 0;
}
'''


def check_immutable_textures(path, command):
    """Execute the actual setup and bind paths across repeated draws/reinit.

    Unrelated GX state setters are counted no-ops; loads and invalidations
    retain their identities. Mutable pack textures and EFB copies are outside
    this check and retain their own publication/synchronization contracts.
    """
    font = (ROOT / FONT).read_text()
    frame = (ROOT / "cube/swiss/source/gui/FrameBufferMagic.c").read_text()
    functions = "\n".join((extract_function(font, "void init_font("),
                            extract_function(font, "void drawFontInit("),
                            extract_function(frame, "static void init_textures("),
                            renderer_definition(frame, "_DrawSimpleBox")))
    calls = set(re.findall(r"\b((?:GX_|gu|UIStage_|SYS_|TPL_|_drawRect)\w*)\s*\(", functions))
    stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef float Mtx[3][4];
typedef float Mtx44[4][4];
typedef struct {unsigned char r,g,b,a;} GXColor;
static bool fontReady, fontTexelsPending, boxTexelsPending;
static unsigned invalidations, loads, rects;
static void note(const char *name) {
    if(!strcmp(name,"GX_InvalidateTexAll")) invalidations++;
    if(!strcmp(name,"GX_LoadTexObj")) loads++;
    if(!strcmp(name,"_drawRect")) rects++;
}
'''
    # Ignore the register values here; the existing native stream tests and
    # target compilation cover their types and pipeline behavior.
    stubs += "\n".join(f'#define {name}(...) note("{name}")' for name in sorted(calls - {"SYS_InitFont"}))
    stubs += "\n#define SYS_InitFont(...) true\n"
    driver = r'''
int main(void) {
    for(unsigned initialization=1;initialization<=2;initialization++) {
        init_font();
        assert(fontReady);
        for(unsigned draw=0;draw<100;draw++) drawFontInit();
        assert(invalidations==initialization && loads==100*initialization);
    }
    invalidations=loads=0;
    for(unsigned initialization=1;initialization<=2;initialization++) {
        init_textures();
        for(unsigned draw=0;draw<100;draw++)
            _DrawSimpleBox(10,20,100,50,0,(GXColor){0},(GXColor){0});
        assert(invalidations==initialization && loads==200*initialization);
        assert(rects==800*initialization);
    }
    puts("immutable textures: one invalidation per publication across 100 draws and reinit PASS");
    return 0;
}
'''
    source = path / "textures.c"
    source.write_text(stubs + functions + driver)
    # Setters' arguments are intentionally discarded by these macros.
    subprocess.run(command + ["-Wno-unused-variable", "-Wno-unused-parameter",
                              "-Wno-unused-but-set-parameter",
                              str(source), "-o", str(path / "textures")], check=True)
    subprocess.run([str(path / "textures")], check=True, timeout=10)


def main():
    baseline = subprocess.check_output(["git", "-C", str(ROOT), "show", "v2.4.0:" + FONT], text=True)
    marker = "static void drawStringWeighted("
    old = extract_function(baseline, marker).replace("drawStringWeighted(", "reference(", 1)
    current = extract_function((ROOT / FONT).read_text(), marker)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / "font.c").write_text(STUBS + old + current + DRIVER)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-ffp-contract=off"]
        if "--sanitize" in sys.argv:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if sys.platform != "darwin":
                command += ["-no-pie"]
        subprocess.run(command + [str(path / "font.c"), "-o", str(path / "font")], check=True)
        subprocess.run([str(path / "font")], check=True, timeout=10)
        check_immutable_textures(path, command)


if __name__ == "__main__":
    main()
