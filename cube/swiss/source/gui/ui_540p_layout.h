#ifndef INDIGO_UI_540P_LAYOUT_H
#define INDIGO_UI_540P_LAYOUT_H

/* Copy starts and destination columns preserve 32-byte alignment. A halo
 * is rendered around each copied rectangle for edge/filter continuity. */
typedef struct { unsigned x, y, width, height; } ui540Tile_t;
static inline ui540Tile_t UI540_Tile(unsigned index)
{
	ui540Tile_t tile = {index % 2u ? 368u : 0u, index / 2u * 270u,
		index % 2u ? 352u : 368u, 270u};
	return tile;
}

#endif
