#include "core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned level(uint8_t value)
{
	switch(value) {
		case 16: return 0;
		case 80: return 1;
		case 160: return 2;
		case 235: return 3;
		default: assert(0); return 0;
	}
}

static void frames(void)
{
	unsigned m, p, x, y, b;
	for(m = 0; m < VL_CASE_COUNT; m++) {
		const vl_mode *mode = &vl_modes[m];
		size_t bytes = vl_frame_bytes(mode), i;
		uint8_t *allocation = malloc(bytes + 64), *image = allocation + 32;
		assert(allocation);
		assert(vl_validate(mode, bytes));
		assert(!vl_validate(mode, bytes - 1));
		for(p = 0; p < VL_PATTERN_COUNT; p++) {
			memset(allocation, 0xa5, bytes + 64);
			assert(vl_render(image, bytes, mode, p, 123));
			for(i = 0; i < 32; i++) {
				assert(allocation[i] == 0xa5);
				assert(image[bytes + i] == 0xa5);
			}
			for(i = 0; i < bytes; i += 2) {
				assert(image[i] >= 16 && image[i] <= 235);
				assert(image[i + 1] == 128);
			}
			if(p == 3) {
				/* Independent decoder, not a second call to vl_luma.
				 * These assertions cover all additional P1 columns too. */
				for(x = 0; x < mode->width; x++) {
					unsigned decoded = 0;
					for(b = 0; b < 10; b++)
						decoded |= (level(image[((6u * b + 2u) * mode->stride + x) * 2u]) & 1u) << b;
					assert(decoded == x);
				}
				for(y = 0; y < mode->height; y++) {
					unsigned decoded = 0;
					for(b = 0; b < 9; b++)
						decoded |= (level(image[(y * mode->stride + 6u * b + 2u) * 2u]) >> 1u) << b;
					assert(decoded == y);
				}
			}
		}
		memset(image, 0xa5, bytes);
		assert(!vl_render(image, bytes, mode, VL_PATTERN_COUNT, 0));
		assert(!vl_render(image, bytes - 1, mode, 0, 0));
		for(i = 0; i < bytes; i++) assert(image[i] == 0xa5);
		free(allocation);
	}
	{
		vl_mode invalid = vl_modes[0];
		invalid.width = 1280;
		assert(!vl_validate(&invalid, 1280u * 720u * 2u));
		invalid = vl_modes[0]; invalid.stride = 639;
		assert(!vl_validate(&invalid, 10000000));
		invalid = vl_modes[0]; invalid.vi_origin = 100;
		assert(!vl_validate(&invalid, 10000000));
		assert(!vl_validate(NULL, 0));
	}
}

static void request(const char *text, int status, int command, unsigned value)
{
	vl_request result = vl_parse_request(text, strlen(text));
	assert(result.status == status);
	if(status == 200) {
		assert(result.command == command);
		assert(result.value == value);
	}
}

static void requests(void)
{
	unsigned i;
	const char *valid = "POST /trial/P1 HTTP/1.1\r\nContent-Length: 0\r\nX-Lab: 1\r\n\r\n";
	char large[1100];
	request("GET /status HTTP/1.0\r\n\r\n", 200, VL_STATUS, 0);
	request("GET /xfb HTTP/1.1\r\nHost: example\r\n\r\n", 200, VL_XFB, 0);
	request(valid, 200, VL_TRIAL, 0);
	request("POST /baseline HTTP/1.1\r\nx-lab:\t1\r\nContent-Length:0\r\n\r\n", 200, VL_BASELINE, 0);
	request("POST /pattern/4 HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 200, VL_PATTERN, 4);
	request("POST /gx/start HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 200, VL_GX_START, 0);
	request("POST /gx/show HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 200, VL_GX_SHOW, 0);
	request("GET /gx/status HTTP/1.0\r\n\r\n", 200, VL_GX_STATUS, 0);
	request("GET /gx/xfb HTTP/1.0\r\n\r\n", 200, VL_GX_XFB, 0);
	request("GET /gx/scene/3 HTTP/1.0\r\n\r\n", 200, VL_GX_SCENE, 3);
	request("GET /gx/scene/4 HTTP/1.0\r\n\r\n", 404, 0, 0);
	request("POST /gx/start HTTP/1.0\r\n\r\n", 403, 0, 0);
	request("POST /expire HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 200, VL_EXPIRE, 0);
	request("POST /exit HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 200, VL_EXIT, 0);
	request("POST /trial/P1 HTTP/1.0\r\n\r\n", 403, 0, 0);
	request("POST /trial/P2 HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 404, 0, 0);
	request("POST /pattern/6 HTTP/1.0\r\nX-Lab: 1\r\n\r\n", 404, 0, 0);
	request("POST /baseline HTTP/1.0\r\nX-Lab: 1\r\nX-Lab: 1\r\n\r\n", 400, 0, 0);
	request("POST /baseline HTTP/1.0\r\nContent-Length: 1\r\nX-Lab: 1\r\n\r\nx", 400, 0, 0);
	request("POST /baseline HTTP/1.0\r\nTransfer-Encoding: chunked\r\nX-Lab: 1\r\n\r\n", 400, 0, 0);
	request("GET /xfb HTTP/1.0\r\n\r\nGET /status HTTP/1.0\r\n\r\n", 400, 0, 0);
	request("GET /xfb HTTP/2.0\r\n\r\n", 400, 0, 0);
	for(i = 0; i < strlen(valid); i++)
		assert(vl_parse_request(valid, i).status == 400);
	memset(large, 'A', sizeof(large));
	assert(vl_parse_request(large, sizeof(large)).status == 400);
	assert(vl_parse_request(NULL, 0).status == 400);
}

int main(void)
{
	assert(vl_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
	frames();
	requests();
	puts("video lab: coordinate decoding, buffer guards, legal luma/chroma and request controls passed");
	return 0;
}
