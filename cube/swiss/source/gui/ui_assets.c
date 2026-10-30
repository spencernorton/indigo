/*
 * ui_assets.c -- bounded caches for the Library's pack art (Phase 4A):
 * posters, and gameplay stills.
 *
 * Pack format and budgets: docs/PACKS.md.
 * Contract, locking rules, and integration sketch: ui_assets.h.
 *
 * Compiles for the GameCube target and, with -DUI_ASSETS_HOST_BUILD, for
 * host test harnesses (buildtools/ui/tests/) with GX and allocation stubs.
 *
 * Concurrency model: one external critical section (the video mutex,
 * supplied as callbacks at Init). Menu-thread APIs enter it around every
 * metadata mutation and publication; video-thread APIs (Query/Peek/
 * DominantColor) are read-only and run with the caller already holding
 * it, so they never lock. The lock is never held across source.read, CRC,
 * allocation, or free: Init stages and validates the new index in private
 * memory before publishing it, Poll captures a load job inside the lock,
 * performs the bounded read + CRC + cache flush outside it, then
 * revalidates slot ownership (generation/state/record) inside the lock
 * before publishing the texture or failure. Published index memory is
 * detached inside the critical section before free, so no locked reader
 * can observe it. The arena is freed only by the separate lock-free final
 * disposal after all readers stop. Slot generations are u32 and advance
 * exactly once per new slot ownership (at assignment), never on eviction.
 *
 * Two caches run this code: posters (posters.pak) and stills (stills.pak).
 * They differ only in the texture shape their pack must declare and in
 * their slot count; each binds its own lock callbacks and owns its arena.
 */

#include <string.h>
#include <zlib.h>

#include "ui_assets.h"

#ifdef UI_ASSETS_HOST_BUILD

/* GX constants mirrored from libogc for the host build. */
#define GX_TF_CMPR 14
#define GX_CLAMP 0
#define GX_FALSE 0
#define GX_TRUE 1
#define GX_LINEAR 1
#define GX_LIN_MIP_LIN 5
#define GX_ANISO_1 0

/* Provided by the test harness so allocations can be counted. */
extern void *uiAssetsHostAllocAligned32(u32 size);
extern void *uiAssetsHostAlloc(u32 size);
extern void uiAssetsHostFree(void *ptr);

#define UIA_ALLOC_ALIGNED32(size) uiAssetsHostAllocAligned32(size)
#define UIA_ALLOC(size) uiAssetsHostAlloc(size)
#define UIA_FREE(ptr) uiAssetsHostFree(ptr)

static void GX_InitTexObj(GXTexObj *obj, void *img, u16 wd, u16 ht, u8 fmt,
                          u8 wrap_s, u8 wrap_t, u8 mipmap) {
	(void)wrap_s; (void)wrap_t;
	obj->data = img;
	obj->width = wd;
	obj->height = ht;
	obj->format = fmt;
	obj->mipmap = mipmap;
	obj->lodConfigured = 0;
}

static void GX_InitTexObjLOD(GXTexObj *obj, u8 minfilt, u8 magfilt,
                             float minlod, float maxlod, float lodbias,
                             u8 biasclamp, u8 edgelod, u8 maxaniso) {
	(void)minfilt; (void)magfilt; (void)lodbias;
	(void)biasclamp; (void)edgelod; (void)maxaniso;
	obj->minlod = minlod;
	obj->maxlod = maxlod;
	obj->lodConfigured = 1;
}

/* Routed to the harness so tests can assert the flush happens outside the
 * critical section, once per successful load. */
extern void uiAssetsHostFlush(void *ptr, u32 len);

static void DCFlushRange(void *ptr, u32 len) {
	uiAssetsHostFlush(ptr, len);
}

#else /* target */

#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/lwp_watchdog.h>

#define UIA_ALLOC_ALIGNED32(size) memalign(32, size)
#define UIA_ALLOC(size) malloc(size)
#define UIA_FREE(ptr) free(ptr)

#endif

/* Pack layout constants (mirrors buildtools/ui/poster_pack.py). */
#define PAK_MAGIC 0x5357504Bu /* 'SWPK' */
#define PAK_VERSION 1
#define PAK_HEADER_SIZE 64
#define PAK_RECORD_SIZE 32
#define PAK_FLAG_UNIVERSAL 0x01
#define PAK_FLAGS_KNOWN PAK_FLAG_UNIVERSAL

enum {
	SLOT_EMPTY = 0,
	SLOT_PENDING,
	SLOT_LOADING, /* captured by an in-flight Poll job */
	SLOT_READY,
	SLOT_FAILED
};

typedef struct {
	u32 generation; /* advances exactly once per new ownership */
	u8 state;
	u8 distance;
	s16 recordPos; /* index into the pack index; -1 when empty */
	u32 quarantineUntil; /* nowMs() stamp before which texels may not be rewritten */
	u32 readyMs; /* nowMs() stamp of its texture's publication */
	u32 wanted; /* the window request that last asked for its record */
	u8 *data; /* fixed arena region */
	GXTexObj tex;
} uiAssetSlot_t;

/* The texture a cache's pack must declare, exactly, and how many of them the
 * cache keeps. A texture with one level is not mipmapped. */
typedef struct {
	u32 recordBytes;
	u16 canvasW;
	u16 canvasH;
	u16 contentW;
	u16 contentH;
	u8 mipLevels;
	u8 slotCount; /* <= UI_ASSETS_SLOTS */
} uiAssetsShape_t;

typedef struct {
	bool active;
	int slotIdx;
	u32 generation;
	s16 recordPos;
	u32 dataOffset, expectedCrc, fileLength;
	u32 readBytes, crc;
	u8 *data;
	s32 (*read)(void *ctx, u32 offset, void *dst, u32 len);
	void *readCtx;
} uiAssetJob_t;

typedef struct {
	const uiAssetsShape_t *shape;
	bool arenaReady;   /* arena allocated (survives CancelForDeviceChange) */
	bool packOpen;     /* header+index validated, source readable */
	bool syncBound;    /* exact callback/context triple bound until disposal */
	uiAssetsSource_t src;
	uiAssetsSync_t sync;
	u32 (*nowMs)(void);
	u8 *arena;         /* slotCount * recordBytes, 32-aligned */
	u8 *index;         /* recordCount * PAK_RECORD_SIZE raw big-endian records */
	u32 recordCount;
	u32 dataOffset;
	u32 fileLength;
	u32 requests;      /* window requests so far */
	uiAssetJob_t loading; /* an unpublished record read between idle frames */
	uiAssetSlot_t slots[UI_ASSETS_SLOTS];
} uiAssetsCache_t;

static const uiAssetsShape_t posterShape = {
	UI_ASSETS_POSTER_BYTES, UI_ASSETS_CANVAS_W, UI_ASSETS_CANVAS_H,
	UI_ASSETS_CONTENT_W, UI_ASSETS_CONTENT_H, UI_ASSETS_MIP_LEVELS,
	UI_ASSETS_SLOTS
};

static const uiAssetsShape_t stillShape = {
	UI_STILLS_BYTES, UI_STILLS_W, UI_STILLS_H, UI_STILLS_W, UI_STILLS_H, 1,
	UI_STILLS_SLOTS
};

static const uiAssetsShape_t discShape = {
	UI_DISCS_BYTES, UI_DISCS_SIZE, UI_DISCS_SIZE, UI_DISCS_SIZE, UI_DISCS_SIZE, 1,
	UI_DISCS_SLOTS
};

_Static_assert(UI_STILLS_SLOTS <= UI_ASSETS_SLOTS, "stills slots overflow");
_Static_assert(UI_STILLS_BYTES == UI_STILLS_W * UI_STILLS_H / 2,
               "a CMPR still is half a byte per texel");
_Static_assert(UI_DISCS_SLOTS <= UI_ASSETS_SLOTS, "disc slots overflow");
_Static_assert(UI_DISCS_BYTES == UI_DISCS_SIZE * UI_DISCS_SIZE / 2,
               "a CMPR disc is half a byte per texel");

static uiAssetsCache_t posters = { .shape = &posterShape };
static uiAssetsCache_t stills = { .shape = &stillShape };
static uiAssetsCache_t discs = { .shape = &discShape };

/* Menu-thread-only critical-section entry/exit. Video APIs never call
 * these: their caller already owns the lock. */
static void syncLock(uiAssetsCache_t *c) {
	if (c->sync.lock)
		c->sync.lock(c->sync.ctx);
}

static void syncUnlock(uiAssetsCache_t *c) {
	if (c->sync.unlock)
		c->sync.unlock(c->sync.ctx);
}

/* Init binds one exact critical-section identity for the cache lifetime.
 * Host-only, single-threaded tests may bind the all-zero identity; target
 * builds require a complete callback pair. A half-pair is never valid. */
static s32 validateSync(const uiAssetsCache_t *c, const uiAssetsSync_t *sync,
                        uiAssetsSync_t *requested) {
	memset(requested, 0, sizeof(*requested));
	if (sync)
		*requested = *sync;
	if ((requested->lock == NULL) != (requested->unlock == NULL))
		return UI_ASSETS_ERR_STATE;
#if !defined(UI_ASSETS_HOST_BUILD) || defined(UI_ASSETS_HOST_REQUIRE_SYNC)
	if (!requested->lock)
		return UI_ASSETS_ERR_STATE;
#endif
	if (c->syncBound &&
	    (c->sync.lock != requested->lock ||
	     c->sync.unlock != requested->unlock ||
	     c->sync.ctx != requested->ctx))
		return UI_ASSETS_ERR_STATE;
	return UI_ASSETS_OK;
}

static u32 be32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u16 be16(const u8 *p) {
	return (u16)((p[0] << 8) | p[1]);
}

static const u8 *record(const u8 *index, u32 pos) {
	return index + (size_t)pos * PAK_RECORD_SIZE;
}

static bool idCharsValid(const char *id) {
	int i;
	for (i = 0; i < UI_ASSETS_ID_LEN; i++) {
		char c = id[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			return false;
	}
	return true;
}

#ifndef UI_ASSETS_HOST_BUILD
static u32 defaultNowMs(void) {
	return (u32)ticks_to_millisecs(SYS_GetSystemTime());
}
#endif

/* Binary search for an exact 6-char ID; -1 when absent. */
static s32 findExact(const uiAssetsCache_t *c, const char *id) {
	s32 lo = 0, hi = (s32)c->recordCount - 1;
	while (lo <= hi) {
		s32 mid = lo + (hi - lo) / 2;
		int cmp = memcmp(record(c->index, mid), id, UI_ASSETS_ID_LEN);
		if (cmp == 0)
			return mid;
		if (cmp < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* First record whose ID is >= the 4-char prefix; recordCount when none. */
static s32 lowerBound4(const uiAssetsCache_t *c, const char *id) {
	s32 lo = 0, hi = (s32)c->recordCount;
	while (lo < hi) {
		s32 mid = lo + (hi - lo) / 2;
		if (memcmp(record(c->index, mid), id, 4) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/*
 * Exact 6-char lookup first; the 4-char fallback applies ONLY to records
 * the pack explicitly marks universal. Returns the record position or -1;
 * *universal reports which path matched. Caller holds the lock (or is the
 * menu thread inside its own locked section).
 */
static s32 resolveRecord(const uiAssetsCache_t *c, const char *id,
                         bool *universal) {
	s32 pos = findExact(c, id);
	*universal = false;
	if (pos >= 0)
		return pos;
	for (pos = lowerBound4(c, id);
	     pos < (s32)c->recordCount &&
	     memcmp(record(c->index, pos), id, 4) == 0; pos++) {
		if (record(c->index, pos)[6] & PAK_FLAG_UNIVERSAL) {
			*universal = true;
			return pos;
		}
	}
	return -1;
}

static uiAssetSlot_t *slotForRecord(uiAssetsCache_t *c, s32 recordPos) {
	int i;
	for (i = 0; i < c->shape->slotCount; i++) {
		if (c->slots[i].state != SLOT_EMPTY && c->slots[i].recordPos == recordPos)
			return &c->slots[i];
	}
	return NULL;
}

/* Stale handles (evicted, cancelled, shut down) resolve to NULL here. */
static uiAssetSlot_t *slotFromHandle(uiAssetsCache_t *c,
                                     uiPosterHandle_t handle) {
	uiAssetSlot_t *slot;
	if (handle.slot >= c->shape->slotCount)
		return NULL;
	slot = &c->slots[handle.slot];
	if (slot->generation != handle.generation)
		return NULL;
	return slot;
}

/* Inside the critical section. Handles die via the EMPTY state; the
 * generation advances when the slot is next ASSIGNED, keeping exactly one
 * increment per ownership. */
static void evictSlot(const uiAssetsCache_t *c, uiAssetSlot_t *slot) {
	slot->state = SLOT_EMPTY;
	slot->recordPos = -1;
	slot->quarantineUntil = c->nowMs ? c->nowMs() + UI_ASSETS_EVICT_QUARANTINE_MS : 0;
}

static void invalidateAll(uiAssetsCache_t *c) {
	int i;
	c->loading.active = false;
	for (i = 0; i < c->shape->slotCount; i++)
		evictSlot(c, &c->slots[i]);
}

/*
 * Validate a staged header + index completely; nothing global is touched.
 * Version 1 packs are canonical: the generator emits one exact layout, so
 * validation enforces equality rather than tolerating variants.
 */
static s32 validateHeaderAndIndex(const uiAssetsShape_t *shape,
                                  const u8 *header, const u8 *index,
                                  u32 srcSize, u32 recordCount,
                                  u32 dataOffset, u32 fileLength) {
	u32 indexLength = recordCount * PAK_RECORD_SIZE;
	u32 crcStored, crcComputed;
	u8 headerCopy[PAK_HEADER_SIZE];
	const u8 *lastUniversal = NULL;
	u32 i;

	(void)srcSize;
	crcStored = be32(header + 0x08);
	memcpy(headerCopy, header, PAK_HEADER_SIZE);
	memset(headerCopy + 0x08, 0, 4);
	crcComputed = crc32(0, headerCopy, PAK_HEADER_SIZE);
	crcComputed = crc32(crcComputed, index, indexLength);
	if (crcComputed != crcStored)
		return UI_ASSETS_ERR_CRC;

	for (i = 0; i < recordCount; i++) {
		const u8 *rec = record(index, i);
		if (!idCharsValid((const char *)rec))
			return UI_ASSETS_ERR_FORMAT;
		if (i > 0 && memcmp(record(index, i - 1), rec, UI_ASSETS_ID_LEN) >= 0)
			return UI_ASSETS_ERR_FORMAT; /* unsorted or duplicate */
		if ((rec[6] & ~PAK_FLAGS_KNOWN) != 0 || rec[7] != 0)
			return UI_ASSETS_ERR_FORMAT;
		if (be32(rec + 0x08) != dataOffset + i * shape->recordBytes)
			return UI_ASSETS_ERR_FORMAT; /* overlap/gap/out-of-range */
		if (be32(rec + 0x0C) != shape->recordBytes)
			return UI_ASSETS_ERR_FORMAT;
		if (be32(rec + 0x1C) != 0)
			return UI_ASSETS_ERR_FORMAT;
		/* An ambiguous universal fallback (two universal records sharing a
		 * 4-char prefix) is a generator error; reject crafted packs too.
		 * Records are sorted, so same-prefix records are contiguous and
		 * comparing against the last universal record suffices. */
		if (rec[6] & PAK_FLAG_UNIVERSAL) {
			if (lastUniversal && memcmp(lastUniversal, rec, 4) == 0)
				return UI_ASSETS_ERR_FORMAT;
			lastUniversal = rec;
		}
	}
	(void)fileLength;
	return UI_ASSETS_OK;
}

/* Header field validation into locals; no global state touched. */
static s32 validateHeader(const uiAssetsShape_t *shape, const u8 *header,
                          u32 srcSize, u32 *outCount, u32 *outDataOffset,
                          u32 *outFileLength) {
	u32 recordCount, indexLength, dataOffset, fileLength;
	u64 expectedLength;
	u32 i;

	if (be32(header + 0x00) != PAK_MAGIC || be32(header + 0x04) != PAK_VERSION)
		return UI_ASSETS_ERR_FORMAT;
	recordCount = be32(header + 0x0C);
	if (recordCount < 1 || recordCount > UI_ASSETS_MAX_RECORDS)
		return UI_ASSETS_ERR_FORMAT;
	indexLength = recordCount * PAK_RECORD_SIZE;
	dataOffset = be32(header + 0x18);
	fileLength = be32(header + 0x1C);
	expectedLength = (u64)dataOffset + (u64)recordCount * shape->recordBytes;
	if (be32(header + 0x10) != PAK_HEADER_SIZE ||
	    be32(header + 0x14) != indexLength ||
	    dataOffset != PAK_HEADER_SIZE + indexLength ||
	    fileLength != expectedLength ||
	    fileLength != srcSize ||
	    be32(header + 0x20) != shape->recordBytes ||
	    be16(header + 0x24) != shape->canvasW ||
	    be16(header + 0x26) != shape->canvasH ||
	    be16(header + 0x28) != shape->contentW ||
	    be16(header + 0x2A) != shape->contentH ||
	    header[0x2C] != shape->mipLevels ||
	    header[0x2D] != GX_TF_CMPR ||
	    be16(header + 0x2E) != 0)
		return UI_ASSETS_ERR_FORMAT;
	for (i = 0x30; i < PAK_HEADER_SIZE; i++) {
		if (header[i] != 0)
			return UI_ASSETS_ERR_FORMAT;
	}
	*outCount = recordCount;
	*outDataOffset = dataOffset;
	*outFileLength = fileLength;
	return UI_ASSETS_OK;
}

static s32 cacheInit(uiAssetsCache_t *c, const uiAssetsSource_t *source,
                     const uiAssetsSync_t *sync) {
	const uiAssetsShape_t *shape = c->shape;
	u8 header[PAK_HEADER_SIZE];
	u8 *newIndex = NULL;
	u8 *oldIndex;
	uiAssetsSync_t requestedSync;
	u32 recordCount, dataOffset, fileLength, indexLength;
	u32 (*nowMs)(void);
	bool freshArena;
	s32 err;
	int i;

	err = validateSync(c, sync, &requestedSync);
	if (err != UI_ASSETS_OK)
		return err;
	if (!source || !source->read)
		return UI_ASSETS_ERR_STATE;
	if (c->packOpen) /* menu-thread state, menu-thread caller: race-free */
		return UI_ASSETS_ERR_STATE;

#ifdef UI_ASSETS_HOST_BUILD
	if (!source->nowMs)
		return UI_ASSETS_ERR_STATE; /* host tests must inject a clock */
	nowMs = source->nowMs;
#else
	nowMs = source->nowMs ? source->nowMs : defaultNowMs;
#endif

	/* Stage + validate everything in private memory; no publication and
	 * no lock until the pack is proven good. */
	if (source->size < PAK_HEADER_SIZE)
		return UI_ASSETS_ERR_FORMAT;
	if (source->read(source->ctx, 0, header, PAK_HEADER_SIZE) != PAK_HEADER_SIZE)
		return UI_ASSETS_ERR_IO;
	err = validateHeader(shape, header, source->size, &recordCount,
	                     &dataOffset, &fileLength);
	if (err != UI_ASSETS_OK)
		return err;
	indexLength = recordCount * PAK_RECORD_SIZE;
	newIndex = UIA_ALLOC(indexLength);
	if (!newIndex)
		return UI_ASSETS_ERR_NOMEM;
	if (source->read(source->ctx, PAK_HEADER_SIZE, newIndex, indexLength) !=
	    (s32)indexLength) {
		err = UI_ASSETS_ERR_IO;
		goto fail;
	}
	err = validateHeaderAndIndex(shape, header, newIndex, source->size,
	                             recordCount, dataOffset, fileLength);
	if (err != UI_ASSETS_OK)
		goto fail;

	if (!c->arenaReady) {
		c->arena = UIA_ALLOC_ALIGNED32(
			(u32)shape->slotCount * shape->recordBytes);
		if (!c->arena) {
			err = UI_ASSETS_ERR_NOMEM;
			goto fail;
		}
	}

	/* Bind once, immediately before the first publication. Cancel keeps the
	 * binding alive, so every re-Init must present the identical callbacks
	 * and context. Only DisposeAfterVideoStop clears it. */
	if (!c->syncBound) {
		c->sync = requestedSync;
		c->syncBound = true;
	}

	syncLock(c);
	c->src = *source;
	c->nowMs = nowMs;
	freshArena = !c->arenaReady;
	if (freshArena) {
		for (i = 0; i < shape->slotCount; i++)
			c->slots[i].data =
				c->arena + (size_t)i * shape->recordBytes;
		c->arenaReady = true;
	}
	oldIndex = c->index;
	c->index = newIndex;
	c->recordCount = recordCount;
	c->dataOffset = dataOffset;
	c->fileLength = fileLength;
	invalidateAll(c); /* new pack: every previous handle goes stale */
	if (freshArena) {
		/* No frame has drawn from a new arena: its first posters need not
		 * wait out a quarantine, so the first Poll after a request reads. */
		for (i = 0; i < shape->slotCount; i++)
			c->slots[i].quarantineUntil = nowMs();
	}
	c->packOpen = true;
	syncUnlock(c);

	if (oldIndex)
		UIA_FREE(oldIndex); /* detached above; freed outside the lock */
	return UI_ASSETS_OK;

fail:
	UIA_FREE(newIndex);
	return err;
}

static void cacheRequestWindow(uiAssetsCache_t *c, const char (*ids)[8],
                               int count, int selected) {
	struct {
		s32 recordPos;
		u8 distance;
	} want[UI_ASSETS_SLOTS];
	int slotCount = c->shape->slotCount;
	int wantCount = 0;
	int i, j;

	if (!ids)
		return;
	/* A cache takes at most one window entry per slot: the stills cache's
	 * caller passes the selected game and its neighbours, nothing more. */
	if (count < 0)
		count = 0;
	if (count > slotCount)
		count = slotCount;
	if (selected < 0)
		selected = 0;
	if (selected >= count && count > 0)
		selected = count - 1;

	syncLock(c);
	if (!c->packOpen) {
		syncUnlock(c);
		return;
	}

	/* Resolve requested IDs to unique pack records, keeping the smallest
	 * distance when two window entries share one universal record. */
	for (i = 0; i < count; i++) {
		bool universal;
		s32 pos;
		u8 distance = (u8)(i >= selected ? i - selected : selected - i);
		if (!idCharsValid(ids[i]))
			continue;
		pos = resolveRecord(c, ids[i], &universal);
		if (pos < 0)
			continue;
		for (j = 0; j < wantCount; j++) {
			if (want[j].recordPos == pos) {
				if (distance < want[j].distance)
					want[j].distance = distance;
				break;
			}
		}
		if (j == wantCount) {
			want[wantCount].recordPos = pos;
			want[wantCount].distance = distance;
			wantCount++;
		}
	}

	/* Keep slots already holding wanted records (including FAILED ones:
	 * a corrupt poster stays corrupt until the pack changes, so don't
	 * retry-loop). A slot whose record left the window keeps it, loaded or
	 * failed, for scrolling back, until a wanted record needs the slot; one
	 * still waiting for its read is let go. */
	c->requests++;
	for (i = 0; i < slotCount; i++) {
		uiAssetSlot_t *slot = &c->slots[i];
		if (slot->state == SLOT_EMPTY)
			continue;
		for (j = 0; j < wantCount; j++) {
			if (want[j].recordPos == slot->recordPos) {
				slot->distance = want[j].distance;
				slot->wanted = c->requests;
				want[j].recordPos = -1; /* satisfied */
				break;
			}
		}
		if (j == wantCount && slot->state != SLOT_READY &&
		    slot->state != SLOT_FAILED)
			evictSlot(c, slot);
	}

	/* Assign the remaining wanted records nearest first, each to an empty
	 * slot, else to the slot whose record left the window longest ago (the
	 * farther first). The window's own slots carry the newest stamp, and
	 * while a record still waits at least one slot is not the window's, so
	 * the oldest is never one of them. Assignment is the single point
	 * where a slot's generation advances. */
	for (;;) {
		int best = -1;
		uiAssetSlot_t *freeSlot = NULL;
		for (j = 0; j < wantCount; j++) {
			if (want[j].recordPos >= 0 &&
			    (best < 0 || want[j].distance < want[best].distance))
				best = j;
		}
		if (best < 0)
			break;
		for (i = 0; i < slotCount; i++) {
			uiAssetSlot_t *slot = &c->slots[i];
			if (slot->state == SLOT_EMPTY) {
				freeSlot = slot;
				break;
			}
			if (!freeSlot || (s32)(slot->wanted - freeSlot->wanted) < 0 ||
			    (slot->wanted == freeSlot->wanted &&
			     slot->distance > freeSlot->distance))
				freeSlot = slot;
		}
		if (freeSlot->state != SLOT_EMPTY)
			evictSlot(c, freeSlot); /* quarantines its texels */
		freeSlot->generation++;
		freeSlot->recordPos = want[best].recordPos;
		freeSlot->distance = want[best].distance;
		freeSlot->wanted = c->requests;
		freeSlot->state = SLOT_PENDING;
		want[best].recordPos = -1;
	}
	syncUnlock(c);
}

static bool cachePoll(uiAssetsCache_t *c, u32 maxBytes) {
	const uiAssetsShape_t *shape = c->shape;
	uiAssetJob_t job;
	uiAssetSlot_t *slot = NULL;
	bool pending = false, loadOk, finished;
	u32 now, take, crc = 0u;
	int i;

	/* Capture one bounded range under the video lock. A range in hand
	 * belongs only to its original slot generation, including across calls. */
	syncLock(c);
	if (!c->packOpen || !c->arenaReady) {
		syncUnlock(c);
		return false;
	}
	if (c->loading.active) {
		slot = &c->slots[c->loading.slotIdx];
		if (slot->state != SLOT_LOADING || slot->generation != c->loading.generation ||
		    slot->recordPos != c->loading.recordPos) {
			c->loading.active = false;
			slot = NULL;
		}
	}
	if (!c->loading.active) {
		now = c->nowMs();
		for (i = 0; i < shape->slotCount; i++) {
			uiAssetSlot_t *candidate = &c->slots[i];
			if (candidate->state != SLOT_PENDING) continue;
			pending = true;
			if ((s32)(now - candidate->quarantineUntil) < 0) continue;
			if (!slot || candidate->distance < slot->distance) slot = candidate;
		}
		if (!slot) {
			syncUnlock(c);
			return pending;
		}
		const u8 *rec = record(c->index, (u32)slot->recordPos);
		slot->state = SLOT_LOADING;
		c->loading = (uiAssetJob_t) {
			.active = true, .slotIdx = (int)(slot - c->slots),
			.generation = slot->generation, .recordPos = slot->recordPos,
			.dataOffset = be32(rec + 0x08), .expectedCrc = be32(rec + 0x10),
			.data = slot->data, .read = c->src.read, .readCtx = c->src.ctx,
			.fileLength = c->fileLength
		};
	}
	job = c->loading;
	take = shape->recordBytes - job.readBytes;
	if (take > maxBytes) take = maxBytes;
	syncUnlock(c);

	/* No lock across device I/O, CRC or cache flush. Partial records stay
	 * unpublished; the next idle frame can read another range or cancel. */
	loadOk = take > 0u && job.read != NULL &&
	         (u64)job.dataOffset + shape->recordBytes <= job.fileLength &&
	         job.read(job.readCtx, job.dataOffset + job.readBytes,
	                  job.data + job.readBytes, take) == (s32)take;
	finished = job.readBytes + take == shape->recordBytes;
	if (loadOk) {
		crc = crc32(job.crc, job.data + job.readBytes, take);
		if (finished) loadOk = crc == job.expectedCrc;
	}
	if (loadOk && finished) DCFlushRange(job.data, shape->recordBytes);

	syncLock(c);
	slot = &c->slots[job.slotIdx];
	if (c->packOpen && c->loading.active && slot->state == SLOT_LOADING &&
	    slot->generation == job.generation && slot->recordPos == job.recordPos) {
		if (loadOk && finished) {
			bool mipmapped = shape->mipLevels > 1;
			GX_InitTexObj(&slot->tex, slot->data, shape->canvasW,
			              shape->canvasH, GX_TF_CMPR, GX_CLAMP,
			              GX_CLAMP, mipmapped ? GX_TRUE : GX_FALSE);
			if (mipmapped)
				GX_InitTexObjLOD(&slot->tex, GX_LIN_MIP_LIN, GX_LINEAR,
				                 0.0f, (float)(shape->mipLevels - 1),
				                 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_1);
			slot->readyMs = c->nowMs();
			slot->state = SLOT_READY;
			c->loading.active = false;
		} else if (!loadOk) {
			slot->state = SLOT_FAILED;
			c->loading.active = false;
		} else {
			c->loading.readBytes += take;
			c->loading.crc = crc;
		}
	} else if (c->loading.active && c->loading.slotIdx == job.slotIdx &&
	           c->loading.generation == job.generation) {
		/* A window change retired this range while it was read. Do not
		 * leave a phantom in-flight job or disturb a newer generation. */
		c->loading.active = false;
	}
	pending = c->loading.active;
	for (i = 0; i < shape->slotCount && !pending; i++) {
		if (c->slots[i].state == SLOT_PENDING) pending = true;
	}
	syncUnlock(c);
	return pending;
}

static uiPosterResult_t cacheQuery(uiAssetsCache_t *c, const char *gameId,
                                   size_t gameIdLen, bool bnrAvailable,
                                   uiPosterHandle_t *out) {
	uiPosterResult_t base;
	uiAssetSlot_t *slot;
	bool universal;
	s32 pos;

	if (out) {
		out->slot = 0xFFFF;
		out->generation = 0;
		out->reserved = 0;
	}
	if (!gameId || gameIdLen < UI_ASSETS_ID_LEN || !idCharsValid(gameId) ||
	    !c->packOpen)
		return bnrAvailable ? UI_POSTER_USE_BNR : UI_POSTER_PROCEDURAL_CARD;

	pos = resolveRecord(c, gameId, &universal);
	if (pos < 0)
		return bnrAvailable ? UI_POSTER_USE_BNR : UI_POSTER_PROCEDURAL_CARD;
	base = universal ? UI_POSTER_UNIVERSAL : UI_POSTER_EXACT;

	slot = slotForRecord(c, pos);
	if (slot) {
		if (slot->state == SLOT_FAILED)
			return UI_POSTER_CORRUPT_OR_UNAVAILABLE;
		if (out) {
			out->slot = (u16)(slot - c->slots);
			out->generation = slot->generation;
		}
	}
	/* No slot yet: still EXACT/UNIVERSAL -- the caller renders its BNR/
	 * procedural stand-in until RequestWindow + Poll land the texels. */
	return base;
}

static GXTexObj *cachePeek(uiAssetsCache_t *c, uiPosterHandle_t handle) {
	uiAssetSlot_t *slot = slotFromHandle(c, handle);
	if (!slot || slot->state != SLOT_READY)
		return NULL;
	return &slot->tex;
}

static u32 cachePeekAgeMs(uiAssetsCache_t *c, uiPosterHandle_t handle) {
	uiAssetSlot_t *slot = slotFromHandle(c, handle);
	if (!slot || slot->state != SLOT_READY || !c->nowMs)
		return 0;
	return c->nowMs() - slot->readyMs;
}

static bool cacheDominantColor(const uiAssetsCache_t *c, const char *gameId,
                               size_t gameIdLen, u8 *r, u8 *g, u8 *b) {
	const u8 *rec;
	bool universal;
	s32 pos;

	if (!gameId || gameIdLen < UI_ASSETS_ID_LEN || !idCharsValid(gameId) ||
	    !c->packOpen)
		return false;
	pos = resolveRecord(c, gameId, &universal);
	if (pos < 0)
		return false;
	rec = record(c->index, (u32)pos);
	if (r)
		*r = rec[0x14];
	if (g)
		*g = rec[0x15];
	if (b)
		*b = rec[0x16];
	return true;
}

static void cacheCancel(uiAssetsCache_t *c) {
	u8 *oldIndex;
	if (!c->arenaReady && !c->packOpen)
		return;
	syncLock(c);
	invalidateAll(c);
	c->packOpen = false;
	c->recordCount = 0;
	oldIndex = c->index;
	c->index = NULL;
	memset(&c->src, 0, sizeof(c->src));
	syncUnlock(c);
	/* Unpublished inside the critical section above: no locked reader can
	 * still see it. Freed outside so the lock never covers free(). */
	if (oldIndex)
		UIA_FREE(oldIndex);
	/* Arena and nowMs stay: in-flight frames may still sample resident
	 * (stale but intact) texels, and re-Init reuses the allocation. */
}

static s32 cacheDispose(uiAssetsCache_t *c) {
	u8 *oldArena;
	int i;

	/* Disposal is deliberately lock-free: its caller has already stopped
	 * the video thread and may already have destroyed the video mutex. The
	 * live-mutex Cancel step must have unpublished and freed the index. */
	if (c->packOpen || c->index != NULL)
		return UI_ASSETS_ERR_STATE;
	oldArena = c->arena;
	c->arena = NULL;
	c->arenaReady = false;
	for (i = 0; i < c->shape->slotCount; i++) {
		c->slots[i].state = SLOT_EMPTY;
		c->slots[i].distance = 0;
		c->slots[i].recordPos = -1;
		c->slots[i].quarantineUntil = 0;
		c->slots[i].data = NULL;
		memset(&c->slots[i].tex, 0, sizeof(c->slots[i].tex));
	}
	memset(&c->src, 0, sizeof(c->src));
	c->recordCount = 0;
	c->dataOffset = 0;
	c->fileLength = 0;
	c->nowMs = NULL;
	memset(&c->sync, 0, sizeof(c->sync));
	c->syncBound = false;
	if (oldArena)
		UIA_FREE(oldArena);
	return UI_ASSETS_OK;
}

static u32 cacheFootprint(const uiAssetsCache_t *c) {
	u32 total = 0;
	if (c->arenaReady)
		total += (u32)c->shape->slotCount * c->shape->recordBytes;
	if (c->index)
		total += c->recordCount * PAK_RECORD_SIZE;
	return total;
}

/* ---- posters ---- */

s32 UIAssets_Init(const uiAssetsSource_t *source, const uiAssetsSync_t *sync) {
	return cacheInit(&posters, source, sync);
}

bool UIAssets_Ready(void) {
	return posters.packOpen;
}

void UIAssets_RequestWindow(const char (*ids)[8], int count, int selected) {
	cacheRequestWindow(&posters, ids, count, selected);
}

bool UIAssets_Poll(void) {
	return cachePoll(&posters, UI_ASSETS_POSTER_BYTES);
}

bool UIAssets_PollStep(void) {
	return cachePoll(&posters, UI_ASSETS_READ_BYTES);
}

uiPosterResult_t UIAssets_Query(const char *gameId, size_t gameIdLen,
                                bool bnrAvailable, uiPosterHandle_t *out) {
	return cacheQuery(&posters, gameId, gameIdLen, bnrAvailable, out);
}

GXTexObj *UIAssets_Peek(uiPosterHandle_t handle) {
	return cachePeek(&posters, handle);
}

u32 UIAssets_PeekAgeMs(uiPosterHandle_t handle) {
	return cachePeekAgeMs(&posters, handle);
}

bool UIAssets_DominantColor(const char *gameId, size_t gameIdLen,
                            u8 *r, u8 *g, u8 *b) {
	return cacheDominantColor(&posters, gameId, gameIdLen, r, g, b);
}

void UIAssets_CancelForDeviceChange(void) {
	cacheCancel(&posters);
}

s32 UIAssets_DisposeAfterVideoStop(void) {
	return cacheDispose(&posters);
}

u32 UIAssets_MemoryFootprint(void) {
	return cacheFootprint(&posters);
}

/* A menu tick has one shared read budget, not one budget per open pack.
 * Keep the original per-pack APIs for entry priming and tools. */
static bool cachePending(uiAssetsCache_t *c)
{
	bool pending = false;

	syncLock(c);
	if(c->packOpen && c->arenaReady) {
		pending = c->loading.active;
		for(int i = 0; i < c->shape->slotCount && !pending; i++) {
			pending = c->slots[i].state == SLOT_PENDING;
		}
	}
	syncUnlock(c);
	return pending;
}

bool UIAssets_PollVisible(bool includeStills, bool includeDiscs)
{
	/* Menu-thread only. Round-robin among visible packs so a poster and
	 * the selected still/disc both progress. Hidden packs retain their
	 * partial records but do no I/O until their layout is used again. */
	static unsigned next;
	uiAssetsCache_t *visible[] = {&posters,
		includeStills ? &stills : NULL, includeDiscs ? &discs : NULL};
	const unsigned count = sizeof(visible) / sizeof(visible[0]);
	bool more = false;

	for(unsigned step = 0; step < count; step++) {
		unsigned index = (next + step) % count;
		if(visible[index] && cachePending(visible[index])) {
			cachePoll(visible[index], UI_ASSETS_READ_BYTES);
			next = (index + 1) % count;
			break;
		}
	}
	for(unsigned i = 0; i < count; i++) {
		if(visible[i]) more = cachePending(visible[i]) || more;
	}
	return more;
}

/* ---- stills ---- */

s32 UIStills_Init(const uiAssetsSource_t *source, const uiAssetsSync_t *sync) {
	return cacheInit(&stills, source, sync);
}

bool UIStills_Ready(void) {
	return stills.packOpen;
}

void UIStills_RequestWindow(const char (*ids)[8], int count, int selected) {
	cacheRequestWindow(&stills, ids, count, selected);
}

bool UIStills_Poll(void) {
	return cachePoll(&stills, UI_STILLS_BYTES);
}

bool UIStills_PollStep(void) {
	return cachePoll(&stills, UI_ASSETS_READ_BYTES);
}

uiPosterResult_t UIStills_Query(const char *gameId, size_t gameIdLen,
                                bool bnrAvailable, uiPosterHandle_t *out) {
	return cacheQuery(&stills, gameId, gameIdLen, bnrAvailable, out);
}

GXTexObj *UIStills_Peek(uiPosterHandle_t handle) {
	return cachePeek(&stills, handle);
}

u32 UIStills_PeekAgeMs(uiPosterHandle_t handle) {
	return cachePeekAgeMs(&stills, handle);
}

bool UIStills_DominantColor(const char *gameId, size_t gameIdLen,
                            u8 *r, u8 *g, u8 *b) {
	return cacheDominantColor(&stills, gameId, gameIdLen, r, g, b);
}

void UIStills_CancelForDeviceChange(void) {
	cacheCancel(&stills);
}

s32 UIStills_DisposeAfterVideoStop(void) {
	return cacheDispose(&stills);
}

u32 UIStills_MemoryFootprint(void) {
	return cacheFootprint(&stills);
}

/* ---- discs ---- */

s32 UIDiscs_Init(const uiAssetsSource_t *source, const uiAssetsSync_t *sync) {
	return cacheInit(&discs, source, sync);
}

bool UIDiscs_Ready(void) {
	return discs.packOpen;
}

void UIDiscs_RequestWindow(const char (*ids)[8], int count, int selected) {
	cacheRequestWindow(&discs, ids, count, selected);
}

bool UIDiscs_Poll(void) {
	return cachePoll(&discs, UI_DISCS_BYTES);
}

bool UIDiscs_PollStep(void) {
	return cachePoll(&discs, UI_ASSETS_READ_BYTES);
}

uiPosterResult_t UIDiscs_Query(const char *gameId, size_t gameIdLen,
                               bool bnrAvailable, uiPosterHandle_t *out) {
	return cacheQuery(&discs, gameId, gameIdLen, bnrAvailable, out);
}

GXTexObj *UIDiscs_Peek(uiPosterHandle_t handle) {
	return cachePeek(&discs, handle);
}

u32 UIDiscs_PeekAgeMs(uiPosterHandle_t handle) {
	return cachePeekAgeMs(&discs, handle);
}

void UIDiscs_CancelForDeviceChange(void) {
	cacheCancel(&discs);
}

s32 UIDiscs_DisposeAfterVideoStop(void) {
	return cacheDispose(&discs);
}

u32 UIDiscs_MemoryFootprint(void) {
	return cacheFootprint(&discs);
}

#ifdef UI_ASSETS_HOST_BUILD

void UIAssetsTest_ForceGeneration(u16 slot, u32 generation) {
	if (slot < UI_ASSETS_SLOTS)
		posters.slots[slot].generation = generation;
}

#else

static s32 fileHandleRead(void *ctx, u32 offset, void *dst, u32 len) {
	file_handle *file = (file_handle *)ctx;
	if (file->device->seekFile(file, offset, DEVICE_HANDLER_SEEK_SET) != offset)
		return UI_ASSETS_ERR_IO;
	return file->device->readFile(file, dst, len);
}

s32 UIAssets_SourceFromFileHandle(file_handle *file, uiAssetsSource_t *out) {
	if (!file || !file->device || !file->device->readFile ||
	    !file->device->seekFile || !out)
		return UI_ASSETS_ERR_STATE;
	if (!file->size && file->device->statFile &&
	    file->device->statFile(file) != 0)
		return UI_ASSETS_ERR_IO;
	if (!file->size)
		return UI_ASSETS_ERR_FORMAT;
	out->read = fileHandleRead;
	out->size = file->size;
	out->ctx = file;
	out->nowMs = NULL;
	return UI_ASSETS_OK;
}

static void syncMutexLock(void *ctx) {
	LWP_MutexLock((mutex_t)(uintptr_t)ctx);
}

static void syncMutexUnlock(void *ctx) {
	LWP_MutexUnlock((mutex_t)(uintptr_t)ctx);
}

void UIAssets_SyncFromMutex(mutex_t mutex, uiAssetsSync_t *out) {
	out->lock = syncMutexLock;
	out->unlock = syncMutexUnlock;
	out->ctx = (void *)(uintptr_t)mutex;
}

#endif
