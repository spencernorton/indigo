/* -----------------------------------------------------------
      IPLFontWrite.c - Font blitter for IPL ROM fonts
	      - by emu_kidid
	   
      Version 1.0 11/11/2009
        - Initial Code
   ----------------------------------------------------------- */

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>
#include <string.h>
#include "IPLFontWrite.h"
#include "ui_color.h"
#include "ui_lang.h"
#include "ui_stage.h"
#include "ui_font_cyrillic.inc"

static u8 fontData[SYS_FONTSIZE_ANSI] ATTRIBUTE_ALIGN (32);
static sys_fontheader *font = (sys_fontheader *)fontData;
static bool fontReady;
/* The Cyrillic sheet stands in for an IPL sheet, so it needs the IPL's
 * layout: I4, as wide, and cells as tall. */
static bool cyrillicReady;

GXTexObj fontTexObj;
GXColor defaultColor = (GXColor) {255,255,255,255};
GXColor disabledColor = (GXColor) {175,175,182,255};
GXColor deSelectedColor = (GXColor) {80,80,73,255};

void init_font(void)
{
	SYS_SetFontEncoding(SYS_FONTENC_ANSI);
	if(SYS_InitFont(font)) {
		fontReady = true;
		GX_InitTexObj(&fontTexObj, NULL, font->sheet_width, font->sheet_height, font->sheet_format, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_InitTexObjLOD(&fontTexObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_TRUE, GX_TRUE, GX_ANISO_4);
		cyrillicReady = font->sheet_format == GX_TF_I4 &&
			font->sheet_width == UI_FONT_CYRILLIC_SHEET_WIDTH &&
			font->cell_height == UI_FONT_CYRILLIC_CELL;
		DCFlushRange(uiFontCyrillicSheet, sizeof(uiFontCyrillicSheet));
		/* DrawInit publishes the immutable sheets before video starts. */
		GX_InvalidateTexAll();
	}
}

/* The character at text, from the IPL's sheets or the Cyrillic one: its
 * sheet, its cell's corner and its width; how many bytes it takes. A
 * Cyrillic letter without its sheet is drawn as '?'. */
static int fontCharacter(const char *text, void **sheet, int *s0, int *t0,
		int *width)
{
	int bytes;
	unsigned code = UILang_Character(text, &bytes);

	if(bytes == 2) {
		int glyph = code >= 0x410u && code <= 0x44fu ? (int)code - 0x410 : 64;

		while(glyph < UI_FONT_CYRILLIC_GLYPHS && uiFontCyrillicCodes[glyph] != code) {
			glyph++;
		}
		if(cyrillicReady && glyph < UI_FONT_CYRILLIC_GLYPHS) {
			*sheet = uiFontCyrillicSheet;
			*s0 = glyph % UI_FONT_CYRILLIC_COLUMNS * UI_FONT_CYRILLIC_CELL;
			*t0 = glyph / UI_FONT_CYRILLIC_COLUMNS * UI_FONT_CYRILLIC_CELL;
			*width = uiFontCyrillicWidths[glyph];
			return bytes;
		}
		code = '?';
	}
	else if(code >= 0xd0u && code <= 0xd2u && text[1] == '\0') {
		/* Half a letter, where a byte budget (snprintf's) cut the text at
		 * its end: nothing, not an IPL letter. Cuts before an ellipsis
		 * keep whole letters (UILang_Head). */
		SYS_GetFontTexture(' ', sheet, s0, t0, width);
		*width = 1;
		return bytes;
	}
	SYS_GetFontTexture((unsigned char)code, sheet, s0, t0, width);
	return bytes;
}

/* The character at text's width, as fontCharacter draws it. A byte that
 * can't start a Cyrillic letter reads its width alone, as before, so the
 * measures text fitting repeats cost English nothing more. */
static int fontCharacterWidth(const char *text, int *bytes)
{
	void *sheet;
	int s0, t0, width;
	unsigned char c = (unsigned char)*text;

	if(c < 0xd0u || c > 0xd2u) {
		*bytes = c ? 1 : 0;
		return SYS_GetFontWidth(c);
	}
	*bytes = fontCharacter(text, &sheet, &s0, &t0, &width);
	return width;
}

int fontCellHeight(void)
{
	return fontReady ? font->cell_height : 0;
}

/* The font sheet's texel (x, y) as coverage: I4 in 8x8 tiles, I8 and IA4
 * (alpha in the high nibble) in 8x4, as GX lays them out. */
static u8 fontTexel(const u8 *sheet, int x, int y)
{
	int tiles = font->sheet_width / 8;

	switch(font->sheet_format) {
		case GX_TF_I4: {
			u8 pair = sheet[((y / 8) * tiles + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];

			return (u8)(((x & 1) ? pair & 15 : pair >> 4) * 17);
		}
		case GX_TF_I8:
			return sheet[((y / 4) * tiles + x / 8) * 32 + (y % 4) * 8 + x % 8];
		case GX_TF_IA4:
			return (u8)((sheet[((y / 4) * tiles + x / 8) * 32 + (y % 4) * 8 + x % 8] >> 4) * 17);
		default:
			return 0;
	}
}

bool fontGlyph(unsigned char c, u8 *coverage, int stride, int maxWidth,
	int *width)
{
	void *image;
	int s0, t0, w, x, y;

	if(!fontReady || (font->sheet_format != GX_TF_I4 &&
		font->sheet_format != GX_TF_I8 && font->sheet_format != GX_TF_IA4)) {
		return false;
	}
	SYS_GetFontTexture(c, &image, &s0, &t0, &w);
	if(image == NULL || w < 1 || w > maxWidth) {
		return false;
	}
	for(y = 0; y < font->cell_height; y++) {
		for(x = 0; x < w; x++) {
			coverage[y * stride + x] = fontTexel(image, s0 + x, t0 + y);
		}
	}
	*width = w;
	return true;
}

void drawFontInit(void)
{
	Mtx44 GXprojection2D;
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	guOrtho(GXprojection2D, 0, 480, 0, 640, 0, 1);
	UIStage_Project(GXprojection2D);
	GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);

	GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_FALSE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	//set vertex attribute formats here
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XY, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_S16, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_ENABLE, 1, 1);

	/* The IPL sheets stay immutable after init_font publishes them. */
	GX_LoadTexObj(&fontTexObj, GX_TEXMAP0);

	GX_SetNumIndStages (0);
	GX_SetNumTevStages (2);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0); // change to (u8) tile later
	GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE0);
	GX_SetTevOrder (GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASA, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE1);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

static void drawStringWeighted(int x, int y, const char *string, float scale,
		int align, GXColor fontColor, bool mediumWeight)
{
	if(string == NULL) {
		return;
	}
	string = UILang_Text(string);
	drawFontInit();
	Mtx GXmodelView2D;
	int strWidth = 0;
	int strHeight = font->cell_height;
	int passes = mediumWeight ? 2 : 1;
	int pass;
	if(align)
	{
		const char* string_work = string;
		while(*string_work)
		{
			int bytes;
			strWidth += fontCharacterWidth(string_work, &bytes) - 1;
			string_work += bytes;
		}
	}
	for(pass=0; pass<passes; pass++) {
		const char *cursor = string;
		int cursorX = 0;
		int screenOffset = mediumWeight && pass == 0 ? 1 : 0;
		u8 alpha = mediumWeight && pass == 0 ?
			(u8)(((u16)fontColor.a * 3u) / 4u) : fontColor.a;
		guMtxTrans(GXmodelView2D, -(align*strWidth)/2, -strHeight/2, 0);
		guMtxScaleApply(GXmodelView2D, GXmodelView2D, scale, scale, 1);
		/* Apply weight after glyph scaling so the coverage pass remains exactly
		 * one native output pixel even for small 480p/480i text. */
		guMtxTransApply(GXmodelView2D, GXmodelView2D, x + screenOffset, y, 0);
		GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
		/* A run shares one sheet and one weight pass. Keep the glyph data on
		 * the stack: no allocation in a frame, and no 16-bit GX count can
		 * overflow even for a very long file name. */
		while(*cursor && *cursor != '\n') {
			struct { int s, t, width; } glyphs[64];
			void *image = NULL;
			const char *end = cursor;
			int count = 0;

			while(*end && *end != '\n' && count < 64) {
				void *sheet;
				int s0, t0, width;
				int bytes = fontCharacter(end, &sheet, &s0, &t0, &width);

				if(count > 0 && sheet != image) break;
				image = sheet;
				glyphs[count].s = s0;
				glyphs[count].t = t0;
				glyphs[count].width = width;
				count++;
				end += bytes;
			}
			if(GX_GetTexObjData(&fontTexObj) != (void *)MEM_VIRTUAL_TO_PHYSICAL(image)) {
				GX_InitTexObjData(&fontTexObj, image);
				GX_LoadTexObj(&fontTexObj, GX_TEXMAP0);
			}
			GX_Begin(GX_QUADS, GX_VTXFMT1, (u16)(count * 4));
			for(int glyph = 0; glyph < count; glyph++) {
				int width = glyphs[glyph].width;

				for(int i = 0; i < 4; i++) {
					int s = (i & 1) ^ ((i & 2) >> 1) ? width : 0;
					int t = (i & 2) ? font->cell_height : 0;
					GX_Position2s16(cursorX + s, t);
					GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, alpha);
					GX_TexCoord2s16(glyphs[glyph].s + s, glyphs[glyph].t + t);
				}
				cursorX += width - 1;
			}
			GX_End();
			cursor = end;
		}
	}
}

void drawString(int x, int y, const char *string, float scale, int align,
		GXColor fontColor)
{
	UIColor_Apply(&fontColor.r, &fontColor.g, &fontColor.b);
	drawStringWeighted(x, y, string, scale, align, fontColor, false);
}

void drawStringMedium(int x, int y, const char *string, float scale, int align,
		GXColor fontColor)
{
	UIColor_Apply(&fontColor.r, &fontColor.g, &fontColor.b);
	drawStringWeighted(x, y, string, scale, align, fontColor, true);
}

/* drawStringMedium in exactly fontColor under every Menu Color: the letters
 * on the controller-button icons keep the controller's own colors. */
void drawStringMediumUntinted(int x, int y, const char *string, float scale,
		int align, GXColor fontColor)
{
	drawStringWeighted(x, y, string, scale, align, fontColor, true);
}

void drawStringWithCaret(int x, int y, const char *string, float scale, int align, GXColor fontColor, int caretPosition, GXColor caretColor)
{
	if(string == NULL) {
		string = "";
	}
	UIColor_Apply(&fontColor.r, &fontColor.g, &fontColor.b);
	UIColor_Apply(&caretColor.r, &caretColor.g, &caretColor.b);
	drawFontInit();
	Mtx GXmodelView2D;
	int strWidth = 0;
	int strHeight = font->cell_height;
	if(align)
	{
		const char* string_work = string;
		while(*string_work)
		{
			unsigned char c = *string_work;
			strWidth += SYS_GetFontWidth(c) - 1;
			string_work++;
		}
	}
	guMtxTrans(GXmodelView2D, -(align*strWidth)/2, -strHeight/2, 0);
	guMtxScaleApply(GXmodelView2D, GXmodelView2D, scale, scale, 1);
	guMtxTransApply(GXmodelView2D, GXmodelView2D, x, y, 0);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	x = 0; y = 0;

	int pos = 0;
	while (*string || pos <= caretPosition)
	{
		unsigned char c = *string;
		if(pos == caretPosition) { 
			c = '|';
		}
		if(c == '\n') break;
		void *image;
		int s0, t0, width;
		SYS_GetFontTexture(c, &image, &s0, &t0, &width);
		if (GX_GetTexObjData(&fontTexObj) != (void *)MEM_VIRTUAL_TO_PHYSICAL(image)) {
			GX_InitTexObjData(&fontTexObj, image);
			GX_LoadTexObj(&fontTexObj, GX_TEXMAP0);
		}
		int i;
		GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
		for (i=0; i<4; i++) {
			int s = (i & 1) ^ ((i & 2) >> 1) ? width : 0;
			int t = (i & 2) ? font->cell_height : 0;
			GX_Position2s16(x + s, y + t);
			if(pos == caretPosition)
				GX_Color4u8(caretColor.r, caretColor.g, caretColor.b, caretColor.a);
			else
				GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
			GX_TexCoord2s16(s0 + s, t0 + t);
		}
		GX_End();

		x += width - 1;
		if(pos != caretPosition)
			string++;
		pos++;
	}
}

int GetFontHeight(float scale)
{
	int strHeight = font->cell_height * scale;
	return strHeight;
}

int GetTextSizeInPixels(const char *string)
{
	int strWidth = 0;
	const char* string_work = UILang_Text(string);
	while(*string_work)
	{
		int bytes;
		strWidth += fontCharacterWidth(string_work, &bytes) - 1;
		string_work += bytes;
	}
	return strWidth;
}

float GetTextScaleToFitInWidth(const char *string, int width) {
	int strWidth = 0;
	const char* string_work = UILang_Text(string);
	while(*string_work)
	{
		int bytes;
		if(*string_work == '\n') break;
		strWidth += fontCharacterWidth(string_work, &bytes) - 1;
		string_work += bytes;
	}
	return width>strWidth ? 1.0f : (float)((float)width/(float)strWidth);
}

float GetTextScaleToFitInWidthWithMax(const char *string, int width, float max) {
	float scale = GetTextScaleToFitInWidth(string, width);
	return max < scale ? max:scale;
}
