#ifndef UI_BNR_H
#define UI_BNR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ui_bnr -- GameCube opening.bnr (BNR1 / BNR2) metadata and 96x32 RGB5A3
 * banner decoding, for Apps (/apps/<slug>/default.dol + opening.bnr).
 *
 * Pure: no GX, no I/O, no dynamic allocation. Every file from a card is
 * untrusted: magic, length and every field bound are checked before reading.
 */

#define UI_BNR_WIDTH 96u
#define UI_BNR_HEIGHT 32u
#define UI_BNR_HEADER_BYTES 32u
#define UI_BNR_PIXEL_BYTES (UI_BNR_WIDTH * UI_BNR_HEIGHT * 2u)
#define UI_BNR_RGBA_BYTES (UI_BNR_WIDTH * UI_BNR_HEIGHT * 4u)
#define UI_BNR_DESC_BYTES 320u
#define UI_BNR_LANG_COUNT 6u
#define UI_BNR1_SIZE (UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES + UI_BNR_DESC_BYTES)
#define UI_BNR2_SIZE (UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES + \
	UI_BNR_LANG_COUNT * UI_BNR_DESC_BYTES)
/* Real BNR1/BNR2 files are 6496 or 8096 bytes; allow modest trailing padding
 * (e.g. sector-aligned banners) while rejecting oversized files up front. */
#define UI_BNR_MAX_FILE 16384u

#define UI_BNR_TITLE_MAX 65u
#define UI_BNR_AUTHOR_MAX 65u
#define UI_BNR_DESC_MAX 129u

typedef struct {
	char title[UI_BNR_TITLE_MAX];
	char author[UI_BNR_AUTHOR_MAX];
	char description[UI_BNR_DESC_MAX];
} uiBnrInfo_t;

/* True when data/size starts with a valid BNR1 or BNR2 header and holds at
 * least all required pixels and language descriptor blocks. */
bool UIBnr_IsBanner(const uint8_t *data, size_t size);

/* Parses title, author and description from a BNR1 or BNR2 buffer. For BNR2,
 * sramLanguage (0=English .. 5=Dutch) selects the descriptor block, falling
 * back to English (0) when out of range or when that block's title is empty.
 * Returns false if data is not a valid BNR1/BNR2 banner. */
bool UIBnr_ParseMeta(const uint8_t *data, size_t size, uint8_t sramLanguage,
	uiBnrInfo_t *out);

/* Decodes the 96x32 GX_TF_RGB5A3 4x4-tiled pixel data of a BNR1/BNR2 buffer
 * into straight RGBA8888 (row-major, top to bottom, UI_BNR_RGBA_BYTES). */
bool UIBnr_DecodeRgba(const uint8_t *data, size_t size,
	uint8_t out[UI_BNR_RGBA_BYTES]);

#endif
