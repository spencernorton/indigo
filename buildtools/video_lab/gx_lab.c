#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "gx_lab.h"

#define FRAME_BYTES (VS_WIDTH * VS_HEIGHT * 2u)
static uint8_t images[VS_VARIANTS][FRAME_BYTES + 64] ATTRIBUTE_ALIGN(32);
static uint8_t coordinates[FRAME_BYTES + 64] ATTRIBUTE_ALIGN(32);
static uint8_t texture[VS_WIDTH * VS_HEIGHT] ATTRIBUTE_ALIGN(32);
static uint8_t cached[VS_WIDTH * VS_HEIGHT * 4u] ATTRIBUTE_ALIGN(32);
static uint8_t fifo[256 * 1024] ATTRIBUTE_ALIGN(32);
static char stack[65536] ATTRIBUTE_ALIGN(8);
static lwp_t thread;
static _Atomic uint32_t state, progress, done;
static vg_result result;

static uint32_t elapsed_us(uint64_t start)
{
	return (uint32_t)ticks_to_microsecs(diff_ticks(start, gettime()));
}

static void draw_done(void) { done = 1; }

/* Only the GPU thread waits for this fence. VIDEO, network service and the
 * CPU fallback remain independent, even if this wait or FIFO submission fails. */
static int fence(void)
{
	uint64_t start = gettime();
	done = 0;
	GX_SetDrawDone();
	while(!done) {
		if(elapsed_us(start) > 2000000u) return 0;
		usleep(100);
	}
	return 1;
}

static void viewport(unsigned w, unsigned h)
{
	Mtx44 projection;
	guOrtho(projection, 0, (float)h, 0, (float)w, 0, 1);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetViewport(0, 0, (float)w, (float)h, 0, 1);
	GX_SetScissor(0, 0, w, h);
}

static void quad(float x, float y, float w, float h, GXColor color,
	float u0, float v0, float u1, float v1)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(x, y, 0); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(u0, v0);
	GX_Position3f32(x + w, y, 0); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(u1, v0);
	GX_Position3f32(x + w, y + h, 0); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(u1, v1);
	GX_Position3f32(x, y + h, 0); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(u0, v1);
	GX_End();
}

static void solid(void)
{
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void textured(void *data, unsigned w, unsigned h, unsigned format)
{
	GXTexObj object;
	/* I8 supplies intensity as alpha too. Source replacement must be
	 * opaque, otherwise the coordinate levels would be multiplied twice. */
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
	GX_InitTexObj(&object, data, w, h, format, GX_CLAMP, GX_CLAMP, false);
	GX_InitTexObjFilterMode(&object, GX_NEAR, GX_NEAR);
	GX_LoadTexObj(&object, GX_TEXMAP0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
}

static void layer(unsigned frame, int dynamic, vs_box tile)
{
	vs_rect rects[VS_RECTS];
	unsigned i, count = vs_rects(frame, dynamic, rects);
	solid();
	for(i = 0; i < count; i++) {
		vs_rect r = rects[i];
		if(!vs_intersects(r.box, tile)) continue;
		quad((float)r.box.x - tile.x, (float)r.box.y - tile.y,
			(float)r.box.w, (float)r.box.h,
			(GXColor){r.gray, r.gray, r.gray, r.alpha}, 0, 0, 1, 1);
	}
}

static void clear_tile(vs_box tile)
{
	viewport(tile.w, tile.h);
	solid();
	quad(0, 0, (float)tile.w, (float)tile.h, (GXColor){0, 0, 0, 255}, 0, 0, 1, 1);
}

static size_t texture_offset(unsigned index)
{
	unsigned i;
	size_t offset = 0;
	for(i = 0; i < index; i++) {
		vs_box b = vs_tile(i);
		offset += b.w * b.h * 4u;
	}
	return offset;
}

static int cache_static(unsigned frame)
{
	unsigned t;
	for(t = 0; t < VS_TILES; t++) {
		vs_box b = vs_tile(t);
		clear_tile(b);
		layer(frame, 0, b);
		GX_SetTexCopySrc(0, 0, b.w, b.h);
		GX_SetTexCopyDst(b.w, b.h, GX_TF_RGBA8, GX_FALSE);
		GX_CopyTex(cached + texture_offset(t), GX_FALSE);
		GX_PixModeSync();
	}
	if(!fence()) return 0;
	GX_InvalidateTexAll();
	return 1;
}

static int draw_scene(unsigned variant, unsigned frame)
{
	unsigned t;
	vg_metric *metric = &result.variant[variant];
	uint64_t start;
	u32 topin, topout, botin, botout, clearin, copyclks, vertices, texels;
	if(variant == 3 && (frame == 0 || vs_epoch(frame) != vs_epoch(frame - 1u))) {
		start = gettime();
		if(!cache_static(frame)) return 0;
		metric->cache_us += elapsed_us(start);
		metric->cache_builds++;
	}
	GX_ClearGPMetric();
	GX_ClearPixMetric();
	start = gettime();
	for(t = 0; t < VS_TILES; t++) {
		vs_box b = vs_tile(t);
		if(!vs_update(variant, frame, b)) continue;
		clear_tile(b);
		if(variant == 3) {
			textured(cached + texture_offset(t), b.w, b.h, GX_TF_RGBA8);
			quad(0, 0, (float)b.w, (float)b.h, (GXColor){255, 255, 255, 255}, 0, 0, 1, 1);
		} else layer(frame, 0, b);
		layer(frame, 1, b);
		GX_SetDispCopySrc(0, 0, b.w, b.h);
		GX_SetDispCopyDst(VS_WIDTH, VS_HEIGHT);
		GX_CopyDisp(images[variant] + 32 + (b.y * VS_WIDTH + b.x) * 2u, GX_FALSE);
		metric->copies++;
		metric->copy_pixels += b.w * b.h;
	}
	if(!fence()) return 0;
	metric->work_us += elapsed_us(start);
	GX_ReadGPMetric(&vertices, &texels);
	GX_ReadPixMetric(&topin, &topout, &botin, &botout, &clearin, &copyclks);
	metric->vertices += vertices;
	metric->texels += texels;
	metric->gpu_pixels_in += topin + botin;
	metric->gpu_copy_clocks += copyclks;
	metric->frames++;
	DCInvalidateRange(images[variant] + 32, FRAME_BYTES);
	return 1;
}

static unsigned decode_level(uint8_t value)
{
	const unsigned levels[4] = {16, 80, 160, 235};
	unsigned i;
	for(i = 0; i < 4; i++)
		if((unsigned)value + 2u >= levels[i] && (unsigned)value <= levels[i] + 2u) return i;
	return 4;
}

static int draw_coordinates(void)
{
	unsigned x, y, bit;
	uint8_t *image = coordinates + 32;
	static const uint8_t rgb[4] = {0, 75, 168, 255};
	/* I8 uses 8x4 texel tiles. Generate an independent source-coordinate
 * texture, then rasterize it through two <=640-wide EFB passes. */
	for(y = 0; y < VS_HEIGHT; y++)
		for(x = 0; x < VS_WIDTH; x++) {
			unsigned code = ((x >> ((y / 6u) % 10u)) & 1u) |
				(((y >> ((x / 6u) % 9u)) & 1u) << 1);
			texture[(y / 4u * (VS_WIDTH / 8u) + x / 8u) * 32u + y % 4u * 8u + x % 8u] = rgb[code];
		}
	DCFlushRange(texture, sizeof(texture));
	GX_InvalidateTexAll();
	for(x = 0; x < VS_WIDTH; x += 640) {
		unsigned w = VS_WIDTH - x < 640 ? VS_WIDTH - x : 640;
		viewport(w, VS_HEIGHT);
		textured(texture, VS_WIDTH, VS_HEIGHT, GX_TF_I8);
		quad(0, 0, (float)w, VS_HEIGHT, (GXColor){255, 255, 255, 255},
			(float)x / VS_WIDTH, 0, (float)(x + w) / VS_WIDTH, 1);
		GX_SetDispCopySrc(0, 0, w, VS_HEIGHT);
		GX_SetDispCopyDst(VS_WIDTH, VS_HEIGHT);
		GX_CopyDisp(image + x * 2u, GX_FALSE);
	}
	if(!fence()) return 0;
	DCInvalidateRange(image, FRAME_BYTES);
	result.coordinate_fenced = 1;
	result.coordinate_crc = vl_crc32(image, FRAME_BYTES);
	for(x = 0; x < VS_WIDTH; x++) {
		unsigned decoded = 0;
		for(bit = 0; bit < 10; bit++) {
			unsigned level = decode_level(image[((bit * 6u + 2u) * VS_WIDTH + x) * 2u]);
			if(level > 3) {
				result.error = 0x30000000u | bit << 24 | x << 8 | image[((bit * 6u + 2u) * VS_WIDTH + x) * 2u];
				return 0;
			}
			decoded |= (level & 1u) << bit;
		}
		if(decoded != x) { result.error = 0x40000000u | x << 16 | decoded; return 0; }
	}
	for(y = 0; y < VS_HEIGHT; y++) {
		unsigned decoded = 0;
		for(bit = 0; bit < 9; bit++) {
			unsigned level = decode_level(image[(y * VS_WIDTH + bit * 6u + 2u) * 2u]);
			if(level > 3) { result.error = 0x50000000u | y << 16 | bit; return 0; }
			decoded |= (level >> 1u) << bit;
		}
		if(decoded != y) { result.error = 0x60000000u | y << 16 | decoded; return 0; }
	}
	result.coordinate_crc = vl_crc32(image, FRAME_BYTES);
	return 1;
}

static int canaries(void)
{
	unsigned v, i;
	for(v = 0; v <= VS_VARIANTS; v++) {
		uint8_t *p = v == VS_VARIANTS ? coordinates : images[v];
		DCInvalidateRange(p, FRAME_BYTES + 64);
		for(i = 0; i < 32; i++)
			if(p[i] != 0xa5 || p[FRAME_BYTES + 32 + i] != 0xa5) return 0;
	}
	return 1;
}

static int scene_oracle(void)
{
	unsigned x, y;
	for(y = 0; y < VS_HEIGHT; y++)
		for(x = 0; x < VS_WIDTH; x++) {
			unsigned expected = 16u + (219u * vs_pixel(VS_FRAMES - 1u, x, y) + 127u) / 255u;
			unsigned value = images[0][32 + (y * VS_WIDTH + x) * 2u];
			unsigned error = value > expected ? value - expected : expected - value;
			if(error > result.oracle_max_error) result.oracle_max_error = error;
			if(error > 3) return 0;
		}
	return 1;
}

static void run(void)
{
	Mtx model;
	unsigned f, v;
	uint64_t start;
	memset(images, 0xa5, sizeof(images));
	memset(coordinates, 0xa5, sizeof(coordinates));
	DCFlushRange(images, sizeof(images));
	DCFlushRange(coordinates, sizeof(coordinates));
	DCFlushRange(cached, sizeof(cached));
	GX_Init(fifo, sizeof(fifo));
	GX_SetDrawDoneCallback(draw_done);
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDither(GX_FALSE);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetAlphaUpdate(GX_FALSE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	guMtxIdentity(model);
	GX_LoadPosMtxImm(model, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetDispCopyYScale(1);
	GX_SetDispCopyFrame2Field(GX_COPY_PROGRESSIVE);
	GX_SetFieldMode(GX_FALSE, GX_FALSE);
	GX_SetCopyFilter(GX_FALSE, TVNtsc480Prog.sample_pattern, GX_FALSE, TVNtsc480Prog.vfilter);
	GX_SetGPMetric(GX_PERF0_VERTICES, GX_PERF1_TEXELS);
	if(!fence()) { result.error = 2; return; }
	progress = 1;
	if(!draw_coordinates()) { if(!result.error) result.error = 3; return; }
	progress = 2;
	for(f = 0; f < VS_FRAMES; f++) {
		for(v = 0; v < VS_VARIANTS; v++) {
			if(!draw_scene(v, f)) { result.error = 4; return; }
			start = gettime();
			if(v && memcmp(images[0] + 32, images[v] + 32, FRAME_BYTES)) {
				result.variant[v].mismatches++;
				result.error = 1000u + f * 10u + v;
				return;
			}
			if(v) result.matched_frames++;
			result.variant[v].compare_us += elapsed_us(start);
		}
		progress = f + 3;
	}
	result.canaries_ok = canaries();
	if(!result.canaries_ok) { result.error = 5; return; }
	if(!scene_oracle()) { result.error = 6; return; }
	for(v = 0; v < VS_VARIANTS; v++)
		result.variant[v].crc32 = vl_crc32(images[v] + 32, FRAME_BYTES);
}

static void *worker(void *unused)
{
	(void)unused;
	while(!state) usleep(1000);
	run();
	state = result.error ? 3 : 2;
	return NULL;
}

int vg_init(void)
{
	return LWP_CreateThread(&thread, worker, NULL, stack, sizeof(stack), 100) == 0;
}

int vg_start(void)
{
	uint32_t expected = 0;
	return atomic_compare_exchange_strong(&state, &expected, 1);
}

unsigned vg_state(void) { return state; }
unsigned vg_progress(void) { return progress; }
const vg_result *vg_results(void) { return &result; }
const uint8_t *vg_coordinates(void) { return coordinates + 32; }
const uint8_t *vg_scene(unsigned variant) { return variant < VS_VARIANTS ? images[variant] + 32 : NULL; }
