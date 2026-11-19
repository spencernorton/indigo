#ifndef UI_UPDATE_H
#define UI_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The software updater's reading and deciding (Settings > Network): the
 * release manifest's Key=Value text, whether what it offers is newer than
 * the build running, the update server's address grammar, and the head of
 * a plain-HTTP reply. No console calls, so the host tests run them and the
 * fuzzer feeds them (buildtools/ui/tests/test_ui_update.c,
 * buildtools/ui/tests/fuzz/fuzz_update.c). update.c does the network, the
 * card and the screen. The format and the rules are written down in
 * docs/dev/software-update-design.md. */

#define UI_UPDATE_MANIFEST_MAX 4096u   /* a manifest is never longer */
#define UI_UPDATE_LINE_MAX 512u        /* ... and neither is one line of it */
#define UI_UPDATE_DOL_MAX (16u * 1024u * 1024u) /* boot_dol's ceiling */
#define UI_UPDATE_APPLOADER_MAX (8u * 1024u * 1024u) /* a packed DOL: ~2.4 MiB today */
#define UI_UPDATE_CHANNELS 2           /* Stable, Release candidate */
#define UI_UPDATE_BOOT_AUTO 0          /* Update Boot File: look on the card */

typedef struct {
	char version[24];        /* "v2.10.0": shown, never compared */
	char commit[41];         /* the release commit, 40 hex */
	char date[11];           /* YYYY-MM-DD, shown */
	int64_t buildTime;       /* the release commit's time: what "newer" means */
	char file[65];           /* the DOL's name beside the manifest */
	uint32_t size;           /* its bytes */
	uint8_t sha256[32];      /* its SHA-256 */
	bool hasApploader;       /* the three apploader lines came together */
	char apploaderFile[65];
	uint32_t apploaderSize;
	uint8_t apploaderSha256[32];
	bool hasMinBuild;        /* MinBuildTime (and MinVersion) were there */
	int64_t minBuildTime;
	char minVersion[24];
	char notesUrl[128];      /* shown for a computer; never fetched */
} ui_update_manifest_t;

typedef enum {
	UI_UPDATE_OK = 0,
	UI_UPDATE_INVALID,   /* not a manifest, or a line of it breaks the rules */
	UI_UPDATE_FORMAT,    /* a newer FormatVersion than this Indigo reads */
	UI_UPDATE_CHANNEL    /* not the channel that was asked for */
} ui_update_err_t;

/* Reads one channel's manifest: `text` is its bytes (NUL-terminated by the
 * caller, `length` of them), `channel` is "stable" or "rc" and must match
 * the Channel line inside. Unknown keys are skipped, so a later manifest
 * can carry lines this build ignores. */
ui_update_err_t UIUpdate_ParseManifest(const char *text, size_t length,
	const char *channel, ui_update_manifest_t *out);

typedef enum {
	UI_UPDATE_DECIDE_MIN_BUILD = 0, /* an older stepping-stone release first */
	UI_UPDATE_DECIDE_CURRENT,       /* the manifest is the build running */
	UI_UPDATE_DECIDE_NEWER,         /* an update */
	UI_UPDATE_DECIDE_OLDER          /* another build, and not a newer one */
} ui_update_decision_t;

/* Whether the manifest should be offered, and how. The running build knows
 * only its commit's first seven letters (GIT_COMMIT) and its commit's time
 * (GIT_TIMESTAMP); a different commit alone never means "newer". */
ui_update_decision_t UIUpdate_Decide(const ui_update_manifest_t *manifest,
	const char *runningCommit7, int64_t runningBuildTime);

/* "stable" or "rc" for the Update Channel setting (0 is Stable). */
const char *UIUpdate_ChannelName(int channel);

/* A manifest's File is used as a name on a server and nowhere else until it
 * has passed this: one plain name, letters, digits, '.', '_' and '-',
 * never starting with a dot, at most 64 letters. */
bool UIUpdate_SafeBasename(const char *name);

/* 64 lower-case hex letters into their 32 bytes; false for anything else. */
bool UIUpdate_HexToBytes32(const char *hex, uint8_t out[32]);

/* The first sixteen hex letters of a digest, as the install question
 * shows them (and the download page shows the same sixteen). */
void UIUpdate_Hex16(const uint8_t hash[32], char out[17]);

typedef struct {
	char host[16];     /* the dotted IPv4 address, for inet_aton */
	uint16_t port;     /* 80 until the text says otherwise */
	char path[64];     /* "/" or "/folder/": where the channel files sit */
} ui_update_base_t;

typedef enum {
	UI_UPDATE_BASE_OK = 0,
	UI_UPDATE_BASE_BAD,    /* not an address, port and folder of this shape */
	UI_UPDATE_BASE_HTTPS,  /* https:// — there is no TLS in this program */
	UI_UPDATE_BASE_NAME    /* a host name — the console has no DNS */
} ui_update_base_err_t;

/* The Update Server text: [http://]ipv4[:port][/folder]. The three ways it
 * can be wrong get three answers, so the screen can say which one it is. */
bool UIUpdate_ParseBase(const char *text, ui_update_base_t *out,
	ui_update_base_err_t *why);

/* The head of an HTTP reply, up to and including its blank line. False when
 * the head is not finished in these bytes or is not HTTP at all. The status
 * is the reply's code; `contentLength` is its Content-Length (0 and
 * `hasLength` false when it sends none); `chunked` is a chunked
 * Transfer-Encoding, which the updater does not read. A second
 * Content-Length that disagrees is not a head this reader trusts. */
bool UIUpdate_ParseHttpHead(const char *head, size_t length, int *status,
	uint32_t *contentLength, bool *hasLength, bool *chunked);

#endif
