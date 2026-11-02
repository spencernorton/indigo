/*
 * Frame budget: the cube renderer (gui/indigo_background.c), the hint
 * icons, Memory Cards' save cubes (gui/FrameBufferMagic.c, with
 * gui/ui_save_cubes.c), the Library's jewel case (gui/ui_jewel_case.c) and
 * the File Browser (with gui/ui_files.c), its words
 * through a font stand-in below, run against counting GX stubs, one steady
 * frame per scene, posed by the real scene module. It counts what a frame
 * costs the console: software maths on the CPU (Gekko has no square-root
 * instruction, so sqrtf is newlib's integer loop; sinf, cosf, expf, powf,
 * fminf and friends are calls too) and the GPU's work (vertices, primitives, the
 * pixels each EFB copy reads). Each scene prints one JSON line;
 * test_frame_budget.py compares them with frame_budget.json.
 *
 * hash covers what the frame shows: every vertex with some alpha, its screen
 * position to 1/64 pixel and its color. An optimization that changes no
 * pixel keeps it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

typedef struct {
	long sqrtCalls, trigCalls, minMaxCalls, vertices, begins, invalidations;
	double copyPixels;
	unsigned long long hash;
} frameCost_t;

static frameCost_t cost;
long stubStateCalls;

static float countSqrtf(float x) { cost.sqrtCalls++; return sqrtf(x); }
static float countSinf(float x) { cost.trigCalls++; return sinf(x); }
static float countCosf(float x) { cost.trigCalls++; return cosf(x); }
static float countTanf(float x) { cost.trigCalls++; return tanf(x); }
static float countTanhf(float x) { cost.trigCalls++; return tanhf(x); }
static float countAcosf(float x) { cost.trigCalls++; return acosf(x); }
static float countFmodf(float x, float y) { cost.trigCalls++; return fmodf(x, y); }
static float countCeilf(float x) { cost.trigCalls++; return ceilf(x); }
static float countFminf(float x, float y) { cost.minMaxCalls++; return fminf(x, y); }
static float countFmaxf(float x, float y) { cost.minMaxCalls++; return fmaxf(x, y); }
static float countExpf(float x) { cost.trigCalls++; return expf(x); }
static float countPowf(float x, float y) { cost.trigCalls++; return powf(x, y); }
static float countAtan2f(float y, float x) { cost.trigCalls++; return atan2f(y, x); }
#define sqrtf countSqrtf
#define sinf countSinf
#define cosf countCosf
#define tanf countTanf
#define tanhf countTanhf
#define acosf countAcosf
#define fmodf countFmodf
#define ceilf countCeilf
#define fminf countFminf
#define fmaxf countFmaxf
#define expf countExpf
#define powf countPowf
#define atan2f countAtan2f
#include "indigo_background.c"
#include "ui_hint_geometry.c"
#include "hint_source.c"	/* written by test_frame_budget.py */
#include "save_cubes_source.c"	/* and this */
/* The IPL font for the File Browser's words: 11 px a character at scale 1
 * and 24 px tall, each character a quad a pass, two passes for the medium
 * weight and one for the light, as drawStringWeighted sends them. */
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2
static int GetTextSizeInPixels(const char *text) { return 11 * (int)strlen(text); }
static int GetFontHeight(float scale) { return (int)(24.0f * scale); }
static void fontQuads(int x, int y, const char *text, float scale, int align,
	GXColor color, int passes)
{
	float left = (float)x - (float)align * (float)GetTextSizeInPixels(text) * scale / 2.0f;
	int pass, v;
	const char *c;

	for(pass = 2 - passes; pass < 2; pass++) {
		c = text;
		while(*c != '\0' && *c != '\n') {
			const char *end = c;
			while(*end != '\0' && *end != '\n' && end - c < 64) end++;
			GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)((end - c) * 4));
			for(; c < end; c++) {
				float x0 = left + (float)(c - text) * 11.0f * scale + (float)(1 - pass);
				for(v = 0; v < 4; v++) {
					GX_Position3f32(x0 + (((v & 1) ^ ((v & 2) >> 1)) ? 11.0f * scale : 0.0f),
						(float)y + ((v & 2) ? 12.0f : -12.0f) * scale, 0.0f);
					GX_Color4u8(color.r, color.g, color.b, pass == 0 ? color.a * 3 / 4 : color.a);
					GX_TexCoord2f32(0.0f, 0.0f);
				}
			}
			GX_End();
		}
	}
}
static void drawStringMediumUntinted(int x, int y, const char *text, float scale, int align,
	GXColor color)
{
	fontQuads(x, y, text, scale, align, color, 2);
}
static void drawStringMedium(int x, int y, const char *text, float scale, int align,
	GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	fontQuads(x, y, text, scale, align, color, 2);
}
static void drawString(int x, int y, const char *text, float scale, int align, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	fontQuads(x, y, text, scale, align, color, 1);
}
#include "files_source.c"	/* and this */
#include "ui_jewel_case.c"
#undef sqrtf
#undef sinf
#undef cosf
#undef tanf
#undef tanhf
#undef acosf
#undef fmodf
#undef ceilf
#undef fminf
#undef fmaxf
#undef expf
#undef powf
#undef atan2f

/* ---- GX: count, and project each vertex the way the console would ---- */
static Mtx position;
static Mtx44 projection;
static int orthographic;
static int primitive, declared, emitted;
static float lastX, lastY;
static u16 copyWidth, copyHeight;

void GX_LoadPosMtxImm(const float m[3][4], u32 index) { (void)index; memcpy(position, m, sizeof(Mtx)); }
void GX_LoadProjectionMtx(Mtx44 m, u8 type)
{
	memcpy(projection, m, sizeof(Mtx44));
	orthographic = type == GX_ORTHOGRAPHIC;
}
void GX_LoadTexMtxImm(Mtx m, u32 index, u8 type) { (void)m; (void)index; (void)type; }
void GX_SetNumTevStages(u8 count) { (void)count; stubStateCalls++; }
void GX_SetBlendMode(u8 a, u8 b, u8 c, u8 d) { (void)a; (void)b; (void)c; (void)d; stubStateCalls++; }
void GX_InvalidateTexAll(void) { cost.invalidations++; }
void GX_PixModeSync(void) {}
void GX_InitTexObj(GXTexObj *o, void *p, u16 w, u16 h, u8 f, u8 s, u8 t, u8 m)
{
	(void)o; (void)p; (void)w; (void)h; (void)f; (void)s; (void)t; (void)m;
}
void GX_InitTexObjLOD(GXTexObj *o, u8 a, u8 b, float c, float d, float e, u8 f, u8 g, u8 h)
{
	(void)o; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}
void GX_LoadTexObj(GXTexObj *o, u8 map) { (void)o; (void)map; }
void GX_SetTevKColor(u8 index, GXColor color) { (void)index; (void)color; }
void DCFlushRange(void *p, u32 n) { (void)p; (void)n; }
void GX_SetTexCopySrc(u16 l, u16 t, u16 w, u16 h) { (void)l; (void)t; copyWidth = w; copyHeight = h; }
void GX_SetTexCopyDst(u16 w, u16 h, u32 f, u8 m) { (void)w; (void)h; (void)f; (void)m; }
void GX_CopyTex(void *dst, u8 clear) { (void)dst; (void)clear; cost.copyPixels += (double)copyWidth * copyHeight; }

void GX_Begin(u8 type, u8 format, u16 count)
{
	(void)format;
	primitive = type;
	declared = count;
	emitted = 0;
	cost.begins++;
}

void GX_End(void)
{
	if(emitted != declared) {
		fprintf(stderr, "GX_Begin declared %d vertices, %d were sent (primitive 0x%x)\n",
			declared, emitted, primitive);
		exit(2);
	}
}

void GX_Position3f32(float x, float y, float z)
{
	float eye[3], clipX, clipY, w;
	int row;

	for(row = 0; row < 3; row++) {
		eye[row] = position[row][0] * x + position[row][1] * y +
			position[row][2] * z + position[row][3];
	}
	clipX = projection[0][0] * eye[0] + projection[0][1] * eye[1] +
		projection[0][2] * eye[2] + projection[0][3];
	clipY = projection[1][0] * eye[0] + projection[1][1] * eye[1] +
		projection[1][2] * eye[2] + projection[1][3];
	w = orthographic ? 1.0f : -eye[2];
	lastX = 320.0f + 320.0f * clipX / w;
	lastY = 240.0f - 240.0f * clipY / w;
	emitted++;
	cost.vertices++;
}

static void hashBytes(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t i;

	for(i = 0; i < size; i++) {
		cost.hash ^= bytes[i];
		cost.hash *= 1099511628211ULL;
	}
}

void GX_Color4u8(u8 r, u8 g, u8 b, u8 a)
{
	long point[2];
	u8 rgba[4] = {r, g, b, a};

	if(a == 0) {
		return;
	}
	point[0] = lroundf(lastX * 64.0f);
	point[1] = lroundf(lastY * 64.0f);
	hashBytes(point, sizeof(point));
	hashBytes(rgba, sizeof(rgba));
}

void GX_TexCoord2f32(float s, float t) { (void)s; (void)t; }

/* ---- display lists: what one holds counts each time it is called ---- */
static void *recording;
static long recordedVertices, recordedBegins;
static struct { void *list; long vertices, begins; } lists[4];

void GX_SetArray(u32 attr, void *ptr, u8 stride) { (void)attr; (void)ptr; (void)stride; stubStateCalls++; }
void GX_Position1x16(u16 index) { (void)index; emitted++; cost.vertices++; }
void GX_Normal1x16(u16 index) { (void)index; }
void GX_BeginDispList(void *list, u32 size)
{
	(void)size;
	recording = list;
	recordedVertices = cost.vertices;
	recordedBegins = cost.begins;
}
u32 GX_EndDispList(void)
{
	int i;

	for(i = 0; i < 4 && lists[i].list != NULL && lists[i].list != recording; i++) {}
	if(i == 4) {
		fprintf(stderr, "more than four display lists\n");
		exit(2);
	}
	lists[i].list = recording;
	lists[i].vertices = cost.vertices - recordedVertices;
	lists[i].begins = cost.begins - recordedBegins;
	/* Recording one draws nothing. */
	cost.vertices = recordedVertices;
	cost.begins = recordedBegins;
	recording = NULL;
	return 32u;
}
void GX_CallDispList(void *list, u32 size)
{
	(void)size;
	for(int i = 0; i < 4; i++) {
		if(lists[i].list == list) {
			cost.vertices += lists[i].vertices;
			cost.begins += lists[i].begins;
		}
	}
}
void GX_SetTevColor(u8 reg, GXColor color) { (void)reg; (void)color; }
void DCInvalidateRange(void *p, u32 n) { (void)p; (void)n; }

/* ---- gu ---- */
void guMtxIdentity(Mtx m)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = m[1][1] = m[2][2] = 1.0f;
}
void guMtxCopy(Mtx a, Mtx b) { memcpy(b, a, sizeof(Mtx)); }
void guMtxConcat(Mtx a, Mtx b, Mtx out)
{
	Mtx n = {{0}};
	int i, j, k;

	for(i = 0; i < 3; i++) {
		for(j = 0; j < 4; j++) {
			for(k = 0; k < 3; k++) n[i][j] += a[i][k] * b[k][j];
		}
		n[i][3] += a[i][3];
	}
	memcpy(out, n, sizeof(Mtx));
}
void guMtxRotAxisRad(Mtx m, const guVector *axis, float radians)
{
	float c = cosf(radians), s = sinf(radians), t = 1.0f - c;
	float x = axis->x, y = axis->y, z = axis->z;

	guMtxIdentity(m);
	m[0][0] = t * x * x + c; m[0][1] = t * x * y - s * z; m[0][2] = t * x * z + s * y;
	m[1][0] = t * x * y + s * z; m[1][1] = t * y * y + c; m[1][2] = t * y * z - s * x;
	m[2][0] = t * x * z - s * y; m[2][1] = t * y * z + s * x; m[2][2] = t * z * z + c;
}
/* libogc's works out a sine and a cosine: counted as the maths above is. */
void guMtxRotRad(Mtx m, char axis, float radians)
{
	float c = cosf(radians), s = sinf(radians);
	int a = axis == 'x' ? 0 : axis == 'y' ? 1 : 2, b = (a + 1) % 3, d = (a + 2) % 3;

	cost.trigCalls += 2;
	guMtxIdentity(m);
	m[b][b] = c; m[b][d] = -s;
	m[d][b] = s; m[d][d] = c;
}
void guMtxScale(Mtx m, float x, float y, float z)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = x; m[1][1] = y; m[2][2] = z;
}
void guMtxScaleApply(Mtx src, Mtx dst, float x, float y, float z)
{
	float scale[3] = {x, y, z};
	int i, j;

	for(i = 0; i < 3; i++) {
		for(j = 0; j < 4; j++) dst[i][j] = src[i][j] * scale[i];
	}
}
void guMtxTransApply(Mtx src, Mtx dst, float x, float y, float z)
{
	memcpy(dst, src, sizeof(Mtx));
	dst[0][3] += x; dst[1][3] += y; dst[2][3] += z;
}
void guPerspective(Mtx44 p, float fovy, float aspect, float n, float f)
{
	float c = 1.0f / tanf(fovy * 0.5f * 3.14159265f / 180.0f);

	memset(p, 0, sizeof(Mtx44));
	p[0][0] = c / aspect; p[1][1] = c;
	p[2][2] = -n / (f - n); p[2][3] = -(f * n) / (f - n); p[3][2] = -1.0f;
}
void guOrtho(Mtx44 p, float t, float b, float l, float r, float n, float f)
{
	memset(p, 0, sizeof(Mtx44));
	p[0][0] = 2.0f / (r - l); p[0][3] = -(r + l) / (r - l);
	p[1][1] = 2.0f / (t - b); p[1][3] = -(t + b) / (t - b);
	p[2][2] = -1.0f / (f - n); p[2][3] = -f / (f - n); p[3][3] = 1.0f;
}

/* ---- scenes ---- */
#define DT (1.0f / 60.0f)

static float seconds;
static uiClockFrame_t clock;
static const indigoPadFrame_t pad = {true, 0, 0, 0, 0, 0};
static int icons[UI_HOME_FACE_COUNT] = {0, 0, 0, 0, -1, -1, -1, -1, -1};

static void settle(int frames)
{
	while(frames-- > 0) {
		UIScene_Update(DT, UI_MOTION_FULL);
		seconds += DT;
	}
}

/* The Library's jewel case, drawn over the backdrop when it is on stage. */
static const uiJewelCaseShot_t *jewelShots;
static int jewelCount, jewelDirection;

static void drawFrame(bool bootOverlay)
{
	IndigoBackground_Draw(seconds, true, true, UIScene_Frame(), &clock, &pad, icons);
	if(jewelCount > 0) {
		UIJewelCase_Draw(jewelShots, jewelCount, jewelDirection, DT, 0.0f, 0.0f, UI_MOTION_FULL, true);
	}
	if(bootOverlay) {
		IndigoBackground_DrawBootOverlay(seconds, true, UIScene_Frame(), &clock, icons);
	}
}

/* One steady frame: a warm-up draw (the studio map bakes on the first),
 * then the measured one. */
static void measure(const char *name, bool bootOverlay)
{
	drawFrame(bootOverlay);
	settle(1);
	memset(&cost, 0, sizeof(cost));
	cost.hash = 1469598103934665603ULL;
	stubStateCalls = 0;
	drawFrame(bootOverlay);
	printf("{\"scene\": \"%s\", \"sqrtf\": %ld, \"trig\": %ld, \"minmax\": %ld, "
		"\"vertices\": %ld, \"begins\": %ld, \"copy_pixels\": %.0f, \"state\": %ld, "
		"\"invalidations\": %ld, \"hash\": \"%016llx\"}\n", name, cost.sqrtCalls, cost.trigCalls,
		cost.minMaxCalls, cost.vertices, cost.begins, cost.copyPixels,
		stubStateCalls, cost.invalidations, cost.hash);
}

static void startScene(void)
{
	UIScene_Reset();
	UIScene_Activate();
	settle(600);
}

/* Home's cube some turns from the Library face, settled some frames. */
static void homeTurned(int turns, int framesAfter)
{
	static const uiHomeCapabilities_t capabilities = {true, true, false};
	uiHomeState_t state;
	int i;

	startScene();
	UIHome_Init(&state, capabilities);
	for(i = 0; i < turns; i++) {
		UIHome_Apply(&state, UI_HOME_INPUT_RIGHT, capabilities);
	}
	UIScene_Request(UI_SCENE_HOME);
	UIScene_RequestHome(&state);
	settle(framesAfter);
}

static void home(const char *name, int turns, int framesAfter)
{
	homeTurned(turns, framesAfter);
	measure(name, false);
}

static void library(const char *name, uiGameflowLayout_t layout, bool wide)
{
	startScene();
	UIScene_Request(UI_SCENE_LIBRARY);
	UIScene_RequestLibraryLayout(layout);
	settle(600);
	UIStage_SetWide(wide);
	measure(name, false);
	UIStage_SetWide(false);
}

/* The Jewel Case layout: the selected game's case at rest, its disc
 * printed with its cover, or bare with the rainbow; and half way through a
 * move, the case leaving and the one arriving. */
static void jewelCase(const char *name, bool printed, bool moving, bool wide)
{
	static GXTexObj cover;
	const uiJewelCaseShot_t shots[2] = {
		{moving ? 0.5f : 0.0f, 20u, printed ? &cover : NULL, {0.0f, 0.125f, 0.75f, 0.875f}, 1.0f, 1.0f},
		{-0.5f, 19u, &cover, {0.0f, 0.125f, 0.75f, 0.875f}, 1.0f, 1.0f}
	};

	startScene();
	UIScene_Request(UI_SCENE_LIBRARY);
	UIScene_RequestLibraryLayout(UI_GAMEFLOW_LAYOUT_JEWEL_CASE);
	settle(600);
	if(!UIJewelCase_Prepare()) {
		fprintf(stderr, "no memory for the jewel case\n");
		exit(2);
	}
	jewelShots = shots;
	jewelCount = moving ? 2 : 1;
	jewelDirection = moving ? 1 : 0;
	UIStage_SetWide(wide);
	measure(name, false);
	UIStage_SetWide(false);
	jewelCount = 0;
}

static void scene(const char *name, uiSceneId_t id)
{
	startScene();
	UIScene_Request(id);
	settle(600);
	measure(name, false);
}

static void hintRoundRect(void)
{
	/* One call to warm up, then the steady call a frame makes. */
	_HintRoundRect(100.0f, 100.0f, 22.0f, 22.0f, 11.0f, (GXColor) {0, 170, 122, 255});
	memset(&cost, 0, sizeof(cost));
	cost.hash = 1469598103934665603ULL;
	stubStateCalls = 0;
	_HintRoundRect(100.0f, 100.0f, 22.0f, 22.0f, 11.0f, (GXColor) {0, 170, 122, 255});
	printf("{\"scene\": \"hint-round-rect\", \"sqrtf\": %ld, \"trig\": %ld, \"minmax\": %ld, "
		"\"vertices\": %ld, \"begins\": %ld, \"copy_pixels\": 0, \"state\": %ld, "
		"\"invalidations\": %ld, \"hash\": \"%016llx\"}\n", cost.sqrtCalls, cost.trigCalls, cost.minMaxCalls,
		cost.vertices, cost.begins, stubStateCalls, cost.invalidations, cost.hash);
}

/* Memory Cards: the backdrop and both stacks, every cell a save with an
 * icon, a sixth of a second into scrolling a row with the focus grown: the
 * most cubes a frame draws. With them, a copy's cube mid-flight, an erased
 * one's pieces, or the opening a quarter of a second in, the Home cube
 * going back as the cubes start out of the middle. */
enum { CARDS_STEADY, CARDS_COPY, CARDS_ERASE, CARDS_OPENING };

static void memoryCards(const char *name, bool wide, int what)
{
	static u8 texels[UI_SAVES_ICON_FRAMES * UI_SAVES_ICON_BYTES] __attribute__((aligned(32)));
	static uiSavesArt_t art;
	static uiSaveCubesGrid_t grid;
	static uiSaveCubesMotion_t motion;
	static saveCubesDraw_t draw;
	int count = 0, floating = 0, frame, s, k;

	art.steps = 1;
	art.stepHold[0] = 1;
	art.period = 1;
	memset(&motion, 0, sizeof(motion));
	memset(&grid, 0, sizeof(grid));
	for(s = 0; s < UI_SAVE_CUBES_STACKS; s++) {
		grid.stack[s].cells = 128;
		grid.stack[s].first = 4;
		grid.stack[s].listing = (u32)s + 1u;
		for(k = 0; k < UI_SAVE_CUBES_DRAWN; k++) {
			grid.stack[s].cell[k].kind = UI_SAVE_CUBES_KIND_SAVE;
			grid.stack[s].cell[k].texels = texels;
			grid.stack[s].cell[k].art = &art;
		}
	}
	grid.focusStack = 0;
	grid.focusCell = 4 * UI_SAVE_CUBES_COLUMNS + 1;
	UIStage_SetWide(wide);
	if(what == CARDS_OPENING) {
		/* Home's cube on its System side, where Memory Cards opens from. */
		homeTurned(3, 600);
		for(frame = 0; frame < 15; frame++) {
			UISaveCubes_Frame(&motion, &grid, DT, UI_MOTION_FULL, draw.cubes, &floating);
			settle(1);
		}
	}
	else {
		for(frame = 0; frame < 600; frame++) {
			UISaveCubes_Frame(&motion, &grid, DT, UI_MOTION_FULL, draw.cubes, &floating);
		}
		grid.stack[0].first = grid.stack[1].first = 5;
		grid.focusCell = 5 * UI_SAVE_CUBES_COLUMNS + 1;
		grid.op.kind = what == CARDS_COPY ? UI_SAVE_CUBES_OP_COPY :
			what == CARDS_ERASE ? UI_SAVE_CUBES_OP_ERASE : UI_SAVE_CUBES_OP_NONE;
		grid.op.serial = 1;
		grid.op.fromCell = grid.focusCell;
		grid.op.toCell = 6 * UI_SAVE_CUBES_COLUMNS + 2;
		grid.op.cube = grid.stack[0].cell[8];
		for(frame = 0; frame < 10; frame++) {
			UISaveCubes_Frame(&motion, &grid, DT, UI_MOTION_FULL, draw.cubes, &floating);
		}
		if(what == CARDS_ERASE) {
			grid.op.phase = UI_SAVE_CUBES_LAND;
			for(frame = 0; frame < 6; frame++) {
				UISaveCubes_Frame(&motion, &grid, DT, UI_MOTION_FULL, draw.cubes, &floating);
			}
		}
	}
	memset(&cost, 0, sizeof(cost));
	cost.hash = 1469598103934665603ULL;
	stubStateCalls = 0;
	count = UISaveCubes_Frame(&motion, &grid, DT, UI_MOTION_FULL, draw.cubes, &floating);
	IndigoBackground_DrawSavesBackdrop(motion.paper, motion.handover, seconds, true,
		UIScene_Frame(), &clock, icons);
	_SaveCubesShades(&draw);
	draw.invalidated = false;
	_SaveCubesEmit(&draw, 0, floating);
	for(k = floating; k < count; k++) {
		_SaveCubesEmit(&draw, k, k + 1);
	}
	printf("{\"scene\": \"%s\", \"sqrtf\": %ld, \"trig\": %ld, \"minmax\": %ld, "
		"\"vertices\": %ld, \"begins\": %ld, \"copy_pixels\": %.0f, \"state\": %ld, "
		"\"invalidations\": %ld, \"hash\": \"%016llx\"}\n", name, cost.sqrtCalls, cost.trigCalls,
		cost.minMaxCalls, cost.vertices, cost.begins, cost.copyPixels,
		stubStateCalls, cost.invalidations, cost.hash);
	UIStage_SetWide(false);
}

/* The File Browser over Memory Cards' backdrop: both panes full of rows
 * of every kind, each with its cube and the longest name its column holds
 * (Redump names, cut in the middle), the focus bars, both scroll tracks,
 * the chips and boxes, the info bar with its size box and two lines, and
 * the hint line with five round buttons. The banner is one texture. */
static int filesMeasure(const char *text) { return GetTextSizeInPixels(text); }

/* What else is open over the panes. */
enum { FILES_PLAIN = 0, FILES_ACTIONS, FILES_COPY, FILES_MESSAGE };

static void files(const char *name, bool wide, int active, int menu, int extra)
{
	static const u8 kinds[UI_FILES_ROWS] = {
		UI_FILES_KIND_PARENT, UI_FILES_KIND_FOLDER, UI_FILES_KIND_DISC,
		UI_FILES_KIND_DISC, UI_FILES_KIND_DISC_COMPRESSED, UI_FILES_KIND_PROGRAM,
		UI_FILES_KIND_FIRMWARE, UI_FILES_KIND_MUSIC
	};
	static const char *const long_name =
		"Copper Orchard - The Long Way Round of the Seven Valleys (USA, Europe) "
		"(En,Fr,De,Es,It) (Rev 1) (Disc 1).iso";
	static uiFilesSnapshot_t snapshot;
	const float focusY[UI_FILES_PANES] = {2.0f, 2.0f};
	uiFilesLayout_t layout;
	int p, i;

	UIStage_SetWide(wide);
	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	memset(&snapshot, 0, sizeof(snapshot));
	for(p = 0; p < UI_FILES_PANES; p++) {
		uiFilesPaneSnapshot_t *pane = &snapshot.pane[p];

		pane->rows = UI_FILES_ROWS;
		pane->focusRow = 2;
		pane->count = 400;
		pane->first = 3;
		strcpy(pane->button, p == UI_FILES_LEFT ? "L  Choose storage" : "R  Choose storage");
		strcpy(pane->free, "8.57 GB");
		pane->freeWidth = 72;
		pane->source = p == UI_FILES_LEFT;
		pane->autoload = p == UI_FILES_LEFT;
		pane->deviceScale = UIFiles_FitDevice(pane->device, sizeof(pane->device),
			"SD Card - SD2SP2", &layout, p, pane->source, pane->freeWidth, 23, filesMeasure);
		pane->deviceWidth = (s16)(GetTextSizeInPixels(pane->device) * pane->deviceScale);
		UIFiles_FitPath(pane->path, sizeof(pane->path),
			"sd:/Backups/Old consoles/GameCube/Collection/Redump", 180, 0.46f, filesMeasure);
		pane->pathWidth = (s16)(GetTextSizeInPixels(pane->path) * 0.46f);
		strcpy(pane->counter, "123 / 400");
		for(i = 0; i < UI_FILES_ROWS; i++) {
			uiFilesRowSnapshot_t *row = &pane->row[i];

			row->kind = kinds[i];
			row->flags = i == 2 ? UI_FILES_ROW_FOCUS : 0u;
			UIFiles_RowMeta(row->meta, sizeof(row->meta), row->kind, "1.35 GB");
			row->scale = UIFiles_FitName(row->name, sizeof(row->name), long_name,
				UIFiles_NameWidth(&layout, p, row->meta[0] != '\0' ?
				(int)(GetTextSizeInPixels(row->meta) * 0.44f) : 0, true), filesMeasure);
		}
	}
	snapshot.active = (u8)active;
	snapshot.hasBanner = 1;
	strcpy(snapshot.size, "1.35 GB");
	snapshot.sizeWidth = 64;
	strcpy(snapshot.chip, "HIDDEN");
	snapshot.titleScale = UIFiles_FitName(snapshot.title, sizeof(snapshot.title),
		"Copper Orchard: The Long Way Round of the Seven Valleys",
		layout.info.x1 - 16 - layout.infoTextX - 64, filesMeasure);
	snapshot.titleWidth = (s16)(GetTextSizeInPixels(snapshot.title) * snapshot.titleScale);
	UIFiles_FitName(snapshot.line[0], sizeof(snapshot.line[0]), long_name,
		layout.info.x1 - 112 - layout.infoTextX - 76, filesMeasure);
	strcpy(snapshot.line[1], "GameCube disc  \267  GCOE01  \267  Copper Orchard Games");
	UIFiles_Hints(UI_FILES_HINTS_LIST, active, UI_FILES_KIND_DISC, true, true, false,
		snapshot.hint[0], snapshot.hint[1]);
	/* A storage menu open: six devices (one greyed) and Other devices, over
	 * the rows; the right pane, its storage not ready, says why. */
	if(menu >= 0) {
		static const char *const names[UI_FILES_STORAGE_DEVICES] = {
			"GC Loader", "SD Card - Slot A", "SD Card - SD2SP2", "Memory Card - Slot A",
			"SMB 1.0/CIFS", "File Transfer Protocol"
		};
		uiFilesDevice_t listed[UI_FILES_STORAGE_DEVICES];
		static uiFilesStorageMenu_t storage;
		int widest = 0;

		for(i = 0; i < UI_FILES_STORAGE_DEVICES; i++) {
			memset(&listed[i], 0, sizeof(listed[i]));
			listed[i].handler = &listed[i];
			listed[i].name = names[i];
			listed[i].network = i >= 4;
		}
		UIFiles_StorageMenu(menu, listed, UI_FILES_STORAGE_DEVICES, &listed[menu ? 2 : 0],
			&listed[menu ? 0 : 4], "/games", &storage);
		for(i = 0; i < storage.box.count; i++) {
			int width = (int)(GetTextSizeInPixels(storage.box.item[i]) * 0.56f) + 32;

			widest = width > widest ? width : widest;
		}
		snapshot.menu = storage.box;
		snapshot.menu.width = (s16)widest;
		UIFiles_Hints(UI_FILES_HINTS_BOX, active, UI_FILES_KIND_FOLDER, false, false, false,
			snapshot.hint[0], snapshot.hint[1]);
		if(menu == UI_FILES_RIGHT) {
			snapshot.pane[UI_FILES_RIGHT].rows = 0;
			snapshot.pane[UI_FILES_RIGHT].focusRow = -1;
			snapshot.pane[UI_FILES_RIGHT].count = 0;
			UIFiles_NotReady(snapshot.pane[UI_FILES_RIGHT].message, "SD Card - SD2SP2",
				"No card is inserted.", NULL);
		}
	}
	/* Z: the Actions box beside the focused row, its letters, Copy and Move
	 * greyed with the reason on line 2. */
	if(extra == FILES_ACTIONS) {
		static const char *const labels[UI_FILES_ACTIONS] = {"Copy", "Move", "Rename", "Hide",
			"Delete"};
		int widest = 0;

		memset(&snapshot.menu, 0, sizeof(snapshot.menu));
		for(i = 0; i < UI_FILES_ACTIONS; i++) {
			int width = (int)(GetTextSizeInPixels(labels[i]) * 0.56f) + 60;

			strcpy(snapshot.menu.item[i], labels[i]);
			snapshot.menu.letter[i] = "XYRLZ"[i];
			widest = width > widest ? width : widest;
		}
		snapshot.menu.count = UI_FILES_ACTIONS;
		snapshot.menu.dim = 3u;
		snapshot.menu.focus = 2;
		snapshot.menu.row = 2;
		snapshot.menu.pane = (u8)active;
		snapshot.menu.open = 1;
		snapshot.menu.width = (s16)widest;
		UIFiles_ActionLine(UI_FILES_ACTION_COPY, false, "GC Loader", "SD Card - SD2SP2",
			"/Backups/Old consoles/GameCube", snapshot.line[0], sizeof(snapshot.line[0]));
		strcpy(snapshot.line[1], "SD Card - SD2SP2 has 0.80 GB free; this needs 1.35 GB.");
		snapshot.warn = 1;
		UIFiles_Hints(UI_FILES_HINTS_BOX, active, UI_FILES_KIND_FOLDER, false, false, false,
			snapshot.hint[0], snapshot.hint[1]);
	}
	/* The Copy question beside the row, and the ghost row in the other pane
	 * where the copy will land. */
	if(extra == FILES_COPY) {
		uiFilesRowSnapshot_t ghost = snapshot.pane[active].row[2];

		memset(&snapshot.menu, 0, sizeof(snapshot.menu));
		strcpy(snapshot.menu.title, "Copy to SD Card - SD2SP2?");
		strcpy(snapshot.menu.item[0], "Yes");
		strcpy(snapshot.menu.item[1], "No");
		snapshot.menu.count = 2;
		snapshot.menu.row = 2;
		snapshot.menu.pane = (u8)active;
		snapshot.menu.open = 1;
		snapshot.menu.width = (s16)(GetTextSizeInPixels(snapshot.menu.title) * 0.56f) + 24;
		UIFiles_InsertGhost(&snapshot.pane[!active], 4, &ghost);
		strcpy(snapshot.line[0], "To SD Card - SD2SP2  \233  /Backups  \267  29.1 GB free");
		strcpy(snapshot.line[1], "Needs 1.35 GB. It fits.");
		UIFiles_Hints(UI_FILES_HINTS_QUESTION, active, UI_FILES_KIND_FOLDER, false, false, false,
			snapshot.hint[0], snapshot.hint[1]);
	}
	/* A copy stopped: the maroon message, two lines. */
	if(extra == FILES_MESSAGE) {
		UIFiles_Result(UI_FILES_RESULT_STOPPED, false, "Copper Orchard", "GC Loader",
			"SD Card - SD2SP2", "Backups", 0, true, false, snapshot.message);
		/* Under it, the copy that landed flashes. */
		if(snapshot.pane[!active].rows > 0) {
			snapshot.pane[!active].row[0].flags |= UI_FILES_ROW_FLASH;
		}
		snapshot.messageWidth = (s16)(GetTextSizeInPixels(snapshot.message[1]) * 0.46f) + 48;
		UIFiles_Hints(UI_FILES_HINTS_MESSAGE, active, UI_FILES_KIND_FOLDER, false, false, false,
			snapshot.hint[0], snapshot.hint[1]);
	}
	/* First use builds edge normals; the second is the steady frame. */
	for(int frame = 0; frame < 2; frame++) {
		memset(&cost, 0, sizeof(cost));
		cost.hash = 1469598103934665603ULL;
		stubStateCalls = 0;
		IndigoBackground_DrawSavesBackdrop(1.0f, 0.0f, seconds, true, UIScene_Frame(), &clock, icons);
		_FilesShapes(&snapshot, &layout, 1.0f, 1.0f, (float)active, focusY, 0, 0.84f,
			extra == FILES_MESSAGE ? 1.0f : 0.0f);
		_FilesWords(&snapshot, &layout, 1.0f, 1.0f, (float)active, 0.84f);
		if(menu >= 0 || extra == FILES_ACTIONS || extra == FILES_COPY) {
			_FilesMenu(&snapshot.menu, &layout, 1.0f, 1.0f, (float)snapshot.menu.focus);
		}
		if(extra == FILES_MESSAGE) {
			_FilesMessage(snapshot.message, snapshot.messageWidth, 1.0f);
		}
	}
	printf("{\"scene\": \"%s\", \"sqrtf\": %ld, \"trig\": %ld, \"minmax\": %ld, "
		"\"vertices\": %ld, \"begins\": %ld, \"copy_pixels\": %.0f, \"state\": %ld, "
		"\"invalidations\": %ld, \"hash\": \"%016llx\"}\n", name, cost.sqrtCalls, cost.trigCalls,
		cost.minMaxCalls, cost.vertices, cost.begins, cost.copyPixels,
		stubStateCalls, cost.invalidations, cost.hash);
	UIStage_SetWide(false);
}

static void checkJewelMotion(void)
{
	for(int hz = 50; hz <= 60; hz += 10) {
		float dt = 1.0f / (float)hz;
		memset(&jc, 0, sizeof(jc));
		jc.t = 20.0f * PI_F - dt * 0.5f;
		pose_t before = restPose(jc.t, 0.4f, 1.0f);
		update(dt, 0.0f, 0.0f, UI_MOTION_FULL, true);
		pose_t after = restPose(jc.t, 0.4f, 1.0f);
		if(jc.t >= dt || fabsf(after.yaw - before.yaw) > 0.005f ||
			fabsf(after.pitch - before.pitch) > 0.005f ||
			fabsf(after.roll - before.roll) > 0.005f ||
			fabsf(after.y - before.y) > 0.05f) {
			fprintf(stderr, "Jewel Case sway must join across its clock wrap (%d Hz)\n", hz);
			exit(2);
		}
		jc.spin = 1.25f;
		jc.spinRate = SPIN_RATE;
		jc.dragX = 0.2f; jc.dragY = -0.3f;
		jc.dragVX = 0.4f; jc.dragVY = -0.5f;
		for(int frame = 0; frame < 4; frame++) {
			update(dt, 0.0f, 0.0f, UI_MOTION_OFF, true);
			if(jc.spin != 1.25f || jc.spinRate != 0.0f ||
				jc.dragX != 0.0f || jc.dragY != 0.0f ||
				jc.dragVX != 0.0f || jc.dragVY != 0.0f) {
				fprintf(stderr, "Jewel Case Motion Off must stop without coasting (%d Hz)\n", hz);
				exit(2);
			}
		}
		/* Manual inspection still follows the stick immediately in Off. */
		update(dt, 1.0f, -1.0f, UI_MOTION_OFF, true);
		if(jc.dragX != 0.45f || jc.dragY != 0.9f || jc.spin != 1.25f) {
			fprintf(stderr, "Jewel Case Motion Off must preserve direct stick control\n");
			exit(2);
		}
		/* Z keeps its intentional ease to rest under Full motion. */
		jc.spinRate = SPIN_RATE;
		update(dt, 0.0f, 0.0f, UI_MOTION_FULL, false);
		if(!(jc.spin > 1.25f && jc.spinRate > 0.0f && jc.spinRate < SPIN_RATE)) {
			fprintf(stderr, "Jewel Case Z stop must retain its easing\n");
			exit(2);
		}
	}
	memset(&jc, 0, sizeof(jc));
}

int main(void)
{
	int colors[UI_COLOR_LAYERS] = {0};

	IndigoBackground_SetFramebuffer(640, 480);
	IndigoBackground_SetColors(colors);
	UIColor_Select(0);
	UIClock_Compose(&clock, 10, 9, 30.5f);
	checkJewelMotion();

	home("home-library", 0, 600);
	home("home-source", 1, 600);
	home("home-settings", 2, 600);
	home("home-system", 3, 600);
	home("home-turning", 1, 6);
	library("library-horizontal", UI_GAMEFLOW_LAYOUT_HORIZONTAL, false);
	library("library-vertical", UI_GAMEFLOW_LAYOUT_VERTICAL, false);
	library("library-grid", UI_GAMEFLOW_LAYOUT_GRID, false);
	library("library-grid-wide", UI_GAMEFLOW_LAYOUT_GRID, true);
	library("library-spotlight", UI_GAMEFLOW_LAYOUT_SPOTLIGHT, false);
	library("library-spotlight-wide", UI_GAMEFLOW_LAYOUT_SPOTLIGHT, true);
	scene("settings", UI_SCENE_SETTINGS);
	scene("system", UI_SCENE_SYSTEM);

	/* The boot reveal at the handoff, overlay and background both. */
	UIScene_Reset();
	UIScene_Activate();
	for(int frame = 0; frame < 600 && UIScene_Frame()->introProgress < 0.5f; frame++) {
		settle(1);
	}
	measure("boot-handoff", true);

	hintRoundRect();
	memoryCards("memory-cards", false, CARDS_STEADY);
	memoryCards("memory-cards-wide", true, CARDS_STEADY);
	memoryCards("memory-cards-copy", true, CARDS_COPY);
	memoryCards("memory-cards-erase", true, CARDS_ERASE);
	memoryCards("memory-cards-opening", true, CARDS_OPENING);
	files("files", false, UI_FILES_LEFT, -1, FILES_PLAIN);
	files("files-right", false, UI_FILES_RIGHT, -1, FILES_PLAIN);
	files("files-wide", true, UI_FILES_LEFT, -1, FILES_PLAIN);
	files("files-storage", false, UI_FILES_LEFT, UI_FILES_LEFT, FILES_PLAIN);
	files("files-storage-right", true, UI_FILES_RIGHT, UI_FILES_RIGHT, FILES_PLAIN);
	files("files-actions", false, UI_FILES_LEFT, -1, FILES_ACTIONS);
	files("files-copy", true, UI_FILES_LEFT, -1, FILES_COPY);
	files("files-message", false, UI_FILES_RIGHT, -1, FILES_MESSAGE);
	/* Last, so the scenes before keep their clock. */
	library("library-jewel-case", UI_GAMEFLOW_LAYOUT_JEWEL_CASE, false);
	jewelCase("jewel-case", true, false, false);
	jewelCase("jewel-case-wide", true, false, true);
	jewelCase("jewel-case-bare", false, false, false);
	jewelCase("jewel-case-moving", true, true, false);
	return 0;
}
