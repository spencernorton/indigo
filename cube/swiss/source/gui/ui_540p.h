#ifndef INDIGO_UI_540P_H
#define INDIGO_UI_540P_H

#include <stdbool.h>
#include <stdint.h>

#ifndef UI_EXPERIMENTAL_540P
#define UI_EXPERIMENTAL_540P 0
#endif

#define UI540_TVMODE ((6u << 2) | 2u)
#define UI540_WIDTH 720u
#define UI540_HEIGHT 540u
#define UI540_EFB_WIDTH 400u
#define UI540_EFB_HEIGHT 274u

typedef struct {
	uint32_t state, width, height, frames, work_mean_us, work_max_us;
	uint32_t over_budget, frame_period_mean_us, list_bytes_max;
} ui540Stats_t;

#if UI_EXPERIMENTAL_540P
void UI540_Request(int argc, char **argv);
void UI540_BeginFrame(void);
bool UI540_Active(void);
bool UI540_Recording(void);
void UI540_RecordBegin(void);
bool UI540_Present(void *xfb);
void UI540_EndFrame(uint64_t started);
void UI540_DrawStatus(void);
void UI540_Stats(void *stats);
void UI540_Leave(void);
#else
static inline bool UI540_Active(void) { return false; }
static inline bool UI540_Recording(void) { return false; }
static inline void UI540_Leave(void) {}
#endif

#endif
