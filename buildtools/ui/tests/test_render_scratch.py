#!/usr/bin/env python3
"""Check the target's sharpened font and poster pixels against v3.0.0."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

GUI = Path(__file__).resolve().parents[3] / "cube/swiss/source/gui"
# Digest of four complete I8 sheets and a three-level poster in big-endian
# RGB565, produced by the released filters on the same input.
REFERENCE = "d10db519e2cdaf7ed869ed87796dcd37a08602fc2cd0a50c34e78ebf7a0fb116"

STUBS = """
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
#define DCFlushRange(p,n) ((void)0)
enum {GX_TF_I4, GX_TF_I8, GX_TF_IA4};
static struct {int sheet_format, sheet_width, sheet_height, cell_width, cell_height;} header, *font = &header;
static bool fontReady = true;
static u8 ipl[512*512/2];
static void SYS_GetFontTexture(int c, void **sheet, int *s, int *t, int *w) { *sheet = ipl; }
typedef struct {int canvasW,canvasH,contentW,srMipLevels,srBytes;} uiAssetsShape_t;
"""

DRIVER = """
static unsigned state = 81372;
static u8 sample(void) { state = state * 1664525u + 1013904223u; return (u8)(state >> 24); }
int main(void) {
    for(size_t i = 0; i < sizeof(ipl); ++i) ipl[i] = sample();
    font->sheet_format = GX_TF_I4;
    font->sheet_width = font->sheet_height = 512;
    const int cells[][2] = {{1,1},{24,24},{17,31},{512,512}};
    for(size_t i = 0; i < sizeof(cells)/sizeof(cells[0]); ++i) {
        font->cell_width = cells[i][0]; font->cell_height = cells[i][1];
        buildSharpenedFontSheetI8();
        if(!fontSheetI8Ready) return 1;
        fwrite(fontSheetI8, 1, sizeof(fontSheetI8), stdout);
    }
    enum {pixels = 320*256 + 160*128 + 80*64};
    static u8 source[pixels/2];
    static u16 poster[pixels];
    for(size_t i = 0; i < sizeof(source); ++i) source[i] = sample();
    uiAssetsShape_t shape = {320,256,300,3,sizeof(poster)};
    buildSuperResTexture(&shape, source, (u8 *)poster);
    for(size_t i = 0; i < pixels; ++i) { putchar(poster[i] >> 8); putchar(poster[i] & 255); }
    return ferror(stdout) != 0;
}
"""


def render(font, art):
    font_filter = font[font.index("#define FONT_I8_L0_BYTES"):font.index("#endif", font.index("#define FONT_I8_L0_BYTES"))]
    texel = font[font.index("static u8 fontTexel(", font.index("/* The font sheet's texel")):font.index("bool fontGlyph(")]
    poster = art[art.index("#define SR_MAX_SRC_W"):art.index("static u32 defaultNowMs(", art.index("#define SR_MAX_SRC_W"))]
    with tempfile.TemporaryDirectory() as path:
        binary = Path(path) / "render"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-x", "c", "-", "-o", str(binary)],
                       input=STUBS + font_filter + texel + poster + DRIVER, text=True, check=True)
        return hashlib.sha256(subprocess.check_output([str(binary)])).hexdigest()


if __name__ == "__main__":
    actual = render((GUI / "IPLFontWrite.c").read_text(), (GUI / "ui_assets.c").read_text())
    if actual != REFERENCE:
        raise SystemExit(f"font/poster pixels changed: {actual}")
    print("Four font cell geometries and three poster mip levels match the release, byte for byte.")
