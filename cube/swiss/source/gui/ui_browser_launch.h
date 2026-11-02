/* Stable, named argv contract for the separately built browser. */
#ifndef UI_BROWSER_LAUNCH_H
#define UI_BROWSER_LAUNCH_H
#include <stdbool.h>
#include <stdint.h>
#define UI_BROWSER_ARG_MAX 13

typedef struct {
    int color, video, timezoneMinutes;
    bool wide, animationsOff, reduceMotion, soundsOff, rumbleOff, lab, timezoneKnown;
    const char *device;
} uiBrowserLaunchContext_t;
typedef struct {
    char values[UI_BROWSER_ARG_MAX][64];
    char *argv[UI_BROWSER_ARG_MAX + 1];
    int argc;
} uiBrowserLaunchArgs_t;
void UIBrowserLaunch_Arguments(uiBrowserLaunchArgs_t *args, const uiBrowserLaunchContext_t *context);
/* Configured POSIX zone at a local console timestamp, if the offset is known. */
bool UIBrowserLaunch_ZoneOffset(const char *zone, int64_t localSeconds, int32_t *eastSeconds);
#endif
