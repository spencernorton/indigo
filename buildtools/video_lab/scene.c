#include "scene.h"

vs_box vs_tile(unsigned index)
{
	vs_box box = {index % 5u * 160u, index / 5u * 120u, 160, 120};
	if(box.x + box.w > VS_WIDTH) box.w = VS_WIDTH - box.x;
	return box;
}

unsigned vs_epoch(unsigned frame) { return frame / 8u; }

int vs_intersects(vs_box a, vs_box b)
{
	return a.x < b.x + b.w && b.x < a.x + a.w &&
		a.y < b.y + b.h && b.y < a.y + a.h;
}

unsigned vs_rects(unsigned frame, int dynamic, vs_rect out[VS_RECTS])
{
	unsigned n = 0, epoch = vs_epoch(frame), i;
	if(!dynamic) {
		unsigned x = epoch == 3 ? 48 : epoch == 1 ? 384 : 208;
		if(epoch == 2) return 0;
		out[n++] = (vs_rect){{x + 8, 136, 256, 192}, 112, 100};
		out[n++] = (vs_rect){{x, 128, 256, 192}, 48, 255};
		for(i = 0; i < 24; i++)
			out[n++] = (vs_rect){{x + 16, 140 + i * 7, 96 + i % 5 * 24, 3}, 168, 255};
	} else {
		/* Cross the 640-column join, touch both edges, overlap the panel,
		 * then disappear. Bounds include the translucent shadow. */
		unsigned x = frame * 47u % 656u, y = frame * 29u % 416u;
		if(frame % 16u < 12u) {
			out[n++] = (vs_rect){{x + 8, y + 8, 56, 48}, 168, 96};
			out[n++] = (vs_rect){{x, y, 56, 48}, 235, 255};
			out[n++] = (vs_rect){{x + 8, y + 8, 40, 8}, 80, 255};
		}
		if(frame % 16u >= 12u)
			out[n++] = (vs_rect){{448, 56, 160, 80}, 112, 192};
	}
	return n;
}

static int layer_touches(unsigned frame, int dynamic, vs_box tile)
{
	vs_rect rects[VS_RECTS];
	unsigned i, count = vs_rects(frame, dynamic, rects);
	for(i = 0; i < count; i++)
		if(vs_intersects(rects[i].box, tile)) return 1;
	return 0;
}

int vs_black(unsigned frame, vs_box tile)
{
	return !layer_touches(frame, 0, tile) && !layer_touches(frame, 1, tile);
}

int vs_update(unsigned variant, unsigned frame, vs_box tile)
{
	if(variant == 0 || frame == 0) return 1;
	if(variant == 1)
		return !vs_black(frame, tile) || !vs_black(frame - 1u, tile);
	/* Changes to the static scene invalidate the entire retained image.
	 * Dynamic damage includes old and new bounds, including the shadow. */
	return vs_epoch(frame) != vs_epoch(frame - 1u) ||
		layer_touches(frame, 1, tile) || layer_touches(frame - 1u, 1, tile);
}

uint8_t vs_pixel(unsigned frame, unsigned x, unsigned y)
{
	vs_rect rects[VS_RECTS];
	unsigned layer, i, count, color = 0;
	for(layer = 0; layer < 2; layer++) {
		count = vs_rects(frame, (int)layer, rects);
		for(i = 0; i < count; i++) {
			vs_rect r = rects[i];
			if(x >= r.box.x && x < r.box.x + r.box.w &&
				y >= r.box.y && y < r.box.y + r.box.h)
				color = (r.gray * r.alpha + color * (255u - r.alpha)) / 255u;
		}
	}
	return (uint8_t)color;
}
