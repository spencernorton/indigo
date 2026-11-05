#include "ui_540p.h"

#if UI_EXPERIMENTAL_540P
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "swiss.h"
#include "video.h"
#include "input.h"
#include "util.h"
#include "IPLFontWrite.h"
#include "ui_540p_layout.h"

#define LIST_CAPACITY (512u * 1024u)
static uint8_t list[LIST_CAPACITY + 64] ATTRIBUTE_ALIGN(32);
static char watchdog_stack[16384] ATTRIBUTE_ALIGN(8);
static lwp_t watchdog_thread;
static _Atomic uint32_t state, started_ms, fallback_ms;
static bool active, recording;
static GXRModeObj high_mode, normal_mode;
static ui540Stats_t stats;
static uint64_t previous_frame, work_total, period_total;
enum { OFF, REQUESTED, TRIAL, KEPT, FALLBACK, RETURNED, FAILED };

static uint32_t now_ms(void) { return (uint32_t)ticks_to_millisecs(gettime()); }

static void request_fallback(void)
{
	uint32_t current = state;
	if(current == TRIAL || current == KEPT) {
		fallback_ms = now_ms();
		state = FALLBACK;
	}
}

static void *watchdog(void *unused)
{
	(void)unused;
	for(;;) {
		if(state == TRIAL && now_ms() - started_ms >= 15000u) request_fallback();
		/* If the render thread cannot process a normal-mode request, return
		 * to the boot program. Hard GPU/kernel failures still require reset. */
		if(state == FALLBACK && now_ms() - fallback_ms >= 2000u)
			SYS_ResetSystem(SYS_HOTRESET, 0, false);
		usleep(10000);
	}
	return NULL;
}

void UI540_Request(int argc, char **argv)
{
	for(int i = 1; i < argc; i++)
		if(!strcmp(argv[i], "Experimental540p=No")) return;
	if(!getRawDTVStatus()) {
		print_debug("Experimental 540p: digital progressive output required\n");
		return;
	}
	DCFlushRange(list, sizeof(list));
	if(LWP_CreateThread(&watchdog_thread, watchdog, NULL, watchdog_stack,
		sizeof(watchdog_stack), 70) != 0) return;
	state = REQUESTED;
}

bool UI540_Active(void)
{
	const GXRModeObj *m = getVideoMode();
	return active && m->viTVMode == UI540_TVMODE && m->fbWidth == UI540_WIDTH &&
		m->xfbHeight == UI540_HEIGHT;
}

bool UI540_Recording(void) { return recording; }

void UI540_BeginFrame(void)
{
	if(state == REQUESTED) {
		normal_mode = *getVideoMode();
		high_mode = TVNtsc480Prog;
		high_mode.viTVMode = UI540_TVMODE;
		high_mode.fbWidth = high_mode.viWidth = UI540_WIDTH;
		high_mode.efbHeight = high_mode.xfbHeight = high_mode.viHeight = UI540_HEIGHT;
		high_mode.viXOrigin = high_mode.viYOrigin = 0;
		started_ms = now_ms();
		state = TRIAL;
		setVideoMode(&high_mode);
		active = getVideoMode() == &high_mode;
		if(!active) { state = FAILED; return; }
		memset(&stats, 0, sizeof(stats));
		previous_frame = work_total = period_total = 0;
		print_debug("Experimental 540p:720x540,800x563 total,%.6fHz;15s trial\n", VIDEO_GetRetraceRate());
	}
	if(active && !UI540_Active()) { active = false; state = RETURNED; }
	if(state == FALLBACK) {
		setVideoMode(&normal_mode);
		/* Leave the request live for the watchdog if memory pressure
		 * prevents allocating normal buffers. Never run the 480p copy
		 * path against a still-active 540-line framebuffer. */
		if(getVideoMode() != &normal_mode) return;
		active = false;
		state = RETURNED;
		print_debug("Experimental 540p: returned to normal menu mode\n");
	}
	if(UI540_Active()) {
		u32 held = padsButtonsHeld();
		if((held & BUTTON_L) && (held & BUTTON_R)) {
			if(held & BUTTON_B) request_fallback();
			else if((held & BUTTON_A) && state == TRIAL) {
				state = KEPT;
				print_debug("Experimental 540p: kept for this RAM session\n");
			}
		}
	}
}

void UI540_RecordBegin(void)
{
	DCInvalidateRange(list, LIST_CAPACITY);
	GX_SetMisc(GX_MT_DL_SAVE_CTX, GX_FALSE);
	recording = true;
	GX_BeginDispList(list, LIST_CAPACITY);
}

static void clear_efb(void)
{
	Mtx44 projection;
	Mtx identity;
	GX_SetScissorBoxOffset(0, 0);
	GX_SetViewport(0, 0, UI540_EFB_WIDTH, UI540_EFB_HEIGHT, 1, 1);
	GX_SetScissor(0, 0, UI540_EFB_WIDTH, UI540_EFB_HEIGHT);
	guOrtho(projection, 0, UI540_EFB_HEIGHT, 0, UI540_EFB_WIDTH, 0, 1);
	guMtxIdentity(identity);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(0, 0, 0); GX_Color4u8(0, 0, 0, 255);
	GX_Position3f32(UI540_EFB_WIDTH, 0, 0); GX_Color4u8(0, 0, 0, 255);
	GX_Position3f32(UI540_EFB_WIDTH, UI540_EFB_HEIGHT, 0); GX_Color4u8(0, 0, 0, 255);
	GX_Position3f32(0, UI540_EFB_HEIGHT, 0); GX_Color4u8(0, 0, 0, 255);
	GX_End();
}

bool UI540_Present(void *framebuffer)
{
	u32 bytes = GX_EndDispList();
	recording = false;
	if(!bytes || bytes > LIST_CAPACITY || bytes % 32) {
		request_fallback();
		print_debug("Experimental 540p: frame-list capacity exceeded\n");
		return false;
	}
	if(bytes > stats.list_bytes_max) stats.list_bytes_max = bytes;
	for(unsigned i = 0; i < 4; i++) {
		ui540Tile_t t = UI540_Tile(i);
		unsigned left = t.x > 16 ? t.x - 16 : 0, top = t.y > 2 ? t.y - 2 : 0;
		unsigned right = t.x + t.width + 16, bottom = t.y + t.height + 2;
		if(right > UI540_WIDTH) right = UI540_WIDTH;
		if(bottom > UI540_HEIGHT) bottom = UI540_HEIGHT;
		clear_efb();
		GX_SetViewport(0, 0, UI540_WIDTH, UI540_HEIGHT, 0, 1);
		GX_SetScissorBoxOffset((int)t.x - 16, (int)t.y - 2);
		GX_SetScissor(left, top, right - left, bottom - top);
		GX_CallDispList(list, bytes);
		GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
		GX_SetColorUpdate(GX_TRUE);
		GX_SetDispCopyYScale(1);
		GX_SetDispCopySrc(16, 2, t.width, t.height);
		GX_SetDispCopyDst(UI540_WIDTH, UI540_HEIGHT);
		GX_CopyDisp((uint8_t *)framebuffer + (t.y * UI540_WIDTH + t.x) * 2, GX_TRUE);
	}
	GX_SetScissorBoxOffset(0, 0);
	return true;
}

void UI540_EndFrame(uint64_t started)
{
	uint64_t now = gettime();
	u32 work = (u32)ticks_to_microsecs(diff_ticks(started, now));
	if(previous_frame) period_total += ticks_to_microsecs(diff_ticks(previous_frame, now));
	previous_frame = now;
	stats.frames++;
	work_total += work;
	stats.work_mean_us = work_total / stats.frames;
	if(work > stats.work_max_us) stats.work_max_us = work;
	if(work > 16682u) stats.over_budget++;
	if(stats.frames > 1) stats.frame_period_mean_us = period_total / (stats.frames - 1);
}

void UI540_DrawStatus(void)
{
	const char *text = state == TRIAL ? "540p trial: L+R+A keep / L+R+B return" : "540p experimental / L+R+B return";
	drawStringMedium(320, 464, text, 0.48f, ALIGN_CENTER, (GXColor){230, 228, 244, 255});
}

void UI540_Stats(void *out)
{
	ui540Stats_t copy = stats;
	copy.state = state;
	copy.width = getVideoMode()->fbWidth;
	copy.height = getVideoMode()->xfbHeight;
	*(ui540Stats_t *)out = copy;
}

void UI540_Leave(void)
{
	uint32_t start = now_ms();
	request_fallback();
	while(state == FALLBACK && now_ms() - start < 1500u) usleep(1000);
}
#endif
