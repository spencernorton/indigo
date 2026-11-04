#ifndef VIDEO_LAB_SCENE_H
#define VIDEO_LAB_SCENE_H

#include <stdint.h>

#define VS_WIDTH 720u
#define VS_HEIGHT 480u
#define VS_FRAMES 32u
#define VS_VARIANTS 4u
#define VS_TILES 20u
#define VS_RECTS 48u

typedef struct { unsigned x, y, w, h; } vs_box;
typedef struct { vs_box box; uint8_t gray, alpha; } vs_rect;

vs_box vs_tile(unsigned index);
unsigned vs_epoch(unsigned frame);
unsigned vs_rects(unsigned frame, int dynamic, vs_rect out[VS_RECTS]);
int vs_intersects(vs_box a, vs_box b);
int vs_black(unsigned frame, vs_box tile);
int vs_update(unsigned variant, unsigned frame, vs_box tile);
uint8_t vs_pixel(unsigned frame, unsigned x, unsigned y);

#endif
