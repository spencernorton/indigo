#include "ui_builtin_images.h"

#include <stdint.h>
#include <zlib.h>
#include "images_tplz.h"
#include "images_meta.h"

#ifdef UI_BUILTIN_IMAGES_HOST_BUILD
extern void uiBuiltinImagesHostFlush(void *data, size_t bytes);
#define FLUSH(data, bytes) uiBuiltinImagesHostFlush(data, bytes)
#else
#include <ogc/cache.h>
#define FLUSH(data, bytes) DCFlushRange(data, bytes)
#endif

static uint8_t expanded[UI_BUILTIN_IMAGES_BYTES] __attribute__((aligned(32)));
static bool ready;

bool UIBuiltinImages_Decode(void *out, size_t out_bytes,
	const void *packed, size_t packed_bytes)
{
	uLongf decoded = out_bytes;
	uLong consumed = packed_bytes;
	if(!out || !packed || !out_bytes || !packed_bytes ||
		(size_t)decoded != out_bytes || (size_t)consumed != packed_bytes)
		return false;
	return uncompress2(out, &decoded, packed, &consumed) == Z_OK &&
		decoded == out_bytes && consumed == packed_bytes;
}

void *UIBuiltinImages_Get(void)
{
	if(!ready) {
		if(!UIBuiltinImages_Decode(expanded, sizeof(expanded),
			images_tplz, images_tplz_size))
			return NULL;
		/* GX reads main memory; publish the CPU's decoded pixels once. */
		FLUSH(expanded, sizeof(expanded));
		ready = true;
	}
	return expanded;
}

size_t UIBuiltinImages_Size(void)
{
	return sizeof(expanded);
}
