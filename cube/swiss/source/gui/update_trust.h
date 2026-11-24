#ifndef UPDATE_TRUST_H
#define UPDATE_TRUST_H

#include "ui_update.h"

#define UPDATE_SIGNATURE_BYTES 64u
#define UPDATE_ENVELOPE_MAX (UPDATE_SIGNATURE_BYTES + UI_UPDATE_MANIFEST_MAX)
#define INDIGO_UPDATE_PUBLIC_KEY "18c60188a116f53fbbebb264c7d332af09ee2b2b98de0cb026e651082c7fe7f1"

/* A detached Ed25519 signature followed by the exact published manifest. */
bool updateReadSigned(const char *bytes, size_t size, const char *channel,
	ui_update_manifest_t *manifest);

#endif
