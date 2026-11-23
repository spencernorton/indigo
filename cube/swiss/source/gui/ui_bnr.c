/* ui_bnr.c - untrusted opening.bnr (BNR1/BNR2) metadata and RGB5A3 decoder.
 * See ui_bnr.h. */

#include <string.h>

#include "ui_bnr.h"

#define BNR_SHORT_LEN 0x20u
#define BNR_FULL_LEN 0x40u
#define BNR_DESC_LEN 0x80u

static int bannerKind(const uint8_t *data, size_t size)
{
	if(data == NULL || size < UI_BNR1_SIZE || size > UI_BNR_MAX_FILE) {
		return 0;
	}
	if(data[0] == 'B' && data[1] == 'N' && data[2] == 'R') {
		if(data[3] == '1') {
			return 1;
		}
		if(data[3] == '2' && size >= UI_BNR2_SIZE) {
			return 2;
		}
	}
	return 0;
}

bool UIBnr_IsBanner(const uint8_t *data, size_t size)
{
	return bannerKind(data, size) != 0;
}

/* Copies a single-line BNR text field into out, stopping at NUL, turning
 * control characters into spaces, collapsing runs of spaces, and trimming
 * leading/trailing whitespace. */
static void cleanSingleLine(const uint8_t *src, size_t srcMax,
	char *out, size_t outSize)
{
	size_t i;
	size_t written = 0u;
	bool pendingSpace = false;

	if(out == NULL || outSize == 0u) {
		return;
	}
	out[0] = '\0';
	if(src == NULL) {
		return;
	}
	for(i = 0u; i < srcMax && src[i] != 0u; ++i) {
		uint8_t c = src[i];

		if(c <= 0x20u || c == 0x7Fu) {
			if(written > 0u) {
				pendingSpace = true;
			}
			continue;
		}
		if(pendingSpace) {
			if(written + 1u >= outSize) {
				break;
			}
			out[written++] = ' ';
			pendingSpace = false;
		}
		if(written + 1u >= outSize) {
			break;
		}
		out[written++] = (char)c;
	}
	out[written] = '\0';
}

/* Copies a multi-line BNR description into out, preserving '\n' line breaks
 * while normalizing '\r', stripping other control bytes, and trimming trailing
 * whitespace. */
static void cleanDescription(const uint8_t *src, size_t srcMax,
	char *out, size_t outSize)
{
	size_t i;
	size_t written = 0u;

	if(out == NULL || outSize == 0u) {
		return;
	}
	out[0] = '\0';
	if(src == NULL) {
		return;
	}
	for(i = 0u; i < srcMax && src[i] != 0u && written + 1u < outSize; ++i) {
		uint8_t c = src[i];

		if(c == '\r') {
			continue;
		}
		if(c == '\n') {
			if(written > 0u) {
				out[written++] = '\n';
			}
			continue;
		}
		if(c < 0x20u || c == 0x7Fu) {
			c = ' ';
		}
		if(written == 0u && c == ' ') {
			continue;
		}
		out[written++] = (char)c;
	}
	while(written > 0u && (out[written - 1u] == ' ' ||
		out[written - 1u] == '\n')) {
		--written;
	}
	out[written] = '\0';
}

static void parseDescBlock(const uint8_t *desc, uiBnrInfo_t *out)
{
	const uint8_t *gameShort = desc;
	const uint8_t *companyShort = desc + BNR_SHORT_LEN;
	const uint8_t *gameFull = companyShort + BNR_SHORT_LEN;
	const uint8_t *companyFull = gameFull + BNR_FULL_LEN;
	const uint8_t *description = companyFull + BNR_FULL_LEN;

	cleanSingleLine(gameFull, BNR_FULL_LEN, out->title, sizeof(out->title));
	if(out->title[0] == '\0') {
		cleanSingleLine(gameShort, BNR_SHORT_LEN, out->title,
			sizeof(out->title));
	}
	cleanSingleLine(companyFull, BNR_FULL_LEN, out->author,
		sizeof(out->author));
	if(out->author[0] == '\0') {
		cleanSingleLine(companyShort, BNR_SHORT_LEN, out->author,
			sizeof(out->author));
	}
	cleanDescription(description, BNR_DESC_LEN, out->description,
		sizeof(out->description));
}

bool UIBnr_ParseMeta(const uint8_t *data, size_t size, uint8_t sramLanguage,
	uiBnrInfo_t *out)
{
	int kind;
	const uint8_t *descs;
	uint8_t lang = 0u;

	if(out != NULL) {
		memset(out, 0, sizeof(*out));
	}
	kind = bannerKind(data, size);
	if(kind == 0 || out == NULL) {
		return false;
	}
	descs = data + UI_BNR_HEADER_BYTES + UI_BNR_PIXEL_BYTES;
	if(kind == 2 && sramLanguage < UI_BNR_LANG_COUNT) {
		lang = sramLanguage;
	}
	parseDescBlock(descs + (size_t)lang * UI_BNR_DESC_BYTES, out);
	if(lang != 0u && out->title[0] == '\0') {
		uiBnrInfo_t english;

		memset(&english, 0, sizeof(english));
		parseDescBlock(descs, &english);
		if(english.title[0] != '\0') {
			memcpy(out->title, english.title, sizeof(out->title));
		}
		if(out->author[0] == '\0' && english.author[0] != '\0') {
			memcpy(out->author, english.author, sizeof(out->author));
		}
		if(out->description[0] == '\0' && english.description[0] != '\0') {
			memcpy(out->description, english.description,
				sizeof(out->description));
		}
	}
	return true;
}

bool UIBnr_DecodeRgba(const uint8_t *data, size_t size,
	uint8_t out[UI_BNR_RGBA_BYTES])
{
	const uint8_t *src;
	uint32_t tileY, tileX, y, x;

	if(out == NULL || bannerKind(data, size) == 0) {
		return false;
	}
	src = data + UI_BNR_HEADER_BYTES;
	for(tileY = 0u; tileY < UI_BNR_HEIGHT; tileY += 4u) {
		for(tileX = 0u; tileX < UI_BNR_WIDTH; tileX += 4u) {
			for(y = 0u; y < 4u; ++y) {
				for(x = 0u; x < 4u; ++x) {
					uint16_t pixel = (uint16_t)((uint16_t)src[0] << 8 | src[1]);
					uint8_t *dst = out + ((tileY + y) * UI_BNR_WIDTH +
						(tileX + x)) * 4u;

					src += 2;
					if((pixel & 0x8000u) != 0u) {
						uint32_t r5 = (pixel >> 10) & 31u;
						uint32_t g5 = (pixel >> 5) & 31u;
						uint32_t b5 = pixel & 31u;

						dst[0] = (uint8_t)(r5 << 3 | r5 >> 2);
						dst[1] = (uint8_t)(g5 << 3 | g5 >> 2);
						dst[2] = (uint8_t)(b5 << 3 | b5 >> 2);
						dst[3] = 255u;
					}
					else {
						uint32_t a3 = (pixel >> 12) & 7u;
						uint32_t r4 = (pixel >> 8) & 15u;
						uint32_t g4 = (pixel >> 4) & 15u;
						uint32_t b4 = pixel & 15u;

						dst[0] = (uint8_t)(r4 << 4 | r4);
						dst[1] = (uint8_t)(g4 << 4 | g4);
						dst[2] = (uint8_t)(b4 << 4 | b4);
						dst[3] = (uint8_t)(a3 << 5 | a3 << 2 | a3 >> 1);
					}
				}
			}
		}
	}
	return true;
}
