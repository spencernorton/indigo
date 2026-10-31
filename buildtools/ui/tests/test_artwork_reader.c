#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "ui_assets.h"
#include "ui_artwork_reader.h"
#include <ogc/lwp.h>
#include <ogc/semaphore.h>

static pthread_t menu, worker;
static pthread_mutex_t semLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t semChanged = PTHREAD_COND_INITIALIZER;
static unsigned semCount, stacks, creates, semaphores;
static int failStack, failSem, failThread;
static bool threadLive;

void *artworkTestAlloc(size_t alignment, size_t size)
{
	void *ptr = NULL;
	assert(size == 16u * 1024u && alignment == 32u);
	if(failStack) return NULL;
	assert(posix_memalign(&ptr, alignment, size) == 0);
	stacks++;
	return ptr;
}

void artworkTestFree(void *ptr) { assert(ptr && stacks); stacks--; free(ptr); }

s32 LWP_SemInit(sem_t *sem, u32 start, u32 max)
{
	assert(!semaphores && max == 1u);
	if(failSem) return -1;
	*sem = 1u;
	semCount = start;
	semaphores++;
	return 0;
}

s32 LWP_SemWait(sem_t sem)
{
	assert(sem == 1u);
	pthread_mutex_lock(&semLock);
	while(!semCount) pthread_cond_wait(&semChanged, &semLock);
	semCount--;
	pthread_mutex_unlock(&semLock);
	return 0;
}

s32 LWP_SemPost(sem_t sem)
{
	assert(sem == 1u);
	pthread_mutex_lock(&semLock);
	semCount = 1u;
	pthread_cond_signal(&semChanged);
	pthread_mutex_unlock(&semLock);
	return 0;
}

s32 LWP_SemDestroy(sem_t sem) { assert(sem == 1u && semaphores); semaphores--; return 0; }

s32 LWP_CreateThread(lwp_t *thread, void *(*entry)(void *), void *arg,
	void *stack, u32 stackSize, u8 priority)
{
	assert(!threadLive && stack && stackSize == 16u * 1024u);
	assert(priority < LWP_PRIO_NORMAL);
	creates++;
	if(failThread) return -1;
	*thread = 1u;
	threadLive = true;
	return pthread_create(&worker, NULL, entry, arg);
}

s32 LWP_JoinThread(lwp_t thread, void **value)
{
	assert(thread == 1u && threadLive && !pthread_equal(pthread_self(), worker));
	assert(pthread_join(worker, value) == 0);
	threadLive = false;
	return 0;
}

static pthread_mutex_t videoLock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool videoHeld;
static atomic_uint allocations, activeReads, completedReads;
static atomic_uint readDelayUs;
static pthread_mutex_t ioLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ioChanged = PTHREAD_COND_INITIALIZER;
static bool gate, entered;

static u64 nowUs(void)
{
	struct timespec ts;
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (u64)ts.tv_sec * 1000000u + (u64)ts.tv_nsec / 1000u;
}

static u32 nowMs(void) { return (u32)(nowUs() / 1000u); }
static void lock(void *ctx) { (void)ctx; assert(!videoHeld); pthread_mutex_lock(&videoLock); videoHeld = true; }
static void unlock(void *ctx) { (void)ctx; assert(videoHeld); videoHeld = false; pthread_mutex_unlock(&videoLock); }
static uiAssetsSync_t syncPair = {lock, unlock, NULL};
void *uiAssetsHostAlloc(u32 size) { atomic_fetch_add(&allocations, 1u); return malloc(size); }
void *uiAssetsHostAllocAligned32(u32 size)
{
	void *ptr = NULL;
	assert(posix_memalign(&ptr, 32u, size) == 0);
	atomic_fetch_add(&allocations, 1u);
	return ptr;
}
void uiAssetsHostFree(void *ptr) { assert(!atomic_load(&activeReads)); atomic_fetch_sub(&allocations, 1u); free(ptr); }
void uiAssetsHostFlush(void *ptr, u32 size) { assert(!videoHeld && ptr && size); }

typedef struct { u8 *data; u32 size, bytes, count; atomic_uint reads; bool threadSafe; } pack_t;
static void be16(u8 *p, u16 n) { p[0] = (u8)(n >> 8); p[1] = (u8)n; }
static void be32(u8 *p, u32 n) { be16(p, (u16)(n >> 16)); be16(p + 2, (u16)n); }
static void gameId(char id[8], unsigned index) { assert(index < 100u); snprintf(id, 8, "G%05u", index); }

static pack_t makePack(unsigned kind, u32 count)
{
	const u32 bytes[] = {UI_ASSETS_POSTER_BYTES, UI_STILLS_BYTES, UI_DISCS_BYTES};
	const u16 width[] = {UI_ASSETS_CANVAS_W, UI_STILLS_W, UI_DISCS_SIZE};
	const u16 height[] = {UI_ASSETS_CANVAS_H, UI_STILLS_H, UI_DISCS_SIZE};
	pack_t p = {.bytes=bytes[kind], .count=count};
	u32 offset = 64u + count * 32u;
	p.size = offset + count * p.bytes;
	p.data = calloc(1u, p.size);
	assert(p.data);
	be32(p.data, 0x5357504bu); be32(p.data + 4, 1u); be32(p.data + 12, count);
	be32(p.data + 16, 64u); be32(p.data + 20, count * 32u);
	be32(p.data + 24, offset); be32(p.data + 28, p.size); be32(p.data + 32, p.bytes);
	be16(p.data + 36, width[kind]); be16(p.data + 38, height[kind]);
	be16(p.data + 40, kind ? width[kind] : UI_ASSETS_CONTENT_W);
	be16(p.data + 42, kind ? height[kind] : UI_ASSETS_CONTENT_H);
	p.data[44] = kind ? 1u : UI_ASSETS_MIP_LEVELS; p.data[45] = 14u;
	for(u32 i = 0; i < count; i++) {
		u8 *rec = p.data + 64u + i * 32u;
		char id[8]; gameId(id, i); memcpy(rec, id, 6u);
		memset(p.data + offset, (int)(i + 1u), p.bytes);
		be32(rec + 8, offset); be32(rec + 12, p.bytes);
		be32(rec + 16, (u32)crc32(0u, p.data + offset, p.bytes));
		offset += p.bytes;
	}
	be32(p.data + 8, (u32)crc32(0u, p.data, 64u + count * 32u));
	return p;
}

static s32 readPack(void *ctx, u32 offset, void *dst, u32 len)
{
	pack_t *p = ctx;
	assert(!videoHeld && (u64)offset + len <= p->size);
	if(offset >= 64u + p->count * 32u) {
		assert(len <= UI_ASSETS_READ_BYTES);
		assert(p->threadSafe || pthread_equal(pthread_self(), menu));
		assert(atomic_fetch_add(&activeReads, 1u) == 0u);
		pthread_mutex_lock(&ioLock);
		entered = true;
		pthread_cond_broadcast(&ioChanged);
		while(gate) pthread_cond_wait(&ioChanged, &ioLock);
		pthread_mutex_unlock(&ioLock);
		usleep(atomic_load(&readDelayUs));
		memcpy(dst, p->data + offset, len);
		atomic_fetch_add(&p->reads, 1u);
		atomic_fetch_add(&completedReads, 1u);
		assert(atomic_fetch_sub(&activeReads, 1u) == 1u);
	} else {
		assert(!atomic_load(&activeReads));
		memcpy(dst, p->data + offset, len);
	}
	return (s32)len;
}

static void openPack(pack_t *p, unsigned kind, bool safe, bool synchronized)
{
	uiAssetsSource_t src = {readPack, p->size, p, nowMs, safe};
	p->threadSafe = safe;
	s32 (*init[])(const uiAssetsSource_t *, const uiAssetsSync_t *) = {
		UIAssets_Init, UIStills_Init, UIDiscs_Init};
	assert(init[kind](&src, synchronized ? &syncPair : NULL) == UI_ASSETS_OK);
	char ids[25][8] = {{0}};
	for(u32 i = 0; i < p->count && i < 25u; i++) gameId(ids[i], i);
	void (*request[])(const char (*)[8], int, int) = {
		UIAssets_RequestWindow, UIStills_RequestWindow, UIDiscs_RequestWindow};
	request[kind](ids, (int)p->count, 0);
}

static bool ready(pack_t *p, unsigned kind, unsigned index)
{
	uiPosterHandle_t handle;
	uiPosterResult_t (*query[])(const char *, size_t, bool, uiPosterHandle_t *) = {
		UIAssets_Query, UIStills_Query, UIDiscs_Query};
	GXTexObj *(*peek[])(uiPosterHandle_t) = {UIAssets_Peek, UIStills_Peek, UIDiscs_Peek};
	char id[8]; gameId(id, index);
	lock(NULL);
	query[kind](id, 6u, false, &handle);
	GXTexObj *texture = peek[kind](handle);
	if(texture) assert(!memcmp(texture->data,
		p->data + 64u + p->count * 32u + index * p->bytes, p->bytes));
	unlock(NULL);
	return texture != NULL;
}

static void awaitReady(pack_t *p, unsigned kind, unsigned index)
{
	u64 deadline = nowUs() + 3000000u;
	while(!ready(p, kind, index) && nowUs() < deadline) usleep(500);
	assert(ready(p, kind, index));
}

static void closePacks(void)
{
	UIAssets_CancelForDeviceChange(); UIStills_CancelForDeviceChange(); UIDiscs_CancelForDeviceChange();
	assert(!UIArtworkReader_Running() && !atomic_load(&activeReads));
	assert(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_OK);
	assert(UIStills_DisposeAfterVideoStop() == UI_ASSETS_OK);
	assert(UIDiscs_DisposeAfterVideoStop() == UI_ASSETS_OK);
	assert(!atomic_load(&allocations) && !stacks && !semaphores && !threadLive);
}

static void independentAndHidden(void)
{
	pack_t p = makePack(0, 3), s = makePack(1, 1), d = makePack(2, 1);
	openPack(&p, 0, true, true); openPack(&s, 1, true, true); openPack(&d, 2, true, true);
	assert(UIAssets_PollVisible(true, false));
	/* One wake, no more menu ticks: complete more than one 16 KiB range. */
	awaitReady(&p, 0, 2); awaitReady(&s, 1, 0);
	assert(atomic_load(&p.reads) == 9u && !atomic_load(&d.reads));
	unsigned stillReads = atomic_load(&s.reads);
	UIAssets_PollVisible(false, true);
	awaitReady(&d, 2, 0);
	assert(atomic_load(&s.reads) == stillReads);
	closePacks(); free(p.data); free(s.data); free(d.data);
}

static void fallback(bool safe, bool synchronized, unsigned failure)
{
	pack_t p = makePack(0, 3);
	openPack(&p, 0, safe, synchronized);
	failStack = failure == 1u; failSem = failure == 2u; failThread = failure == 3u;
	unsigned beforeCreates = creates;
	for(unsigned i = 0; i < 4u; i++) {
		unsigned reads = atomic_load(&p.reads);
		UIAssets_PollVisible(false, false);
		assert(!UIArtworkReader_Running() && atomic_load(&p.reads) == reads + 1u);
	}
	assert(creates - beforeCreates <= 1u);
	failStack = failSem = failThread = 0;
	UIAssets_PauseReads();
	if(safe && synchronized) {
		UIAssets_PollVisible(false, false);
		awaitReady(&p, 0, 2);
	}
	closePacks(); free(p.data);
}

static void *releaseRead(void *unused)
{
	(void)unused;
	usleep(10000);
	pthread_mutex_lock(&ioLock); gate = false;
	pthread_cond_broadcast(&ioChanged); pthread_mutex_unlock(&ioLock);
	return NULL;
}

static void drain(unsigned operation)
{
	pack_t p = makePack(0, 6);
	pack_t other = makePack(1, 1);
	openPack(&p, 0, true, true);
	pthread_mutex_lock(&ioLock); gate = true; entered = false; pthread_mutex_unlock(&ioLock);
	UIAssets_PollVisible(false, false);
	pthread_mutex_lock(&ioLock);
	while(!entered) pthread_cond_wait(&ioChanged, &ioLock);
	pthread_mutex_unlock(&ioLock);
	unsigned beforeCreates = creates;
	/* Frame polling never joins a running read, allocates or starts another. */
	for(unsigned i = 0; i < 1000u; i++) UIAssets_PollVisible(false, false);
	assert(creates == beforeCreates && atomic_load(&activeReads) == 1u);
	pthread_t release;
	assert(pthread_create(&release, NULL, releaseRead, NULL) == 0);
	char ids[3][8] = {"G00003", "G00004", "G00005"};
	if(operation == 0u) UIAssets_PauseReads();
	if(operation == 1u) UIAssets_RequestWindow(ids, 3, 0);
	if(operation == 2u) UIAssets_CancelForDeviceChange();
	if(operation == 3u) assert(UIAssets_DisposeAfterVideoStop() == UI_ASSETS_ERR_STATE);
	if(operation == 4u) openPack(&other, 1, true, true);
	if(operation == 5u) UIAssets_PollStep();
	assert(!atomic_load(&activeReads) && !UIArtworkReader_Running());
	assert(pthread_join(release, NULL) == 0);
	unsigned reads = atomic_load(&p.reads);
	usleep(5000);
	assert(reads == atomic_load(&p.reads) && reads == (operation == 5u ? 2u : 1u));
	assert(!ready(&p, 0, 0));
	if(operation < 2u) {
		UIAssets_PollVisible(false, false);
		awaitReady(&p, 0, operation ? 5u : 0u);
	}
	closePacks(); free(p.data); free(other.data);
}

static atomic_bool videoStop;
static void *observeTextures(void *context)
{
	pack_t *p = context;
	while(!atomic_load(&videoStop)) {
		for(unsigned i = 0; i < p->count; i++) ready(p, 0, i);
		usleep(100);
	}
	return NULL;
}

static void churn(void)
{
	pack_t p = makePack(0, 20);
	openPack(&p, 0, true, true);
	atomic_store(&videoStop, false);
	atomic_store(&readDelayUs, 100u);
	pthread_t video;
	assert(pthread_create(&video, NULL, observeTextures, &p) == 0);
	for(unsigned pass = 0; pass < 80u; pass++) {
		char ids[3][8];
		for(unsigned i = 0; i < 3u; i++) gameId(ids[i], (pass + i) % 20u);
		UIAssets_RequestWindow(ids, 3, 1);
		UIAssets_PollVisible(false, false);
		usleep(1000);
	}
	awaitReady(&p, 0, 1u);
	UIAssets_PauseReads();
	atomic_store(&videoStop, true);
	assert(pthread_join(video, NULL) == 0);
	atomic_store(&readDelayUs, 0u);
	closePacks(); free(p.data);
}

static void corrupt(void)
{
	pack_t p = makePack(0, 1);
	p.data[p.size - 1u] ^= 1u;
	openPack(&p, 0, true, true);
	unsigned before = atomic_load(&completedReads);
	UIAssets_PollVisible(false, false);
	u64 deadline = nowUs() + 3000000u;
	while(atomic_load(&completedReads) < before + 3u && nowUs() < deadline) usleep(500);
	UIAssets_PauseReads();
	uiPosterHandle_t handle;
	lock(NULL);
	assert(UIAssets_Query("G00000", 6u, false, &handle) == UI_POSTER_CORRUPT_OR_UNAVAILABLE);
	assert(!UIAssets_Peek(handle)); unlock(NULL);
	closePacks(); free(p.data);
}

static void *holdForHandoff(void *unused)
{
	(void)unused;
	/* Let the menu enter Stop while a device read is still blocked. */
	usleep(1000);
	UIArtworkReader_Block();
	return NULL;
}

static void handoff(void)
{
	pack_t p = makePack(0, 3);
	openPack(&p, 0, true, true);
	pthread_mutex_lock(&ioLock); gate = true; entered = false; pthread_mutex_unlock(&ioLock);
	UIAssets_PollVisible(false, false);
	pthread_mutex_lock(&ioLock);
	while(!entered) pthread_cond_wait(&ioChanged, &ioLock);
	pthread_mutex_unlock(&ioLock);
	pthread_t release, upload;
	assert(pthread_create(&upload, NULL, holdForHandoff, NULL) == 0);
	assert(pthread_create(&release, NULL, releaseRead, NULL) == 0);
	UIAssets_PauseReads();
	assert(pthread_join(upload, NULL) == 0 && pthread_join(release, NULL) == 0);
	assert(UIArtworkReader_Blocked() && !atomic_load(&activeReads));
	unsigned reads = atomic_load(&p.reads), starts = creates;
	for(unsigned i = 0; i < 100u; i++) {
		UIAssets_PollVisible(false, false); UIAssets_PollStep(); UIAssets_PauseReads();
	}
	assert(reads == atomic_load(&p.reads) && starts == creates);
	/* A rejected upload restores the same partial record before the menu
	 * resumes. Neither its arena nor its source was retired. */
	UIArtworkReader_Unblock();
	UIAssets_PollVisible(false, false);
	awaitReady(&p, 0, 2u);
	closePacks(); free(p.data);
}

static void benchmark(unsigned hz, bool background)
{
	pack_t p = makePack(0, 15);
	openPack(&p, 0, background, true);
	atomic_store(&readDelayUs, 6000u);
	u64 start = nowUs(), deadline = start, menuMax = 0;
	unsigned polls = 0;
	do {
		u64 tick = nowUs(); UIAssets_PollVisible(false, false);
		u64 elapsed = nowUs() - tick; if(elapsed > menuMax) menuMax = elapsed;
		polls++;
		deadline += 1000000u / hz;
		if(deadline > nowUs()) usleep((unsigned)(deadline - nowUs()));
	} while(!ready(&p, 0, 14));
	u64 elapsed = nowUs() - start;
	UIAssets_PauseReads();
	printf("{\"hz\":%u,\"background\":%s,\"bytes\":%u,\"elapsed_us\":%llu,"
		"\"menu_poll_max_us\":%llu,\"menu_polls\":%u,\"read_latency_us\":6000}\n",
		hz, background ? "true" : "false", 15u * UI_ASSETS_POSTER_BYTES,
		(unsigned long long)elapsed, (unsigned long long)menuMax, polls);
	atomic_store(&readDelayUs, 0u);
	closePacks(); free(p.data);
}

int main(int argc, char **argv)
{
	(void)argv;
	menu = pthread_self();
	independentAndHidden();
	fallback(false, true, 0); fallback(true, false, 0);
	for(unsigned i = 1; i <= 3; i++) fallback(true, true, i);
	for(unsigned i = 0; i < 6; i++) drain(i);
	churn();
	corrupt();
	handoff();
	if(argc > 1) for(unsigned hz = 50; hz <= 60; hz += 10) {
		benchmark(hz, false); benchmark(hz, true);
	}
	puts("artwork reader: background progress, visibility, CRC, fallback, resource failure, coalesced wakes and drain barriers PASS");
	return 0;
}
