#include "ui_browser_launch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *find(const uiBrowserLaunchArgs_t *args, const char *key)
{
    size_t n = strlen(key);
    for(int i = 1; i < args->argc; i++)
        if(!strncmp(args->argv[i], key, n) && args->argv[i][n] == '=') return args->argv[i] + n + 1;
    return NULL;
}
int main(void)
{
    uiBrowserLaunchArgs_t args;
    uiBrowserLaunchContext_t context = {.color = 4, .video = 2, .wide = true, .reduceMotion = true,
        .soundsOff = true, .rumbleOff = true, .timezoneMinutes = -360, .timezoneKnown = true, .device = "gcldr", .lab = true};
    UIBrowserLaunch_Arguments(&args, &context);
    assert(args.argc == 11 && args.argv[0] == NULL && args.argv[args.argc] == NULL);
    assert(!strcmp(find(&args, "indigo.context"), "1"));
    assert(!strcmp(find(&args, "indigo.color"), "spice")); /* Browser's legacy enum 3 is deliberately not used. */
    assert(!strcmp(find(&args, "indigo.video"), "480p") && !strcmp(find(&args, "indigo.wide"), "1"));
    assert(!strcmp(find(&args, "indigo.motion"), "1") && !strcmp(find(&args, "indigo.sounds"), "0"));
    assert(!strcmp(find(&args, "indigo.rumble"), "0") && !strcmp(find(&args, "indigo.timezone"), "-360"));
    assert(!strcmp(find(&args, "indigo.device"), "gcldr") && !strcmp(find(&args, "lab"), "1"));
    context.color = 7; context.animationsOff = true; context.lab = false; context.device = "../../bad";
    UIBrowserLaunch_Arguments(&args, &context);
    assert(!strcmp(find(&args, "indigo.color"), "jet-black") && !strcmp(find(&args, "indigo.motion"), "2"));
    assert(find(&args, "lab") == NULL && find(&args, "indigo.device") == NULL);
    context.timezoneKnown = false;
    UIBrowserLaunch_Arguments(&args, &context);
    assert(find(&args, "indigo.timezone") == NULL);
    int32_t east;
    assert(UIBrowserLaunch_ZoneOffset("IST-5:30", INT64_C(1793320000), &east) && east == 330 * 60);
    assert(UIBrowserLaunch_ZoneOffset("MST7MDT,M3.2.0,M11.1.0", INT64_C(1793320000), &east) && east == -360 * 60);
    assert(UIBrowserLaunch_ZoneOffset("UTC0", INT64_C(1793320000), &east) && east == 0);
    assert(!UIBrowserLaunch_ZoneOffset("", INT64_C(1793320000), &east));
    assert(!UIBrowserLaunch_ZoneOffset("bad", INT64_C(1793320000), &east));
    puts("browser argv: version, names, display, accessibility, card, lab and timezone pass");
    return 0;
}
