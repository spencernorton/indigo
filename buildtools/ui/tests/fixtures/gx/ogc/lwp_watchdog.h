/* libogc GameCube timebase helpers (40.5 ticks/us), for host unit tests. */
#ifndef STUB_OGC_LWP_WATCHDOG_H
#define STUB_OGC_LWP_WATCHDOG_H

#include <gctypes.h>

#define TB_BUS_CLOCK 162000000u
#define TB_TIMER_CLOCK (TB_BUS_CLOCK / 4000u)

u64 SYS_GetSystemTime(void);

static inline u64 diff_ticks(u64 tick0, u64 tick1)
{
	return (tick1 < tick0) ? ((u64)-1 - tick0 + tick1) : (tick1 - tick0);
}

static inline u64 ticks_to_microsecs(u64 ticks)
{
	return (ticks * 8ULL) / (u64)(TB_TIMER_CLOCK / 125u);
}

static inline u64 microsecs_to_ticks(u64 us)
{
	return (us * (u64)(TB_TIMER_CLOCK / 125u)) / 8ULL;
}

#endif
