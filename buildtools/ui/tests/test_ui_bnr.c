/* Untrusted opening.bnr (BNR1/BNR2) metadata parsing, RGB5A3 tile decoding
 * and poster composition through UIPng_RgbaPoster. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_bnr.h"
#include "ui_png.h"

static unsigned checks;

#define CHECK(condition) do { \
	++checks; \
	if(!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#condition); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

static void writeDesc(uint8_t *block, const char *shortName,
	const char *shortCo, const char *fullName, const char *fullCo,
	const char *desc)
{
	memset(block, 0, UI_BNR_DESC_BYTES);
	if(shortName) memcpy(block, shortName, strlen(shortName));
	if(shortCo) memcpy(block + 0x20u, shortCo, strlen(shortCo));
	if(fullName) memcpy(block + 0x40u, fullName, strlen(fullName));
	if(fullCo) memcpy(block + 0x80u, fullCo, strlen(fullCo));
	if(desc) memcpy(block + 0xC0u, desc, strlen(desc));
}

static void putBe16(uint8_t *dst, uint16_t v)
{
	dst[0] = (uint8_t)(v >> 8);
	dst[1] = (uint8_t)(v & 0xFFu);
}

static void testBnr1Meta(void)
{
	uint8_t bnr[UI_BNR1_SIZE];
	uiBnrInfo_t info;

	memset(bnr, 0, sizeof(bnr));
	memcpy(bnr, "BNR1", 4);
	writeDesc(bnr + UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES,
		"Short", "Co",
		"  Indigo\tLauncher \r\n", "  Spencer  Norton  ",
		"  Line one\r\nLine two  \n ");

	CHECK(UIBnr_IsBanner(bnr, sizeof(bnr)));
	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 0u, &info));
	CHECK(strcmp(info.title, "Indigo Launcher") == 0);
	CHECK(strcmp(info.author, "Spencer Norton") == 0);
	CHECK(strcmp(info.description, "Line one\nLine two") == 0);

	/* Empty full fields fall back to short fields. */
	writeDesc(bnr + UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES,
		"CleanRip", "emu_kidid", "", "", "Disc backup");
	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 3u, &info));
	CHECK(strcmp(info.title, "CleanRip") == 0);
	CHECK(strcmp(info.author, "emu_kidid") == 0);
	CHECK(strcmp(info.description, "Disc backup") == 0);

	/* Unterminated full 64-byte fields stay strictly bounded. */
	memset(bnr + UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES + 0x40u, 'X', 0x40u);
	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 0u, &info));
	CHECK(strlen(info.title) == 0x40u);
}

static void testBnr2Language(void)
{
	uint8_t bnr[UI_BNR2_SIZE];
	uint8_t *descs = bnr + UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES;
	uiBnrInfo_t info;

	memset(bnr, 0, sizeof(bnr));
	memcpy(bnr, "BNR2", 4);
	writeDesc(descs + 0u * UI_BNR_DESC_BYTES,
		"App EN", "Dev EN", "English Title", "English Dev", "English desc");
	writeDesc(descs + 2u * UI_BNR_DESC_BYTES,
		"App FR", "Dev FR", "Titre Francais", "Dev Francais", "Desc FR");
	/* Language 3 (Spanish) left empty: must fall back to English. */

	CHECK(UIBnr_IsBanner(bnr, sizeof(bnr)));
	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 2u, &info));
	CHECK(strcmp(info.title, "Titre Francais") == 0);
	CHECK(strcmp(info.author, "Dev Francais") == 0);

	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 3u, &info));
	CHECK(strcmp(info.title, "English Title") == 0);
	CHECK(strcmp(info.author, "English Dev") == 0);

	/* Out-of-range language index falls back to English. */
	CHECK(UIBnr_ParseMeta(bnr, sizeof(bnr), 99u, &info));
	CHECK(strcmp(info.title, "English Title") == 0);
}

static void testDecodeAndPoster(void)
{
	uint8_t bnr[UI_BNR1_SIZE];
	uint8_t rgba[UI_BNR_RGBA_BYTES];
	uint8_t poster[UI_PNG_POSTER_BYTES];
	uint8_t poster2[UI_PNG_POSTER_BYTES];
	uint8_t *pixels = bnr + UI_BNR_HEADER_BYTES;
	uint32_t i;
	bool nonZero = false;

	memset(bnr, 0, sizeof(bnr));
	memcpy(bnr, "BNR1", 4);
	/* Fill entire banner with opaque indigo RGB555 (R=8, G=6, B=24). */
	for(i = 0u; i < UI_BNR_WIDTH * UI_BNR_HEIGHT; ++i) {
		putBe16(pixels + i * 2u, (uint16_t)(0x8000u | (8u << 10) | (6u << 5) | 24u));
	}
	/* Tile (0,0), pixel (0,0): opaque pure red RGB555 (31,0,0). */
	putBe16(pixels + 0u, (uint16_t)(0x8000u | (31u << 10)));
	/* Tile (0,0), pixel (3,2): translucent green ARGB3444 (a=4, r=0, g=15, b=0). */
	putBe16(pixels + (2u * 4u + 3u) * 2u, (uint16_t)((4u << 12) | (15u << 4)));
	/* Tile (1,0) -> x=4..7, y=0..3, pixel (1,1) -> (x=5, y=1): opaque pure blue. */
	putBe16(pixels + (16u + 1u * 4u + 1u) * 2u, (uint16_t)(0x8000u | 31u));

	CHECK(UIBnr_DecodeRgba(bnr, sizeof(bnr), rgba));
	/* (0, 0) -> pure red opaque */
	CHECK(rgba[0] == 255u && rgba[1] == 0u && rgba[2] == 0u && rgba[3] == 255u);
	/* (3, 2) -> green with a3=4 -> (4<<5)|(4<<2)|(4>>1) = 128+16+2 = 146 */
	i = (2u * UI_BNR_WIDTH + 3u) * 4u;
	CHECK(rgba[i + 0u] == 0u && rgba[i + 1u] == 255u &&
		rgba[i + 2u] == 0u && rgba[i + 3u] == 146u);
	/* (5, 1) -> pure blue opaque */
	i = (1u * UI_BNR_WIDTH + 5u) * 4u;
	CHECK(rgba[i + 0u] == 0u && rgba[i + 1u] == 0u &&
		rgba[i + 2u] == 255u && rgba[i + 3u] == 255u);

	/* Compose decoded banner into a Library CMPR poster deterministically. */
	CHECK(UIPng_RgbaPoster(rgba, UI_BNR_WIDTH, UI_BNR_HEIGHT, poster));
	CHECK(UIPng_RgbaPoster(rgba, UI_BNR_WIDTH, UI_BNR_HEIGHT, poster2));
	CHECK(memcmp(poster, poster2, sizeof(poster)) == 0);
	for(i = 0u; i < sizeof(poster); ++i) {
		if(poster[i] != 0u) {
			nonZero = true;
			break;
		}
	}
	CHECK(nonZero);
}

static void testInvalid(void)
{
	uint8_t buf[UI_BNR2_SIZE];
	uint8_t rgba[UI_BNR_RGBA_BYTES];
	uiBnrInfo_t info;

	memset(buf, 0, sizeof(buf));
	CHECK(!UIBnr_IsBanner(NULL, UI_BNR1_SIZE));
	CHECK(!UIBnr_IsBanner(buf, UI_BNR1_SIZE));
	memcpy(buf, "BNR1", 4);
	CHECK(!UIBnr_IsBanner(buf, UI_BNR1_SIZE - 1u));
	CHECK(!UIBnr_IsBanner(buf, UI_BNR_MAX_FILE + 1u));
	memcpy(buf, "BNR2", 4);
	CHECK(!UIBnr_IsBanner(buf, UI_BNR1_SIZE));
	CHECK(!UIBnr_IsBanner(buf, UI_BNR2_SIZE - 1u));
	CHECK(!UIBnr_ParseMeta(buf, UI_BNR2_SIZE - 1u, 0u, &info));
	CHECK(!UIBnr_ParseMeta(buf, UI_BNR2_SIZE, 0u, NULL));
	CHECK(!UIBnr_DecodeRgba(buf, UI_BNR2_SIZE - 1u, rgba));
	CHECK(!UIBnr_DecodeRgba(buf, UI_BNR2_SIZE, NULL));
	CHECK(!UIPng_RgbaPoster(NULL, UI_BNR_WIDTH, UI_BNR_HEIGHT, rgba));
	CHECK(!UIPng_RgbaPoster(rgba, 0u, UI_BNR_HEIGHT, rgba));
}

int main(void)
{
	testBnr1Meta();
	testBnr2Language();
	testDecodeAndPoster();
	testInvalid();
	printf("test_ui_bnr: %u checks passed\n", checks);
	return 0;
}
