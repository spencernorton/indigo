/*
 * test_ui_assets.c -- host test harness for the Phase 4A poster cache.
 *
 * Builds ui_assets.c with -DUI_ASSETS_HOST_BUILD (GX stubbed, allocation
 * counted, clock injected) and drives it against in-memory packs built by
 * the same layout rules as buildtools/ui/poster_pack.py. Poster payloads
 * are deterministic per-ID noise: the runtime never decodes CMPR, so tests
 * only need byte-exact transport, not real textures. One optional argv[1]
 * points at a real generator-produced .pak for an end-to-end pass.
 *
 * Usage: ./test_ui_assets [real-pack.pak [real-stills.pak]]
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ui_assets.h"

/* ---- tiny assert harness ---- */

static int failures;
static int checks;
static const char *currentTest;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, \
		        currentTest, #cond); \
	} \
} while (0)

#define RUN(fn) do { \
	currentTest = #fn; \
	resetWorld(); \
	fn(); \
	printf("  %-44s %s\n", #fn, failures == failuresBefore ? "ok" : "FAILED"); \
	failuresBefore = failures; \
} while (0)

/* ---- fake external critical section with invariant checking ----
 *
 * Non-blocking (single-threaded harness), but it enforces the contract
 * mechanically: the lock is never taken recursively, never released
 * unheld, source reads and heap traffic never happen inside it, and every
 * test ends balanced. Tests additionally count acquisitions to prove the
 * Poll capture/publish split. */

static int lockDepth;
static int lockCalls;

/* Boundary probe: the observable module state (a probe ID's classification
 * plus its Peek pointer) is sampled at every unlock and compared at the
 * NEXT lock. Any menu-side mutation of video-visible state that happens
 * OUTSIDE a critical section changes the probe between those two points
 * and fails here -- this falsifies "mutation outside the lock", not just
 * section counts. Disabled around test seams that mutate state directly. */
static int probeEnabled = 1;
static int probeArmed;
static char probeId[8] = "GALE01";

typedef struct {
	int result;
	const GXTexObj *tex;
	int ready;
	int stillResult;
	const GXTexObj *stillTex;
	int stillReady;
} probeSnap_t;

static probeSnap_t probeLastUnlock;

static probeSnap_t probeSample(void) {
	probeSnap_t snap;
	uiPosterHandle_t h;
	snap.result = (int)UIAssets_Query(probeId, 6, true, &h);
	snap.tex = UIAssets_Peek(h);
	snap.ready = UIAssets_Ready() ? 1 : 0;
	snap.stillResult = (int)UIStills_Query(probeId, 6, true, &h);
	snap.stillTex = UIStills_Peek(h);
	snap.stillReady = UIStills_Ready() ? 1 : 0;
	return snap;
}

static void fakeLock(void *ctx) {
	(void)ctx;
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] recursive lock (depth %d)\n",
		        currentTest, lockDepth);
		failures++;
	}
	if (probeEnabled && probeArmed) {
		probeSnap_t now = probeSample();
		if (now.result != probeLastUnlock.result ||
		    now.tex != probeLastUnlock.tex ||
		    now.ready != probeLastUnlock.ready ||
		    now.stillResult != probeLastUnlock.stillResult ||
		    now.stillTex != probeLastUnlock.stillTex ||
		    now.stillReady != probeLastUnlock.stillReady) {
			fprintf(stderr,
			        "FAIL [%s] observable state mutated outside the "
			        "critical section (probe %s)\n", currentTest, probeId);
			failures++;
		}
	}
	probeArmed = 0;
	lockDepth++;
	lockCalls++;
}

static void fakeUnlock(void *ctx) {
	(void)ctx;
	if (lockDepth != 1) {
		fprintf(stderr, "FAIL [%s] unlock at depth %d\n",
		        currentTest, lockDepth);
		failures++;
	}
	lockDepth--;
	if (probeEnabled) {
		probeLastUnlock = probeSample();
		probeArmed = 1;
	}
}

static const uiAssetsSync_t testSync = { fakeLock, fakeUnlock, NULL };

typedef struct {
	int destroyed;
	int lockCalls;
	int unlockCalls;
} trackedMutex_t;

static void trackedLock(void *ctx) {
	trackedMutex_t *mutex = (trackedMutex_t *)ctx;
	if (!mutex || mutex->destroyed) {
		fprintf(stderr, "FAIL [%s] lock callback used after destruction\n",
		        currentTest);
		failures++;
	}
	if (mutex)
		mutex->lockCalls++;
	fakeLock(NULL);
}

static void trackedUnlock(void *ctx) {
	trackedMutex_t *mutex = (trackedMutex_t *)ctx;
	if (!mutex || mutex->destroyed) {
		fprintf(stderr, "FAIL [%s] unlock callback used after destruction\n",
		        currentTest);
		failures++;
	}
	if (mutex)
		mutex->unlockCalls++;
	fakeUnlock(NULL);
}

/* A second complete pair used only to prove callback identity is part of
 * the lifetime binding. These delegate to the same fake mutex mechanics. */
static void alternateLock(void *ctx) {
	fakeLock(ctx);
}

static void alternateUnlock(void *ctx) {
	fakeUnlock(ctx);
}

/* ---- allocation hooks required by the host build ---- */

static int liveAllocs;
static long liveBytes;
static int allocCalls;
static int allocsFrozen; /* set outside Init to prove nothing allocates */

typedef struct {
	size_t size;
} allocHeader_t;

static void assertUnlockedHeap(const char *what) {
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] %s inside the critical section\n",
		        currentTest, what);
		failures++;
	}
}

void *uiAssetsHostAlloc(u32 size) {
	allocHeader_t *h;
	assertUnlockedHeap("allocation");
	if (allocsFrozen) {
		fprintf(stderr, "FAIL [%s] allocation outside Init (%u bytes)\n",
		        currentTest, (unsigned)size);
		failures++;
	}
	h = malloc(sizeof(*h) + size);
	if (!h)
		return NULL;
	h->size = size;
	liveAllocs++;
	allocCalls++;
	liveBytes += (long)size;
	return h + 1;
}

static void *alignedBlocks[16];
static long alignedSizes[16];
static int alignedCount;

void *uiAssetsHostAllocAligned32(u32 size) {
	void *p = NULL;
	assertUnlockedHeap("aligned allocation");
	if (allocsFrozen) {
		fprintf(stderr, "FAIL [%s] aligned allocation outside Init\n",
		        currentTest);
		failures++;
	}
	/* Track aligned blocks in a side table: posix_memalign leaves no room
	 * for a size header without breaking the 32-byte alignment. */
	if (alignedCount >= 16 || posix_memalign(&p, 32, size) != 0)
		return NULL;
	alignedBlocks[alignedCount] = p;
	alignedSizes[alignedCount] = (long)size;
	alignedCount++;
	liveAllocs++;
	allocCalls++;
	liveBytes += (long)size;
	return p;
}

static int flushCalls;

void uiAssetsHostFlush(void *ptr, u32 len) {
	(void)ptr;
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] DCFlushRange inside the critical "
		        "section\n", currentTest);
		failures++;
	}
	if (len != UI_ASSETS_POSTER_BYTES && len != UI_STILLS_BYTES &&
	    len != UI_DISCS_BYTES) {
		fprintf(stderr, "FAIL [%s] flush of %u bytes\n", currentTest,
		        (unsigned)len);
		failures++;
	}
	flushCalls++;
}

void uiAssetsHostFree(void *ptr) {
	int i;
	assertUnlockedHeap("free");
	if (!ptr)
		return;
	for (i = 0; i < alignedCount; i++) {
		if (alignedBlocks[i] == ptr) {
			liveAllocs--;
			liveBytes -= alignedSizes[i];
			alignedBlocks[i] = NULL;
			free(ptr);
			return;
		}
	}
	{
		allocHeader_t *h = (allocHeader_t *)ptr - 1;
		liveAllocs--;
		liveBytes -= (long)h->size;
		free(h);
	}
}

/* ---- fake clock ---- */

static u32 fakeNowValue = 1000;
static int nowLockDepth; /* lockDepth at the clock's last reading */

static u32 fakeNow(void) {
	nowLockDepth = lockDepth;
	return fakeNowValue;
}

/* ---- pack builder (mirrors poster_pack.py layout) ---- */

#define HDR 64
#define REC 32

typedef struct {
	const char *id;
	int universal;
	u8 dom[3];
} testRec_t;

static u32 wbe32(u8 *p, u32 v) {
	p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
	return v;
}

static void wbe16(u8 *p, u16 v) {
	p[0] = (u8)(v >> 8); p[1] = (u8)v;
}

static u32 rbe32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* Deterministic per-ID payload so slot contents are verifiable. */
static void fillPayloadLen(u8 *dst, const char *id, u32 len) {
	u32 x = 2166136261u;
	u32 i;
	for (i = 0; i < UI_ASSETS_ID_LEN; i++)
		x = (x ^ (u8)id[i]) * 16777619u;
	for (i = 0; i < len; i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		dst[i] = (u8)x;
	}
}

static void fillPayload(u8 *dst, const char *id) {
	fillPayloadLen(dst, id, UI_ASSETS_POSTER_BYTES);
}

/* count is passed explicitly: corruption tests mutate the stored count and
 * the fixer must still hash the real header+index extent. */
static void packFixCrc(u8 *pack, u32 count) {
	u32 crc;
	wbe32(pack + 0x08, 0);
	crc = crc32(0, pack, HDR + count * REC);
	wbe32(pack + 0x08, crc);
}

#define BUILD_NOSORT 1

/* The texture shape a pack declares: posters.pak's, stills.pak's or
 * discs.pak's. */
typedef struct {
	u32 bytes;
	u16 canvasW, canvasH, contentW, contentH;
	u8 mips;
} packShape_t;

static const packShape_t POSTER_SHAPE = {
	UI_ASSETS_POSTER_BYTES, UI_ASSETS_CANVAS_W, UI_ASSETS_CANVAS_H,
	UI_ASSETS_CONTENT_W, UI_ASSETS_CONTENT_H, UI_ASSETS_MIP_LEVELS
};
static const packShape_t STILL_SHAPE = {
	UI_STILLS_BYTES, UI_STILLS_W, UI_STILLS_H, UI_STILLS_W, UI_STILLS_H, 1
};
static const packShape_t DISC_SHAPE = {
	UI_DISCS_BYTES, UI_DISCS_SIZE, UI_DISCS_SIZE, UI_DISCS_SIZE, UI_DISCS_SIZE, 1
};

static u8 *buildShapedPack(const testRec_t *recs, int count, size_t *outLen,
                           int flags, const packShape_t *shape) {
	u32 dataOffset = HDR + (u32)count * REC;
	size_t len = dataOffset + (size_t)count * shape->bytes;
	u8 *pack = calloc(1, len);
	int order[UI_ASSETS_MAX_RECORDS + 1];
	int i, j;

	CHECK(count <= UI_ASSETS_MAX_RECORDS + 1);
	for (i = 0; i < count; i++)
		order[i] = i;
	if (!(flags & BUILD_NOSORT)) {
		for (i = 0; i < count; i++) {
			for (j = i + 1; j < count; j++) {
				if (strcmp(recs[order[j]].id, recs[order[i]].id) < 0) {
					int t = order[i]; order[i] = order[j]; order[j] = t;
				}
			}
		}
	}

	wbe32(pack + 0x00, 0x5357504Bu);
	wbe32(pack + 0x04, 1);
	wbe32(pack + 0x0C, (u32)count);
	wbe32(pack + 0x10, HDR);
	wbe32(pack + 0x14, (u32)count * REC);
	wbe32(pack + 0x18, dataOffset);
	wbe32(pack + 0x1C, (u32)len);
	wbe32(pack + 0x20, shape->bytes);
	wbe16(pack + 0x24, shape->canvasW);
	wbe16(pack + 0x26, shape->canvasH);
	wbe16(pack + 0x28, shape->contentW);
	wbe16(pack + 0x2A, shape->contentH);
	pack[0x2C] = shape->mips;
	pack[0x2D] = 14; /* GX_TF_CMPR */

	for (i = 0; i < count; i++) {
		const testRec_t *r = &recs[order[i]];
		u8 *rec = pack + HDR + (size_t)i * REC;
		u8 *payload = pack + dataOffset + (size_t)i * shape->bytes;
		memcpy(rec, r->id, UI_ASSETS_ID_LEN);
		rec[6] = r->universal ? 0x01 : 0x00;
		wbe32(rec + 0x08, dataOffset + (u32)i * shape->bytes);
		wbe32(rec + 0x0C, shape->bytes);
		fillPayloadLen(payload, r->id, shape->bytes);
		wbe32(rec + 0x10, crc32(0, payload, shape->bytes));
		rec[0x14] = r->dom[0]; rec[0x15] = r->dom[1]; rec[0x16] = r->dom[2];
		rec[0x17] = 0xFF;
		wbe16(rec + 0x18, 0x8000);
		wbe16(rec + 0x1A, 0x8000);
	}
	packFixCrc(pack, (u32)count);
	*outLen = len;
	return pack;
}

static u8 *buildPack(const testRec_t *recs, int count, size_t *outLen, int flags) {
	return buildShapedPack(recs, count, outLen, flags, &POSTER_SHAPE);
}

static u8 *buildStillPack(const testRec_t *recs, int count, size_t *outLen) {
	return buildShapedPack(recs, count, outLen, 0, &STILL_SHAPE);
}

static u8 *buildDiscPack(const testRec_t *recs, int count, size_t *outLen) {
	return buildShapedPack(recs, count, outLen, 0, &DISC_SHAPE);
}

/* ---- in-memory source with fault injection ---- */

static struct {
	const u8 *data;
	size_t len;
	int readCalls;
	int failAfter;   /* -1: never fail; else fail on the Nth call (0-based) */
	u32 shortReadAt; /* offset whose read returns len-1; 0xFFFFFFFF off */
	u32 lastOffset, lastLength, maxLength;
} mem;

static void (*memReadHook)(void); /* optional reentrant chaos, runs pre-read */

static s32 memRead(void *ctx, u32 offset, void *dst, u32 len) {
	int call = mem.readCalls++;
	mem.lastOffset = offset;
	mem.lastLength = len;
	if (len > mem.maxLength) mem.maxLength = len;
	(void)ctx;
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] source read under the lock (depth %d)\n",
		        currentTest, lockDepth);
		failures++;
	}
	if (memReadHook)
		memReadHook();
	if (mem.failAfter >= 0 && call >= mem.failAfter)
		return UI_ASSETS_ERR_IO;
	if ((size_t)offset + len > mem.len)
		return UI_ASSETS_ERR_IO;
	memcpy(dst, mem.data + offset, len);
	if (offset == mem.shortReadAt)
		return (s32)len - 1;
	return (s32)len;
}

static uiAssetsSource_t memSource(const u8 *data, size_t len) {
	uiAssetsSource_t src;
	mem.data = data;
	mem.len = len;
	mem.readCalls = 0;
	mem.failAfter = -1;
	mem.shortReadAt = 0xFFFFFFFFu;
	mem.lastOffset = mem.lastLength = mem.maxLength = 0u;
	src.read = memRead;
	src.size = (u32)len;
	src.ctx = NULL;
	src.nowMs = fakeNow;
	return src;
}

static void resetWorld(void) {
	allocsFrozen = 0;
	memReadHook = NULL;
	UIAssets_CancelForDeviceChange();
	if (UIAssets_DisposeAfterVideoStop() != UI_ASSETS_OK) {
		fprintf(stderr, "FAIL [%s] reset could not dispose poster cache\n",
		        currentTest ? currentTest : "startup");
		failures++;
	}
	UIStills_CancelForDeviceChange();
	if (UIStills_DisposeAfterVideoStop() != UI_ASSETS_OK) {
		fprintf(stderr, "FAIL [%s] reset could not dispose stills cache\n",
		        currentTest ? currentTest : "startup");
		failures++;
	}
	UIDiscs_CancelForDeviceChange();
	if (UIDiscs_DisposeAfterVideoStop() != UI_ASSETS_OK) {
		fprintf(stderr, "FAIL [%s] reset could not dispose discs cache\n",
		        currentTest ? currentTest : "startup");
		failures++;
	}
	fakeNowValue = 1000;
	memset(&mem, 0, sizeof(mem));
	alignedCount = 0;
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] lock left held across a test\n",
		        currentTest);
		failures++;
		lockDepth = 0;
	}
	lockCalls = 0;
	flushCalls = 0;
	probeEnabled = 1;
	probeArmed = 0;
	memcpy(probeId, "GALE01\0", 8);
}

/* ---- helpers ---- */

static void requestOne(const char *id) {
	char ids[1][8];
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], id, UI_ASSETS_ID_LEN);
	UIAssets_RequestWindow(ids, 1, 0);
}

static void pollAll(void) {
	int guard = 64;
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	while (UIAssets_Poll() && guard-- > 0)
		fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
}

static const testRec_t BASIC[] = {
	{ "GALE01", 0, { 10, 20, 30 } },
	{ "GC6E01", 0, { 1, 2, 3 } },
	{ "GZLE01", 1, { 40, 50, 60 } },
};

/* ---- tests ---- */

static void test_init_valid(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UIAssets_Ready());
	CHECK(UIAssets_MemoryFootprint() ==
	      UI_ASSETS_SLOTS * UI_ASSETS_POSTER_BYTES + 3 * REC);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_STATE); /* double init */
	free(pack);
}

static void test_exact_hit(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	GXTexObj *tex;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) == NULL); /* not polled yet */
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	allocsFrozen = 1; /* per-frame path must not allocate */
	tex = UIAssets_Peek(h);
	allocsFrozen = 0;
	CHECK(tex != NULL);
	if (tex) {
		u8 expect[64];
		u8 full[UI_ASSETS_POSTER_BYTES];
		fillPayload(full, "GALE01");
		memcpy(expect, full, 64);
		CHECK(tex->width == UI_ASSETS_CANVAS_W);
		CHECK(tex->height == UI_ASSETS_CANVAS_H);
		CHECK(tex->format == 14);
		CHECK(tex->mipmap == 1);
		CHECK(tex->lodConfigured);
		CHECK(tex->minlod == 0.0f);
		CHECK(tex->maxlod == (float)(UI_ASSETS_MIP_LEVELS - 1));
		CHECK(memcmp(tex->data, expect, 64) == 0);
	}
	free(pack);
}

static void test_universal_fallback(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* GZLE01 is universal: GZLE69 (different publisher) may use it. */
	requestOne("GZLE69");
	CHECK(UIAssets_Query("GZLE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	pollAll();
	CHECK(UIAssets_Query("GZLE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	CHECK(UIAssets_Peek(h) != NULL);
	/* The exact ID still classifies as EXACT and shares the slot. */
	CHECK(UIAssets_Query("GZLE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	free(pack);
}

static void test_no_implicit_fallback(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* GALE01 is NOT universal: GALE69 must never borrow it. */
	CHECK(UIAssets_Query("GALE69", 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_Query("GALE69", 6, false, &h) == UI_POSTER_PROCEDURAL_CARD);
	requestOne("GALE69");
	pollAll();
	CHECK(UIAssets_Query("GALE69", 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(mem.readCalls == 2); /* header + index only, no poster read */
	free(pack);
}

static void test_duplicate_and_unsorted_rejected(void) {
	static const testRec_t DUP[] = {
		{ "GALE01", 0, { 0, 0, 0 } },
		{ "GALE01", 0, { 0, 0, 0 } },
	};
	static const testRec_t UNSORTED[] = {
		{ "GZLE01", 0, { 0, 0, 0 } },
		{ "GALE01", 0, { 0, 0, 0 } },
	};
	size_t len;
	u8 *pack = buildPack(DUP, 2, &len, BUILD_NOSORT);
	uiAssetsSource_t src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(!UIAssets_Ready());
	free(pack);
	pack = buildPack(UNSORTED, 2, &len, BUILD_NOSORT);
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(pack);
}

static void test_ambiguous_universal_rejected(void) {
	/* Non-adjacent ambiguity: a non-universal record sits between two
	 * universal records sharing the GALE prefix. */
	static const testRec_t AMB[] = {
		{ "GALE01", 1, { 0, 0, 0 } },
		{ "GALE02", 0, { 0, 0, 0 } },
		{ "GALE03", 1, { 0, 0, 0 } },
	};
	size_t len;
	u8 *pack = buildPack(AMB, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(pack);
}

static void test_missing_or_truncated_pack(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src;

	src = memSource(pack, len);
	mem.failAfter = 0; /* header read fails: pack unreadable/missing */
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_IO);
	{
		uiPosterHandle_t h;
		CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);
		CHECK(UIAssets_Query("GALE01", 6, false, &h) == UI_POSTER_PROCEDURAL_CARD);
	}

	src = memSource(pack, len - 1); /* truncated: fileLength mismatch */
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);

	src = memSource(pack, 32); /* shorter than a header */
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(pack);
}

static void test_header_field_corruption(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src;
	static const struct { u32 off; u32 val; } cases[] = {
		{ 0x00, 0x4B505753 }, /* wrong magic */
		{ 0x04, 2 },          /* wrong version */
		{ 0x0C, 0 },          /* zero records */
		{ 0x0C, 4096 },       /* over MAX_RECORDS */
		{ 0x0C, 0xFFFFFFFF }, /* count overflow attempt */
		{ 0x10, 128 },        /* bad index offset */
		{ 0x14, 1 },          /* bad index length */
		{ 0x18, 0xFFFFFFE0 }, /* data offset overflow attempt */
		{ 0x1C, 0xFFFFFFFF }, /* absurd file length */
		{ 0x20, 32768 },      /* wrong poster size */
		{ 0x24, 0x00C0 },     /* non-pow2 canvas smuggled in */
	};
	size_t i;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		u8 *evil = malloc(len);
		memcpy(evil, pack, len);
		wbe32(evil + cases[i].off, cases[i].val);
		packFixCrc(evil, 3); /* re-hash the REAL extent so field checks run */
		src = memSource(evil, len);
		CHECK(UIAssets_Init(&src, &testSync) != UI_ASSETS_OK);
		CHECK(!UIAssets_Ready());
		free(evil);
	}
	free(pack);
}

static void test_malformed_record_offsets(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	u8 *evil;
	uiAssetsSource_t src;

	/* Record 1 overlaps record 0's payload. */
	evil = malloc(len);
	memcpy(evil, pack, len);
	wbe32(evil + HDR + REC + 0x08, rbe32(evil + HDR + 0x08) + 16);
	packFixCrc(evil, 3);
	src = memSource(evil, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(evil);

	/* Record points past EOF. */
	evil = malloc(len);
	memcpy(evil, pack, len);
	wbe32(evil + HDR + 0x08, (u32)len - 100);
	packFixCrc(evil, 3);
	src = memSource(evil, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(evil);

	/* Poster length overflow attempt. */
	evil = malloc(len);
	memcpy(evil, pack, len);
	wbe32(evil + HDR + 0x0C, 0xFFFFFF00);
	packFixCrc(evil, 3);
	src = memSource(evil, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(evil);
	free(pack);
}

static void test_field_validation_behind_valid_crc(void) {
	/* Every one of these mutations is re-CRC'd, so ONLY the field
	 * validators can reject it -- proving they aren't shadowed by the
	 * checksum (a fuzz flip always trips the CRC first). */
	static const struct { u32 off; u8 val; } cases[] = {
		{ 0x2C, 4 },            /* wrong mip count */
		{ 0x2D, 6 },            /* wrong texture format */
		{ 0x2E, 1 },            /* header reserved u16 */
		{ 0x30, 1 },            /* header reserved tail (first byte) */
		{ 0x3F, 1 },            /* header reserved tail (last byte) */
		{ HDR + 0, 'g' },       /* lowercase in record ID */
		{ HDR + 5, '!' },       /* punctuation in record ID */
		{ HDR + 6, 0x80 },      /* unknown record flag bit */
		{ HDR + 7, 1 },         /* record reserved byte */
		{ HDR + 0x1C, 1 },      /* record reserved u32 */
	};
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	size_t i;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		u8 *evil = malloc(len);
		uiAssetsSource_t src;
		memcpy(evil, pack, len);
		evil[cases[i].off] = cases[i].val;
		packFixCrc(evil, 3);
		src = memSource(evil, len);
		CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
		free(evil);
	}
	/* Content-geometry words, also behind a valid CRC. */
	{
		u8 *evil = malloc(len);
		uiAssetsSource_t src;
		memcpy(evil, pack, len);
		wbe16(evil + 0x28, 128); /* wrong content width */
		packFixCrc(evil, 3);
		src = memSource(evil, len);
		CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
		free(evil);
	}
	free(pack);
}

static void test_index_read_failure(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	mem.failAfter = 1; /* header succeeds, index read fails */
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_IO);
	CHECK(!UIAssets_Ready());
	free(pack);
}

static void test_index_crc_mismatch(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src;
	pack[HDR + 0x14] ^= 0xFF; /* flip a dominant-color byte: CRC must catch */
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_CRC);
	free(pack);
}

static void test_byte_flip_fuzz(void) {
	/* Every single-byte corruption of header+index must be rejected
	 * without a crash. Payload corruption is caught later per-poster. */
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	u32 metaLen = HDR + 3 * REC;
	u32 off;
	uiAssetsSource_t src;
	for (off = 0; off < metaLen; off++) {
		pack[off] ^= 0xA5;
		src = memSource(pack, len);
		CHECK(UIAssets_Init(&src, &testSync) != UI_ASSETS_OK);
		pack[off] ^= 0xA5;
	}
	src = memSource(pack, len); /* pristine pack still loads */
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	free(pack);
}

static void test_poster_payload_corrupt(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src;
	uiPosterHandle_t h;
	u32 dataOffset = rbe32(pack + 0x18);
	int readsAfter;
	pack[dataOffset + 5] ^= 0xFF; /* corrupt GALE01's texels */
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	CHECK(UIAssets_Peek(h) == NULL);
	/* FAILED is a negative cache: re-request must not retry-loop. */
	readsAfter = mem.readCalls;
	requestOne("GALE01");
	pollAll();
	CHECK(mem.readCalls == readsAfter);
	free(pack);
}

static void test_short_read_fails_poster(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	mem.shortReadAt = rbe32(pack + 0x18);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	free(pack);
}

/* Each thousand takes the next letter from G: G###E0, H###E0, I###E0...
 * Unique, valid charset, and lexicographic order matches numeric order up
 * to 19,999 titles (G to Z), past UI_ASSETS_MAX_RECORDS. */
static void nthId(char *dst, int n) {
	unsigned int value;
	memset(dst, 0, 8);
	CHECK(n >= 0 && n < 20000);
	if (n < 0 || n >= 20000)
		return;
	value = (unsigned int)n % 1000u;
	dst[0] = (char)('G' + n / 1000);
	dst[1] = (char)('0' + value / 100u);
	dst[2] = (char)('0' + value / 10u % 10u);
	dst[3] = (char)('0' + value % 10u);
	dst[4] = 'E';
	dst[5] = '0';
}

static void test_nth_id_boundaries(void) {
	char id[8];

	nthId(id, 0);
	CHECK(strcmp(id, "G000E0") == 0);
	nthId(id, 999);
	CHECK(strcmp(id, "G999E0") == 0);
	nthId(id, 1000);
	CHECK(strcmp(id, "H000E0") == 0);
	nthId(id, 1999);
	CHECK(strcmp(id, "H999E0") == 0);
	nthId(id, 2047);
	CHECK(strcmp(id, "I047E0") == 0);
	nthId(id, 19999);
	CHECK(strcmp(id, "Z999E0") == 0);
}

static void makeIds(char ids[][8], int first, int count) {
	int i;
	for (i = 0; i < count; i++)
		nthId(ids[i], first + i);
}

static testRec_t *makeRecs(int count) {
	testRec_t *recs = calloc((size_t)count, sizeof(*recs) + 8u);
	char *names;
	int i;
	if (!recs) {
		perror("makeRecs");
		exit(2);
	}
	names = (char *)(recs + count);
	for (i = 0; i < count; i++) {
		nthId(names + (size_t)i * 8, i);
		recs[i].id = names + (size_t)i * 8;
		recs[i].dom[0] = (u8)i;
	}
	return recs; /* IDs share this allocation and are released with recs. */
}

/* Assert a poster is fully loaded and holds its record's exact payload. */
static void checkLoaded(const char *id) {
	uiPosterHandle_t h;
	GXTexObj *tex;
	u8 full[UI_ASSETS_POSTER_BYTES];
	CHECK(UIAssets_Query(id, 6, true, &h) == UI_POSTER_EXACT);
	tex = UIAssets_Peek(h);
	CHECK(tex != NULL);
	if (tex) {
		fillPayload(full, id);
		CHECK(memcmp(tex->data, full, 64) == 0);
	}
}

static void test_window_prefetch_loads_whole_window(void) {
	int count = 20;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_WINDOW][8];
	int i;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	makeIds(ids, 5, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	/* Every window entry -- not just the selected one -- must be resident
	 * with its own record's exact payload. */
	for (i = 0; i < UI_ASSETS_WINDOW; i++)
		checkLoaded(ids[i]);
	CHECK(mem.readCalls == 2 + UI_ASSETS_WINDOW); /* header + index + 9 */
	CHECK(flushCalls == UI_ASSETS_WINDOW); /* one unlocked flush per load */
	free(pack);
	free(recs);
}

static void test_window_eviction_and_reversal(void) {
	int count = 20;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_WINDOW][8];
	uiPosterHandle_t h;
	int reads;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	makeIds(ids, 3, UI_ASSETS_WINDOW); /* window 3..11, selected 7 */
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	CHECK(UIAssets_Query("G006E0", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	reads = mem.readCalls;

	/* Shift by one: exactly one new poster read, eight kept. */
	makeIds(ids, 4, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	CHECK(mem.readCalls == reads + 1);

	/* The edge that left (G003E0) keeps its poster, so going back reads
	 * nothing. */
	CHECK(UIAssets_Query("G003E0", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	makeIds(ids, 3, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	CHECK(mem.readCalls == reads + 1);

	/* Rapid reversal WITH loads interleaved: the clock advances past the
	 * quarantine between flips, so posters land mid-storm and the ones
	 * still waiting are dropped -- the harsher path than pure request
	 * churn. */
	{
		int flip;
		int readsBefore = mem.readCalls;
		for (flip = 0; flip < 25; flip++) {
			makeIds(ids, (flip & 1) ? 3 : 10, UI_ASSETS_WINDOW);
			UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
			fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
			UIAssets_Poll();
			UIAssets_Poll();
		}
		CHECK(mem.readCalls > readsBefore); /* the storm really loaded */
		pollAll();
		makeIds(ids, 10, UI_ASSETS_WINDOW);
		UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
		pollAll();
		/* Full settled window is correct after the storm. */
		{
			int i;
			for (i = 0; i < UI_ASSETS_WINDOW; i++)
				checkLoaded(ids[i]);
		}
	}
	CHECK(UIAssets_MemoryFootprint() ==
	      UI_ASSETS_SLOTS * UI_ASSETS_POSTER_BYTES + (u32)count * REC);
	free(pack);
	free(recs);
}

/* Two carousel windows fill 18 of the 25 slots; a third needs nine, so
 * after the seven empty ones it reuses the slots of the window left longest
 * ago, the posters farthest from its selection first, and a fourth takes
 * the rest of that window and the two ends of the next. */
static void test_window_retention_oldest_first(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_WINDOW][8];
	char id[8];
	uiPosterHandle_t h;
	int reads;
	int i;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	for (i = 0; i < 4; i++) {
		makeIds(ids, i * UI_ASSETS_WINDOW, UI_ASSETS_WINDOW);
		UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
		pollAll();
	}
	CHECK(mem.readCalls == 2 + 4 * UI_ASSETS_WINDOW);
	for (i = 0; i < 4 * UI_ASSETS_WINDOW; i++) {
		nthId(id, i);
		CHECK(UIAssets_Query(id, 6, true, &h) == UI_POSTER_EXACT);
		/* Gone: the whole first window and the second window's two
		 * ends. */
		CHECK((UIAssets_Peek(h) == NULL) == (i < UI_ASSETS_WINDOW ||
		      i == UI_ASSETS_WINDOW || i == 2 * UI_ASSETS_WINDOW - 1));
	}
	/* The third window is still whole: going back reads nothing. */
	reads = mem.readCalls;
	makeIds(ids, 2 * UI_ASSETS_WINDOW, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	CHECK(mem.readCalls == reads);
	free(pack);
	free(recs);
}

static void test_stale_handle_after_eviction(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_SLOTS][8];
	uiPosterHandle_t h;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	makeIds(ids, 0, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, 0);
	pollAll();
	CHECK(UIAssets_Query("G000E0", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	/* A full window of other posters takes every slot, G000E0's too. */
	makeIds(ids, 10, UI_ASSETS_SLOTS);
	UIAssets_RequestWindow(ids, UI_ASSETS_SLOTS, 0);
	CHECK(UIAssets_Peek(h) == NULL);    /* stale before any reload */
	pollAll();
	CHECK(UIAssets_Peek(h) == NULL);    /* and after the slot is reused */
	free(pack);
	free(recs);
}

/* The Grid layout asks for its whole window nearest first (selected 0), and
 * a carousel afterwards finds its nine resident: none is read again, and
 * the grid's other posters stay for going back. */
static void test_grid_window_then_carousel(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_SLOTS][8];
	char carousel[UI_ASSETS_WINDOW][8];
	uiPosterHandle_t h;
	int reads;
	int i;
	int j;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* The window in the grid's own order, not the pack's. */
	for (i = 0; i < UI_ASSETS_SLOTS; i++)
		nthId(ids[i], (i * 7) % UI_ASSETS_SLOTS);
	UIAssets_RequestWindow(ids, UI_ASSETS_SLOTS, 0);
	for (i = 0; i < UI_ASSETS_SLOTS; i++) {
		fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
		UIAssets_Poll();
		/* After i + 1 polls the first i + 1 of the window are in. */
		for (j = 0; j < UI_ASSETS_SLOTS; j++) {
			CHECK(UIAssets_Query(ids[j], 6, true, &h) == UI_POSTER_EXACT);
			CHECK((UIAssets_Peek(h) != NULL) == (j <= i));
		}
	}
	CHECK(mem.readCalls == 2 + UI_ASSETS_SLOTS);
	reads = mem.readCalls;

	makeIds(carousel, 9, UI_ASSETS_WINDOW); /* all nine already resident */
	UIAssets_RequestWindow(carousel, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
	pollAll();
	CHECK(mem.readCalls == reads);
	for (i = 0; i < count; i++) {
		char id[8];
		nthId(id, i);
		CHECK(UIAssets_Query(id, 6, true, &h) == UI_POSTER_EXACT);
		CHECK((UIAssets_Peek(h) != NULL) == (i < UI_ASSETS_SLOTS));
	}
	free(pack);
	free(recs);
}

static void test_cancel_for_device_change(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[3][8];
	uiPosterHandle_t h;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], "GALE01", 6);
	memcpy(ids[1], "GC6E01", 6);
	memcpy(ids[2], "GZLE01", 6);
	UIAssets_RequestWindow(ids, 3, 0);
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	UIAssets_Poll(); /* only the selected poster lands; two still pending */
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);

	UIAssets_CancelForDeviceChange(); /* mid-load */
	CHECK(!UIAssets_Ready());
	CHECK(UIAssets_Peek(h) == NULL);      /* every handle dies */
	CHECK(!UIAssets_Poll());              /* loading fully stopped */
	{
		int reads = mem.readCalls;
		UIAssets_Poll();
		CHECK(mem.readCalls == reads);    /* and never touches the device */
	}
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);

	/* Device comes back: re-Init reuses the arena and works. */
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	free(pack);
}

static void test_sync_validation_policy(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiAssetsSync_t lockOnly = { fakeLock, NULL, NULL };
	uiAssetsSync_t unlockOnly = { NULL, fakeUnlock, NULL };
	uiAssetsSync_t noCallbacks = { NULL, NULL, NULL };

	/* A half-pair is rejected before any pack I/O or allocation in every
	 * build, including the deliberately synchronization-free host mode. */
	CHECK(UIAssets_Init(&src, &lockOnly) == UI_ASSETS_ERR_STATE);
	CHECK(UIAssets_Init(&src, &unlockOnly) == UI_ASSETS_ERR_STATE);
	CHECK(mem.readCalls == 0);
	CHECK(UIAssets_MemoryFootprint() == 0);

#ifdef UI_ASSETS_HOST_REQUIRE_SYNC
	/* This host build exercises the target policy without requiring libogc:
	 * NULL and an all-zero pair are both illegal on-console. */
	CHECK(UIAssets_Init(&src, NULL) == UI_ASSETS_ERR_STATE);
	CHECK(UIAssets_Init(&src, &noCallbacks) == UI_ASSETS_ERR_STATE);
	CHECK(mem.readCalls == 0);
#else
	/* Normal host tests may deliberately run single-threaded with no lock.
	 * NULL and the identical all-zero triple are the same bound identity. */
	CHECK(UIAssets_Init(&src, NULL) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_Init(&src, &noCallbacks) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
#endif
	free(pack);
}

static void test_sync_binding_lifecycle(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	trackedMutex_t first = { 0, 0, 0 };
	trackedMutex_t second = { 0, 0, 0 };
	uiAssetsSync_t bound = { trackedLock, trackedUnlock, &first };
	uiAssetsSync_t same = { trackedLock, trackedUnlock, &first };
	uiAssetsSync_t changedCtx = { trackedLock, trackedUnlock, &second };
	uiAssetsSync_t changedCallbacks = {
		alternateLock, alternateUnlock, &first
	};
	uiAssetsSync_t lockOnly = { trackedLock, NULL, &first };
	uiAssetsSync_t unlockOnly = { NULL, trackedUnlock, &first };
	int reads;

	CHECK(UIAssets_Init(&src, &bound) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange();

	/* The identical triple survives Cancel and is accepted on re-Init. */
	CHECK(UIAssets_Init(&src, &same) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange();

	/* Context, callback identity, and completeness are immutable until the
	 * final disposal. Rejections happen before any source read. */
	reads = mem.readCalls;
	CHECK(UIAssets_Init(&src, &changedCtx) == UI_ASSETS_ERR_STATE);
	CHECK(UIAssets_Init(&src, &changedCallbacks) == UI_ASSETS_ERR_STATE);
	CHECK(UIAssets_Init(&src, &lockOnly) == UI_ASSETS_ERR_STATE);
	CHECK(UIAssets_Init(&src, &unlockOnly) == UI_ASSETS_ERR_STATE);
	CHECK(mem.readCalls == reads);
	CHECK(!UIAssets_Ready());

	/* Failed replacements do not disturb the original binding. */
	CHECK(UIAssets_Init(&src, &bound) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	free(pack);
}

static void test_dispose_requires_cancel(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	u32 footprint;
	int allocations;
	int calls;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	footprint = UIAssets_MemoryFootprint();
	allocations = liveAllocs;
	calls = lockCalls;
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_ERR_STATE);
	CHECK(lockCalls == calls); /* even the error path is lock-free */
	CHECK(UIAssets_Ready());
	CHECK(UIAssets_MemoryFootprint() == footprint);
	CHECK(liveAllocs == allocations);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	free(pack);
}

static void test_dispose_after_mutex_destroyed(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	trackedMutex_t mutex = { 0, 0, 0 };
	uiAssetsSync_t sync = { trackedLock, trackedUnlock, &mutex };
	int locks, unlocks, allLocks;

	CHECK(UIAssets_Init(&src, &sync) == UI_ASSETS_OK);
	UIAssets_CancelForDeviceChange(); /* mutex is still live here */
	locks = mutex.lockCalls;
	unlocks = mutex.unlockCalls;
	allLocks = lockCalls;
	mutex.destroyed = 1;
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(mutex.lockCalls == locks);
	CHECK(mutex.unlockCalls == unlocks);
	CHECK(lockCalls == allLocks);
	CHECK(UIAssets_MemoryFootprint() == 0);
	CHECK(liveAllocs == 0);
	free(pack);
}

static void test_dispose_releases_everything(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(UIAssets_MemoryFootprint() == 0);
	CHECK(liveAllocs == 0);
	CHECK(liveBytes == 0);
	CHECK(UIAssets_Peek(h) == NULL);
	/* Handles from before disposal stay stale even after re-Init. */
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Peek(h) == NULL);
	free(pack);
}

static void test_quarantine_blocks_rewrite(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_SLOTS][8];
	int reads;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	makeIds(ids, 0, UI_ASSETS_SLOTS);
	UIAssets_RequestWindow(ids, UI_ASSETS_SLOTS, 0);
	pollAll();
	reads = mem.readCalls;

	/* Every slot holds a poster, so a new window reuses slots whose
	 * posters left it: their texels must not be rewritten until a frame
	 * has passed (the GPU may still sample). */
	makeIds(ids, 30, UI_ASSETS_WINDOW);
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, 0);
	CHECK(UIAssets_Poll());               /* work queued... */
	CHECK(mem.readCalls == reads);        /* ...but no rewrite yet */
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS / 2;
	UIAssets_Poll();
	CHECK(mem.readCalls == reads);        /* still inside quarantine */
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS;
	UIAssets_Poll();
	CHECK(mem.readCalls == reads + 1);    /* quarantine passed: loads */
	free(pack);
	free(recs);
}

static void test_scale_100_and_250(void) {
	int sizes[2] = { 100, 250 };
	int s;
	for (s = 0; s < 2; s++) {
		int count = sizes[s];
		testRec_t *recs = makeRecs(count);
		size_t len;
		u8 *pack = buildPack(recs, count, &len, 0);
		uiAssetsSource_t src = memSource(pack, len);
		char ids[UI_ASSETS_WINDOW][8];
		int pos;
		uiPosterHandle_t h;

		CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
		for (pos = 0; pos + UI_ASSETS_WINDOW <= count; pos += 13) {
			UIAssets_RequestWindow(ids, 0, 0); /* degenerate call is a no-op */
			makeIds(ids, pos, UI_ASSETS_WINDOW);
			UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
			pollAll();
			CHECK(UIAssets_Query(ids[3], 6, true, &h) == UI_POSTER_EXACT);
			CHECK(UIAssets_Peek(h) != NULL);
			if (UIAssets_Peek(h)) {
				u8 expect[16];
				u8 full[UI_ASSETS_POSTER_BYTES];
				fillPayload(full, ids[3]);
				memcpy(expect, full, 16);
				CHECK(memcmp(UIAssets_Peek(h)->data, expect, 16) == 0);
			}
			CHECK(UIAssets_MemoryFootprint() ==
			      UI_ASSETS_SLOTS * UI_ASSETS_POSTER_BYTES +
			      (u32)count * REC);
		}
		UIAssets_CancelForDeviceChange();
		CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
		free(pack);
		free(recs);
	}
}

static void test_max_records_boundary(void) {
	int count = UI_ASSETS_MAX_RECORDS;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* Ceiling holds at the largest legal pack: arena + full 64 KiB index. */
	CHECK(UIAssets_MemoryFootprint() ==
	      UI_ASSETS_SLOTS * UI_ASSETS_POSTER_BYTES +
	      (u32)UI_ASSETS_MAX_RECORDS * REC);
	{
		char ids[UI_ASSETS_WINDOW][8];
		makeIds(ids, count - UI_ASSETS_WINDOW, UI_ASSETS_WINDOW);
		UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, UI_ASSETS_WINDOW / 2);
		pollAll();
		checkLoaded(ids[3]); /* deep-index record loads correctly */
	}
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	/* One record over the limit must be rejected before anything else is
	 * trusted. */
	wbe32(pack + 0x0C, UI_ASSETS_MAX_RECORDS + 1);
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	free(pack);
	free(recs);
}

static void test_memory_budget(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	int allocsBefore;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* Arena: 25 x 43,648 = 1,091,200 bytes, the Grid layout's five rows of
	 * five (a carousel's nine and the posters it scrolled past); index
	 * <= 32 KiB on top. */
	CHECK(UI_ASSETS_SLOTS * UI_ASSETS_POSTER_BYTES == 1091200);
	CHECK(UIAssets_MemoryFootprint() <= 1091200 + 32768);
	allocsBefore = allocCalls;
	allocsFrozen = 1;
	requestOne("GALE01");
	pollAll();
	{
		uiPosterHandle_t h;
		UIAssets_Query("GALE01", 6, true, &h);
		UIAssets_Peek(h);
		UIAssets_DominantColor("GALE01", 6, NULL, NULL, NULL);
	}
	allocsFrozen = 0;
	CHECK(allocCalls == allocsBefore); /* zero allocation after Init */
	free(pack);
}

static void test_dominant_color(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	u8 r = 0, g = 0, b = 0;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UIAssets_DominantColor("GALE01", 6, &r, &g, &b));
	CHECK(r == 10 && g == 20 && b == 30);
	CHECK(UIAssets_DominantColor("GZLE69", 6, &r, &g, &b)); /* via universal */
	CHECK(r == 40 && g == 50 && b == 60);
	CHECK(!UIAssets_DominantColor("XXXX00", 6, NULL, NULL, NULL));
	free(pack);
}

static void test_bad_ids_and_args(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	char ids[9][8];
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UIAssets_Query(NULL, 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_Query("gale01", 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_Query("GA LE1", 6, false, &h) == UI_POSTER_PROCEDURAL_CARD);
	CHECK(UIAssets_Query("GALE01", 6, true, NULL) == UI_POSTER_EXACT);
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], "GALE01", 6);
	UIAssets_RequestWindow(NULL, 3, 0);
	UIAssets_RequestWindow(ids, 9, 42);   /* clamped, no crash */
	UIAssets_RequestWindow(ids, -1, -5);
	pollAll();
	{
		uiPosterHandle_t bogus = { 0, 0xFFFF, 0 };
		CHECK(UIAssets_Peek(bogus) == NULL);
		bogus.slot = 3;
		bogus.generation = 0xFFFF;
		CHECK(UIAssets_Peek(bogus) == NULL);
	}
	free(pack);
}

static void test_uninitialized_api_is_inert(void) {
	uiPosterHandle_t h = { 0, 0, 0 };
	char ids[1][8];
	memset(ids, 0, sizeof(ids));
	CHECK(!UIAssets_Ready());
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_Peek(h) == NULL);
	CHECK(!UIAssets_Poll());
	CHECK(UIAssets_MemoryFootprint() == 0);
	UIAssets_RequestWindow(ids, 1, 0);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(liveAllocs == 0);
}

/* The pack's first open: no frame has drawn from the new arena, so the
 * first Poll after a request reads at once (Detail polls once before it
 * shows). A pack opened again over posters a frame may still sample keeps
 * the quarantine. */
static void test_fresh_arena_reads_at_once(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	int reads;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	reads = mem.readCalls;
	UIAssets_Poll();
	CHECK(mem.readCalls == reads + 1);
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);

	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	reads = mem.readCalls;
	CHECK(UIAssets_Poll());
	CHECK(mem.readCalls == reads); /* the old texels may still be drawn */
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	UIAssets_Poll();
	CHECK(mem.readCalls == reads + 1);
	free(pack);
}

static void test_poll_lock_phase_split(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	int calls, reads;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	calls = lockCalls;
	UIAssets_Poll(); /* loads: capture section + publish section */
	CHECK(lockCalls == calls + 2);
	{
		uiPosterHandle_t h;
		CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
		CHECK(UIAssets_Peek(h) != NULL);
	}
	calls = lockCalls;
	CHECK(!UIAssets_Poll()); /* idle: one section, no read */
	CHECK(lockCalls == calls + 1);

	/* Quarantined pending work: one section, and provably no read. */
	requestOne("GC6E01");
	requestOne("GZLE01"); /* drops GC6E01 unread, takes its quarantined slot */
	calls = lockCalls;
	reads = mem.readCalls;
	CHECK(UIAssets_Poll());
	CHECK(lockCalls == calls + 1);
	CHECK(mem.readCalls == reads);
	free(pack);
}

static void test_video_apis_never_lock(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	int calls;
	u8 r, g, b;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	/* The EV_GAMEFLOW render pass calls these while it already owns the
	 * video mutex: they must never take the lock themselves. */
	calls = lockCalls;
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	CHECK(UIAssets_Query("GZLE69", 6, true, NULL) == UI_POSTER_UNIVERSAL);
	CHECK(UIAssets_DominantColor("GALE01", 6, &r, &g, &b));
	CHECK(lockCalls == calls);
	free(pack);
}

static void test_menu_apis_single_section(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	int calls;
	char ids[1][8];

	calls = lockCalls;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(lockCalls == calls + 1); /* staging is unlocked; publish locked */
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);

	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], "GALE01", 6);
	calls = lockCalls;
	UIAssets_RequestWindow(ids, 1, 0);
	CHECK(lockCalls == calls + 1);
	calls = lockCalls;
	UIAssets_CancelForDeviceChange();
	CHECK(lockCalls == calls + 1);
	/* Final disposal follows the live-mutex Cancel above and must never
	 * invoke the synchronization callbacks after the video thread stops. */
	calls = lockCalls;
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(lockCalls == calls);
	free(pack);
}

static void chaosCancel(void) {
	memReadHook = NULL; /* fire once */
	UIAssets_CancelForDeviceChange();
}

static void test_cancel_inside_read_drops_publication(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	/* The device disappears while Poll's unlocked read is in flight (the
	 * hook runs from inside the read callback, where the lock is NOT
	 * held). The completed job must be dropped, not published. */
	memReadHook = chaosCancel;
	CHECK(!UIAssets_Poll());
	CHECK(!UIAssets_Ready());
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);

	/* Re-open the pack: the stale job must not have leaked a READY state
	 * -- the poster still needs a real load. */
	src = memSource(pack, len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) == NULL); /* pending, not stale-published */
	pollAll();
	checkLoaded("GALE01");
	free(pack);
}

static u8 *chaosPack;
static size_t chaosPackLen;

static void chaosCancelAndReinit(void) {
	char ids[1][8];
	uiAssetsSource_t src;
	memReadHook = NULL; /* fire once */
	UIAssets_CancelForDeviceChange();
	src = memSource(chaosPack, chaosPackLen);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], "GBLE01", 6);
	UIAssets_RequestWindow(ids, 1, 0);
}

static void test_reinit_inside_read_no_stale_publish(void) {
	static const testRec_t PACK_B[] = {
		{ "GBLE01", 0, { 7, 7, 7 } },
	};
	size_t lenA;
	u8 *packA = buildPack(BASIC, 3, &lenA, 0);
	uiAssetsSource_t src;
	uiPosterHandle_t h;

	chaosPack = buildPack(PACK_B, 1, &chaosPackLen, 0);
	src = memSource(packA, lenA);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	/* Mid-read, the whole pack is swapped (cancel + re-Init + rewindow).
	 * The in-flight job -- which now reads from the NEW backing store at
	 * the OLD offset -- must never publish into the new owner's slot. */
	memReadHook = chaosCancelAndReinit;
	UIAssets_Poll();
	CHECK(UIAssets_Ready());
	CHECK(UIAssets_Query("GBLE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) == NULL); /* still pending: stale job dropped */
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);
	pollAll();
	checkLoaded("GBLE01");
	free(packA);
	free(chaosPack);
	chaosPack = NULL;
}

static void test_generation_u32_wrap(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_SLOTS][8];
	uiPosterHandle_t preWrap, postWrap;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("G000E0");
	pollAll();
	CHECK(UIAssets_Query("G000E0", 6, true, &preWrap) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(preWrap) != NULL);

	/* Park the slot at the wrap boundary, then hand ownership over: the
	 * next assignment increments 0xFFFFFFFF -> 0. A full window of other
	 * posters fills the empty slots first, so its farthest takes it. */
	UIAssetsTest_ForceGeneration(preWrap.slot, 0xFFFFFFFFu);
	CHECK(UIAssets_Query("G000E0", 6, true, &preWrap) == UI_POSTER_EXACT);
	CHECK(preWrap.generation == 0xFFFFFFFFu);
	makeIds(ids, 10, UI_ASSETS_SLOTS);
	UIAssets_RequestWindow(ids, UI_ASSETS_SLOTS, 0);
	pollAll();
	CHECK(UIAssets_Query(ids[UI_ASSETS_SLOTS - 1], 6, true, &postWrap) ==
	      UI_POSTER_EXACT);
	CHECK(postWrap.slot == preWrap.slot);
	CHECK(postWrap.generation == 0);
	CHECK(UIAssets_Peek(postWrap) != NULL);
	/* The boundary handle is stale across the wrap, not resurrected. */
	CHECK(UIAssets_Peek(preWrap) == NULL);
	free(pack);
	free(recs);
}

static void test_short_id_rejected_before_read(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	char *shortId;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* Exactly-5-byte heap buffer: under ASan, touching byte 6 aborts, so
	 * passing this proves rejection happens before any read. */
	shortId = malloc(5);
	memcpy(shortId, "GALE0", 5);
	CHECK(UIAssets_Query(shortId, 5, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_Query(shortId, 0, false, &h) == UI_POSTER_PROCEDURAL_CARD);
	CHECK(!UIAssets_DominantColor(shortId, 5, NULL, NULL, NULL));
	free(shortId);
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Query("GALE01xyz", 9, true, &h) == UI_POSTER_EXACT);
	free(pack);
}

static char chaosWindowIds[UI_ASSETS_WINDOW][8];

static void chaosRewindow(void) {
	memReadHook = NULL; /* fire once */
	UIAssets_RequestWindow(chaosWindowIds, UI_ASSETS_WINDOW, 0);
}

static void test_request_window_mid_read_no_stale_publish(void) {
	/* Pure eviction/reassignment between Poll's capture and publish, with
	 * NO cancel: the loading slot is evicted and reassigned to a new
	 * record while its read is in flight. The stale job must not publish
	 * into the new owner. */
	int count = 20;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildPack(recs, count, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[UI_ASSETS_WINDOW][8];
	uiPosterHandle_t h;
	int i;

	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	makeIds(ids, 0, UI_ASSETS_WINDOW);       /* window A: 0..8 */
	makeIds(chaosWindowIds, 10, UI_ASSETS_WINDOW); /* window B: 10..18 */
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, 0);
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	memReadHook = chaosRewindow;
	UIAssets_Poll(); /* loads G000E0; mid-read the window jumps to B */
	/* The in-flight G000E0 job must not have published anywhere. */
	CHECK(UIAssets_Query("G000E0", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) == NULL);
	for (i = 0; i < UI_ASSETS_WINDOW; i++) {
		CHECK(UIAssets_Query(chaosWindowIds[i], 6, true, &h) ==
		      UI_POSTER_EXACT);
		CHECK(UIAssets_Peek(h) == NULL); /* pending, never stale-published */
	}
	pollAll();
	for (i = 0; i < UI_ASSETS_WINDOW; i++)
		checkLoaded(chaosWindowIds[i]); /* correct content lands after */
	free(pack);
	free(recs);
}

/* ---- stills (stills.pak): the second cache ---- */

/* A second in-memory source: the stills cache and the poster cache can be
 * open at once, and memSource() serves only one pack. */
typedef struct {
	const u8 *data;
	size_t len;
} testBuf_t;

static s32 bufRead(void *ctx, u32 offset, void *dst, u32 len) {
	const testBuf_t *buf = (const testBuf_t *)ctx;
	if (lockDepth != 0) {
		fprintf(stderr, "FAIL [%s] source read under the lock (depth %d)\n",
		        currentTest, lockDepth);
		failures++;
	}
	if ((size_t)offset + len > buf->len)
		return UI_ASSETS_ERR_IO;
	memcpy(dst, buf->data + offset, len);
	return (s32)len;
}

static uiAssetsSource_t bufSource(testBuf_t *buf) {
	uiAssetsSource_t src;
	src.read = bufRead;
	src.size = (u32)buf->len;
	src.ctx = buf;
	src.nowMs = fakeNow;
	return src;
}

/* The selected game and the one either side, as the Library asks. */
static void requestStills(const char *before, const char *selected,
                          const char *after) {
	char ids[3][8];
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], before, UI_ASSETS_ID_LEN);
	memcpy(ids[1], selected, UI_ASSETS_ID_LEN);
	memcpy(ids[2], after, UI_ASSETS_ID_LEN);
	UIStills_RequestWindow(ids, 3, 1);
}

static void pollStills(void) {
	int guard = 64;
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	while (UIStills_Poll() && guard-- > 0)
		fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
}

static bool stillReady(const char *id) {
	uiPosterHandle_t h;
	UIStills_Query(id, 6, true, &h);
	return UIStills_Peek(h) != NULL;
}

/* A still is one 320x240 CMPR texture with no mip chain, byte for byte as
 * the pack holds it, looked up as a poster is. */
static void test_incremental_records_publish_only_when_complete(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	u32 start = rbe32(pack + 0x18), done = 0;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	mem.maxLength = 0;
	allocsFrozen = 1;
	while (done < UI_ASSETS_POSTER_BYTES) {
		int reads = mem.readCalls;
		bool more = UIAssets_PollStep();
		u32 take = UI_ASSETS_POSTER_BYTES - done;
		if (take > UI_ASSETS_READ_BYTES) take = UI_ASSETS_READ_BYTES;
		CHECK(mem.readCalls == reads + 1);
		CHECK(mem.lastOffset == start + done && mem.lastLength == take);
		done += take;
		CHECK((UIAssets_Peek(h) != NULL) == (done == UI_ASSETS_POSTER_BYTES));
		CHECK(more == (done < UI_ASSETS_POSTER_BYTES));
		CHECK(flushCalls == (done == UI_ASSETS_POSTER_BYTES ? 1 : 0));
	}
	CHECK(mem.maxLength <= UI_ASSETS_READ_BYTES);
	CHECK(memcmp(UIAssets_Peek(h)->data, pack + start, UI_ASSETS_POSTER_BYTES) == 0);
	allocsFrozen = 0;
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS;
	requestOne("GALE01");
	CHECK(UIAssets_PollStep());
	int reads = mem.readCalls;
	UIAssets_CancelForDeviceChange();
	CHECK(!UIAssets_PollStep() && mem.readCalls == reads);
	CHECK(UIAssets_Peek(h) == NULL);
	free(pack);
}

static void test_incremental_crc_and_read_failure(void) {
	size_t len;
	u8 *pack = buildPack(BASIC, 3, &len, 0);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	pack[rbe32(pack + 0x18) + UI_ASSETS_READ_BYTES + 7] ^= 1;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	CHECK(UIAssets_PollStep());
	CHECK(UIAssets_PollStep());
	CHECK(!UIAssets_PollStep());
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	CHECK(UIAssets_Peek(h) == NULL && flushCalls == 0);
	requestOne("GC6E01");
	CHECK(UIAssets_PollStep());
	mem.failAfter = mem.readCalls;
	CHECK(!UIAssets_PollStep());
	CHECK(UIAssets_Query("GC6E01", 6, true, &h) == UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	CHECK(UIAssets_Peek(h) == NULL && flushCalls == 0);
	free(pack);
}

static void test_incremental_stills_and_full_poll_interoperate(void) {
	size_t len;
	u8 *pack = buildStillPack(BASIC, 3, &len);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	char empty[8] = {0};
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	requestStills(empty, "GC6E01", empty);
	CHECK(UIStills_Query("GC6E01", 6, true, &h) == UI_POSTER_EXACT);
	mem.maxLength = 0;
	CHECK(UIStills_PollStep());
	CHECK(UIStills_Peek(h) == NULL && mem.maxLength <= UI_ASSETS_READ_BYTES);
	CHECK(!UIStills_Poll()); /* finish the same record, continuing its CRC */
	CHECK(UIStills_Peek(h) != NULL && flushCalls == 1);
	CHECK(!UIAssets_Ready());
	free(pack);
}

static void test_stills_load_one_texture_each(void) {
	size_t len;
	u8 *pack = buildStillPack(BASIC, 3, &len);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	GXTexObj *tex;
	u8 r = 0, g = 0, b = 0;
	static u8 full[UI_STILLS_BYTES];
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UIStills_Ready());
	CHECK(!UIAssets_Ready()); /* a cache of its own */
	CHECK(UIStills_MemoryFootprint() ==
	      UI_STILLS_SLOTS * UI_STILLS_BYTES + 3 * REC);
	requestStills("GALE01", "GC6E01", "GZLE01");
	pollStills();
	CHECK(flushCalls == 3);
	CHECK(UIStills_Query("GC6E01", 6, true, &h) == UI_POSTER_EXACT);
	tex = UIStills_Peek(h);
	CHECK(tex != NULL);
	if (tex) {
		CHECK(tex->width == UI_STILLS_W && tex->height == UI_STILLS_H);
		CHECK(tex->format == 14 && !tex->mipmap && !tex->lodConfigured);
		fillPayloadLen(full, "GC6E01", UI_STILLS_BYTES);
		CHECK(memcmp(tex->data, full, UI_STILLS_BYTES) == 0);
	}
	CHECK(UIStills_Query("GZLE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	CHECK(UIStills_Peek(h) != NULL);
	CHECK(UIStills_Query("GC6E69", 6, false, &h) ==
	      UI_POSTER_PROCEDURAL_CARD);
	CHECK(UIStills_DominantColor("GALE01", 6, &r, &g, &b));
	CHECK(r == 10 && g == 20 && b == 30);
	free(pack);
}

/* A picture's age counts from its publication, stamped inside the critical
 * section by the cache's own clock: 0 on the frame it lands, then the
 * clock's time, across the clock's wrap too, and 0 for a handle whose
 * picture is not there. Posters and stills alike. */
static void test_ready_age(void) {
	int count = 40;
	testRec_t *recs = makeRecs(count);
	size_t len, stillLen;
	u8 *pack = buildPack(recs, count, &len, 0);
	u8 *stillPack = buildStillPack(BASIC, 3, &stillLen);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	char ids[UI_ASSETS_SLOTS][8];

	fakeNowValue = 0xFFFFFF00u;
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	requestOne("G000E0");
	CHECK(UIAssets_Query("G000E0", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_PeekAgeMs(h) == 0); /* not read yet */
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	CHECK(!UIAssets_Poll());
	CHECK(nowLockDepth == 1); /* the stamp is the locked publication's */
	CHECK(UIAssets_Peek(h) != NULL);
	CHECK(UIAssets_PeekAgeMs(h) == 0);
	fakeNowValue += 0x100u; /* past the clock's wrap */
	CHECK(UIAssets_PeekAgeMs(h) == 0x100u);
	/* A full window of other posters takes its slot: no picture, no age. */
	makeIds(ids, 10, UI_ASSETS_SLOTS);
	UIAssets_RequestWindow(ids, UI_ASSETS_SLOTS, 0);
	pollAll();
	CHECK(UIAssets_Peek(h) == NULL);
	CHECK(UIAssets_PeekAgeMs(h) == 0);

	src = memSource(stillPack, stillLen);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	requestStills("GALE01", "GC6E01", "GZLE01");
	CHECK(UIStills_Query("GC6E01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIStills_PeekAgeMs(h) == 0);
	pollStills();
	CHECK(UIStills_Peek(h) != NULL);
	fakeNowValue += 75u;
	CHECK(UIStills_PeekAgeMs(h) >= 75u);
	free(pack);
	free(stillPack);
	free(recs);
}

/* posters.pak and stills.pak declare different textures, and each cache
 * refuses the other's pack before it allocates anything. */
static void test_stills_and_posters_refuse_each_other(void) {
	size_t posterLen, stillLen;
	u8 *posterPack = buildPack(BASIC, 3, &posterLen, 0);
	u8 *stillPack = buildStillPack(BASIC, 3, &stillLen);
	uiAssetsSource_t src = memSource(posterPack, posterLen);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(!UIStills_Ready());
	src = memSource(stillPack, stillLen);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(!UIAssets_Ready());
	CHECK(liveAllocs == 0);
	free(posterPack);
	free(stillPack);
}

/* ---- discs (discs.pak): the third cache ---- */

/* A disc is one 160x160 CMPR texture with no mip chain, byte for byte as the
 * pack holds it, in a cache of its own of three slots. */
static void test_discs_load_one_texture_each(void) {
	size_t len;
	u8 *pack = buildDiscPack(BASIC, 3, &len);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	GXTexObj *tex;
	char ids[3][8];
	int guard = 64;
	static u8 full[UI_DISCS_BYTES];
	CHECK(UIDiscs_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UIDiscs_Ready());
	CHECK(!UIAssets_Ready() && !UIStills_Ready()); /* a cache of its own */
	CHECK(UIDiscs_MemoryFootprint() ==
	      UI_DISCS_SLOTS * UI_DISCS_BYTES + 3 * REC);
	memset(ids, 0, sizeof(ids));
	memcpy(ids[0], "GALE01", UI_ASSETS_ID_LEN);
	memcpy(ids[1], "GC6E01", UI_ASSETS_ID_LEN);
	memcpy(ids[2], "GZLE01", UI_ASSETS_ID_LEN);
	UIDiscs_RequestWindow((const char (*)[8])ids, 3, 1);
	fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	while (UIDiscs_Poll() && guard-- > 0)
		fakeNowValue += UI_ASSETS_EVICT_QUARANTINE_MS + 1;
	CHECK(flushCalls == 3);
	CHECK(UIDiscs_Query("GC6E01", 6, true, &h) == UI_POSTER_EXACT);
	tex = UIDiscs_Peek(h);
	CHECK(tex != NULL);
	if (tex) {
		CHECK(tex->width == UI_DISCS_SIZE && tex->height == UI_DISCS_SIZE);
		CHECK(tex->format == 14 && !tex->mipmap && !tex->lodConfigured);
		fillPayloadLen(full, "GC6E01", UI_DISCS_BYTES);
		CHECK(memcmp(tex->data, full, UI_DISCS_BYTES) == 0);
	}
	CHECK(UIDiscs_Query("GZLE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	CHECK(UIDiscs_Peek(h) != NULL);
	free(pack);
}

/* Each cache takes only its own pack's shape. */
typedef struct {
	u8 *data;
	size_t length;
	u32 calls, bytes;
} pollSource_t;

static s32 pollSourceRead(void *ctx, u32 offset, void *dst, u32 length)
{
	pollSource_t *source = ctx;
	CHECK(lockDepth == 0);
	CHECK(length <= UI_ASSETS_READ_BYTES);
	if((u64)offset + length > source->length) return UI_ASSETS_ERR_IO;
	source->calls++;
	source->bytes += length;
	memcpy(dst, source->data + offset, length);
	return (s32)length;
}

static void test_visible_packs_share_one_read_and_pause_hidden(void)
{
	pollSource_t sources[3] = {{0}};
	sources[0].data = buildPack(BASIC, 3, &sources[0].length, 0);
	sources[1].data = buildStillPack(BASIC, 3, &sources[1].length);
	sources[2].data = buildDiscPack(BASIC, 3, &sources[2].length);
	for(int i = 0; i < 3; i++) {
		uiAssetsSource_t source = {.ctx = &sources[i], .read = pollSourceRead,
			.size = (u32)sources[i].length, .nowMs = fakeNow};
		s32 result = i == 0 ? UIAssets_Init(&source, &testSync) :
			i == 1 ? UIStills_Init(&source, &testSync) : UIDiscs_Init(&source, &testSync);
		CHECK(result == UI_ASSETS_OK);
		sources[i].calls = sources[i].bytes = 0;
	}
	const char ids[1][8] = {"GALE01"};
	uiPosterHandle_t poster, still, disc;
	UIAssets_RequestWindow(ids, 1, 0);
	UIStills_RequestWindow(ids, 1, 0);
	UIDiscs_RequestWindow(ids, 1, 0);
	UIAssets_Query(ids[0], 6, false, &poster);
	UIStills_Query(ids[0], 6, false, &still);
	UIDiscs_Query(ids[0], 6, false, &disc);
	allocsFrozen = 1;

	/* Spotlight shares ticks fairly; neither unfinished texture is visible. */
	for(int tick = 0; tick < 2; tick++) {
		CHECK(UIAssets_PollVisible(true, false));
		CHECK(sources[0].calls + sources[1].calls == (u32)(tick + 1));
		CHECK(sources[0].bytes + sources[1].bytes <= (u32)(tick + 1) * UI_ASSETS_READ_BYTES);
	}
	CHECK(sources[0].calls == 1 && sources[1].calls == 1 && sources[2].calls == 0);
	CHECK(!UIAssets_Peek(poster) && !UIStills_Peek(still) && !UIDiscs_Peek(disc));

	/* Switching to Jewel Case pauses the partial still and finishes the
	 * poster and disc. A final range may return false, but must be published. */
	bool more = true;
	for(int tick = 0; tick < 12 && more; tick++) {
		u32 beforeCalls = sources[0].calls + sources[2].calls;
		u32 beforeBytes = sources[0].bytes + sources[2].bytes;
		more = UIAssets_PollVisible(false, true);
		CHECK(sources[0].calls + sources[2].calls <= beforeCalls + 1);
		CHECK(sources[0].bytes + sources[2].bytes <= beforeBytes + UI_ASSETS_READ_BYTES);
		CHECK(sources[1].calls == 1);
	}
	CHECK(!more && UIAssets_Peek(poster) && UIDiscs_Peek(disc));
	CHECK(!UIStills_Peek(still));
	CHECK(!UIAssets_PollVisible(false, false)); /* hidden work is not drained */
	CHECK(sources[1].calls == 1);

	/* Resume the same still's partial read and CRC, without restarting it. */
	more = true;
	for(int tick = 0; tick < 12 && more; tick++) {
		u32 calls = sources[1].calls, bytes = sources[1].bytes;
		more = UIAssets_PollVisible(true, false);
		CHECK(sources[1].calls <= calls + 1);
		CHECK(sources[1].bytes <= bytes + UI_ASSETS_READ_BYTES);
	}
	CHECK(!more && UIStills_Peek(still));
	CHECK(sources[1].bytes == UI_STILLS_BYTES);
	for(int i = 0; i < 3; i++) free(sources[i].data);
}

static void test_discs_refuse_the_other_packs(void) {
	size_t posterLen, stillLen, discLen;
	u8 *posterPack = buildPack(BASIC, 3, &posterLen, 0);
	u8 *stillPack = buildStillPack(BASIC, 3, &stillLen);
	u8 *discPack = buildDiscPack(BASIC, 3, &discLen);
	uiAssetsSource_t src = memSource(posterPack, posterLen);
	CHECK(UIDiscs_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	src = memSource(stillPack, stillLen);
	CHECK(UIDiscs_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(!UIDiscs_Ready());
	src = memSource(discPack, discLen);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_ERR_FORMAT);
	CHECK(!UIAssets_Ready() && !UIStills_Ready());
	CHECK(liveAllocs == 0);
	free(posterPack);
	free(stillPack);
	free(discPack);
}

/* Three slots: a longer window is cut to its first three entries, and
 * moving one game on reads only the still that came into the window. */
static void test_stills_window_of_three(void) {
	int count = 7;
	testRec_t *recs = makeRecs(count);
	size_t len;
	u8 *pack = buildStillPack(recs, count, &len);
	uiAssetsSource_t src = memSource(pack, len);
	char ids[7][8];
	int i, ready = 0;
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	memset(ids, 0, sizeof(ids));
	for (i = 0; i < count; i++)
		memcpy(ids[i], recs[i].id, UI_ASSETS_ID_LEN);
	UIStills_RequestWindow((const char (*)[8])ids, count, 1);
	pollStills();
	for (i = 0; i < count; i++)
		ready += stillReady(ids[i]) ? 1 : 0;
	CHECK(ready == 3);
	CHECK(stillReady(ids[0]) && stillReady(ids[1]) && stillReady(ids[2]));
	CHECK(flushCalls == 3);
	UIStills_RequestWindow((const char (*)[8])&ids[1], 3, 1);
	pollStills();
	CHECK(!stillReady(ids[0]));
	CHECK(stillReady(ids[1]) && stillReady(ids[2]) && stillReady(ids[3]));
	CHECK(flushCalls == 4);
	free(recs);
	free(pack);
}

/* A damaged still fails alone, as a damaged poster does. */
static void test_stills_payload_corrupt(void) {
	size_t len;
	u8 *pack = buildStillPack(BASIC, 3, &len);
	uiAssetsSource_t src;
	uiPosterHandle_t h;
	/* GC6E01 is the second record; flip a byte inside its still. */
	pack[HDR + 3 * REC + UI_STILLS_BYTES + 100] ^= 0xFF;
	src = memSource(pack, len);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	requestStills("GALE01", "GC6E01", "GZLE01");
	pollStills();
	CHECK(UIStills_Query("GC6E01", 6, true, &h) ==
	      UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	CHECK(UIStills_Peek(h) == NULL);
	CHECK(stillReady("GALE01") && stillReady("GZLE01"));
	free(pack);
}

/* Either cache can close for a device change and be disposed without the
 * other noticing: each has its own pack, arena and lifetime. */
static void test_stills_independent_of_posters(void) {
	size_t posterLen, stillLen;
	u8 *posterPack = buildPack(BASIC, 3, &posterLen, 0);
	u8 *stillPack = buildStillPack(BASIC, 3, &stillLen);
	testBuf_t stillBuf;
	uiAssetsSource_t posterSrc = memSource(posterPack, posterLen);
	uiAssetsSource_t stillSrc;
	uiPosterHandle_t h;
	stillBuf.data = stillPack;
	stillBuf.len = stillLen;
	stillSrc = bufSource(&stillBuf);
	CHECK(UIAssets_Init(&posterSrc, &testSync) == UI_ASSETS_OK);
	CHECK(UIStills_Init(&stillSrc, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	requestStills("GALE01", "GC6E01", "GZLE01");
	pollStills();
	UIAssets_CancelForDeviceChange();
	CHECK(!UIAssets_Ready());
	CHECK(UIStills_Ready() && stillReady("GC6E01"));
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_USE_BNR);
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(UIAssets_MemoryFootprint() == 0);
	CHECK(UIStills_MemoryFootprint() ==
	      UI_STILLS_SLOTS * UI_STILLS_BYTES + 3 * REC);
	UIStills_CancelForDeviceChange();
	CHECK(UIStills_DisposeAfterVideoStop() == UI_ASSETS_OK);
	CHECK(liveAllocs == 0);
	/* And the other way round. */
	posterSrc = memSource(posterPack, posterLen);
	stillSrc = bufSource(&stillBuf);
	CHECK(UIAssets_Init(&posterSrc, &testSync) == UI_ASSETS_OK);
	CHECK(UIStills_Init(&stillSrc, &testSync) == UI_ASSETS_OK);
	requestOne("GALE01");
	pollAll();
	UIStills_CancelForDeviceChange();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	free(posterPack);
	free(stillPack);
}

/* 3 x 38,400 bytes, only once a stills pack opens, and nothing allocated
 * after that. */
static void test_stills_memory_budget(void) {
	size_t len;
	u8 *pack = buildStillPack(BASIC, 3, &len);
	uiAssetsSource_t src = memSource(pack, len);
	uiPosterHandle_t h;
	int allocsBefore;
	CHECK(UIStills_MemoryFootprint() == 0);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	CHECK(UI_STILLS_SLOTS * UI_STILLS_BYTES == 115200);
	allocsBefore = allocCalls;
	allocsFrozen = 1;
	requestStills("GALE01", "GC6E01", "GZLE01");
	pollStills();
	UIStills_Query("GC6E01", 6, true, &h);
	UIStills_Peek(h);
	UIStills_DominantColor("GC6E01", 6, NULL, NULL, NULL);
	allocsFrozen = 0;
	CHECK(allocCalls == allocsBefore);
	free(pack);
}

static u8 *readWholeFile(const char *path, long *outLen) {
	FILE *f = fopen(path, "rb");
	u8 *data;
	long len;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(len > 0 ? (size_t)len : 1u);
	if (!data || len <= 0 || fread(data, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(data);
		return NULL;
	}
	fclose(f);
	*outLen = len;
	return data;
}

/* The generator's own stills pack (fixture_pack.py --stills), end to end. */
static void test_real_stills_pack(const char *path) {
	long len = 0;
	u8 *data = readWholeFile(path, &len);
	uiAssetsSource_t src;
	uiPosterHandle_t h;
	int failuresBefore = failures;

	currentTest = "test_real_stills_pack";
	if (!data) {
		fprintf(stderr, "FAIL cannot read real stills pack %s\n", path);
		failures++;
		return;
	}
	resetWorld();
	src = memSource(data, (size_t)len);
	CHECK(UIStills_Init(&src, &testSync) == UI_ASSETS_OK);
	/* fixture_pack.py --stills writes GALE01 (universal), GC6E01, GM4E01. */
	requestStills("GALE01", "GC6E01", "GM4E01");
	pollStills();
	CHECK(UIStills_Query("GC6E01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIStills_Peek(h) != NULL);
	CHECK(UIStills_Query("GALE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	CHECK(UIStills_Query("GM4E69", 6, true, &h) == UI_POSTER_USE_BNR);
	UIStills_CancelForDeviceChange();
	CHECK(UIStills_DisposeAfterVideoStop() == UI_ASSETS_OK);
	free(data);
	printf("  %-44s %s\n", "test_real_stills_pack",
	       failures == failuresBefore ? "ok" : "see above");
}

static void test_real_pack(const char *path) {
	FILE *f = fopen(path, "rb");
	u8 *data;
	long len;
	uiAssetsSource_t src;
	uiPosterHandle_t h;

	currentTest = "test_real_pack";
	if (!f) {
		fprintf(stderr, "FAIL cannot open real pack %s\n", path);
		failures++;
		return;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc((size_t)len);
	if (fread(data, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(data);
		failures++;
		return;
	}
	fclose(f);

	resetWorld();
	src = memSource(data, (size_t)len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	/* fixture_pack.py writes GALE01 (universal), GC6E01, GM4E01. */
	requestOne("GALE01");
	pollAll();
	CHECK(UIAssets_Query("GALE01", 6, true, &h) == UI_POSTER_EXACT);
	CHECK(UIAssets_Peek(h) != NULL);
	CHECK(UIAssets_Query("GALE69", 6, true, &h) == UI_POSTER_UNIVERSAL);
	CHECK(UIAssets_Query("GC6E69", 6, true, &h) == UI_POSTER_USE_BNR);
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	free(data);
	printf("  %-44s %s\n", "test_real_pack", failures ? "see above" : "ok");
}

static void test_exact_pack(const char *path, int idCount, char **gameIds) {
	FILE *f = fopen(path, "rb");
	u8 *data;
	long len;
	uiAssetsSource_t src;
	uiPosterHandle_t handle;
	char ids[UI_ASSETS_WINDOW][8] = {{0}};
	int i;

	currentTest = "test_exact_pack";
	if (!f || idCount < 1 || idCount > UI_ASSETS_WINDOW) {
		fprintf(stderr, "FAIL exact-pack needs a readable pack and 1..%d IDs\n",
		        UI_ASSETS_WINDOW);
		if (f)
			fclose(f);
		failures++;
		return;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc((size_t)len);
	if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(data);
		failures++;
		return;
	}
	fclose(f);

	for (i = 0; i < idCount; i++) {
		if (strlen(gameIds[i]) != UI_ASSETS_ID_LEN) {
			fprintf(stderr, "FAIL exact-pack ID must be six bytes: %s\n",
			        gameIds[i]);
			free(data);
			failures++;
			return;
		}
		memcpy(ids[i], gameIds[i], UI_ASSETS_ID_LEN);
	}

	resetWorld();
	src = memSource(data, (size_t)len);
	CHECK(UIAssets_Init(&src, &testSync) == UI_ASSETS_OK);
	UIAssets_RequestWindow(ids, idCount, 0);
	pollAll();
	for (i = 0; i < idCount; i++) {
		CHECK(UIAssets_Query(ids[i], UI_ASSETS_ID_LEN, false, &handle) ==
		      UI_POSTER_EXACT);
		CHECK(UIAssets_Peek(handle) != NULL);
	}
	UIAssets_CancelForDeviceChange();
	CHECK(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	free(data);
	printf("  %-44s %s\n", "test_exact_pack", failures ? "see above" : "ok");
}

int main(int argc, char **argv) {
	int failuresBefore = 0;

	RUN(test_init_valid);
	RUN(test_exact_hit);
	RUN(test_universal_fallback);
	RUN(test_no_implicit_fallback);
	RUN(test_duplicate_and_unsorted_rejected);
	RUN(test_ambiguous_universal_rejected);
	RUN(test_missing_or_truncated_pack);
	RUN(test_header_field_corruption);
	RUN(test_malformed_record_offsets);
	RUN(test_index_crc_mismatch);
	RUN(test_byte_flip_fuzz);
	RUN(test_field_validation_behind_valid_crc);
	RUN(test_index_read_failure);
	RUN(test_poster_payload_corrupt);
	RUN(test_short_read_fails_poster);
	RUN(test_nth_id_boundaries);
	RUN(test_window_prefetch_loads_whole_window);
	RUN(test_window_eviction_and_reversal);
	RUN(test_window_retention_oldest_first);
	RUN(test_max_records_boundary);
	RUN(test_stale_handle_after_eviction);
	RUN(test_grid_window_then_carousel);
	RUN(test_ready_age);
	RUN(test_cancel_for_device_change);
	RUN(test_sync_validation_policy);
	RUN(test_sync_binding_lifecycle);
	RUN(test_dispose_requires_cancel);
	RUN(test_dispose_after_mutex_destroyed);
	RUN(test_dispose_releases_everything);
	RUN(test_quarantine_blocks_rewrite);
	RUN(test_fresh_arena_reads_at_once);
	RUN(test_scale_100_and_250);
	RUN(test_memory_budget);
	RUN(test_dominant_color);
	RUN(test_bad_ids_and_args);
	RUN(test_uninitialized_api_is_inert);
	RUN(test_poll_lock_phase_split);
	RUN(test_video_apis_never_lock);
	RUN(test_menu_apis_single_section);
	RUN(test_cancel_inside_read_drops_publication);
	RUN(test_reinit_inside_read_no_stale_publish);
	RUN(test_generation_u32_wrap);
	RUN(test_short_id_rejected_before_read);
	RUN(test_request_window_mid_read_no_stale_publish);
	RUN(test_stills_load_one_texture_each);
	RUN(test_incremental_records_publish_only_when_complete);
	RUN(test_incremental_crc_and_read_failure);
	RUN(test_incremental_stills_and_full_poll_interoperate);
	RUN(test_stills_and_posters_refuse_each_other);
	RUN(test_stills_window_of_three);
	RUN(test_stills_payload_corrupt);
	RUN(test_stills_independent_of_posters);
	RUN(test_stills_memory_budget);
	RUN(test_discs_load_one_texture_each);
	RUN(test_discs_refuse_the_other_packs);
	RUN(test_visible_packs_share_one_read_and_pause_hidden);

	if (argc > 2 && strcmp(argv[1], "--exact-pack") == 0)
		test_exact_pack(argv[2], argc - 3, &argv[3]);
	else if (argc > 1)
		test_real_pack(argv[1]);
	if (argc > 2 && strcmp(argv[1], "--exact-pack") != 0)
		test_real_stills_pack(argv[2]);

	resetWorld();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
