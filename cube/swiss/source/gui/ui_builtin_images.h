/* Built-in textures are expanded once, before the video thread is started. */
#ifndef UI_BUILTIN_IMAGES_H
#define UI_BUILTIN_IMAGES_H

#include <stdbool.h>
#include <stddef.h>

/* Require exactly one complete stream and exactly the promised output size. */
bool UIBuiltinImages_Decode(void *out, size_t out_bytes,
	const void *packed, size_t packed_bytes);

/* The aligned, writable TPL storage persists for GX and TPL's pointer fixups.
 * NULL means initialization failed; no partially decoded texture is published.
 * Only the initialization thread calls this, before starting the video thread. */
void *UIBuiltinImages_Get(void);
size_t UIBuiltinImages_Size(void);

#endif
