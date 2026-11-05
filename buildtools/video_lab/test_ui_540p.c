#include "../../cube/swiss/source/gui/ui_540p.h"
#include "../../cube/swiss/source/gui/ui_540p_layout.h"
#include <assert.h>
#include <stdio.h>

static unsigned char coverage[UI540_HEIGHT][UI540_WIDTH];

int main(void)
{
	for(unsigned i = 0; i < 4; i++) {
		ui540Tile_t t = UI540_Tile(i);
		assert(t.x + t.width <= UI540_WIDTH && t.y + t.height <= UI540_HEIGHT);
		assert(((t.y * UI540_WIDTH + t.x) * 2u) % 32u == 0);
		assert((UI540_WIDTH * 2u) % 32u == 0);
		assert(16 + t.width + 16 <= UI540_EFB_WIDTH);
		assert(2 + t.height + 2 <= UI540_EFB_HEIGHT);
		assert(UI540_EFB_WIDTH <= 640 && UI540_EFB_HEIGHT <= 528);
		assert((int)t.x - 16 >= -342 && (int)t.x - 16 <= 382);
		assert((int)t.y - 2 >= -342 && (int)t.y - 2 <= 494);
		/* Scissor offsets are in pairs of pixels. No tile rounds to a
		 * different sample phase; the XFB has exactly one writer per pixel. */
		assert((t.x - 16u) % 2 == 0 && (t.y - 2u) % 2 == 0);
		for(unsigned y = t.y; y < t.y + t.height; y++)
			for(unsigned x = t.x; x < t.x + t.width; x++) {
				assert(coverage[y][x]++ == 0);
				assert(x - t.x + 16 < UI540_EFB_WIDTH);
				assert(y - t.y + 2 < UI540_EFB_HEIGHT);
			}
	}
	for(unsigned y = 0; y < UI540_HEIGHT; y++)
		for(unsigned x = 0; x < UI540_WIDTH; x++) assert(coverage[y][x] == 1);
	puts("720x540 tile coverage, EFB bounds, sample phase and DMA alignment pass");
}
