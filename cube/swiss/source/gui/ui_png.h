#ifndef UI_PNG_H
#define UI_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ui_png -- an app's or a folder's picture, from a PNG on the card to a
 * Library poster.
 *
 * Pure: no GX, no I/O, no locks. card_art.c reads the file and hands its
 * bytes here. The result has the poster pack's texture layout (a 256x256
 * GX_TF_CMPR canvas and its four mipmaps, 192x256 of it shown, the right
 * band repeating the last column), so the Library's renderer draws it as
 * it draws a poster.
 *
 * A file from the card is untrusted. Every length and dimension is checked,
 * each chunk's CRC must match, nothing is read past size, the image is
 * decoded a row at a time into a picture a few times the poster's size,
 * and a PNG this can't read is refused whole (false), never shown in part.
 * Interlaced PNGs are refused, and so are a file over UI_PNG_MAX_FILE, at
 * once, and a picture over UI_PNG_MAX_SIDE a side, from its header, before
 * any of its rows are.
 * Whatever the PNG, making its poster never holds more than UI_PNG_MAX_WORK
 * at once (zlib's window included) besides the PNG's bytes and out, and
 * gives all of it back, and it needs little stack (card_art's thread has
 * 32 KB): zlib is never asked for a CRC of more than 8 KB at once.
 * test_ui_png.py counts both.
 */

#define UI_PNG_POSTER_BYTES 43648u	/* the poster pack's record */
#define UI_PNG_MAX_FILE (2u * 1024u * 1024u)
#define UI_PNG_MAX_SIDE 2048u
#define UI_PNG_MAX_WORK (1536u * 1024u)

/* The poster: 192x256 of a 256x256 canvas. */
#define UI_PNG_POSTER_W 192u
#define UI_PNG_POSTER_H 256u
#define UI_PNG_CANVAS 256u

/* The PNG's size in pixels; false for a PNG this won't read. Reads only the
 * signature and the header. */
bool UIPng_Info(const uint8_t *png, size_t size, uint32_t *width,
	uint32_t *height);

/* png as a poster, written to out (UI_PNG_POSTER_BYTES). A picture about
 * the shape of a poster (3:4, within an eighth) fills it, cropped to fit;
 * any other shape sits whole in the middle of it, at most four times its
 * size, over a backdrop in the picture's own colours. False, with out
 * unspecified, when the PNG can't be read or memory runs out. */
bool UIPng_Poster(const uint8_t *png, size_t size, uint8_t *out);

/* A card without a picture gets its name as a poster: the name in big
 * letters, in up to four lines broken at its spaces, '-', '_' and '.' and
 * before a '+', over a backdrop whose colour comes from its first word, so
 * gbihf-ossc and gbihf-direct-hdmi share one and gbisr-ossc has another.
 * The font is the caller's: card_art.c passes the IPL font. */
#define UI_PNG_GLYPH_MAX 32	/* the widest and tallest glyph a font may have */

typedef struct {
	int cellHeight;	/* a line, in font pixels: 1 to UI_PNG_GLYPH_MAX */
	/* Character c: its width in font pixels (1 to UI_PNG_GLYPH_MAX) and its
	 * coverage, 0 to 255, UI_PNG_GLYPH_MAX bytes a row for cellHeight rows.
	 * Neighbours overlap by a column, as drawString sets them. False when
	 * the font has no c. */
	bool (*glyph)(void *context, unsigned char c,
		uint8_t coverage[UI_PNG_GLYPH_MAX * UI_PNG_GLYPH_MAX], int *width);
	void *context;
} uiPngFont_t;

/* name as a poster, written to out (UI_PNG_POSTER_BYTES). False, with out
 * unspecified, for a name with nothing to draw, a font it can't use or no
 * memory. */
bool UIPng_NamePoster(const char *name, const uiPngFont_t *font,
	uint8_t *out);

/* UIPng_Poster and UIPng_NamePoster that another thread can stop: once
 * *stop is set they return false between rows and blocks, out unspecified.
 * A NULL stop never stops them. */
bool UIPng_PosterUntil(const uint8_t *png, size_t size, uint8_t *out,
	const volatile bool *stop);
bool UIPng_NamePosterUntil(const char *name, const uiPngFont_t *font,
	uint8_t *out, const volatile bool *stop);

/* Straight RGBA8888 image (width x height, up to UI_PNG_MAX_SIDE per side)
 * composed and CMPR-encoded as a Library poster (e.g. a decoded opening.bnr
 * 96x32 banner). */
bool UIPng_RgbaPoster(const uint8_t *rgba, uint32_t width, uint32_t height,
	uint8_t *out);
bool UIPng_RgbaPosterUntil(const uint8_t *rgba, uint32_t width, uint32_t height,
	uint8_t *out, const volatile bool *stop);

/* The encoder the poster uses, for tests: rgb is a size x size image (8-bit
 * RGB, rows top to bottom), size a multiple of 8; out takes size*size/2
 * bytes of GX CMPR (8x8 tiles, each four DXT1 blocks, big-endian colors). */
void UIPng_EncodeCmpr(const uint8_t *rgb, uint32_t size, uint8_t *out);

#endif
