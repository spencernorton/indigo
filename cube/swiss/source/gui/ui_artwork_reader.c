#include "ui_artwork_reader.h"

#include <malloc.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>

/* As with folder pictures and file metadata, menu input and video preempt
 * these reads. The extra 1 ms sleep also shares idle CPU time on passive
 * adapters whose device reads do not sleep for DMA. */
#define ARTWORK_STACK_BYTES (16u * 1024u)
#define ARTWORK_PRIORITY (LWP_PRIO_NORMAL - 1)

static lwp_t readerThread = LWP_THREAD_NULL;
static sem_t readerWake;
static void *readerStack;
static atomic_uint readerStopRequested;
static atomic_flag readerControl = ATOMIC_FLAG_INIT;
/* 0: menu controls may enter, 1: handoff draining, 2: drained and held. */
static atomic_uint readerBlocked;
static bool readerUnavailable;
static bool (*readerStep)(void *);
static void *readerContext;

static bool controlLock(void)
{
	while(!atomic_load_explicit(&readerBlocked, memory_order_acquire)) {
		if(!atomic_flag_test_and_set_explicit(&readerControl, memory_order_acquire)) {
			if(!atomic_load_explicit(&readerBlocked, memory_order_acquire)) return true;
			atomic_flag_clear_explicit(&readerControl, memory_order_release);
			break;
		}
		usleep(1000);
	}
	return false;
}

static void controlUnlock(void)
{
	atomic_flag_clear_explicit(&readerControl, memory_order_release);
}

static void *readerMain(void *unused)
{
	(void)unused;
	while(!atomic_load_explicit(&readerStopRequested, memory_order_relaxed)) {
		LWP_SemWait(readerWake);
		while(!atomic_load_explicit(&readerStopRequested, memory_order_relaxed)) {
			if(!readerStep(readerContext)) break;
			usleep(1000);
		}
	}
	return NULL;
}

bool UIArtworkReader_Running(void)
{
	if(!controlLock()) return false;
	bool running = readerThread != LWP_THREAD_NULL;
	controlUnlock();
	return running;
}

bool UIArtworkReader_Start(bool (*step)(void *), void *context)
{
	if(!controlLock()) return false;
	if(readerThread != LWP_THREAD_NULL) {
		controlUnlock();
		return true;
	}
	if(readerUnavailable || step == NULL) {
		controlUnlock();
		return false;
	}
	readerStack = memalign(32, ARTWORK_STACK_BYTES);
	if(readerStack == NULL) goto fail;
	if(LWP_SemInit(&readerWake, 0, 1) < 0) goto fail_stack;
	readerStep = step;
	readerContext = context;
	atomic_store_explicit(&readerStopRequested, 0u, memory_order_relaxed);
	if(LWP_CreateThread(&readerThread, readerMain, NULL, readerStack,
		ARTWORK_STACK_BYTES, ARTWORK_PRIORITY) < 0) {
		readerThread = LWP_THREAD_NULL;
		LWP_SemDestroy(readerWake);
		goto fail_stack;
	}
	controlUnlock();
	return true;

fail_stack:
	free(readerStack);
	readerStack = NULL;
fail:
	/* Retry only after the next pause/window/device change, not every frame
	 * while the menu is already short of memory or thread resources. */
	readerUnavailable = true;
	controlUnlock();
	return false;
}

void UIArtworkReader_Wake(void)
{
	/* A full binary semaphore already promises a wake; posts coalesce. */
	if(!controlLock()) return;
	if(readerThread != LWP_THREAD_NULL) LWP_SemPost(readerWake);
	controlUnlock();
}

static void stopReader(void)
{
	if(readerThread != LWP_THREAD_NULL) {
		atomic_store_explicit(&readerStopRequested, 1u, memory_order_relaxed);
		LWP_SemPost(readerWake);
		LWP_JoinThread(readerThread, NULL);
		readerThread = LWP_THREAD_NULL;
		LWP_SemDestroy(readerWake);
		free(readerStack);
		readerStack = NULL;
	}
	readerUnavailable = false;
}

void UIArtworkReader_Stop(void)
{
	if(controlLock()) {
		stopReader();
		controlUnlock();
	} else {
		/* A handoff owns cleanup; do not mutate a cache until it has drained.
		 * Once held, no menu control can acquire a lock and then be suspended
		 * with it held during DrawShutdown. */
		while(atomic_load_explicit(&readerBlocked, memory_order_acquire) == 1u)
			usleep(1000);
	}
}

bool UIArtworkReader_Blocked(void)
{
	return atomic_load_explicit(&readerBlocked, memory_order_acquire) != 0u;
}

void UIArtworkReader_Block(void)
{
	unsigned expected = 0u;
	if(!atomic_compare_exchange_strong_explicit(&readerBlocked, &expected, 1u,
		memory_order_acq_rel, memory_order_acquire)) {
		while(atomic_load_explicit(&readerBlocked, memory_order_acquire) == 1u)
			usleep(1000);
		return;
	}
	while(atomic_flag_test_and_set_explicit(&readerControl, memory_order_acquire))
		usleep(1000);
	stopReader();
	atomic_store_explicit(&readerBlocked, 2u, memory_order_release);
	/* Keep control held until Unblock. The menu may now be suspended even
	 * inside a reader control call without owning this lock or its cleanup. */
}

void UIArtworkReader_Unblock(void)
{
	if(atomic_load_explicit(&readerBlocked, memory_order_acquire) == 2u) {
		controlUnlock();
		atomic_store_explicit(&readerBlocked, 0u, memory_order_release);
	}
}
