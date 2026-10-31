#ifndef UI_ARTWORK_READER_H
#define UI_ARTWORK_READER_H

#include <stdbool.h>

/* One menu-owned worker for bounded pack reads. The callback and its context
 * stay unchanged until Stop has joined the worker. Start/Wake/Stop are never
 * called by the callback or while the video mutex is held. */
#if defined(UI_ASSETS_HOST_BUILD) && !defined(UI_ASSETS_HOST_THREADS)
/* The ordinary cache harness is deliberately single-threaded. */
static inline bool UIArtworkReader_Start(bool (*step)(void *), void *context)
{
	(void)step;
	(void)context;
	return false;
}
static inline void UIArtworkReader_Wake(void) {}
static inline void UIArtworkReader_Stop(void) {}
static inline bool UIArtworkReader_Running(void) { return false; }
static inline void UIArtworkReader_Block(void) {}
static inline void UIArtworkReader_Unblock(void) {}
static inline bool UIArtworkReader_Blocked(void) { return false; }
#else
bool UIArtworkReader_Start(bool (*step)(void *), void *context);
void UIArtworkReader_Wake(void);
/* Finish at most the current bounded read, then join before its source,
 * index, slot ownership, arena or callback context can change. */
void UIArtworkReader_Stop(void);
bool UIArtworkReader_Running(void);
/* Upload/reset thread: drain before suspending the menu thread, and prevent
 * new starts until a rejected upload unblocks it before resuming the menu.
 * The upload/reset handoff already serializes callers of this pair. */
void UIArtworkReader_Block(void);
void UIArtworkReader_Unblock(void);
bool UIArtworkReader_Blocked(void);
#endif

#endif
