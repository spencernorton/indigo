#include <gccore.h>
#include <float.h>
#include <math.h>

#include "indigo_background.h"
#include "ui_anim.h"
#include "ui_color.h"
#include "ui_stage.h"

#define INDIGO_TAU 6.28318530718f
#define RADIAL_SEGMENTS 24
#define GLOBE_SEGMENTS 24
#define PRIMARY_WAVE_SEGMENTS 16
#define REAR_WAVE_SEGMENTS 12
#define CUBE_CAMERA_Z -5.4f
/* The boot fly-in's tumble: turns about the cube's own horizontal axis per
 * turn about its vertical one, so the spin shows every face on the way in. */
#define CUBE_INTRO_TUMBLE 0.45f
/* Boot progress at which the flown-in cube arrives and the background cube,
 * lit, takes over; ui_scene.c's fly-in takes 0.5 s of the 0.8 s boot. */
#define BOOT_CUBE_HANDOFF 0.625f
/* The veil is gone 0.12 s in, while the cube is still far off. */
#define BOOT_VEIL_LIFT 0.15f
#define CUBE_IDLE_SWAY_RATE 0.31f
#define CUBE_IDLE_SWAY_RADIANS 0.035f
/* Idle Animation › Sway: while Home rests, the cube turns from Home's own
 * pose (turned 0.28 rad, ui_scene.c) to its mirror image and back, once in
 * 2 pi / CUBE_SWAY_RATE seconds (14): at the far end it shows its left side
 * as much as Home shows its right, and never more, so the face in front
 * stays in front and the cube's outline is never wider than Home's. */
#define CUBE_SWAY_RADIANS 0.28f
#define CUBE_SWAY_RATE 0.45f
#define CUBE_SWAY_RESTART_BLEND 0.01f
/* While Home rests the studio the glass mirrors turns this far (radians)
 * about the view axis and back, at this rate, so its highlights drift. */
#define GLASS_STUDIO_DRIFT 0.20f
#define GLASS_STUDIO_DRIFT_RATE 0.45f
/* How far the studio's lights stand from the cube, in cube sizes: nearer
 * spreads a face's reflection over more of them. */
#define GLASS_STUDIO_REACH 3.0f
#define HOME_DECORATIVE_STRENGTH 0.76f
/* Every icon is drawn this far out along its face's normal: just above the
 * glass (1.0), and no further, or an icon on a face seen nearly edge-on
 * would hang past the cube's outline. */
#define FACE_ICON_PLANE 1.012f
/* A face turned to the camera lifts its icon this much further, so the
 * icon floats near the cube instead of lying in it, and shifts against
 * the glass as the cube turns and sways. Only faces within some 37
 * degrees of the camera rise (faceIconLift), and none of those has an
 * icon near the outline. */
#define FACE_ICON_LIFT 0.04f
#define FACE_POLYGON_MAX 48
#define FACE_BAND_MAX 80
#define FACE_ARC_MAX 24
#define CONTROLLER_IDLE_HOLD 2.0f
#define CONTROLLER_IDLE_RELEASE 0.12f
/* The idle presses' cycle: six seconds, give or take a millisecond, so that
 * the clock's wrap holds a whole number of them. */
#define CONTROLLER_PRESS_CYCLE (UI_ANIM_TIME_WRAP_SECONDS / 1047.0f)

typedef struct indigoPoint {
	float x;
	float y;
} indigoPoint_t;

typedef struct cubeRasterTransform {
	Mtx model;
	Mtx semanticFaces[UI_HOME_FACE_COUNT];
	float motifAlpha[UI_HOME_FACE_COUNT];
	float scaleX;
	float scaleY;
} cubeRasterTransform_t;

typedef struct cubeSurfaceQuad {
	guVector point[4];
	GXColor color[4];
} cubeSurfaceQuad_t;

typedef struct cubeOutline {
	indigoPoint_t point[24];
	int count;
	/* Measured once a frame by buildCubeOutline for every edge drawn
	 * against it: each side's length in stage units and in frame pixels,
	 * and each corner's outward miter (joined[] false: no miter). */
	float length[24];
	float pixels[24];
	indigoPoint_t corner[24];
	bool joined[24];
} cubeOutline_t;

typedef struct cubeCoverageEdge {
	guVector eye[2];
	indigoPoint_t outward[2];
	GXColor color[2];
} cubeCoverageEdge_t;

/* Controller icon state: stick axes in -1..1 (y up) and PAD_* bits held. */
typedef struct controllerPose {
	float stickX;
	float stickY;
	float substickX;
	float substickY;
	u32 pressed;
} controllerPose_t;

/* Idle play resumes only once the controller has been left alone, so it is
 * never mistaken for the user's own input. It gives way to live input over
 * CONTROLLER_IDLE_RELEASE rather than in one frame. */
typedef struct controllerIdle {
	float lastLiveInput;
	bool liveSeen;
	float shown;	/* how much idle play shows, easing down to the target */
	float lastSeconds;
} controllerIdle_t;

typedef struct waveOscillator {
	float sine;
	float cosine;
	float stepSine;
	float stepCosine;
} waveOscillator_t;

/* Gekko has no square-root instruction: newlib's sqrtf works bit by bit,
 * some 300 instructions a call, and a frame takes thousands. Two Newton
 * steps from the classic estimate land within 5 parts in a million, under
 * a sixty-fourth of a pixel anywhere on the stage. */
static float fastSqrt(float x)
{
	union { float f; u32 i; } bits = {x};
	float y;

	if(!(x >= FLT_MIN && x <= FLT_MAX)) return sqrtf(x);
	bits.i = 0x5f3759dfu - (bits.i >> 1);
	y = bits.f;
	y *= 1.5f - 0.5f * x * y * y;
	y *= 1.5f - 0.5f * x * y * y;
	return x * y;
}

static void putVertex(indigoPoint_t point, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(point.x, point.y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

static void setupRasterPipeline(void)
{
	Mtx44 projection;
	Mtx modelView;

	guMtxIdentity(modelView);
	GX_LoadPosMtxImm(modelView, GX_PNMTX0);
	guOrtho(projection, 0.0f, 480.0f, 0.0f, 640.0f, 0.0f, 1.0f);
	UIStage_Project(projection);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetNumChans(1);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetColorUpdate(GX_ENABLE);
	GX_SetCullMode(GX_CULL_NONE);
}

/* The stage, and at boot the veil over it: a veil in the stage's own colors
 * shows no step when the menu activates, and lifts to show the scene.
 * shade darkens it (UIColor_BackdropShade). */
static void drawIndigoWash(u8 alpha, float shade)
{
	float left = UIStage_Left(), right = UIStage_Right();
#define WASH(r, g, b) (GXColor) {(u8)((r) * shade + 0.5f), (u8)((g) * shade + 0.5f), \
	(u8)((b) * shade + 0.5f), alpha}

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		/* This pass deliberately replaces the legacy grey backdrop rather than
		 * tinting it. The cube needs a clean, high-contrast stage. */
		putVertex((indigoPoint_t) {left, 0.0f}, WASH(6, 6, 22));
		putVertex((indigoPoint_t) {right, 0.0f}, WASH(9, 7, 27));
		putVertex((indigoPoint_t) {right, 480.0f}, WASH(29, 19, 65));
		putVertex((indigoPoint_t) {left, 480.0f}, WASH(19, 14, 48));
	GX_End();
#undef WASH
}

static void initWaveOscillator(waveOscillator_t *oscillator, float phase,
		float stepSine, float stepCosine)
{
	oscillator->sine = sinf(phase);
	oscillator->cosine = cosf(phase);
	oscillator->stepSine = stepSine;
	oscillator->stepCosine = stepCosine;
}

static void advanceWaveOscillator(waveOscillator_t *oscillator)
{
	float sine = oscillator->sine;
	float cosine = oscillator->cosine;

	oscillator->sine = sine * oscillator->stepCosine +
		cosine * oscillator->stepSine;
	oscillator->cosine = cosine * oscillator->stepCosine -
		sine * oscillator->stepSine;
}

static void buildWavePath(float *center, float *halfWidth, int segments,
		float baseY, float amplitudeA, float amplitudeB, float baseHalfWidth,
		float widthAmplitude, float amplitudeScale,
		waveOscillator_t centerA, waveOscillator_t centerB,
		waveOscillator_t width)
{
	for(int i = 0; i <= segments; i++) {
		center[i] = baseY + amplitudeScale *
			(amplitudeA * centerA.sine + amplitudeB * centerB.sine);
		halfWidth[i] = baseHalfWidth + widthAmplitude * width.sine;
		advanceWaveOscillator(&centerA);
		advanceWaveOscillator(&centerB);
		advanceWaveOscillator(&width);
	}
}

static float waveEdgeFade(int point, int segments)
{
	float u = (float)point / (float)segments;
	return 4.0f * u * (1.0f - u);
}

static GXColor waveVertexColor(GXColor color, float fade, float strength)
{
	color.a = (u8)((float)color.a * fade * strength);
	return color;
}

static bool railJoin(indigoPoint_t previous, indigoPoint_t point,
		indigoPoint_t next, indigoPoint_t *join);

static bool buildRasterJoins(const indigoPoint_t *points, indigoPoint_t *joins,
		int count, bool closed)
{
	if(count < 2) return false;
	for(int i = 0; i < count; i++) {
		indigoPoint_t previous = points[(i + count - 1) % count];
		indigoPoint_t next = points[(i + 1) % count];
		if(!closed && i == 0) previous = (indigoPoint_t) {
			2.0f * points[0].x - next.x, 2.0f * points[0].y - next.y};
		if(!closed && i == count - 1) next = (indigoPoint_t) {
			2.0f * points[i].x - previous.x, 2.0f * points[i].y - previous.y};
		if(!railJoin(previous, points[i], next, &joins[i])) return false;
	}
	return true;
}

static void drawRasterStroke(const indigoPoint_t *points,
		const indigoPoint_t *joins, const GXColor *colors, int count,
		const float *offsets, int bands)
{
	/* All segments share their exact joint vertices. Transparent outer rows
	 * replace GX line rasterization without gaps or additive joint overlap. */
	for(int band = 0; band < bands; band++) {
		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, count * 2);
		for(int i = 0; i < count; i++) for(int side = band; side <= band + 1; side++) {
			GXColor color = colors[i];
			if(side == 0 || side == bands) color.a = 0;
			putVertex((indigoPoint_t) {points[i].x + joins[i].x * offsets[side],
				points[i].y + joins[i].y * offsets[side]}, color);
		}
		GX_End();
	}
}

static void drawWaveFeather(const float *center, const float *halfWidth,
		int segments, float startX, float span, float side, GXColor color,
		float strength)
{
	indigoPoint_t points[PRIMARY_WAVE_SEGMENTS + 1];
	indigoPoint_t joins[PRIMARY_WAVE_SEGMENTS + 1];
	if(segments < 1 || segments > PRIMARY_WAVE_SEGMENTS) return;
	for(int i = 0; i <= segments; i++) points[i] = (indigoPoint_t) {
		startX + span * (float)i / segments, center[i] + halfWidth[i] * side};
	if(!buildRasterJoins(points, joins, segments + 1, false)) return;
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (segments + 1) * 2);
	for(int i = 0; i <= segments; i++) {
		GXColor edge = waveVertexColor(color, waveEdgeFade(i, segments), strength);
		putVertex(points[i], edge);
		edge.a = 0;
		putVertex((indigoPoint_t) {points[i].x + joins[i].x * side,
			points[i].y + joins[i].y * side}, edge);
	}
	GX_End();
}

static void drawSilkWaves(float seconds, bool animated, float strength)
{
	static const float rearRows[3] = {-1.0f, 0.0f, 1.0f};
	static const GXColor rearColors[3] = {
		{55, 47, 140, 4}, {128, 105, 232, 48}, {42, 31, 105, 4}
	};
	static const float primaryRows[4] = {-1.0f, -0.28f, 0.30f, 1.0f};
	static const GXColor primaryColors[4] = {
		{86, 60, 168, 6}, {196, 178, 255, 82},
		{113, 85, 210, 46}, {45, 31, 103, 6}
	};
	float rearCenter[REAR_WAVE_SEGMENTS + 1];
	float rearWidth[REAR_WAVE_SEGMENTS + 1];
	float primaryCenter[PRIMARY_WAVE_SEGMENTS + 1];
	float primaryWidth[PRIMARY_WAVE_SEGMENTS + 1];
	float motionTime = animated ? seconds : 0.0f;
	/* The ribbons run 48 units past each edge the frame shows. */
	float waveLeft = UIStage_Left() - 48.0f;
	float waveSpan = UIStage_Right() - UIStage_Left() + 96.0f;
	float amplitudeScale;
	float primaryOffsetX;
	waveOscillator_t centerA;
	waveOscillator_t centerB;
	waveOscillator_t width;

	if(strength <= 0.0f) {
		return;
	}
	if(strength > 1.0f) {
		strength = 1.0f;
	}
	amplitudeScale = 0.70f + strength * 0.30f;
	primaryOffsetX = (1.0f - strength) * -24.0f;

	/* Precomputed angular steps keep the per-frame trigonometry bounded to
	 * each slowly changing phase rather than each screen sample. */
	initWaveOscillator(&centerA, 2.05f - motionTime * 0.031f,
		0.328866647f, 0.944376370f);
	initWaveOscillator(&centerB, 0.20f + motionTime * 0.018f,
		0.608761429f, 0.793353340f);
	initWaveOscillator(&width, 1.40f + motionTime * 0.016f,
		0.377840787f, 0.925870585f);
	buildWavePath(rearCenter, rearWidth, REAR_WAVE_SEGMENTS,
		220.0f, 30.0f, 7.0f, 31.0f, 4.0f, amplitudeScale,
		centerA, centerB, width);

	initWaveOscillator(&centerA, 0.35f + motionTime * 0.052f,
		0.301537960f, 0.953454172f);
	initWaveOscillator(&centerB, 1.15f - motionTime * 0.029f,
		0.562083378f, 0.827080574f);
	initWaveOscillator(&width, 0.80f - motionTime * 0.021f,
		0.353474844f, 0.935444031f);
	buildWavePath(primaryCenter, primaryWidth, PRIMARY_WAVE_SEGMENTS,
		272.0f, 43.0f, 10.0f, 24.0f, 5.0f, amplitudeScale,
		centerA, centerB, width);

	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
	/* The rear fringe stays behind both ribbons. Its outward-only coverage
	 * does not overlap its own fill or paint a seam over the primary wave. */
	drawWaveFeather(rearCenter, rearWidth, REAR_WAVE_SEGMENTS,
		waveLeft, waveSpan, -1.0f, rearColors[0], strength);
	drawWaveFeather(rearCenter, rearWidth, REAR_WAVE_SEGMENTS,
		waveLeft, waveSpan, 1.0f, rearColors[2], strength);
	GX_Begin(GX_QUADS, GX_VTXFMT0,
		REAR_WAVE_SEGMENTS * 2 * 4 + PRIMARY_WAVE_SEGMENTS * 3 * 4);
	for(int segment = 0; segment < REAR_WAVE_SEGMENTS; segment++) {
		float x0 = waveLeft + waveSpan * (float)segment / REAR_WAVE_SEGMENTS;
		float x1 = waveLeft + waveSpan * (float)(segment + 1) / REAR_WAVE_SEGMENTS;
		float fade0 = waveEdgeFade(segment, REAR_WAVE_SEGMENTS);
		float fade1 = waveEdgeFade(segment + 1, REAR_WAVE_SEGMENTS);
		for(int row = 0; row < 2; row++) {
			putVertex((indigoPoint_t) {x0,
				rearCenter[segment] + rearWidth[segment] * rearRows[row]},
				waveVertexColor(rearColors[row], fade0, strength));
			putVertex((indigoPoint_t) {x1,
				rearCenter[segment + 1] + rearWidth[segment + 1] * rearRows[row]},
				waveVertexColor(rearColors[row], fade1, strength));
			putVertex((indigoPoint_t) {x1,
				rearCenter[segment + 1] + rearWidth[segment + 1] * rearRows[row + 1]},
				waveVertexColor(rearColors[row + 1], fade1, strength));
			putVertex((indigoPoint_t) {x0,
				rearCenter[segment] + rearWidth[segment] * rearRows[row + 1]},
				waveVertexColor(rearColors[row + 1], fade0, strength));
		}
	}
	for(int segment = 0; segment < PRIMARY_WAVE_SEGMENTS; segment++) {
		float x0 = primaryOffsetX + waveLeft +
			waveSpan * (float)segment / PRIMARY_WAVE_SEGMENTS;
		float x1 = primaryOffsetX + waveLeft +
			waveSpan * (float)(segment + 1) / PRIMARY_WAVE_SEGMENTS;
		float fade0 = waveEdgeFade(segment, PRIMARY_WAVE_SEGMENTS);
		float fade1 = waveEdgeFade(segment + 1, PRIMARY_WAVE_SEGMENTS);
		for(int row = 0; row < 3; row++) {
			putVertex((indigoPoint_t) {x0,
				primaryCenter[segment] + primaryWidth[segment] * primaryRows[row]},
				waveVertexColor(primaryColors[row], fade0, strength));
			putVertex((indigoPoint_t) {x1,
				primaryCenter[segment + 1] + primaryWidth[segment + 1] * primaryRows[row]},
				waveVertexColor(primaryColors[row], fade1, strength));
			putVertex((indigoPoint_t) {x1,
				primaryCenter[segment + 1] + primaryWidth[segment + 1] * primaryRows[row + 1]},
				waveVertexColor(primaryColors[row + 1], fade1, strength));
			putVertex((indigoPoint_t) {x0,
				primaryCenter[segment] + primaryWidth[segment] * primaryRows[row + 1]},
				waveVertexColor(primaryColors[row + 1], fade0, strength));
		}
	}
	GX_End();
	drawWaveFeather(primaryCenter, primaryWidth, PRIMARY_WAVE_SEGMENTS,
		primaryOffsetX + waveLeft, waveSpan, -1.0f, primaryColors[0], strength);
	drawWaveFeather(primaryCenter, primaryWidth, PRIMARY_WAVE_SEGMENTS,
		primaryOffsetX + waveLeft, waveSpan, 1.0f, primaryColors[3], strength);

	/* A single restrained additive shoulder provides the familiar silk crest
	 * without turning the background into a competing luminous object. Its
	 * original 8/6-pixel integrated width is a 1/3-pixel core plus two linear
	 * one-pixel fringes; the same color, alpha, path and endpoint fade remain. */
	static const float crestOffsets[4] = {-7.0f/6.0f, -1.0f/6.0f, 1.0f/6.0f, 7.0f/6.0f};
	indigoPoint_t crest[PRIMARY_WAVE_SEGMENTS + 1], joins[PRIMARY_WAVE_SEGMENTS + 1];
	GXColor crestColors[PRIMARY_WAVE_SEGMENTS + 1];
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int i = 0; i <= PRIMARY_WAVE_SEGMENTS; i++) {
		crest[i] = (indigoPoint_t) {primaryOffsetX + waveLeft +
			waveSpan * (float)i / PRIMARY_WAVE_SEGMENTS,
			primaryCenter[i] - primaryWidth[i] * 0.28f};
		crestColors[i] = waveVertexColor((GXColor) {238, 232, 255, 34},
			waveEdgeFade(i, PRIMARY_WAVE_SEGMENTS), strength);
	}
	if(buildRasterJoins(crest, joins, PRIMARY_WAVE_SEGMENTS + 1, false))
		drawRasterStroke(crest, joins, crestColors, PRIMARY_WAVE_SEGMENTS + 1, crestOffsets, 3);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
}


/* The waves' clock: the menu clock's steps at Wave Speed's pace, so a new
 * speed counts from where the waves are. It wraps where the menu clock does
 * (ui_anim.c): every wave rate is a multiple of 0.001 rad/s, so no phase
 * jumps there at any speed, where scaling the menu clock itself would jump
 * at its wrap unless the speed kept every rate a multiple. */
#define WAVE_CLOCK_WRAP 6283.18530718f
static float waveSpeedSetting = 1.0f, waveLastSeconds = -1.0f, waveSeconds;
static bool wavesShown = true;

void IndigoBackground_SetWaveSpeed(float speed)
{
	waveSpeedSetting = speed;
}

void IndigoBackground_SetWaves(bool shown)
{
	wavesShown = shown;
}

/* Sway's clock: how long Home has rested. While the cube leaves its rest
 * (the idle blend falling) it waits, so a shallow dip (one that keeps the
 * blend above CUBE_SWAY_RESTART_BLEND) picks the same swing up where it was.
 * A press that takes Home off its rest takes the blend under that, and the
 * clock starts over from 0, so the next swing starts from Home's own pose;
 * a turn's blend never quite reaches 0 before Home rests again, and what the
 * restart drops is under 2 * CUBE_SWAY_RADIANS * CUBE_SWAY_RESTART_BLEND
 * (0.006).
 * A frame's two cube passes read it at the same seconds and step it once; a
 * frame that took long holds the swing rather than jumping it. */
static bool idleSway;
static float swayLastSeconds = -1.0f, swayLastBlend, swaySeconds;

void IndigoBackground_SetIdleSway(bool sway)
{
	idleSway = sway;
}

static float swayClock(float seconds, float idleBlend)
{
	float step = swayLastSeconds < 0.0f ? 0.0f : seconds - swayLastSeconds;

	if(step < 0.0f) {
		step += UI_ANIM_TIME_WRAP_SECONDS;  /* the menu clock wrapped */
	}
	swayLastSeconds = seconds;
	if(idleBlend < CUBE_SWAY_RESTART_BLEND) {
		swaySeconds = 0.0f;
	}
	else if(idleBlend >= swayLastBlend) {
		swaySeconds = fmodf(swaySeconds + fminf(step, 0.1f),
			6.28318531f / CUBE_SWAY_RATE);
	}
	swayLastBlend = idleBlend;
	return swaySeconds;
}

static float waveClock(float seconds)
{
	float step = waveLastSeconds < 0.0f ? seconds : seconds - waveLastSeconds;

	if(step < 0.0f) {
		step += WAVE_CLOCK_WRAP;  /* the menu clock wrapped */
	}
	waveLastSeconds = seconds;
	waveSeconds = fmodf(waveSeconds + step * waveSpeedSetting, WAVE_CLOCK_WRAP);
	return waveSeconds;
}

static void drawGlobeGrid(float centerX, float centerY, float drift)
{
	static const float radii[6][2] = {
		{252.0f, 176.0f}, {252.0f, 116.0f}, {252.0f, 58.0f},
		{70.0f, 184.0f}, {140.0f, 184.0f}, {218.0f, 184.0f}
	};
	const float step = INDIGO_TAU / GLOBE_SEGMENTS;
	const float stepCos = cosf(step);
	const float stepSin = sinf(step);
	GXColor color = {117, 101, 209, 7};
	static const float offsets[3] = {-0.5f, 0.0f, 0.5f};
	indigoPoint_t points[GLOBE_SEGMENTS + 1], joins[GLOBE_SEGMENTS + 1];
	GXColor colors[GLOBE_SEGMENTS + 1];

	/* A one-pixel triangular profile retains the original 3/6-pixel
	 * integrated width. Reuse the first join exactly to close every ring. */
	for(int ring = 0; ring < 6; ring++) {
		float unitX = 1.0f;
		float unitY = 0.0f;
		for(int segment = 0; segment < GLOBE_SEGMENTS; segment++) {
			float nextX = unitX * stepCos - unitY * stepSin;
			float nextY = unitX * stepSin + unitY * stepCos;
			points[segment] = (indigoPoint_t) {
				centerX + radii[ring][0] * unitX,
				centerY + radii[ring][1] * unitY + drift
			};
			colors[segment] = color;
			unitX = nextX;
			unitY = nextY;
		}
		if(!buildRasterJoins(points, joins, GLOBE_SEGMENTS, true)) continue;
		points[GLOBE_SEGMENTS] = points[0];
		joins[GLOBE_SEGMENTS] = joins[0];
		colors[GLOBE_SEGMENTS] = colors[0];
		drawRasterStroke(points, joins, colors, GLOBE_SEGMENTS + 1, offsets, 2);
	}
}

static void drawRadialDisc(float centerX, float centerY, float radiusX, float radiusY,
		GXColor centerColor, GXColor edgeColor)
{
	const float step = INDIGO_TAU / RADIAL_SEGMENTS;
	const float stepCos = cosf(step);
	const float stepSin = sinf(step);
	float unitX = 1.0f;
	float unitY = 0.0f;

	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, RADIAL_SEGMENTS + 2);
		putVertex((indigoPoint_t) {centerX, centerY}, centerColor);
		for(int i = 0; i <= RADIAL_SEGMENTS; i++) {
			putVertex((indigoPoint_t) {
				centerX + radiusX * unitX,
				centerY + radiusY * unitY
			}, edgeColor);
			float nextX = (unitX * stepCos) - (unitY * stepSin);
			unitY = (unitX * stepSin) + (unitY * stepCos);
			unitX = nextX;
		}
	GX_End();
}

static Mtx44 cubeProjection;

/* The cube's perspective, loaded again wherever a screen-space pass left an
 * orthographic one: the icons after the bloom. */
static void loadCubeProjection(void)
{
	static bool ready;
	Mtx44 projection;

	if(!ready) {
		/* Far enough for the boot fly-in, which starts 60 units back. */
		guPerspective(cubeProjection, 42.0f, 640.0f / 480.0f, 0.1f, 80.0f);
		ready = true;
	}
	/* The rails and the glass measure in stage units through
	 * cubeProjection; only the copy GX draws with is squeezed. */
	for(int row = 0; row < 4; row++)
		for(int column = 0; column < 4; column++)
			projection[row][column] = cubeProjection[row][column];
	UIStage_Project(projection);
	GX_LoadProjectionMtx(projection, GX_PERSPECTIVE);
}

static void setupCubePipeline(const uiSceneFrame_t *scene, float seconds, bool animated,
		cubeRasterTransform_t *raster)
{
	static const guVector xAxis = {1.0f, 0.0f, 0.0f};
	static const guVector yAxis = {0.0f, 1.0f, 0.0f};
	Mtx rotateX;
	Mtx rotateY;
	Mtx navigation;
	Mtx rotation;
	Mtx model;
	Mtx translation;
	/* Home faces are semantic destinations. Keep the selected face cardinal;
	 * decorative motion may breathe around it, but must never rotate it away. */
	float idleBlend = scene->homeIdleBlend;
	float idleYaw = animated ? sinf(seconds * CUBE_IDLE_SWAY_RATE) *
		CUBE_IDLE_SWAY_RADIANS * idleBlend : 0.0f;
	float yaw;
	float pitch = 0.09f + scene->cubePitch +
		(animated ? sinf(seconds * 0.17f) * 0.030f : 0.0f);
	float bob = animated ? sinf(seconds * 0.62f) * 0.035f : 0.0f;

	/* Sway turns to the mirror of Home's pose and back instead. */
	if(idleSway) {
		float sway = swayClock(seconds, animated ? idleBlend : 0.0f);

		idleYaw = animated ? -CUBE_SWAY_RADIANS *
			(1.0f - cosf(sway * CUBE_SWAY_RATE)) * idleBlend : 0.0f;
	}
	yaw = scene->cubeYaw + idleYaw;

	loadCubeProjection();

	guMtxRotAxisRad(rotateX, &xAxis, pitch);
	guMtxRotAxisRad(rotateY, &yAxis, yaw);
	guMtxConcat(rotateY, rotateX, rotation);
	/* The boot fly-in spins the cube about its vertical axis and tumbles it
	 * about its horizontal one; both unwind to square on the Home face. */
	if(scene->introSpin > 0.0f) {
		Mtx spin;

		guMtxRotAxisRad(spin, &yAxis, scene->introSpin);
		guMtxConcat(rotation, spin, rotation);
		guMtxRotAxisRad(spin, &xAxis, scene->introSpin * CUBE_INTRO_TUMBLE);
		guMtxConcat(rotation, spin, rotation);
	}
	guMtxIdentity(navigation);
	for(int row = 0; row < 3; row++) {
		for(int column = 0; column < 3; column++) {
			navigation[row][column] = scene->homeOrientation[row][column];
		}
	}
	/* Navigation is a true spatial rotation, separate from the authored camera
	 * tilt. Pre-composed quarter turns preserve mixed-axis order. */
	guMtxConcat(rotation, navigation, rotation);
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++) {
		guMtxIdentity(raster->semanticFaces[face]);
		for(int row = 0; row < 3; row++)
			for(int column = 0; column < 3; column++)
				raster->semanticFaces[face][row][column] =
					scene->homeMotifBasis[face][row][column];
	}
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++)
		raster->motifAlpha[face] = scene->homeMotifAlpha[face];
	guMtxScaleApply(rotation, rotation, scene->cubeScale, scene->cubeScale, scene->cubeScale);
	guMtxIdentity(translation);
	guMtxTransApply(translation, translation, scene->cubeX, scene->cubeY + bob,
		CUBE_CAMERA_Z - scene->introDistance);
	guMtxConcat(translation, rotation, model);
	GX_LoadPosMtxImm(model, GX_PNMTX0);
	guMtxCopy(model, raster->model);
	raster->scaleX = cubeProjection[0][0] * 320.0f;
	raster->scaleY = cubeProjection[1][1] * 240.0f;

	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	/* The glass is clear all through: no part of the cube writes depth, so
	 * its passes layer in the order drawCube draws them. */
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetNumChans(1);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetColorUpdate(GX_ENABLE);
	GX_SetCullMode(GX_CULL_BACK);
}

static void putCubeVertex(float x, float y, float z, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(x, y, z);
	GX_Color4u8(color.r, color.g, color.b, color.a);
}

static guVector cubeViewNormal(const cubeRasterTransform_t *raster, guVector n)
{
	guVector eye = {
		raster->model[0][0] * n.x + raster->model[0][1] * n.y + raster->model[0][2] * n.z,
		raster->model[1][0] * n.x + raster->model[1][1] * n.y + raster->model[1][2] * n.z,
		raster->model[2][0] * n.x + raster->model[2][1] * n.y + raster->model[2][2] * n.z
	};
	float length = fastSqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
	if(length < 0.0001f) return (guVector) {0.0f, 0.0f, 1.0f};
	return (guVector) {eye.x / length, eye.y / length, eye.z / length};
}

/* Outward body axes in buildCubeFaces order: back, left, right, bottom, top, front. */
static const guVector cubeFaceAxes[6] = {
	{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
	{0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
};

/* Tints are authored per camera direction in cubeFaceAxes order. Squared
 * unit-normal components sum to one, so a turning face blends between them
 * and the light stays with the camera instead of rotating with the cube. */
static void litCubeTints(const cubeRasterTransform_t *raster,
		const GXColor tints[6], GXColor lit[6])
{
	for(int face = 0; face < 6; face++) {
		guVector n = cubeViewNormal(raster, cubeFaceAxes[face]);
		const GXColor *x = &tints[n.x < 0.0f ? 1 : 2];
		const GXColor *y = &tints[n.y < 0.0f ? 3 : 4];
		const GXColor *z = &tints[n.z < 0.0f ? 0 : 5];
		float wx = n.x * n.x, wy = n.y * n.y, wz = n.z * n.z;
		lit[face] = (GXColor) {
			(u8)(x->r * wx + y->r * wy + z->r * wz + 0.5f),
			(u8)(x->g * wx + y->g * wy + z->g * wz + 0.5f),
			(u8)(x->b * wx + y->b * wy + z->b * wz + 0.5f),
			(u8)(x->a * wx + y->a * wy + z->a * wz + 0.5f)
		};
	}
}

static void buildCubeFaces(cubeSurfaceQuad_t quads[6], float outer, float inset,
		const GXColor colors[6])
{
	const float n = -inset, p = inset, back = -outer, front = outer;
	const guVector points[6][4] = {
		{{p,n,back}, {p,p,back}, {n,p,back}, {n,n,back}},
		{{-outer,n,n}, {-outer,p,n}, {-outer,p,p}, {-outer,n,p}},
		{{outer,n,p}, {outer,p,p}, {outer,p,n}, {outer,n,n}},
		{{n,-outer,p}, {p,-outer,p}, {p,-outer,n}, {n,-outer,n}},
		{{n,outer,n}, {p,outer,n}, {p,outer,p}, {n,outer,p}},
		{{n,n,front}, {n,p,front}, {p,p,front}, {p,n,front}}
	};

	/* Back, left, right, bottom, top, front. GX treats clockwise-to-viewer
	 * primitives as front-facing. Keep every original fill vertex unchanged. */
	for(int face = 0; face < 6; face++) {
		for(int vertex = 0; vertex < 4; vertex++) {
			quads[face].point[vertex] = points[face][vertex];
			quads[face].color[vertex] = colors[face];
		}
	}
}

static bool projectRailPoint(const cubeRasterTransform_t *raster,
		float x, float y, float z, guVector *eye, indigoPoint_t *screen)
{
	eye->x = raster->model[0][0] * x + raster->model[0][1] * y +
		raster->model[0][2] * z + raster->model[0][3];
	eye->y = raster->model[1][0] * x + raster->model[1][1] * y +
		raster->model[1][2] * z + raster->model[1][3];
	eye->z = raster->model[2][0] * x + raster->model[2][1] * y +
		raster->model[2][2] * z + raster->model[2][3];
	if(!isfinite(eye->x) || !isfinite(eye->y) || !isfinite(eye->z) ||
		eye->z >= -0.1f) {
		return false;
	}
	*screen = (indigoPoint_t) {raster->scaleX * eye->x / -eye->z,
		raster->scaleY * eye->y / -eye->z};
	return true;
}

static bool railJoin(indigoPoint_t previous, indigoPoint_t point,
		indigoPoint_t next, indigoPoint_t *join)
{
	/* Mitred in frame pixels, which the widescreen squeeze makes 4/3 of a
	 * stage unit across: one unit along the join is one frame pixel off
	 * both edges, so every fade spans one pixel in either screen shape. */
	float across = UIStage_PixelWidth();
	float ax = (point.x - previous.x) / across, ay = point.y - previous.y;
	float bx = (next.x - point.x) / across, by = next.y - point.y;
	float a = fastSqrt(ax * ax + ay * ay), b = fastSqrt(bx * bx + by * by);
	if(a < 0.001f || b < 0.001f) return false;
	ax /= a; ay /= a; bx /= b; by /= b;
	float denominator = 1.0f + ax * bx + ay * by;
	/* Near edge-on corners do not get an unbounded miter spike. */
	if(denominator < 0.125f) return false;
	*join = (indigoPoint_t) {(-ay - by) / denominator * across,
		(ax + bx) / denominator};
	return true;
}

static void putProjectedRailVertex(const cubeRasterTransform_t *raster,
		guVector eye, indigoPoint_t join, float offset, GXColor color)
{
	/* Preserve each endpoint's depth; only its camera-space X/Y move. The
	 * existing perspective projection and core depth test remain in force. */
	putCubeVertex(eye.x + join.x * offset * -eye.z / raster->scaleX,
		eye.y + join.y * offset * -eye.z / raster->scaleY, eye.z, color);
}

static float outlineCross(indigoPoint_t a, indigoPoint_t b, indigoPoint_t c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static void buildCubeOutline(const cubeRasterTransform_t *raster,
		const cubeSurfaceQuad_t faces[6], cubeOutline_t *outline)
{
	indigoPoint_t sorted[24], hull[48];
	int count = 0, used = 0;
	outline->count = 0;
	/* Six chamfer faces contain every outer mesh vertex. Their projected
	 * convex hull identifies the silhouette, including the sealed corner caps. */
	for(int face = 0; face < 6; face++) for(int vertex = 0; vertex < 4; vertex++) {
		guVector eye, point = faces[face].point[vertex];
		indigoPoint_t screen;
		if(!projectRailPoint(raster, point.x, point.y, point.z, &eye, &screen)) return;
		int at = 0;
		for(; at < count; at++) {
			if(fabsf(sorted[at].x - screen.x) < 0.0001f &&
				fabsf(sorted[at].y - screen.y) < 0.0001f) break;
		}
		if(at < count) continue;
		at = count++;
		while(at > 0 && (sorted[at - 1].x > screen.x ||
			(sorted[at - 1].x == screen.x && sorted[at - 1].y > screen.y))) {
			sorted[at] = sorted[at - 1];
			at--;
		}
		sorted[at] = screen;
	}
	if(count < 3) return;
	for(int i = 0; i < count; i++) {
		while(used >= 2 && outlineCross(hull[used - 2], hull[used - 1], sorted[i]) <= 0.001f) used--;
		hull[used++] = sorted[i];
	}
	int lower = used + 1;
	for(int i = count - 2; i >= 0; i--) {
		while(used >= lower && outlineCross(hull[used - 2], hull[used - 1], sorted[i]) <= 0.001f) used--;
		hull[used++] = sorted[i];
	}
	if(used < 4) return;
	outline->count = used - 1;
	for(int i = 0; i < outline->count; i++) outline->point[i] = hull[i];
	/* Every silhouette edge of the frame is measured against these; work
	 * them out once (sqrtf is a software loop on the console). */
	float across = UIStage_PixelWidth();
	for(int i = 0; i < outline->count; i++) {
		indigoPoint_t p = outline->point[i], q = outline->point[(i + 1) % outline->count];
		indigoPoint_t inward;
		float dx = q.x - p.x, dy = q.y - p.y;
		outline->length[i] = fastSqrt(dx * dx + dy * dy);
		outline->pixels[i] = fastSqrt(dx * dx / (across * across) + dy * dy);
		outline->joined[i] = railJoin(outline->point[(i + outline->count - 1) % outline->count],
			p, q, &inward);
		if(outline->joined[i]) outline->corner[i] = (indigoPoint_t) {-inward.x, -inward.y};
	}
}

static indigoPoint_t cubeOutlineNormal(const cubeOutline_t *outline, int corner,
		indigoPoint_t fallback)
{
	return outline->joined[corner] ? outline->corner[corner] : fallback;
}

static bool cubeOutlineEdge(const cubeOutline_t *outline,
		indigoPoint_t a, indigoPoint_t b, indigoPoint_t outward[2])
{
	for(int i = 0; i < outline->count; i++) {
		int next = (i + 1) % outline->count;
		indigoPoint_t p = outline->point[i], q = outline->point[next];
		float dx = q.x - p.x, dy = q.y - p.y;
		float length = outline->length[i];
		if(length < 0.001f) continue;
		float alongA = ((a.x - p.x) * dx + (a.y - p.y) * dy) / length;
		float alongB = ((b.x - p.x) * dx + (b.y - p.y) * dy) / length;
		if(fabsf(outlineCross(p, q, a)) / length > 0.005f ||
			fabsf(outlineCross(p, q, b)) / length > 0.005f ||
			alongA < -0.005f || alongA > length + 0.005f ||
			alongB < -0.005f || alongB > length + 0.005f) continue;
		/* Shared internal edges never reach this branch. Use the same miter
		 * for neighbouring boundary faces so their one-pixel fringes meet.
		 * Like railJoin's, the normal is a frame pixel long. */
		float across = UIStage_PixelWidth();
		float pixels = outline->pixels[i];
		indigoPoint_t normal = {dy / pixels * across, -dx / across / pixels};
		outward[0] = fabsf(alongA) < 0.005f ? cubeOutlineNormal(outline, i, normal) :
			(fabsf(alongA - length) < 0.005f ? cubeOutlineNormal(outline, next, normal) : normal);
		outward[1] = fabsf(alongB) < 0.005f ? cubeOutlineNormal(outline, i, normal) :
			(fabsf(alongB - length) < 0.005f ? cubeOutlineNormal(outline, next, normal) : normal);
		return true;
	}
	return false;
}

static void drawCubeSurfacePassVertices(const cubeRasterTransform_t *raster,
		const cubeOutline_t *outline, const cubeSurfaceQuad_t *quads, int count, int vertexCount,
		u8 cullMode)
{
	/* Fringes go on silhouette edges only: the outline has at most 24
	 * sides, each covered by at most three polygon edges (a banded bevel's
	 * end, a fanned corner's side). Static: the video thread's stack is small. */
	static cubeCoverageEdge_t edges[72];
	int edgeCount = 0;
	Mtx identity;
	if(count < 1 || count > 72 || (vertexCount != 3 && vertexCount != 4)) return;
	/* Preserve the fill vertices, gradients and draw order exactly.
	 * Coverage extends outward only, never opens mesh seams. */
	GX_LoadPosMtxImm(raster->model, GX_PNMTX0);
	GX_SetCullMode(cullMode);
	GX_Begin(vertexCount == 3 ? GX_TRIANGLES : GX_QUADS, GX_VTXFMT0,
		count * vertexCount);
	for(int quad = 0; quad < count; quad++) for(int vertex = 0; vertex < vertexCount; vertex++) {
		guVector p = quads[quad].point[vertex];
		putCubeVertex(p.x, p.y, p.z, quads[quad].color[vertex]);
	}
	GX_End();
	if(outline->count < 3) return;
	for(int quad = 0; quad < count; quad++) {
		guVector eyes[4];
		indigoPoint_t points[4];
		bool valid = true;
		float area = 0.0f;
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			guVector p = quads[quad].point[vertex];
			if(!projectRailPoint(raster, p.x, p.y, p.z, &eyes[vertex], &points[vertex])) valid = false;
		}
		if(!valid) continue;
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			int next = (vertex + 1) % vertexCount;
			area += points[vertex].x * points[next].y - points[next].x * points[vertex].y;
		}
		if(fabsf(area) < 0.001f || (cullMode == GX_CULL_BACK && area >= 0.0f) ||
			(cullMode == GX_CULL_FRONT && area <= 0.0f)) continue;
		for(int vertex = 0; vertex < vertexCount && edgeCount < 72; vertex++) {
			int next = (vertex + 1) % vertexCount;
			cubeCoverageEdge_t *edge = &edges[edgeCount];
			if(!cubeOutlineEdge(outline, points[vertex], points[next], edge->outward)) continue;
			edge->eye[0] = eyes[vertex]; edge->eye[1] = eyes[next];
			edge->color[0] = quads[quad].color[vertex];
			edge->color[1] = quads[quad].color[next];
			edgeCount++;
		}
	}
	if(edgeCount == 0) return;
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_Begin(GX_QUADS, GX_VTXFMT0, edgeCount * 4);
	for(int i = 0; i < edgeCount; i++) {
		cubeCoverageEdge_t *edge = &edges[i];
		GXColor transparentA = edge->color[0], transparentB = edge->color[1];
		transparentA.a = transparentB.a = 0;
		putProjectedRailVertex(raster, edge->eye[0], edge->outward[0], 0.0f, edge->color[0]);
		putProjectedRailVertex(raster, edge->eye[1], edge->outward[1], 0.0f, edge->color[1]);
		putProjectedRailVertex(raster, edge->eye[1], edge->outward[1], 1.0f, transparentB);
		putProjectedRailVertex(raster, edge->eye[0], edge->outward[0], 1.0f, transparentA);
	}
	GX_End();
	GX_LoadPosMtxImm(raster->model, GX_PNMTX0);
	GX_SetCullMode(cullMode);
}

static void drawCubeSurfacePass(const cubeRasterTransform_t *raster,
		const cubeOutline_t *outline, const cubeSurfaceQuad_t *quads, int count,
		u8 cullMode)
{
	drawCubeSurfacePassVertices(raster, outline, quads, count, 4, cullMode);
}

/* The scene owns cardinal body-space symbol bases. During an axis change it
 * swaps maps only at zero opacity, so symbols never jump across visible faces. */
static guVector semanticFacePoint(const cubeRasterTransform_t *raster,
		int face, float u, float v, float plane)
{
	const float (*basis)[4] = raster->semanticFaces[face];
	return (guVector) {
		basis[0][0] * u + basis[0][1] * v + basis[0][2] * plane,
		basis[1][0] * u + basis[1][1] * v + basis[1][2] * plane,
		basis[2][0] * u + basis[2][1] * v + basis[2][2] * plane
	};
}

static void putSemanticMotifQuad(const cubeRasterTransform_t *raster, int face,
		const indigoPoint_t corners[4], float plane, GXColor color)
{
	color.a = (u8)((float)color.a * raster->motifAlpha[face]);
	guVector eyes[4];
	indigoPoint_t points[4], joins[4], center = {0.0f, 0.0f};
	float area = 0.0f, clearance = 1000.0f;
	bool sharp = false;
	for(int i = 0; i < 4; i++) {
		guVector point = semanticFacePoint(raster, face, corners[i].x, corners[i].y, plane);
		if(!projectRailPoint(raster, point.x, point.y, point.z,
			&eyes[i], &points[i])) goto hidden;
		center.x += points[i].x * 0.25f;
		center.y += points[i].y * 0.25f;
	}
	for(int i = 0; i < 4; i++) {
		int next = (i + 1) % 4;
		area += points[i].x * points[next].y - points[next].x * points[i].y;
	}
	/* Match the existing clockwise-to-viewer face winding. Invisible and
	 * collapsed motifs still emit the fixed count as transparent degenerates. */
	if(area >= -0.001f || color.a == 0) goto hidden;
	for(int i = 0; i < 4; i++) {
		int next = (i + 1) % 4;
		float dx = points[next].x - points[i].x;
		float dy = points[next].y - points[i].y;
		float length = fastSqrt(dx * dx + dy * dy);
		if(length < 0.001f) goto hidden;
		/* A slanted stroke on a face seen nearly edge-on, such as the clock's
		 * minute hand on a side face, has corners too sharp to mitre. */
		if(!railJoin(points[(i + 3) % 4], points[i], points[next], &joins[i])) sharp = true;
		float distance = fabsf(dx * (center.y - points[i].y) -
			dy * (center.x - points[i].x)) / length;
		if(distance < clearance) clearance = distance;
	}
	/* Inset the solid core by half a pixel; very narrow oblique strokes
	 * retain area through alpha instead of inverting their inner polygon. */
	float inset = fminf(0.5f, clearance * 0.5f);
	float outside = 1.0f - inset;
	if(sharp) {
		/* Its exact outline instead, unfeathered and at full strength: the
		 * rasterizer's coverage is its brightness, and a turned face is drawn
		 * as a supersampled picture (renderFacePictures), which smooths it. */
		for(int i = 0; i < 4; i++) joins[i] = (indigoPoint_t) {0.0f, 0.0f};
	}
	else {
		color.a = (u8)((float)color.a * fminf(1.0f, clearance * 2.0f));
	}
	GXColor transparent = color;
	transparent.a = 0;
	for(int i = 0; i < 4; i++) {
		putProjectedRailVertex(raster, eyes[i], joins[i], -inset, color);
	}
	for(int i = 0; i < 4; i++) {
		int next = (i + 1) % 4;
		putProjectedRailVertex(raster, eyes[i], joins[i], -inset, color);
		putProjectedRailVertex(raster, eyes[i], joins[i], outside, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], outside, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], -inset, color);
	}
	return;

hidden:
	for(int i = 0; i < 20; i++) {
		putCubeVertex(0.0f, 0.0f, CUBE_CAMERA_Z, (GXColor) {0, 0, 0, 0});
	}
}

static void putSemanticMotifRect(const cubeRasterTransform_t *raster, int face,
		float u0, float v0, float u1, float v1, float plane, GXColor color)
{
	const indigoPoint_t corners[4] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
	putSemanticMotifQuad(raster, face, corners, plane, color);
}

static void putSemanticFaceDiamond(const cubeRasterTransform_t *raster,
		int face, float u, float v, float radius, float plane, GXColor color)
{
	const indigoPoint_t corners[4] = {
		{u, v - radius}, {u - radius, v}, {u, v + radius}, {u + radius, v}
	};
	putSemanticMotifQuad(raster, face, corners, plane, color);
}

static void putSemanticFaceHand(const cubeRasterTransform_t *raster, int face, float x, float y, float length,
		float halfWidth, float tail, float plane, GXColor color)
{
	float perpendicularX = -y * halfWidth;
	float perpendicularY = x * halfWidth;
	float startX = -x * tail;
	float startY = -y * tail;
	float endX = x * length;
	float endY = y * length;

	const indigoPoint_t corners[4] = {
		{startX + perpendicularX, startY + perpendicularY},
		{endX + perpendicularX, endY + perpendicularY},
		{endX - perpendicularX, endY - perpendicularY},
		{startX - perpendicularX, startY - perpendicularY}
	};
	putSemanticMotifQuad(raster, face, corners, plane, color);
}

/* One book on a shelf: three pieces whose two gaps read as the spine's
 * bands. lean tips it about its bottom-left corner into the row. */
static void putSemanticFaceBook(const cubeRasterTransform_t *raster, int face,
		float u, float v, float width, float height, float lean, float plane,
		GXColor color)
{
	static const float bands[6] = {0.0f, 0.15f, 0.20f, 0.80f, 0.85f, 1.0f};
	float c = cosf(lean), s = sinf(lean);

	for(int i = 0; i < 6; i += 2) {
		float b0 = bands[i] * height, b1 = bands[i + 1] * height;
		const indigoPoint_t corners[4] = {
			{u - b0 * s, v + b0 * c},
			{u - b1 * s, v + b1 * c},
			{u + width * c - b1 * s, v + width * s + b1 * c},
			{u + width * c - b0 * s, v + width * s + b0 * c}
		};
		putSemanticMotifQuad(raster, face, corners, plane, color);
	}
}

/* A convex polygon on a semantic face, clockwise in face space, in its own
 * primitives: a solid fan inset by half a pixel and a coverage fringe that
 * falls to zero one native pixel out, as the motif quads do. Back-facing or
 * collapsed polygons draw nothing. */
static void drawFacePolygon(const cubeRasterTransform_t *raster, int face,
		const indigoPoint_t *corners, int count, float plane, GXColor color)
{
	guVector eyes[FACE_POLYGON_MAX];
	indigoPoint_t points[FACE_POLYGON_MAX], joins[FACE_POLYGON_MAX];
	indigoPoint_t center = {0.0f, 0.0f};
	float area = 0.0f, clearance = 1000.0f;

	color.a = (u8)((float)color.a * raster->motifAlpha[face]);
	if(count < 3 || count > FACE_POLYGON_MAX || color.a == 0) return;
	for(int i = 0; i < count; i++) {
		guVector point = semanticFacePoint(raster, face, corners[i].x, corners[i].y, plane);
		if(!projectRailPoint(raster, point.x, point.y, point.z,
			&eyes[i], &points[i])) return;
		center.x += points[i].x / (float)count;
		center.y += points[i].y / (float)count;
	}
	for(int i = 0; i < count; i++) {
		int next = (i + 1) % count;
		area += points[i].x * points[next].y - points[next].x * points[i].y;
	}
	if(area >= -0.001f) return;
	for(int i = 0; i < count; i++) {
		int next = (i + 1) % count;
		float dx = points[next].x - points[i].x;
		float dy = points[next].y - points[i].y;
		float length = fastSqrt(dx * dx + dy * dy);
		if(length <= 0.0f || !railJoin(points[(i + count - 1) % count],
			points[i], points[next], &joins[i])) return;
		float distance = fabsf(dx * (center.y - points[i].y) -
			dy * (center.x - points[i].x)) / length;
		if(distance < clearance) clearance = distance;
	}
	float inset = fminf(0.5f, clearance * 0.5f);
	float outside = 1.0f - inset;
	color.a = (u8)((float)color.a * fminf(1.0f, clearance * 2.0f));
	GXColor transparent = color;
	transparent.a = 0;
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, (u16)count);
	for(int i = 0; i < count; i++) {
		putProjectedRailVertex(raster, eyes[i], joins[i], -inset, color);
	}
	GX_End();
	GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 4));
	for(int i = 0; i < count; i++) {
		int next = (i + 1) % count;
		putProjectedRailVertex(raster, eyes[i], joins[i], -inset, color);
		putProjectedRailVertex(raster, eyes[i], joins[i], outside, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], outside, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], -inset, color);
	}
	GX_End();
}

/* A closed band of face-space width centred on a clockwise polyline: the
 * controller's outline and its stick gates. Neighbouring quads share their
 * mitred corners and each boundary fades over one native pixel, so the
 * additive icon pass never double-lights a seam. */
static void drawFaceBand(const cubeRasterTransform_t *raster, int face,
		const indigoPoint_t *centre, int count, float halfWidth, float plane,
		GXColor color)
{
	guVector eyes[2][FACE_BAND_MAX];
	indigoPoint_t points[2][FACE_BAND_MAX], joins[2][FACE_BAND_MAX];
	float area = 0.0f;

	color.a = (u8)((float)color.a * raster->motifAlpha[face]);
	if(count < 3 || count > FACE_BAND_MAX || color.a == 0) return;
	for(int i = 0; i < count; i++) {
		indigoPoint_t prev = centre[(i + count - 1) % count];
		indigoPoint_t next = centre[(i + 1) % count];
		float ax = centre[i].x - prev.x, ay = centre[i].y - prev.y;
		float bx = next.x - centre[i].x, by = next.y - centre[i].y;
		float al = fastSqrt(ax * ax + ay * ay), bl = fastSqrt(bx * bx + by * by);
		if(al <= 0.0f || bl <= 0.0f) return;
		/* Clockwise with v up: each edge's outward normal is its left side. */
		float nx = -ay / al - by / bl, ny = ax / al + bx / bl;
		float nl = fastSqrt(nx * nx + ny * ny);
		if(nl <= 0.0f) return;
		nx /= nl; ny /= nl;
		float miter = nx * (-ay / al) + ny * (ax / al);
		float reach = halfWidth / (miter > 0.5f ? miter : 0.5f);
		for(int side = 0; side < 2; side++) {
			float offset = side == 0 ? -reach : reach;
			guVector point = semanticFacePoint(raster, face,
				centre[i].x + nx * offset, centre[i].y + ny * offset, plane);
			if(!projectRailPoint(raster, point.x, point.y, point.z,
				&eyes[side][i], &points[side][i])) return;
		}
	}
	for(int i = 0; i < count; i++) {
		int next = (i + 1) % count;
		area += points[1][i].x * points[1][next].y - points[1][next].x * points[1][i].y;
	}
	if(area >= -0.001f) return;
	for(int side = 0; side < 2; side++) {
		for(int i = 0; i < count; i++) {
			if(!railJoin(points[side][(i + count - 1) % count], points[side][i],
				points[side][(i + 1) % count], &joins[side][i])) return;
		}
	}
	GXColor transparent = color;
	transparent.a = 0;
	/* Solid band, then the outer fringe outward and the inner fringe inward. */
	GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 12));
	for(int i = 0; i < count; i++) {
		int next = (i + 1) % count;
		putProjectedRailVertex(raster, eyes[0][i], joins[0][i], 0.0f, color);
		putProjectedRailVertex(raster, eyes[1][i], joins[1][i], 0.0f, color);
		putProjectedRailVertex(raster, eyes[1][next], joins[1][next], 0.0f, color);
		putProjectedRailVertex(raster, eyes[0][next], joins[0][next], 0.0f, color);
		for(int side = 0; side < 2; side++) {
			float fade = side == 0 ? -1.0f : 1.0f;
			putProjectedRailVertex(raster, eyes[side][i], joins[side][i], 0.0f, color);
			putProjectedRailVertex(raster, eyes[side][i], joins[side][i], fade, transparent);
			putProjectedRailVertex(raster, eyes[side][next], joins[side][next], fade, transparent);
			putProjectedRailVertex(raster, eyes[side][next], joins[side][next], 0.0f, color);
		}
	}
	GX_End();
}

/* The eight authored circle sizes share unit samples between the icon and
 * shadow passes, across faces and frames. Only 1,328 bytes, filled once
 * on the video thread: no allocation or trigonometry on a warm frame.
 * Keep the general emitters' other segment counts working uncached. */
static const indigoPoint_t *faceCircleSamples(int segments)
{
	static const u8 sides[] = {8, 12, 14, 16, 20, 24, 32, 40};
	static indigoPoint_t samples[8 + 12 + 14 + 16 + 20 + 24 + 32 + 40];
	static u8 ready;
	int offset = 0;

	for(unsigned slot = 0; slot < sizeof(sides) / sizeof(sides[0]); slot++) {
		if(segments != sides[slot]) {
			offset += sides[slot];
			continue;
		}
		if(!(ready & (1u << slot))) {
			for(int i = 0; i < segments; i++) {
				float angle = -INDIGO_TAU * (float)i / (float)segments;
				samples[offset + i] = (indigoPoint_t) {cosf(angle), sinf(angle)};
			}
			ready |= 1u << slot;
		}
		return samples + offset;
	}
	return NULL;
}

/* A filled circle of segments sides: the controller's sticks and buttons,
 * the Toggles' knobs, the Info dot. */
static void drawFaceCircle(const cubeRasterTransform_t *raster, int face,
		float x, float y, float radius, int segments, float plane, GXColor color)
{
	indigoPoint_t corners[FACE_POLYGON_MAX];

	if(segments < 3 || segments > FACE_POLYGON_MAX) return;
	const indigoPoint_t *unit = faceCircleSamples(segments);
	for(int i = 0; i < segments; i++) {
		if(unit) {
			corners[i] = (indigoPoint_t) {x + radius * unit[i].x, y + radius * unit[i].y};
		}
		else {
			float angle = -INDIGO_TAU * (float)i / (float)segments;
			corners[i] = (indigoPoint_t) {x + radius * cosf(angle), y + radius * sinf(angle)};
		}
	}
	drawFacePolygon(raster, face, corners, segments, plane, color);
}

/* A ring of segments sides with a vertex on each axis: 8 for the stick
 * gates, more for the round rims of the Disc and Gear icons. */
static void drawFaceRing(const cubeRasterTransform_t *raster, int face,
		float x, float y, float radius, int segments, float halfWidth,
		float plane, GXColor color)
{
	indigoPoint_t corners[FACE_BAND_MAX];

	if(segments < 3 || segments > FACE_BAND_MAX) return;
	const indigoPoint_t *unit = faceCircleSamples(segments);
	for(int i = 0; i < segments; i++) {
		if(unit) {
			corners[i] = (indigoPoint_t) {x + radius * unit[i].x, y + radius * unit[i].y};
		}
		else {
			float angle = -INDIGO_TAU * (float)i / (float)segments;
			corners[i] = (indigoPoint_t) {x + radius * cosf(angle), y + radius * sinf(angle)};
		}
	}
	drawFaceBand(raster, face, corners, segments, halfWidth, plane, color);
}

static void drawControllerRect(const cubeRasterTransform_t *raster, int face,
		float u0, float v0, float u1, float v1, float plane, GXColor color)
{
	const indigoPoint_t corners[4] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
	drawFacePolygon(raster, face, corners, 4, plane, color);
}

/* The controller's silhouette, clockwise in face units: the union of its
 * two main lobes, the bridge carrying Start, the D-pad and C-stick lobes and
 * the flared grips, traced and simplified by
 * buildtools/ui/controller_outline.py. */
static const float controllerOutline[][2] = {
		{-0.316f, 0.307f}, {-0.23f, 0.285f}, {-0.186f, 0.28f}, {0.144f, 0.279f}, {0.217f, 0.282f},
		{0.288f, 0.303f}, {0.352f, 0.307f}, {0.418f, 0.293f}, {0.468f, 0.266f}, {0.527f, 0.204f},
		{0.562f, 0.129f}, {0.57f, 0.022f}, {0.611f, -0.109f}, {0.667f, -0.324f}, {0.664f, -0.36f},
		{0.645f, -0.399f}, {0.615f, -0.424f}, {0.571f, -0.438f}, {0.539f, -0.434f}, {0.502f, -0.415f},
		{0.473f, -0.38f}, {0.376f, -0.211f}, {0.352f, -0.18f}, {0.324f, -0.158f}, {0.305f, -0.159f},
		{0.295f, -0.17f}, {0.284f, -0.232f}, {0.255f, -0.283f}, {0.215f, -0.308f}, {0.16f, -0.317f},
		{0.118f, -0.308f}, {0.077f, -0.282f}, {0.046f, -0.234f}, {0.035f, -0.164f}, {0.021f, -0.153f},
		{-0.005f, -0.15f}, {-0.022f, -0.153f}, {-0.036f, -0.164f}, {-0.044f, -0.228f}, {-0.073f, -0.277f},
		{-0.112f, -0.305f}, {-0.161f, -0.317f}, {-0.215f, -0.308f}, {-0.256f, -0.283f}, {-0.285f, -0.232f},
		{-0.295f, -0.17f}, {-0.306f, -0.159f}, {-0.325f, -0.158f}, {-0.353f, -0.18f}, {-0.376f, -0.211f},
		{-0.471f, -0.378f}, {-0.497f, -0.411f}, {-0.527f, -0.43f}, {-0.562f, -0.438f}, {-0.595f, -0.434f},
		{-0.628f, -0.417f}, {-0.659f, -0.38f}, {-0.667f, -0.329f}, {-0.632f, -0.182f}, {-0.571f, 0.023f},
		{-0.566f, 0.109f}, {-0.55f, 0.165f}, {-0.51f, 0.23f}, {-0.454f, 0.276f}, {-0.387f, 0.302f}
};

/* An arc: a band of face-space width along a circular arc with round ends,
 * swept clockwise from a0 down to a1 (radians) in ARC straight steps. */
static void drawFaceArc(const cubeRasterTransform_t *raster, int face,
		float cx, float cy, float radius, float a0, float a1, float halfWidth,
		int ARC, float plane, GXColor color)
{
	enum { CAP = 5 };
	const int POINTS = 2 * ARC + 2 * CAP;
	indigoPoint_t outline[2 * FACE_ARC_MAX + 2 * CAP];
	guVector eyes[2 * FACE_ARC_MAX + 2 * CAP];
	indigoPoint_t points[2 * FACE_ARC_MAX + 2 * CAP], joins[2 * FACE_ARC_MAX + 2 * CAP];
	float area = 0.0f;
	int count = 0;

	color.a = (u8)((float)color.a * raster->motifAlpha[face]);
	if(color.a == 0 || ARC < 1 || ARC > FACE_ARC_MAX) return;
	/* Outer arc, the a1 end cap, the inner arc back, then the a0 end cap. */
	for(int i = 0; i <= ARC; i++) {
		float angle = a0 + (a1 - a0) * (float)i / ARC;
		outline[count++] = (indigoPoint_t) {cx + (radius + halfWidth) * cosf(angle),
			cy + (radius + halfWidth) * sinf(angle)};
	}
	for(int j = 1; j < CAP; j++) {
		float angle = a1 - INDIGO_TAU * 0.5f * (float)j / CAP;
		outline[count++] = (indigoPoint_t) {cx + radius * cosf(a1) + halfWidth * cosf(angle),
			cy + radius * sinf(a1) + halfWidth * sinf(angle)};
	}
	for(int i = ARC; i >= 0; i--) {
		float angle = a0 + (a1 - a0) * (float)i / ARC;
		outline[count++] = (indigoPoint_t) {cx + (radius - halfWidth) * cosf(angle),
			cy + (radius - halfWidth) * sinf(angle)};
	}
	for(int j = 1; j < CAP; j++) {
		float angle = a0 + INDIGO_TAU * 0.5f * (1.0f - (float)j / CAP);
		outline[count++] = (indigoPoint_t) {cx + radius * cosf(a0) + halfWidth * cosf(angle),
			cy + radius * sinf(a0) + halfWidth * sinf(angle)};
	}
	for(int i = 0; i < POINTS; i++) {
		guVector point = semanticFacePoint(raster, face, outline[i].x, outline[i].y, plane);
		if(!projectRailPoint(raster, point.x, point.y, point.z,
			&eyes[i], &points[i])) return;
	}
	for(int i = 0; i < POINTS; i++) {
		int next = (i + 1) % POINTS;
		area += points[i].x * points[next].y - points[next].x * points[i].y;
	}
	if(area >= -0.001f) return;
	for(int i = 0; i < POINTS; i++) {
		if(!railJoin(points[(i + POINTS - 1) % POINTS], points[i],
			points[(i + 1) % POINTS], &joins[i])) return;
	}
	GXColor transparent = color;
	transparent.a = 0;
	/* The arc as a strip of quads: outer i, i+1 against the inner arc. */
	GX_Begin(GX_QUADS, GX_VTXFMT0, ARC * 4);
	for(int i = 0; i < ARC; i++) {
		putProjectedRailVertex(raster, eyes[i], joins[i], 0.0f, color);
		putProjectedRailVertex(raster, eyes[i + 1], joins[i + 1], 0.0f, color);
		putProjectedRailVertex(raster, eyes[2 * ARC + CAP - i - 1],
			joins[2 * ARC + CAP - i - 1], 0.0f, color);
		putProjectedRailVertex(raster, eyes[2 * ARC + CAP - i],
			joins[2 * ARC + CAP - i], 0.0f, color);
	}
	GX_End();
	/* Each round end fans from the strip's corner there, round the cap to
	 * its other corner, so the fan and the strip share their whole edge. A
	 * fan from the cap's centre put a vertex in the middle of that edge, a
	 * T-junction the console's 1/16-pixel vertex snap opened into sparkles. */
	for(int end = 0; end < 2; end++) {
		int first = end == 0 ? ARC : 2 * ARC + CAP;
		GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, CAP + 1);
		for(int j = 0; j <= CAP; j++) {
			int index = (first + j) % POINTS;
			putProjectedRailVertex(raster, eyes[index], joins[index], 0.0f, color);
		}
		GX_End();
	}
	GX_Begin(GX_QUADS, GX_VTXFMT0, POINTS * 4);
	for(int i = 0; i < POINTS; i++) {
		int next = (i + 1) % POINTS;
		putProjectedRailVertex(raster, eyes[i], joins[i], 0.0f, color);
		putProjectedRailVertex(raster, eyes[i], joins[i], 1.0f, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], 1.0f, transparent);
		putProjectedRailVertex(raster, eyes[next], joins[next], 0.0f, color);
	}
	GX_End();
}

/* A bean: a short arc in ten steps (X, Y, the triggers, the Disc's glints). */
static void drawFaceBean(const cubeRasterTransform_t *raster, int face,
		float cx, float cy, float radius, float a0, float a1, float halfWidth,
		float plane, GXColor color)
{
	drawFaceArc(raster, face, cx, cy, radius, a0, a1, halfWidth, 10, plane, color);
}

/* Normalised stick axis with a small dead zone, so a resting stick's drift
 * never wiggles the icon. */
static float controllerAxis(s8 value, float fullScale)
{
	float axis = (float)value / fullScale;

	if(fabsf(axis) < 0.1f) return 0.0f;
	return axis > 1.0f ? 1.0f : (axis < -1.0f ? -1.0f : axis);
}

/* Live input wins. After CONTROLLER_IDLE_HOLD seconds without any, the sticks
 * drift and the buttons play a slow press sequence; reduced motion keeps the
 * live mirror and drops that idle play. */
static void controllerPose(const indigoPadFrame_t *pad, float seconds,
		bool animated, controllerIdle_t *idle, controllerPose_t *pose)
{
	static const struct {
		u32 button;
		float start;
		float length;
	} presses[] = {
		{PAD_BUTTON_A, 0.30f, 0.26f}, {PAD_BUTTON_B, 1.20f, 0.22f},
		{PAD_BUTTON_Y, 2.20f, 0.22f}, {PAD_BUTTON_X, 2.80f, 0.22f},
		{PAD_BUTTON_RIGHT, 3.80f, 0.20f}, {PAD_BUTTON_DOWN, 4.20f, 0.20f},
		{PAD_BUTTON_START, 5.10f, 0.22f}
	};
	const u32 shown = PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X |
		PAD_BUTTON_Y | PAD_BUTTON_START | PAD_BUTTON_UP | PAD_BUTTON_DOWN |
		PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_TRIGGER_L | PAD_TRIGGER_R;
	float idleWeight;
	float since;
	float step;

	*pose = (controllerPose_t) {0.0f, 0.0f, 0.0f, 0.0f, 0u};
	if(pad != NULL && pad->available) {
		pose->stickX = controllerAxis(pad->stickX, 80.0f);
		pose->stickY = controllerAxis(pad->stickY, 80.0f);
		pose->substickX = controllerAxis(pad->substickX, 72.0f);
		pose->substickY = controllerAxis(pad->substickY, 72.0f);
		pose->pressed = pad->buttons & shown;
		if(pose->pressed != 0u || pose->stickX != 0.0f ||
			pose->stickY != 0.0f || pose->substickX != 0.0f ||
			pose->substickY != 0.0f) {
			idle->lastLiveInput = seconds;
			idle->liveSeen = true;
		}
	}
	/* An input from before the clock wrapped was still that long ago. */
	since = seconds - idle->lastLiveInput;
	if(since < 0.0f) since += UI_ANIM_TIME_WRAP_SECONDS;
	idleWeight = !animated ? 0.0f : (!idle->liveSeen ? 1.0f :
		(since - CONTROLLER_IDLE_HOLD) / 0.8f);
	if(idleWeight < 0.0f) idleWeight = 0.0f;
	if(idleWeight > 1.0f) idleWeight = 1.0f;
	/* Idle play comes back on its own 0.8 s ramp; live input takes the
	 * sticks from it over a moment, so they do not jump to the hand. */
	step = seconds - idle->lastSeconds;
	if(step < 0.0f) step += UI_ANIM_TIME_WRAP_SECONDS;
	idle->lastSeconds = seconds;
	if(animated && idleWeight < idle->shown) {
		idleWeight = fmaxf(idleWeight,
			idle->shown - step / CONTROLLER_IDLE_RELEASE);
	}
	idle->shown = idleWeight;
	if(idleWeight <= 0.0f) return;
	pose->stickX += idleWeight * 0.38f * sinf(seconds * 0.8f);
	pose->stickY += idleWeight * 0.32f * sinf(seconds * 1.1f + 0.6f);
	pose->substickX += idleWeight * 0.34f * sinf(seconds * 1.3f + 2.0f);
	pose->substickY += idleWeight * 0.30f * cosf(seconds * 0.9f);
	if(idleWeight < 1.0f) return;
	float phase = fmodf(seconds, CONTROLLER_PRESS_CYCLE);
	for(unsigned i = 0; i < sizeof(presses) / sizeof(presses[0]); i++) {
		if(phase >= presses[i].start &&
			phase < presses[i].start + presses[i].length) {
			pose->pressed |= presses[i].button;
		}
	}
}

/* Every part shares its face's glow; a pressed part sinks to 84% of its
 * size and brightens, the way a lit button reads on the other icons. */
static GXColor controllerGlow(GXColor color, float weight, bool pressed)
{
	float alpha = (float)color.a * weight * (pressed ? 1.3f : 1.0f);

	color.a = (u8)(alpha > 255.0f ? 255.0f : alpha);
	return color;
}

/* When the Library emblem last saw the pad in use: kept while its face is
 * turned away and while a page covers the cube, so idle play keeps time. */
static controllerIdle_t controllerIdle;

static void drawControllerIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, float seconds, bool animated, const indigoPadFrame_t *pad)
{
	const float plane = FACE_ICON_PLANE;
	controllerPose_t pose;

	controllerPose(pad, seconds, animated, &controllerIdle, &pose);
	if(raster->motifAlpha[face] <= 0.0f) return;
	bool l = (pose.pressed & PAD_TRIGGER_L) != 0u;
	bool r = (pose.pressed & PAD_TRIGGER_R) != 0u;
	bool a = (pose.pressed & PAD_BUTTON_A) != 0u;
	bool b = (pose.pressed & PAD_BUTTON_B) != 0u;
	bool x = (pose.pressed & PAD_BUTTON_X) != 0u;
	bool y = (pose.pressed & PAD_BUTTON_Y) != 0u;
	bool start = (pose.pressed & PAD_BUTTON_START) != 0u;
	float magnitude = fastSqrt(pose.stickX * pose.stickX + pose.stickY * pose.stickY);
	float cMagnitude = fastSqrt(pose.substickX * pose.substickX +
		pose.substickY * pose.substickY);
	/* A cap travels until a small gap remains inside its gate's inner edge. */
	float reach = (magnitude > 1.0f ? 1.0f / magnitude : 1.0f) * 0.030f;
	float cReach = (cMagnitude > 1.0f ? 1.0f / cMagnitude : 1.0f) * 0.016f;
	const float degree = INDIGO_TAU / 360.0f;
	indigoPoint_t outline[FACE_BAND_MAX];
	int outlineCount = (int)(sizeof(controllerOutline) / sizeof(controllerOutline[0]));

	for(int i = 0; i < outlineCount; i++) {
		outline[i] = (indigoPoint_t) {controllerOutline[i][0], controllerOutline[i][1]};
	}
	/* Nothing overlaps: the pass is additive. */
	drawFaceBand(raster, face, outline, outlineCount, 0.018f,
		plane, controllerGlow(glow, 0.78f, false));
	/* The triggers ride the shoulders and press in toward the body. */
	drawFaceBean(raster, face, -0.335f, 0.075f, l ? 0.28f : 0.30f,
		152.0f * degree, 100.0f * degree, 0.022f, plane, controllerGlow(glow, 0.9f, l));
	drawFaceBean(raster, face, 0.335f, 0.075f, r ? 0.28f : 0.30f,
		80.0f * degree, 28.0f * degree, 0.022f, plane, controllerGlow(glow, 0.9f, r));

	/* Both sticks follow the live controller inside their octagonal gates. */
	drawFaceRing(raster, face, -0.335f, 0.075f, 0.12f, 8, 0.014f, plane,
		controllerGlow(glow, 0.9f, false));
	drawFaceCircle(raster, face, -0.335f + pose.stickX * reach,
		0.075f + pose.stickY * reach, 0.058f, 20, plane, controllerGlow(glow, 1.0f, false));
	drawFaceRing(raster, face, 0.165f, -0.19f, 0.054f, 8, 0.012f, plane,
		controllerGlow(glow, 0.9f, false));
	drawFaceCircle(raster, face, 0.165f + pose.substickX * cReach,
		-0.19f + pose.substickY * cReach, 0.024f, 14, plane,
		controllerGlow(glow, 1.0f, false));

	/* The face cluster: a large A, B below-left, X and Y curving round A. */
	drawFaceCircle(raster, face, 0.335f, 0.075f, a ? 0.066f : 0.078f, 24, plane,
		controllerGlow(glow, 1.05f, a));
	drawFaceCircle(raster, face, 0.215f, -0.025f, b ? 0.030f : 0.036f, 16, plane,
		controllerGlow(glow, 1.0f, b));
	drawFaceBean(raster, face, 0.335f, 0.075f, 0.128f,
		42.0f * degree, -34.0f * degree, x ? 0.019f : 0.024f, plane,
		controllerGlow(glow, 1.0f, x));
	drawFaceBean(raster, face, 0.335f, 0.075f, 0.128f,
		172.0f * degree, 102.0f * degree, y ? 0.019f : 0.024f, plane,
		controllerGlow(glow, 1.0f, y));
	drawFaceCircle(raster, face, 0.0f, 0.10f, start ? 0.018f : 0.022f, 12,
		plane, controllerGlow(glow, 0.9f, start));

	/* Four separate D-pad arms around an open centre; a held arm brightens. */
	drawControllerRect(raster, face, -0.182f, -0.167f, -0.148f, -0.117f, plane,
		controllerGlow(glow, 1.0f, (pose.pressed & PAD_BUTTON_UP) != 0u));
	drawControllerRect(raster, face, -0.182f, -0.263f, -0.148f, -0.213f, plane,
		controllerGlow(glow, 1.0f, (pose.pressed & PAD_BUTTON_DOWN) != 0u));
	drawControllerRect(raster, face, -0.238f, -0.207f, -0.188f, -0.173f, plane,
		controllerGlow(glow, 1.0f, (pose.pressed & PAD_BUTTON_LEFT) != 0u));
	drawControllerRect(raster, face, -0.142f, -0.207f, -0.092f, -0.173f, plane,
		controllerGlow(glow, 1.0f, (pose.pressed & PAD_BUTTON_RIGHT) != 0u));
}

static void buildChamferStrip(cubeSurfaceQuad_t *quad, float ax, float ay, float az,
		float bx, float by, float bz, float cx, float cy, float cz,
		float dx, float dy, float dz, GXColor outerColor, GXColor innerColor)
{
	*quad = (cubeSurfaceQuad_t) {
		{{ax,ay,az}, {bx,by,bz}, {cx,cy,cz}, {dx,dy,dz}},
		{outerColor, outerColor, innerColor, innerColor}
	};
	/* GX front faces wind clockwise when seen from outside the cube. Some
	 * legacy two-sided depth strips used the reverse winding. Normalize it
	 * without moving a corner, its color, or the quad's triangulation seam. */
	guVector a = {bx - ax, by - ay, bz - az};
	guVector b = {cx - ax, cy - ay, cz - az};
	guVector normal = {a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	if(normal.x * ax + normal.y * ay + normal.z * az > 0.0f) {
		guVector point = quad->point[1];
		GXColor color = quad->color[1];
		quad->point[1] = quad->point[3]; quad->color[1] = quad->color[3];
		quad->point[3] = point; quad->color[3] = color;
	}
}

/* How far across a bevel, from each seam, its own tint takes over from the
 * face beside it: the tuning knob between a soft seam and a crisp bevel. */
#define BEVEL_SEAM_BLEND 0.15f

/* The point BEVEL_SEAM_BLEND of the way across a bevel from a corner on one
 * seam toward its partner on the other. Bevels and corner fans both cut from
 * the nearer seam, so the points they share are bit-identical. */
static guVector bevelCut(guVector seam, guVector across)
{
	return (guVector) {seam.x + (across.x - seam.x) * BEVEL_SEAM_BLEND,
		seam.y + (across.y - seam.y) * BEVEL_SEAM_BLEND,
		seam.z + (across.z - seam.z) * BEVEL_SEAM_BLEND};
}

/* Bevel strip `strip`, from its seam with faceA (a, b) to its seam with
 * faceB (c, d). With no face tints it is the bare strip the refraction and
 * the reflection shade. With them it is cut in three along its length: the
 * seam rows take the faces' own tints for the pass, so the glass carries on
 * across every seam in one colour and leaves the frame buffer no step to
 * stair, and the bevel's edge tints take over BEVEL_SEAM_BLEND of its width
 * in. */
static void buildChamferBands(cubeSurfaceQuad_t *quads, int strip,
		float ax, float ay, float az, float bx, float by, float bz,
		float cx, float cy, float cz, float dx, float dy, float dz,
		int faceA, int faceB, const GXColor edge[6], const GXColor *face)
{
	const guVector a = {ax, ay, az}, b = {bx, by, bz}, c = {cx, cy, cz}, d = {dx, dy, dz};
	const guVector a1 = bevelCut(a, d), b1 = bevelCut(b, c);
	const guVector c1 = bevelCut(c, b), d1 = bevelCut(d, a);

	if(face == NULL) {
		buildChamferStrip(&quads[strip], ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz,
			edge[faceA], edge[faceB]);
		return;
	}
	quads += strip * 3;
	buildChamferStrip(&quads[0], ax, ay, az, bx, by, bz, b1.x, b1.y, b1.z,
		a1.x, a1.y, a1.z, face[faceA], edge[faceA]);
	buildChamferStrip(&quads[1], a1.x, a1.y, a1.z, b1.x, b1.y, b1.z, c1.x, c1.y, c1.z,
		d1.x, d1.y, d1.z, edge[faceA], edge[faceB]);
	buildChamferStrip(&quads[2], d1.x, d1.y, d1.z, c1.x, c1.y, c1.z, cx, cy, cz,
		dx, dy, dz, edge[faceB], face[faceB]);
}

/* The twelve bevels: bare (12 quads) when face is NULL, else banded in the
 * pass's face tints (36 quads). */
static void buildChamferStrips(cubeSurfaceQuad_t *quads, float outer, float inset,
		const GXColor edge[6], const GXColor *face)
{
	const float o = outer;
	const float i = inset;
	enum { BACK, LEFT, RIGHT, BOTTOM, TOP, FRONT };

	/* Each bevel runs from the lit tint of one neighbouring face to the other.
	 * Its place before or after the glass is determined by camera-facing
	 * culling, so a vertical turn cannot leave an old front strip on top of
	 * the pane. */
	buildChamferBands(quads, 0, -i,i,-o, i,i,-o, i,o,-i, -i,o,-i, BACK, TOP, edge, face);
	buildChamferBands(quads, 1, -i,-o,-i, i,-o,-i, i,-i,-o, -i,-i,-o, BOTTOM, BACK, edge, face);
	buildChamferBands(quads, 2, -i,-i,-o, -i,i,-o, -o,i,-i, -o,-i,-i, BACK, LEFT, edge, face);
	buildChamferBands(quads, 3, o,-i,-i, o,i,-i, i,i,-o, i,-i,-o, RIGHT, BACK, edge, face);

	buildChamferBands(quads, 4, -o,i,-i, -o,i,i, -i,o,i, -i,o,-i, LEFT, TOP, edge, face);
	buildChamferBands(quads, 5, i,o,-i, i,o,i, o,i,i, o,i,-i, TOP, RIGHT, edge, face);
	buildChamferBands(quads, 6, -i,-o,-i, -i,-o,i, -o,-i,i, -o,-i,-i, BOTTOM, LEFT, edge, face);
	buildChamferBands(quads, 7, o,-i,-i, o,-i,i, i,-o,i, i,-o,-i, RIGHT, BOTTOM, edge, face);
	buildChamferBands(quads, 8, -i,o,i, i,o,i, i,i,o, -i,i,o, TOP, FRONT, edge, face);
	buildChamferBands(quads, 9, -i,-i,o, i,-i,o, i,-o,i, -i,-o,i, FRONT, BOTTOM, edge, face);
	buildChamferBands(quads, 10, -o,-i,i, -o,i,i, -i,i,o, -i,-i,o, LEFT, FRONT, edge, face);
	buildChamferBands(quads, 11, i,-i,o, i,i,o, o,i,i, o,-i,i, FRONT, RIGHT, edge, face);
}

static void buildCubeCorners(cubeSurfaceQuad_t corners[8], float outer, float inset)
{
	/* The inset faces and twelve edge bevels leave eight triangular holes.
	 * Seal each with the same existing endpoints. The refraction and the
	 * reflection shade these; the tint passes draw buildCornerFans. */
	for(int corner = 0; corner < 8; corner++) {
		float x = (corner & 1) ? 1.0f : -1.0f;
		float y = (corner & 2) ? 1.0f : -1.0f;
		float z = (corner & 4) ? 1.0f : -1.0f;
		corners[corner] = (cubeSurfaceQuad_t) {
			{{x * outer, y * inset, z * inset},
			 {x * inset, y * outer, z * inset},
			 {x * inset, y * inset, z * outer}, {0.0f, 0.0f, 0.0f}},
			{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}
		};
		/* A sign reflection reverses orientation. All exterior faces must
		 * share GX's clockwise winding for the far/near culling passes. */
		if(x * y * z > 0.0f) {
			guVector point = corners[corner].point[1];
			corners[corner].point[1] = corners[corner].point[2];
			corners[corner].point[2] = point;
		}
	}
}

/* The corners as the tint passes draw them: buildCubeCorners' triangles,
 * each fanned from its middle into nine, so the rim meets the three bevels'
 * ends point for point and tint for tint (face, edge, edge, face along each
 * side) and no seam shows. The middle takes the mean of the edge tints. */
static void buildCornerFans(cubeSurfaceQuad_t fans[72], float outer, float inset,
		const GXColor edge[6], const GXColor face[6])
{
	for(int corner = 0; corner < 8; corner++) {
		float x = (corner & 1) ? 1.0f : -1.0f;
		float y = (corner & 2) ? 1.0f : -1.0f;
		float z = (corner & 4) ? 1.0f : -1.0f;
		/* The three points and the face each lies on (cubeFaceAxes order). */
		guVector p[3] = {{x * outer, y * inset, z * inset},
			{x * inset, y * outer, z * inset}, {x * inset, y * inset, z * outer}};
		int on[3] = {x < 0.0f ? 1 : 2, y < 0.0f ? 3 : 4, z < 0.0f ? 0 : 5};
		guVector rim[9], middle;
		GXColor tint[9], hub;

		if(x * y * z > 0.0f) {
			guVector point = p[1];
			int side = on[1];
			p[1] = p[2]; on[1] = on[2];
			p[2] = point; on[2] = side;
		}
		middle = (guVector) {(p[0].x + p[1].x + p[2].x) / 3.0f,
			(p[0].y + p[1].y + p[2].y) / 3.0f, (p[0].z + p[1].z + p[2].z) / 3.0f};
		hub = (GXColor) {
			(u8)((edge[on[0]].r + edge[on[1]].r + edge[on[2]].r) / 3),
			(u8)((edge[on[0]].g + edge[on[1]].g + edge[on[2]].g) / 3),
			(u8)((edge[on[0]].b + edge[on[1]].b + edge[on[2]].b) / 3),
			(u8)((edge[on[0]].a + edge[on[1]].a + edge[on[2]].a) / 3)
		};
		for(int k = 0; k < 3; k++) {
			int next = (k + 1) % 3;
			rim[k * 3] = p[k];
			tint[k * 3] = face[on[k]];
			rim[k * 3 + 1] = bevelCut(p[k], p[next]);
			tint[k * 3 + 1] = edge[on[k]];
			rim[k * 3 + 2] = bevelCut(p[next], p[k]);
			tint[k * 3 + 2] = edge[on[next]];
		}
		/* Each triangle keeps the corner's clockwise winding. */
		for(int k = 0; k < 9; k++) {
			fans[corner * 9 + k] = (cubeSurfaceQuad_t) {
				{middle, rim[k], rim[(k + 1) % 9], middle},
				{hub, tint[k], tint[(k + 1) % 9], hub}
			};
		}
	}
}

/* A central port with four linked endpoints: 9 quads, 180 vertices. */
static void drawHubIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 180);
		putSemanticFaceDiamond(raster, face, 0.0f, 0.0f, 0.17f, plane, glow);
		putSemanticFaceDiamond(raster, face, -0.48f, 0.0f, 0.09f, plane, glow);
		putSemanticFaceDiamond(raster, face, 0.48f, 0.0f, 0.09f, plane, glow);
		putSemanticFaceDiamond(raster, face, 0.0f, -0.48f, 0.09f, plane, glow);
		putSemanticFaceDiamond(raster, face, 0.0f, 0.48f, 0.09f, plane, glow);
		putSemanticMotifRect(raster, face, -0.40f, -0.025f, -0.16f, 0.025f, plane, glow);
		putSemanticMotifRect(raster, face, 0.16f, -0.025f, 0.40f, 0.025f, plane, glow);
		putSemanticMotifRect(raster, face, -0.025f, -0.40f, 0.025f, -0.16f, plane, glow);
		putSemanticMotifRect(raster, face, -0.025f, 0.16f, 0.025f, 0.40f, plane, glow);
	GX_End();
}

/* Three calm tracks with independently placed controls: 6 quads, 120
 * vertices. slider drifts the knobs while motion is on. */
static void drawSlidersIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, float slider)
{
	const float plane = FACE_ICON_PLANE;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 120);
	for(int track = 0; track < 3; ++track) {
		float y = -0.42f + (float)track * 0.42f;
		float knob = (track == 0 ? -0.25f : (track == 1 ? 0.12f : 0.34f));
		knob += (track == 1 ? slider : -slider * 0.45f);
		putSemanticMotifRect(raster, face, -0.55f, y - 0.025f,
			0.55f, y + 0.025f, plane, glow);
		putSemanticMotifRect(raster, face, knob - 0.065f,
			y - 0.13f, knob + 0.065f, y + 0.13f, plane, glow);
	}
	GX_End();
}

/* A framed live clock with three civil-time hands and cardinal ticks; the
 * hand tails form a compact luminous hub. 11 quads, 220 vertices. Invalid
 * civil time emits transparent degenerate hands, never an invented time. */
static void drawClockIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, const uiClockFrame_t *clock)
{
	const float plane = FACE_ICON_PLANE;
	GXColor secondHand = {glow.r, glow.g, glow.b, 228};
	GXColor hiddenHand = {0, 0, 0, 0};
	bool clockAvailable = clock != NULL && clock->available;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 220);
		putSemanticFaceHand(raster, face,
			clockAvailable ? clock->hourX : 0.0f,
			clockAvailable ? clock->hourY : 0.0f,
			clockAvailable ? 0.34f : 0.0f,
			clockAvailable ? 0.040f : 0.0f,
			clockAvailable ? 0.045f : 0.0f, plane,
			clockAvailable ? glow : hiddenHand);
		putSemanticFaceHand(raster, face,
			clockAvailable ? clock->minuteX : 0.0f,
			clockAvailable ? clock->minuteY : 0.0f,
			clockAvailable ? 0.49f : 0.0f,
			clockAvailable ? 0.027f : 0.0f,
			clockAvailable ? 0.055f : 0.0f, plane,
			clockAvailable ? glow : hiddenHand);
		putSemanticFaceHand(raster, face,
			clockAvailable ? clock->secondX : 0.0f,
			clockAvailable ? clock->secondY : 0.0f,
			clockAvailable ? 0.56f : 0.0f,
			clockAvailable ? 0.013f : 0.0f,
			clockAvailable ? 0.090f : 0.0f, plane,
			clockAvailable ? secondHand : hiddenHand);
		putSemanticMotifRect(raster, face, -0.55f, 0.55f, 0.55f, 0.61f, plane, glow);
		putSemanticMotifRect(raster, face, -0.55f, -0.61f, 0.55f, -0.55f, plane, glow);
		putSemanticMotifRect(raster, face, -0.61f, -0.55f, -0.55f, 0.55f, plane, glow);
		putSemanticMotifRect(raster, face, 0.55f, -0.55f, 0.61f, 0.55f, plane, glow);
		putSemanticMotifRect(raster, face, -0.035f, 0.55f, 0.035f, 0.66f, plane, glow);
		putSemanticMotifRect(raster, face, -0.035f, -0.66f, 0.035f, -0.55f, plane, glow);
		putSemanticMotifRect(raster, face, -0.66f, -0.035f, -0.55f, 0.035f, plane, glow);
		putSemanticMotifRect(raster, face, 0.55f, -0.035f, 0.66f, 0.035f, plane, glow);
	GX_End();
}

/* Banded books on a shelf, the last leaning into the row so it reads as a
 * bookshelf, not a bar chart: 13 quads, 260 vertices. The lean clears the
 * third spine; additive blending would double-light any overlap. */
static void drawBooksIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 260);
		putSemanticFaceBook(raster, face, -0.585f, -0.46f, 0.20f, 0.90f, 0.0f, plane, glow);
		putSemanticFaceBook(raster, face, -0.35f, -0.46f, 0.25f, 1.00f, 0.0f, plane, glow);
		putSemanticFaceBook(raster, face, -0.065f, -0.46f, 0.17f, 0.80f, 0.0f, plane, glow);
		putSemanticFaceBook(raster, face, 0.412f, -0.46f, 0.19f, 0.84f, 0.349f, plane, glow);
		putSemanticMotifRect(raster, face, -0.64f, -0.555f, 0.64f, -0.49f, plane, glow);
	GX_End();
}

/* A GameCube disc: its rim, the ring round the centre hole and two glints
 * between them, which spin turns while motion is on. */
static void drawDiscIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, float spin)
{
	const float plane = FACE_ICON_PLANE;
	const float degree = INDIGO_TAU / 360.0f;

	drawFaceRing(raster, face, 0.0f, 0.0f, 0.52f, 32, 0.020f, plane, glow);
	drawFaceRing(raster, face, 0.0f, 0.0f, 0.14f, 16, 0.018f, plane, glow);
	for(int glint = 0; glint < 2; glint++) {
		float from = spin + 125.0f * degree + (float)glint * INDIGO_TAU * 0.5f;
		drawFaceBean(raster, face, 0.0f, 0.0f, 0.33f, from, from - 50.0f * degree,
			0.028f, plane, glow);
	}
}

/* A gear: six rounded teeth and its centre hole, turned by turn. tanh
 * squares each tooth off; at this sharpness every corner of the 78-point
 * outline stays under the 45 degrees drawFaceBand needs. */
static void drawGearIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, float turn)
{
	enum { TEETH = 6, POINTS = TEETH * 13 };
	const float plane = FACE_ICON_PLANE;
	indigoPoint_t outline[POINTS];

	for(int i = 0; i < POINTS; i++) {
		float angle = -INDIGO_TAU * (float)i / (float)POINTS;
		float radius = 0.45f + 0.085f * tanhf(2.0f * sinf(TEETH * angle)) / tanhf(2.0f);
		outline[i] = (indigoPoint_t) {radius * cosf(angle + turn),
			radius * sinf(angle + turn)};
	}
	drawFaceBand(raster, face, outline, POINTS, 0.018f, plane, glow);
	drawFaceRing(raster, face, 0.0f, 0.0f, 0.15f, 16, 0.018f, plane, glow);
}

/* A closed outline through corners (clockwise, v up) with every corner
 * rounded off from radius before it, in steps of at most 20 degrees, so no
 * point turns the 45 degrees drawFaceBand needs to stay under. Returns the
 * point count, or 0 when it would not fit in max. */
static int roundedOutline(const indigoPoint_t *corners, int count, float radius,
		indigoPoint_t *out, int max)
{
	const float step = 20.0f * INDIGO_TAU / 360.0f;
	int n = 0;

	for(int i = 0; i < count; i++) {
		indigoPoint_t p = corners[(i + count - 1) % count], c = corners[i];
		indigoPoint_t q = corners[(i + 1) % count];
		float ax = c.x - p.x, ay = c.y - p.y, bx = q.x - c.x, by = q.y - c.y;
		float al = fastSqrt(ax * ax + ay * ay), bl = fastSqrt(bx * bx + by * by);
		if(al <= 0.0f || bl <= 0.0f) return 0;
		ax /= al; ay /= al; bx /= bl; by /= bl;
		float turn = acosf(fmaxf(-1.0f, fminf(1.0f, ax * bx + ay * by)));
		int steps = (int)ceilf(turn / step);
		if(steps < 1) {
			if(n == max) return 0;
			out[n++] = c;
			continue;
		}
		if(n + steps + 1 > max) return 0;
		/* A quadratic from one tangent point to the other, the corner its
		 * control point: close to the arc, and every step a small turn. */
		float cut = radius * tanf(turn * 0.5f);
		indigoPoint_t from = {c.x - ax * cut, c.y - ay * cut};
		indigoPoint_t to = {c.x + bx * cut, c.y + by * cut};
		for(int k = 0; k <= steps; k++) {
			float t = (float)k / (float)steps, u = 1.0f - t;
			indigoPoint_t point = {u * u * from.x + 2.0f * u * t * c.x + t * t * to.x,
				u * u * from.y + 2.0f * u * t * c.y + t * t * to.y};
			/* Arcs that meet (a pill's two rounded ends) share a point. */
			if(n > 0 && fabsf(point.x - out[n - 1].x) < 1e-5f &&
				fabsf(point.y - out[n - 1].y) < 1e-5f) continue;
			out[n++] = point;
		}
	}
	if(n > 1 && fabsf(out[n - 1].x - out[0].x) < 1e-5f &&
		fabsf(out[n - 1].y - out[0].y) < 1e-5f) n--;
	return n;
}

/* A band along roundedOutline(corners). */
static void drawRoundedBand(const cubeRasterTransform_t *raster, int face,
		const indigoPoint_t *corners, int count, float radius, float halfWidth,
		float plane, GXColor color)
{
	indigoPoint_t outline[FACE_BAND_MAX];
	int points = roundedOutline(corners, count, radius, outline, FACE_BAND_MAX);

	if(points >= 3) drawFaceBand(raster, face, outline, points, halfWidth, plane, color);
}

static void drawRoundedRect(const cubeRasterTransform_t *raster, int face,
		float u0, float v0, float u1, float v1, float radius, float halfWidth,
		float plane, GXColor color)
{
	const indigoPoint_t corners[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};

	drawRoundedBand(raster, face, corners, 4, radius, halfWidth, plane, color);
}

static void drawFaceBar(const cubeRasterTransform_t *raster, int face,
		float u0, float v0, float u1, float v1, float plane, GXColor color)
{
	const indigoPoint_t corners[4] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};

	drawFacePolygon(raster, face, corners, 4, plane, color);
}

/* A bar from radius r0 to r1 along angle, halfWidth to each side. */
static void drawFaceSpoke(const cubeRasterTransform_t *raster, int face,
		float angle, float r0, float r1, float halfWidth, float plane, GXColor color)
{
	float x = cosf(angle), y = sinf(angle), px = -y * halfWidth, py = x * halfWidth;
	const indigoPoint_t corners[4] = {
		{x * r0 - px, y * r0 - py}, {x * r0 + px, y * r0 + py},
		{x * r1 + px, y * r1 + py}, {x * r1 - px, y * r1 - py}
	};

	drawFacePolygon(raster, face, corners, 4, plane, color);
}

/* Library: three covers, the middle one raised and larger, its title below. */
static void drawCoversIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawRoundedRect(raster, face, -0.57f, -0.24f, -0.29f, 0.16f, 0.035f, 0.016f, plane, glow);
	drawRoundedRect(raster, face, -0.20f, -0.30f, 0.20f, 0.28f, 0.04f, 0.018f, plane, glow);
	drawRoundedRect(raster, face, 0.29f, -0.24f, 0.57f, 0.16f, 0.035f, 0.016f, plane, glow);
	drawFaceBar(raster, face, -0.15f, -0.43f, 0.15f, -0.38f, plane, glow);
}

/* Library: a play button in its ring. */
static void drawPlayIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;
	const indigoPoint_t triangle[3] = {{-0.12f, 0.24f}, {0.26f, 0.0f}, {-0.12f, -0.24f}};

	drawFaceRing(raster, face, 0.0f, 0.0f, 0.50f, 40, 0.022f, plane, glow);
	drawFacePolygon(raster, face, triangle, 3, plane, glow);
}

/* Source: an SD card, its corner cut and its contacts along the top. */
static void drawSdCardIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;
	const indigoPoint_t card[5] = {
		{-0.28f, 0.36f}, {0.12f, 0.36f}, {0.28f, 0.20f}, {0.28f, -0.36f}, {-0.28f, -0.36f}
	};

	drawRoundedBand(raster, face, card, 5, 0.04f, 0.018f, plane, glow);
	for(int contact = 0; contact < 4; contact++) {
		float u = -0.195f + (float)contact * 0.09f;
		drawFaceBar(raster, face, u, 0.12f, u + 0.05f, 0.27f, plane, glow);
	}
}

/* Source: a folder with its tab, and the edge of the front pocket. */
static void drawFolderIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;
	const indigoPoint_t folder[6] = {
		{-0.42f, 0.30f}, {-0.12f, 0.30f}, {-0.04f, 0.20f}, {0.42f, 0.20f},
		{0.42f, -0.30f}, {-0.42f, -0.30f}
	};

	drawRoundedBand(raster, face, folder, 6, 0.04f, 0.018f, plane, glow);
	drawFaceBar(raster, face, -0.33f, 0.08f, 0.33f, 0.12f, plane, glow);
}

/* Settings: two switches, one on and one off. */
static void drawTogglesIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawRoundedRect(raster, face, -0.36f, 0.08f, 0.36f, 0.36f, 0.14f, 0.018f, plane, glow);
	drawFaceCircle(raster, face, 0.22f, 0.22f, 0.085f, 24, plane, glow);
	drawRoundedRect(raster, face, -0.36f, -0.36f, 0.36f, -0.08f, 0.14f, 0.018f, plane, glow);
	drawFaceCircle(raster, face, -0.22f, -0.22f, 0.085f, 24, plane, glow);
}

/* Settings: a knob with its pointer, turned slowly by turn, over a scale of
 * seven ticks. */
static void drawDialIcon(const cubeRasterTransform_t *raster, int face,
		GXColor glow, float turn)
{
	const float plane = FACE_ICON_PLANE;
	const float degree = INDIGO_TAU / 360.0f;

	drawFaceRing(raster, face, 0.0f, 0.0f, 0.33f, 40, 0.022f, plane, glow);
	drawFaceSpoke(raster, face, (120.0f + turn) * degree, 0.05f, 0.25f, 0.032f, plane, glow);
	for(int tick = 0; tick < 7; tick++) {
		drawFaceSpoke(raster, face, (225.0f - 45.0f * (float)tick) * degree,
			0.44f, 0.54f, 0.02f, plane, glow);
	}
}

/* System: information, an i in its ring. */
static void drawInfoIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawFaceRing(raster, face, 0.0f, 0.0f, 0.50f, 40, 0.022f, plane, glow);
	drawFaceCircle(raster, face, 0.0f, 0.22f, 0.055f, 16, plane, glow);
	drawFaceBar(raster, face, -0.045f, -0.28f, 0.045f, 0.08f, plane, glow);
}

/* System: the power symbol, a ring open at the top and a bar through it. */
static void drawPowerIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;
	const float degree = INDIGO_TAU / 360.0f;

	drawFaceArc(raster, face, 0.0f, -0.02f, 0.36f, 58.0f * degree, -238.0f * degree,
		0.035f, 24, plane, glow);
	drawFaceBar(raster, face, -0.035f, 0.04f, 0.035f, 0.44f, plane, glow);
}

/* System: a chip, its die, the pin-one mark and three pins a side. */
static void drawChipIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawRoundedRect(raster, face, -0.27f, -0.27f, 0.27f, 0.27f, 0.045f, 0.018f, plane, glow);
	drawFaceBar(raster, face, -0.11f, -0.11f, 0.11f, 0.11f, plane, glow);
	drawFaceCircle(raster, face, -0.175f, 0.175f, 0.028f, 12, plane, glow);
	for(int pin = 0; pin < 3; pin++) {
		float at = -0.13f + (float)pin * 0.13f;
		drawFaceBar(raster, face, at - 0.025f, 0.31f, at + 0.025f, 0.42f, plane, glow);
		drawFaceBar(raster, face, at - 0.025f, -0.42f, at + 0.025f, -0.31f, plane, glow);
		drawFaceBar(raster, face, -0.42f, at - 0.025f, -0.31f, at + 0.025f, plane, glow);
		drawFaceBar(raster, face, 0.31f, at - 0.025f, 0.42f, at + 0.025f, plane, glow);
	}
}

/* Apps: four app tiles, two by two. */
static void drawAppsIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	for(int tile = 0; tile < 4; tile++) {
		float u = (tile & 1) ? 0.10f : -0.40f;
		float v = (tile & 2) ? -0.40f : 0.10f;

		drawRoundedRect(raster, face, u, v, u + 0.30f, v + 0.30f, 0.07f, 0.018f,
			plane, glow);
	}
}

/* Memory Cards: a GameCube memory card, the edge it plugs in by along its
 * top and its label below. */
static void drawSavesIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawRoundedRect(raster, face, -0.26f, -0.38f, 0.26f, 0.38f, 0.05f, 0.018f,
		plane, glow);
	drawFaceBar(raster, face, -0.16f, 0.22f, 0.16f, 0.28f, plane, glow);
	drawRoundedRect(raster, face, -0.15f, -0.26f, 0.15f, 0.06f, 0.03f, 0.018f,
		plane, glow);
}

/* Emulators: a game pad, its cross on the left and two buttons on the right. */
static void drawEmulatorsIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawRoundedRect(raster, face, -0.42f, -0.22f, 0.42f, 0.22f, 0.12f, 0.018f,
		plane, glow);
	drawFaceBar(raster, face, -0.31f, -0.03f, -0.11f, 0.03f, plane, glow);
	drawFaceBar(raster, face, -0.24f, -0.10f, -0.18f, 0.10f, plane, glow);
	drawFaceCircle(raster, face, 0.14f, -0.05f, 0.045f, 12, plane, glow);
	drawFaceCircle(raster, face, 0.28f, 0.06f, 0.045f, 12, plane, glow);
}

/* Every face shows the icon chosen for it in Settings: choices[face] picks
 * one of that face's own four (uiHomeIcon_t face * UI_HOME_ICON_CHOICES +
 * choice). One additive pass without depth writes, every icon in the same
 * lilac. Each icon's primitive count is fixed, so a face turning away draws
 * transparent degenerates or nothing; a choice out of range draws nothing. */
/* How squarely a semantic face looks at the camera: the eye-space z of its
 * outward normal, 1 square on, 0 edge-on, below 0 turned away. */
static float faceFacing(const cubeRasterTransform_t *raster, int face)
{
	const float (*basis)[4] = raster->semanticFaces[face];
	const float (*m)[4] = raster->model;
	float x = m[0][0] * basis[0][2] + m[0][1] * basis[1][2] + m[0][2] * basis[2][2];
	float y = m[1][0] * basis[0][2] + m[1][1] * basis[1][2] + m[1][2] * basis[2][2];
	float z = m[2][0] * basis[0][2] + m[2][1] * basis[1][2] + m[2][2] * basis[2][2];
	float length = fastSqrt(x * x + y * y + z * z);

	return length > 0.0001f ? z / length : 0.0f;
}

/* A face turned nearly edge-on squeezes its icon's strokes below a pixel,
 * which the console's rasterizer breaks into fragments. So the strokes fade
 * out as a face turns away, gone while it points more than ~71 degrees from
 * the camera (Home's side faces rest 72 to 76 degrees away), whole inside
 * 53; the rest of the icon is its picture (drawFacePictures). */
static float faceStrokeShare(float facing)
{
	return UIMotion_Smoothstep((facing - 0.33f) / 0.27f);
}

/* How far a face lifts its icon off FACE_ICON_PLANE: all of FACE_ICON_LIFT
 * within ~18 degrees of the camera, none past ~37. Every scene's side faces
 * rest further round than that, where a raised icon would leave the cube's
 * outline. */
static float faceIconLift(float facing)
{
	return FACE_ICON_LIFT * UIMotion_Smoothstep((facing - 0.80f) / 0.15f);
}

/* Lifts a face's icon by lengthening its normal: every icon is drawn at
 * FACE_ICON_PLANE, so it lands at FACE_ICON_PLANE + lift. faceFacing
 * normalises the normal, so it reads the same afterwards. */
static void liftFaceIcon(cubeRasterTransform_t *raster, int face, float lift)
{
	float stretch = (FACE_ICON_PLANE + lift) / FACE_ICON_PLANE;

	for(int row = 0; row < 3; row++)
		raster->semanticFaces[face][row][2] *= stretch;
}

/* Web: a globe, its outline with a meridian and three latitudes. */
static void drawWebIcon(const cubeRasterTransform_t *raster, int face, GXColor glow)
{
	const float plane = FACE_ICON_PLANE;

	drawFaceRing(raster, face, 0.0f, 0.0f, 0.42f, 40, 0.022f, plane, glow);
	drawFaceBar(raster, face, -0.013f, -0.40f, 0.013f, 0.40f, plane, glow);
	drawFaceBar(raster, face, -0.40f, -0.013f, 0.40f, 0.013f, plane, glow);
	drawFaceBar(raster, face, -0.30f, 0.175f, 0.30f, 0.199f, plane, glow);
	drawFaceBar(raster, face, -0.30f, -0.199f, 0.30f, -0.175f, plane, glow);
}

/* One face's icon, choice one of that face's four. */
static void drawOneFaceIcon(const cubeRasterTransform_t *raster, int face,
		int choice, float seconds, bool animated, const uiClockFrame_t *clock,
		const indigoPadFrame_t *pad)
{
	float pulse = animated ? 0.5f + sinf(seconds * 1.10f) * 0.5f : 0.62f;
	float slider = animated ? sinf(seconds * 0.43f) * 0.12f : 0.0f;
	GXColor glow = {196, 177, 255, (u8)(142.0f + pulse * 42.0f)};

	if(choice < 0 || choice >= UI_HOME_ICON_CHOICES) return;
	switch(face * UI_HOME_ICON_CHOICES + choice) {
		case UI_HOME_ICON_CONTROLLER:
			drawControllerIcon(raster, face, glow, seconds, animated, pad);
			break;
		case UI_HOME_ICON_BOOKS: drawBooksIcon(raster, face, glow); break;
		case UI_HOME_ICON_COVERS: drawCoversIcon(raster, face, glow); break;
		case UI_HOME_ICON_PLAY: drawPlayIcon(raster, face, glow); break;
		case UI_HOME_ICON_HUB: drawHubIcon(raster, face, glow); break;
		case UI_HOME_ICON_DISC:
			drawDiscIcon(raster, face, glow, animated ? seconds * 0.6f : 0.0f);
			break;
		case UI_HOME_ICON_SD_CARD: drawSdCardIcon(raster, face, glow); break;
		case UI_HOME_ICON_FOLDER: drawFolderIcon(raster, face, glow); break;
		case UI_HOME_ICON_SLIDERS: drawSlidersIcon(raster, face, glow, slider); break;
		case UI_HOME_ICON_GEAR:
			drawGearIcon(raster, face, glow, animated ? seconds * 0.2f : 0.0f);
			break;
		case UI_HOME_ICON_TOGGLES: drawTogglesIcon(raster, face, glow); break;
		case UI_HOME_ICON_DIAL:
			drawDialIcon(raster, face, glow, animated ? sinf(seconds * 0.4f) * 25.0f : 0.0f);
			break;
		case UI_HOME_ICON_CLOCK: drawClockIcon(raster, face, glow, clock); break;
		case UI_HOME_ICON_INFO: drawInfoIcon(raster, face, glow); break;
		case UI_HOME_ICON_POWER: drawPowerIcon(raster, face, glow); break;
		case UI_HOME_ICON_CHIP: drawChipIcon(raster, face, glow); break;
		case UI_HOME_ICON_APPS: drawAppsIcon(raster, face, glow); break;
		case UI_HOME_ICON_SAVES: drawSavesIcon(raster, face, glow); break;
		case UI_HOME_ICON_EMULATORS: drawEmulatorsIcon(raster, face, glow); break;
		case UI_HOME_ICON_FILES: drawFolderIcon(raster, face, glow); break;
		case UI_HOME_ICON_WEB: drawWebIcon(raster, face, glow); break;
		default: break;
	}
}

static void drawFaceIcons(float seconds, bool animated,
		const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
		const int choices[UI_HOME_FACE_COUNT], const cubeRasterTransform_t *raster)
{
	Mtx identity;
	/* Static: the video thread's stack is small. */
	static cubeRasterTransform_t faded;

	/* The faces' plates keep their opacity; only the strokes fade. */
	faded = *raster;
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++) {
		float facing = faceFacing(raster, face);

		faded.motifAlpha[face] *= faceStrokeShare(facing);
		liftFaceIcon(&faded, face, faceIconLift(facing));
	}
	raster = &faded;

	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++) {
		/* A face turned away draws nothing. The controller still reads the
		 * pad (it returns before drawing), so its idle play does not start
		 * the moment its face comes back. */
		if(raster->motifAlpha[face] <= 0.0f &&
			face * UI_HOME_ICON_CHOICES + choices[face] != UI_HOME_ICON_CONTROLLER) continue;
		drawOneFaceIcon(raster, face, choices[face], seconds, animated, clock, pad);
	}
	GX_LoadPosMtxImm(raster->model, GX_PNMTX0);
	GX_SetCullMode(GX_CULL_BACK);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
}

/* Each icon's shadow on the glass under it. The light comes from the upper
 * right, as the rim's does (drawGlassRim), so the shadow falls down and to
 * the left, further the higher the icon floats. It is the icon drawn again
 * before it, sheared onto the glass and darkening what is there: its fades
 * three times as wide (a lower scale widens every fringe on screen and
 * moves no core), so it reads as a soft shadow under a sharp icon, never
 * as a blur of the icon itself. */
#define FACE_SHADOW_PLANE 1.0f
/* No wider: at six times its fades fold over inside an icon's tighter
 * curves (an Apps tile's inner corner is under five pixels round at Home)
 * and darken the glass twice there, in single dark pixels. */
#define FACE_SHADOW_FEATHER 3.0f
/* A quarter of the icon's strength, so its fade darkens the glass by no
 * more than 6 % a pixel. At 0.45 each pixel across the fade was a tenth
 * darker than the next, steps of 4 to 10 (of 255) that read as pixels in
 * the reflection round the icon. */
#define FACE_SHADOW_ALPHA 0.25f
/* How far a fully lifted icon's shadow falls, in cube sizes: about four
 * pixels at Home. */
#define FACE_SHADOW_DROP 0.034f
static void drawFaceShadows(float seconds, bool animated,
		const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
		const int choices[UI_HOME_FACE_COUNT], const cubeRasterTransform_t *raster,
		float strength)
{
	/* Down and to the left in eye space, y up. */
	const float fallX = -0.68f, fallY = -0.73f;
	const float (*m)[4] = raster->model;
	float scale = fastSqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
	Mtx identity;
	/* Static: the video thread's stack is small. */
	static cubeRasterTransform_t shadow;

	if(strength <= 0.0f || scale < 0.0001f) return;
	/* The fall in body space: the model turns, then scales by scale. */
	guVector fall = {(m[0][0] * fallX + m[1][0] * fallY) / scale,
		(m[0][1] * fallX + m[1][1] * fallY) / scale,
		(m[0][2] * fallX + m[1][2] * fallY) / scale};
	shadow = *raster;
	shadow.scaleX /= FACE_SHADOW_FEATHER;
	shadow.scaleY /= FACE_SHADOW_FEATHER;
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++) {
		const float (*basis)[4] = raster->semanticFaces[face];
		float facing = faceFacing(raster, face);
		float drop = FACE_SHADOW_DROP *
			(FACE_ICON_PLANE + faceIconLift(facing) - FACE_SHADOW_PLANE) /
			(FACE_ICON_PLANE + FACE_ICON_LIFT - FACE_SHADOW_PLANE);
		float du = (basis[0][0] * fall.x + basis[1][0] * fall.y + basis[2][0] * fall.z) * drop;
		float dv = (basis[0][1] * fall.x + basis[1][1] * fall.y + basis[2][1] * fall.z) * drop;

		/* Only as much as the strokes show: none on a face turned away. */
		shadow.motifAlpha[face] *= FACE_SHADOW_ALPHA * strength * faceStrokeShare(facing);
		/* Every icon is drawn at FACE_ICON_PLANE: through this normal its
		 * (u, v) lands on the glass at (u + du, v + dv). faceFacing cannot
		 * read a sheared normal, so the fades above come from raster. */
		for(int row = 0; row < 3; row++)
			shadow.semanticFaces[face][row][2] = (basis[row][2] * FACE_SHADOW_PLANE +
				basis[row][0] * du + basis[row][1] * dv) / FACE_ICON_PLANE;
	}
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	/* The frame times one minus the shadow's alpha. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	for(int face = 0; face < UI_HOME_FACE_COUNT; face++) {
		if(shadow.motifAlpha[face] > 0.0f)
			drawOneFaceIcon(&shadow, face, choices[face], seconds, animated, clock, pad);
	}
	GX_LoadPosMtxImm(raster->model, GX_PNMTX0);
	GX_SetCullMode(GX_CULL_BACK);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

static float glassSmoothstep(float edge0, float edge1, float x)
{
	return UIMotion_Smoothstep((x - edge0) / (edge1 - edge0));
}

/* The studio the glass mirrors: soft light cards fixed to the camera,
 * standing in for the environment the IPL's cube mirrors. A broad key lays a
 * gentle gradient across the faces, and a window above it a brighter patch
 * that slides across a face as it turns; narrow cards catch the rounded
 * bevels as thin glints; a faint fill and floor keep the far sides alive,
 * and a rim behind lights glancing glass. Each card lights reflected rays
 * from start to full cosine about its direction (eye space). */
static const struct {
	guVector direction;
	float start, full, weight;
	GXColor color;
} glassLights[] = {
	{{0.861f, 0.148f, 0.487f}, 0.74f, 0.99f, 0.60f, {200, 214, 255, 255}}, /* key */
	{{0.500f, 0.550f, 0.669f}, 0.90f, 0.935f, 0.60f, {214, 222, 255, 255}}, /* window */
	{{0.097f, 0.970f, 0.222f}, 0.88f, 0.97f, 1.20f, {246, 244, 255, 255}}, /* overhead */
	{{0.958f, 0.240f, 0.157f}, 0.88f, 0.97f, 1.00f, {226, 206, 255, 255}}, /* right */
	{{-0.800f, 0.150f, 0.581f}, 0.55f, 0.95f, 0.16f, {170, 150, 255, 255}}, /* fill */
	{{0.000f, -0.940f, 0.342f}, 0.80f, 0.97f, 0.22f, {150, 120, 240, 255}}, /* floor */
	{{0.000f, 0.350f, -0.937f}, 0.75f, 0.97f, 0.25f, {168, 146, 255, 255}}  /* rim */
};
#define GLASS_LIGHTS ((int)(sizeof(glassLights) / sizeof(glassLights[0])))

/* The light reflected along unit direction r, with the cards in colors (their
 * own, recoloured for Menu Color), premultiplied: the cards' mean colour
 * times their summed strength, which is full at one. */
static GXColor glassStudioLight(guVector r, const GXColor colors[GLASS_LIGHTS])
{
	float light = 0.0f, red = 0.0f, green = 0.0f, blue = 0.0f;
	for(int i = 0; i < GLASS_LIGHTS; i++) {
		float d = r.x * glassLights[i].direction.x + r.y * glassLights[i].direction.y +
			r.z * glassLights[i].direction.z;
		float s = glassLights[i].weight * glassSmoothstep(glassLights[i].start,
			glassLights[i].full, d);
		light += s;
		red += s * colors[i].r;
		green += s * colors[i].g;
		blue += s * colors[i].b;
	}
	if(light <= 0.0f) return (GXColor) {0, 0, 0, 255};
	float scale = fminf(1.0f, light) / light;
	return (GXColor) {(u8)(red * scale + 0.5f), (u8)(green * scale + 0.5f),
		(u8)(blue * scale + 0.5f), 255};
}

/* The studio is a sphere map: a reflected direction r lands where the unit
 * half vector between r and the view axis (+z) points, h.x and h.y across a
 * disc of radius 1/2 about the map's middle, up at the top. The camera's own
 * direction is the middle, the one straight behind the cube the rim. */
static void glassStudioCoords(guVector r, float *s, float *t)
{
	float m = 2.0f * fastSqrt(r.x * r.x + r.y * r.y + (r.z + 1.0f) * (r.z + 1.0f));
	if(m < 0.0001f) {
		*s = 0.5f;
		*t = 0.0f;
		return;
	}
	*s = 0.5f + r.x / m;
	*t = 0.5f - r.y / m;
}

/* The direction glassStudioCoords puts at (s, t); outside the disc, the one
 * on its rim. */
static guVector glassStudioDirection(float s, float t)
{
	float x = 2.0f * s - 1.0f, y = 1.0f - 2.0f * t;
	float d = x * x + y * y;
	if(d > 1.0f) {
		float rim = 1.0f / sqrtf(d);
		x *= rim;
		y *= rim;
		d = 1.0f;
	}
	float z = sqrtf(1.0f - d);
	return (guVector) {2.0f * z * x, 2.0f * z * y, 2.0f * z * z - 1.0f};
}

/* How the glass at eye-space point eye with unit normal n, offset (cube
 * sizes) from the cube's centre, mirrors the studio: where its reflected ray
 * lands on the map, and (returned) how much it reflects. The studio stands
 * close, GLASS_STUDIO_REACH cube sizes away, so a point sees it from its own
 * place along the glass: a flat face spans a wide stretch of the map and
 * its highlights slide across it as it turns. Fresnel brightens glancing
 * glass, and the last few degrees before the silhouette fade out, so the
 * reflection never draws a hard aliased rim. 0 where the glass faces away. */
static u8 glassReflect(guVector eye, guVector n, guVector offset, float *s, float *t)
{
	float length = fastSqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
	guVector view = {-eye.x / length, -eye.y / length, -eye.z / length};
	float facing = n.x * view.x + n.y * view.y + n.z * view.z;
	*s = 0.5f;
	*t = 0.5f;
	if(facing <= 0.0f) return 0;
	/* Along the glass only: a face's middle mirrors what it would from the
	 * centre, its sides reach further round the studio. */
	float out = offset.x * n.x + offset.y * n.y + offset.z * n.z;
	guVector r = {2.0f * facing * n.x - view.x + (offset.x - out * n.x) / GLASS_STUDIO_REACH,
		2.0f * facing * n.y - view.y + (offset.y - out * n.y) / GLASS_STUDIO_REACH,
		2.0f * facing * n.z - view.z + (offset.z - out * n.z) / GLASS_STUDIO_REACH};
	float reach = fastSqrt(r.x * r.x + r.y * r.y + r.z * r.z);
	float glancing = 1.0f - facing;
	float fresnel = 0.35f + 0.65f * glancing * glancing;
	glassStudioCoords((guVector) {r.x / reach, r.y / reach, r.z / reach}, s, t);
	return (u8)(255.0f * fresnel * fminf(1.0f, facing * 6.0f) + 0.5f);
}

/* The studio as the GPU samples it: GLASS_STUDIO_SIZE square, RGBA8 in the
 * GameCube's 4x4 tiles (each 32 bytes of alpha, red then 32 of green, blue).
 * It is baked on the CPU once, and again whenever Menu Color recolours the
 * cards. */
#define GLASS_STUDIO_SIZE 64
/* Its mipmaps: 32, 16, 8, 4, 2 and 1 texels square, each in whole tiles.
 * On a bevel the mirrored ray sweeps the studio within a pixel or two; the
 * GPU then reads a smaller level instead of catching a card's edge in one
 * pixel and missing it in the next, which reads as a dashed line. */
#define GLASS_STUDIO_LEVELS 7
#define GLASS_STUDIO_BYTES (64 * 64 * 4 + 32 * 32 * 4 + 16 * 16 * 4 + 8 * 8 * 4 + 3 * 64)

_Static_assert(GLASS_STUDIO_SIZE == 64, "GLASS_STUDIO_BYTES counts a 64-texel studio's levels");
static u8 glassStudioTexels[GLASS_STUDIO_BYTES] ATTRIBUTE_ALIGN(32);
static GXColor glassStudioLevel[GLASS_STUDIO_SIZE * GLASS_STUDIO_SIZE];
static GXTexObj glassStudioTexObj;
static GXColor glassStudioColors[GLASS_LIGHTS];
static bool glassStudioBaked;

/* Bakes the studio if the cards' Menu Color differs from the last bake's;
 * true if it baked. The frame before has been drawn (the video thread waits
 * for the GPU at every frame's end), so nothing still reads the texels. */
static bool prepareGlassStudio(void)
{
	GXColor colors[GLASS_LIGHTS];
	bool same = glassStudioBaked;

	for(int i = 0; i < GLASS_LIGHTS; i++) {
		colors[i] = glassLights[i].color;
		UIColor_Apply(&colors[i].r, &colors[i].g, &colors[i].b);
		same = same && colors[i].r == glassStudioColors[i].r &&
			colors[i].g == glassStudioColors[i].g && colors[i].b == glassStudioColors[i].b;
	}
	if(same) return false;
	for(int y = 0; y < GLASS_STUDIO_SIZE; y++) for(int x = 0; x < GLASS_STUDIO_SIZE; x++) {
		glassStudioLevel[y * GLASS_STUDIO_SIZE + x] = glassStudioLight(glassStudioDirection(
			((float)x + 0.5f) / GLASS_STUDIO_SIZE, ((float)y + 0.5f) / GLASS_STUDIO_SIZE), colors);
	}
	u8 *level = glassStudioTexels;
	for(int size = GLASS_STUDIO_SIZE; size > 0; size >>= 1) {
		int tiles = size < 4 ? 1 : size >> 2;
		for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) {
			GXColor light = glassStudioLevel[y * size + x];
			u8 *tile = level + ((y >> 2) * tiles + (x >> 2)) * 64;
			int at = ((y & 3) * 4 + (x & 3)) * 2;
			tile[at] = light.a;
			tile[at + 1] = light.r;
			tile[32 + at] = light.g;
			tile[32 + at + 1] = light.b;
		}
		level += tiles * tiles * 64;
		/* The next level in place: each texel averages the four it covers,
		 * all read before anything at or after its own index is written. */
		int half = size >> 1;
		for(int y = 0; y < half; y++) for(int x = 0; x < half; x++) {
			const GXColor *a = &glassStudioLevel[2 * y * size + 2 * x], *b = a + size;
			glassStudioLevel[y * half + x] = (GXColor) {
				(u8)((a[0].r + a[1].r + b[0].r + b[1].r + 2) >> 2),
				(u8)((a[0].g + a[1].g + b[0].g + b[1].g + 2) >> 2),
				(u8)((a[0].b + a[1].b + b[0].b + b[1].b + 2) >> 2),
				(u8)((a[0].a + a[1].a + b[0].a + b[1].a + 2) >> 2)};
		}
	}
	DCFlushRange(glassStudioTexels, sizeof(glassStudioTexels));
	GX_InitTexObj(&glassStudioTexObj, glassStudioTexels, GLASS_STUDIO_SIZE,
		GLASS_STUDIO_SIZE, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_TRUE);
	GX_InitTexObjLOD(&glassStudioTexObj, GX_LIN_MIP_LIN, GX_LINEAR, 0.0f,
		(float)(GLASS_STUDIO_LEVELS - 1), 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_1);
	GX_InvalidateTexAll();
	for(int i = 0; i < GLASS_LIGHTS; i++) glassStudioColors[i] = colors[i];
	glassStudioBaked = true;
	return true;
}

/* The reflection's pipeline: the studio in TEXMAP1 (TEXMAP0 is the frame
 * copy's), turned by drift radians about the view axis, times each vertex's
 * reflectance in its alpha, added to the frame. One texture coordinate, one
 * TEV stage. */
static void setupGlassReflectionPipeline(float drift)
{
	float c = cosf(drift), s = sinf(drift);
	Mtx turn = {
		{c, -s, 0.0f, 0.5f - 0.5f * c + 0.5f * s},
		{s, c, 0.0f, 0.5f - 0.5f * s - 0.5f * c},
		{0.0f, 0.0f, 1.0f, 0.0f}
	};

	prepareGlassStudio();
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_LoadTexObj(&glassStudioTexObj, GX_TEXMAP1);
	GX_LoadTexMtxImm(turn, GX_TEXMTX0, GX_MTX2x4);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
}

/* Face and bevel vertices each lie on exactly one outer plane, whose axis is
 * their normal. Interpolating those normals across a flat bevel shades it
 * as a rounded, polished edge that the reflection rolls over. */
static guVector glassVertexNormal(const cubeRasterTransform_t *raster,
		guVector p, float outer)
{
	guVector axis = {
		fabsf(p.x) >= outer - 0.001f ? (p.x < 0.0f ? -1.0f : 1.0f) : 0.0f,
		fabsf(p.y) >= outer - 0.001f ? (p.y < 0.0f ? -1.0f : 1.0f) : 0.0f,
		fabsf(p.z) >= outer - 0.001f ? (p.z < 0.0f ? -1.0f : 1.0f) : 0.0f
	};
	return cubeViewNormal(raster, axis);
}

static bool glassSameNormal(guVector a, guVector b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

static guVector glassBilinear(const guVector corner[4], float u, float v)
{
	float w0 = (1.0f - u) * (1.0f - v), w1 = u * (1.0f - v), w2 = u * v, w3 = (1.0f - u) * v;
	return (guVector) {
		w0 * corner[0].x + w1 * corner[1].x + w2 * corner[2].x + w3 * corner[3].x,
		w0 * corner[0].y + w1 * corner[1].y + w2 * corner[2].y + w3 * corner[3].y,
		w0 * corner[0].z + w1 * corner[1].z + w2 * corner[2].z + w3 * corner[3].z
	};
}

/* The point k/steps of the way along a side from p to q, made the same way
 * whichever surface asks and whichever way round it walks the side: the
 * ends in one fixed order, one copy of the arithmetic (the console's
 * compiler fuses multiply-adds, so one sum written two ways can round
 * apart). Surfaces that meet at a side cut it the same number of times, so
 * they share every point on it bit for bit. A point that only one of them
 * has there (a T-junction) is snapped to the console's 1/16 pixel apart from
 * the other's straight side, and single pixels along the seam open and
 * close as the cube moves: sparkles, which Dolphin's finer snap all but
 * hides. */
static guVector glassSidePoint(guVector p, guVector q, int k, int steps)
{
	if(q.x < p.x || (q.x == p.x && (q.y < p.y || (q.y == p.y && q.z < p.z)))) {
		guVector swap = p;

		p = q;
		q = swap;
		k = steps - k;
	}
	if(k <= 0) return p;
	if(k >= steps) return q;
	float t = (float)k / (float)steps;
	return (guVector) {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t,
		p.z + (q.z - p.z) * t};
}

/* Point (column, row) of a surface cut columns by rows: on its sides from
 * glassSidePoint, so the surface beside it has the same points, and
 * bilinear only inside. */
static guVector glassGridPoint(const guVector corner[4], int column, int columns,
		int row, int rows)
{
	if(row == 0) return glassSidePoint(corner[0], corner[1], column, columns);
	if(row == rows) return glassSidePoint(corner[3], corner[2], column, columns);
	if(column == 0) return glassSidePoint(corner[0], corner[3], row, rows);
	if(column == columns) return glassSidePoint(corner[1], corner[2], row, rows);
	return glassBilinear(corner, (float)column / columns, (float)row / rows);
}

/* Point k of a corner triangle drawn as a fan: 0 is its middle, and 1 to
 * 3 * cuts go round its sides from corner 0, each side cut as the bevel end
 * it closes is cut across, so the two share every point. */
static void glassFanPoint(const guVector corner[3], const guVector eyes[3],
		const guVector normals[3], int cuts, int k, guVector *body, guVector *eye,
		guVector *normal)
{
	guVector n;
	float length;

	if(k == 0) {
		*body = (guVector) {(corner[0].x + corner[1].x + corner[2].x) / 3.0f,
			(corner[0].y + corner[1].y + corner[2].y) / 3.0f,
			(corner[0].z + corner[1].z + corner[2].z) / 3.0f};
		*eye = (guVector) {(eyes[0].x + eyes[1].x + eyes[2].x) / 3.0f,
			(eyes[0].y + eyes[1].y + eyes[2].y) / 3.0f,
			(eyes[0].z + eyes[1].z + eyes[2].z) / 3.0f};
		n = (guVector) {normals[0].x + normals[1].x + normals[2].x,
			normals[0].y + normals[1].y + normals[2].y,
			normals[0].z + normals[1].z + normals[2].z};
	}
	else {
		int side = (k - 1) / cuts, next = (side + 1) % 3, cut = (k - 1) % cuts;

		*body = glassSidePoint(corner[side], corner[next], cut, cuts);
		*eye = glassSidePoint(eyes[side], eyes[next], cut, cuts);
		n = glassSidePoint(normals[side], normals[next], cut, cuts);
	}
	length = fastSqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	*normal = (guVector) {n.x / length, n.y / length, n.z / length};
}

/* One reflecting vertex: its body position, where it looks in the studio
 * and how much it reflects. */
typedef struct glassMirrorVertex {
	guVector body;
	float s;
	float t;
	u8 alpha;
} glassMirrorVertex_t;

static void putGlassMirrorVertex(const glassMirrorVertex_t *vertex)
{
	GX_Position3f32(vertex->body.x, vertex->body.y, vertex->body.z);
	GX_Color4u8(255, 255, 255, vertex->alpha);
	GX_TexCoord2f32(vertex->s, vertex->t);
}

/* The camera-facing outer glass mirroring the studio, additively. The GPU
 * reads the studio at every pixel, so a highlight is as sharp as the map and
 * slides across the glass as it turns; the vertices only need to follow
 * the reflected ray: faces in a coarse grid for perspective, bevels finely
 * across their width, where the normal rolls. Cells that reflect nothing at
 * all four corners (turned away or grazing) are skipped. Corner triangles
 * are fans cut as the bevel ends they close, and mirror at every point. */
static void drawGlassReflection(const cubeRasterTransform_t *raster,
		const cubeSurfaceQuad_t *quads, int count, int vertexCount, float outer)
{
	/* A bevel is cut along as often as the face beside it (glassSidePoint). */
	enum { FACE_STEPS = 4, ACROSS_STEPS = 6, ALONG_STEPS = FACE_STEPS, GRID = 7 };
	const guVector centre = {raster->model[0][3], raster->model[1][3], raster->model[2][3]};
	const float size = fastSqrt(raster->model[0][0] * raster->model[0][0] +
		raster->model[1][0] * raster->model[1][0] + raster->model[2][0] * raster->model[2][0]);

	for(int quad = 0; quad < count; quad++) {
		guVector eyes[4], normals[4];
		glassMirrorVertex_t grid[GRID * GRID];
		indigoPoint_t points[4];
		float area = 0.0f;
		int cells = 0;
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			guVector p = quads[quad].point[vertex];
			if(!projectRailPoint(raster, p.x, p.y, p.z, &eyes[vertex], &points[vertex])) return;
			normals[vertex] = glassVertexNormal(raster, p, outer);
		}
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			int next = (vertex + 1) % vertexCount;
			area += points[vertex].x * points[next].y - points[next].x * points[vertex].y;
		}
		if(area >= -0.001f) continue;
		if(vertexCount == 3) {
			/* A fan, its sides cut as the bevel ends it closes are cut
			 * across; a triangle that reflects nothing is skipped. */
			const int points = 1 + 3 * ACROSS_STEPS;
			int lit = 0;
			for(int k = 0; k < points; k++) {
				guVector e, n;
				glassFanPoint(quads[quad].point, eyes, normals, ACROSS_STEPS, k,
					&grid[k].body, &e, &n);
				grid[k].alpha = glassReflect(e, n, (guVector) {(e.x - centre.x) / size,
					(e.y - centre.y) / size, (e.z - centre.z) / size}, &grid[k].s, &grid[k].t);
			}
			for(int k = 1; k < points; k++)
				lit += (grid[0].alpha | grid[k].alpha | grid[k + 1 < points ? k + 1 : 1].alpha) != 0;
			if(lit == 0) continue;
			GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)(lit * 3));
			for(int k = 1; k < points; k++) {
				const glassMirrorVertex_t *next = &grid[k + 1 < points ? k + 1 : 1];
				if((grid[0].alpha | grid[k].alpha | next->alpha) == 0) continue;
				putGlassMirrorVertex(&grid[0]);
				putGlassMirrorVertex(&grid[k]);
				putGlassMirrorVertex(next);
			}
			GX_End();
			continue;
		}
		/* A bevel's normal changes only between its two sides. */
		bool acrossU = !glassSameNormal(normals[0], normals[1]);
		bool acrossV = !glassSameNormal(normals[0], normals[3]);
		int columns = acrossU ? ACROSS_STEPS : (acrossV ? ALONG_STEPS : FACE_STEPS);
		int rows = acrossV ? ACROSS_STEPS : (acrossU ? ALONG_STEPS : FACE_STEPS);
		for(int row = 0; row <= rows; row++) for(int column = 0; column <= columns; column++) {
			float u = (float)column / columns, v = (float)row / rows;
			glassMirrorVertex_t *at = &grid[row * GRID + column];
			guVector n = glassBilinear(normals, u, v), e = glassBilinear(eyes, u, v);
			float length = fastSqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			n = (guVector) {n.x / length, n.y / length, n.z / length};
			at->body = glassGridPoint(quads[quad].point, column, columns, row, rows);
			at->alpha = glassReflect(e, n, (guVector) {(e.x - centre.x) / size,
				(e.y - centre.y) / size, (e.z - centre.z) / size}, &at->s, &at->t);
		}
		for(int row = 0; row < rows; row++) for(int column = 0; column < columns; column++) {
			const glassMirrorVertex_t *first = &grid[row * GRID + column];
			cells += (first[0].alpha | first[1].alpha | first[GRID].alpha | first[GRID + 1].alpha) != 0;
		}
		if(cells == 0) continue;
		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(cells * 4));
		for(int row = 0; row < rows; row++) for(int column = 0; column < columns; column++) {
			static const int corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
			const glassMirrorVertex_t *first = &grid[row * GRID + column];
			if((first[0].alpha | first[1].alpha | first[GRID].alpha | first[GRID + 1].alpha) == 0)
				continue;
			for(int vertex = 0; vertex < 4; vertex++)
				putGlassMirrorVertex(&first[corner[vertex][1] * GRID + corner[vertex][0]]);
		}
		GX_End();
	}
}

/* Screen-space glass. The frame behind the cube is copied from the EFB at
 * half resolution into one RGBA8 texture, which the glass then refracts,
 * and after the cube is drawn the same texture carries its bloom. Half
 * resolution is the look, not only the budget: a soft copy reads as depth
 * inside the glass and as a wide glow. RGBA8 keeps the dark indigo
 * gradient free of the banding a 16-bit copy would show. */
#define GLASS_COPY_MAX_W 320
#define GLASS_COPY_MAX_H 288
#define GLASS_BLOOM_TAPS 5

static u8 glassTexels[GLASS_COPY_MAX_W * GLASS_COPY_MAX_H * 4] ATTRIBUTE_ALIGN(32);
static GXTexObj glassTexObj;
static u16 glassEfbWidth = 640;
static u16 glassEfbHeight = 480;
static u16 glassCopyWidth;
static u16 glassCopyHeight;
static u16 glassCopyLeft;
static u16 glassCopyTop;
static bool glassTexelsFlushed;

void IndigoBackground_SetFramebuffer(u16 width, u16 height)
{
	glassEfbWidth = width;
	glassEfbHeight = height;
}

typedef struct glassRefraction {
	float centerX;
	float centerY;
	float bend;
	float magnify;
	float dispersion;
	float sScale;
	float tScale;
	float sOffset;
	float tOffset;
	GXColor tint; /* its alpha is the layer's opacity */
} glassRefraction_t;

/* How much of the glass light a scene shows. Home and Source carry the full
 * effect; the cube behind the Library, a game's details and Settings keeps a
 * quieter version of it. */
static float glassSceneStrength(const uiSceneFrame_t *scene)
{
	float strength = scene->orbitStrength < 0.0f ? 0.0f :
		(scene->orbitStrength > 1.0f ? 1.0f : scene->orbitStrength);
	if(scene->scene == UI_SCENE_SETTINGS) {
		/* Settings covers the whole screen; the cube there is only seen
		 * while the page fades, so it keeps just the vertex light. */
		return 0.0f;
	}
	/* During the boot reveal the overlay's cube is plain glass. The light
	 * fades in once the background cube takes over, so nothing pops. */
	return strength * glassSmoothstep(BOOT_CUBE_HANDOFF, 1.0f, scene->introProgress);
}

/* The copy runs in the pixel pipeline behind every draw already queued, so
 * it holds exactly what the frame shows at this point. The CPU never reads
 * the texels; their cache lines are written back once, before the GPU first
 * writes the buffer, so no stale line can later overwrite a copy. */
/* Copies the part of the frame the glass samples: the box (frame pixels,
 * 640 x 480 units) halved by the box filter. Its corners snap outward to
 * 8 EFB pixels, so every texel averages the same four pixels as it would
 * in a copy of the whole frame, and the copy stays in whole 4x4 tiles.
 * glassCopyLeft and glassCopyTop keep where it starts, in EFB pixels. */
static bool glassCopyFrame(float left, float top, float right, float bottom)
{
	float sx = (float)glassEfbWidth / 640.0f, sy = (float)glassEfbHeight / 480.0f;
	int x0, y0, x1, y1;
	u16 width, height;

	left = left < 0.0f ? 0.0f : left * sx;
	top = top < 0.0f ? 0.0f : top * sy;
	right = right * sx; bottom = bottom * sy;
	x0 = (int)left & ~7;
	y0 = (int)top & ~7;
	x1 = ((int)right + 8) & ~7;
	y1 = ((int)bottom + 8) & ~7;
	if(x1 > (int)(glassEfbWidth & ~7u)) x1 = (int)(glassEfbWidth & ~7u);
	if(y1 > (int)(glassEfbHeight & ~7u)) y1 = (int)(glassEfbHeight & ~7u);
	if(x1 - x0 < 8 || y1 - y0 < 8) return false;
	width = (u16)((x1 - x0) / 2);
	height = (u16)((y1 - y0) / 2);
	if(width > GLASS_COPY_MAX_W) width = GLASS_COPY_MAX_W;
	if(height > GLASS_COPY_MAX_H) height = GLASS_COPY_MAX_H;
	if(!glassTexelsFlushed) {
		DCFlushRange(glassTexels, sizeof(glassTexels));
		glassTexelsFlushed = true;
	}
	GX_SetTexCopySrc((u16)x0, (u16)y0, (u16)(width * 2u), (u16)(height * 2u));
	GX_SetTexCopyDst(width, height, GX_TF_RGBA8, GX_TRUE);
	GX_CopyTex(glassTexels, GX_FALSE);
	GX_PixModeSync();
	GX_InitTexObj(&glassTexObj, glassTexels, width, height, GX_TF_RGBA8,
		GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&glassTexObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f,
		GX_FALSE, GX_FALSE, GX_ANISO_1);
	GX_InvalidateTexAll();
	GX_LoadTexObj(&glassTexObj, GX_TEXMAP0);
	glassCopyWidth = width;
	glassCopyHeight = height;
	glassCopyLeft = (u16)x0;
	glassCopyTop = (u16)y0;
	return true;
}

/* Texture coordinates of a frame point (640 x 480 units) in the copy: the
 * point times the scale, less the offset; UIStage_FrameX takes a stage x
 * there. */
static float glassCopyS(void)
{
	return (float)glassEfbWidth / (float)(glassCopyWidth * 2u) / 640.0f;
}

static float glassCopyT(void)
{
	return (float)glassEfbHeight / (float)(glassCopyHeight * 2u) / 480.0f;
}

static float glassCopyS0(void)
{
	return (float)glassCopyLeft / (float)(glassCopyWidth * 2u);
}

static float glassCopyT0(void)
{
	return (float)glassCopyTop / (float)(glassCopyHeight * 2u);
}

/* Vertex color back to the only TEV input: the state every cube pass and
 * setupCubePipeline share. */
static void restoreCubeRaster(void)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetNumTexGens(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
}

/* A face turned away from the camera shows its icon as a picture instead of
 * as strokes: the strokes drawn four times as wide and twice as tall into
 * the top left of the frame before the backdrop (which then paints over the
 * corner), halved both ways by the copy's 2x2 box filter and laid where the
 * icon sits, two texels to a pixel across, so each pixel averages four
 * samples across and two down. A face turned away is squeezed across: a
 * line thinner than a pixel there keeps its light, spread evenly, where
 * drawn at size it breaks into fragments, and it no longer pops in and out
 * as the cube sways; so every face's icon shows, and keeps moving, as the
 * cube turns. At most three faces of a cube face the camera. */
#define FACE_PICTURE_SLOTS 3
#define FACE_PICTURE_MAX_W 128
#define FACE_PICTURE_MAX_H 224
/* The square a face's icon fits, a little past the plate so a stroke's fade
 * fits too. */
#define FACE_PICTURE_REACH 0.76f

typedef struct facePicture {
	GXTexObj texture;
	float weight;
	u16 x, y, width, height;
} facePicture_t;

static u8 facePictureTexels[FACE_PICTURE_SLOTS][2 * FACE_PICTURE_MAX_W * FACE_PICTURE_MAX_H * 2]
	ATTRIBUTE_ALIGN(32);
static facePicture_t facePictures[FACE_PICTURE_SLOTS];
static int facePictureCount;
static bool facePictureTexelsFlushed;

/* The cube's projection, scaled about frame pixel (x, y) by four across and
 * two down: what lands there lands at the frame's top left instead. */
static void facePictureProjection(Mtx44 projection, int x, int y)
{
	float shiftX = 3.0f - 8.0f * (float)x / glassEfbWidth;
	float shiftY = -1.0f + 4.0f * (float)y / glassEfbHeight;

	for(int row = 0; row < 4; row++)
		for(int column = 0; column < 4; column++)
			projection[row][column] = cubeProjection[row][column];
	UIStage_Project(projection);
	for(int column = 0; column < 4; column++) {
		projection[0][column] = 4.0f * projection[0][column] + shiftX * projection[3][column];
		projection[1][column] = 2.0f * projection[1][column] + shiftY * projection[3][column];
	}
}

/* Before the frame: a picture of each turned face's icon. */
static void renderFacePictures(const uiSceneFrame_t *scene, float seconds,
		bool animated, const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
		const int icons[UI_HOME_FACE_COUNT])
{
	/* Static: the video thread's stack is small. */
	static cubeRasterTransform_t raster, fine;
	float frameX = glassEfbWidth / 640.0f, frameY = glassEfbHeight / 480.0f;
	Mtx44 projection;
	Mtx identity;

	facePictureCount = 0;
	setupCubePipeline(scene, seconds, animated, &raster);
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int face = 0; face < UI_HOME_FACE_COUNT && facePictureCount < FACE_PICTURE_SLOTS; face++) {
		float facing = faceFacing(&raster, face);
		float weight = raster.motifAlpha[face] * (1.0f - faceStrokeShare(facing));
		/* Lifted as its strokes are, so the two line up as they cross. */
		float lift = faceIconLift(facing);
		float left = 1.0e9f, top = 1.0e9f, right = -1.0e9f, bottom = -1.0e9f;
		bool seen = true;

		if(icons[face] < 0 || icons[face] >= UI_HOME_ICON_CHOICES ||
			facing <= 0.0f || weight < 1.0f / 255.0f) {
			continue;
		}
		for(int corner = 0; corner < 4 && seen; corner++) {
			guVector body = semanticFacePoint(&raster, face,
				corner & 1 ? FACE_PICTURE_REACH : -FACE_PICTURE_REACH,
				corner & 2 ? FACE_PICTURE_REACH : -FACE_PICTURE_REACH,
				FACE_ICON_PLANE + lift);
			guVector eye;
			indigoPoint_t at;
			seen = projectRailPoint(&raster, body.x, body.y, body.z, &eye, &at);
			float x = UIStage_FrameX(320.0f + at.x) * frameX, y = (240.0f - at.y) * frameY;
			left = fminf(left, x);
			right = fmaxf(right, x);
			top = fminf(top, y);
			bottom = fmaxf(bottom, y);
		}
		int x0 = (int)floorf(left) - 1, y0 = (int)floorf(top) - 1;
		int width = ((int)ceilf(right) + 1 - x0 + 3) & ~3;
		int height = ((int)ceilf(bottom) + 1 - y0 + 3) & ~3;
		/* Off the frame's edge, too big for a slot or for the corner it is
		 * drawn in: the strokes alone. */
		if(!seen || x0 < 0 || y0 < 0 || x0 + width > glassEfbWidth ||
			y0 + height > glassEfbHeight || width > FACE_PICTURE_MAX_W ||
			height > FACE_PICTURE_MAX_H || 4 * width > glassEfbWidth ||
			2 * height > glassEfbHeight) {
			continue;
		}

		facePicture_t *picture = &facePictures[facePictureCount];
		fine = raster;
		fine.scaleX *= 4.0f;
		fine.scaleY *= 2.0f;
		fine.motifAlpha[face] = 1.0f;
		liftFaceIcon(&fine, face, lift);
		if(!facePictureTexelsFlushed) {
			DCFlushRange(facePictureTexels, sizeof(facePictureTexels));
			facePictureTexelsFlushed = true;
		}
		/* Nothing is drawn before the background, so the corner holds the
		 * frame's black clear; each copy clears what it read for the next. */
		GX_SetTexCopySrc(0, 0, (u16)(width * 4), (u16)(height * 2));
		GX_SetTexCopyDst((u16)(width * 2), (u16)height, GX_TF_RGB565, GX_TRUE);
		facePictureProjection(projection, x0, y0);
		GX_LoadProjectionMtx(projection, GX_PERSPECTIVE);
		GX_SetScissor(0, 0, (u32)width * 4u, (u32)height * 2u);
		drawOneFaceIcon(&fine, face, icons[face], seconds, animated, clock, pad);
		GX_CopyTex(facePictureTexels[facePictureCount], GX_TRUE);
		/* Two texels to a pixel across: a pixel's centre falls between two,
		 * and the bilinear read averages them. */
		GX_InitTexObj(&picture->texture, facePictureTexels[facePictureCount],
			(u16)(width * 2), (u16)height, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_InitTexObjLOD(&picture->texture, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f,
			GX_FALSE, GX_FALSE, GX_ANISO_1);
		picture->weight = weight * glassSmoothstep(BOOT_CUBE_HANDOFF, 1.0f, scene->introProgress);
		picture->x = (u16)x0;
		picture->y = (u16)y0;
		picture->width = (u16)width;
		picture->height = (u16)height;
		facePictureCount++;
	}
	GX_SetScissor(0, 0, glassEfbWidth, glassEfbHeight);
	if(facePictureCount > 0) {
		GX_PixModeSync();
		GX_InvalidateTexAll();
	}
}

/* The pictures laid on the glass, added like the strokes; each one's weight
 * is the share of its icon the strokes leave. */
static void drawFacePictures(const cubeRasterTransform_t *raster)
{
	Mtx44 projection;
	Mtx identity;

	if(facePictureCount == 0) return;
	guOrtho(projection, 0.0f, (f32)glassEfbHeight, 0.0f, (f32)glassEfbWidth, 0.0f, 1.0f);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP3, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int i = 0; i < facePictureCount; i++) {
		const facePicture_t *picture = &facePictures[i];
		float x0 = picture->x, y0 = picture->y;
		float x1 = x0 + picture->width, y1 = y0 + picture->height;
		u8 alpha = (u8)(255.0f * fminf(1.0f, picture->weight) + 0.5f);

		GX_LoadTexObj((GXTexObj *)&picture->texture, GX_TEXMAP3);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(x0, y0, 0.0f);
			GX_Color4u8(255, 255, 255, alpha);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(x1, y0, 0.0f);
			GX_Color4u8(255, 255, 255, alpha);
			GX_TexCoord2f32(1.0f, 0.0f);
			GX_Position3f32(x1, y1, 0.0f);
			GX_Color4u8(255, 255, 255, alpha);
			GX_TexCoord2f32(1.0f, 1.0f);
			GX_Position3f32(x0, y1, 0.0f);
			GX_Color4u8(255, 255, 255, alpha);
			GX_TexCoord2f32(0.0f, 1.0f);
		GX_End();
	}
	loadCubeProjection();
	restoreCubeRaster();
	GX_LoadPosMtxImm(raster->model, GX_PNMTX0);
	GX_SetCullMode(GX_CULL_BACK);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

/* Dispersion: three samples of the copy, one per channel, each bent a
 * little further than the last, so edges part into red, green and blue the
 * way a prism does. Stage 3 multiplies the vertex tint in at double scale,
 * so 128 leaves the light as it came through. */
static void setupGlassRefractionPipeline(void)
{
	static const GXColor masks[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX1, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX2, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX2, GX_TEX_ST, GX_F32, 0);
	GX_SetNumTexGens(3);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, GX_IDENTITY);
	GX_SetNumTevStages(4);
	for(int channel = 0; channel < 3; channel++) {
		u8 stage = (u8)(GX_TEVSTAGE0 + channel);
		GX_SetTevKColor((u8)(GX_KCOLOR0 + channel), masks[channel]);
		GX_SetTevKColorSel(stage, (u8)(GX_TEV_KCSEL_K0 + channel));
		GX_SetTevOrder(stage, (u8)(GX_TEXCOORD0 + channel), GX_TEXMAP0, GX_COLOR0A0);
		GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST,
			channel == 0 ? GX_CC_ZERO : GX_CC_CPREV);
		GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
		GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
		GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	}
	GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
}

/* What one glass vertex refracts: its body position, tint and the three
 * channels' coordinates in the copy. */
typedef struct glassRefractedVertex {
	guVector body;
	GXColor color;
	float s[3];
	float t[3];
} glassRefractedVertex_t;

/* A convex surface bends the rays behind it toward its middle, so the frame
 * is sampled nearer the cube's centre: a slight magnification across the
 * faces and a strong pull round the rounded bevels, where the normal turns
 * sideways. Each channel bends a little more than the last. The layer fades
 * out over the last glancing degrees, so at the silhouette the bent image
 * meets the straight one. */
static void refractGlassVertex(const cubeRasterTransform_t *raster,
		const glassRefraction_t *glass, guVector body, guVector eye, guVector n,
		glassRefractedVertex_t *out)
{
	float length = fastSqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
	guVector view = {-eye.x / length, -eye.y / length, -eye.z / length};
	float facing = n.x * view.x + n.y * view.y + n.z * view.z;
	float sx = 320.0f + raster->scaleX * eye.x / -eye.z;
	float sy = 240.0f - raster->scaleY * eye.y / -eye.z;
	float bx = glass->centerX + (sx - glass->centerX) * (1.0f - glass->magnify);
	float by = glass->centerY + (sy - glass->centerY) * (1.0f - glass->magnify);
	float ox = -n.x * glass->bend, oy = n.y * glass->bend;

	out->body = body;
	out->color = glass->tint;
	out->color.a = (u8)((float)glass->tint.a * glassSmoothstep(0.0f, 0.34f, facing) + 0.5f);
	UIColor_Apply(&out->color.r, &out->color.g, &out->color.b);
	for(int channel = 0; channel < 3; channel++) {
		float k = 1.0f + glass->dispersion * (float)(channel - 1);
		out->s[channel] = UIStage_FrameX(bx + ox * k) * glass->sScale - glass->sOffset;
		out->t[channel] = (by + oy * k) * glass->tScale - glass->tOffset;
	}
}

static void putGlassRefractedVertex(const glassRefractedVertex_t *vertex)
{
	GX_Position3f32(vertex->body.x, vertex->body.y, vertex->body.z);
	GX_Color4u8(vertex->color.r, vertex->color.g, vertex->color.b, vertex->color.a);
	for(int channel = 0; channel < 3; channel++)
		GX_TexCoord2f32(vertex->s[channel], vertex->t[channel]);
}

/* The camera-facing outer glass as a refracting layer over the copy: faces
 * in a coarse grid, where only perspective needs the extra vertices, and
 * bevels finely across their width, where the normal rolls. It is drawn
 * before the dark core, which then covers the middle, so what refracts is
 * the frame seen through the glass around it. */
static void drawGlassRefraction(const cubeRasterTransform_t *raster,
		const glassRefraction_t *glass, const cubeSurfaceQuad_t *quads, int count,
		int vertexCount, float outer)
{
	/* A bevel is cut along as often as the face beside it (glassSidePoint). */
	enum { FACE_STEPS = 4, ACROSS_STEPS = 6, ALONG_STEPS = FACE_STEPS, GRID = 7 };

	for(int quad = 0; quad < count; quad++) {
		guVector eyes[4], normals[4];
		glassRefractedVertex_t grid[GRID * GRID];
		indigoPoint_t points[4];
		float area = 0.0f;
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			guVector p = quads[quad].point[vertex];
			if(!projectRailPoint(raster, p.x, p.y, p.z, &eyes[vertex], &points[vertex])) return;
			normals[vertex] = glassVertexNormal(raster, p, outer);
		}
		for(int vertex = 0; vertex < vertexCount; vertex++) {
			int next = (vertex + 1) % vertexCount;
			area += points[vertex].x * points[next].y - points[next].x * points[vertex].y;
		}
		if(area >= -0.001f) continue;
		if(vertexCount == 3) {
			/* A fan, its sides cut as the bevel ends it closes are cut across. */
			const int points = 1 + 3 * ACROSS_STEPS;
			for(int k = 0; k < points; k++) {
				guVector body, e, n;
				glassFanPoint(quads[quad].point, eyes, normals, ACROSS_STEPS, k,
					&body, &e, &n);
				refractGlassVertex(raster, glass, body, e, n, &grid[k]);
			}
			GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)((points - 1) * 3));
			for(int k = 1; k < points; k++) {
				putGlassRefractedVertex(&grid[0]);
				putGlassRefractedVertex(&grid[k]);
				putGlassRefractedVertex(&grid[k + 1 < points ? k + 1 : 1]);
			}
			GX_End();
			continue;
		}
		bool acrossU = !glassSameNormal(normals[0], normals[1]);
		bool acrossV = !glassSameNormal(normals[0], normals[3]);
		int columns = acrossU ? ACROSS_STEPS : (acrossV ? ALONG_STEPS : FACE_STEPS);
		int rows = acrossV ? ACROSS_STEPS : (acrossU ? ALONG_STEPS : FACE_STEPS);
		for(int row = 0; row <= rows; row++) for(int column = 0; column <= columns; column++) {
			float u = (float)column / columns, v = (float)row / rows;
			guVector normal = glassBilinear(normals, u, v);
			float normalLength = fastSqrt(normal.x * normal.x + normal.y * normal.y +
				normal.z * normal.z);
			normal = (guVector) {normal.x / normalLength, normal.y / normalLength,
				normal.z / normalLength};
			refractGlassVertex(raster, glass,
				glassGridPoint(quads[quad].point, column, columns, row, rows),
				glassBilinear(eyes, u, v), normal, &grid[row * GRID + column]);
		}
		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(rows * columns * 4));
		for(int row = 0; row < rows; row++) for(int column = 0; column < columns; column++) {
			static const int corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
			for(int vertex = 0; vertex < 4; vertex++)
				putGlassRefractedVertex(&grid[(row + corner[vertex][1]) * GRID +
					column + corner[vertex][0]]);
		}
		GX_End();
	}
}

/* A soft round light in screen units: a centre and three rings whose alphas
 * follow a gaussian, so additive glows have no visible cone or edge. */
static void drawSoftGlow(float x, float y, float radiusX, float radiusY,
		GXColor color, float alpha)
{
	static const float ring[4] = {0.0f, 0.28f, 0.58f, 1.0f};
	static const float falloff[4] = {1.0f, 0.74f, 0.30f, 0.0f};
	const float step = INDIGO_TAU / RADIAL_SEGMENTS;
	const float stepCos = cosf(step), stepSin = sinf(step);

	if(alpha <= 0.004f || radiusX <= 0.0f || radiusY <= 0.0f) return;
	if(alpha > 1.0f) alpha = 1.0f;
	for(int band = 0; band < 3; band++) {
		float unitX = 1.0f, unitY = 0.0f;
		GXColor inner = color, outerColor = color;
		inner.a = (u8)(255.0f * alpha * falloff[band] + 0.5f);
		outerColor.a = (u8)(255.0f * alpha * falloff[band + 1] + 0.5f);
		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (RADIAL_SEGMENTS + 1) * 2);
		for(int i = 0; i <= RADIAL_SEGMENTS; i++) {
			putVertex((indigoPoint_t) {x + radiusX * ring[band] * unitX,
				y + radiusY * ring[band] * unitY}, inner);
			putVertex((indigoPoint_t) {x + radiusX * ring[band + 1] * unitX,
				y + radiusY * ring[band + 1] * unitY}, outerColor);
			float nextX = unitX * stepCos - unitY * stepSin;
			unitY = unitX * stepSin + unitY * stepCos;
			unitX = nextX;
		}
		GX_End();
	}
}

/* Light on the glass's outline: a fine rim that is brightest on the edges
 * turned toward the key light and gone on the far ones, as glass edges
 * catch light by Fresnel reflection. Just outside it a warm fringe and just
 * inside a cool one part the rim the way dispersion parts a bright edge.
 * The outline is the convex hull buildCubeOutline gives (screen units
 * about the centre, y up). */
static void drawGlassRim(const cubeOutline_t *outline, float strength)
{
	static const struct {
		float offset;
		float alpha;
		GXColor color;
	} layers[3] = {
		{0.0f, 0.37f, {236, 230, 255, 255}},
		{1.35f, 0.12f, {255, 184, 150, 255}},
		{-1.35f, 0.13f, {90, 224, 246, 255}}
	};
	/* Brightest on the edge, fading to nothing two pixels either side: as
	 * much light as the old solid one-pixel core between one-pixel fades,
	 * and any pixel centre within half a pixel of the edge still gets three
	 * quarters of it, so the rim does not bead along the edge. A solid core
	 * stair-stepped on the console where the rim runs at an angle (the top
	 * right corner). */
	static const float profile[3] = {-2.0f, 0.0f, 2.0f};
	const float lightX = 0.68f, lightY = -0.73f;
	indigoPoint_t points[25], joins[25];
	GXColor colors[25];
	int count = outline->count;

	if(count < 3 || count > 24 || strength <= 0.01f) return;
	for(int i = 0; i < count; i++) {
		points[i] = (indigoPoint_t) {320.0f + outline->point[i].x, 240.0f - outline->point[i].y};
	}
	if(!buildRasterJoins(points, joins, count, true)) return;
	points[count] = points[0];
	joins[count] = joins[0];
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int layer = 0; layer < 3; layer++) {
		indigoPoint_t shifted[25];
		for(int i = 0; i <= count; i++) {
			/* The joins point along the outline's outward normal. */
			float length = fastSqrt(joins[i].x * joins[i].x + joins[i].y * joins[i].y);
			float nx = length > 0.0f ? joins[i].x / length : 0.0f;
			float ny = length > 0.0f ? joins[i].y / length : 0.0f;
			float lit = nx * lightX + ny * lightY;
			lit = lit <= 0.0f ? 0.0f : lit * lit;
			colors[i] = layers[layer].color;
			colors[i].a = (u8)(255.0f * fminf(1.0f, layers[layer].alpha * lit * strength) + 0.5f);
			shifted[i] = (indigoPoint_t) {points[i].x + nx * layers[layer].offset,
				points[i].y + ny * layers[layer].offset};
		}
		drawRasterStroke(shifted, joins, colors, count + 1, profile, 2);
	}
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

/* Bloom: the finished cube copied again, blurred by five taps, cut to what
 * is brighter than the glass itself, and screened back over it. Highlights,
 * glints and the icons' strokes glow; the tinted body does not. Screening
 * (the frame gains glow * (1 - frame)) rather than adding keeps a face that
 * turns through the key light's reflection, bright across its whole width,
 * from burning to white and hiding its icon for a few frames. */
static void drawGlassBloom(float left, float top, float right, float bottom,
		float strength)
{
	static const float taps[GLASS_BLOOM_TAPS][2] = {
		{0.0f, 0.0f}, {-3.0f, -2.0f}, {3.0f, -2.0f}, {-3.0f, 2.0f}, {3.0f, 2.0f}
	};
	GXColor glow = {220, 208, 255, 255};
	float sScale, tScale, sOffset, tOffset;

	/* The taps reach 3 texels (6 pixels) round each point, and bilinear
	 * filtering one more. */
	if(strength <= 0.01f || !glassCopyFrame(UIStage_FrameX(left) - 12.0f,
		top - 12.0f, UIStage_FrameX(right) + 12.0f, bottom + 12.0f)) return;
	sScale = glassCopyS();
	tScale = glassCopyT();
	sOffset = glassCopyS0();
	tOffset = glassCopyT0();
	left = fmaxf(UIStage_Left(), left); top = fmaxf(0.0f, top);
	right = fminf(UIStage_Right(), right); bottom = fminf(480.0f, bottom);
	if(right - left < 2.0f || bottom - top < 2.0f) return;
	setupRasterPipeline();
	GX_SetNumTexGens(GLASS_BLOOM_TAPS);
	for(int tap = 0; tap < GLASS_BLOOM_TAPS; tap++) {
		Mtx offset;
		guMtxIdentity(offset);
		/* Offsets in copy texels, about two screen pixels each. */
		offset[0][3] = taps[tap][0] / (float)glassCopyWidth;
		offset[1][3] = taps[tap][1] / (float)glassCopyHeight;
		GX_LoadTexMtxImm(offset, GX_TEXMTX0 + tap * 3u, GX_MTX2x4);
		GX_SetTexCoordGen((u16)(GX_TEXCOORD0 + tap), GX_TG_MTX2x4, GX_TG_TEX0,
			GX_TEXMTX0 + tap * 3u);
	}
	/* K0 weighs the centre tap, K1 each of the four around it, K2 is the
	 * brightness the glass body already reaches, which does not glow. It is
	 * the glass's own hue, so a lilac highlight blooms lilac rather than
	 * pink, and it turns with Menu Color like everything else. */
	GXColor threshold = {150, 136, 196, 255};

	UIColor_Apply(&threshold.r, &threshold.g, &threshold.b);
	GX_SetTevKColor(GX_KCOLOR0, (GXColor) {92, 92, 92, 255});
	GX_SetTevKColor(GX_KCOLOR1, (GXColor) {41, 41, 41, 255});
	GX_SetTevKColor(GX_KCOLOR2, threshold);
	GX_SetNumTevStages(GLASS_BLOOM_TAPS + 2);
	for(int tap = 0; tap < GLASS_BLOOM_TAPS; tap++) {
		u8 stage = (u8)(GX_TEVSTAGE0 + tap);
		GX_SetTevOrder(stage, (u8)(GX_TEXCOORD0 + tap), GX_TEXMAP0, GX_COLOR0A0);
		GX_SetTevKColorSel(stage, tap == 0 ? GX_TEV_KCSEL_K0 : GX_TEV_KCSEL_K1);
		GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST,
			tap == 0 ? GX_CC_ZERO : GX_CC_CPREV);
		GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
		GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
		GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	}
	GX_SetTevOrder(GX_TEVSTAGE5, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K2);
	GX_SetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_ONE, GX_CC_KONST, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE5, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE5, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE6, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	/* A screen has no source alpha, so the strength rides in the colour, and
	 * the stage doubles it back: screening gives the bright strokes less
	 * than adding did, and at rest the glow should read as before. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_INVDSTCLR, GX_BL_ONE, GX_LO_CLEAR);
	UIColor_Apply(&glow.r, &glow.g, &glow.b);
	strength = 0.8f * fminf(1.0f, strength);
	glow.r = (u8)(glow.r * strength + 0.5f);
	glow.g = (u8)(glow.g * strength + 0.5f);
	glow.b = (u8)(glow.b * strength + 0.5f);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32(left, top, 0.0f);
		GX_Color4u8(glow.r, glow.g, glow.b, glow.a);
		GX_TexCoord2f32(UIStage_FrameX(left) * sScale - sOffset, top * tScale - tOffset);
		GX_Position3f32(right, top, 0.0f);
		GX_Color4u8(glow.r, glow.g, glow.b, glow.a);
		GX_TexCoord2f32(UIStage_FrameX(right) * sScale - sOffset, top * tScale - tOffset);
		GX_Position3f32(right, bottom, 0.0f);
		GX_Color4u8(glow.r, glow.g, glow.b, glow.a);
		GX_TexCoord2f32(UIStage_FrameX(right) * sScale - sOffset, bottom * tScale - tOffset);
		GX_Position3f32(left, bottom, 0.0f);
		GX_Color4u8(glow.r, glow.g, glow.b, glow.a);
		GX_TexCoord2f32(UIStage_FrameX(left) * sScale - sOffset, bottom * tScale - tOffset);
	GX_End();
	setupRasterPipeline();
}

/* The silhouette's box: stage x, frame y. */
static void cubeOutlineBox(const cubeOutline_t *outline, float *left, float *top,
		float *right, float *bottom)
{
	*left = *top = 1e9f;
	*right = *bottom = -1e9f;
	for(int i = 0; i < outline->count; i++) {
		float x = 320.0f + outline->point[i].x;
		float y = 240.0f - outline->point[i].y;
		if(x < *left) *left = x;
		if(x > *right) *right = x;
		if(y < *top) *top = y;
		if(y > *bottom) *bottom = y;
	}
}

static void drawCube(const uiSceneFrame_t *scene, float seconds, bool animated,
		const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
		const int icons[UI_HOME_FACE_COUNT], bool screenGlass)
{
	static const GXColor backGlassColors[6] = {
		{61, 46, 132, 14}, {72, 55, 151, 18}, {111, 88, 193, 25},
		{62, 47, 136, 17}, {153, 135, 220, 34}, {92, 71, 174, 20}
	};
	static const GXColor frontGlassColors[6] = {
		{69, 54, 145, 24}, {82, 65, 166, 34}, {146, 124, 225, 58},
		{74, 58, 151, 31}, {219, 207, 255, 78}, {119, 96, 203, 42}
	};
	static const GXColor bevelColors[6] = {
		{25, 18, 70, 88}, {132, 108, 218, 98}, {196, 180, 255, 134},
		{65, 48, 143, 84}, {235, 228, 255, 154}, {132, 108, 218, 98}
	};
	/* Glass tints for the pass being drawn, and the bevels' edge tints near
	 * and far. */
	GXColor lit[6], edge[6], rear[6];
	cubeRasterTransform_t raster;
	cubeSurfaceQuad_t shell[6], strips[12], corners[8];
	/* The bevels and corners in the tint passes' seamless form. Static: too
	 * large for the video thread's stack. */
	static cubeSurfaceQuad_t bevels[36], fans[72];
	static const indigoPoint_t pane[4] = {
		{-0.72f, -0.72f}, {-0.72f, 0.72f}, {0.72f, 0.72f}, {0.72f, -0.72f}
	};
	cubeOutline_t shellOutline;
	float boxLeft, boxTop, boxRight, boxBottom;
	const float outer = 1.0f;
	const float inset = 0.78f;
	float strength = glassSceneStrength(scene);
	bool refract;

	setupCubePipeline(scene, seconds, animated, &raster);
	litCubeTints(&raster, bevelColors, edge);
	buildChamferStrips(strips, outer, inset, edge, NULL);
	buildCubeCorners(corners, outer, inset);
	/* Seen through the glass, the far bevels and corners are the cube's rear
	 * frame: at half the near ones' opacity they read as depth, not as a
	 * second wireframe. */
	for(int face = 0; face < 6; face++) {
		rear[face] = edge[face];
		rear[face].a /= 2;
	}
	litCubeTints(&raster, backGlassColors, lit);
	buildCubeFaces(shell, outer, inset, lit);
	buildChamferStrips(bevels, outer, inset, rear, lit);
	buildCornerFans(fans, outer, inset, rear, lit);
	buildCubeOutline(&raster, shell, &shellOutline);
	cubeOutlineBox(&shellOutline, &boxLeft, &boxTop, &boxRight, &boxBottom);
	if(shellOutline.count >= 3 &&
		(boxRight <= UIStage_Left() || boxLeft >= UIStage_Right())) {
		/* Parked beside the stage (Grid and Spotlight in 4:3): none of it
		 * shows. The Library emblem still follows the pad. */
		controllerPose_t pose;
		controllerPose(pad, seconds, animated, &controllerIdle, &pose);
		return;
	}

	/* Far structural surfaces must precede the transparent shell; otherwise
	 * rear rails and caps visibly composite across the front pane. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	drawCubeSurfacePass(&raster, &shellOutline, bevels, 36, GX_CULL_FRONT);
	drawCubeSurfacePassVertices(&raster, &shellOutline, fans, 72, 3, GX_CULL_FRONT);

	/* Convex glass is rendered back-to-front with depth writes disabled. */
	GX_SetCullMode(GX_CULL_FRONT);
	drawCubeSurfacePass(&raster, &shellOutline, shell, 6, GX_CULL_FRONT);
	/* Every semantic lateral face has the same smoked plate set into the
	 * glass, so no destination becomes a blank reverse side of the Library
	 * artwork. It is dark: each face's icon, glowing on the glass above it,
	 * reads against it. It is behind the copy, so the front glass bends it
	 * like an inlay. Its edge fades over one pixel, as an icon's does. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	{
		Mtx identity;
		guMtxIdentity(identity);
		GX_LoadPosMtxImm(identity, GX_PNMTX0);
		GX_SetCullMode(GX_CULL_NONE);
		for(int face = 0; face < UI_HOME_FACE_COUNT; face++)
			drawFacePolygon(&raster, face, pane, 4, 0.86f, (GXColor) {12, 9, 34, 132});
		GX_LoadPosMtxImm(raster.model, GX_PNMTX0);
	}

	/* Everything behind the front glass so far (backdrop, halo, far bevels,
	 * the plates and the back glass) is copied and bent through the
	 * camera-facing outer glass, as a solid glass block shows its own far
	 * edges: magnified across the faces, wrapped round the bevels and parted
	 * into colour where the bend is strongest, with a faint indigo body. The
	 * front tint, near bevels, reflections and icons then sit on the glass,
	 * sharp. */
	refract = screenGlass && strength > 0.01f && shellOutline.count >= 3 &&
		glassCopyFrame(UIStage_FrameX(boxLeft) - 16.0f, boxTop - 16.0f,
			UIStage_FrameX(boxRight) + 16.0f, boxBottom + 16.0f);
	{
		float radius = raster.scaleY * scene->cubeScale / -CUBE_CAMERA_Z;
		glassRefraction_t glass = {
			320.0f + raster.model[0][3] * raster.scaleX / -raster.model[2][3],
			240.0f - raster.model[1][3] * raster.scaleY / -raster.model[2][3],
			0.46f * radius, 0.05f, 0.24f,
			refract ? glassCopyS() : 0.0f, refract ? glassCopyT() : 0.0f,
			refract ? glassCopyS0() : 0.0f, refract ? glassCopyT0() : 0.0f,
			/* The layer fades in with the boot's light, never all at once. */
			{116, 110, 140, (u8)(255.0f * glassSmoothstep(BOOT_CUBE_HANDOFF, 1.0f,
				scene->introProgress) + 0.5f)}
		};
		if(refract) {
			setupGlassRefractionPipeline();
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
			GX_SetCullMode(GX_CULL_BACK);
			drawGlassRefraction(&raster, &glass, shell, 6, 4, outer);
			drawGlassRefraction(&raster, &glass, strips, 12, 4, outer);
			drawGlassRefraction(&raster, &glass, corners, 8, 3, outer);
			restoreCubeRaster();
		}
	}

	GX_SetCullMode(GX_CULL_BACK);
	litCubeTints(&raster, frontGlassColors, lit);
	buildCubeFaces(shell, outer, inset, lit);
	buildChamferStrips(bevels, outer, inset, edge, lit);
	buildCornerFans(fans, outer, inset, edge, lit);
	drawCubeSurfacePass(&raster, &shellOutline, shell, 6, GX_CULL_BACK);

	/* Only near structural surfaces composite in front of the shell. Corner
	 * fans close the chamfer and share its camera-facing depth policy. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	drawCubeSurfacePass(&raster, &shellOutline, bevels, 36, GX_CULL_BACK);
	drawCubeSurfacePassVertices(&raster, &shellOutline, fans, 72, 3, GX_CULL_BACK);

	/* The outer glass mirrors the studio over everything the shell shows,
	 * additively. While Home rests, the studio turns a little about the view
	 * axis and back, so the highlights drift and the glass is never still. */
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	GX_SetCullMode(GX_CULL_BACK);
	setupGlassReflectionPipeline(animated ? GLASS_STUDIO_DRIFT * scene->homeIdleBlend *
		sinf(seconds * GLASS_STUDIO_DRIFT_RATE) : 0.0f);
	drawGlassReflection(&raster, shell, 6, 4, outer);
	drawGlassReflection(&raster, strips, 12, 4, outer);
	drawGlassReflection(&raster, corners, 8, 3, outer);
	restoreCubeRaster();
	/* Light leaving the finished glass: its bloom first, then the icons, so
	 * their strokes stay sharp instead of blurring with it (Spencer found
	 * them hard to read), then the rim. Each icon's shadow lands on the
	 * glass between the bloom and the icon. */
	bool light = strength > 0.01f && shellOutline.count >= 3;
	if(light && screenGlass) {
		drawGlassBloom(boxLeft - 28.0f, boxTop - 28.0f, boxRight + 28.0f,
			boxBottom + 28.0f, 0.78f * strength);
		loadCubeProjection();
		restoreCubeRaster();
	}
	drawFaceShadows(seconds, animated, clock, pad, icons, &raster, strength);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	drawFaceIcons(seconds, animated, clock, pad, icons, &raster);
	if(screenGlass) {
		drawFacePictures(&raster);
	}
	if(light) {
		setupRasterPipeline();
		drawGlassRim(&shellOutline, strength);
	}
}

static int layerColors[UI_COLOR_LAYERS];

void IndigoBackground_SetColors(const int colors[UI_COLOR_LAYERS])
{
	for(int i = 0; i < UI_COLOR_LAYERS; i++) {
		layerColors[i] = colors[i];
	}
}

void IndigoBackground_Draw(float seconds, bool backdropAnimated,
	bool cubeAnimated, const uiSceneFrame_t *scene,
	const uiClockFrame_t *clock, const indigoPadFrame_t *pad,
	const int icons[UI_HOME_FACE_COUNT])
{
	bool backdropMotionActive = backdropAnimated && scene->visible;
	bool cubeMotionActive = cubeAnimated && scene->visible;
	float drift = backdropMotionActive ? sinf(seconds * 0.12f) * 5.0f : 0.0f;
	float centerX = 320.0f + (scene->cubeX * 112.0f);
	float centerY = 238.0f - (scene->cubeY * 112.0f);
	float orbitScale = 0.70f + (scene->cubeScale * 0.30f);
	float orbitStrength = scene->orbitStrength < 0.0f ? 0.0f :
		(scene->orbitStrength > 1.0f ? 1.0f : scene->orbitStrength);
	/* Home and Source quiet the waves; the change eases with the cube
	 * rather than stepping when the scene does. */
	float decorativeStrength = orbitStrength +
		(orbitStrength * HOME_DECORATIVE_STRENGTH - orbitStrength) *
		scene->homeDecorativeBlend;

	/* The turned faces' icon pictures go first: the backdrop paints over
	 * the corner of the frame they are drawn in. */
	if(scene->visible && scene->introProgress >= BOOT_CUBE_HANDOFF) {
		UIColor_Select(layerColors[UI_COLOR_LAYER_MENU]);
		renderFacePictures(scene, seconds, cubeMotionActive, clock, pad, icons);
	}
	else {
		facePictureCount = 0;
	}
	setupRasterPipeline();
	/* The backdrop and the waves have colors of their own; the cube and
	 * everything after it take the menus'. */
	UIColor_Select(layerColors[UI_COLOR_LAYER_BACKDROP]);
	drawIndigoWash(255, UIColor_BackdropShade(layerColors[UI_COLOR_LAYER_BACKDROP]));
	drawGlobeGrid(320.0f, 212.0f, drift * 0.18f);
	/* Waves off: none. On again, they are where they would have drifted to. */
	if(scene->visible && wavesShown) {
		UIColor_Select(layerColors[UI_COLOR_LAYER_WAVES]);
		drawSilkWaves(waveClock(seconds), backdropMotionActive, decorativeStrength);
	}
	UIColor_Select(layerColors[UI_COLOR_LAYER_MENU]);
	if(!scene->visible) {
		return;
	}
	drawRadialDisc(centerX, centerY + 132.0f * orbitScale,
		104.0f * orbitScale, 14.0f * orbitScale,
		(GXColor) {3, 2, 12, (u8)(92.0f * orbitStrength)},
		(GXColor) {3, 2, 12, 0});
	if(scene->introProgress >= BOOT_CUBE_HANDOFF) {
		drawCube(scene, seconds, cubeMotionActive, clock, pad, icons, true);
	}
}

/* Memory Cards' stage: the backdrop's wash and, as the IPL's Memory Card
 * screen has, graph paper (as much as paper, 0 .. 1), a line every half a
 * cube's cell through the cubes' vanishing point, in the backdrop's colors;
 * whatever follows takes the menus'. One batch of lines a pixel wide in
 * either screen shape. While the screen opens and leaves, the Home cube
 * where Home left it hands over: handover 1 is where it was, going back
 * into the distance toward 0, the boot's form of it with no glass to copy
 * the frame. */
#define SAVES_GRID_PITCH 28.0f
#define SAVES_HANDOVER_DISTANCE 9.0f
void IndigoBackground_DrawSavesBackdrop(float paper, float handover,
	float seconds, bool animated, const uiSceneFrame_t *scene,
	const uiClockFrame_t *clock, const int icons[UI_HOME_FACE_COUNT])
{
	/* Too large for the video thread's stack. */
	static uiSceneFrame_t away;
	const GXColor line = {70, 66, 112, (u8)(90.0f * (paper < 0.0f ? 0.0f :
		paper > 1.0f ? 1.0f : paper) + 0.5f)};
	float left = UIStage_Left(), right = UIStage_Right();
	float width = UIStage_PixelWidth();
	/* Truncating toward zero keeps each end's line on the stage. */
	int firstColumn = (int)((left - 320.0f) / SAVES_GRID_PITCH);
	int lastColumn = (int)((right - 320.0f) / SAVES_GRID_PITCH);
	int firstRow = (int)(-224.0f / SAVES_GRID_PITCH);
	int lastRow = (int)((480.0f - 224.0f) / SAVES_GRID_PITCH);
	int i;

	setupRasterPipeline();
	UIColor_Select(layerColors[UI_COLOR_LAYER_BACKDROP]);
	drawIndigoWash(255, UIColor_BackdropShade(layerColors[UI_COLOR_LAYER_BACKDROP]));
	if(line.a > 0) {
		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(4 * (lastColumn - firstColumn + 1 +
			lastRow - firstRow + 1)));
		for(i = firstColumn; i <= lastColumn; i++) {
			float x = 320.0f + (float)i * SAVES_GRID_PITCH;

			putVertex((indigoPoint_t) {x, 0.0f}, line);
			putVertex((indigoPoint_t) {x + width, 0.0f}, line);
			putVertex((indigoPoint_t) {x + width, 480.0f}, line);
			putVertex((indigoPoint_t) {x, 480.0f}, line);
		}
		for(i = firstRow; i <= lastRow; i++) {
			float y = 224.0f + (float)i * SAVES_GRID_PITCH;

			putVertex((indigoPoint_t) {left, y}, line);
			putVertex((indigoPoint_t) {right, y}, line);
			putVertex((indigoPoint_t) {right, y + 1.0f}, line);
			putVertex((indigoPoint_t) {left, y + 1.0f}, line);
		}
		GX_End();
	}
	UIColor_Select(layerColors[UI_COLOR_LAYER_MENU]);
	if(handover > 0.02f && scene != NULL && scene->visible) {
		away = *scene;
		away.introDistance += SAVES_HANDOVER_DISTANCE * (1.0f - handover);
		drawCube(&away, seconds, animated, clock, NULL, icons, false);
	}
}

void IndigoBackground_TrackPad(float seconds, bool animated,
	const indigoPadFrame_t *pad)
{
	controllerPose_t pose;

	controllerPose(pad, seconds, animated, &controllerIdle, &pose);
}

void IndigoBackground_DrawBootOverlay(float seconds, bool animated,
	const uiSceneFrame_t *scene, const uiClockFrame_t *clock,
	const int icons[UI_HOME_FACE_COUNT])
{
	float hidden;
	float reveal;
	u8 veilAlpha;

	/* Before menu activation, progress dialogs and any required prompts must
	 * remain visible. The boot veil belongs to the interactive scene only. */
	if(!scene->visible || scene->introProgress >= 1.0f) {
		return;
	}

	reveal = UIMotion_Smoothstep(scene->introProgress / BOOT_VEIL_LIFT);
	hidden = 1.0f - reveal;
	veilAlpha = (u8)(hidden * 255.0f);
	if(scene->visible && scene->introProgress < BOOT_CUBE_HANDOFF) {
		drawCube(scene, seconds, animated, clock, NULL, icons, false);
	}
	setupRasterPipeline();
	UIColor_Select(layerColors[UI_COLOR_LAYER_BACKDROP]);
	drawIndigoWash(veilAlpha, UIColor_BackdropShade(layerColors[UI_COLOR_LAYER_BACKDROP]));
	UIColor_Select(layerColors[UI_COLOR_LAYER_MENU]);
}
