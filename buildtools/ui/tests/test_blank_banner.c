#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "blankbanner.h"

int main(void)
{
	uint32_t hash = UINT32_C(2166136261);
	assert(sizeof(blankbanner) == 96u * 32u * 2u);
	/* Pin every RGB5A3 pixel from the original BNR1 asset, excluding its
	 * unused header and text. A layout/offset mistake changes this hash. */
	for(size_t i = 0; i < sizeof(blankbanner); ++i) {
		hash = (hash ^ blankbanner[i]) * UINT32_C(16777619);
	}
	assert(hash == UINT32_C(0xdf9fee64));
	puts("fallback banner pixels: ok");
	return 0;
}
