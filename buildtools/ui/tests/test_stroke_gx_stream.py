#!/usr/bin/env python3
"""Compile the actual native stroke emitters against a checked GX FIFO.

Exercises constant screen-space width through perspective, closed joins, alpha
coverage, attribute completeness, matrix restoration, and near-edge-on guards.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"

HARNESS = r"""
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct { u8 r,g,b,a; } GXColor;
/* Menu Color is Indigo here: the emitters' recolor passes colors through. */
static void UIColor_Apply(u8 *r,u8 *g,u8 *b) { (void)r; (void)g; (void)b; }
/* 4:3 unless a run widens x fades for Menu Widescreen's 3/4 squeeze; fades
 * are then measured through the squeeze, in frame pixels. */
static float pixelWidth=1, squeeze=1;
static float UIStage_PixelWidth(void) { return pixelWidth; }
typedef float Mtx[3][4];
typedef struct { float x,y,z; } guVector;
typedef struct { float x,y; } indigoPoint_t;
typedef struct { guVector point[4]; GXColor color[4]; } cubeSurfaceQuad_t;
typedef struct { indigoPoint_t point[24]; int count; float length[24], pixels[24];
    indigoPoint_t corner[24]; bool joined[24]; } cubeOutline_t;
typedef struct { guVector eye[2]; indigoPoint_t outward[2]; GXColor color[2]; } cubeCoverageEdge_t;
enum { GX_QUADS=1, GX_TRIANGLESTRIP=2, GX_TRIANGLES=3, GX_TRIANGLEFAN=4, GX_VTXFMT0=0, GX_PNMTX0=0 };
/* HOME ENUMS */
typedef struct { Mtx model,semanticFaces[UI_HOME_FACE_COUNT];
    float motifAlpha[UI_HOME_FACE_COUNT],scaleX,scaleY; } cubeRasterTransform_t;
/* Every face's glyph at full opacity. */
static void lightAll(cubeRasterTransform_t *r) {
    for(int face=0;face<UI_HOME_FACE_COUNT;face++) r->motifAlpha[face]=1.0f;
}
enum { GX_ENABLE=1, GX_LEQUAL=2, GX_FALSE=0, GX_TRUE=1,
    GX_CULL_BACK=1, GX_CULL_NONE=0, GX_CULL_FRONT=2,
    GX_BM_BLEND=3, GX_BL_SRCALPHA=4, GX_BL_ONE=5, GX_LO_CLEAR=6, GX_BL_INVSRCALPHA=7,
    GX_BM_NONE=8, GX_BL_ZERO=9 };
typedef struct { bool available; float hourX,hourY,minuteX,minuteY,secondX,secondY; } uiClockFrame_t;
typedef struct { bool available; s8 stickX,stickY,substickX,substickY; u32 buttons; } indigoPadFrame_t;
typedef struct { float stickX,stickY,substickX,substickY; u32 pressed; } controllerPose_t;
/* CONTROLLER IDLE */
static controllerIdle_t controllerIdle;
enum { PAD_BUTTON_LEFT=0x0001, PAD_BUTTON_RIGHT=0x0002, PAD_BUTTON_DOWN=0x0004, PAD_BUTTON_UP=0x0008,
    PAD_TRIGGER_Z=0x0010, PAD_TRIGGER_R=0x0020, PAD_TRIGGER_L=0x0040, PAD_BUTTON_A=0x0100,
    PAD_BUTTON_B=0x0200, PAD_BUTTON_X=0x0400, PAD_BUTTON_Y=0x0800, PAD_BUTTON_START=0x1000 };
/* CONTROLLER DEFINES */
#define INDIGO_TAU 6.28318530718f
#define CUBE_CAMERA_Z -5.4f
#define CHECK(c,m) do { if(!(c)) { fprintf(stderr,"%s\n",m); exit(73); } } while(0)
static bool active, uv, surfaceTest, studio;
static int phase, remaining, count, begins, matrixLoads, firstPrimitive, litBegins;
static guVector positions[4096];
static u8 alphas[4096];
static GXColor colors[4096];
static Mtx loaded;
static int culling, depthWrites;
static void GX_SetCullMode(int mode) { culling=mode; }
/* The glass is clear all through: no pass writes depth or draws opaque. */
static void GX_SetZMode(int enable,int comparison,int write) {
    CHECK(enable==GX_ENABLE && comparison==GX_LEQUAL && write==GX_FALSE,
        "glass depth policy: a pass writes depth");
    depthWrites=write;
}
/* Light adds or covers; only the icons' shadows multiply what is there. */
static int blendSource=GX_BL_SRCALPHA, blendDestination=GX_BL_INVSRCALPHA;
static void GX_SetBlendMode(int mode,int source,int destination,int operation) {
    CHECK(operation==GX_LO_CLEAR && mode==GX_BM_BLEND &&
        ((source==GX_BL_SRCALPHA && (destination==GX_BL_ONE || destination==GX_BL_INVSRCALPHA)) ||
         (source==GX_BL_ZERO && destination==GX_BL_INVSRCALPHA)),"glass blend policy");
    blendSource=source; blendDestination=destination;
}
static void reset(bool textured) {
    CHECK(!active,"previous primitive unfinished");
    uv=textured; surfaceTest=studio=false;
    phase=remaining=count=begins=matrixLoads=firstPrimitive=litBegins=0;
}
static void GX_Begin(int primitive,int format,int vertices) {
    CHECK(!active && phase==0,"nested begin or partial vertex");
    CHECK((primitive==GX_QUADS || primitive==GX_TRIANGLESTRIP || primitive==GX_TRIANGLES ||
        primitive==GX_TRIANGLEFAN) && format==0,
        "wrong primitive/format");
    CHECK(vertices>0,"empty primitive");
    if(surfaceTest && begins>0) {
        CHECK(culling==GX_CULL_NONE && depthWrites==GX_FALSE,
            "silhouette fringe inherited culling or writes depth");
    }
    if(!uv && matrixLoads>0 && (!surfaceTest || begins>0)) {
        for(int row=0;row<3;row++) for(int column=0;column<4;column++) {
            CHECK(loaded[row][column]==(row==column?1.0f:0.0f),
                "camera-space geometry inherited a model transform");
        }
    }
    if(begins==0) firstPrimitive=primitive;
    if(blendSource!=GX_BL_ZERO) ++litBegins;
    active=true; remaining=vertices; ++begins;
}
static void GX_Position3f32(float x,float y,float z) {
    CHECK(active && phase==0 && remaining>0,"missing attribute or excess vertex");
    CHECK(isfinite(x)&&isfinite(y)&&isfinite(z),"nonfinite vertex");
    CHECK(count<4096,"unbounded vertex stream");
    positions[count]=(guVector){x,y,z}; phase=1;
}
static void GX_Color4u8(u8 r,u8 g,u8 b,u8 a) {
    CHECK(active && phase==1,"color order"); alphas[count]=a;
    colors[count]=(GXColor){r,g,b,a};
    if(uv) phase=2; else { phase=0; --remaining; ++count; }
}
static float studioS[4096], studioT[4096];
static void GX_TexCoord2f32(float s,float t) {
    CHECK(uv && active && phase==2,"missing or unexpected texture attribute");
    if(studio) {
        CHECK(s>=0 && s<=1 && t>=0 && t<=1,"studio coordinates off the map");
        studioS[count]=s; studioT[count]=t;
    }
    else CHECK(s==0 && t==0,"raster UV");
    phase=0; --remaining; ++count;
}
static void GX_End(void) {
    CHECK(active && phase==0 && remaining==0,"incomplete GX primitive"); active=false;
}
static void guMtxIdentity(Mtx m) {
    memset(m,0,sizeof(Mtx)); m[0][0]=m[1][1]=m[2][2]=1;
}
static void GX_LoadPosMtxImm(const Mtx m,int slot) {
    CHECK(!active && slot==0,"matrix changed inside primitive");
    memcpy(loaded,m,sizeof(Mtx)); ++matrixLoads;
}
/* EMITTERS */
static bool closef(float a,float b) { return fabsf(a-b)<0.002f; }
static void test_fast_sqrt(void) {
    /* The estimate's error repeats every two binades, so [1, 4) bounds it
     * everywhere (every 64th float: 262,144 of them); special values stay
     * sqrtf's. */
    union { float f; u32 i; } x={1.0f};
    for(;x.f<4.0f;x.i+=64)
        CHECK(fabs(fastSqrt(x.f)-sqrt(x.f))<=5e-6*sqrt(x.f),"fast square root drifted");
    CHECK(fastSqrt(0.0f)==0.0f && fastSqrt(INFINITY)==INFINITY && isnan(fastSqrt(-1.0f)) &&
        fastSqrt(FLT_MIN/4)==sqrtf(FLT_MIN/4),"fast square root's special values");
}
static void test_dial(void) {
    for(int segments=1;segments<=24;segments++) {
        reset(true);
        _DrawSystemRing(598,42,19,0.8f,18,segments,(GXColor){122,112,201,62});
        CHECK(count==3*(segments+1)*2 && begins==3,"dial vertex budget");
        for(int band=0;band<3;band++) for(int i=0;i<=segments;i++) {
            int a=(band*(segments+1)+i)*2,b=a+1;
            float wanted[4]={17.7f,18.7f,19.3f,20.3f};
            CHECK(closef(hypotf(positions[a].x-598,positions[a].y-42),wanted[band]) &&
                closef(hypotf(positions[b].x-598,positions[b].y-42),wanted[band+1]),
                "dial coverage/radius changed");
            CHECK((band==0 ? alphas[a]==0 : alphas[a]==62) &&
                (band==2 ? alphas[b]==0 : alphas[b]==62),"dial fringe alpha");
        }
    }
}
static void test_rail_joins(void) {
    indigoPoint_t join;
    CHECK(!railJoin((indigoPoint_t){0,0},(indigoPoint_t){0,0},(indigoPoint_t){1,1},&join),
        "zero-length join accepted");
    CHECK(!railJoin((indigoPoint_t){0,0},(indigoPoint_t){1,0},(indigoPoint_t){0,0},&join),
        "opposed join accepted");
}
/* The default Source, Settings and System icons, with None on Library. */
static const int quadIcons[UI_HOME_FACE_COUNT]={-1,0,0,0,-1,-1,-1,-1};
static void test_motifs(void) {
    cubeRasterTransform_t r;
    const Mtx semanticFaces[4]={
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{0,0,1,0},{0,1,0,0},{-1,0,0,0}},
        {{-1,0,0,0},{0,1,0,0},{0,0,-1,0}},
        {{0,0,-1,0},{0,1,0,0},{1,0,0,0}}
    };
    memcpy(r.semanticFaces,semanticFaces,sizeof(semanticFaces)); lightAll(&r);
    uiClockFrame_t clock={true,0,1,1,0,0.70710678f,0.70710678f};
    r.scaleX=r.scaleY=625.221f;
    long glowAt25=0, glowAt90=0;
    for(int angle=0;angle<360;angle+=5) {
        float yaw=angle*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw);
        guMtxIdentity(r.model);
        r.model[0][0]=c; r.model[0][2]=s; r.model[2][0]=-s; r.model[2][2]=c;
        r.model[2][3]=-5.4f;
        reset(false);
        drawFaceIcons(1.0f,true,&clock,NULL,quadIcons,&r);
        /* Each quad icon draws its whole fixed stream, or nothing while its
         * face is turned away (Hub 180, Sliders 120, Clock 220). */
        CHECK(begins<=3 && count<=520 && count%20==0 && matrixLoads==2,"motif stream/matrix budget");
        CHECK(culling==GX_CULL_BACK && memcmp(loaded,r.model,sizeof(Mtx))==0,
            "motif culling/model restoration");
        int lit=0;
        for(int i=0;i<count;i+=20) {
            if(!alphas[i]) continue;
            ++lit;
            CHECK(alphas[i+1] && alphas[i+2] && alphas[i+3],"solid motif core missing");
            for(int j=4;j<20;j+=4) {
                CHECK(alphas[i+j] && !alphas[i+j+1] && !alphas[i+j+2] && alphas[i+j+3],
                    "semantic polygon fringe must reach zero");
            }
        }
        /* A side face's icon fades out as the face turns edge-on, gone more
         * than ~71 degrees from the camera, so within 20 degrees of
         * Library-front nothing draws: the Library face shows None here. */
        CHECK(lit>0 || angle<=20 || angle>=340,"all face motifs vanished");
        if(angle==0) CHECK(lit==0,"None on the Library face drew something");
        if(angle==15 || angle==345) CHECK(lit==0,"an icon drew on a face 75 degrees from the camera");
        if(angle==90) CHECK(lit==11,"front System must show its clock and all three hands");
        long glow=0; for(int i=0;i<count;i++) glow+=alphas[i];
        if(angle==25) glowAt25=glow;
        if(angle==90) glowAt90=glow;
    }
    /* At 65 degrees from the camera the System icon is on its way in:
     * lit, but fainter than facing the camera. */
    CHECK(glowAt25>0 && glowAt25<glowAt90/2,"a turning face's icon did not fade in");
    /* Subpixel-width geometry stays ordered and fades instead of inverting. */
    guMtxIdentity(r.model); r.model[2][3]=-5.4f;
    reset(false); GX_Begin(GX_QUADS,0,20);
    putSemanticMotifRect(&r,UI_HOME_FACE_LIBRARY,-0.0003f,-0.3f,0.0003f,0.3f,
        1.012f,(GXColor){255,255,255,200});
    GX_End();
    CHECK(alphas[0]>0 && alphas[0]<200,"subpixel motif did not attenuate");
    float area=0;
    for(int i=0;i<4;i++) area+=positions[i].x*positions[(i+1)%4].y-
        positions[(i+1)%4].x*positions[i].y;
    CHECK(area<0,"subpixel core inverted");
    /* The clock's minute hand at ten to the hour on a face turned 60 to 78
     * degrees, still toward the camera, at the 4x by 2x scale the side-face
     * pictures draw at: some of these turns make its corners too sharp to
     * mitre, and it keeps its outline instead of vanishing. */
    r.scaleX=4*625.221f; r.scaleY=2*625.221f;
    for(int angle=60;angle<=78;angle+=2) {
        float yaw=angle*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw);
        guMtxIdentity(r.model);
        r.model[0][0]=c; r.model[0][2]=s; r.model[2][0]=-s; r.model[2][2]=c;
        r.model[2][3]=-5.4f;
        reset(false); GX_Begin(GX_QUADS,0,20);
        putSemanticFaceHand(&r,UI_HOME_FACE_LIBRARY,-0.8660254f,0.5f,0.49f,0.027f,0.055f,
            1.012f,(GXColor){255,255,255,200});
        GX_End();
        float hand=0;
        for(int i=0;i<4;i++) hand+=positions[i].x*positions[(i+1)%4].y-
            positions[(i+1)%4].x*positions[i].y;
        CHECK(alphas[0] && alphas[1] && hand<0,"an edge-on slanted hand vanished");
    }
    r.scaleX=r.scaleY=625.221f;
    clock.available=false;
    float yaw=INDIGO_TAU*0.25f;
    r.model[0][0]=cosf(yaw); r.model[0][2]=sinf(yaw);
    r.model[2][0]=-sinf(yaw); r.model[2][2]=cosf(yaw);
    clock.available=true;
    reset(false); drawFaceIcons(1.0f,false,&clock,NULL,quadIcons,&r);
    int validCount=count;
    clock.available=false;
    reset(false); drawFaceIcons(1.0f,false,&clock,NULL,quadIcons,&r);
    CHECK(count==validCount && count>=220,"invalid clock changed GX count");
    /* Hands occupy the first three of the clock's eleven shapes, which come
     * last; the side faces' icons before it draw nothing facing away. */
    for(int i=count-220;i<count-220+3*20;i++) CHECK(alphas[i]==0,"invalid clock invented hands");
}
static int moved(const guVector *before,int n) {
    int changed=0;
    for(int i=0;i<n;i++) if(!closef(before[i].x,positions[i].x) || !closef(before[i].y,positions[i].y)) ++changed;
    return changed;
}
static int brighter(const u8 *before,int n) {
    int changed=0;
    for(int i=0;i<n;i++) if(alphas[i]>before[i]) ++changed;
    return changed;
}
static void drawController(const cubeRasterTransform_t *r,float seconds,bool animated,
    const indigoPadFrame_t *pad) {
    static const int icons[UI_HOME_FACE_COUNT]={0,-1,-1,-1,-1,-1,-1,-1};
    drawFaceIcons(seconds,animated,NULL,pad,icons,r);
}
static void test_controller(void) {
    static guVector neutral[4096];
    static u8 neutralAlphas[4096];
    cubeRasterTransform_t r;
    const Mtx semanticFaces[4]={
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{0,0,1,0},{0,1,0,0},{-1,0,0,0}},
        {{-1,0,0,0},{0,1,0,0},{0,0,-1,0}},
        {{0,0,-1,0},{0,1,0,0},{1,0,0,0}}
    };
    memcpy(r.semanticFaces,semanticFaces,sizeof(semanticFaces)); lightAll(&r);
    r.scaleX=r.scaleY=625.221f;
    guMtxIdentity(r.model); r.model[2][3]=-5.4f;
    /* The traced silhouette is a clockwise loop that fits a band. */
    int points=(int)(sizeof(controllerOutline)/sizeof(controllerOutline[0]));
    float area=0;
    for(int i=0;i<points;i++) area+=controllerOutline[i][0]*controllerOutline[(i+1)%points][1]-
        controllerOutline[(i+1)%points][0]*controllerOutline[i][1];
    CHECK(points<=FACE_BAND_MAX && area<0,"controller outline is not a clockwise loop within a band");
    /* Bands: 12 vertices per point, one primitive. Fills: a fan plus a fringe
     * quad per corner, two primitives. Beans (X, Y, L, R): a 10-quad strip, two
     * 6-vertex cap fans and a 30-quad fringe, four primitives. */
    int bands=12*(points+8+8), fills=5*(20+14+24+16+12+4*4), beans=4*(4*10+2*6+4*30);
    indigoPadFrame_t rest={true,0,0,0,0,0u};
    reset(false); drawController(&r,0.0f,false,&rest);
    CHECK(count==bands+fills+beans && begins==3+2*9+4*4 && matrixLoads==2,
        "controller budget: a part is hidden, overlapping or wound backwards");
    CHECK(culling==GX_CULL_BACK && memcmp(loaded,r.model,sizeof(Mtx))==0,"controller state restore");
    /* Every vertex a stroke emits is shared: a round end fans from the strip's
     * corners, so it has no vertex of its own in the middle of the edge it
     * shares with the strip (a T-junction, which sparkles on a console). */
    for(int i=0;i<count;i++) {
        int same=0;
        for(int j=0;j<count && same<2;j++) same+=memcmp(&positions[i],&positions[j],sizeof(guVector))==0;
        CHECK(same>1,"a stroke vertex belongs to one primitive alone");
    }
    int front=count;
    memcpy(neutral,positions,sizeof(guVector)*front);
    memcpy(neutralAlphas,alphas,front);
    /* The main stick moves its cap and nothing else, in the stick's direction. */
    indigoPadFrame_t left={true,-80,0,0,0,0u};
    reset(false); drawController(&r,0.0f,false,&left);
    CHECK(count==front && moved(neutral,front)==20*5,"main stick must move exactly its cap");
    float shift=0;
    for(int i=0;i<front;i++) shift+=positions[i].x-neutral[i].x;
    CHECK(shift<0,"stick left moved the cap the wrong way");
    indigoPadFrame_t cUp={true,0,0,0,72,0u};
    reset(false); drawController(&r,0.0f,false,&cUp);
    CHECK(count==front && moved(neutral,front)==14*5,"C-stick must move exactly its cap");
    /* A press shrinks and brightens A alone. */
    indigoPadFrame_t a={true,0,0,0,0,PAD_BUTTON_A};
    reset(false); drawController(&r,0.0f,false,&a);
    CHECK(count==front && moved(neutral,front)==24*5,"A press must change exactly the A button");
    /* Its fan and the inner edge of its fringe brighten; the outer fringe stays clear. */
    CHECK(brighter(neutralAlphas,front)==24*3,"pressed A does not brighten");
    /* A held D-pad arm brightens in place. */
    indigoPadFrame_t right={true,0,0,0,0,PAD_BUTTON_RIGHT};
    reset(false); drawController(&r,0.0f,false,&right);
    CHECK(count==front && moved(neutral,front)==0 && brighter(neutralAlphas,front)==4*3,
        "held D-pad arm must brighten without moving");
    /* Idle play waits out the hold after live input, then moves on its own. */
    reset(false); drawController(&r,1.0f,true,&rest);
    CHECK(count==front && moved(neutral,front)==0,"idle play started inside the live-input hold");
    reset(false); drawController(&r,3.9f,true,&rest);
    CHECK(count==front && moved(neutral,front)>0,"idle play missing after the hold");
    reset(false); drawController(&r,3.9f,false,&rest);
    CHECK(count==front && moved(neutral,front)==0,"reduced motion kept idle play");
    reset(false); drawController(&r,9.9f,true,NULL);
    CHECK(count==front && moved(neutral,front)>0,"boot overlay (no pad) lost idle play");
    /* Facing away, the emblem draws nothing at all. */
    float yaw=INDIGO_TAU*0.5f;
    r.model[0][0]=cosf(yaw); r.model[0][2]=sinf(yaw); r.model[2][0]=-sinf(yaw); r.model[2][2]=cosf(yaw);
    reset(false); drawController(&r,0.0f,false,&rest);
    CHECK(count==0 && begins==0,"back-facing controller drew");
}
/* The sharpest corner, in degrees on screen, of the band that opens the
 * stream: its centre line runs between the two sides of each solid quad. */
static float sharpestBandTurn(int points) {
    float worst=0;
    for(int i=0;i<points;i++) {
        float x[3],y[3];
        for(int k=0;k<3;k++) {
            int at=12*((i+k+points-1)%points);
            float depth=-(positions[at].z+positions[at+1].z)*0.5f;
            x[k]=(positions[at].x+positions[at+1].x)*0.5f/depth;
            y[k]=(positions[at].y+positions[at+1].y)*0.5f/depth;
        }
        float ax=x[1]-x[0],ay=y[1]-y[0],bx=x[2]-x[1],by=y[2]-y[1];
        float turn=acosf(fmaxf(-1.0f,fminf(1.0f,(ax*bx+ay*by)/sqrtf((ax*ax+ay*ay)*(bx*bx+by*by)))));
        if(turn>worst) worst=turn;
    }
    return worst*360.0f/INDIGO_TAU;
}
/* roundedOutline keeps every turn under drawFaceBand's 45 degrees, also
 * where a pill's two rounded ends meet, and never repeats a point. */
static void test_rounded_outlines(void) {
    const indigoPoint_t square[4]={{-0.27f,0.27f},{0.27f,0.27f},{0.27f,-0.27f},{-0.27f,-0.27f}};
    const indigoPoint_t pill[4]={{-0.36f,0.36f},{0.36f,0.36f},{0.36f,0.08f},{-0.36f,0.08f}};
    const indigoPoint_t card[5]={{-0.28f,0.36f},{0.12f,0.36f},{0.28f,0.20f},{0.28f,-0.36f},{-0.28f,-0.36f}};
    const indigoPoint_t folder[6]={{-0.42f,0.30f},{-0.12f,0.30f},{-0.04f,0.20f},{0.42f,0.20f},
        {0.42f,-0.30f},{-0.42f,-0.30f}};
    const struct { const indigoPoint_t *corners; int count; float radius; } shapes[]={
        {square,4,0.045f},{pill,4,0.14f},{card,5,0.04f},{folder,6,0.04f}};
    for(unsigned k=0;k<sizeof(shapes)/sizeof(shapes[0]);k++) {
        indigoPoint_t out[FACE_BAND_MAX];
        int n=roundedOutline(shapes[k].corners,shapes[k].count,shapes[k].radius,out,FACE_BAND_MAX);
        CHECK(n>=3,"a rounded outline did not fit");
        for(int i=0;i<n;i++) {
            indigoPoint_t a=out[(i+n-1)%n],b=out[i],c=out[(i+1)%n];
            float ax=b.x-a.x,ay=b.y-a.y,bx=c.x-b.x,by=c.y-b.y;
            float al=sqrtf(ax*ax+ay*ay),bl=sqrtf(bx*bx+by*by);
            CHECK(al>1e-4f && bl>1e-4f,"a rounded outline repeats a point");
            float turn=acosf(fmaxf(-1.0f,fminf(1.0f,(ax*bx+ay*by)/(al*bl))))*360.0f/INDIGO_TAU;
            CHECK(turn<45.0f,"a rounded outline turns too sharply for a band");
        }
    }
    indigoPoint_t tiny[2];
    CHECK(roundedOutline(card,5,0.04f,tiny,2)==0,"an outline overran its buffer");
}
/* Each face shows its own four icons (choice 0-3) on that face and nowhere
 * else. Turned to the camera the whole icon draws, every band and polygon
 * of it; turned away nothing does; quad icons keep their budget at any
 * angle; every icon glows in the same shade. */
static void test_icons_on_their_faces(void) {
    /* The GX primitives each icon draws when its face looks at the camera. */
    static const int primitives[UI_HOME_ICON_COUNT]={
        [UI_HOME_ICON_CONTROLLER]=3+2*9+4*4,[UI_HOME_ICON_BOOKS]=1,[UI_HOME_ICON_COVERS]=3+2,
        [UI_HOME_ICON_PLAY]=1+2,[UI_HOME_ICON_HUB]=1,[UI_HOME_ICON_DISC]=2+2*4,
        [UI_HOME_ICON_SD_CARD]=1+4*2,[UI_HOME_ICON_FOLDER]=1+2,[UI_HOME_ICON_SLIDERS]=1,
        [UI_HOME_ICON_GEAR]=2,[UI_HOME_ICON_TOGGLES]=2+2*2,[UI_HOME_ICON_DIAL]=1+2+7*2,
        [UI_HOME_ICON_CLOCK]=1,[UI_HOME_ICON_INFO]=1+2+2,[UI_HOME_ICON_POWER]=4+2,
        [UI_HOME_ICON_CHIP]=1+2+2+12*2,[UI_HOME_ICON_APPS]=4,
        [UI_HOME_ICON_SAVES]=1+2+1,[UI_HOME_ICON_EMULATORS]=1+2*2+2*2,
        [UI_HOME_ICON_FILES]=1+2};
    static const int quadBudget[UI_HOME_ICON_COUNT]={
        [UI_HOME_ICON_BOOKS]=260,[UI_HOME_ICON_HUB]=180,[UI_HOME_ICON_SLIDERS]=120,[UI_HOME_ICON_CLOCK]=220};
    /* Apps', Memory Cards', Emulators' and File Browser's own icons are
     * drawn alone, on the front. */
    const Mtx semanticFaces[UI_HOME_FACE_COUNT]={
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{0,0,1,0},{0,1,0,0},{-1,0,0,0}},
        {{-1,0,0,0},{0,1,0,0},{0,0,-1,0}},
        {{0,0,-1,0},{0,1,0,0},{1,0,0,0}},
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}}
    };
    uiClockFrame_t clock={true,0,1,1,0,0.70710678f,0.70710678f};
    indigoPadFrame_t pad={true,0,0,0,0,0u};
    cubeRasterTransform_t r;
    GXColor shade={0,0,0,0};
    bool shaded=false;
    memcpy(r.semanticFaces,semanticFaces,sizeof(semanticFaces)); lightAll(&r);
    r.scaleX=r.scaleY=625.221f;
    for(int face=0;face<UI_HOME_FACE_COUNT;face++) for(int choice=0;choice<UI_HOME_ICON_CHOICES;choice++) {
        int icon=face*UI_HOME_ICON_CHOICES+choice;
        int choices[UI_HOME_FACE_COUNT]={-1,-1,-1,-1,-1,-1,-1,-1};
        /* Apps, Memory Cards, Emulators and File Browser have one icon
         * each. */
        if(icon>=UI_HOME_ICON_COUNT || primitives[icon]==0) continue;
        float nx=semanticFaces[face][0][2],nz=semanticFaces[face][2][2];
        bool shown=false;
        choices[face]=choice;
        for(int angle=0;angle<360;angle+=15) {
            float yaw=angle*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw),toward=-s*nx+c*nz;
            guMtxIdentity(r.model);
            r.model[0][0]=c; r.model[0][2]=s; r.model[2][0]=-s; r.model[2][2]=c;
            r.model[2][3]=-5.4f;
            reset(false); drawFaceIcons(3.9f,true,&clock,&pad,choices,&r);
            CHECK(matrixLoads==2 && culling==GX_CULL_BACK && memcmp(loaded,r.model,sizeof(Mtx))==0,
                "icon pass state restore");
            if(quadBudget[icon]) CHECK((count==quadBudget[icon] && begins==1) ||
                (count==0 && begins==0),"quad icon budget moved");
            /* An icon fades out with its face, gone 71 degrees from the
             * camera; a face turned away draws nothing at all. */
            if(toward<0.3f) CHECK(count==0 && begins==0,"an icon drew on a face turned away");
            if(toward>0.99f) CHECK(begins==primitives[icon],"an icon lost a part facing the camera");
            int lit=0;
            for(int i=0;i<count;i++) {
                if(!alphas[i]) continue;
                /* Back to body space: the rotation is orthonormal. */
                float ex=positions[i].x,ey=positions[i].y,ez=positions[i].z+5.4f;
                float bx=c*ex-s*ez,by=ey,bz=s*ex+c*ez;
                float plane=bx*semanticFaces[face][0][2]+by*semanticFaces[face][1][2]+bz*nz;
                CHECK(fabsf(plane-FACE_ICON_PLANE)<0.1f,"an icon drew off its own face");
                if(!shaded) { shade=colors[i]; shaded=true; }
                CHECK(colors[i].r==shade.r && colors[i].g==shade.g && colors[i].b==shade.b,
                    "icons glow in different shades");
                ++lit;
            }
            if(toward>0.5f) { CHECK(lit>0,"an icon vanished from a face turned to the camera"); shown=true; }
            if(toward<-0.2f) CHECK(lit==0,"an icon drew on a face turned away");
        }
        CHECK(shown,"no angle showed the icon");
    }
    /* A choice outside a face's four draws nothing. */
    const int none[UI_HOME_FACE_COUNT]={UI_HOME_ICON_CHOICES,-1,1000,4,1,2,3,1};
    guMtxIdentity(r.model); r.model[2][3]=-5.4f;
    reset(false); drawFaceIcons(1.0f,true,&clock,&pad,none,&r);
    CHECK(count==0 && begins==0 && matrixLoads==2,"an out-of-range choice drew");
    /* The turning Gear and spinning Disc keep every band joint at any turn:
     * a failed join would drop the whole outline. */
    GXColor glow={196,177,255,180};
    for(int step=0;step<48;step++) for(int view=-1;view<=1;view++) {
        float yaw=view*50.0f*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw);
        guMtxIdentity(r.model);
        r.model[0][0]=c; r.model[0][2]=s; r.model[2][0]=-s; r.model[2][2]=c;
        r.model[2][3]=-5.4f;
        reset(false); drawGearIcon(&r,UI_HOME_FACE_LIBRARY,glow,step*INDIGO_TAU/48);
        CHECK(count==12*(78+16) && begins==2,"gear outline lost a joint");
        /* drawFaceBand folds a short-sided band at corners past 45 degrees. */
        if(view==0) CHECK(sharpestBandTurn(78)<45.0f,"gear teeth turn too sharply for a band");
        reset(false); drawDiscIcon(&r,UI_HOME_FACE_LIBRARY,glow,step*INDIGO_TAU/48);
        CHECK(count==12*(32+16)+2*(4*10+2*6+4*30) && begins==2+2*4,"disc lost a ring or glint");
    }
}
static void surface_pose(cubeRasterTransform_t *r,float yaw,float pitch,float roll,float scale) {
    float cy=cosf(yaw),sy=sinf(yaw),cx=cosf(pitch),sx=sinf(pitch),cz=cosf(roll),sz=sinf(roll);
    r->scaleX=r->scaleY=625.221f;
    guMtxIdentity(r->model);
    r->model[0][0]=cz*cy*scale; r->model[0][1]=(cz*sy*sx-sz*cx)*scale;
    r->model[0][2]=(cz*sy*cx+sz*sx)*scale;
    r->model[1][0]=sz*cy*scale; r->model[1][1]=(sz*sy*sx+cz*cx)*scale;
    r->model[1][2]=(sz*sy*cx-cz*sx)*scale;
    r->model[2][0]=-sy*scale; r->model[2][1]=cy*sx*scale; r->model[2][2]=cy*cx*scale;
    r->model[0][3]=0.25f; r->model[1][3]=0.1f; r->model[2][3]=-5.4f;
}
static indigoPoint_t projected(const cubeRasterTransform_t *r,guVector p) {
    return (indigoPoint_t){r->scaleX*p.x/-p.z,r->scaleY*p.y/-p.z};
}
/* How far p lies from the line through a and b, in frame pixels. */
static float framePixels(indigoPoint_t a,indigoPoint_t b,indigoPoint_t p) {
    a.x*=squeeze; b.x*=squeeze; p.x*=squeeze;
    return fabsf((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/hypotf(b.x-a.x,b.y-a.y);
}
static void check_outline(const cubeRasterTransform_t *r,const cubeSurfaceQuad_t *faces,
    const cubeOutline_t *outline) {
    CHECK(outline->count>=3 && outline->count<=24,"invalid projected outline budget");
    for(int i=0;i<outline->count;i++) {
        indigoPoint_t a=outline->point[i],b=outline->point[(i+1)%outline->count];
        CHECK(outlineCross(a,b,outline->point[(i+2)%outline->count])>0,"outline is not convex CCW");
        float length=hypotf(b.x-a.x,b.y-a.y);
        for(int face=0;face<6;face++) for(int vertex=0;vertex<4;vertex++) {
            guVector eye,p=faces[face].point[vertex]; indigoPoint_t screen;
            CHECK(projectRailPoint(r,p.x,p.y,p.z,&eye,&screen),"valid outline source failed projection");
            CHECK(outlineCross(a,b,screen)/length > -0.006f,"outline cut into a solid fill");
        }
    }
    indigoPoint_t center={0,0},outward[2];
    for(int i=0;i<outline->count;i++) {
        center.x+=outline->point[i].x/outline->count;
        center.y+=outline->point[i].y/outline->count;
    }
    CHECK(!cubeOutlineEdge(outline,center,(indigoPoint_t){center.x+0.1f,center.y},outward),
        "interior edge was classified as silhouette");
}
static void check_surface_vertices(const cubeRasterTransform_t *r,const cubeOutline_t *outline,
    const cubeSurfaceQuad_t *quads,int quadsCount,int vertexCount,int cull,bool *covered) {
    reset(false); surfaceTest=true; depthWrites=GX_FALSE;
    if(vertexCount==4) drawCubeSurfacePass(r,outline,quads,quadsCount,cull);
    else drawCubeSurfacePassVertices(r,outline,quads,quadsCount,vertexCount,cull);
    CHECK(firstPrimitive==(vertexCount==3?GX_TRIANGLES:GX_QUADS),"surface primitive type");
    CHECK(count>=quadsCount*vertexCount && count<=quadsCount*vertexCount*5 && begins>=1 && begins<=2,
        "surface vertex/draw budget");
    CHECK(culling==cull && depthWrites==GX_FALSE &&
        memcmp(loaded,r->model,sizeof(Mtx))==0,"surface state not restored");
    for(int i=0;i<quadsCount*vertexCount;i++) {
        guVector p=quads[i/vertexCount].point[i%vertexCount]; GXColor c=quads[i/vertexCount].color[i%vertexCount];
        CHECK(positions[i].x==p.x && positions[i].y==p.y && positions[i].z==p.z,
            "original fill vertex changed");
        CHECK(memcmp(&colors[i],&c,sizeof(c))==0,"original face gradient changed");
    }
    for(int i=quadsCount*vertexCount;i<count;i+=4) {
        indigoPoint_t a=projected(r,positions[i]),b=projected(r,positions[i+1]);
        indigoPoint_t outsideA=projected(r,positions[i+3]),outsideB=projected(r,positions[i+2]);
        CHECK(alphas[i]>0 && alphas[i+1]>0 && alphas[i+2]==0 && alphas[i+3]==0,
            "solid silhouette did not fade to transparent");
        CHECK(positions[i].z==positions[i+3].z && positions[i+1].z==positions[i+2].z,
            "silhouette coverage changed endpoint depth");
        CHECK(closef(framePixels(a,b,outsideA),1.0f) && closef(framePixels(a,b,outsideB),1.0f),
            "solid edge coverage is not one frame pixel");
        CHECK(hypotf((outsideA.x-a.x)*squeeze,outsideA.y-a.y)<=4.01f &&
            hypotf((outsideB.x-b.x)*squeeze,outsideB.y-b.y)<=4.01f,"outline miter spike");
        bool boundary=false;
        for(int edge=0;edge<outline->count;edge++) {
            indigoPoint_t p=outline->point[edge],q=outline->point[(edge+1)%outline->count];
            float extent=hypotf(q.x-p.x,q.y-p.y);
            if(fabsf(outlineCross(p,q,a))/extent>0.006f ||
               fabsf(outlineCross(p,q,b))/extent>0.006f) continue;
            CHECK(outlineCross(p,q,outsideA)<0 && outlineCross(p,q,outsideB)<0,
                "coverage expanded into the solid instead of outward");
            covered[edge]=true; boundary=true;
        }
        CHECK(boundary,"shared internal mesh edge received a fringe");
        /* Independent eye-space facing check: a fringe must come from a
         * face rendered in this pass, with both original endpoint colors. */
        bool source=false;
        for(int q=0;q<quadsCount;q++) {
            guVector eye[4]; indigoPoint_t screen;
            for(int v=0;v<vertexCount;v++) {
                guVector p=quads[q].point[v];
                CHECK(projectRailPoint(r,p.x,p.y,p.z,&eye[v],&screen),"source projection");
            }
            guVector u={eye[1].x-eye[0].x,eye[1].y-eye[0].y,eye[1].z-eye[0].z};
            guVector v={eye[2].x-eye[0].x,eye[2].y-eye[0].y,eye[2].z-eye[0].z};
            guVector normal={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            float facing=normal.x*eye[0].x+normal.y*eye[0].y+normal.z*eye[0].z;
            if((cull==GX_CULL_BACK && facing<=0) ||
               (cull==GX_CULL_FRONT && facing>=0)) continue;
            for(int edge=0;edge<vertexCount;edge++) {
                int next=(edge+1)%vertexCount;
                if(memcmp(&positions[i],&eye[edge],sizeof(guVector)) ||
                   memcmp(&positions[i+1],&eye[next],sizeof(guVector))) continue;
                CHECK(!memcmp(&colors[i],&quads[q].color[edge],sizeof(GXColor)) &&
                    !memcmp(&colors[i+1],&quads[q].color[next],sizeof(GXColor)),
                    "silhouette lost its source face gradient");
                source=true;
            }
        }
        CHECK(source,"silhouette came from a culled face");
    }
}
static void check_surface_pass(const cubeRasterTransform_t *r,const cubeOutline_t *outline,
    const cubeSurfaceQuad_t *quads,int quadsCount,int cull,bool *covered) {
    check_surface_vertices(r,outline,quads,quadsCount,4,cull,covered);
}
static int compare_point(guVector a,guVector b) {
    if(a.x!=b.x) return a.x<b.x?-1:1;
    if(a.y!=b.y) return a.y<b.y?-1:1;
    if(a.z!=b.z) return a.z<b.z?-1:1;
    return 0;
}
/* The Home faces' bases, and a turn about the vertical axis, scaled. */
static const Mtx homeFaces[UI_HOME_FACE_COUNT]={
    {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
    {{0,0,1,0},{0,1,0,0},{-1,0,0,0}},
    {{-1,0,0,0},{0,1,0,0},{0,0,-1,0}},
    {{0,0,-1,0},{0,1,0,0},{1,0,0,0}},
    {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
    {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
    {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
    {{1,0,0,0},{0,1,0,0},{0,0,1,0}}
};
static void turnTo(cubeRasterTransform_t *r,float degrees,float scale) {
    float yaw=degrees*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw);
    guMtxIdentity(r->model);
    r->model[0][0]=c*scale; r->model[0][2]=s*scale; r->model[1][1]=scale;
    r->model[2][0]=-s*scale; r->model[2][2]=c*scale; r->model[2][3]=-5.4f;
}
/* A face square on lifts its icon FACE_ICON_LIFT off FACE_ICON_PLANE, so it
 * floats clear of the glass (1.0), some five pixels at Home's size; a face
 * turned 60 degrees, as side faces rest, keeps its icon at FACE_ICON_PLANE,
 * close to the glass. Read at each Books quad's core, whose half-pixel inset
 * cancels across its corners. */
static void test_lift(void) {
    const int books[UI_HOME_FACE_COUNT]={1,-1,-1,-1,-1,-1,-1,-1};
    uiClockFrame_t clock={true,0,1,1,0,0.70710678f,0.70710678f};
    cubeRasterTransform_t r;
    memcpy(r.semanticFaces,homeFaces,sizeof(homeFaces)); lightAll(&r);
    r.scaleX=r.scaleY=625.221f;
    for(int angle=0;angle<=60;angle+=60) {
        float yaw=angle*INDIGO_TAU/360,c=cosf(yaw),s=sinf(yaw);
        float want=angle==0?FACE_ICON_PLANE+FACE_ICON_LIFT:FACE_ICON_PLANE;
        int quads=0;
        CHECK(angle==0?want>1.03f:want<1.02f,
            angle?"a side face's icon stands off the glass":"a front face's icon lies on the glass");
        turnTo(&r,(float)angle,1);
        reset(false); drawFaceIcons(1.0f,false,&clock,NULL,books,&r);
        CHECK(count==260,"Books did not draw");
        for(int i=0;i<count;i+=20) {
            float x=0,z=0;
            if(!alphas[i]) continue;
            for(int k=0;k<4;k++) { x+=positions[i+k].x/4; z+=(positions[i+k].z+5.4f)/4; }
            /* Back to body space, along the Library face's normal (z). */
            CHECK(fabsf(s*x+c*z-want)<(angle?2e-3f:1e-4f),
                angle?"a turned face lifted its icon":"a face square on did not lift its icon");
            ++quads;
        }
        CHECK(quads==13,"a Books quad went missing");
    }
}
/* Where a stream lands on screen: its alpha-weighted centre, its strongest
 * alpha and the width of its first quad's fade, from a core corner out to
 * the fringe's clear edge. */
static void streamSpread(const cubeRasterTransform_t *r,float *x,float *y,int *peak,float *fade) {
    float weight=0;
    *x=*y=0; *peak=0;
    for(int i=0;i<count;i++) {
        indigoPoint_t p=projected(r,positions[i]);
        *x+=p.x*alphas[i]; *y+=p.y*alphas[i]; weight+=alphas[i];
        if(alphas[i]>*peak) *peak=alphas[i];
    }
    *x/=weight; *y/=weight;
    indigoPoint_t core=projected(r,positions[4]),edge=projected(r,positions[5]);
    *fade=hypotf(edge.x-core.x,edge.y-core.y);
}
/* A face square on casts its icon's shadow onto the glass: down and to the
 * left, away from the light, a few pixels at Home's size, three times as
 * soft as the icon and well under its strength, multiplied into the frame.
 * Faces turned away cast none, and nothing draws at all while the scene
 * keeps its glass plain (strength 0). */
static void test_shadows(void) {
    /* Books on Library, square on; the faces either side are edge-on. */
    const int choices[UI_HOME_FACE_COUNT]={1,0,0,0,-1,-1,-1,-1};
    const int books[UI_HOME_FACE_COUNT]={1,-1,-1,-1,-1,-1,-1,-1};
    uiClockFrame_t clock={true,0,1,1,0,0.70710678f,0.70710678f};
    indigoPadFrame_t pad={true,0,0,0,0,0u};
    cubeRasterTransform_t r;
    float ix,iy,sx,sy,iFade,sFade;
    int iPeak,sPeak,halfPeak;
    memcpy(r.semanticFaces,homeFaces,sizeof(homeFaces)); lightAll(&r);
    r.scaleX=r.scaleY=625.221f;
    turnTo(&r,0,0.92f);
    reset(false); drawFaceIcons(1.0f,false,&clock,&pad,choices,&r);
    int icon=count;
    CHECK(icon==260 && begins==1,"Books alone should draw");
    streamSpread(&r,&ix,&iy,&iPeak,&iFade);
    reset(false); drawFaceShadows(1.0f,false,&clock,&pad,choices,&r,1.0f);
    CHECK(count==icon && begins==1,"a face turned away cast a shadow, or the front none");
    CHECK(litBegins==0,"the shadow adds light instead of multiplying");
    CHECK(matrixLoads==2 && culling==GX_CULL_BACK && memcmp(loaded,r.model,sizeof(Mtx))==0 &&
        blendSource==GX_BL_SRCALPHA && blendDestination==GX_BL_INVSRCALPHA,"shadow state restore");
    streamSpread(&r,&sx,&sy,&sPeak,&sFade);
    /* On the glass (1.0), under the lifted icon: square on, every vertex
     * keeps its plane's depth. */
    for(int i=0;i<count;i++)
        CHECK(!alphas[i] || fabsf(positions[i].z-(0.92f*1.0f-5.4f))<1e-4f,
            "the shadow is not on the glass");
    /* y is up on this screen. */
    CHECK(sx-ix<-1.5f && sy-iy<-1.5f && hypotf(sx-ix,sy-iy)<8.0f,
        "the shadow does not fall down and to the left of its icon");
    /* Subtle but seen: 0.2 to 0.3 of the icon's strength. */
    CHECK(sPeak<=0.3f*iPeak+1 && sPeak>=0.2f*iPeak,"the shadow is too strong or too faint");
    CHECK(sFade>2.5f*iFade && sFade<3.5f*iFade,"the shadow is not soft");
    /* Across its fade (the icon's is one pixel) it darkens the glass under
     * 6 % more each pixel. The glass round an icon is some 60 to 70 of 255:
     * a steeper fade steps it by 4 or more a pixel, bands a pixel wide that
     * read as pixelation in its reflection. */
    CHECK((float)sPeak/255.0f/(sFade/iFade)<0.06f,
        "the shadow's fade steps the glass a pixel at a time");
    reset(false); drawFaceShadows(1.0f,false,&clock,&pad,choices,&r,0.5f);
    streamSpread(&r,&sx,&sy,&halfPeak,&sFade);
    CHECK(abs(2*halfPeak-sPeak)<=2,"the shadow does not follow the glass light's strength");
    reset(false); drawFaceShadows(1.0f,false,&clock,&pad,choices,&r,0.0f);
    CHECK(count==0 && begins==0 && matrixLoads==0,"a shadow drew on plain glass");
    /* Turned 45 degrees its strokes are whole but its icon is down near the
     * glass, so the shadow lies close under it, under a pixel's drop. */
    turnTo(&r,45,0.92f);
    reset(false); drawFaceIcons(1.0f,false,&clock,&pad,books,&r);
    streamSpread(&r,&ix,&iy,&iPeak,&iFade);
    reset(false); drawFaceShadows(1.0f,false,&clock,&pad,books,&r,1.0f);
    CHECK(count==260,"a face turned 45 degrees cast no shadow");
    streamSpread(&r,&sx,&sy,&sPeak,&sFade);
    CHECK(sy-iy<0.0f && sy-iy>-1.5f,"an icon at rest casts its shadow as far as a lifted one");
    /* Turned 75 degrees its strokes are gone, and so is its shadow. */
    turnTo(&r,75,0.92f);
    reset(false); drawFaceShadows(1.0f,false,&clock,&pad,books,&r,1.0f);
    CHECK(count==0 && begins==0,"a face turned edge-on cast a shadow");
}
static void test_hidden_controller_reads_the_pad(void) {
    /* Library's face turns away while A is held, then back with the pad at
     * rest: the press was seen, so the emblem has not begun its idle play
     * and draws at rest, exactly as with motion off. */
    static guVector live[4096];
    cubeRasterTransform_t r;
    const Mtx semanticFaces[UI_HOME_FACE_COUNT]={
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{0,0,1,0},{0,1,0,0},{-1,0,0,0}},
        {{-1,0,0,0},{0,1,0,0},{0,0,-1,0}},
        {{0,0,-1,0},{0,1,0,0},{1,0,0,0}},
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}}
    };
    uiClockFrame_t clock={true,0,1,1,0,0.70710678f,0.70710678f};
    indigoPadFrame_t held={true,0,0,0,0,PAD_BUTTON_A},rest={true,0,0,0,0,0u};
    int choices[UI_HOME_FACE_COUNT]={0,-1,-1,-1,-1,-1,-1,-1};
    memcpy(r.semanticFaces,semanticFaces,sizeof(semanticFaces)); lightAll(&r);
    r.scaleX=r.scaleY=625.221f;
    guMtxIdentity(r.model); r.model[0][0]=r.model[2][2]=-1; r.model[2][3]=-5.4f;
    reset(false); drawFaceIcons(100.0f,true,&clock,&held,choices,&r);
    CHECK(begins==0,"the controller drew on a face turned away");
    guMtxIdentity(r.model); r.model[2][3]=-5.4f;
    reset(false); drawFaceIcons(100.5f,true,&clock,&rest,choices,&r);
    int n=count;
    CHECK(n>0,"the controller did not draw facing the camera");
    memcpy(live,positions,sizeof(guVector)*(size_t)n);
    reset(false); drawFaceIcons(100.5f,false,&clock,&rest,choices,&r);
    CHECK(count==n && memcmp(live,positions,sizeof(guVector)*(size_t)n)==0,
        "the controller missed a press while its face was turned away");
}
static void test_controller_across_the_clock_wrap(void) {
    /* The animation clock wraps to 0 every 2000 pi s. The stick, held until
     * just before the wrap, is still released 3.5 s later after it: idle
     * play is back. And the presses' cycle goes on across the wrap: the
     * cycle before it plays every press where the first cycle does. */
    static const struct { u32 button; float at; } presses[]={
        {PAD_BUTTON_A,0.43f},{PAD_BUTTON_B,1.31f},{PAD_BUTTON_Y,2.31f},
        {PAD_BUTTON_X,2.91f},{PAD_BUTTON_RIGHT,3.9f},{PAD_BUTTON_DOWN,4.3f},
        {PAD_BUTTON_START,5.21f}};
    indigoPadFrame_t held={true,60,0,0,0,0u},rest={true,0,0,0,0,0u};
    controllerIdle_t idle={0.0f,false,0.0f,0.0f};
    controllerPose_t pose;
    controllerPose(&held,UI_ANIM_TIME_WRAP_SECONDS-0.5f,true,&idle,&pose);
    controllerPose(&rest,3.0f,true,&idle,&pose);
    CHECK(pose.stickX!=0.0f || pose.stickY!=0.0f,"idle play stopped at the clock's wrap");
    for(unsigned i=0;i<sizeof(presses)/sizeof(presses[0]);i++) {
        controllerPose(&rest,UI_ANIM_TIME_WRAP_SECONDS-CONTROLLER_PRESS_CYCLE+presses[i].at,
            true,&idle,&pose);
        CHECK(pose.pressed&presses[i].button,"the presses jump at the clock's wrap");
    }
}
static void test_controller_gives_way_to_the_hand(void) {
    /* Idle play hands the sticks to live input over CONTROLLER_IDLE_RELEASE:
     * the idle sway shrinks a frame at a time, never in one, and the idle
     * presses stop at once. Not animated, there is none to give way. */
    indigoPadFrame_t rest={true,0,0,0,0,0u},pressing={true,0,0,0,0,PAD_BUTTON_B};
    controllerIdle_t idle={0.0f,false,0.0f,0.0f};
    controllerPose_t pose;
    float t=100.0f, previous=1.0f;
    controllerPose(&rest,t,true,&idle,&pose);
    float sway=fabsf(pose.stickX)+fabsf(pose.stickY)+fabsf(pose.substickX)+fabsf(pose.substickY);
    CHECK(sway>0.1f,"no idle play to give way");
    for(int frame=1;frame<=12;frame++) {
        t+=1.0f/60.0f;
        controllerPose(&pressing,t,true,&idle,&pose);
        float share=(fabsf(pose.stickX)+fabsf(pose.stickY)+fabsf(pose.substickX)+
            fabsf(pose.substickY))/(.38f*fabsf(sinf(t*.8f))+.32f*fabsf(sinf(t*1.1f+.6f))+
            .34f*fabsf(sinf(t*1.3f+2.0f))+.30f*fabsf(cosf(t*.9f)));
        CHECK(pose.pressed==PAD_BUTTON_B,"idle presses kept on under the hand");
        CHECK(share<=previous+1e-4f && previous-share<.2f,"idle play left the sticks in one frame");
        CHECK(frame<8 || share==0.0f,"idle play kept the sticks past its release");
        previous=share;
    }
    idle=(controllerIdle_t){0.0f,false,0.0f,0.0f};
    controllerPose(&rest,t,true,&idle,&pose);
    controllerPose(&pressing,t+1.0f/60.0f,false,&idle,&pose);
    CHECK(pose.stickX==0.0f && pose.substickY==0.0f,"idle play lingered with animation off");
}
static void test_closed_cube(void) {
    cubeSurfaceQuad_t mesh[26];
    const GXColor color[6]={{19,15,54,255},{28,20,76,255},{50,36,111,255},
        {24,18,64,255},{76,59,139,255},{44,31,102,255}};
    struct {guVector a,b;int uses,balance;} edges[96];
    guVector vertices[96]; int edgeCount=0,vertexCount=0;
    memset(mesh,0,sizeof(mesh)); memset(edges,0,sizeof(edges));
    buildCubeFaces(mesh,1,.78f,color);
    buildChamferStrips(mesh+6,1,.78f,color,NULL); buildCubeCorners(mesh+18,1,.78f);
    for(int f=0;f<26;f++) {
        int sides=f<18?4:3;
        guVector p=mesh[f].point[0],a=mesh[f].point[1],b=mesh[f].point[2];
        a=(guVector){a.x-p.x,a.y-p.y,a.z-p.z};
        b=(guVector){b.x-p.x,b.y-p.y,b.z-p.z};
        guVector normal={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        CHECK(normal.x*p.x+normal.y*p.y+normal.z*p.z<0,"closed mesh winding/degenerate face");
        for(int v=0;v<sides;v++) {
            guVector from=mesh[f].point[v],to=mesh[f].point[(v+1)%sides];
            int point=0;
            for(;point<vertexCount;point++) if(!compare_point(vertices[point],from)) break;
            if(point==vertexCount) vertices[vertexCount++]=from;
            int direction=compare_point(from,to);
            CHECK(direction!=0,"closed mesh zero-length edge");
            if(direction>0) {guVector swap=from;from=to;to=swap;}
            int edge=0;
            for(;edge<edgeCount;edge++) if(!compare_point(edges[edge].a,from)&&
                !compare_point(edges[edge].b,to)) break;
            if(edge==edgeCount) {edges[edgeCount].a=from;edges[edgeCount++].b=to;}
            edges[edge].uses++;edges[edge].balance+=direction;
        }
    }
    CHECK(vertexCount==24&&edgeCount==48&&vertexCount-edgeCount+26==2,
        "sealed cube changed geometry or topology");
    for(int edge=0;edge<edgeCount;edge++) CHECK(edges[edge].uses==2&&edges[edge].balance==0,
        "cube has an open boundary, overlapping face, or inconsistent winding");
}
/* The face a point lies on (cubeFaceAxes order), or -1 inside a bevel. */
static int faceOfPoint(guVector p) {
    if(p.z==-1) return 0;
    if(p.x==-1) return 1;
    if(p.x==1) return 2;
    if(p.y==-1) return 3;
    if(p.y==1) return 4;
    return p.z==1?5:-1;
}
static bool sameColor(GXColor a,GXColor b) { return !memcmp(&a,&b,sizeof(a)); }
/* The tint passes' mesh: faces, banded bevels and fanned corners close with
 * no gap or flipped polygon, and every seam carries one colour on both
 * sides, so the frame buffer has no colour step to stair. Points on a face
 * take its tint; a bevel's inner rows take the edge tints. */
static void test_seamless_mesh(void) {
    static cubeSurfaceQuad_t mesh[6+36+72];
    static struct {guVector a,b;GXColor ca,cb;int uses,balance;} edges[256];
    static guVector vertices[128];
    GXColor face[6],edge[6];
    int edgeCount=0,vertexCount=0;
    for(int f=0;f<6;f++) {
        face[f]=(GXColor){(u8)(10+f),20,30,(u8)(40+f)};
        edge[f]=(GXColor){(u8)(100+f),120,140,(u8)(160+f)};
    }
    buildCubeFaces(mesh,1,.78f,face);
    buildChamferStrips(mesh+6,1,.78f,edge,face);
    buildCornerFans(mesh+42,1,.78f,edge,face);
    for(int f=0;f<114;f++) {
        int sides=f<42?4:3;
        guVector p=mesh[f].point[0],a=mesh[f].point[1],b=mesh[f].point[2];
        a=(guVector){a.x-p.x,a.y-p.y,a.z-p.z};
        b=(guVector){b.x-p.x,b.y-p.y,b.z-p.z};
        guVector normal={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        CHECK(normal.x*p.x+normal.y*p.y+normal.z*p.z<0,"seamless mesh winding/degenerate polygon");
        for(int v=0;v<sides;v++) {
            guVector from=mesh[f].point[v],to=mesh[f].point[(v+1)%sides];
            GXColor cFrom=mesh[f].color[v],cTo=mesh[f].color[(v+1)%sides];
            int on=faceOfPoint(from),point=0;
            if(on>=0) CHECK(sameColor(cFrom,face[on]),"a point on a face lost the face's tint");
            else if(f<42) {
                int tinted=0;
                for(int g=0;g<6;g++) tinted+=sameColor(cFrom,edge[g]);
                CHECK(tinted==1,"a bevel's inner row lost its edge tint");
            }
            for(;point<vertexCount;point++) if(!compare_point(vertices[point],from)) break;
            if(point==vertexCount) { CHECK(vertexCount<128,"seamless mesh vertex budget"); vertices[vertexCount++]=from; }
            int direction=compare_point(from,to);
            CHECK(direction!=0,"seamless mesh zero-length edge");
            if(direction>0) { guVector swap=from; GXColor tint=cFrom; from=to; to=swap; cFrom=cTo; cTo=tint; }
            int at=0;
            for(;at<edgeCount;at++) if(!compare_point(edges[at].a,from)&&!compare_point(edges[at].b,to)) break;
            if(at==edgeCount) {
                CHECK(edgeCount<256,"seamless mesh edge budget");
                edges[edgeCount].a=from; edges[edgeCount].b=to;
                edges[edgeCount].ca=cFrom; edges[edgeCount++].cb=cTo;
            }
            else CHECK(sameColor(edges[at].ca,cFrom)&&sameColor(edges[at].cb,cTo),
                "a seam changes colour from one side to the other");
            edges[at].uses++; edges[at].balance+=direction;
        }
    }
    CHECK(vertexCount==80&&edgeCount==192&&vertexCount-edgeCount+114==2,
        "seamless mesh changed geometry or topology");
    for(int at=0;at<edgeCount;at++) CHECK(edges[at].uses==2&&edges[at].balance==0,
        "seamless mesh has an open seam, an overlap or a flipped polygon");
}
static void test_chamfer_color_pairs(void) {
    const guVector points[4]={{-1,.78f,-.78f},{-1,.78f,.78f},
        {-.78f,1,.78f},{-.78f,1,-.78f}};
    const GXColor outer={132,108,218,84},inner={196,180,255,112};
    for(int reverse=0;reverse<2;reverse++) {
        guVector p[4]; cubeSurfaceQuad_t quad;
        for(int i=0;i<4;i++) p[i]=points[reverse?3-i:i];
        buildChamferStrip(&quad,p[0].x,p[0].y,p[0].z,p[1].x,p[1].y,p[1].z,
            p[2].x,p[2].y,p[2].z,p[3].x,p[3].y,p[3].z,outer,inner);
        for(int i=0;i<4;i++) {
            int original=0;
            for(;original<4;original++) {
                if(!memcmp(&quad.point[i],&p[original],sizeof(guVector))) break;
            }
            CHECK(original<4,"bevel normalization moved a corner");
            GXColor expected=original<2?outer:inner;
            CHECK(!memcmp(&quad.color[i],&expected,sizeof(GXColor)),
                "bevel winding normalization moved an endpoint color");
        }
    }
}
static void test_surfaces(void) {
    const GXColor colors[6]={{19,15,54,255},{28,20,76,255},{50,36,111,255},
        {24,18,64,255},{76,59,139,255},{44,31,102,255}};
    static cubeSurfaceQuad_t shell[6],bevels[36],fans[72];
    cubeOutline_t shellOutline;
    cubeRasterTransform_t r;
    buildCubeFaces(shell,1,.78f,colors);
    buildChamferStrips(bevels,1,.78f,colors,colors); buildCornerFans(fans,1,.78f,colors,colors);
    for(int q=0;q<36;q++) {
        guVector p=bevels[q].point[0],a=bevels[q].point[1],b=bevels[q].point[2];
        a=(guVector){a.x-p.x,a.y-p.y,a.z-p.z};
        b=(guVector){b.x-p.x,b.y-p.y,b.z-p.z};
        guVector normal={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        CHECK(normal.x*p.x+normal.y*p.y+normal.z*p.z<0,
            "bevel winding disagrees with clockwise exterior faces");
    }
    /* Every pose in 4:3, then every other one through Menu Widescreen's
     * squeeze: the silhouette still fades over exactly one frame pixel. */
    int poses=0;
    for(int wide=0;wide<2;wide++) {
        int step=wide?60:30;
        pixelWidth=wide?4.0f/3:1; squeeze=wide?.75f:1;
        for(int yaw=0;yaw<360;yaw+=step) for(int pitch=0;pitch<360;pitch+=step)
        for(int roll=0;roll<180;roll+=45) for(int size=0;size<2;size++) {
            surface_pose(&r,yaw*INDIGO_TAU/360,pitch*INDIGO_TAU/360,
                roll*INDIGO_TAU/360,size?1.3f:.65f);
            buildCubeOutline(&r,shell,&shellOutline);
            check_outline(&r,shell,&shellOutline);
            bool shellCovered[24]={false};
            check_surface_pass(&r,&shellOutline,bevels,36,GX_CULL_FRONT,shellCovered);
            check_surface_vertices(&r,&shellOutline,fans,72,3,GX_CULL_FRONT,shellCovered);
            check_surface_pass(&r,&shellOutline,shell,6,GX_CULL_FRONT,shellCovered);
            check_surface_pass(&r,&shellOutline,shell,6,GX_CULL_BACK,shellCovered);
            check_surface_pass(&r,&shellOutline,bevels,36,GX_CULL_BACK,shellCovered);
            check_surface_vertices(&r,&shellOutline,fans,72,3,GX_CULL_BACK,shellCovered);
            for(int i=0;i<shellOutline.count;i++) CHECK(shellCovered[i],"shell silhouette has a coverage gap");
            ++poses;
        }
    }
    pixelWidth=1; squeeze=1;
    CHECK(poses==1152+288,"full yaw/pitch/roll/scale sweep missing");
    guMtxIdentity(r.model); buildCubeOutline(&r,shell,&shellOutline);
    CHECK(shellOutline.count==0,"near-plane-invalid outline accepted");
}
/* What the viewer sees the glass at eye mirror: its reflectance times the
 * studio's light where its ray lands, as the GPU reads it (0..255). */
static int mirrored(const cubeRasterTransform_t *r,guVector eye,guVector n) {
    GXColor cards[GLASS_LIGHTS],c;
    float size=sqrtf(r->model[0][0]*r->model[0][0]+r->model[1][0]*r->model[1][0]+
        r->model[2][0]*r->model[2][0]),s,t;
    guVector offset={(eye.x-r->model[0][3])/size,(eye.y-r->model[1][3])/size,
        (eye.z-r->model[2][3])/size};
    int alpha=glassReflect(eye,n,offset,&s,&t),peak;
    for(int i=0;i<GLASS_LIGHTS;i++) cards[i]=glassLights[i].color;
    c=glassStudioLight(glassStudioDirection(s,t),cards);
    peak=c.r>c.g?c.r:c.g;
    if(c.b>peak) peak=c.b;
    return alpha*peak/255;
}
static void check_glass_stream(const cubeRasterTransform_t *r,const cubeSurfaceQuad_t *quads,
    int quadsCount,int vertexCount,int *total) {
    reset(true); studio=true;
    drawGlassReflection(r,quads,quadsCount,vertexCount,1.0f);
    CHECK(!active && matrixLoads==0,"reflection left a primitive open or moved the matrix");
    int sides=vertexCount==3?3:4;
    for(int i=0;i<count;i+=sides) {
        guVector eye; indigoPoint_t p[4]; float area=0; int lit=0;
        for(int v=0;v<sides;v++) {
            CHECK(projectRailPoint(r,positions[i+v].x,positions[i+v].y,positions[i+v].z,&eye,&p[v]),
                "reflection vertex left the view");
            lit|=alphas[i+v];
        }
        for(int v=0;v<sides;v++) area+=p[v].x*p[(v+1)%sides].y-p[(v+1)%sides].x*p[v].y;
        /* GX culls clockwise-negative back faces: every reflection cell must face the camera. */
        CHECK(area<0.0f,"reflection cell faces away and would be culled");
        CHECK(lit,"a cell that reflects nothing was drawn");
        for(int v=0;v<sides;v++) {
            guVector q=positions[i+v];
            float m=fmaxf(fabsf(q.x),fmaxf(fabsf(q.y),fabsf(q.z)));
            CHECK(m<=1.0001f && m>=.78f-.0001f,"reflection left the glass surface");
        }
    }
    *total+=count;
}
static void test_glass(void) {
    const GXColor tint[6]={{10,1,2,3},{20,1,2,3},{30,1,2,3},{40,1,2,3},{50,1,2,3},{60,1,2,3}};
    GXColor white[6],lit[6];
    cubeRasterTransform_t r;
    for(int f=0;f<6;f++) white[f]=(GXColor){255,255,255,255};
    /* Light belongs to the camera: at rest each face keeps its own tint, after a
     * quarter turn the face now toward the viewer takes the front tint, and a
     * face midway between two directions blends them rather than snapping. */
    surface_pose(&r,0,0,0,1); litCubeTints(&r,tint,lit);
    for(int f=0;f<6;f++) CHECK(lit[f].r==tint[f].r && lit[f].a==3,"cardinal tint changed");
    surface_pose(&r,INDIGO_TAU/4,0,0,1); litCubeTints(&r,tint,lit);
    CHECK(lit[1].r==60 && lit[5].r==30 && lit[2].r==10 && lit[0].r==20 &&
        lit[4].r==50 && lit[3].r==40,"tint turned with the cube instead of the camera");
    surface_pose(&r,INDIGO_TAU/8,0,0,1); litCubeTints(&r,tint,lit);
    CHECK(abs(lit[5].r-45)<=1,"a turning face snapped between tints");
    for(int i=0;i<500;i++) {
        surface_pose(&r,i*0.37f,i*0.23f,i*0.11f,0.5f+i*0.002f); litCubeTints(&r,white,lit);
        for(int f=0;f<6;f++) CHECK(lit[f].r==255 && lit[f].a==255,"tint weights must sum to one");
    }
    /* Only camera-facing glass reflects, and grazing glass fades out before
     * the silhouette rather than drawing a hard aliased rim. */
    guVector eye={0,0,-5},grazing={0.99995f,0,0.01f};
    surface_pose(&r,0,0,0,1);
    CHECK(mirrored(&r,eye,(guVector){0,0,-1})==0,"back-facing glass reflected");
    CHECK(mirrored(&r,eye,grazing)<=8,"grazing reflection is not faded");
    /* The studio stands close: along a face, a point's ray lands further
     * round the map the further it sits from the middle; out along the
     * normal changes nothing. */
    float s0,t0,s1,t1;
    guVector view={1,0.5f,-4.4f},front={0,0,1};
    glassReflect(view,front,(guVector){0,0,1},&s0,&t0);
    glassReflect(view,front,(guVector){0.8f,0,1},&s1,&t1);
    CHECK(s1>s0+0.02f && fabsf(t1-t0)<0.01f,"a face's side mirrors what its middle does");
    glassReflect(view,front,(guVector){0,0,3},&s1,&t1);
    CHECK(s1==s0 && t1==t0,"the studio moved with the glass's distance from the centre");
    /* At each Home face's rest pitch the front pane carries a gentle reflection
     * that brightens toward the key light on the right. */
    static const float restPitch[4]={0.09f,0.16f,0.0f,0.135f};
    for(int face=0;face<4;face++) {
        surface_pose(&r,0.28f,restPitch[face],0,0.92f);
        guVector left,right,center,n=cubeViewNormal(&r,(guVector){0,0,1});
        indigoPoint_t screen;
        CHECK(projectRailPoint(&r,-.7f,0,1,&left,&screen) && projectRailPoint(&r,.7f,0,1,&right,&screen) &&
            projectRailPoint(&r,0,0,1,&center,&screen),"front pane left the view");
        int a=mirrored(&r,left,n),b=mirrored(&r,center,n),c=mirrored(&r,right,n);
        CHECK(a<b && b<c,"front reflection does not brighten toward the key");
        CHECK(b>=15 && c<=70,"front reflection is not gentle at rest");
    }
    /* Every reflection cell is complete, camera-facing, reflecting, on the
     * glass and on the map, within a bounded vertex budget, across the full
     * pose sweep. */
    cubeSurfaceQuad_t shell[6],strips[12],corners[8];
    buildCubeFaces(shell,1,.78f,tint); buildChamferStrips(strips,1,.78f,tint,NULL);
    buildCubeCorners(corners,1,.78f);
    int most=0;
    for(int yaw=0;yaw<360;yaw+=30) for(int pitch=0;pitch<360;pitch+=30)
    for(int roll=0;roll<180;roll+=45) for(int size=0;size<2;size++) {
        int total=0;
        surface_pose(&r,yaw*INDIGO_TAU/360,pitch*INDIGO_TAU/360,roll*INDIGO_TAU/360,size?1.3f:.65f);
        check_glass_stream(&r,shell,6,4,&total);
        check_glass_stream(&r,strips,12,4,&total);
        check_glass_stream(&r,corners,8,3,&total);
        if(total>most) most=total;
    }
    /* Measured 856 at the worst sweep pose (449 before bevels were cut along
     * as often as faces and corners became fans, 1,381 when the light was
     * shaded per vertex); the video thread pays for each. */
    CHECK(most>200 && most<=1100,"reflection vertex budget");
}
int main(void) {
    test_fast_sqrt(); test_dial(); test_rail_joins(); test_motifs(); test_controller(); test_rounded_outlines();
    test_icons_on_their_faces(); test_lift(); test_shadows(); test_hidden_controller_reads_the_pad();
    test_controller_across_the_clock_wrap();
    test_controller_gives_way_to_the_hand();
    test_closed_cube(); test_seamless_mesh(); test_chamfer_color_pairs(); test_surfaces(); test_glass();
    puts("native strokes: bounded complete GX streams, perspective coverage and closed seams");
    return 0;
}
"""

class StrokeGXStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        indigo=(GUI / "indigo_background.c").read_text()
        frame=(GUI / "FrameBufferMagic.c").read_text()
        blocks=[extract_function(indigo, "static float fastSqrt("),
                extract_function((GUI / "ui_motion.c").read_text(), "float UIMotion_Smoothstep(")]
        blocks += [extract_function(indigo, "static void " + name + "(")
                   for name in ("putCubeVertex",)]
        blocks += [extract_function(indigo[indigo.rindex("static bool " + name + "("):], "static bool " + name + "(")
                   for name in ("projectRailPoint", "railJoin")]
        blocks += [extract_function(indigo, "static void " + name + "(")
                   for name in ("putProjectedRailVertex",)]
        blocks += [extract_function(indigo, "static guVector semanticFacePoint(")]
        blocks += [extract_function(indigo, "static const indigoPoint_t *faceCircleSamples(")]
        blocks += [extract_function(indigo, "static void " + name + "(") for name in (
            "putSemanticMotifQuad", "putSemanticMotifRect", "putSemanticFaceDiamond",
            "putSemanticFaceHand", "putSemanticFaceBook", "drawFacePolygon", "drawFaceBand",
            "drawFaceCircle", "drawFaceRing", "drawControllerRect")]
        blocks += [re.search(r"static const float controllerOutline\[\]\[2\] = \{.*?\n\};",
            indigo, re.S).group(0)]
        blocks += [extract_function(indigo, "static void " + name + "(")
                   for name in ("drawFaceArc", "drawFaceBean")]
        blocks += [extract_function(indigo, "static float controllerAxis(")]
        blocks += [extract_function(indigo, "static void controllerPose(")]
        blocks += [extract_function(indigo, "static GXColor controllerGlow(")]
        blocks += [extract_function(indigo, "static int roundedOutline(")]
        blocks += [extract_function(indigo, "static void " + name + "(") for name in (
            "drawRoundedBand", "drawRoundedRect", "drawFaceBar", "drawFaceSpoke",
            "drawControllerIcon", "drawHubIcon", "drawSlidersIcon", "drawClockIcon",
            "drawBooksIcon", "drawDiscIcon", "drawGearIcon", "drawCoversIcon",
            "drawPlayIcon", "drawSdCardIcon", "drawFolderIcon", "drawTogglesIcon",
            "drawDialIcon", "drawInfoIcon", "drawPowerIcon", "drawChipIcon",
            "drawAppsIcon", "drawSavesIcon", "drawEmulatorsIcon", "drawWebIcon")]
        blocks += [extract_function(indigo, "static float faceFacing(")]
        blocks += [extract_function(indigo, "static float faceStrokeShare(")]
        blocks += [extract_function(indigo, "static float faceIconLift(")]
        blocks += [extract_function(indigo, "static void liftFaceIcon(")]
        blocks += [extract_function(indigo, "static void drawOneFaceIcon(")]
        blocks += [extract_function(indigo, "static void drawFaceIcons(")]
        blocks += [extract_function(indigo, "static void drawFaceShadows(")]
        blocks += [extract_function(indigo, "static float outlineCross(")]
        blocks += [extract_function(indigo, "static void buildCubeOutline(")]
        blocks += [extract_function(indigo, "static indigoPoint_t cubeOutlineNormal(")]
        blocks += [extract_function(indigo, "static bool cubeOutlineEdge(")]
        blocks += [extract_function(indigo, "static guVector bevelCut(")]
        blocks += [extract_function(indigo, "static void " + name + "(") for name in (
            "buildCubeFaces", "buildChamferStrip", "buildChamferBands", "buildChamferStrips",
            "buildCubeCorners", "buildCornerFans", "drawCubeSurfacePassVertices",
            "drawCubeSurfacePass")]
        blocks += [re.search(r"static const guVector cubeFaceAxes\[6\] = \{.*?\n\};",
            indigo, re.S).group(0)]
        blocks += [re.search(r"static const struct \{\n\tguVector direction;.*?\} glassLights\[\] = "
            r"\{.*?\n\};", indigo, re.S).group(0)]
        blocks += re.findall(r"^#define GLASS_(?:LIGHTS|STUDIO_REACH) .*$", indigo, re.M)
        blocks += [re.search(r"typedef struct glassMirrorVertex \{.*?\} glassMirrorVertex_t;",
            indigo, re.S).group(0)]
        blocks += [extract_function(indigo, signature) for signature in (
            "static guVector cubeViewNormal(", "static void litCubeTints(",
            "static float glassSmoothstep(", "static GXColor glassStudioLight(",
            "static void glassStudioCoords(", "static guVector glassStudioDirection(",
            "static u8 glassReflect(", "static guVector glassVertexNormal(",
            "static bool glassSameNormal(", "static guVector glassBilinear(",
            "static guVector glassSidePoint(", "static guVector glassGridPoint(",
            "static void glassFanPoint(",
            "static void putGlassMirrorVertex(", "static void drawGlassReflection(")]
        blocks += [frame[frame.index("typedef struct systemDialPoint"):frame.index("static void _SetupRasterColor(")]]
        blocks += [extract_function(frame, "static void " + name + "(")
                   for name in ("_PutSystemDialVertex", "_DrawSystemRing")]
        cls.emitters="\n".join(blocks)
        # The controller's sizing constants, the icons' plane, lift and
        # shadow, and the bevels' seam blend come from the source, never a
        # copy.
        cls.defines="\n".join(re.findall(
            r"^#define (?:UI_ANIM_TIME_WRAP_SECONDS) .*$",
            (GUI / "ui_anim.h").read_text(), re.MULTILINE) + re.findall(
            r"^#define (?:FACE_POLYGON_MAX|FACE_BAND_MAX|FACE_ARC_MAX|CONTROLLER_IDLE_HOLD|"
            r"CONTROLLER_IDLE_RELEASE|CONTROLLER_PRESS_CYCLE|BEVEL_SEAM_BLEND|FACE_ICON_\w+|FACE_SHADOW_\w+) .*$",
            indigo, re.MULTILINE))
        cls.idle=re.search(r"typedef struct controllerIdle \{.*?\} controllerIdle_t;",
            indigo, re.S).group(0)
        # The face and icon lists come from the source, never a copy.
        home=(GUI / "ui_home.h").read_text()
        cls.enums="\n".join([re.search(r"^#define UI_HOME_ICON_CHOICES \d+$", home, re.M).group(0)] +
            [re.search(r"typedef enum \{[^}]*\} " + name + ";", home).group(0)
             for name in ("uiHomeFace_t", "uiHomeIcon_t")])
        cls.indigo = indigo
        cls.frame = frame

    def test_stream_contract_matches_native_pipelines(self):
        cube = extract_function(self.indigo, "static void setupCubePipeline(")
        raster = extract_function(self.indigo, "static void setupRasterPipeline(")
        frame = extract_function(self.frame, "static void drawInit()\n")
        def attributes(source):
            self.assertIn("GX_ClearVtxDesc();", source)
            return set(re.findall(r"GX_SetVtxDesc\((GX_VA_\w+),\s*GX_DIRECT\)", source))
        self.assertEqual(attributes(cube), {"GX_VA_POS", "GX_VA_CLR0"})
        self.assertEqual(attributes(raster), {"GX_VA_POS", "GX_VA_CLR0", "GX_VA_TEX0"})
        self.assertEqual(attributes(frame), {"GX_VA_POS", "GX_VA_CLR0", "GX_VA_TEX0"})

    def test_bevel_passes_follow_camera(self):
        draw = extract_function(self.indigo, "static void drawCube(")
        def check(source):
            calls = re.findall(r"drawCubeSurfacePass(?:Vertices)?\(&raster, &shellOutline, "
                r"(\w+), (\d+), (?:(3|4), )?(GX_CULL_\w+)\);", source)
            self.assertEqual(calls, [
                ("bevels", "36", "", "GX_CULL_FRONT"),
                ("fans", "72", "3", "GX_CULL_FRONT"),
                ("shell", "6", "", "GX_CULL_FRONT"),
                ("shell", "6", "", "GX_CULL_BACK"),
                ("bevels", "36", "", "GX_CULL_BACK"),
                ("fans", "72", "3", "GX_CULL_BACK")])
            # The refraction and the reflection shade the bare strips.
            self.assertEqual(source.count("buildChamferStrips(strips, outer, inset, edge, NULL);"), 1)
            # Each tint pass's bevels and corners are rebuilt in that pass's
            # glass tints, so their seams match the faces they meet.
            order = []
            for tints in ("backGlassColors", "frontGlassColors"):
                at = source.index("litCubeTints(&raster, " + tints + ", lit);")
                order += [at, source.index("buildChamferStrips(bevels, outer, inset, ", at),
                          source.index("buildCornerFans(fans, outer, inset, ", at),
                          source.index("bevels, 36, ", at)]
            self.assertEqual(order, sorted(order))
            # The rear frame, seen through the glass, is at half the near
            # bevels' opacity: depth, not a second wireframe.
            self.assertIn("buildChamferStrips(bevels, outer, inset, rear, lit);", source)
            self.assertIn("buildCornerFans(fans, outer, inset, rear, lit);", source)
            self.assertIn("rear[face] = edge[face];\n\t\trear[face].a /= 2;", source)
        check(draw)
        # Both complete mesh passes must use current facing, not an object-axis split.
        for old,new in [("bevels, 36, GX_CULL_FRONT", "bevels, 24, GX_CULL_NONE"),
                        ("bevels, 36, GX_CULL_BACK", "bevels, 12, GX_CULL_NONE"),
                        ("bevels, 36, GX_CULL_FRONT", "bevels, 36, GX_CULL_BACK"),
                        ("fans, 72, 3, GX_CULL_FRONT", "fans, 72, 3, GX_CULL_BACK"),
                        ("\tbuildChamferStrips(bevels, outer, inset, edge, lit);\n\tbuildCornerFans(fans, outer, inset, edge, lit);\n\tdrawCubeSurfacePass(&raster, &shellOutline, shell, 6, GX_CULL_BACK",
                         "\tdrawCubeSurfacePass(&raster, &shellOutline, shell, 6, GX_CULL_BACK"),
                        ("rear[face].a /= 2;", "rear[face].a /= 1;")]:
            with self.subTest(mutant=new), self.assertRaises((AssertionError, ValueError)):
                check(draw.replace(old,new,1))

    def run_emitters(self, emitters):
        with tempfile.TemporaryDirectory(prefix="swiss-stroke-gx-") as directory:
            root=Path(directory); source=root / "strokes.c"; binary=root / "strokes"
            source.write_text(HARNESS.replace("/* HOME ENUMS */", self.enums)
                .replace("/* CONTROLLER DEFINES */", self.defines)
                .replace("/* CONTROLLER IDLE */", self.idle)
                .replace("/* EMITTERS */", emitters))
            result=subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                ["-std=c99", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary), "-lm"],
                capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return subprocess.run([str(binary)],capture_output=True,text=True,timeout=5)

    def test_controller_defines_come_from_the_source(self):
        self.assertEqual(self.defines.count("#define"), 14)

    def test_native_emitters(self):
        result=self.run_emitters(self.emitters)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_regressions_are_rejected(self):
        mutants={
            "missing UV": ("GX_TexCoord2f32(0.0f, 0.0f);",
                "if(color.a == 254) GX_TexCoord2f32(0.0f, 0.0f);"),
            "opaque fringe": ("if(band == 0) a.a = 0;", "if(band == 0) a.a = color.a;"),
            "depth-dependent width": ("join.x * offset * -eye.z", "join.x * offset * 5.4f"),
            "joins mitred in stage units": ("float across = UIStage_PixelWidth();\n\tfloat ax",
                "float across = 1.0f;\n\tfloat ax"),
            "silhouette normal in stage units": ("float across = UIStage_PixelWidth();\n\t\tfloat pixels",
                "float across = 1.0f;\n\t\tfloat pixels"),
            "changed depth": ("/ raster->scaleY, eye.z, color)", "/ raster->scaleY, eye.z - 0.1f, color)"),
            "matrix restore": ("GX_LoadPosMtxImm(raster->model, GX_PNMTX0);", "GX_LoadPosMtxImm(identity, GX_PNMTX0);"),
            "opaque semantic fringe": ("transparent.a = 0;", "transparent.a = color.a;"),
            "wrong face winding": ("area >= -0.001f", "area <= -0.001f"),
            "subpixel brightness": ("fminf(1.0f, clearance * 2.0f)", "1.0f"),
            "edge-on stroke vanishes": ("&joins[i])) sharp = true;", "&joins[i])) goto hidden;"),
            "degenerate vertex count": ("i < 20;", "i < 19;"),
            "outward silhouette normal": ("{-inward.x, -inward.y}", "{inward.x, inward.y}"),
            "opaque silhouette fringe": ("transparentA.a = transparentB.a = 0;",
                "transparentA.a = transparentB.a = 255;"),
            "silhouette width": ("edge->outward[1], 1.0f, transparentB",
                "edge->outward[1], 2.0f, transparentB"),
            "silhouette face cull": ("cullMode == GX_CULL_BACK && area >= 0.0f",
                "cullMode == GX_CULL_BACK && area <= 0.0f"),
            "silhouette fill changed": ("putCubeVertex(p.x, p.y, p.z, quads[quad].color[vertex]);",
                "putCubeVertex(p.x + 0.01f, p.y, p.z, quads[quad].color[vertex]);"),
            "silhouette source gradient": ("edge->color[1] = quads[quad].color[next];",
                "edge->color[1] = quads[quad].color[vertex];"),
            "interior silhouette fringe": (
                "fabsf(outlineCross(p, q, a)) / length > 0.005f ||\n\t\t\t"
                "fabsf(outlineCross(p, q, b)) / length > 0.005f ||", "false ||"),
            "silhouette fringe depth write": (
                "GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);\n"
                "\tGX_Begin(GX_QUADS, GX_VTXFMT0, edgeCount * 4);",
                "GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);\n"
                "\tGX_Begin(GX_QUADS, GX_VTXFMT0, edgeCount * 4);"),
            "silhouette fringe culling": (
                "GX_SetCullMode(GX_CULL_NONE);\n"
                "\tGX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);",
                "GX_SetCullMode(GX_CULL_BACK);\n"
                "\tGX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);"),
            "open corner": ("corner < 8", "corner < 7"),
            "corner reversed winding": ("x * y * z > 0.0f", "x * y * z < 0.0f"),
            "detached cap": ("x * outer, y * inset, z * inset", "x * outer, y * inset, z * inset + 0.01f"),
            "triangle primitive": ("vertexCount == 3 ? GX_TRIANGLES : GX_QUADS", "GX_QUADS"),
            "seam takes the edge tint": ("a1.x, a1.y, a1.z, face[faceA], edge[faceA]);",
                "a1.x, a1.y, a1.z, edge[faceA], edge[faceA]);"),
            "inner row takes the face tint": ("d1.x, d1.y, d1.z, edge[faceA], edge[faceB]);",
                "d1.x, d1.y, d1.z, face[faceA], edge[faceB]);"),
            "corner fan misses a bevel's cut": ("rim[k * 3 + 2] = bevelCut(p[next], p[k]);",
                "rim[k * 3 + 2] = bevelCut(p[k], p[next]);"),
            "corner fan's corner takes another face's tint": ("tint[k * 3] = face[on[k]];",
                "tint[k * 3] = face[on[next]];"),
            "corner fan reversed winding": ("\t\tif(x * y * z > 0.0f) {\n\t\t\tguVector point = p[1];",
                "\t\tif(x * y * z < 0.0f) {\n\t\t\tguVector point = p[1];"),
            "triangle count": ("count * vertexCount);", "count * 4);"),
            "bevel reversed winding": (
                "normal.x * ax + normal.y * ay + normal.z * az > 0.0f",
                "normal.x * ax + normal.y * ay + normal.z * az < 0.0f"),
            "bevel endpoint gradient": (
                "quad->color[1] = quad->color[3];",
                "quad->color[1] = color;"),
            "tint turns with the cube": (
                "guVector n = cubeViewNormal(raster, cubeFaceAxes[face]);",
                "guVector n = raster ? cubeFaceAxes[face] : cubeFaceAxes[face];"),
            "tint snaps between directions": (
                "float wx = n.x * n.x, wy = n.y * n.y, wz = n.z * n.z;",
                "float wx = n.x * n.x > .5f, wy = n.y * n.y > .5f, wz = n.z * n.z > .5f;"),
            "no grazing fade": ("fminf(1.0f, facing * 6.0f)", "1.0f"),
            "key light on the left": ("{{0.861f, 0.148f, 0.487f}", "{{-0.861f, 0.148f, 0.487f}"),
            "back-facing reflection": ("if(area >= -0.001f) continue;", "if(area >= 1e30f) continue;"),
            "reversed reflection cells": ("{{0, 0}, {1, 0}, {1, 1}, {0, 1}}",
                "{{0, 0}, {0, 1}, {1, 1}, {1, 0}}"),
            "dark reflection cells": ("\t\t\tif((first[0].alpha | first[1].alpha | first[GRID].alpha | "
                "first[GRID + 1].alpha) == 0)\n\t\t\t\tcontinue;\n", ""),
            "studio stands far": ("#define GLASS_STUDIO_REACH 3.0f", "#define GLASS_STUDIO_REACH 3000.0f"),
            "studio follows the glass out along its normal": (
                "float out = offset.x * n.x + offset.y * n.y + offset.z * n.z;", "float out = 0.0f;"),
            "studio map upside down": ("*t = 0.5f - r.y / m;", "*t = 0.5f + r.y / m;"),
            "one Newton step": ("\ty *= 1.5f - 0.5f * x * y * y;\n\ty *= 1.5f - 0.5f * x * y * y;\n",
                "\ty *= 1.5f - 0.5f * x * y * y;\n"),
            "estimate past its range": ("if(!(x >= FLT_MIN && x <= FLT_MAX)) return sqrtf(x);", ""),
            "icons not lifted": ("liftFaceIcon(&faded, face, faceIconLift(facing));",
                "liftFaceIcon(&faded, face, 0.0f * faceIconLift(facing));"),
            "side faces lifted": ("(facing - 0.80f) / 0.15f", "(facing - 0.33f) / 0.27f"),
            "shadow adds light": ("GX_BL_ZERO, GX_BL_INVSRCALPHA", "GX_BL_SRCALPHA, GX_BL_ONE"),
            "shadow on turned faces": ("FACE_SHADOW_ALPHA * strength * faceStrokeShare(facing);",
                "FACE_SHADOW_ALPHA * strength;"),
            "shadow ignores the glass light": ("FACE_SHADOW_ALPHA * strength *", "FACE_SHADOW_ALPHA *"),
            "shadow on plain glass": ("if(strength <= 0.0f || scale < 0.0001f) return;",
                "if(scale < 0.0001f) return;"),
            "shadow falls toward the light": ("const float fallX = -0.68f, fallY = -0.73f;",
                "const float fallX = 0.68f, fallY = 0.73f;"),
            "sharp shadow": ("\tshadow.scaleX /= FACE_SHADOW_FEATHER;\n\tshadow.scaleY /= FACE_SHADOW_FEATHER;\n",
                ""),
            "shadow as dark as it was": ("FACE_SHADOW_ALPHA * strength * faceStrokeShare(facing);",
                "0.45f * strength * faceStrokeShare(facing);"),
            "shadow state left": ("clock, pad);\n\t}\n\tGX_LoadPosMtxImm(raster->model, GX_PNMTX0);\n"
                "\tGX_SetCullMode(GX_CULL_BACK);\n"
                "\tGX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);",
                "clock, pad);\n\t}"),
            "shadow off the glass": ("(basis[row][2] * FACE_SHADOW_PLANE +",
                "(basis[row][2] * FACE_ICON_PLANE +"),
            "shadow drop ignores the lift": ("FACE_ICON_PLANE + faceIconLift(facing) - FACE_SHADOW_PLANE",
                "FACE_ICON_PLANE + FACE_ICON_LIFT - FACE_SHADOW_PLANE"),
        }
        for name,(old,new) in mutants.items():
            with self.subTest(name=name):
                self.assertIn(old,self.emitters)
                result=self.run_emitters(self.emitters.replace(old,new,1))
                self.assertNotEqual(result.returncode,0,"mutant survived: "+name)

if __name__ == "__main__":
    unittest.main()
