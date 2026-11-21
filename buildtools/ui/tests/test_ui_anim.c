#include <assert.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>

#include "ui_anim.h"
#include "ui_perf.h"

static u64 fakeTicks;

u64 SYS_GetSystemTime(void)
{
	return fakeTicks;
}

static void advanceUs(u64 us)
{
	fakeTicks += microsecs_to_ticks(us);
	UIAnim_BeginFrame();
}

static void testSteadyStepAndStallClamp(void)
{
	uiPerfSnapshot_t snap;

	fakeTicks = 10000ULL;
	UIPerf_Reset();
	UIAnim_Reset();
	assert(UIAnim_Seconds() == 0.0f);
	assert(UIAnim_Delta() == 0.0f);

	/* 20 ms (50 Hz) frame: exact 0.020 s delta, below 25 ms period threshold. */
	advanceUs(20000ULL);
	assert(fabsf(UIAnim_Delta() - 0.020f) < 1e-6f);
	assert(fabsf(UIAnim_Seconds() - 0.020f) < 1e-6f);

	/* 250 ms device-probe stall: animation delta clamps to 50 ms so springs do
	 * not explode, while UIPerf still records the true 250 ms frame period. */
	advanceUs(250000ULL);
	assert(fabsf(UIAnim_Delta() - 0.050f) < 1e-6f);
	assert(fabsf(UIAnim_Seconds() - 0.070f) < 1e-6f);

	UIPerf_Snapshot(&snap);
	assert(snap.metrics[UI_PERF_METRIC_FRAME_PERIOD].count == 2ULL);
	assert(snap.metrics[UI_PERF_METRIC_FRAME_PERIOD].thresholdExceedances == 1ULL);
	assert(snap.metrics[UI_PERF_METRIC_FRAME_PERIOD].maxMicroseconds == 250000ULL);
}

static void testTickCounterWrapAndPhasePreservingClockWrap(void)
{
	float phaseBefore, phaseAfter;
	int i;

	/* 64-bit hardware timebase wraparound across UINT64_MAX. */
	fakeTicks = (u64)-1 - microsecs_to_ticks(8000ULL);
	UIAnim_Reset();
	fakeTicks = microsecs_to_ticks(12000ULL);
	UIAnim_BeginFrame();
	assert(fabsf(UIAnim_Delta() - 0.020f) < 1e-5f);

	/* Step up to just before UI_ANIM_TIME_WRAP_SECONDS (1000 * 2pi) in 50 ms
	 * increments, then cross the wrap and check ambient 0.001 rad/s phase. */
	fakeTicks = 0ULL;
	UIAnim_Reset();
	for(i = 0; i < 200000 && UIAnim_Seconds() < UI_ANIM_TIME_WRAP_SECONDS - 0.05f; i++) {
		advanceUs(50000ULL);
	}
	assert(UIAnim_Seconds() >= UI_ANIM_TIME_WRAP_SECONDS - 0.055f &&
		UIAnim_Seconds() < UI_ANIM_TIME_WRAP_SECONDS);
	phaseBefore = sinf(0.007f * UIAnim_Seconds());
	advanceUs(50000ULL);
	advanceUs(50000ULL);
	assert(UIAnim_Seconds() >= 0.0f && UIAnim_Seconds() < 0.11f);
	phaseAfter = sinf(0.007f * UIAnim_Seconds());
	/* 50 ms * 0.007 rad/s = 0.00035 rad max step; a non-harmonic wrap would jump. */
	assert(fabsf(phaseAfter - phaseBefore) < 0.002f);
}

int main(void)
{
	testSteadyStepAndStallClamp();
	testTickCounterWrapAndPhasePreservingClockWrap();
	return 0;
}
