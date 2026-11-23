/* Fuzz target for untrusted opening.bnr (BNR1/BNR2) files from a card. */
#include <stddef.h>
#include <stdint.h>

#include "ui_bnr.h"
#include "ui_png.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uiBnrInfo_t info;
	uint8_t rgba[UI_BNR_RGBA_BYTES];
	uint8_t poster[UI_PNG_POSTER_BYTES];
	uint8_t lang = size > 0u ? (uint8_t)(data[size - 1u] % 8u) : 0u;

	if(UIBnr_ParseMeta(data, size, lang, &info)) {
		if(UIBnr_DecodeRgba(data, size, rgba)) {
			(void)UIPng_RgbaPoster(rgba, UI_BNR_WIDTH, UI_BNR_HEIGHT, poster);
		}
	}
	return 0;
}
