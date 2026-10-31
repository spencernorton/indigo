/* Just enough of libogc's gccore.h for test_frame_budget.py to compile the
 * cube renderer on the host; the GX calls are counting stubs in
 * frame_budget.c. */
#ifndef STUB_GCCORE_H
#define STUB_GCCORE_H
#include <gctypes.h>
#include <stddef.h>
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { float x, y, z; } guVector;
typedef float Mtx[3][4];
typedef float Mtx44[4][4];
typedef float (*MtxP)[4];
typedef struct { u32 val[8]; } GXTexObj;
enum {
 GX_PNMTX0 = 0, GX_TEXMTX0 = 30, GX_IDENTITY = 60, GX_MTX2x4 = 1,
 GX_ORTHOGRAPHIC = 1, GX_PERSPECTIVE = 0,
 GX_DISABLE = 0, GX_ENABLE = 1, GX_FALSE = 0, GX_TRUE = 1,
 GX_CLIP_ENABLE = 0, GX_ALWAYS = 7, GX_LEQUAL = 3, GX_AOP_AND = 0,
 GX_DIRECT = 1, GX_VA_POS = 9, GX_VA_CLR0 = 11, GX_VA_TEX0 = 13, GX_VA_TEX1 = 14, GX_VA_TEX2 = 15,
 GX_VTXFMT0 = 0, GX_POS_XYZ = 1, GX_F32 = 4, GX_CLR_RGBA = 1, GX_RGBA8 = 5, GX_TEX_ST = 1,
 GX_TEVSTAGE0 = 0, GX_TEVSTAGE1, GX_TEVSTAGE2, GX_TEVSTAGE3, GX_TEVSTAGE4, GX_TEVSTAGE5, GX_TEVSTAGE6,
 GX_TEXCOORD0 = 0, GX_TEXCOORD1, GX_TEXCOORD2, GX_TEXCOORDNULL = 0xff,
 GX_TEXMAP0 = 0, GX_TEXMAP1 = 1, GX_TEXMAP_NULL = 0xff, GX_COLOR0A0 = 4,
 GX_CC_CPREV = 0, GX_CC_TEXC = 8, GX_CC_RASC = 10, GX_CC_ONE = 12, GX_CC_KONST = 14, GX_CC_ZERO = 15,
 GX_CA_APREV = 0, GX_CA_RASA = 5, GX_CA_ZERO = 7,
 GX_TEV_ADD = 0, GX_TEV_SUB = 1, GX_TB_ZERO = 0, GX_CS_SCALE_1 = 0, GX_CS_SCALE_2 = 1, GX_CS_SCALE_4 = 2,
 GX_TEVPREV = 0, GX_KCOLOR0 = 0, GX_KCOLOR1, GX_KCOLOR2,
 GX_TEV_KCSEL_1 = 0, GX_TEV_KCSEL_K0 = 12, GX_TEV_KCSEL_K1 = 13, GX_TEV_KCSEL_K2 = 14, GX_TEV_KASEL_1 = 0,
 GX_BM_BLEND = 1, GX_BL_ZERO = 0, GX_BL_ONE = 1, GX_BL_SRCALPHA = 4, GX_BL_INVSRCALPHA = 5, GX_BL_INVDSTCLR = 3,
 GX_LO_CLEAR = 0, GX_CULL_NONE = 0, GX_CULL_FRONT = 1, GX_CULL_BACK = 2,
 GX_QUADS = 0x80, GX_TRIANGLES = 0x90, GX_TRIANGLESTRIP = 0x98, GX_TRIANGLEFAN = 0xA0,
 GX_TG_MTX2x4 = 1, GX_TG_POS = 0, GX_TG_TEX0 = 4, GX_TG_TEX1 = 5, GX_TG_TEX2 = 6,
 GX_TF_RGBA8 = 6, GX_TF_RGB565 = 4, GX_TF_RGB5A3 = 5, GX_TEXMAP2 = 2, GX_TEXMAP3 = 3, GX_CLAMP = 0, GX_LINEAR = 1, GX_LIN_MIP_LIN = 5, GX_ANISO_1 = 0
};
/* The Library's jewel case (gui/ui_jewel_case.c): its rods, indexed in a
 * display list, and its own vertex formats, textures and TEV inputs. */
enum {
 GX_VTXFMT5 = 5, GX_VTXFMT6 = 6, GX_INDEX16 = 3, GX_VA_NRM = 10, GX_NRM_XYZ = 0,
 GX_TF_I8 = 1, GX_TG_NRM = 1, GX_TEXMTX1 = 33, GX_TEXMTX2 = 36, GX_PASSCLR = 4, GX_MODULATE = 0,
 GX_TEVREG0 = 1, GX_CC_C0 = 2, GX_CA_A0 = 1, GX_CA_TEXA = 4, GX_CA_KONST = 6,
 GX_TEV_KASEL_K0_A = 28, GX_SRC_REG = 0, GX_SRC_VTX = 1, GX_LIGHTNULL = 0, GX_DF_NONE = 0,
 GX_AF_NONE = 2, GX_COLORNULL = 0xff
};
void GX_SetArray(u32 attr, void *ptr, u8 stride);
void GX_Position1x16(u16 index); void GX_Normal1x16(u16 index);
void GX_BeginDispList(void *list, u32 size); u32 GX_EndDispList(void);
void GX_CallDispList(void *list, u32 size);
void GX_SetTevColor(u8 reg, GXColor color);
void guMtxRotRad(Mtx m, char axis, float rad); void guMtxScale(Mtx m, float x, float y, float z);
void DCInvalidateRange(void *p, u32 n);
#define GX_SetCurrentMtx(...) STUBCALL()
#define GX_SetChanCtrl(...) STUBCALL()
#define GX_SetTevOp(...) STUBCALL()
/* Counting stubs, defined in frame_budget.c */
void GX_Begin(u8 p, u8 f, u16 n); void GX_End(void);
void GX_Position3f32(float x, float y, float z); void GX_Color4u8(u8 r, u8 g, u8 b, u8 a);
void GX_TexCoord2f32(float s, float t);
void GX_CopyTex(void *dst, u8 clear); void GX_SetTexCopySrc(u16 l, u16 t, u16 w, u16 h);
void GX_SetTexCopyDst(u16 w, u16 h, u32 fmt, u8 mip);
void GX_SetNumTevStages(u8 n);
void GX_LoadPosMtxImm(const float m[3][4], u32 i); void GX_LoadProjectionMtx(Mtx44 m, u8 t);
void GX_LoadTexMtxImm(Mtx m, u32 i, u8 t);
void GX_SetBlendMode(u8 a, u8 b, u8 c, u8 d);
void GX_InvalidateTexAll(void); void GX_PixModeSync(void);
void GX_InitTexObj(GXTexObj *o, void *p, u16 w, u16 h, u8 f, u8 s, u8 t, u8 m);
void GX_InitTexObjLOD(GXTexObj *o, u8 a, u8 b, float c, float d, float e, u8 f, u8 g, u8 h);
void GX_LoadTexObj(GXTexObj *o, u8 m);
void GX_SetTevKColor(u8 i, GXColor c);
void DCFlushRange(void *p, u32 n);
typedef float f32;
#define GX_SetScissor(...) STUBCALL()
extern long stubStateCalls;
#define STUBCALL(...) ((void)(stubStateCalls++))
#define GX_SetCoPlanar(...) STUBCALL()
#define GX_SetClipMode(...) STUBCALL()
#define GX_SetAlphaCompare(...) STUBCALL()
#define GX_SetZMode(...) STUBCALL()
#define GX_ClearVtxDesc(...) STUBCALL()
#define GX_SetVtxDesc(...) STUBCALL()
#define GX_SetVtxAttrFmt(...) STUBCALL()
#define GX_SetNumChans(...) STUBCALL()
#define GX_SetNumTexGens(...) STUBCALL()
#define GX_SetNumIndStages(...) STUBCALL()
#define GX_SetTevOrder(...) STUBCALL()
#define GX_SetTevColorIn(...) STUBCALL()
#define GX_SetTevColorOp(...) STUBCALL()
#define GX_SetTevAlphaIn(...) STUBCALL()
#define GX_SetTevAlphaOp(...) STUBCALL()
#define GX_SetTevDirect(...) STUBCALL()
#define GX_SetColorUpdate(...) STUBCALL()
#define GX_SetCullMode(...) STUBCALL()
#define GX_SetTexCoordGen(...) STUBCALL()
#define GX_SetTevKColorSel(...) STUBCALL()
#define GX_SetTevKAlphaSel(...) STUBCALL()
void guMtxIdentity(Mtx m); void guMtxCopy(Mtx a, Mtx b); void guMtxConcat(Mtx a, Mtx b, Mtx ab);
void guMtxRotAxisRad(Mtx m, const guVector *axis, float rad);
void guMtxScaleApply(Mtx src, Mtx dst, float x, float y, float z);
void guMtxTransApply(Mtx src, Mtx dst, float x, float y, float z);
void guPerspective(Mtx44 m, float fovy, float aspect, float n, float f);
void guOrtho(Mtx44 m, float t, float b, float l, float r, float n, float f);
#define PAD_BUTTON_LEFT 0x0001
#define PAD_BUTTON_RIGHT 0x0002
#define PAD_BUTTON_DOWN 0x0004
#define PAD_BUTTON_UP 0x0008
#define PAD_TRIGGER_Z 0x0010
#define PAD_TRIGGER_R 0x0020
#define PAD_TRIGGER_L 0x0040
#define PAD_BUTTON_A 0x0100
#define PAD_BUTTON_B 0x0200
#define PAD_BUTTON_X 0x0400
#define PAD_BUTTON_Y 0x0800
#define PAD_BUTTON_START 0x1000
#endif
