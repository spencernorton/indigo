/* -----------------------------------------------------------
      ui_jewel_case.c - the Library's Jewel Case layout

      Ported from portlandhodl's jewel case games view for
      Swiss (gui/jewelcase.c in github.com/portlandhodl/swiss-gc,
      GPL-2.0), a GX port of Ann Nguyen's CD Case
      (bubbbly.com/cd-case). The case is built as there, in
      millimetres: every edge, rib and hinge is a "rod" (a
      rounded profile swept along a path) and the flat parts are
      thin clear sheets. The original traces light through the
      plastic per pixel; here the rods look it up in a matcap
      baked once from the same studio, and the sheets, the disc
      and its rainbow are lit per vertex against that studio.
      The cases' poses and the way one leaves and the next comes
      in are the original's.

      In Indigo the Library's carousel moves the cases and each
      disc carries its game's cover; the original's dock, white
      page, opening lid and launch are left out.
   ----------------------------------------------------------- */

#include <float.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ui_color.h"
#include "ui_jewel_case.h"
#include "ui_stage.h"

#define VTXFMT_ROD GX_VTXFMT5
#define VTXFMT_DIR GX_VTXFMT6

#define PI_F 3.14159265f

/* ---------------------------------------------------------------------------
 * Small maths
 * ------------------------------------------------------------------------- */

typedef struct { float x, y, z; } v3;

/* Gekko has no square-root instruction: newlib's sqrtf works bit by bit,
 * some 300 instructions a call, as indigo_background.c's fastSqrt says. What a
 * frame works out takes one over a length from the classic estimate and two
 * Newton steps, within 5 parts in a million. */
static inline float invLength(float d)
{
	union { float f; u32 i; } bits = {d};
	float y;

	if(!(d >= FLT_MIN && d <= FLT_MAX)) return d > 0.0f ? 1.0f / sqrtf(d) : 0.0f;
	bits.i = 0x5f3759dfu - (bits.i >> 1);
	y = bits.f;
	y *= 1.5f - 0.5f * d * y * y;
	y *= 1.5f - 0.5f * d * y * y;
	return y;
}

static inline float caseSqrt(float d) { return d * invLength(d); }

static inline v3 V3(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static inline v3 vadd(v3 a, v3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 vsub(v3 a, v3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 vmul(v3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
static inline float vdot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float vlen(v3 a) { return caseSqrt(vdot(a, a)); }
static inline v3 vnorm(v3 a) { float k = invLength(vdot(a, a)); return k > 0.0f ? vmul(a, k) : a; }
static inline v3 vmix(v3 a, v3 b, float t) { return vadd(a, vmul(vsub(b, a), t)); }
static inline v3 vmin(v3 a, float m) { return V3(fminf(a.x, m), fminf(a.y, m), fminf(a.z, m)); }
static inline v3 vreflect(v3 i, v3 n) { return vsub(i, vmul(n, 2.0f * vdot(i, n))); }

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sat(float x) { return clampf(x, 0.0f, 1.0f); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstep(float a, float b, float x)
{
	float t = sat((x - a) / (b - a));
	return t * t * (3.0f - 2.0f * t);
}

static void xform(Mtx m, v3 p, v3 *out)
{
	out->x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
	out->y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
	out->z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3];
}

static void xformDir(Mtx m, v3 p, v3 *out)
{
	out->x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z;
	out->y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z;
	out->z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z;
}

/* Linear light to sRGB through a table: the original renders in linear
 * light. */
static u8 srgbLut[1025];
static void initSrgb(void)
{
	for(int i = 0; i <= 1024; i++) {
		float c = i / 1024.0f;
		float s = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
		srgbLut[i] = (u8)lrintf(sat(s) * 255.0f);
	}
}
static inline float toSrgb(float c) { return srgbLut[(int)(sat(c) * 1024.0f)] / 255.0f; }
static inline u8 toSrgb8(float c) { return srgbLut[(int)(sat(c) * 1024.0f)]; }

/* ---------------------------------------------------------------------------
 * The studio, as the original's shaders describe it (view space, z towards
 * the camera): a grey room hung with black flags and hot strip lights behind
 * the case, a white backdrop straight behind it, softboxes round the camera.
 * ------------------------------------------------------------------------- */

static float boxf(float px, float py, float cx, float cy, float hx, float hy, float soft)
{
	float qx = fabsf(px - cx) - hx, qy = fabsf(py - cy) - hy;
	float mx = fmaxf(qx, 0.0f), my = fmaxf(qy, 0.0f);
	float d = sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0.0f);
	return 1.0f - smoothstep(-soft, soft, d);
}

static float band(float q, float c, float hw, float soft)
{
	return 1.0f - smoothstep(hw - soft, hw + soft, fabsf(q - c));
}

static float flags(float q)
{
	float k = band(q, 0.27f, 0.07f, 0.02f);
	k = fmaxf(k, band(q, 0.74f, 0.12f, 0.03f));
	k = fmaxf(k, band(q, -0.32f, 0.08f, 0.02f));
	k = fmaxf(k, band(q, -0.86f, 0.14f, 0.03f));
	return k;
}

static float hot(float q)
{
	return band(q, 0.48f, 0.07f, 0.02f) + band(q, 1.08f, 0.14f, 0.03f) + band(q, -0.56f, 0.08f, 0.02f) +
		0.8f * band(q, -1.25f, 0.15f, 0.03f);
}

static v3 room(v3 d)
{
	d = vnorm(d);
	float k = 1.0f / fmaxf(fabsf(d.z), 0.08f);
	float px = d.x * k, py = d.y * k;
	float lp = sqrtf(px * px + py * py);
	v3 dim = V3(0.02f, 0.021f, 0.026f);
	if(d.z < 0.0f) {
		v3 c = vmul(V3(0.1f, 0.103f, 0.112f), 1.0f - 0.5f * smoothstep(0.4f, 1.6f, lp));
		c = vadd(c, vmul(V3(0.8f, 0.8f, 0.8f), 1.0f - smoothstep(0.05f, 0.14f, lp)));
		c = vmul(c, 1.0f - 0.97f * fmaxf(flags(px), flags(py)));
		c = vadd(c, vmul(V3(2.4f, 2.6f, 3.0f), hot(px) + hot(py)));
		return vmix(dim, c, smoothstep(0.0f, 0.2f, -d.z));
	}
	v3 c = vmul(dim, 0.5f + 0.5f * smoothstep(-1.2f, 1.6f, py));
	float s = 0.38f * smoothstep(-0.7f, 0.8f, py) * (1.0f - smoothstep(0.5f, 1.0f, lp));
	c = vadd(c, V3(s, s, s));
	c = vadd(c, vmul(V3(1.0f, 0.99f, 0.97f), 24.0f * boxf(px, py, -0.86f, 0.3f, 0.065f, 0.85f, 0.035f)));
	c = vadd(c, vmul(V3(0.96f, 0.98f, 1.0f), 20.0f * boxf(px, py, 1.06f, 0.04f, 0.06f, 1.15f, 0.03f)));
	float t = 13.0f * boxf(px, py, 0.05f, 1.28f, 1.25f, 0.055f, 0.03f);
	c = vadd(c, V3(t, t, t));
	c = vadd(c, vmul(V3(0.92f, 0.94f, 0.97f), 2.4f * boxf(px, py, 0.1f, -1.05f, 1.15f, 0.24f, 0.28f)));
	c = vmul(c, 1.0f - 0.92f * boxf(px, py, -0.2f, 0.5f, 0.055f, 1.4f, 0.02f));
	c = vmul(c, 1.0f - 0.85f * boxf(px, py, 0.62f, -0.35f, 0.04f, 0.9f, 0.02f));
	return vmix(dim, c, smoothstep(0.0f, 0.2f, d.z));
}

static float backdropOf(v3 d)
{
	d = vnorm(d);
	if(d.z >= 0.0f) return 0.0f;
	float k = 1.0f / fmaxf(fabsf(d.z), 0.08f);
	float px = d.x * k * 0.92f, py = d.y * k;
	float lit = 1.0f - smoothstep(0.3f, 0.68f, sqrtf(px * px + py * py));
	return lit * smoothstep(0.0f, 0.2f, -d.z);
}

/* The room, baked into a small cube map as the original does, for what is
 * lit every frame: looked up and interpolated it costs a fraction of working
 * it out. */
#define ROOM_RES 48
static v3 *roomTable;
static v3 *studioTable;	/* the room with the backdrop behind, as studio() gives it */

static void faceDir(int f, float u, float v, v3 *d)
{
	switch(f) {
		case 0: *d = V3(1, v, -u); break;
		case 1: *d = V3(-1, v, u); break;
		case 2: *d = V3(u, 1, -v); break;
		case 3: *d = V3(u, -1, v); break;
		case 4: *d = V3(u, v, 1); break;
		default: *d = V3(-u, v, -1); break;
	}
}

static v3 studioSlow(v3 d);

static bool buildRoomTable(void)
{
	roomTable = malloc(6 * ROOM_RES * ROOM_RES * sizeof(v3));
	studioTable = malloc(6 * ROOM_RES * ROOM_RES * sizeof(v3));
	if(roomTable == NULL || studioTable == NULL) return false;
	for(int f = 0; f < 6; f++) {
		for(int y = 0; y < ROOM_RES; y++) {
			for(int x = 0; x < ROOM_RES; x++) {
				v3 d;
				faceDir(f, (x + 0.5f) / ROOM_RES * 2 - 1, (y + 0.5f) / ROOM_RES * 2 - 1, &d);
				roomTable[(f * ROOM_RES + y) * ROOM_RES + x] = room(d);
			}
		}
	}
	for(int f = 0; f < 6; f++) {
		for(int y = 0; y < ROOM_RES; y++) {
			for(int x = 0; x < ROOM_RES; x++) {
				v3 d;
				faceDir(f, (x + 0.5f) / ROOM_RES * 2 - 1, (y + 0.5f) / ROOM_RES * 2 - 1, &d);
				studioTable[(f * ROOM_RES + y) * ROOM_RES + x] = studioSlow(d);
			}
		}
	}
	return true;
}

/* A direction looked up in one of the cube map tables, bilinearly. */
static v3 cubeLookup(const v3 *table, v3 d)
{
	float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z), u, v, m;
	int f;
	if(ax >= ay && ax >= az) { f = d.x > 0 ? 0 : 1; m = ax; u = d.x > 0 ? -d.z : d.z; v = d.y; }
	else if(ay >= az) { f = d.y > 0 ? 2 : 3; m = ay; u = d.x; v = d.y > 0 ? -d.z : d.z; }
	else { f = d.z > 0 ? 4 : 5; m = az; u = d.z > 0 ? d.x : -d.x; v = d.y; }
	if(!(m > 0.0f)) return table[0];
	float fx = clampf((u / m + 1) * 0.5f * ROOM_RES - 0.5f, 0, ROOM_RES - 1.001f);
	float fy = clampf((v / m + 1) * 0.5f * ROOM_RES - 0.5f, 0, ROOM_RES - 1.001f);
	int x0 = (int)fx, y0 = (int)fy;
	float tx = fx - x0, ty = fy - y0;
	const v3 *t = table + (f * ROOM_RES + y0) * ROOM_RES + x0;
	v3 a = vmix(t[0], t[1], tx), b = vmix(t[ROOM_RES], t[ROOM_RES + 1], tx);
	return vmix(a, b, ty);
}

static v3 roomT(v3 d) { return cubeLookup(roomTable, d); }

static v3 studioSlow(v3 d)
{
	return vmix(roomT(d), V3(1.0f, 1.0f, 1.0f), backdropOf(d));
}

static v3 studio(v3 d) { return cubeLookup(studioTable, d); }

static v3 glare(v3 d)
{
	v3 s = studio(d);
	return V3(fmaxf(s.x - 0.45f, 0.0f), fmaxf(s.y - 0.45f, 0.0f), fmaxf(s.z - 0.45f, 0.0f));
}

static const v3 KEY    = {-0.625f, 0.218f, 0.75f};
static const v3 STRIP  = { 0.725f, 0.028f, 0.688f};
static const v3 TOP    = { 0.031f, 0.786f, 0.618f};
static const v3 BOUNCE = { 0.069f, -0.722f, 0.688f};

static float fresnel(float cosv, float n)
{
	float f0 = (n - 1.0f) / (n + 1.0f);
	f0 *= f0;
	float m = 1.0f - sat(cosv);
	return f0 + (1.0f - f0) * m * m * m * m * m;
}

static float lights(v3 n)
{
	return 0.42f + 0.62f * fmaxf(vdot(n, KEY), 0.0f) + 0.28f * fmaxf(vdot(n, STRIP), 0.0f) +
		0.18f * fmaxf(vdot(n, TOP), 0.0f);
}

/* A wavelength (nm) as a colour: Zucconi's fit of the visible spectrum. */
static float bump(float x, float y0) { return sat(1.0f - x * x - y0); }
static v3 spectral(float w)
{
	if(w < 380.0f || w > 760.0f) return V3(0, 0, 0);
	float x = sat((w - 400.0f) / 300.0f);
	return V3(
		bump(3.54585104f * (x - 0.69549072f), 0.02312639f) + bump(3.90307140f * (x - 0.11748627f), 0.84897130f),
		bump(2.93225262f * (x - 0.49228336f), 0.15225084f) + bump(3.21182957f * (x - 0.86755042f), 0.88445281f),
		bump(2.41593945f * (x - 0.27699880f), 0.52607955f) + bump(3.96587128f * (x - 0.66077860f), 0.73949448f));
}

#define IOR 1.59f

/* ---------------------------------------------------------------------------
 * Rod matcap: a clear rod seen against the studio, looked up by the view
 * space normal. Each texel traces the ray through a round cross-section in
 * the plane of the normal and the line of sight, the same four ways out as
 * the original, and keeps apart what comes from the studio (rgb) and how
 * much of the picture straight behind shows through (alpha), which the blend
 * adds back from the framebuffer.
 * ------------------------------------------------------------------------- */

#define MATCAP_SIZE 64
#define MATCAP_LODS 4
#define PAGE_SHOW   0.25f	/* how much of the backdrop a rod shows through it */
/* A rod is only a few pixels across, so the original reads the studio
 * blurred over each pixel's spread of directions: about this many radians. */
#define ROD_SPREAD  0.35f
static u8 *matcapData;
static GXTexObj matcapTex;

typedef struct { float s, z; } v2;

static bool refract2(v2 i, v2 n, float eta, v2 *out)
{
	float d = n.s * i.s + n.z * i.z;
	float k = 1.0f - eta * eta * (1.0f - d * d);
	if(k < 0.0f) return false;
	float f = eta * d + sqrtf(k);
	out->s = eta * i.s - f * n.s;
	out->z = eta * i.z - f * n.z;
	return true;
}

/* The room averaged over a cone of directions round d. */
#define BLUR_TAPS 12
static float blurTaps[BLUR_TAPS][2];
static void initBlurTaps(void)
{
	for(int i = 0; i < BLUR_TAPS; i++) {
		float r = sqrtf((i + 0.5f) / BLUR_TAPS), a = i * 2.39996323f;
		blurTaps[i][0] = r * cosf(a);
		blurTaps[i][1] = r * sinf(a);
	}
}

/* The studio's small hot lamps, far brighter than the room: a clear rod's
 * rounded surface catches each as a pin-point. Blurred over a cone they
 * spread their light (in the original's cube map, mip-mapped) over
 * everything within it. */
static const struct { float x, y; v3 col; bool front; } studioLamps[] = {
	{-0.42f, 0.98f, {1.0f, 1.1f, 1.27f}, true}, {0.62f, 0.72f, {0.85f, 0.96f, 1.1f}, true},
	{-0.95f, -0.5f, {0.75f, 0.85f, 1.0f}, true}, {0.85f, -0.7f, {0.75f, 0.85f, 1.0f}, true},
	{-0.62f, 0.58f, {0.15f, 0.17f, 0.2f}, false}, {0.66f, -0.6f, {0.15f, 0.17f, 0.2f}, false},
};
#define LAMP 12000.0f

static v3 lampsBlur(v3 d, float radius)
{
	v3 acc = V3(0, 0, 0);
	for(u32 i = 0; i < sizeof(studioLamps) / sizeof(studioLamps[0]); i++) {
		float z = studioLamps[i].front ? 1.0f : -1.0f;
		v3 l = vnorm(V3(studioLamps[i].x, studioLamps[i].y, z));
		float ang = acosf(clampf(vdot(d, l), -1.0f, 1.0f));
		if(ang > radius) continue;
		/* A lamp is a disc of radius ~0.0075 on the plane z = 1: its solid
		 * angle shrinks off axis. */
		float cosz = fabsf(l.z), omega = PI_F * 0.0075f * 0.0075f * cosz * cosz * cosz;
		float cone = PI_F * radius * radius;
		float k = omega * LAMP / cone * (1.0f - smoothstep(0.7f * radius, radius, ang)) * smoothstep(0.0f, 0.2f, fabsf(l.z));
		acc = vadd(acc, vmul(studioLamps[i].col, k));
	}
	return acc;
}

static v3 roomBlur(v3 d, float radius)
{
	d = vnorm(d);
	v3 up = fabsf(d.z) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0);
	v3 t = vnorm(V3(up.y * d.z - up.z * d.y, up.z * d.x - up.x * d.z, up.x * d.y - up.y * d.x));
	v3 b = V3(d.y * t.z - d.z * t.y, d.z * t.x - d.x * t.z, d.x * t.y - d.y * t.x);
	v3 acc = V3(0, 0, 0);
	for(int i = 0; i < BLUR_TAPS; i++) {
		v3 dir = vadd(d, vadd(vmul(t, blurTaps[i][0] * radius), vmul(b, blurTaps[i][1] * radius)));
		acc = vadd(acc, room(dir));
	}
	return vadd(vmul(acc, 1.0f / BLUR_TAPS), lampsBlur(d, radius));
}

/* A ray leaving the rod: what the studio gives (a) and the share of the
 * picture behind (b). The backdrop fills about 15 degrees round the line of
 * sight, widened by the spread. */
static void outsideRay(v3 dir, v3 *a, float *b)
{
	*a = roomBlur(dir, ROD_SPREAD);
	*b = 0.0f;
	if(dir.z > -0.05f) return;
	float spread = fminf(ROD_SPREAD, 0.3f);
	float bend = acosf(clampf(-dir.z, -1.0f, 1.0f));
	float w = 1.0f - smoothstep(0.04f - spread, 0.13f + spread, bend);
	*a = vmul(*a, 1.0f - w * PAGE_SHOW);
	*b = w * PAGE_SHOW;
}

static void matcapTexel(float nx, float ny, v3 *outCol, float *outB)
{
	float a = sqrtf(nx * nx + ny * ny);
	if(a > 0.999f) { nx *= 0.999f / a; ny *= 0.999f / a; a = 0.999f; }
	float b = sqrtf(1.0f - a * a);
	float ux = a > 1e-4f ? nx / a : 1.0f, uy = a > 1e-4f ? ny / a : 0.0f;
	#define TO3(v) V3((v).s * ux, (v).s * uy, (v).z)

	v2 N = {a, b}, I = {0.0f, -1.0f};
	float F = fresnel(b, IOR);
	v2 R = {2.0f * a * b, 2.0f * b * b - 1.0f};
	v3 R3 = TO3(R);
	v3 refl = vmix(roomBlur(R3, ROD_SPREAD), V3(1, 1, 1), backdropOf(R3));

	v3 acc = V3(0, 0, 0);
	float accB = 0.0f, thr = 1.0f;
	v2 dir, pos = N;
	if(!refract2(I, N, 1.0f / IOR, &dir)) dir = I;
	for(int k = 0; k < 4; k++) {
		float t = -2.0f * (pos.s * dir.s + pos.z * dir.z);
		pos.s += dir.s * t;
		pos.z += dir.z * t;
		v2 ne = pos, nin = {-pos.s, -pos.z}, out;
		bool trapped = !refract2(dir, nin, IOR, &out);
		float Fe = trapped ? 1.0f : fresnel(fabsf(out.s * ne.s + out.z * ne.z), IOR);
		if(!trapped) {
			v3 ea; float eb;
			outsideRay(vnorm(TO3(out)), &ea, &eb);
			float w = thr * (1.0f - Fe);
			acc = vadd(acc, vmul(ea, w));
			accB += eb * w;
		}
		thr *= Fe;
		float d = dir.s * ne.s + dir.z * ne.z;
		dir.s -= 2.0f * d * ne.s;
		dir.z -= 2.0f * d * ne.z;
	}
	/* What is still inside leaks out dimly, as after many more bounces. */
	acc = vadd(acc, vmul(roomBlur(TO3(dir), ROD_SPREAD), thr * 0.6f));
	#undef TO3

	*outCol = vadd(vmul(refl, F), vmul(acc, 1.0f - F));
	*outB = accB * (1.0f - F);
}

static void putRGBA8(u8 *tex, int w, int x, int y, u8 r, u8 g, u8 b, u8 a)
{
	int tile = (y >> 2) * (w >> 2) + (x >> 2);
	int off = tile * 64 + ((y & 3) * 4 + (x & 3)) * 2;
	tex[off] = a;
	tex[off + 1] = r;
	tex[off + 32] = g;
	tex[off + 33] = b;
}

static void getRGBA8(const u8 *tex, int w, int x, int y, int *rgba)
{
	int tile = (y >> 2) * (w >> 2) + (x >> 2);
	int off = tile * 64 + ((y & 3) * 4 + (x & 3)) * 2;
	rgba[3] += tex[off];
	rgba[0] += tex[off + 1];
	rgba[1] += tex[off + 32];
	rgba[2] += tex[off + 33];
}

static u32 rgba8Size(int w, int h) { return (u32)(((w + 3) & ~3) * ((h + 3) & ~3) * 4); }

/* Box filter one RGBA8 level down into the next. */
static void downsampleRGBA8(const u8 *src, int w, int h, u8 *dst)
{
	int dw = w / 2, dh = h / 2;
	for(int y = 0; y < dh; y++) {
		for(int x = 0; x < dw; x++) {
			int c[4] = {0, 0, 0, 0};
			getRGBA8(src, w, x * 2, y * 2, c);
			getRGBA8(src, w, x * 2 + 1, y * 2, c);
			getRGBA8(src, w, x * 2, y * 2 + 1, c);
			getRGBA8(src, w, x * 2 + 1, y * 2 + 1, c);
			putRGBA8(dst, dw, x, y, c[0] >> 2, c[1] >> 2, c[2] >> 2, c[3] >> 2);
		}
	}
}

static bool buildMatcap(void)
{
	u32 total = 0;
	for(int l = 0, s = MATCAP_SIZE; l < MATCAP_LODS; l++, s >>= 1) total += rgba8Size(s, s);
	initBlurTaps();
	matcapData = memalign(32, total);
	if(matcapData == NULL) return false;
	for(int y = 0; y < MATCAP_SIZE; y++) {
		for(int x = 0; x < MATCAP_SIZE; x++) {
			float nx = (x + 0.5f) / (MATCAP_SIZE / 2) - 1.0f;
			float ny = 1.0f - (y + 0.5f) / (MATCAP_SIZE / 2);
			v3 col; float B;
			matcapTexel(nx, ny, &col, &B);
			/* Blended as src + dst * B: exact over a white backdrop. */
			float r = toSrgb(col.x + B) - B, g = toSrgb(col.y + B) - B, b = toSrgb(col.z + B) - B;
			putRGBA8(matcapData, MATCAP_SIZE, x, y, (u8)(sat(r) * 255.0f), (u8)(sat(g) * 255.0f),
				(u8)(sat(b) * 255.0f), (u8)(sat(B) * 255.0f));
		}
	}
	u8 *level = matcapData;
	for(int l = 1, s = MATCAP_SIZE; l < MATCAP_LODS; l++, s >>= 1) {
		u8 *next = level + rgba8Size(s, s);
		downsampleRGBA8(level, s, s, next);
		level = next;
	}
	DCFlushRange(matcapData, total);
	GX_InitTexObj(&matcapTex, matcapData, MATCAP_SIZE, MATCAP_SIZE, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_TRUE);
	GX_InitTexObjLOD(&matcapTex, GX_LIN_MIP_LIN, GX_LINEAR, 0.0f, MATCAP_LODS - 1, 0.0f, GX_DISABLE, GX_ENABLE, GX_ANISO_1);
	return true;
}

/* ---------------------------------------------------------------------------
 * Paths, as the original builds them (2D, millimetres)
 * ------------------------------------------------------------------------- */

#define PATH_MAX_PTS 512
#define DETAIL 2.0f	/* the original's point spacing is multiplied by this */

typedef struct { float p[PATH_MAX_PTS][2]; int n; } path_t;

static void pathPush(path_t *o, float x, float y)
{
	if(o->n < PATH_MAX_PTS) { o->p[o->n][0] = x; o->p[o->n][1] = y; o->n++; }
}

/* Points enough round an arc of radius r for the chords to keep within
 * ARC_TOL of it (a fifth of a pixel at the case's usual size), and no more
 * than the original's spacing gives. */
#define ARC_TOL 0.1f
static int arcPoints(float r, float angle, float step)
{
	int n = (int)ceilf(angle * r / (step * DETAIL));
	if(r > ARC_TOL) {
		int byTol = (int)ceilf(angle / (2.0f * acosf(1.0f - ARC_TOL / r)));
		if(byTol < n) n = byTol;
	}
	return n;
}

static void arcPts(path_t *o, float cx, float cy, float r, float a0, float a1, float step)
{
	int n = arcPoints(r, fabsf(a1 - a0), step);
	if(n < 2) n = 2;
	for(int i = 0; i <= n; i++) {
		float a = a0 + (a1 - a0) * i / n;
		pathPush(o, cx + r * cosf(a), cy + r * sinf(a));
	}
}

/* Round off the corners of a polyline with arcs of radius t. */
static void roundCorners(const float (*e)[2], int n, float t, bool closed, path_t *o)
{
	o->n = 0;
	for(int i = 0; i < n; i++) {
		const float *pt = e[i];
		if(!closed && (i == 0 || i == n - 1)) { pathPush(o, pt[0], pt[1]); continue; }
		const float *s = e[(i - 1 + n) % n], *l = e[(i + 1) % n];
		float cx = s[0] - pt[0], cy = s[1] - pt[1], cl = sqrtf(cx * cx + cy * cy);
		float dx = l[0] - pt[0], dy = l[1] - pt[1], dl = sqrtf(dx * dx + dy * dy);
		cx /= cl; cy /= cl; dx /= dl; dy /= dl;
		float h = acosf(clampf(cx * dx + cy * dy, -1.0f, 1.0f)) / 2.0f;
		if(h > PI_F / 2.0f - 0.001f) { pathPush(o, pt[0], pt[1]); continue; }
		float u = t / tanf(h);
		float vx = cx + dx, vy = cy + dy, vl = sqrtf(vx * vx + vy * vy);
		vx /= vl; vy /= vl;
		float mx = pt[0] + vx * (t / sinf(h)), my = pt[1] + vy * (t / sinf(h));
		float gx = pt[0] + cx * u, gy = pt[1] + cy * u;
		float xx = pt[0] + dx * u, xy = pt[1] + dy * u;
		float y = atan2f(gy - my, gx - mx), b = atan2f(xy - my, xx - mx);
		if(b - y > PI_F) b -= 2.0f * PI_F;
		if(y - b > PI_F) b += 2.0f * PI_F;
		arcPts(o, mx, my, t, y, b, 0.8f);
	}
}

static void roundSquare(float e, float t, path_t *o)
{
	const float q[4][2] = {{e, -e}, {e, e}, {-e, e}, {-e, -e}};
	roundCorners(q, 4, t, true, o);
}

static void roundRect(float cx, float cy, float hw, float hh, float r, path_t *o)
{
	const float q[4][2] = {{cx + hw, cy - hh}, {cx + hw, cy + hh}, {cx - hw, cy + hh}, {cx - hw, cy - hh}};
	roundCorners(q, 4, r, true, o);
}

static void circlePts(float cx, float cy, float r, float step, path_t *o)
{
	o->n = 0;
	int n = arcPoints(r, 2.0f * PI_F, step);
	if(n < 12) n = 12;
	for(int i = 0; i < n; i++) {
		float a = (float)i / n * 2.0f * PI_F;
		pathPush(o, cx + r * cosf(a), cy + r * sinf(a));
	}
}

static void segment(float x0, float y0, float x1, float y1, path_t *o)
{
	o->n = 0;
	pathPush(o, x0, y0);
	pathPush(o, x1, y1);
}

/* ---------------------------------------------------------------------------
 * Rods: a rounded rectangle profile (half width hw across, half height hh in
 * z, corner radius rc, centred at height zc) swept along a path. Open paths
 * end in a dome, or flat when asked (the hinge knuckles).
 * ------------------------------------------------------------------------- */

typedef struct { float hw, hh, rc, zc; int seg; bool flat; } profile_t;

typedef struct {
	float *pos, *nrm;
	int count, cap;
	int glintEnd;	/* the first rod, the frame, is where the lamps glint */
	u16 *prims;	/* [GX primitive, count, indices...]... */
	int primLen, primCap;
	void *dl;
	u32 dlSize, dlCap;
	bool failed;	/* out of memory while it was built */
} rodmesh_t;

static int rodVertex(rodmesh_t *m, v3 p, v3 n)
{
	if(m->count == m->cap) {
		int cap = m->cap ? m->cap * 2 : 1024;
		float *pos = realloc(m->pos, (size_t)cap * 3 * sizeof(float));
		if(pos == NULL) { m->failed = true; return 0; }
		m->pos = pos;
		float *nrm = realloc(m->nrm, (size_t)cap * 3 * sizeof(float));
		if(nrm == NULL) { m->failed = true; return 0; }
		m->nrm = nrm;
		m->cap = cap;
	}
	n = vnorm(n);
	float *pp = m->pos + m->count * 3, *nn = m->nrm + m->count * 3;
	pp[0] = p.x; pp[1] = p.y; pp[2] = p.z;
	nn[0] = n.x; nn[1] = n.y; nn[2] = n.z;
	return m->count++;
}

static void rodPrim(rodmesh_t *m, u8 type, const u16 *idx, int n)
{
	if(m->primLen + n + 2 > m->primCap) {
		int cap = (m->primLen + n + 2) * 2;
		u16 *prims = realloc(m->prims, (size_t)cap * sizeof(u16));
		if(prims == NULL) { m->failed = true; return; }
		m->prims = prims;
		m->primCap = cap;
	}
	m->prims[m->primLen++] = type;
	m->prims[m->primLen++] = n;
	memcpy(m->prims + m->primLen, idx, n * sizeof(u16));
	m->primLen += n;
}

static void rodAdd(rodmesh_t *m, const path_t *path, profile_t pr, bool closed)
{
	/* The profile round its four corners. */
	int seg = (pr.seg ? pr.seg : 6);
	seg = (seg + 1) / 2;
	/* The thinnest are a pixel or two across: eight sides are as round as
	 * they look. */
	if(pr.hw < 1.0f && pr.hh < 1.0f) seg = 1;
	float pts[40][2], nrm[40][2];
	int np = 0;
	const float corners[4][3] = {
		{pr.hw - pr.rc, pr.hh - pr.rc, 0.0f}, {-(pr.hw - pr.rc), pr.hh - pr.rc, PI_F / 2.0f},
		{-(pr.hw - pr.rc), -(pr.hh - pr.rc), PI_F}, {pr.hw - pr.rc, -(pr.hh - pr.rc), 3.0f * PI_F / 2.0f}};
	for(int c = 0; c < 4; c++) {
		for(int t = 0; t <= seg; t++) {
			float a = corners[c][2] + (float)t / seg * (PI_F / 2.0f);
			float x = corners[c][0] + pr.rc * cosf(a), y = corners[c][1] + pr.rc * sinf(a);
			if(np && hypotf(x - pts[np - 1][0], y - pts[np - 1][1]) < 1e-6f) continue;
			pts[np][0] = x; pts[np][1] = y;
			nrm[np][0] = cosf(a); nrm[np][1] = sinf(a);
			np++;
		}
	}
	if(np > 1 && hypotf(pts[0][0] - pts[np - 1][0], pts[0][1] - pts[np - 1][1]) < 1e-6f) np--;

	int s = path->n;
	float tx[PATH_MAX_PTS], ty[PATH_MAX_PTS];
	for(int i = 0; i < s; i++) {
		const float *a = path->p[closed ? (i - 1 + s) % s : (i > 0 ? i - 1 : 0)];
		const float *b = path->p[closed ? (i + 1) % s : (i < s - 1 ? i + 1 : s - 1)];
		float dx = b[0] - a[0], dy = b[1] - a[1], l = hypotf(dx, dy);
		if(l < 1e-9f) l = 1.0f;
		tx[i] = dx / l; ty[i] = dy / l;
	}
	int base = m->count;
	for(int i = 0; i < s; i++) {
		float sx = -ty[i], sy = tx[i];
		for(int j = 0; j < np; j++) {
			rodVertex(m, V3(path->p[i][0] + sx * pts[j][0], path->p[i][1] + sy * pts[j][0], pr.zc + pts[j][1]),
				V3(sx * nrm[j][0], sy * nrm[j][0], nrm[j][1]));
		}
	}
	u16 strip[2 * 41];
	int segs = closed ? s : s - 1;
	for(int i = 0; i < segs; i++) {
		int r0 = base + i * np, r1 = base + ((i + 1) % s) * np, k = 0;
		for(int j = 0; j <= np; j++) {
			strip[k++] = r0 + j % np;
			strip[k++] = r1 + j % np;
		}
		rodPrim(m, GX_TRIANGLESTRIP, strip, k);
	}
	if(closed) return;

	for(int end = 0; end < 2; end++) {
		int i = end ? s - 1 : 0;
		float sign = end ? 1.0f : -1.0f;
		float sx = -ty[i], sy = tx[i];
		v3 h = V3(path->p[i][0], path->p[i][1], pr.zc);
		v3 u = V3(tx[i] * sign, ty[i] * sign, 0.0f);
		if(pr.flat) {
			u16 fan[42];
			int k = 0;
			fan[k++] = rodVertex(m, h, u);
			for(int j = 0; j <= np; j++) {
				int jj = j % np;
				fan[k++] = rodVertex(m, V3(h.x + sx * pts[jj][0], h.y + sy * pts[jj][0], pr.zc + pts[jj][1]), u);
			}
			rodPrim(m, GX_TRIANGLEFAN, fan, k);
			continue;
		}
		float e = 0.8f * fminf(pr.hw, pr.hh);
		int prev = base + i * np;
		for(int n = 1; n <= 3; n++) {
			float c = n / 3.0f * (PI_F / 2.0f), d = cosf(c), v = sinf(c) * e;
			int ring = m->count;
			for(int j = 0; j < np; j++) {
				v3 mm = V3(sx * nrm[j][0], sy * nrm[j][0], nrm[j][1]);
				v3 g = V3(mm.x * d * e + u.x * sinf(c) * e * 1.1f, mm.y * d * e + u.y * sinf(c) * e * 1.1f, mm.z * d * e);
				rodVertex(m, V3(h.x + sx * pts[j][0] * d + u.x * v, h.y + sy * pts[j][0] * d + u.y * v, pr.zc + pts[j][1] * d), g);
			}
			int k = 0;
			for(int j = 0; j <= np; j++) {
				strip[k++] = prev + j % np;
				strip[k++] = ring + j % np;
			}
			rodPrim(m, GX_TRIANGLESTRIP, strip, k);
			prev = ring;
		}
	}
}

static void rodSetupVtx(void)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
	GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
	GX_SetVtxAttrFmt(VTXFMT_ROD, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VTXFMT_ROD, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
}

/* Move the arrays to aligned memory and make room for the display list the
 * video thread records them in. Menu thread. */
static bool rodPack(rodmesh_t *m)
{
	if(m->failed || m->count == 0) return false;
	float *pos = memalign(32, m->count * 12), *nrm = memalign(32, m->count * 12);
	if(pos == NULL || nrm == NULL) {
		free(pos);
		free(nrm);
		return false;
	}
	memcpy(pos, m->pos, m->count * 12);
	memcpy(nrm, m->nrm, m->count * 12);
	free(m->pos);
	free(m->nrm);
	m->pos = pos;
	m->nrm = nrm;
	DCFlushRange(pos, m->count * 12);
	DCFlushRange(nrm, m->count * 12);

	u32 size = 64;
	for(int i = 0; i < m->primLen; i += m->prims[i + 1] + 2) size += 3 + m->prims[i + 1] * 4;
	size = (size + 31) & ~31;
	m->dl = memalign(32, size);
	if(m->dl == NULL) return false;
	memset(m->dl, 0, size);
	DCFlushRange(m->dl, size);
	m->dlCap = size;
	return true;
}

/* Record the strips in the display list: on the thread that owns GX, the
 * first time the case is drawn. */
static bool rodRecord(rodmesh_t *m)
{
	DCInvalidateRange(m->dl, m->dlCap);
	rodSetupVtx();
	GX_BeginDispList(m->dl, m->dlCap);
	for(int i = 0; i < m->primLen; i += m->prims[i + 1] + 2) {
		int n = m->prims[i + 1];
		GX_Begin(m->prims[i], VTXFMT_ROD, n);
		for(int j = 0; j < n; j++) {
			GX_Position1x16(m->prims[i + 2 + j]);
			GX_Normal1x16(m->prims[i + 2 + j]);
		}
		GX_End();
	}
	m->dlSize = GX_EndDispList();
	free(m->prims);
	m->prims = NULL;
	m->primLen = m->primCap = 0;
	return m->dlSize > 0;
}

static void rodFree(rodmesh_t *m)
{
	free(m->pos);
	free(m->nrm);
	free(m->prims);
	free(m->dl);
	memset(m, 0, sizeof(*m));
}

static rodmesh_t trayRods, lidRods;

/* The hinge axis the lid turns about. */
#define HINGE_X -72.0f
#define HINGE_Z 4.0f

static const profile_t PROF_FRAME = {3.3f, 3.1f, 2.8f, -0.7f, 6, false};
static const profile_t PROF_RAIL  = {2.4f, 2.6f, 2.2f, -1.2f, 6, false};

static void buildTrayRods(rodmesh_t *m)
{
	path_t p;
	float r = 73.0f - PROF_FRAME.hw;
	/* The frame, and the lip round the back plate. */
	roundSquare(r, 6.5f, &p);
	rodAdd(m, &p, PROF_FRAME, true);
	m->glintEnd = m->count;
	roundSquare(72.25f, 7.4f, &p);
	rodAdd(m, &p, (profile_t) {0.75f, 0.7f, 0.68f, -4.5f, 6, false}, true);

	/* The bumps along the frame. */
	profile_t bump = {3.75f, 3.5f, 3.1f, PROF_FRAME.zc, 6, false};
	const float at[4] = {-35.0f, -12.0f, 12.0f, 35.0f};
	for(int e = -1; e <= 1; e += 2) {
		for(int k = 0; k < 4; k++) {
			segment(at[k] - 2.2f, e * r, at[k] + 2.2f, e * r, &p);
			rodAdd(m, &p, bump, false);
		}
		for(int k = 0; k < 4; k++) {
			if(e < 0 && fabsf(at[k]) < 20.0f) continue;
			segment(e * r, at[k] - 2.2f, e * r, at[k] + 2.2f, &p);
			rodAdd(m, &p, bump, false);
		}
	}

	/* The rails that hold the disc in the corners. */
	const float o = 63.4f;
	const int corners[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
	profile_t foot = {1.9f, 2.2f, 1.6f, PROF_RAIL.zc, 6, false};
	for(int c = 0; c < 4; c++) {
		float e = corners[c][0], t = corners[c][1];
		float a = e < 0 ? 26.0f : 35.0f;
		const float q[3][2] = {{e * o, t * a}, {e * o, t * o}, {35.0f * e, t * o}};
		roundCorners(q, 3, 4.5f, false, &p);
		rodAdd(m, &p, PROF_RAIL, false);
		segment(e * o, t * a, e * r, t * a, &p);
		rodAdd(m, &p, foot, false);
		segment(35.0f * e, t * o, 35.0f * e, t * r, &p);
		rodAdd(m, &p, foot, false);
	}

	/* The ring round the disc, and its four tabs. */
	circlePts(0, 0, 61.6f, 1.1f, &p);
	rodAdd(m, &p, (profile_t) {1.15f, 1.7f, 1.0f, -2.1f, 5, false}, true);
	circlePts(0, 0, 61.6f - 1.55f, 1.1f, &p);
	rodAdd(m, &p, (profile_t) {0.5f, 0.8f, 0.45f, -3.0f, 4, false}, true);
	const float tabs[4] = {59.0f, 121.0f, 238.5f, 301.5f};
	for(int k = 0; k < 4; k++) {
		float t = tabs[k] * PI_F / 180.0f;
		segment(58.6f * cosf(t), 58.6f * sinf(t), 63.2f * cosf(t), 63.2f * sinf(t), &p);
		rodAdd(m, &p, (profile_t) {1.6f, 1.0f, 0.85f, -0.6f, 4, false}, false);
	}

	/* The finger notches along the bottom. */
	for(int e = -1; e <= 1; e += 2) {
		const float q[3][2] = {{46.0f * e, -o}, {46.0f * e, -49.5f}, {42.6f * e, -45.6f}};
		roundCorners(q, 3, 4.2f, false, &p);
		rodAdd(m, &p, (profile_t) {1.9f, 1.9f, 1.8f, -2.0f, 6, false}, false);
	}

	/* The tab by the spine. */
	float sx = (-73.0f + -50.5f) / 2.0f, cx = (-50.5f - -73.0f) / 2.0f;
	roundRect(sx, 0, cx - 0.8f, 22.0f - 0.8f, 3.4f, &p);
	rodAdd(m, &p, (profile_t) {0.8f, 0.8f, 0.75f, 1.8f, 4, false}, true);
	{
		const float q[4][2] = {{-69.0f, 13.0f}, {-58.5f, 13.0f}, {-58.5f, -13.0f}, {-69.0f, -13.0f}};
		roundCorners(q, 4, 3.2f, false, &p);
		rodAdd(m, &p, (profile_t) {1.35f, 1.0f, 0.9f, 3.0f, 6, false}, false);
	}
	roundRect(-55.4f, 0, 4.4f, 3.2f, 1.2f, &p);
	rodAdd(m, &p, (profile_t) {0.65f, 0.55f, 0.5f, 3.0f, 3, false}, true);

	/* The hub the disc clips onto. */
	circlePts(0, 0, 18.8f, 0.7f, &p);
	rodAdd(m, &p, (profile_t) {0.8f, 0.75f, 0.7f, -0.5f, 4, false}, true);
	circlePts(0, 0, 16.5f, 0.7f, &p);
	rodAdd(m, &p, (profile_t) {0.45f, 0.45f, 0.42f, -0.15f, 3, false}, true);
	circlePts(0, 0, 9.6f, 0.6f, &p);
	rodAdd(m, &p, (profile_t) {0.95f, 0.95f, 0.9f, 0.0f, 4, false}, true);
	circlePts(0, 0, 6.6f, 0.5f, &p);
	rodAdd(m, &p, (profile_t) {0.55f, 0.85f, 0.5f, -0.35f, 3, false}, true);
	for(int c = 0; c < 4; c++) {
		float e = c == 0 || c == 3 ? 1.0f : -1.0f, t = c < 2 ? 1.0f : -1.0f;
		circlePts(8.9f * e, 8.9f * t, 2.0f, 0.4f, &p);
		rodAdd(m, &p, (profile_t) {0.45f, 0.6f, 0.42f, -0.3f, 3, false}, true);
	}
	/* The clear middle round the disc's hole. */
	circlePts(0, 0, 7.75f, 0.5f, &p);
	rodAdd(m, &p, (profile_t) {0.25f, 0.6f, 0.24f, -1.8f, 3, false}, true);

	/* The hinge knuckles. */
	profile_t knuckle = {2.1f, 2.1f, 2.08f, HINGE_Z, 6, true};
	segment(HINGE_X, 44.0f, HINGE_X, 63.0f, &p);
	rodAdd(m, &p, knuckle, false);
	segment(HINGE_X, -63.0f, HINGE_X, -44.0f, &p);
	rodAdd(m, &p, knuckle, false);
}

static void buildLidRods(rodmesh_t *m)
{
	path_t p;
	roundSquare(72.0f, 7.0f, &p);
	rodAdd(m, &p, (profile_t) {1.0f, 1.0f, 0.98f, 4.0f, 5, false}, true);
	m->glintEnd = m->count;
	roundSquare(69.6f, 5.0f, &p);
	rodAdd(m, &p, (profile_t) {0.42f, 0.4f, 0.38f, 3.25f, 3, false}, true);
	const float notches[2] = {-42.0f, 30.0f};
	for(int t = -1; t <= 1; t += 2) {
		for(int k = 0; k < 2; k++) {
			p.n = 0;
			arcPts(&p, notches[k], 69.6f * t, 4.4f, t > 0 ? PI_F : 0.0f, t > 0 ? 2.0f * PI_F : PI_F, 0.5f);
			rodAdd(m, &p, (profile_t) {0.42f, 0.38f, 0.36f, 3.22f, 3, false}, false);
		}
	}
	for(int t = -1; t <= 1; t += 2) {
		for(int n = -30; n <= 30; n += 60) {
			segment(69.6f * t, n - 3.0f, 69.6f * t, n + 3.0f, &p);
			rodAdd(m, &p, (profile_t) {0.5f, 0.45f, 0.4f, 3.3f, 3, false}, false);
		}
	}
	profile_t knuckle = {2.0f, 2.0f, 1.98f, HINGE_Z, 6, true};
	segment(HINGE_X, 20.0f, HINGE_X, 42.0f, &p);
	rodAdd(m, &p, knuckle, false);
	segment(HINGE_X, -42.0f, HINGE_X, -20.0f, &p);
	rodAdd(m, &p, knuckle, false);
}

/* ---------------------------------------------------------------------------
 * Sheets: the flat clear parts, two faces each. Rings run from the outline in
 * to the middle (or a hole) so the light they mirror can vary across them.
 * ------------------------------------------------------------------------- */

#define SHEET_RINGS 4

typedef struct {
	int segs;	/* points round the outline */
	float *xy;	/* (SHEET_RINGS + 1) x segs points */
	float z0, z1;
} sheet_t;

static sheet_t traySheets[4], lidSheet;
static int traySheetCount;

static bool sheetBuild(sheet_t *s, const path_t *outer, const path_t *inner, float cx, float cy, float z0, float z1)
{
	s->segs = outer->n;
	s->z0 = z0;
	s->z1 = z1;
	s->xy = malloc((SHEET_RINGS + 1) * s->segs * 2 * sizeof(float));
	if(s->xy == NULL) return false;
	for(int r = 0; r <= SHEET_RINGS; r++) {
		float t = (float)r / SHEET_RINGS;
		for(int i = 0; i < s->segs; i++) {
			float ix = inner ? inner->p[i][0] : cx, iy = inner ? inner->p[i][1] : cy;
			s->xy[(r * s->segs + i) * 2 + 0] = mixf(outer->p[i][0], ix, t);
			s->xy[(r * s->segs + i) * 2 + 1] = mixf(outer->p[i][1], iy, t);
		}
	}
	return true;
}

static bool buildSheets(void)
{
	path_t o, in;
	bool ok = true;
	traySheetCount = 0;
	roundSquare(73.0f, 8.0f, &o);
	ok &= sheetBuild(&traySheets[traySheetCount++], &o, NULL, 0, 0, -5.2f, -3.8f);
	float sx = (-73.0f + -50.5f) / 2.0f, cx = (-50.5f - -73.0f) / 2.0f;
	roundRect(sx, 0, cx, 22.0f, 4.2f, &o);
	ok &= sheetBuild(&traySheets[traySheetCount++], &o, NULL, sx, 0, 1.0f, 2.6f);
	circlePts(0, 0, 19.4f, 1.4f, &o);
	/* The hole, with as many points as the outline. */
	in.n = 0;
	for(int i = 0; i < o.n; i++) {
		float a = (float)i / o.n * 2.0f * PI_F;
		pathPush(&in, 6.2f * cosf(a), 6.2f * sinf(a));
	}
	ok &= sheetBuild(&traySheets[traySheetCount++], &o, &in, 0, 0, -1.1f, -0.2f);
	circlePts(0, 0, 15.0f, 1.2f, &o);
	in.n = 0;
	for(int i = 0; i < o.n; i++) {
		float a = (float)i / o.n * 2.0f * PI_F;
		pathPush(&in, 7.5f * cosf(a), 7.5f * sinf(a));
	}
	ok &= sheetBuild(&traySheets[traySheetCount++], &o, &in, 0, 0, -2.4f, -1.2f);
	roundSquare(73.0f, 8.0f, &o);
	ok &= sheetBuild(&lidSheet, &o, NULL, 0, 0, 3.0f, 5.0f);
	return ok;
}

/* ---------------------------------------------------------------------------
 * The camera, and the case's place in the Library
 * ------------------------------------------------------------------------- */

#define FOV_Y 28.0f
#define CASE_HALF 73.0f
/* The case at rest stands this much of the original's size, to leave the
 * Library's heading above it and the title and publisher below, and is
 * raised this many stage pixels above the middle of the screen. */
#define VIEW_FILL 0.80f
#define VIEW_LIFT 34.0f

static float camDist;
static Mtx view;

/* The plastic's light was worked out against the original's white page:
 * over Indigo's dark backdrop it is dimmed and cooled to its lavender, and
 * lets more through. The lavender follows Menu Color. */
#define GLASS_TINT_R 0.34f
#define GLASS_TINT_G 0.33f
#define GLASS_TINT_B 0.50f
#define GLASS_TAKE 0.38f	/* share of the original's darkening of what's behind */
static float glassTint[3];

/* ---------------------------------------------------------------------------
 * Poses, as the original: a case at rest sways a little; one leaving falls
 * back and fades as the next comes in from behind it, swings out and lands
 * in front.
 * ------------------------------------------------------------------------- */

static float easeInOut(float e) { float u = -2 * e + 2; return e < 0.5f ? 4 * e * e * e : 1 - u * u * u / 2; }
static float easeIn(float e) { return e * e * e; }
static float easeOut(float e) { float u = 1 - e; return 1 - u * u * u; }
static float easeSpring(float e) { return 1 - expf(-6 * e) * cosf(3 * PI_F * e); }

typedef float (*ease_fn)(float);
typedef struct { float t, v; ease_fn ease; } kf_t;

/* Keyframes, each eased in from the one before. */
static float keys(float e, const kf_t *k, int n)
{
	if(e <= k[0].t) return k[0].v;
	for(int i = 1; i < n; i++) {
		if(e <= k[i].t) {
			ease_fn f = k[i].ease ? k[i].ease : easeInOut;
			return mixf(k[i - 1].v, k[i].v, f((e - k[i - 1].t) / (k[i].t - k[i - 1].t)));
		}
	}
	return k[n - 1].v;
}

typedef struct { float x, y, z, yaw, pitch, roll, s, fade; } pose_t;

/* sway: 1 for the original's, less for Reduced, 0 for Off. */
static pose_t restPose(float t, float seed, float sway)
{
	pose_t a = {0, 0, 0, -0.2f, -0.12f, 0.03f, 1, 1};
	if(sway <= 0.0f) return a;
	a.yaw += sway * (0.1f * sinf(0.9f * t + seed) + 0.03f * sinf(2.1f * t + 2 * seed));
	a.pitch += sway * 0.045f * sinf(0.7f * t + 1.7f * seed);
	a.roll += sway * 0.018f * sinf(0.5f * t + 0.7f * seed);
	a.y = sway * 1.8f * sinf(1.1f * t + seed);
	return a;
}

/* The original swaps cases over 1.15 s; here the Library's carousel takes
 * a case through the same keys, as far as it has come on its way: a case
 * at slot s, moving direction g, is on its way in when s and g agree and on
 * its way out when they don't. The original's rise and fall run across: its
 * y keys on x, its pitch as yaw. */
static pose_t slotPose(float slot, int direction, float t, float seed, float sway)
{
	pose_t a = restPose(t, seed, sway);
	float g = direction > 0 ? 1.0f : direction < 0 ? -1.0f : slot > 0.0f ? 1.0f : -1.0f;

	if(slot == 0.0f) return a;
	if(slot * g < 0.0f) {
		float v = sat(fabsf(slot));
		const kf_t fy[] = {{0, 0, NULL}, {.48f, -(.8f * g * 146), NULL}, {1, -(.12f * g * 146), NULL}};
		const kf_t fz[] = {{0, 0, NULL}, {.45f, -46, NULL}, {1, -82, easeIn}};
		const kf_t fs[] = {{0, 1, NULL}, {1, .62f, easeIn}};
		const kf_t fr[] = {{0, 0, NULL}, {.5f, .2f * g, NULL}, {1, .08f * g, NULL}};
		const kf_t fp[] = {{0, 0, NULL}, {.45f, .3f * g, NULL}, {1, 0, NULL}};
		const kf_t ff[] = {{0, 1, NULL}, {.55f, .8f, NULL}, {1, 0, easeIn}};
		a.x += keys(v, fy, 3);
		a.z += keys(v, fz, 3);
		a.s *= keys(v, fs, 2);
		a.roll += keys(v, fr, 3);
		a.yaw -= keys(v, fp, 3);
		a.fade = keys(v, ff, 3);
	}
	else {
		float v = sat(1.0f - fabsf(slot));
		const kf_t ty[] = {{0, .12f * g * 146, NULL}, {.42f, .9f * g * 146, easeOut}, {1, 0, easeInOut}};
		const kf_t tz[] = {{0, -55, NULL}, {.25f, -32, easeOut}, {.5f, 27, NULL}, {1, 0, NULL}};
		const kf_t ts[] = {{0, .62f, NULL}, {.42f, .9f, easeOut}, {1, 1, easeOut}};
		const kf_t tr[] = {{0, -.28f * g, NULL}, {.42f, .1f * g, NULL}, {1, 0, easeSpring}};
		const kf_t tp[] = {{0, .1f * g, NULL}, {.42f, -.4f * g, NULL}, {.8f, .14f * g, NULL}, {1, 0, NULL}};
		a.x += keys(v, ty, 3);
		a.z += keys(v, tz, 4);
		a.s *= keys(v, ts, 3);
		a.roll += keys(v, tr, 3);
		a.yaw -= keys(v, tp, 4);
	}
	return a;
}

#define SPIN_RATE (18.0f * PI_F / 180.0f)
/* Every restPose frequency is an integer multiple of 0.1 rad/s. This
 * period joins all of them, including each game's seed, without an hourly
 * jump or the loss of float precision from an ever-growing clock. */
#define SWAY_PERIOD (20.0f * PI_F)

static struct {
	float t;
	float spin, spinRate;
	/* The C-stick standing in for the original's drag. */
	float dragX, dragY, dragVX, dragVY;
} jc;

static void update(float dt, float turnX, float turnY, uiMotionMode_t motion,
	bool spin)
{
	jc.t += dt;
	if(jc.t >= SWAY_PERIOD) jc.t -= SWAY_PERIOD;
	bool turning = fabsf(turnX) > 12.0f / 90.0f || fabsf(turnY) > 12.0f / 90.0f;
	float tx = turning ? clampf(-turnY * 0.45f, -0.45f, 0.45f) : 0.0f;
	float ty = turning ? clampf(turnX * 0.9f, -0.9f, 0.9f) : 0.0f;
	if(motion == UI_MOTION_OFF) {
		/* Off freezes the print where it is and follows manual inspection
		 * without a coast. Z's intentional ease to rest remains below. */
		jc.spinRate = 0.0f;
		jc.dragX = tx;
		jc.dragY = ty;
		jc.dragVX = jc.dragVY = 0.0f;
		return;
	}
	float target = spin ? UIMotion_Amplitude(SPIN_RATE, motion) : 0.0f;
	jc.spinRate += (target - jc.spinRate) * (1 - expf(-2.7f * dt));
	jc.spin = fmodf(jc.spin + jc.spinRate * dt, 2 * PI_F);

	/* Follow the stick, and keep its speed for the flick when it's let go;
	 * let go, the case springs back. */
	if(turning) {
		float k = 1 - expf(-12.0f * dt);
		float nx = jc.dragX + (tx - jc.dragX) * k, ny = jc.dragY + (ty - jc.dragY) * k;
		if(dt > 0) { jc.dragVX = (nx - jc.dragX) / dt; jc.dragVY = (ny - jc.dragY) / dt; }
		jc.dragX = nx;
		jc.dragY = ny;
	}
	else {
		jc.dragVX += (-16 * jc.dragX - 5.5f * jc.dragVX) * dt;
		jc.dragVY += (-16 * jc.dragY - 5.5f * jc.dragVY) * dt;
		jc.dragX += jc.dragVX * dt;
		jc.dragY += jc.dragVY * dt;
	}
}

/* ---------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */

typedef struct {
	const uiJewelCaseShot_t *shot;
	pose_t pose;
	Mtx mv;	/* model to view, the shut lid's as well */
	float scale;
	float depthTray, depthLid;
} shown_t;

static void eulerXYZ(Mtx r, float x, float y, float z)
{
	Mtx rx, ry, rz, t;
	guMtxRotRad(rx, 'x', x);
	guMtxRotRad(ry, 'y', y);
	guMtxRotRad(rz, 'z', z);
	guMtxConcat(rx, ry, t);
	guMtxConcat(t, rz, r);
}

static void placeCase(shown_t *c)
{
	pose_t *t = &c->pose;
	Mtx r, s, m;
	eulerXYZ(r, t->pitch + jc.dragX, t->yaw + jc.dragY, t->roll);
	guMtxScale(s, t->s, t->s, t->s);
	guMtxConcat(r, s, m);
	m[0][3] = t->x;
	m[1][3] = t->y;
	m[2][3] = t->z;
	guMtxConcat(view, m, c->mv);
	c->scale = t->s;
	v3 p;
	xform(c->mv, V3(0, 0, -2), &p);
	c->depthTray = p.z;
	xform(c->mv, V3(0, 0, 4), &p);
	c->depthLid = p.z;
}

static void loadProjection(void)
{
	Mtx44 proj;
	guPerspective(proj, FOV_Y, 640.0f / 480.0f, 5.0f, 4000.0f);
	proj[1][2] -= VIEW_LIFT / 240.0f;
	UIStage_Project(proj);
	GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
}

static void directVtx(bool tex)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	if(tex) GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(VTXFMT_DIR, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VTXFMT_DIR, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(VTXFMT_DIR, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}

static void colorPipeline(void)
{
	directVtx(false);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevDirect(GX_TEVSTAGE0);
}

static inline void emitColor(float x, float y, float z, u8 r, u8 g, u8 b, u8 a)
{
	GX_Position3f32(x, y, z);
	GX_Color4u8(r, g, b, a);
}

/* The disc ---------------------------------------------------------------- */

#define DISC_SEGS 48
#define DISC_LIGHT_STEP 2	/* the silver is lit on every other point round, DISC_SEGS a multiple */
static const float discRings[] = {15.0f, 16.0f, 16.4f, 16.6f, 16.8f, 17.2f, 19.0f, 19.9f, 20.3f, 22.0f, 30.0f, 40.0f, 50.0f, 58.0f, 58.6f, 59.1f, 59.5f, 60.0f};
#define DISC_RINGS (sizeof(discRings) / sizeof(discRings[0]))
#define ART_IN 20.1f
#define PRINT_OUT 59.1f
static float discCos[DISC_SEGS + 1], discSin[DISC_SEGS + 1];

/* The bare disc: aluminium under lacquer, a bright satin mirror with a
 * little of the room in it. */
static v3 silver(v3 R, v3 N, float r)
{
	v3 st = studio(R);
	v3 c = vadd(vmul(vmin(st, 1.6f), 0.5f), vmul(V3(0.42f, 0.44f, 0.47f), 0.6f + 0.4f * fmaxf(vdot(N, KEY), 0.0f)));
	/* The stacking ring in the mirror band. */
	c = vmul(c, 1.0f - 0.35f * (1.0f - smoothstep(0.15f, 0.4f, fabsf(r - 16.6f))));
	/* And its last millimetre is clear plastic over the silver's edge. */
	float rim = smoothstep(58.0f, 58.9f, r);
	if(rim <= 0) return c;
	return vmix(c, vadd(vmul(st, 0.35f), V3(0.22f, 0.23f, 0.25f)), rim * 0.85f);
}

/* The rainbow a light draws through the middle of the pressed track, as a
 * strip of quads. */
static void drawSpokes(Mtx mv, float zFace, float nzLocal, float fade)
{
	const v3 *L[4] = {&KEY, &STRIP, &TOP, &BOUNCE};
	const float gain[4] = {1.6f, 1.4f, 0.9f, 0.35f}, spread[4] = {0.05f, 0.045f, 0.06f, 0.16f};
	v3 centre, N, ex, ey;
	if(fade <= 0.0f) return;
	xform(mv, V3(0, 0, zFace), &centre);
	xformDir(mv, V3(0, 0, nzLocal), &N);
	xformDir(mv, V3(1, 0, 0), &ex);
	xformDir(mv, V3(0, 1, 0), &ey);
	float sc = vlen(ex);
	N = vnorm(N);
	ex = vnorm(ex);
	ey = vnorm(ey);
	v3 V0 = vnorm(vmul(centre, -1));
	if(vdot(N, V0) <= 0.0f) return;

	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
	for(int l = 0; l < 4; l++) {
		v3 H = vadd(*L[l], V0);
		float hx = vdot(H, ex), hy = vdot(H, ey);
		float hl = caseSqrt(hx * hx + hy * hy);
		float theta = atan2f(hy, hx);
		float dth = fminf(spread[l] * 2.2f / fmaxf(hl, 1e-3f), 0.5f);
		for(int side = 0; side < 2; side++) {
			float th = theta + side * PI_F;
			enum { RS = 12, CS = 4 };
			/* The directions across the band, and the silver under its two
			 * edges on each ring, to blend between (it is narrow, the silver
			 * smooth across it). */
			float ca[CS + 1], sa[CS + 1];
			for(int ci = 0; ci <= CS; ci++) {
				float a = th + dth * ((ci / (float)CS) * 2.0f - 1.0f);
				ca[ci] = cosf(a);
				sa[ci] = sinf(a);
			}
			v3 under[RS + 1][2];
			float data[RS + 1];
			for(int ri = 0; ri <= RS; ri++) {
				float r = 21.5f + (58.8f - 21.5f) * ri / RS;
				for(int e = 0; e < 2; e++) {
					v3 P;
					xform(mv, V3(r * ca[e * CS], r * sa[e * CS], zFace), &P);
					v3 V = vnorm(vmul(P, -1));
					under[ri][e] = silver(vreflect(vmul(V, -1), N), N, r);
				}
				data[ri] = smoothstep(21.5f, 23.0f, r) * (1 - smoothstep(58.2f, 58.8f, r));
			}
			for(int ci = 0; ci < CS; ci++) {
				GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, (RS + 1) * 2);
				for(int ri = 0; ri <= RS; ri++) {
					float r = 21.5f + (58.8f - 21.5f) * ri / RS;
					for(int k = 0; k < 2; k++) {
						float cs = ca[ci + k], sn = sa[ci + k];
						float x = r * cs, y = r * sn;
						v3 P;
						xform(mv, V3(x, y, zFace), &P);
						v3 V = vnorm(vmul(P, -1));
						v3 h = vadd(*L[l], V);
						v3 rad = vadd(vmul(ex, cs), vmul(ey, sn));
						v3 tan = vadd(vmul(ex, -sn), vmul(ey, cs));
						float along = vdot(h, tan), u = fabsf(vdot(h, rad));
						float w = expf(-along * along / (spread[l] * spread[l]));
						v3 c = vadd(vadd(spectral(1600 * u), vmul(spectral(800 * u), 0.75f)), vmul(spectral(533.3f * u), 0.45f));
						c = vmul(c, w * gain[l] * 1.3f * data[ri] * fade);
						/* Added on top of the mirror, in display light. */
						v3 base = vmix(under[ri][0], under[ri][1], (float)(ci + k) / CS);
						u8 cr = toSrgb8(base.x + c.x) - toSrgb8(base.x);
						u8 cg = toSrgb8(base.y + c.y) - toSrgb8(base.y);
						u8 cb = toSrgb8(base.z + c.z) - toSrgb8(base.z);
						emitColor(x, y, zFace + nzLocal * 0.02f / sc, cr, cg, cb, 255);
					}
				}
				GX_End();
			}
		}
	}
}

/* The print turns with the disc: rotate the texture rather than the mesh,
 * laying the disc's 120 mm over the shot's square of it. */
static void loadPrintMtx(float spin, const float rect[4])
{
	Mtx tm;
	float cs = cosf(spin), sn = sinf(spin);
	float ks = (rect[2] - rect[0]) / 120.0f, kt = (rect[3] - rect[1]) / 120.0f;
	guMtxIdentity(tm);
	tm[0][0] = cs * ks; tm[0][1] = -sn * ks; tm[0][3] = (rect[0] + rect[2]) * 0.5f;
	tm[1][0] = -sn * kt; tm[1][1] = -cs * kt; tm[1][3] = (rect[1] + rect[3]) * 0.5f;
	GX_LoadTexMtxImm(tm, GX_TEXMTX2, GX_MTX2x4);
}

static void drawDisc(shown_t *c)
{
	Mtx mv;
	guMtxCopy(c->mv, mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	float fade = c->pose.fade;
	u8 fa = (u8)(sat(fade) * 255);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

	v3 N, NB;
	xformDir(mv, V3(0, 0, 1), &N);
	N = vnorm(N);
	NB = vmul(N, -1);
	GXTexObj *print = c->shot->print;
	float printAlpha = print != NULL ? sat(c->shot->printAlpha) : 0.0f;
	const float zf = -1.2f, zb = -2.4f;
	/* Only the face turned to the camera is drawn: the disc is thinner than
	 * the depth buffer can tell apart. */
	v3 V0 = vnorm(V3(-mv[0][3], -mv[1][3], -mv[2][3]));
	bool back = vdot(N, V0) < 0;
	bool printing = print != NULL && printAlpha > 0.0f && !back;

	/* The printed side, or the bare silver when there's no print. */
	if(printing) {
		directVtx(true);
		GX_SetNumChans(1);
		GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
		GX_SetNumTexGens(1);
		loadPrintMtx(jc.spin, c->shot->rect);
		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX2);
		GX_SetNumIndStages(0);
		GX_SetNumTevStages(2);
		GX_LoadTexObj(print, GX_TEXMAP0);
		/* Stage 0: the ink, lit by the room; its alpha is the print's where
		 * it's printed. */
		float lit = powf(lights(N), 1.0f / 2.2f);
		u8 kl = (u8)(clampf(lit * 0.5f, 0, 1) * 255);
		GX_SetTevKColor(GX_KCOLOR0, (GXColor) {kl, kl, kl, fa});
		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GX_SetTevDirect(GX_TEVSTAGE0);
		GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
		GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		/* Stage 1: over the silver. */
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
		GX_SetTevDirect(GX_TEVSTAGE1);
		GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K0_A);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_RASC, GX_CC_CPREV, GX_CC_APREV, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
		GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	}
	else {
		colorPipeline();
	}

	/* Ring by ring; under the print the silver can't be seen, so it takes one
	 * colour there. */
	{
		v3 Nf = back ? NB : N;
		float z = back ? zb : zf;
		v3 flat = silver(vreflect(vmul(V0, -1), Nf), Nf, 40.0f);
		u8 flatC[3] = {toSrgb8(flat.x), toSrgb8(flat.y), toSrgb8(flat.z)};
		/* Each ring's colours once: the silver on every other point, the
		 * rest between. */
		static u8 ringCol[DISC_RINGS][DISC_SEGS + 1][4];
		for(u32 ri = 0; ri < DISC_RINGS; ri++) {
			float r = discRings[ri];
			float printed = printing ? smoothstep(ART_IN - 0.2f, ART_IN + 0.2f, r) * (1 - smoothstep(PRINT_OUT, PRINT_OUT + 0.4f, r)) : 0.0f;
			u8 alpha = printing ? (u8)(printed * printAlpha * 255) : fa;
			u8 (*row)[4] = ringCol[ri];
			if(printing && printAlpha >= 1.0f && r >= ART_IN + 1.5f && r <= 57.5f) {
				for(int si = 0; si <= DISC_SEGS; si++) {
					row[si][0] = flatC[0]; row[si][1] = flatC[1]; row[si][2] = flatC[2]; row[si][3] = alpha;
				}
				continue;
			}
			for(int si = 0; si <= DISC_SEGS; si += DISC_LIGHT_STEP) {
				v3 P;
				xform(mv, V3(r * discCos[si], r * discSin[si], z), &P);
				v3 V = vnorm(vmul(P, -1));
				v3 col = silver(vreflect(vmul(V, -1), Nf), Nf, r);
				row[si][0] = toSrgb8(col.x); row[si][1] = toSrgb8(col.y); row[si][2] = toSrgb8(col.z); row[si][3] = alpha;
			}
			for(int si = 0; si < DISC_SEGS; si += DISC_LIGHT_STEP) {
				for(int j = 1; j < DISC_LIGHT_STEP; j++) {
					for(int k = 0; k < 4; k++) {
						row[si + j][k] = (u8)((row[si][k] * (DISC_LIGHT_STEP - j) + row[si + DISC_LIGHT_STEP][k] * j) / DISC_LIGHT_STEP);
					}
				}
			}
		}
		for(u32 ri = 0; ri + 1 < DISC_RINGS; ri++) {
			GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, (DISC_SEGS + 1) * 2);
			for(int si = 0; si <= DISC_SEGS; si++) {
				for(int k = 0; k < 2; k++) {
					float r = discRings[ri + k];
					float x = r * discCos[si], y = r * discSin[si];
					const u8 *col = ringCol[ri + k][si];
					GX_Position3f32(x, y, z);
					GX_Color4u8(col[0], col[1], col[2], col[3]);
					if(printing) GX_TexCoord2f32(x, y);
				}
			}
			GX_End();
		}
	}

	/* The edge: a CD's clear plastic seen end on. */
	colorPipeline();
	GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, (DISC_SEGS + 1) * 2);
	for(int si = 0; si <= DISC_SEGS; si++) {
		v3 n, P;
		xformDir(mv, V3(discCos[si], discSin[si], 0), &n);
		n = vnorm(n);
		for(int k = 0; k < 2; k++) {
			float z = k ? zb : zf;
			xform(mv, V3(60 * discCos[si], 60 * discSin[si], z), &P);
			v3 V = vnorm(vmul(P, -1));
			v3 col = vadd(V3(0.3f, 0.31f, 0.33f), vmul(studio(vreflect(vmul(V, -1), n)), 0.25f));
			emitColor(60 * discCos[si], 60 * discSin[si], z, toSrgb8(col.x), toSrgb8(col.y), toSrgb8(col.z), fa);
		}
	}
	GX_End();

	/* The rainbow on the data side, and on the front while nothing is
	 * printed there, fading as a print comes in. */
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	if(back) drawSpokes(mv, zb, -1.0f, fade);
	else drawSpokes(mv, zf, 1.0f, fade * (1.0f - printAlpha));
}

/* The plastic -------------------------------------------------------------- */

static void drawRods(rodmesh_t *m, Mtx mv, float scale, float fade)
{
	rodSetupVtx();
	GX_SetArray(GX_VA_POS, m->pos, 12);
	GX_SetArray(GX_VA_NRM, m->nrm, 12);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	/* The normal in view space picks the texel: u = 0.5 + 0.5 nx,
	 * v = 0.5 - 0.5 ny. */
	Mtx tm;
	guMtxIdentity(tm);
	for(int i = 0; i < 3; i++) {
		tm[0][i] = 0.5f * mv[0][i] / scale;
		tm[1][i] = -0.5f * mv[1][i] / scale;
	}
	tm[0][3] = 0.5f;
	tm[1][3] = 0.5f;
	GX_LoadTexMtxImm(tm, GX_TEXMTX1, GX_MTX2x4);
	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_NRM, GX_TEXMTX1);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_LoadTexObj(&matcapTex, GX_TEXMAP0);
	float f = sat(fade);
	GX_SetTevColor(GX_TEVREG0, (GXColor) {(u8)(f * glassTint[0] * 255), (u8)(f * glassTint[1] * 255),
		(u8)(f * glassTint[2] * 255), (u8)(f * GLASS_TAKE * 255)});
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	/* Colour: the studio's share; alpha: how much of what's behind comes
	 * through (1 - a0 + a0 * texa: a0 below the fade lets more through). */
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_KONST, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_SRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);
	GX_CallDispList(m->dl, m->dlSize);
}

/* A flat sheet: face on it takes almost nothing away, at a slant it turns to
 * a mirror. Over the dark backdrop: the light it adds, plainly, and less
 * taken away than over the original's page. */
static void sheetShade(v3 P, v3 N, float thick, float fade, u8 *rgba)
{
	v3 V = vnorm(vmul(P, -1));
	float d = vdot(N, V);
	float cosv = fabsf(d);
	if(d < 0) N = vmul(N, -1);
	float F = fresnel(cosv, IOR);
	/* Face on: fresnel(1, IOR) and the same through both surfaces. */
	const float F0 = ((IOR - 1) / (IOR + 1)) * ((IOR - 1) / (IOR + 1));
	const float Fall0 = F0 + (1 - F0) * (1 - F0) * F0;
	float Fall = F + (1 - F) * (1 - F) * F;
	float cosT = caseSqrt(fmaxf(1 - (1 - cosv * cosv) / (IOR * IOR), 0));
	float absorbed = 1 - expf(-0.00327f * thick / fmaxf(cosT, 0.2f));
	float takes = sat(fmaxf(Fall - Fall0, 0) + Fall0 * 0.05f + absorbed * 0.5f);
	v3 R = vreflect(vmul(V, -1), N);
	v3 g = vmin(vmul(glare(R), Fall * 0.25f), 0.1f);
	v3 refl = vadd(g, vmul(vmin(roomT(R), 1.2f), fmaxf(Fall - Fall0, 0)));
	refl = vadd(refl, vmul(V3(0.86f, 0.9f, 0.93f), (1 - cosv) * 0.03f));
	refl = vmul(refl, fade);
	takes *= fade;
	rgba[0] = (u8)(sat(refl.x * glassTint[0] * 2) * 255);
	rgba[1] = (u8)(sat(refl.y * glassTint[1] * 2) * 255);
	rgba[2] = (u8)(sat(refl.z * glassTint[2] * 2) * 255);
	rgba[3] = (u8)(takes * GLASS_TAKE * 255);
}

static void drawSheet(const sheet_t *s, Mtx mv, float scale, float fade)
{
	v3 n;
	xformDir(mv, V3(0, 0, 1), &n);
	n = vnorm(n);
	float thick = (s->z1 - s->z0) * scale;
	/* Both faces look the same from a point so close: shade each point once,
	 * and only every other point of every other ring, the light being smooth
	 * across it. */
	static u8 shade[(SHEET_RINGS + 1) * 128][4];
	int segs = s->segs, pts = (SHEET_RINGS + 1) * segs;
	if(pts > (int)(sizeof(shade) / sizeof(shade[0])) || (segs & 1)) return;
	float zm = (s->z0 + s->z1) / 2;
	for(int r = 0; r <= SHEET_RINGS; r += 2) {
		for(int i = 0; i < segs; i += 2) {
			int idx = r * segs + i;
			v3 P;
			xform(mv, V3(s->xy[idx * 2], s->xy[idx * 2 + 1], zm), &P);
			sheetShade(P, n, thick, fade, shade[idx]);
		}
		for(int i = 1; i < segs; i += 2) {
			const u8 *a = shade[r * segs + i - 1], *b = shade[r * segs + (i + 1) % segs];
			u8 *d = shade[r * segs + i];
			for(int c = 0; c < 4; c++) d[c] = (a[c] + b[c] + 1) >> 1;
		}
	}
	for(int r = 1; r < SHEET_RINGS; r += 2) {
		for(int i = 0; i < segs; i++) {
			const u8 *a = shade[(r - 1) * segs + i], *b = shade[(r + 1) * segs + i];
			u8 *d = shade[r * segs + i];
			for(int c = 0; c < 4; c++) d[c] = (a[c] + b[c] + 1) >> 1;
		}
	}
	/* And both faces in one: over what's behind, the second takes (1 - a) of
	 * the first's light and darkens the same again. */
	for(int i = 0; i < pts; i++) {
		u8 *d = shade[i];
		int keep = 255 - d[3];
		for(int c = 0; c < 3; c++) { int v = d[c] + d[c] * keep / 255; d[c] = v > 255 ? 255 : v; }
		d[3] = 255 - keep * keep / 255;
	}
	for(int r = 0; r < SHEET_RINGS; r++) {
		GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, (segs + 1) * 2);
		for(int i = 0; i <= segs; i++) {
			for(int k = 0; k < 2; k++) {
				int idx = (r + k) * segs + i % segs;
				const float *p = &s->xy[idx * 2];
				emitColor(p[0], p[1], zm, shade[idx][0], shade[idx][1], shade[idx][2], shade[idx][3]);
			}
		}
		GX_End();
	}
}

/* The lid's sheen: the streaks a strip light leaves on a glass top. Where on
 * the face they fall follows how the face is turned: the line of sight
 * reflected in it, measured against the light, moves them across the face
 * (with some gain, as the case is a lot smaller than the light is far), so
 * they sweep over the lid as it tilts. They run corner to corner; their
 * sharp profile is a small texture looked up from a few points, along which
 * the measure changes smoothly. */

#define SHEEN_GRID 6	/* points along each side of the face */
#define SHEEN_SPAN 0.6f	/* how far either way (in face widths) the texture reaches */
#define SHEEN_GAIN 1.3f	/* face widths the streaks move per radian of reflection */
#define SHEEN_REST 0.45f	/* the reflection's measure at rest, where they start out */
#define SHEEN_AT 0.22f	/* and where on the face that puts them */
#define SHEEN_TEX 128
static u8 sheenData[SHEEN_TEX * 4] ATTRIBUTE_ALIGN(32);
static GXTexObj sheenTex;
static float sheenPts[SHEEN_GRID * SHEEN_GRID][2];

static void buildSheen(void)
{
	/* Across the streaks: a bright one with a soft halo, and a thin one
	 * beside it (I8, 8x4 tiles). */
	for(int x = 0; x < SHEEN_TEX; x++) {
		float d = ((x + 0.5f) / SHEEN_TEX * 2 - 1) * SHEEN_SPAN;
		float e = (d - 0.13f) / 0.012f;
		float k = 0.75f * expf(-(d / 0.035f) * (d / 0.035f)) + 0.25f * expf(-(d / 0.16f) * (d / 0.16f)) + 0.45f * expf(-e * e);
		for(int y = 0; y < 4; y++) sheenData[(x / 8) * 32 + y * 8 + (x % 8)] = (u8)(sat(k) * 255);
	}
	DCFlushRange(sheenData, sizeof(sheenData));
	GX_InitTexObj(&sheenTex, sheenData, SHEEN_TEX, 4, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&sheenTex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_DISABLE, GX_DISABLE, GX_ANISO_1);
	/* A grid over the lid's face, its corners pulled in to the rounded ones. */
	const float e = CASE_HALF - 0.5f, rc = 8.0f, c = e - rc;
	for(int j = 0; j < SHEEN_GRID; j++) {
		for(int i = 0; i < SHEEN_GRID; i++) {
			float x = -e + 2 * e * i / (SHEEN_GRID - 1), y = -e + 2 * e * j / (SHEEN_GRID - 1);
			float dx = fabsf(x) - c, dy = fabsf(y) - c;
			if(dx > 0 && dy > 0) {
				float l = sqrtf(dx * dx + dy * dy), k = rc / l;
				x = copysignf(c + dx * k, x);
				y = copysignf(c + dy * k, y);
			}
			sheenPts[j * SHEEN_GRID + i][0] = x;
			sheenPts[j * SHEEN_GRID + i][1] = y;
		}
	}
}

static void drawSheen(Mtx mv, float z, float fade)
{
	if(fade <= 0) return;
	v3 N;
	xformDir(mv, V3(0, 0, 1), &N);
	N = vnorm(N);
	/* Square to the light's strip, in view space: tilting the top of the case
	 * towards the upper left takes the streaks that way. */
	static const v3 A = {-0.62f, 0.70f, 0.0f};
	float u[SHEEN_GRID * SHEEN_GRID];
	bool any = false;
	for(int i = 0; i < SHEEN_GRID * SHEEN_GRID; i++) {
		v3 P;
		xform(mv, V3(sheenPts[i][0], sheenPts[i][1], z), &P);
		v3 V = vnorm(vmul(P, -1));
		v3 n = vdot(N, V) < 0 ? vmul(N, -1) : N;
		v3 R = vreflect(vmul(V, -1), n);
		/* Across the face, corner to corner, in face widths. */
		float q = (sheenPts[i][1] - sheenPts[i][0]) / (2 * 1.41421f * CASE_HALF);
		float d = q - (SHEEN_AT + SHEEN_GAIN * (vdot(R, A) - SHEEN_REST));
		u[i] = 0.5f + d / (2 * SHEEN_SPAN);
		if(fabsf(d) < SHEEN_SPAN) any = true;
	}
	/* Nowhere near the face: nothing to draw. */
	if(!any) return;

	directVtx(true);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_LoadTexObj(&sheenTex, GX_TEXMAP0);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	/* Added light, a little cooler over the backdrop. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	float k = sat(fade) * 0.42f;
	u8 cr = (u8)(k * 225), cg = (u8)(k * 222), cb = (u8)(k * 255);
	UIColor_Apply(&cr, &cg, &cb);
	for(int j = 0; j + 1 < SHEEN_GRID; j++) {
		GX_Begin(GX_TRIANGLESTRIP, VTXFMT_DIR, SHEEN_GRID * 2);
		for(int i = 0; i < SHEEN_GRID; i++) {
			for(int r = 0; r < 2; r++) {
				int idx = (j + r) * SHEEN_GRID + i;
				GX_Position3f32(sheenPts[idx][0], sheenPts[idx][1], z);
				GX_Color4u8(cr, cg, cb, 255);
				GX_TexCoord2f32(u[idx], 0.5f);
			}
		}
		GX_End();
	}
}

static void drawSheets(const sheet_t *sheets, int n, Mtx mv, float scale, float fade)
{
	colorPipeline();
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_FALSE);
	for(int i = 0; i < n; i++) drawSheet(&sheets[i], mv, scale, fade);
}

/* Stars --------------------------------------------------------------------- */

/* Where a rod mirrors one of the studio's hot lamps the original throws a
 * star: a glint that streaks out in eight directions, tinting what it lies
 * over a cool blue, white-hot in the middle. */
#define MAX_STARS 2
#define GLINT_TOL 0.3f
#define GLINT_MIN 0.3f
typedef struct { float x, y, k; } star_t;

static v3 glintLamps[4];
static float glintCosTol;
static u8 glowData[64 * 64] ATTRIBUTE_ALIGN(32);
static GXTexObj glowTex;

/* A soft round glow, I8 in 8x4 tiles. */
static void buildGlow(void)
{
	for(int y = 0; y < 64; y++) {
		for(int x = 0; x < 64; x++) {
			float dx = (x - 31.5f) / 32.0f, dy = (y - 31.5f) / 32.0f, d = 1.0f - sqrtf(dx * dx + dy * dy);
			glowData[((y / 4) * 8 + (x / 8)) * 32 + (y % 4) * 8 + (x % 8)] = (u8)(d <= 0 ? 0 : d * d * 255);
		}
	}
	DCFlushRange(glowData, sizeof(glowData));
	GX_InitTexObj(&glowTex, glowData, 64, 64, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&glowTex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_DISABLE, GX_DISABLE, GX_ANISO_1);
	for(int l = 0; l < 4; l++) glintLamps[l] = vnorm(V3(studioLamps[l].x, studioLamps[l].y, 1.0f));
	glintCosTol = cosf(GLINT_TOL);
}

static void findGlints(const rodmesh_t *m, Mtx mv, float scale, float fade, star_t *stars, int *n)
{
	Mtx44 proj;
	guPerspective(proj, FOV_Y, 640.0f / 480.0f, 5.0f, 4000.0f);
	const float lift = VIEW_LIFT / 240.0f;
	/* The case is small in the picture: one direction to the camera serves
	 * all of it. */
	v3 V = vnorm(V3(-mv[0][3], -mv[1][3], -mv[2][3]));
	for(int i = 0; i < m->glintEnd; i++) {
		const float *nn = m->nrm + i * 3;
		v3 N;
		xformDir(mv, V3(nn[0], nn[1], nn[2]), &N);
		N = vmul(N, 1.0f / scale);
		float cosv = vdot(N, V);
		if(cosv <= 0.05f) continue;
		v3 R = vreflect(vmul(V, -1), N);
		float best = 0;
		for(int l = 0; l < 4; l++) {
			float d = vdot(R, glintLamps[l]);
			if(d < glintCosTol) continue;
			float ang = acosf(fminf(d, 1.0f));
			/* The profile is sampled every 20-odd degrees: allow for that,
			 * but fall off fast. */
			float k = 1 - ang / GLINT_TOL;
			best = fmaxf(best, k * k * k * k * fresnel(cosv, IOR) * 16.0f);
		}
		if(best < GLINT_MIN) continue;
		best *= fade;
		const float *pp = m->pos + i * 3;
		v3 P;
		xform(mv, V3(pp[0], pp[1], pp[2]), &P);
		/* To the stage, as the 2D pass draws it. */
		float cx = proj[0][0] * P.x, cw = -P.z;
		float cy = proj[1][1] * P.y + lift * cw;
		float sx = (cx / cw * 0.5f + 0.5f) * 640, sy = (0.5f - cy / cw * 0.5f) * 480;
		/* Keep the strongest few, apart from each other. */
		int j;
		for(j = 0; j < *n; j++) {
			float dx = stars[j].x - sx, dy = stars[j].y - sy;
			if(dx * dx + dy * dy < 30 * 30) break;
		}
		if(j < *n) {
			if(best > stars[j].k) stars[j] = (star_t) {sx, sy, best};
			continue;
		}
		if(*n < MAX_STARS) stars[(*n)++] = (star_t) {sx, sy, best};
		else {
			int w = 0;
			for(int q = 1; q < *n; q++) if(stars[q].k < stars[w].k) w = q;
			if(best > stars[w].k) stars[w] = (star_t) {sx, sy, best};
		}
	}
}

/* The 2D stage's projection, Menu Widescreen's squeeze and all. */
static void stagePipeline(void)
{
	Mtx m;
	Mtx44 o;
	guMtxIdentity(m);
	GX_LoadPosMtxImm(m, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	guOrtho(o, 0, 480, 0, 640, 0, 1);
	UIStage_Project(o);
	GX_LoadProjectionMtx(o, GX_ORTHOGRAPHIC);
	colorPipeline();
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

static void fillCircle(float cx, float cy, float r, GXColor c)
{
	GX_Begin(GX_TRIANGLEFAN, VTXFMT_DIR, 18);
	emitColor(cx, cy, 0, c.r, c.g, c.b, c.a);
	for(int i = 0; i <= 16; i++) {
		emitColor(cx + r * discCos[i * 3], cy + r * discSin[i * 3], 0, c.r, c.g, c.b, c.a);
	}
	GX_End();
}

static void glowQuad(float cx, float cy, float size, GXColor c)
{
	directVtx(true);
	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_LoadTexObj(&glowTex, GX_TEXMAP0);
	float h = size / 2;
	const float corner[4][4] = {{-1, -1, 0, 0}, {1, -1, 1, 0}, {1, 1, 1, 1}, {-1, 1, 0, 1}};
	GX_Begin(GX_QUADS, VTXFMT_DIR, 4);
	for(int i = 0; i < 4; i++) {
		GX_Position3f32(cx + corner[i][0] * h, cy + corner[i][1] * h, 0);
		GX_Color4u8(c.r, c.g, c.b, c.a);
		GX_TexCoord2f32(corner[i][2], corner[i][3]);
	}
	GX_End();
	colorPipeline();
}

static void streak(float x, float y, float dx, float dy, float len, float width, GXColor c)
{
	float nx = -dy * width, ny = dx * width;
	GX_Begin(GX_TRIANGLEFAN, VTXFMT_DIR, 6);
	emitColor(x, y, 0, c.r, c.g, c.b, c.a);
	emitColor(x + nx, y + ny, 0, c.r, c.g, c.b, c.a / 3);
	emitColor(x + dx * len, y + dy * len, 0, c.r, c.g, c.b, 0);
	emitColor(x - nx, y - ny, 0, c.r, c.g, c.b, c.a / 3);
	emitColor(x - dx * len, y - dy * len, 0, c.r, c.g, c.b, 0);
	emitColor(x + nx, y + ny, 0, c.r, c.g, c.b, c.a / 3);
	GX_End();
}

static void drawStars(const star_t *stars, int n)
{
	if(!n) return;
	stagePipeline();
	for(int i = 0; i < n; i++) {
		/* Softer over the dark backdrop, where they stand out more. */
		float k = sat(stars[i].k) * 0.6f;
		u8 a = (u8)(sat(k * 1.6f) * 0.75f * 255);
		GXColor blue = {107, 158, 255, a}, glow = {130, 175, 255, a};
		UIColor_Apply(&blue.r, &blue.g, &blue.b);
		UIColor_Apply(&glow.r, &glow.g, &glow.b);
		/* The original's streaks run longest up and down. */
		float len = 16 + 30 * k;
		streak(stars[i].x, stars[i].y, 0, 1, len, 0.8f, blue);
		streak(stars[i].x, stars[i].y, 1, 0, len * 0.45f, 0.8f, blue);
		streak(stars[i].x, stars[i].y, 0.7071f, 0.7071f, len * 0.2f, 0.6f, blue);
		streak(stars[i].x, stars[i].y, 0.7071f, -0.7071f, len * 0.2f, 0.6f, blue);
		glowQuad(stars[i].x, stars[i].y, 8 + 8 * k, glow);
		/* The white-hot middle. */
		GXColor white = {255, 255, 255, (u8)(sat(k * 1.6f) * 255)};
		streak(stars[i].x, stars[i].y, 0, 1, len * 0.35f, 0.6f, white);
		streak(stars[i].x, stars[i].y, 1, 0, len * 0.15f, 0.6f, white);
		fillCircle(stars[i].x, stars[i].y, 0.9f + k, (GXColor) {255, 255, 255, (u8)(k * 255)});
	}
}

/* Set-up and the frame ------------------------------------------------------ */

static bool prepared;
static bool recorded;
static bool recordFailed;

static void releaseAll(void)
{
	free(roomTable);
	free(studioTable);
	free(matcapData);
	roomTable = studioTable = NULL;
	matcapData = NULL;
	rodFree(&trayRods);
	rodFree(&lidRods);
	for(int i = 0; i < 4; i++) {
		free(traySheets[i].xy);
		traySheets[i].xy = NULL;
	}
	free(lidSheet.xy);
	lidSheet.xy = NULL;
}

bool UIJewelCase_Prepare(void)
{
	if(__atomic_load_n(&prepared, __ATOMIC_ACQUIRE)) return true;
	initSrgb();
	for(int i = 0; i <= DISC_SEGS; i++) {
		discCos[i] = cosf(i * 2 * PI_F / DISC_SEGS);
		discSin[i] = sinf(i * 2 * PI_F / DISC_SEGS);
	}
	buildTrayRods(&trayRods);
	buildLidRods(&lidRods);
	if(!buildMatcap() || !buildRoomTable() || !buildSheets() || !rodPack(&trayRods) || !rodPack(&lidRods)) {
		releaseAll();
		return false;
	}
	buildSheen();
	buildGlow();
	/* The camera stands back far enough for the case to fill 60% of the
	 * height in the original, VIEW_FILL of that here. */
	float c = tanf(14.0f * PI_F / 180.0f), aspect = 640.0f / 480.0f;
	camDist = fmaxf(146.0f / 0.6f / 2.0f / c, 94.9f / (c * aspect)) / VIEW_FILL;
	guMtxIdentity(view);
	view[2][3] = -camDist;
	recorded = recordFailed = false;
	__atomic_store_n(&prepared, true, __ATOMIC_RELEASE);
	return true;
}

bool UIJewelCase_Ready(void)
{
	return __atomic_load_n(&prepared, __ATOMIC_ACQUIRE) && !recordFailed;
}

typedef struct { int caseIdx; bool lid; float depth; } part_t;

void UIJewelCase_Draw(const uiJewelCaseShot_t *shots, int count, int direction,
	float seconds, float turnX, float turnY, uiMotionMode_t motion, bool spin)
{
	if(!UIJewelCase_Ready()) return;
	if(!recorded) {
		recorded = true;
		if(!rodRecord(&trayRods) || !rodRecord(&lidRods)) {
			recordFailed = true;
			return;
		}
	}
	update(clampf(seconds, 0.0f, 0.1f), turnX, turnY, motion, spin);

	/* The lavender, in this frame's Menu Color. */
	u8 tint[3] = {(u8)(GLASS_TINT_R * 255), (u8)(GLASS_TINT_G * 255), (u8)(GLASS_TINT_B * 255)};
	UIColor_Apply(&tint[0], &tint[1], &tint[2]);
	for(int i = 0; i < 3; i++) glassTint[i] = tint[i] / 255.0f;

	/* Which cases are on stage, and how. */
	shown_t cases[2];
	int n = 0;
	float sway = UIMotion_Amplitude(1.0f, motion);
	for(int i = 0; i < count && n < 2; i++) {
		const uiJewelCaseShot_t *shot = &shots[i];
		if(!(fabsf(shot->slot) < 1.0f) || shot->alpha <= 0.0f) continue;
		shown_t *c = &cases[n++];
		c->shot = shot;
		c->pose = slotPose(shot->slot, direction, jc.t, 1.7f * (float)(shot->index % 997u) + 0.4f, sway);
		c->pose.fade *= sat(shot->alpha);
		placeCase(c);
	}
	if(n == 0) return;

	/* The caller holds the publication lock: every supplied print is
	 * complete before this frame, and the studio/sheen/glow never change.
	 * Invalidate once before sampling, retaining all of them through the
	 * case's passes. There are no EFB texture copies inside this renderer. */
	GX_InvalidateTexAll();
	loadProjection();
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);

	/* The discs, then the plastic from the back forwards. */
	for(int i = 0; i < n; i++) drawDisc(&cases[i]);
	part_t parts[4];
	int np = 0;
	for(int i = 0; i < n; i++) {
		parts[np++] = (part_t) {i, false, cases[i].depthTray};
		parts[np++] = (part_t) {i, true, cases[i].depthLid};
	}
	for(int i = 1; i < np; i++) {
		for(int j = i; j > 0 && parts[j].depth < parts[j - 1].depth; j--) {
			part_t tmp = parts[j]; parts[j] = parts[j - 1]; parts[j - 1] = tmp;
		}
	}
	for(int i = 0; i < np; i++) {
		shown_t *c = &cases[parts[i].caseIdx];
		if(parts[i].lid) {
			drawRods(&lidRods, c->mv, c->scale, c->pose.fade);
			drawSheets(&lidSheet, 1, c->mv, c->scale, c->pose.fade);
			drawSheen(c->mv, 5.0f, c->pose.fade);
		}
		else {
			drawRods(&trayRods, c->mv, c->scale, c->pose.fade);
			drawSheets(traySheets, traySheetCount, c->mv, c->scale, c->pose.fade);
		}
	}
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	star_t stars[MAX_STARS];
	int nstars = 0;
	for(int i = 0; i < n; i++) {
		findGlints(&trayRods, cases[i].mv, cases[i].scale, cases[i].pose.fade, stars, &nstars);
		findGlints(&lidRods, cases[i].mv, cases[i].scale, cases[i].pose.fade, stars, &nstars);
	}
	drawStars(stars, nstars);

	/* Back to what the 2D drawing expects of what drawInit leaves alone. */
	GX_SetNumTevStages(1);
	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
	GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_1);
	GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_1);
	GX_SetNumChans(1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}
