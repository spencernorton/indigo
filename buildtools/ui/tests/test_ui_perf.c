#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <ogc/lwp_watchdog.h>

#include "ui_perf.h"

static u64 fakeTicks;

u64 SYS_GetSystemTime(void)
{
	return fakeTicks;
}

#if UI_PERF_CAPTURE

static void testResetAndThresholds(void)
{
	uiPerfSnapshot_t snap;

	fakeTicks = 1000ULL;
	UIPerf_Reset();
	UIPerf_Snapshot(NULL);
	memset(&snap, 0xA5, sizeof(snap));
	UIPerf_Snapshot(&snap);

	assert(snap.metrics[UI_PERF_METRIC_FRAME_PERIOD].count == 0u);
	assert(snap.metrics[UI_PERF_METRIC_FRAME_PERIOD].thresholdMicroseconds ==
		UI_PERF_FRAME_PERIOD_THRESHOLD_US);
	assert(snap.metrics[UI_PERF_METRIC_FRAME_WORK].thresholdMicroseconds ==
		UI_PERF_FRAME_WORK_THRESHOLD_US);
	assert(snap.metrics[UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT].thresholdMicroseconds ==
		UI_PERF_BACKGROUND_CPU_SUBMIT_THRESHOLD_US);
	assert(UIPerf_PercentileUs(NULL, 50u) == 0u);
	assert(UIPerf_PercentileUs(&snap.metrics[UI_PERF_METRIC_FRAME_WORK], 50u) == 0u);
}

static void testHistogramAndPercentiles(void)
{
	uiPerfSnapshot_t snap;
	const uiPerfMetricSnapshot_t *work;
	u32 i;

	UIPerf_Reset();
	UIPerf_RecordMicroseconds(UI_PERF_METRIC_COUNT, 5000ULL);
	UIPerf_RecordMicroseconds((uiPerfMetric_t)99, 5000ULL);

	for(i = 0u; i < 90u; i++) {
		UIPerf_RecordMicroseconds(UI_PERF_METRIC_FRAME_WORK, 1000ULL);
	}
	for(i = 0u; i < 9u; i++) {
		UIPerf_RecordMicroseconds(UI_PERF_METRIC_FRAME_WORK,
			UI_PERF_FRAME_WORK_THRESHOLD_US - 1ULL);
	}
	UIPerf_RecordMicroseconds(UI_PERF_METRIC_FRAME_WORK,
		UI_PERF_FRAME_WORK_THRESHOLD_US);

	UIPerf_Snapshot(&snap);
	work = &snap.metrics[UI_PERF_METRIC_FRAME_WORK];
	assert(work->count == 100ULL);
	assert(work->thresholdExceedances == 1ULL);
	assert(work->maxMicroseconds == UI_PERF_FRAME_WORK_THRESHOLD_US);
	assert(UIPerf_PercentileUs(work, 0u) == 0ULL);
	assert(UIPerf_PercentileUs(work, 50u) == 1250ULL);
	assert(UIPerf_PercentileUs(work, 90u) == 1250ULL);
	assert(UIPerf_PercentileUs(work, 95u) == 12000ULL);
	assert(UIPerf_PercentileUs(work, 99u) == 12000ULL);
	assert(UIPerf_PercentileUs(work, 100u) == 12250ULL);
	assert(UIPerf_PercentileUs(work, 250u) == 12250ULL);
}

static void testOverflowBinTicksWrapAndSaturation(void)
{
	uiPerfSnapshot_t snap;
	const uiPerfMetricSnapshot_t *bg;

	UIPerf_Reset();
	fakeTicks = (u64)-1 - microsecs_to_ticks(500ULL);
	{
		UI_PERF_BEGIN(started);
		fakeTicks = microsecs_to_ticks(1500ULL);
		UI_PERF_END(UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT, started);
	}
	UIPerf_RecordMicroseconds(UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT, 95000ULL);
	UIPerf_RecordMicroseconds(UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT, (u64)-10ULL);

	UIPerf_Snapshot(&snap);
	bg = &snap.metrics[UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT];
	assert(bg->count == 3ULL);
	assert(bg->thresholdExceedances == 3ULL);
	assert(bg->sumMicroseconds == (u64)-1);
	assert(bg->histogram[UI_PERF_HISTOGRAM_OVERFLOW_BIN] == 2u);
	assert(UIPerf_PercentileUs(bg, 99u) == (u64)-10ULL);
}

int main(void)
{
	testResetAndThresholds();
	testHistogramAndPercentiles();
	testOverflowBinTicksWrapAndSaturation();
	return 0;
}

#else

int main(void)
{
	uiPerfSnapshot_t snap;
	int sideEffects = 0;

	memset(&snap, 0, sizeof(snap));
	UIPerf_Reset();
	UI_PERF_BEGIN(unusedStart);
	UI_PERF_END(UI_PERF_METRIC_FRAME_WORK, (sideEffects++, 42ULL));
	UIPerf_RecordTicks(UI_PERF_METRIC_FRAME_WORK, (sideEffects++, 1ULL), (sideEffects++, 2ULL));
	UIPerf_RecordMicroseconds(UI_PERF_METRIC_FRAME_WORK, (sideEffects++, 42ULL));
	UIPerf_Snapshot(&snap);
	assert(UIPerf_Now() == 0ULL);
	assert(UIPerf_PercentileUs(&snap.metrics[0], 99u) == 0ULL);
	assert(sideEffects == 0);
	return 0;
}

#endif
