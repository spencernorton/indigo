#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_builtin_images.h"
#define UI_BUILTIN_IMAGES_TEST_DEFINE_DATA
#include "images_tplz.h"
#include "images_meta.h"

static unsigned flushes;
static void *published;
static void check_pixels(const unsigned char *data);

void uiBuiltinImagesHostFlush(void *data, size_t bytes)
{
	assert(bytes == UI_BUILTIN_IMAGES_BYTES);
	assert(((uintptr_t)data & 31u) == 0);
	check_pixels(data);  /* Cache publication must follow the completed decode. */
	published = data;
	++flushes;
}

static void check_pixels(const unsigned char *data)
{
	assert(memcmp(data, "\x00\x20\xaf\x30", 4) == 0);
	for(size_t i = 4; i < UI_BUILTIN_IMAGES_BYTES; ++i)
		assert(data[i] == (unsigned char)(i - 4));
}

int main(void)
{
	unsigned char guarded[UI_BUILTIN_IMAGES_BYTES + 64];
	unsigned char broken[sizeof(images_tplz) + 1];
	memset(guarded, 0xa5, sizeof(guarded));
	assert(UIBuiltinImages_Decode(guarded + 32, UI_BUILTIN_IMAGES_BYTES,
		images_tplz, images_tplz_size));
	check_pixels(guarded + 32);
	for(size_t i = 0; i < 32; ++i) {
		assert(guarded[i] == 0xa5);
		assert(guarded[UI_BUILTIN_IMAGES_BYTES + 32 + i] == 0xa5);
	}
	assert(!UIBuiltinImages_Decode(guarded, UI_BUILTIN_IMAGES_BYTES,
		images_tplz, images_tplz_size - 1));
	memset(guarded, 0xa5, sizeof(guarded));
	assert(!UIBuiltinImages_Decode(guarded + 32, UI_BUILTIN_IMAGES_BYTES - 1,
		images_tplz, images_tplz_size));
	for(size_t i = UI_BUILTIN_IMAGES_BYTES + 31; i < sizeof(guarded); ++i)
		assert(guarded[i] == 0xa5);
	assert(!UIBuiltinImages_Decode(guarded, UI_BUILTIN_IMAGES_BYTES + 1,
		images_tplz, images_tplz_size));
	memcpy(broken, images_tplz, images_tplz_size);
	broken[images_tplz_size] = 0;
	assert(!UIBuiltinImages_Decode(guarded, UI_BUILTIN_IMAGES_BYTES,
		broken, sizeof(broken)));  /* trailing bytes are not a complete bundle */
	broken[images_tplz_size / 2] ^= 0x80;
	assert(!UIBuiltinImages_Decode(guarded, UI_BUILTIN_IMAGES_BYTES,
		broken, images_tplz_size));
	assert(!UIBuiltinImages_Decode(NULL, UI_BUILTIN_IMAGES_BYTES,
		images_tplz, images_tplz_size));
	assert(!UIBuiltinImages_Decode(guarded, 0, images_tplz, images_tplz_size));
	assert(!UIBuiltinImages_Decode(guarded, UI_BUILTIN_IMAGES_BYTES, NULL, 0));

	/* A failed expansion publishes nothing and leaves initialization retryable. */
	images_tplz[images_tplz_size - 1] ^= 1;
	assert(UIBuiltinImages_Get() == NULL && flushes == 0 && published == NULL);
	images_tplz[images_tplz_size - 1] ^= 1;
	unsigned char *data = UIBuiltinImages_Get();
	assert(data && data == published && flushes == 1);
	assert(UIBuiltinImages_Size() == UI_BUILTIN_IMAGES_BYTES);
	check_pixels(data);
	data[12] ^= 0x55;  /* TPL fixes pointers in place; Get must not overwrite them. */
	unsigned char fixed_up = data[12];
	for(unsigned i = 0; i < 10; ++i)
		assert(UIBuiltinImages_Get() == data && data[12] == fixed_up);
	assert(flushes == 1);
	puts("built-in images: exact bytes, bounds, complete streams and one publication pass");
	return 0;
}
