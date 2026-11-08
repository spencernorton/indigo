#ifndef VIDEO_LAB_GX_H
#define VIDEO_LAB_GX_H

#include <stddef.h>
#include <stdint.h>
#include "scene.h"

typedef struct {
	uint32_t frames, copies, copy_pixels, vertices, texels;
	uint32_t gpu_pixels_in, gpu_copy_clocks;
	uint32_t work_us, cache_us, compare_us, cache_builds;
	uint32_t crc32, mismatches;
	uint32_t submitted_vertices;
} vg_metric;

typedef struct {
	uint32_t error, matched_frames, coordinate_crc, canaries_ok, coordinate_fenced;
	uint32_t oracle_max_error;
	uint32_t gp_vertices_probe, gp_texels_probe, gp_metrics_calibrated;
	vg_metric variant[VS_VARIANTS];
} vg_result;

/* One GPU job per boot. It never owns VIDEO or the image mutex. */
int vg_init(void);
int vg_start(void);
unsigned vg_state(void); /* 0 idle, 1 running, 2 passed, 3 failed */
unsigned vg_progress(void);
const vg_result *vg_results(void); /* only valid after state 2/3 */
const uint8_t *vg_coordinates(void); /* only valid after state 2 */
const uint8_t *vg_scene(unsigned variant); /* final frame, state 2 */

#endif
