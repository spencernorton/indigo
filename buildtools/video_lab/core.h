#ifndef VIDEO_LAB_CORE_H
#define VIDEO_LAB_CORE_H

#include <stddef.h>
#include <stdint.h>

#define VL_VERSION 1u
#define VL_MAGIC 0x564C4142u
#define VL_PATTERN_COUNT 6u
#define VL_CASE_COUNT 2u
#define VL_TRIAL_MS 15000u

typedef struct {
	uint16_t width, height, stride, vi_width, vi_origin;
} vl_mode;

extern const vl_mode vl_modes[VL_CASE_COUNT];

enum vl_command {
	VL_NONE, VL_STATUS, VL_XFB, VL_BASELINE, VL_TRIAL,
	VL_PATTERN, VL_EXIT, VL_EXPIRE, VL_GX_STATUS, VL_GX_START,
	VL_GX_SHOW, VL_GX_XFB, VL_GX_SCENE
};

typedef struct {
	int command;
	unsigned value;
	int status;
} vl_request;

int vl_validate(const vl_mode *mode, size_t capacity);
size_t vl_frame_bytes(const vl_mode *mode);
uint8_t vl_luma(unsigned x, unsigned y, const vl_mode *mode,
	unsigned pattern, uint32_t sequence);
int vl_render(uint8_t *buffer, size_t capacity, const vl_mode *mode,
	unsigned pattern, uint32_t sequence);
uint32_t vl_crc32(const uint8_t *data, size_t length);
void vl_report(uint8_t *buffer, const vl_mode *mode, const uint32_t words[8]);
vl_request vl_parse_request(const char *data, size_t length);

#endif
