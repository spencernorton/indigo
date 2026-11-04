#include "scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	unsigned f, v, t, x, y, updates[VS_VARIANTS] = {0};
	size_t bytes = VS_WIDTH * VS_HEIGHT;
	uint8_t *image[VS_VARIANTS];
	for(v = 0; v < VS_VARIANTS; v++) {
		image[v] = calloc(bytes, 1);
		assert(image[v]);
	}
	for(f = 0; f < VS_FRAMES; f++) {
		for(v = 0; v < VS_VARIANTS; v++) {
			for(t = 0; t < VS_TILES; t++) {
				vs_box b = vs_tile(t);
				assert(b.x + b.w <= VS_WIDTH && b.y + b.h <= VS_HEIGHT);
				assert(b.x % 16 == 0 && b.w % 16 == 0);
				if(!vs_update(v, f, b)) continue;
				updates[v]++;
				for(y = b.y; y < b.y + b.h; y++)
					for(x = b.x; x < b.x + b.w; x++)
						image[v][y * VS_WIDTH + x] = v ? image[0][y * VS_WIDTH + x] : vs_pixel(f, x, y);
			}
			assert(!memcmp(image[v], image[0], bytes));
		}
	}
	assert(updates[0] == VS_FRAMES * VS_TILES);
	assert(updates[1] < updates[0] && updates[2] < updates[1]);
	assert(updates[3] == updates[2]);
	/* A frame with the panel removed still redraws tiles occupied by it.
	 * Omitting old dynamic bounds would also leave a moving object's trail. */
	assert(vs_update(1, 16, (vs_box){384, 120, 160, 120}));
	assert(vs_update(2, 1, (vs_box){0, 0, 160, 120}));
	for(v = 0; v < VS_VARIANTS; v++) {
		printf("R%u: %u tile updates; identical full-image reference across %u frames\n", v, updates[v], VS_FRAMES);
		free(image[v]);
	}
	return 0;
}
