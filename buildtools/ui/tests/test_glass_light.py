#!/usr/bin/env python3
"""The cube's light: refraction, dispersion, studio, bloom, rim and halo.

Compiles the real emitters from indigo_background.c against a checked GX
stub and proves what the glass does with light: the frame behind the front
glass is bent toward the cube's middle and parted into red, green and blue in
that order, the bent layer fades out at the silhouette, the soft glows
and rim draw closed bounded streams, the frame copies keep their contract
(half size, RGBA8, box filter, never clearing the EFB, cache written back
once before the GPU first writes the buffer), and the studio map the glass
mirrors is baked texel for texel in GX's tiles and baked again only when
Menu Color changes. Source checks pin where the passes sit in drawCube and
that nothing in the clear glass writes depth, and mutants prove each
property is really tested.
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
#include "ui_scene.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct { u8 r,g,b,a; } GXColor;
typedef float Mtx[3][4];
typedef struct { float x,y,z; } guVector;
typedef struct { float x,y; } indigoPoint_t;
typedef struct { Mtx model,semanticFaces[UI_HOME_FACE_COUNT];
    float motifAlpha[UI_HOME_FACE_COUNT],scaleX,scaleY; } cubeRasterTransform_t;
typedef struct { guVector point[4]; GXColor color[4]; } cubeSurfaceQuad_t;
typedef struct { indigoPoint_t point[24]; int count; float length[24], pixels[24];
    indigoPoint_t corner[24]; bool joined[24]; } cubeOutline_t;
typedef struct { void *data; u16 w, h; u8 fmt, mip, minFilter; float maxLod; } GXTexObj;
#define CHECK(c,m) do { if(!(c)) { fprintf(stderr,"%s\n",m); exit(73); } } while(0)
#define INDIGO_TAU 6.28318530718f
#define RADIAL_SEGMENTS 24
#define CUBE_CAMERA_Z -5.4f
#define BOOT_CUBE_HANDOFF 0.625f
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
enum { GX_QUADS=1, GX_TRIANGLESTRIP=2, GX_TRIANGLES=3, GX_VTXFMT0=0,
    GX_BM_BLEND=1, GX_BL_SRCALPHA=2, GX_BL_ONE=3, GX_BL_INVSRCALPHA=4, GX_LO_CLEAR=0,
    GX_TF_RGBA8=6, GX_CLAMP=0, GX_LINEAR=1, GX_LIN_MIP_LIN=5, GX_ANISO_1=0, GX_TRUE=1, GX_FALSE=0,
    GX_TEXMAP0=0 };
/* Menu Color is Indigo here, so the emitters' recolor passes colors through,
 * unless a test turns recolor on: then it swaps red and blue. */
static int recolor;
static void UIColor_Apply(u8 *r,u8 *g,u8 *b) {
    (void)g;
    if(recolor) { u8 swap=*r; *r=*b; *b=swap; }
}
/* The 4:3 stage; test_ui_stage.c covers widescreen. */
static float UIStage_FrameX(float x) { return x; }
static float UIStage_PixelWidth(void) { return 1.0f; }
static bool active; static int phase,remaining,count,begins,uvs,uvCalls,blendDst;
static guVector positions[8192];
static GXColor colors[8192];
static float uv[8192][3][2];
static int primitives[1024], primitiveSizes[1024];
static void reset(int texcoords) {
    CHECK(!active,"previous primitive unfinished");
    phase=remaining=count=begins=0; uvs=texcoords; blendDst=GX_BL_INVSRCALPHA;
}
static void GX_SetBlendMode(int mode,int source,int dest,int op) {
    CHECK(mode==GX_BM_BLEND && source==GX_BL_SRCALPHA && op==GX_LO_CLEAR &&
        (dest==GX_BL_ONE || dest==GX_BL_INVSRCALPHA),"glass blend policy");
    blendDst=dest;
}
static void GX_Begin(int primitive,int format,int vertices) {
    CHECK(!active && phase==0 && vertices>0 && format==GX_VTXFMT0,"bad begin");
    CHECK(primitive==GX_QUADS || primitive==GX_TRIANGLES || primitive==GX_TRIANGLESTRIP,
        "unexpected primitive");
    CHECK(begins<1024,"unbounded primitives");
    primitives[begins]=primitive; primitiveSizes[begins]=vertices;
    active=true; remaining=vertices; begins++;
}
static void GX_Position3f32(float x,float y,float z) {
    CHECK(active && phase==0 && remaining>0 && count<8192,"position order/budget");
    CHECK(isfinite(x)&&isfinite(y)&&isfinite(z),"nonfinite position");
    positions[count]=(guVector){x,y,z}; phase=1;
}
static void GX_Color4u8(u8 r,u8 g,u8 b,u8 a) {
    CHECK(active && phase==1,"color order"); colors[count]=(GXColor){r,g,b,a};
    uvCalls=0;
    if(uvs==0) { phase=0; remaining--; count++; } else phase=2;
}
static void GX_TexCoord2f32(float s,float t) {
    CHECK(active && phase==2 && uvCalls<uvs,"texture coordinate order");
    CHECK(isfinite(s)&&isfinite(t),"nonfinite texture coordinate");
    uv[count][uvCalls][0]=s; uv[count][uvCalls][1]=t;
    if(++uvCalls==uvs) { phase=0; remaining--; count++; }
}
static void GX_End(void) { CHECK(active && phase==0 && remaining==0,"incomplete primitive"); active=false; }
/* Frame copies. */
static int copies, flushes, invalidations, copyL, copyT, copyW, copyH, dstW, dstH, dstFmt, dstMip, clearFlag;
static void DCFlushRange(void *p,u32 n) { (void)p; CHECK(n>0,"empty flush"); flushes++; }
static void GX_SetTexCopySrc(u16 l,u16 t,u16 w,u16 h) { copyL=l; copyT=t; copyW=w; copyH=h; }
static void GX_SetTexCopyDst(u16 w,u16 h,u32 f,u8 m) { dstW=w; dstH=h; dstFmt=(int)f; dstMip=m; }
static void GX_CopyTex(void *d,u8 clear) { CHECK(d!=NULL,"copy target"); CHECK(flushes==1,"copy before its cache flush"); clearFlag=clear; copies++; }
static void GX_PixModeSync(void) {}
static void GX_InitTexObj(GXTexObj *o,void *d,u16 w,u16 h,u8 f,u8 a,u8 b,u8 m) { (void)a;(void)b; o->data=d; o->w=w; o->h=h; o->fmt=f; o->mip=m; }
static void GX_InitTexObjLOD(GXTexObj *o,u8 a,u8 b,float c,float d,float e,u8 f,u8 g,u8 h) { (void)b;(void)c;(void)e;(void)f;(void)g;(void)h; o->minFilter=a; o->maxLod=d; }
static void GX_InvalidateTexAll(void) { invalidations++; }
static void GX_LoadTexObj(const GXTexObj *o,u8 map) { CHECK(o->data!=NULL && map==GX_TEXMAP0,"texture load"); }
static void putCubeVertex(float x,float y,float z,GXColor c) {
    UIColor_Apply(&c.r,&c.g,&c.b); GX_Position3f32(x,y,z); GX_Color4u8(c.r,c.g,c.b,c.a);
}
static void putVertex(indigoPoint_t p, GXColor c) {
    UIColor_Apply(&c.r,&c.g,&c.b); GX_Position3f32(p.x,p.y,0); GX_Color4u8(c.r,c.g,c.b,c.a);
    GX_TexCoord2f32(0,0);
}
static void guMtxIdentity(Mtx m) { memset(m,0,sizeof(Mtx)); m[0][0]=m[1][1]=m[2][2]=1; }
/* EMITTERS */
static void pose(cubeRasterTransform_t *r,float yaw,float pitch,float roll,float scale) {
    float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch),cr=cosf(roll),sr=sinf(roll);
    float R[3][3]={{cy*cr+sy*sp*sr,-cy*sr+sy*sp*cr,sy*cp},{cp*sr,cp*cr,-sp},
        {-sy*cr+cy*sp*sr,sy*sr+cy*sp*cr,cy*cp}};
    memset(r,0,sizeof(*r));
    for(int i=0;i<3;i++) { for(int j=0;j<3;j++) r->model[i][j]=R[i][j]*scale; }
    r->model[2][3]=CUBE_CAMERA_Z;
    r->scaleX=r->scaleY=625.2f;
}
static const glassRefraction_t glass0={320,240,36,0.045f,0.17f,1.0f/640,1.0f/480,0,0,{128,125,134,255}};
static bool near(float a,float b,float e) { return fabsf(a-b)<=e; }
static void test_vertex_optics(void) {
    cubeRasterTransform_t r; glassRefractedVertex_t out;
    guVector eye={0,0,-4.4f};
    pose(&r,0,0,0,1);
    /* Face-on at the centre: no lateral bend, all channels together, fully
     * opaque, and the frame sampled exactly where it was. */
    refractGlassVertex(&r,&glass0,(guVector){0,0,1},eye,(guVector){0,0,1},&out);
    CHECK(out.color.a==255,"face-on glass is not the refracting layer");
    /* The tint's alpha is the layer's opacity: the boot fades it in. */
    glassRefraction_t half=glass0; half.tint.a=128;
    refractGlassVertex(&r,&half,(guVector){0,0,1},eye,(guVector){0,0,1},&out);
    CHECK(out.color.a==128,"the refracting layer ignores its opacity");
    for(int c=0;c<3;c++) CHECK(near(out.s[c]*640,320,.01f) && near(out.t[c]*480,240,.01f),
        "centre of the face moved");
    /* Off-centre on the face: magnified toward the centre by the lens. */
    guVector off={1.1f,-0.6f,-4.4f};
    refractGlassVertex(&r,&glass0,(guVector){0,0,1},off,(guVector){0,0,1},&out);
    float sx=320+625.2f*1.1f/4.4f, sy=240+625.2f*0.6f/4.4f;
    CHECK(near(out.s[1]*640,320+(sx-320)*(1-0.045f),.05f) &&
        near(out.t[1]*480,240+(sy-240)*(1-0.045f),.05f),"face is not magnified about the centre");
    CHECK(near(out.s[0],out.s[2],1e-6f),"flat face parted colours without a bend");
    /* A bevel turned right bends the frame toward the middle (left), and
     * blue bends furthest, red least: a prism's order. */
    guVector bevel={0.7071f,0,0.7071f};
    refractGlassVertex(&r,&glass0,(guVector){1,0,0.9f},(guVector){0.8f,0,-4.6f},bevel,&out);
    float base=320+625.2f*0.8f/4.6f; base=320+(base-320)*(1-0.045f);
    CHECK(out.s[0]*640<base && out.s[1]*640<out.s[0]*640 && out.s[2]*640<out.s[1]*640,
        "dispersion lost its red-green-blue order or its direction");
    CHECK(near((base-out.s[1]*640),36*0.7071f,.05f),"bend does not follow the normal");
    /* A bevel turned up bends the frame down (screen y grows). */
    refractGlassVertex(&r,&glass0,(guVector){0,1,0.9f},(guVector){0,0.8f,-4.6f},
        (guVector){0,0.7071f,0.7071f},&out);
    CHECK(out.t[1]*480>240-(625.2f*0.8f/4.6f)*(1-0.045f),"upward bevel bent the wrong way");
    /* Grazing glass fades: at the silhouette the bent image meets the straight one. */
    refractGlassVertex(&r,&glass0,(guVector){1,0,0},(guVector){0,0,-5},(guVector){1,0,0},&out);
    CHECK(out.color.a==0,"silhouette glass not faded");
    refractGlassVertex(&r,&glass0,(guVector){1,0,0},(guVector){0,0,-5},(guVector){0.9982f,0,0.06f},&out);
    int nearEdge=out.color.a;
    refractGlassVertex(&r,&glass0,(guVector){1,0,0},(guVector){0,0,-5},(guVector){0.985f,0,0.1736f},&out);
    CHECK(nearEdge>0 && nearEdge<60 && out.color.a>nearEdge && out.color.a<255,
        "glancing fade is not gradual");
}
static void test_refraction_stream(void) {
    const GXColor tint[6]={{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4}};
    cubeSurfaceQuad_t shell[6],strips[12],corners[8];
    buildCubeFaces(shell,1,.78f,tint); buildChamferStrips(strips,1,.78f,tint,NULL); buildCubeCorners(corners,1,.78f);
    int most=0,poses=0;
    for(int yaw=0;yaw<360;yaw+=30) for(int pitch=0;pitch<360;pitch+=30)
    for(int roll=0;roll<180;roll+=45) for(int size=0;size<2;size++) {
        cubeRasterTransform_t r;
        pose(&r,yaw*INDIGO_TAU/360,pitch*INDIGO_TAU/360,roll*INDIGO_TAU/360,size?1.3f:.65f);
        reset(3);
        drawGlassRefraction(&r,&glass0,shell,6,4,1);
        drawGlassRefraction(&r,&glass0,strips,12,4,1);
        drawGlassRefraction(&r,&glass0,corners,8,3,1);
        CHECK(!active,"refraction left a primitive open");
        int total=count;
        for(int i=0;i<count;i++) {
            guVector q=positions[i];
            float m=fmaxf(fabsf(q.x),fmaxf(fabsf(q.y),fabsf(q.z)));
            CHECK(m<=1.0001f && m>=.78f-.0001f,"refraction left the outer glass");
        }
        /* Every cell faces the camera, as GX's culling requires. */
        int at=0;
        for(int p=0;p<begins;p++) {
            int sides=primitives[p]==GX_TRIANGLES?3:4;
            for(int first=at;first<at+primitiveSizes[p];first+=sides) {
                float area=0; indigoPoint_t s[4]; guVector e;
                for(int v=0;v<sides;v++) CHECK(projectRailPoint(&r,positions[first+v].x,
                    positions[first+v].y,positions[first+v].z,&e,&s[v]),"cell left the view");
                for(int v=0;v<sides;v++) area+=s[v].x*s[(v+1)%sides].y-s[(v+1)%sides].x*s[v].y;
                CHECK(area<0.0f,"refraction cell faces away and would be culled");
            }
            at+=primitiveSizes[p];
        }
        if(total>most) most=total;
        poses++;
    }
    CHECK(poses==1152,"pose sweep incomplete");
    /* Measured 930 at the worst pose (700 before bevels were cut along as
     * often as faces and corners became fans); the video thread pays for each. */
    CHECK(most>300 && most<=1200,"refraction vertex budget");
}
static void test_soft_glow(void) {
    reset(1);
    drawSoftGlow(100,80,40,20,(GXColor){10,20,30,255},0.5f);
    CHECK(begins==3 && count==3*(RADIAL_SEGMENTS+1)*2,"glow is not three closed rings");
    static const float ring[4]={0,.28f,.58f,1}, fall[4]={1,.74f,.30f,0};
    for(int band=0;band<3;band++) {
        int first=band*(RADIAL_SEGMENTS+1)*2;
        CHECK(near(positions[first].x,positions[first+RADIAL_SEGMENTS*2].x,.01f) &&
            near(positions[first].y,positions[first+RADIAL_SEGMENTS*2].y,.01f),"ring not closed");
        for(int i=0;i<=RADIAL_SEGMENTS;i++) {
            guVector a=positions[first+i*2],b=positions[first+i*2+1];
            float ra=hypotf((a.x-100)/40,(a.y-80)/20), rb=hypotf((b.x-100)/40,(b.y-80)/20);
            CHECK(near(ra,ring[band],.002f) && near(rb,ring[band+1],.002f),"ring radius");
            CHECK(abs(colors[first+i*2].a-(int)(127.5f*fall[band]+.5f))<=1 &&
                abs(colors[first+i*2+1].a-(int)(127.5f*fall[band+1]+.5f))<=1,"gaussian profile");
        }
    }
    CHECK(colors[3*(RADIAL_SEGMENTS+1)*2-1].a==0,"glow has a hard edge");
    reset(1); drawSoftGlow(1,1,10,10,(GXColor){1,2,3,4},0.0f);
    CHECK(count==0,"an invisible glow drew");
}
static void test_rim(void) {
    cubeOutline_t o={.point={{-100,-100},{100,-100},{100,100},{-100,100}},.count=4};
    reset(1); drawGlassRim(&o,1);
    CHECK(begins==6 && count==3*2*5*2,"rim is not three feathered closed strokes");
    CHECK(blendDst==GX_BL_INVSRCALPHA,"rim left additive blending on");
    /* Outline corners in screen space: (420,340) (420,140) (220,140) (220,340).
     * The upper right faces the key light, the lower left does not. */
    int lit=0, dark=0;
    for(int i=0;i<count;i++) {
        if(colors[i].a==0) continue;
        if(positions[i].x>=410 && positions[i].y<=150) lit++;
        if(positions[i].x<=230 && positions[i].y>=330) dark++;
    }
    CHECK(lit>0 && dark==0,"rim does not follow the key light");
    /* The lit rim peaks on the edge and fades to nothing two pixels out on
     * either side, with no solid core to stair-step: the first band rises
     * from its outer edge, the second falls to its inner one. */
    for(int i=0;i<10;i+=2) {
        CHECK(colors[i].a==0 && colors[10+i+1].a==0,"rim does not fade to nothing");
        CHECK(colors[i+1].a==colors[10+i].a,"rim's two bands disagree on the edge");
        CHECK(near(fmaxf(fabsf(positions[i+1].x-positions[i].x),
            fabsf(positions[i+1].y-positions[i].y)),2.0f,.01f),"rim does not fade over two pixels");
    }
    reset(1); drawGlassRim(&o,0); CHECK(count==0,"a scene without light drew a rim");
}
static unsigned char texels[16];
/* A texel of the studio, read back from GX's RGBA8 tiles: in each 4x4 tile
 * 32 bytes of alpha-red pairs, then 32 of green-blue. */
static int studioAt(int x,int y,int channel) {
    const u8 *tile=glassStudioTexels+((y/4)*(GLASS_STUDIO_SIZE/4)+x/4)*64;
    int at=((y%4)*4+x%4)*2;
    return channel==0?tile[at+1]:(channel==1?tile[32+at]:tile[33+at]);
}
static void test_studio(void) {
    static u8 indigo[sizeof(glassStudioTexels)];
    GXColor cards[GLASS_LIGHTS];
    for(int i=0;i<GLASS_LIGHTS;i++) cards[i]=glassLights[i].color;
    recolor=0; flushes=invalidations=0;
    CHECK(prepareGlassStudio() && flushes==1 && invalidations==1,
        "the studio was not baked, written back and invalidated");
    CHECK(glassStudioTexObj.data==glassStudioTexels && glassStudioTexObj.w==GLASS_STUDIO_SIZE &&
        glassStudioTexObj.h==GLASS_STUDIO_SIZE && glassStudioTexObj.fmt==GX_TF_RGBA8 &&
        sizeof(glassStudioTexels)==21952,"the studio is not a mipmapped 64-texel RGBA8 texture");
    /* Mipmapped, so a bevel sweeping the studio reads a smaller level instead
     * of catching a card's edge in one pixel and missing it in the next. */
    CHECK(glassStudioTexObj.mip==GX_TRUE && glassStudioTexObj.minFilter==GX_LIN_MIP_LIN &&
        near(glassStudioTexObj.maxLod,GLASS_STUDIO_LEVELS-1,1e-6f),"the studio is not mipmapped");
    /* Each level halves the one before, every texel the rounded mean of the
     * four it covers, one whole tile at least per level, in GX's order. */
    {
        const u8 *level=glassStudioTexels;
        for(int size=GLASS_STUDIO_SIZE,n=0;size>1;size>>=1,n++) {
            int tiles=size<4?1:size/4, half=size/2, halfTiles=half<4?1:half/4;
            const u8 *next=level+tiles*tiles*64;
            for(int y=0;y<half;y++) for(int x=0;x<half;x++) for(int c=0;c<4;c++) {
                static const int offset[4]={1,32,33,0};   /* r, g, b, a within a pair */
                int sum=0;
                for(int k=0;k<4;k++) {
                    int sx=2*x+(k&1), sy=2*y+(k>>1);
                    sum+=level[((sy/4)*tiles+sx/4)*64+((sy%4)*4+sx%4)*2+offset[c]];
                }
                CHECK(next[((y/4)*halfTiles+x/4)*64+((y%4)*4+x%4)*2+offset[c]]==(sum+2)/4,
                    "a studio mip texel is not the mean of the four it covers");
            }
            level=next;
            CHECK(n<GLASS_STUDIO_LEVELS,"more studio levels than it declares");
        }
        CHECK(level+64==glassStudioTexels+sizeof(glassStudioTexels),"the studio's levels do not fill it");
    }
    /* Every texel holds the light its direction receives. */
    for(int y=0;y<GLASS_STUDIO_SIZE;y++) for(int x=0;x<GLASS_STUDIO_SIZE;x++) {
        GXColor c=glassStudioLight(glassStudioDirection((x+.5f)/GLASS_STUDIO_SIZE,
            (y+.5f)/GLASS_STUDIO_SIZE),cards);
        CHECK(studioAt(x,y,0)==c.r && studioAt(x,y,1)==c.g && studioAt(x,y,2)==c.b,
            "a studio texel is not its direction's light");
    }
    /* The map and the glass's lookup agree: a direction comes back from its
     * coordinates, up stays up, and every card lights its own place. */
    for(int i=0;i<500;i++) {
        float pitch=asinf(sinf(i*0.61f)),yaw=i*2.39996f,s,t;
        guVector r={cosf(pitch)*sinf(yaw),sinf(pitch),cosf(pitch)*cosf(yaw)};
        if(r.z<-0.95f) continue;
        glassStudioCoords(r,&s,&t);
        guVector back=glassStudioDirection(s,t);
        CHECK(near(back.x,r.x,1e-3f) && near(back.y,r.y,1e-3f) && near(back.z,r.z,1e-3f),
            "the studio's coordinates and directions disagree");
        CHECK(s>=0 && s<=1 && t>=0 && t<=1 && (r.y>0.01f ? t<0.5f : 1) && (r.y<-0.01f ? t>0.5f : 1),
            "the studio is off its map or upside down");
    }
    for(int i=0;i<GLASS_LIGHTS;i++) {
        float s,t;
        glassStudioCoords(glassLights[i].direction,&s,&t);
        int x=(int)(s*GLASS_STUDIO_SIZE),y=(int)(t*GLASS_STUDIO_SIZE);
        x=x>GLASS_STUDIO_SIZE-1?GLASS_STUDIO_SIZE-1:x; y=y>GLASS_STUDIO_SIZE-1?GLASS_STUDIO_SIZE-1:y;
        CHECK(studioAt(x,y,2)>=30,"a card is missing from its place on the map");
    }
    /* Baked once: the next frame with the same Menu Color touches nothing. */
    memcpy(indigo,glassStudioTexels,sizeof(indigo));
    CHECK(!prepareGlassStudio() && flushes==1 && invalidations==1,"the studio was baked again for nothing");
    /* A new Menu Color recolours the cards and bakes it again. */
    recolor=1;
    CHECK(prepareGlassStudio() && flushes==2 && invalidations==2,"a new Menu Color left the studio stale");
    for(int y=0;y<GLASS_STUDIO_SIZE;y++) for(int x=0;x<GLASS_STUDIO_SIZE;x++) {
        const u8 *was=indigo+((y/4)*(GLASS_STUDIO_SIZE/4)+x/4)*64;
        int at=((y%4)*4+x%4)*2;
        CHECK(studioAt(x,y,0)==was[33+at] && studioAt(x,y,2)==was[at+1],
            "the studio was not baked in the new Menu Color");
    }
    recolor=0;
    CHECK(prepareGlassStudio(),"back to Indigo left the studio recoloured");
}
static void test_copy(void) {
    glassEfbWidth=640; glassEfbHeight=480; flushes=copies=0;
    /* The whole frame: the frame at half size. */
    CHECK(glassCopyFrame(0,0,640,480) && copies==1 && flushes==1,"first copy");
    CHECK(copyL==0 && copyT==0 && copyW==640 && copyH==480 && dstW==320 && dstH==240,"copy is not the frame at half size");
    CHECK(dstFmt==GX_TF_RGBA8 && dstMip==GX_TRUE && clearFlag==GX_FALSE,"copy format, filter or clear");
    CHECK(glassCopyFrame(0,0,640,480) && copies==2 && flushes==1,"the buffer was flushed again");
    CHECK(near(glassCopyS()*640,1,1e-6f) && near(glassCopyT()*480,1,1e-6f) &&
        glassCopyS0()==0 && glassCopyT0()==0,"copy coordinates");
    /* Part of the frame: corners snap out to 8 pixels, so each texel holds
     * the same four pixels as in the whole-frame copy, and a frame point
     * lands on the same place in both. */
    CHECK(glassCopyFrame(101.5f,203.2f,250.0f,330.9f),"part of the frame");
    CHECK(copyL==96 && copyT==200 && copyW%8==0 && copyH%8==0,"copy corners not on 8-pixel steps");
    CHECK(copyL+copyW>=251 && copyT+copyH>=331,"copy cut the box short");
    float s=130*glassCopyS()-glassCopyS0(), t=300*glassCopyT()-glassCopyT0();
    CHECK(near(s*dstW,(130-copyL)/2.0f,1e-3f) && near(t*dstH,(300-copyT)/2.0f,1e-3f),
        "a frame point moved in the copy");
    /* Past the frame's edges, it stops at them. */
    CHECK(glassCopyFrame(-50,-20,700,600) && copyL==0 && copyT==0 && copyW==640 && copyH==480,
        "copy left the frame");
    glassEfbHeight=574;
    CHECK(glassCopyFrame(0,0,640,480) && dstH==284 && copyH==568,"odd heights must stay tile-aligned");
    CHECK(glassCopyT()*480>1.0f,"copy coordinates ignore the cropped rows");
    glassEfbHeight=6; CHECK(!glassCopyFrame(0,0,640,480),"a tiny frame was copied");
    (void)texels;
}
static void test_scene_strength(void) {
    uiSceneFrame_t s; memset(&s,0,sizeof(s));
    s.introProgress=1;
    s.scene=UI_SCENE_HOME; s.orbitStrength=1.4f; CHECK(glassSceneStrength(&s)==1,"strength clamp");
    s.orbitStrength=-1; CHECK(glassSceneStrength(&s)==0,"strength clamp low");
    s.scene=UI_SCENE_SETTINGS; s.orbitStrength=0.58f; CHECK(glassSceneStrength(&s)==0,"Settings kept the screen glass");
    s.scene=UI_SCENE_LIBRARY; s.orbitStrength=0.72f; CHECK(near(glassSceneStrength(&s),.72f,1e-6f),"Library strength");
    /* The boot overlay's cube is plain glass, and the light fades in after
     * the background cube takes over at BOOT_CUBE_HANDOFF. */
    s.scene=UI_SCENE_HOME; s.orbitStrength=1;
    s.introProgress=0.5f; CHECK(glassSceneStrength(&s)==0,"the boot overlay's cube was lit");
    s.introProgress=BOOT_CUBE_HANDOFF; CHECK(glassSceneStrength(&s)==0,"the light popped in at the handoff");
    s.introProgress=(BOOT_CUBE_HANDOFF+1)/2; float half=glassSceneStrength(&s);
    CHECK(half>0.3f && half<0.7f,"the light does not fade in after the handoff");
}
/* The glass passes' surfaces meet without a T-junction: no vertex lies
 * inside another cell's side, and no point two surfaces share is made twice
 * and rounded apart. The console snaps vertices to 1/16 pixel, so a point on
 * a shared side that one surface has and its neighbour lacks opens single
 * pixels along the seam, which sparkle as the cube moves. */
static void check_seams(const char *pass) {
    int at=0;
    for(int p=0;p<begins;p++) {
        int sides=primitives[p]==GX_TRIANGLES?3:4;
        for(int first=at;first<at+primitiveSizes[p];first+=sides) for(int v=0;v<sides;v++) {
            guVector a=positions[first+v], b=positions[first+(v+1)%sides];
            guVector ab={b.x-a.x,b.y-a.y,b.z-a.z};
            float length2=ab.x*ab.x+ab.y*ab.y+ab.z*ab.z;
            for(int i=0;i<count;i++) {
                guVector q=positions[i], aq={q.x-a.x,q.y-a.y,q.z-a.z};
                guVector c={ab.y*aq.z-ab.z*aq.y,ab.z*aq.x-ab.x*aq.z,ab.x*aq.y-ab.y*aq.x};
                float along=aq.x*ab.x+aq.y*ab.y+aq.z*ab.z;
                if(aq.x*aq.x+aq.y*aq.y+aq.z*aq.z<1e-10f && (aq.x!=0||aq.y!=0||aq.z!=0)) {
                    fprintf(stderr,"%s: ",pass); CHECK(0,"a shared point was made twice and rounded apart");
                }
                if(along>1e-4f*length2 && along<(1-1e-4f)*length2 &&
                    c.x*c.x+c.y*c.y+c.z*c.z<1e-9f*length2) {
                    fprintf(stderr,"%s: ",pass); CHECK(0,"a vertex lies inside another cell's side (a T-junction)");
                }
            }
        }
        at+=primitiveSizes[p];
    }
}
static void test_seams(void) {
    const GXColor tint[6]={{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4},{1,2,3,4}};
    cubeSurfaceQuad_t shell[6],strips[12],corners[8];
    int poses=0;
    buildCubeFaces(shell,1,.78f,tint); buildChamferStrips(strips,1,.78f,tint,NULL); buildCubeCorners(corners,1,.78f);
    for(int yaw=0;yaw<360;yaw+=45) for(int pitch=-30;pitch<=30;pitch+=15) {
        cubeRasterTransform_t r;
        pose(&r,yaw*INDIGO_TAU/360,pitch*INDIGO_TAU/360,0,1);
        reset(3);
        drawGlassRefraction(&r,&glass0,shell,6,4,1);
        drawGlassRefraction(&r,&glass0,strips,12,4,1);
        drawGlassRefraction(&r,&glass0,corners,8,3,1);
        CHECK(count>0,"no refraction to check"); check_seams("refraction");
        reset(1);
        drawGlassReflection(&r,shell,6,4,1); drawGlassReflection(&r,strips,12,4,1);
        drawGlassReflection(&r,corners,8,3,1);
        CHECK(count>0,"no reflection to check"); check_seams("reflection");
        poses++;
    }
    CHECK(poses==40,"seam pose sweep incomplete");
}
int main(void) {
    test_vertex_optics(); test_refraction_stream(); test_soft_glow();
    test_rim(); test_copy(); test_scene_strength(); test_studio();
    test_seams();
    puts("glass light: refraction, dispersion, studio, glows, rim and copies hold");
    return 0;
}
"""

FUNCTIONS = [
    "static float fastSqrt(",
    "static bool railJoin(", "static bool buildRasterJoins(", "static void drawRasterStroke(",
    "static bool projectRailPoint(", "static guVector cubeViewNormal(",
    "static void buildCubeFaces(", "static void buildChamferStrip(",
    "static guVector bevelCut(", "static void buildChamferBands(",
    "static void buildChamferStrips(", "static void buildCubeCorners(",
    "static float glassSmoothstep(", "static guVector glassVertexNormal(",
    "static bool glassSameNormal(", "static guVector glassBilinear(",
    "static guVector glassSidePoint(", "static guVector glassGridPoint(",
    "static void glassFanPoint(", "static float glassSceneStrength(", "static bool glassCopyFrame(",
    "static float glassCopyS(", "static float glassCopyT(",
    "static float glassCopyS0(", "static float glassCopyT0(",
    "static void refractGlassVertex(", "static void putGlassRefractedVertex(",
    "static void drawGlassRefraction(", "static void drawSoftGlow(",
    "static void drawGlassRim(",
    "static GXColor glassStudioLight(", "static void glassStudioCoords(",
    "static guVector glassStudioDirection(", "static bool prepareGlassStudio(",
    "static u8 glassReflect(", "static void putGlassMirrorVertex(",
    "static void drawGlassReflection(",
]


class GlassLightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (GUI / "indigo_background.c").read_text()
        blocks = []
        for name in ("glassRefraction", "glassRefractedVertex", "glassMirrorVertex"):
            blocks.append(re.search(r"typedef struct " + name + r" \{.*?\} \w+;",
                cls.source, re.S).group(0))
        blocks.append("\n".join(re.findall(r"^#define (?:GLASS_\w+|BEVEL_SEAM_BLEND) .*$",
            cls.source, re.M)))
        blocks.append(re.search(r"static u8 glassTexels\[.*?;\n(?:static .*?;\n)+",
            cls.source).group(0))
        blocks.append(re.search(r"static const struct \{\n\tguVector direction;.*?\} glassLights\[\] = "
            r"\{.*?\n\};", cls.source, re.S).group(0))
        blocks.append(re.search(r"static u8 glassStudioTexels\[.*?;\n(?:static .*?;\n)+",
            cls.source).group(0))
        blocks.append(extract_function((GUI / "ui_motion.c").read_text(),
                                       "float UIMotion_Smoothstep("))
        for signature in FUNCTIONS:
            text = cls.source
            if signature == "static bool railJoin(":
                text = text[text.rindex(signature):]
            blocks.append(extract_function(text, signature))
        cls.emitters = "\n".join(blocks)

    def run_emitters(self, emitters):
        with tempfile.TemporaryDirectory(prefix="swiss-glass-") as directory:
            root = Path(directory)
            (root / "glass.c").write_text(HARNESS.replace("/* EMITTERS */", emitters))
            result = subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                 "-I" + str(GUI), str(root / "glass.c"), "-o", str(root / "glass"), "-lm"],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return subprocess.run([str(root / "glass")], capture_output=True, text=True,
                timeout=30)

    def test_native_glass(self):
        result = self.run_emitters(self.emitters)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_passes_sit_where_the_light_needs_them(self):
        draw = extract_function(self.source, "static void drawCube(")
        # The smoked plates sit in front of the back glass and behind the copy,
        # so the front glass bends them like an inlay; the studio is mirrored
        # over the finished front glass and its pipeline undone after.
        order = [draw.index(token) for token in (
            "drawCubeSurfacePass(&raster, &shellOutline, shell, 6, GX_CULL_FRONT);",
            "drawFacePolygon(&raster, face, pane, 4, 0.86f,",
            "refract = screenGlass && strength > 0.01f && shellOutline.count >= 3 &&",
            "drawGlassRefraction(&raster, &glass, corners, 8, 3, outer);\n\t\t\trestoreCubeRaster();",
            "litCubeTints(&raster, frontGlassColors, lit);",
            "setupGlassReflectionPipeline(",
            "drawGlassReflection(&raster, corners, 8, 3, outer);\n\trestoreCubeRaster();",
            "drawGlassBloom(",
            "loadCubeProjection();\n\t\trestoreCubeRaster();",
            "drawFaceShadows(seconds, animated, clock, pad, icons, &raster, strength);",
            "drawFaceIcons(seconds, animated, clock, pad, icons, &raster);",
            "drawGlassRim(&shellOutline, strength);")]
        self.assertEqual(order, sorted(order),
            "the glass passes left their place between the back glass and the front glass")
        # The studio drifts only while the cube is animated, and only as Home rests.
        self.assertIn("setupGlassReflectionPipeline(animated ? GLASS_STUDIO_DRIFT * "
            "scene->homeIdleBlend *\n\t\tsinf(seconds * GLASS_STUDIO_DRIFT_RATE) : 0.0f);", draw)
        # The icons come after the bloom so their strokes stay sharp, on the
        # cube's own projection again (the bloom leaves an orthographic one).
        self.assertIn("if(light && screenGlass) {", draw)
        boot = extract_function(self.source, "void IndigoBackground_DrawBootOverlay(")
        self.assertIn("drawCube(scene, seconds, animated, clock, NULL, icons, false);", boot,
            "the boot overlay must never copy the frame: widgets sit under it")
        home = extract_function(self.source, "void IndigoBackground_Draw(")
        self.assertIn("drawCube(scene, seconds, cubeMotionActive, clock, pad, icons, true);", home)
        # No light behind the cube: Spencer took out the glow spot on the
        # floor (2026-09-30) and then the halo round it (2026-10-01).
        self.assertNotIn("drawCubeLight", self.source)

    def test_the_glass_is_clear(self):
        # No solid cube inside: no pass writes depth or draws opaque, and the
        # face plates are the only thing set into the glass.
        for name in ("static void drawCube(", "static void setupCubePipeline(",
                     "static void drawCubeSurfacePassVertices(",
                     "void IndigoBackground_DrawSavesBackdrop("):
            body = extract_function(self.source, name)
            self.assertNotIn("GX_BM_NONE", body, name)
            self.assertNotRegex(body, r"GX_SetZMode\([^)]*GX_TRUE\)", name)
        self.assertNotIn("coreColors", self.source)
        draw = extract_function(self.source, "static void drawCube(")
        self.assertEqual(draw.count("drawFacePolygon(&raster, face, pane, 4, 0.86f,"), 1)

    def test_pipelines(self):
        def attributes(source):
            self.assertIn("GX_ClearVtxDesc();", source)
            return set(re.findall(r"GX_SetVtxDesc\((GX_VA_\w+),\s*GX_DIRECT\)", source))
        refract = extract_function(self.source, "static void setupGlassRefractionPipeline(")
        restore = extract_function(self.source, "static void restoreCubeRaster(")
        self.assertEqual(attributes(refract),
            {"GX_VA_POS", "GX_VA_CLR0", "GX_VA_TEX0", "GX_VA_TEX1", "GX_VA_TEX2"})
        self.assertIn("GX_SetNumTexGens(3);", refract)
        self.assertIn("GX_SetNumTevStages(4);", refract)
        self.assertIn("GX_CS_SCALE_2", refract)
        self.assertIn("GX_LoadTexObj(&glassTexObj, GX_TEXMAP0);", refract,
            "540p tiles replay the refraction; it must bind its own copy, not the font sheet")
        self.assertEqual(attributes(restore), {"GX_VA_POS", "GX_VA_CLR0"})
        self.assertIn("GX_SetNumTexGens(0);", restore)
        self.assertIn("GX_SetNumTevStages(1);", restore)
        # The reflection: one texture coordinate through a turning matrix, the
        # studio in TEXMAP1 beside the copy's TEXMAP0, one TEV stage, added.
        mirror = extract_function(self.source, "static void setupGlassReflectionPipeline(")
        self.assertEqual(attributes(mirror), {"GX_VA_POS", "GX_VA_CLR0", "GX_VA_TEX0"})
        self.assertIn("GX_SetNumTexGens(1);", mirror)
        self.assertIn("GX_SetNumTevStages(1);", mirror)
        self.assertIn("GX_LoadTexObj(&glassStudioTexObj, GX_TEXMAP1);", mirror)
        self.assertIn("GX_LoadTexMtxImm(turn, GX_TEXMTX0, GX_MTX2x4);", mirror)
        self.assertIn("GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);", mirror)
        self.assertIn("GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);", mirror)
        self.assertIn("GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);", mirror)
        self.assertLess(mirror.index("prepareGlassStudio();"), mirror.index("GX_LoadTexObj("))
        bloom = extract_function(self.source, "static void drawGlassBloom(")
        self.assertIn("UIColor_Apply(&threshold.r, &threshold.g, &threshold.b);", bloom,
            "the bloom threshold must follow Menu Color with the glass it cuts")
        self.assertIn("GX_TEV_SUB", bloom)
        self.assertIn("GX_SetBlendMode(GX_BM_BLEND, GX_BL_INVDSTCLR, GX_BL_ONE, GX_LO_CLEAR);", bloom,
            "the bloom must screen, not add: added, a face turning through the light burns white")
        self.assertIn("glow.b = (u8)(glow.b * strength + 0.5f);", bloom,
            "a screen has no source alpha: the strength must ride in the glow colour")
        self.assertLess(bloom.index("glassCopyFrame(UIStage_FrameX(left)"), bloom.index("GX_Begin("))
        self.assertIn("setupRasterPipeline();\n}", bloom, "bloom left its TEV stages behind")
        frame = (GUI / "FrameBufferMagic.c").read_text()
        background = extract_function(frame, "static void _DrawBackground(")
        self.assertLess(background.index("IndigoBackground_SetFramebuffer("),
            background.index("IndigoBackground_Draw("))

    def test_regressions_are_rejected(self):
        mutants = {
            "no silhouette fade": ("(float)glass->tint.a * glassSmoothstep(0.0f, 0.34f, facing) + 0.5f",
                "(float)glass->tint.a + 0.5f * facing"),
            "refraction pops in at boot": ("(float)glass->tint.a * glassSmoothstep(0.0f, 0.34f, facing)",
                "255.0f * glassSmoothstep(0.0f, 0.34f, facing)"),
            "dispersion reversed": ("float k = 1.0f + glass->dispersion * (float)(channel - 1);",
                "float k = 1.0f - glass->dispersion * (float)(channel - 1);"),
            "no dispersion": ("float k = 1.0f + glass->dispersion * (float)(channel - 1);",
                "float k = 1.0f;"),
            "bend away from the middle": ("float ox = -n.x * glass->bend, oy = n.y * glass->bend;",
                "float ox = n.x * glass->bend, oy = n.y * glass->bend;"),
            "bend upside down": ("float ox = -n.x * glass->bend, oy = n.y * glass->bend;",
                "float ox = -n.x * glass->bend, oy = -n.y * glass->bend;"),
            "no magnification": ("(sx - glass->centerX) * (1.0f - glass->magnify)",
                "(sx - glass->centerX)"),
            "back-facing refraction": ("\t\tif(area >= -0.001f) continue;\n\t\tif(vertexCount == 3) {\n"
                "\t\t\t/* A fan, its sides cut as the bevel ends it closes are cut across. */",
                "\t\tif(area >= 1e30f) continue;\n\t\tif(vertexCount == 3) {\n"
                "\t\t\t/* A fan, its sides cut as the bevel ends it closes are cut across. */"),
            # Sparkles: a bevel cut along apart from the face beside it, or a
            # corner fan that misses the bevel ends' cuts, is a T-junction.
            "bevel cut apart from its face": ("ACROSS_STEPS = 6, ALONG_STEPS = FACE_STEPS, GRID = 7 };",
                "ACROSS_STEPS = 6, ALONG_STEPS = 3, GRID = 7 };"),
            "corner fan cut apart from its bevels": (
                "*body = glassSidePoint(corner[side], corner[next], cut, cuts);",
                "*body = glassSidePoint(corner[side], corner[next], cut, cuts + 1);"),
            "copy clears the frame": ("GX_CopyTex(glassTexels, GX_FALSE);", "GX_CopyTex(glassTexels, GX_TRUE);"),
            "copy without the box filter": ("GX_SetTexCopyDst(width, height, GX_TF_RGBA8, GX_TRUE);",
                "GX_SetTexCopyDst(width, height, GX_TF_RGBA8, GX_FALSE);"),
            "no cache write-back": ("\t\tDCFlushRange(glassTexels, sizeof(glassTexels));\n", ""),
            "flush every frame": ("\t\tglassTexelsFlushed = true;\n", ""),
            "untiled height": ("if(y1 > (int)(glassEfbHeight & ~7u)) y1 = (int)(glassEfbHeight & ~7u);",
                "if(y1 > (int)glassEfbHeight) y1 = (int)glassEfbHeight;"),
            "hard glow edge": ("{1.0f, 0.74f, 0.30f, 0.0f}", "{1.0f, 0.74f, 0.30f, 0.1f}"),
            "open glow ring": ("for(int i = 0; i <= RADIAL_SEGMENTS; i++) {\n\t\t\tputVertex((indigoPoint_t) {x + radiusX * ring",
                "for(int i = 0; i < RADIAL_SEGMENTS; i++) {\n\t\t\tputVertex((indigoPoint_t) {x + radiusX * ring"),
            "rim on the dark side": ("float lit = nx * lightX + ny * lightY;", "float lit = -(nx * lightX + ny * lightY);"),
            "light pops in after boot": ("return strength * glassSmoothstep(BOOT_CUBE_HANDOFF, 1.0f, scene->introProgress);",
                "return strength;"),
            "Settings keeps screen glass": ("\t\treturn 0.0f;\n\t}\n\t/* During the boot reveal",
                "\t\treturn strength;\n\t}\n\t/* During the boot reveal"),
            "studio untiled": ("\t\t\ttile[at] = light.a;\n\t\t\ttile[at + 1] = light.r;",
                "\t\t\ttile[at] = light.r;\n\t\t\ttile[at + 1] = light.a;"),
            "studio without mipmaps": ("GLASS_STUDIO_SIZE, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_TRUE);",
                "GLASS_STUDIO_SIZE, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);"),
            "studio mips not averaged": ("(u8)((a[0].r + a[1].r + b[0].r + b[1].r + 2) >> 2),",
                "(u8)(a[0].r),"),
            "rim fades too sharply": ("static const float profile[3] = {-2.0f, 0.0f, 2.0f};",
                "static const float profile[3] = {-1.0f, 0.0f, 1.0f};"),
            "studio not written back": ("\tDCFlushRange(glassStudioTexels, sizeof(glassStudioTexels));\n", ""),
            "studio baked every frame": ("\tif(same) return false;", "\tif(same && recolor > 1) return false;"),
            "studio stale after Menu Color": (
                "\t\tsame = same && colors[i].r == glassStudioColors[i].r &&\n"
                "\t\t\tcolors[i].g == glassStudioColors[i].g && colors[i].b == glassStudioColors[i].b;",
                "\t\t(void)glassStudioColors;"),
            "studio upside down": ("float x = 2.0f * s - 1.0f, y = 1.0f - 2.0f * t;",
                "float x = 2.0f * s - 1.0f, y = 2.0f * t - 1.0f;"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(name=name):
                self.assertIn(old, self.emitters)
                result = self.run_emitters(self.emitters.replace(old, new, 1))
                self.assertNotEqual(result.returncode, 0, "mutant survived: " + name)


if __name__ == "__main__":
    unittest.main()
