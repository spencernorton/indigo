#!/usr/bin/env python3
"""Run the save-only popup's real renderer with bounded text and a checked GX stream.

The generic dialog is unchanged. This checks the new hierarchy, both authored
screen shapes, all menu colors, maximum numbers, long names and metadata failure.
Name and metadata fitting belongs to preparation, with no popup allocation
or refitting during drawing. The existing native hint helper is stubbed here;
its glyph geometry and fixed-label measurement have their own tests. The real
hint parser checks that each footer actually names its controller glyph.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / 'cube/swiss/source/gui'

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_saves_details.h"
#include "ui_home_text.h"
#include "ui_stage.h"
#include "ui_color.h"
#include "ui_hint.h"
typedef struct {uint8_t r,g,b,a;} GXColor;
typedef struct {int type;void *data;void *child;bool disposed;} uiDrawObj_t;
enum {ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT, EV_SAVE_DETAILS, GX_QUADS, GX_VTXFMT0, GX_TEXMAP0};
static int boxinnerTexObj,boxouterTexObj;
static bool drawing,active,ready;
static int phase,vertices,begins,strings,hints,measures,allocations,failAllocation;
static float minX,maxX,minY,maxY;
static void *checkedCalloc(size_t n,size_t bytes) {
    assert(!drawing);allocations++;
    return allocations==failAllocation ? NULL : calloc(n,bytes);
}
#define calloc checkedCalloc
static int GetTextSizeInPixels(const char *text) {
    assert(!drawing);measures++;return (int)strlen(text)*24;
}
static void drawInit(void) {assert(!active);ready=true;}
static void GX_LoadTexObj(const int *texture,int map) {
    assert(!active && ready && texture && map==GX_TEXMAP0);
}
static void GX_Begin(int primitive,int format,int count) {
    assert(drawing && ready && !active && primitive==GX_QUADS && format==GX_VTXFMT0 && count==4);
    active=true;phase=vertices=0;begins++;
}
static void GX_Position3f32(float x,float y,float z) {
    assert(active && phase==0 && isfinite(x) && isfinite(y) && z==0);
    minX=fminf(minX,x);maxX=fmaxf(maxX,x);minY=fminf(minY,y);maxY=fmaxf(maxY,y);
    phase=1;
}
static void GX_Color4u8(uint8_t r,uint8_t g,uint8_t b,uint8_t a) {
    (void)r;(void)g;(void)b;(void)a;assert(active && phase==1);phase=2;
}
static void GX_TexCoord2f32(float s,float t) {
    assert(active && phase==2 && isfinite(s) && isfinite(t));phase=0;vertices++;
}
static void GX_End(void) {assert(active && phase==0 && vertices==4);active=false;}
static void drawStringMedium(int x,int y,const char *text,float scale,int align,GXColor color) {
    float width=(float)strlen(text)*24.0f*scale;
    float left=(float)x-(align==ALIGN_CENTER ? width/2.0f : align==ALIGN_RIGHT ? width : 0.0f);
    assert(drawing && ready && !active && text && scale>=0.46f && scale<=0.92f);
    assert(left>=104 && left+width<=536.01f && y>=119 && y<=317);
    if(y==197) assert(left+width<=(x==128 ? 284.01f : 512.01f));
    if(y==150 && strlen(text)<20u) assert(scale==0.86f);
    UIColor_Apply(&color.r,&color.g,&color.b);
    assert(color.a==255 && (77*color.r+150*color.g+29*color.b)/256>=160);
    if(y==119 && x==104) assert(!strcmp(text,"SAVE DETAILS"));
    if(y==257 && x==104) assert(!strcmp(text,"Source"));
    if(y==287 && x==104) assert(!strcmp(text,"Last updated"));
    if(y==317 && x==104) assert(!strcmp(text,"Save icon"));
    strings++;
}
static void _DrawHintText(int x,int y,const char *text,float scale,int align,GXColor color) {
    uiHintItem_t item;
    const char *label=x==216 ? "Actions" : "Back";
    assert(drawing && ready && !active && y==373 && scale==0.60f && align==ALIGN_CENTER && color.a==255);
    assert(x==216 || x==412);
    assert(UIHint_Parse(text,&item,1)==1 && item.glyphCount==1 && !item.chord);
    assert(item.glyph[0]==(x==216 ? UI_HINT_GLYPH_A : UI_HINT_GLYPH_B));
    assert(item.labelLength==strlen(label) && !memcmp(item.label,label,item.labelLength));
    hints++;
}
/* ACTUAL_SOURCE */
int main(void) {
    uiSaveDetailsSnapshot_t snapshot;
    char longName[64];memset(longName,'W',63);longName[63]=0;
    const char *names[]={"Copper Archive",longName,"Moonlit Lake","Unreadable save"};
    const char *dates[]={"2024-02-29 12:34","2136-02-07 06:28","Unknown","Unable to read metadata"};
    const char *icons[]={"Animated","Paused: UI Motion Off","Static","Unavailable"};
    const uint32_t blocks[]={2,UINT32_MAX/8u,1,0};
    for(int wide=0;wide<2;wide++) for(int theme=0;theme<8;theme++) for(int variant=0;variant<4;variant++) {
        UIStage_SetWide(wide!=0);UIColor_Select(theme);
        assert(UISaveDetails_Build(&snapshot,names[variant],blocks[variant],variant==3,
            variant==2 ? "Slot A" : "Read-only card image",dates[variant],icons[variant]));
        uiDrawObj_t *event=DrawSaveDetails(&snapshot);assert(event && event->type==EV_SAVE_DETAILS);
        drawSaveDetailsEvent_t frozen=*(drawSaveDetailsEvent_t *)event->data;
        if(variant==1) assert(!strcmp(frozen.blocks,"536870911") && !strcmp(frozen.kib,"4294967288"));
        /* The renderer owns its copy after every caller field changes. */
        memset(&snapshot,0,sizeof(snapshot));
        for(int frame=0;frame<3;frame++) {
            int oldMeasures=measures,oldAllocations=allocations;
            begins=strings=hints=0;minX=minY=INFINITY;maxX=maxY=-INFINITY;
            drawing=true;_DrawSaveDetails(event);drawing=false;
            assert(!active && ready && begins==56 && strings==(variant==3 ? 13 : 12) && hints==2);
            assert(minX<=UIStage_Left()-4 && maxX>=UIStage_Right()+3 && minY==-4 && maxY==484);
            assert(measures==oldMeasures && allocations==oldAllocations);
            assert(!memcmp(&frozen,event->data,sizeof(frozen)));
        }
        free(event->data);free(event);
    }
    assert(DrawSaveDetails(NULL)==NULL);
    assert(UISaveDetails_Build(&snapshot,"Save",1,false,"SD save","Unknown","None stored"));
    for(int fail=1;fail<=2;fail++) {
        failAllocation=allocations+fail;assert(DrawSaveDetails(&snapshot)==NULL);failAllocation=0;
    }
    puts("Save details GX: 64 cases x 3 frames, hierarchy, fits, color contrast and zero popup allocation/refitting PASS");
    return 0;
}
'''


def main():
    source = (GUI / 'FrameBufferMagic.c').read_text()
    start = source.index('typedef struct {\n\tuiSaveDetailsSnapshot_t snapshot;')
    end = source.index('} drawSaveDetailsEvent_t;', start) + len('} drawSaveDetailsEvent_t;')
    pieces = [source[start:end]]
    pieces += [extract_function(source, marker) for marker in (
        'static void _drawRect(', 'static void _DrawSimpleBox(int x, int y, int width, int height, int depth, GXColor fillColor, GXColor borderColor) \n{',
        'static void _DrawSaveDetails(', 'static bool _PrepareSaveDetails(', 'uiDrawObj_t* DrawSaveDetails(')]
    with tempfile.TemporaryDirectory(prefix='indigo-save-details-gx-') as temp:
        c = Path(temp) / 'draw.c'
        binary = Path(temp) / 'draw'
        c.write_text(HARNESS.replace('/* ACTUAL_SOURCE */', '\n'.join(pieces)))
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I', str(GUI), str(c),
            str(GUI / 'ui_saves_details.c'), str(GUI / 'ui_home_text.c'),
            str(GUI / 'ui_stage.c'), str(GUI / 'ui_color.c'),
            str(GUI / 'ui_hint.c'), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
