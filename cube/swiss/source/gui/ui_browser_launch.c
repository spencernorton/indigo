#include "ui_browser_launch.h"
#include "ui_time_sync.h"
#include <stdio.h>
#include <string.h>

static void add(uiBrowserLaunchArgs_t *args, const char *key, const char *value)
{
    if(args->argc >= UI_BROWSER_ARG_MAX) return;
    int index = args->argc++;
    snprintf(args->values[index], sizeof(args->values[index]), "%s=%s", key, value);
    args->argv[index] = args->values[index];
    args->argv[args->argc] = NULL;
}
void UIBrowserLaunch_Arguments(uiBrowserLaunchArgs_t *args, const uiBrowserLaunchContext_t *context)
{
    static const char *const colors[] = {
        "indigo", "azure", "emerald", "gold", "spice", "crimson", "rose", "jet-black"
    };
    memset(args, 0, sizeof(*args));
    args->argc = 1; /* boot_dol supplies argv[0], the executable path. */
    int color = context->color >= 0 && context->color < 8 ? context->color : 0;
    add(args, "indigo.context", "1");
    add(args, "indigo.color", colors[color]);
    add(args, "indigo.video", context->video == 2 ? "480p" : context->video == 1 ? "480i" : "auto");
    add(args, "indigo.wide", context->wide ? "1" : "0");
    add(args, "indigo.motion", context->animationsOff ? "2" : context->reduceMotion ? "1" : "0");
    add(args, "indigo.sounds", context->soundsOff ? "0" : "1");
    add(args, "indigo.rumble", context->rumbleOff ? "0" : "1");
    int minutes = context->timezoneMinutes;
    if(context->timezoneKnown && minutes >= -720 && minutes <= 840) {
        char timezone[16];
        snprintf(timezone, sizeof(timezone), "%d", minutes);
        add(args, "indigo.timezone", timezone);
    }
    if(context->device != NULL && (!strcmp(context->device, "sda") || !strcmp(context->device, "sdb") ||
        !strcmp(context->device, "sdc") || !strcmp(context->device, "gcldr")))
        add(args, "indigo.device", context->device);
    if(context->lab) add(args, "lab", "1");
}
bool UIBrowserLaunch_ZoneOffset(const char *zone, int64_t localSeconds, int32_t *eastSeconds)
{
    int32_t first;
    if(zone == NULL || !zone[0] || eastSeconds == NULL ||
        !UITimeSync_ZoneOffset(zone, localSeconds, &first)) return false;
    /* The console clock is already local. Re-evaluate at the UTC estimate. */
    if(!UITimeSync_ZoneOffset(zone, localSeconds - first, eastSeconds)) return false;
    return *eastSeconds >= -720 * 60 && *eastSeconds <= 840 * 60;
}
