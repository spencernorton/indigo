#include "core.h"

#include <string.h>

const vl_mode vl_modes[VL_CASE_COUNT] = {
	{640, 480, 640, 704, 8},
	{720, 480, 720, 720, 0}
};

size_t vl_frame_bytes(const vl_mode *mode)
{
	return mode ? (size_t)mode->stride * mode->height * 2u : 0;
}

int vl_validate(const vl_mode *mode, size_t capacity)
{
	if(!mode || mode->width < 128 || mode->width > 720 ||
		mode->height != 480 || (mode->width & 1u) ||
		mode->stride < mode->width || mode->stride > 720 ||
		(mode->stride & 15u) || mode->vi_width < mode->width ||
		mode->vi_origin + mode->vi_width > 720)
		return 0;
	return capacity >= vl_frame_bytes(mode);
}

static uint32_t signature_word(unsigned row, const vl_mode *mode,
	unsigned pattern, uint32_t sequence)
{
	switch(row) {
		case 0: return VL_MAGIC;
		case 1: return VL_VERSION;
		case 2: return mode->width;
		case 3: return mode->height;
		case 4: return mode->stride;
		case 5: return pattern;
		case 6: return sequence;
		default: return mode->vi_width;
	}
}

uint8_t vl_luma(unsigned x, unsigned y, const vl_mode *mode,
	unsigned pattern, uint32_t sequence)
{
	unsigned bit;
	int64_t dx, dy, radius, square;

	if(pattern == 3) {
		static const uint8_t levels[4] = {16, 80, 160, 235};
		/* Two independent bits per sample: x at six-row bit bands,
		 * y at six-column bit bands. Both coordinates are decodable
		 * across the entire source width/height, including the edges.
		 * Only the bottom-right sequence stamp is reserved; it never
		 * intersects the decoding bands in the first 60 rows/columns. */
		if(x + 32 >= mode->width && y + 32 >= mode->height)
			return sequence & (1u << (x & 31u)) ? 235 : 16;
		bit = ((x >> ((y / 6u) % 10u)) & 1u) |
			(((y >> ((x / 6u) % 9u)) & 1u) << 1);
		return levels[bit];
	}
	/* Eight rows of 32 bits, four lines per row. This is source metadata,
	 * not an assertion about what an HDMI receiver has preserved. */
	if(y < 32) {
		bit = x * 32u / mode->width;
		return signature_word(y / 4u, mode, pattern, sequence) &
			(1u << (31u - bit)) ? 235 : 16;
	}
	if(x < 2 || x + 2 >= mode->width || y < 34 || y + 2 >= mode->height)
		return 235;
	switch(pattern) {
		case 0: return ((x / 16u) ^ (y / 16u)) & 1u ? 235 : 16;
		case 1: return x & 1u ? 235 : 16;
		case 2: return y & 1u ? 235 : 16;
		default:
			/* A circle in a 16:9 display, independent of sample-grid PAR.
			 * The diagonal cross and outside marks reveal clipping/stretch. */
			dx = ((int64_t)x * 2 - mode->width) * 16 * mode->height;
			dy = ((int64_t)y * 2 - mode->height) * 9 * mode->width;
			radius = (int64_t)mode->height * 6 * mode->width;
			square = dx * dx + dy * dy;
			if(square > radius * radius * 98 / 100 &&
				square < radius * radius * 102 / 100)
				return 235;
			return x * mode->height / mode->width == y ||
				(mode->width - 1u - x) * mode->height / mode->width == y ?
				180 : 16;
	}
}

int vl_render(uint8_t *buffer, size_t capacity, const vl_mode *mode,
	unsigned pattern, uint32_t sequence)
{
	unsigned x, y;
	uint8_t *line;

	if(!buffer || pattern >= VL_PATTERN_COUNT || !vl_validate(mode, capacity))
		return 0;
	for(y = 0; y < mode->height; y++) {
		line = buffer + (size_t)y * mode->stride * 2u;
		for(x = 0; x < mode->stride; x += 2) {
			/* Legal YCbCr422 black, including padding; zero is not black
			 * on the console's digital byte stream. Explicit bytes avoid
			 * host/PowerPC endian differences in the reference image. */
			line[x * 2u] = x < mode->width ?
				vl_luma(x, y, mode, pattern, sequence) : 16;
			line[x * 2u + 1] = 128;
			line[x * 2u + 2] = x + 1u < mode->width ?
				vl_luma(x + 1u, y, mode, pattern, sequence) : 16;
			line[x * 2u + 3] = 128;
		}
	}
	return 1;
}

uint32_t vl_crc32(const uint8_t *data, size_t length)
{
	uint32_t crc = 0xffffffffu;
	unsigned bit;
	while(length--) {
		crc ^= *data++;
		for(bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static int equal_ci(const char *a, const char *b, size_t n)
{
	size_t i;
	for(i = 0; i < n; i++) {
		unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
		if(x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if(y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if(x != y) return 0;
	}
	return 1;
}

vl_request vl_parse_request(const char *data, size_t length)
{
	vl_request out = {VL_NONE, 0, 400};
	char request[1024], *line, *end, *path, *version;
	int post, allowed = 0, content_length = 0;

	if(!data || length < 4 || length >= sizeof(request) ||
		memchr(data, 0, length) != NULL)
		return out;
	memcpy(request, data, length);
	request[length] = 0;
	end = strstr(request, "\r\n\r\n");
	if(!end || (size_t)(end + 4 - request) != length) return out;
	line = strstr(request, "\r\n");
	if(!line) return out;
	*line = 0;
	if(strncmp(request, "GET ", 4) == 0) post = 0;
	else if(strncmp(request, "POST ", 5) == 0) post = 1;
	else return out;
	path = request + (post ? 5 : 4);
	version = strchr(path, ' ');
	if(!version) return out;
	*version++ = 0;
	if(strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1")) return out;
	line += 2;
	while(line < end) {
		char *next = strstr(line, "\r\n");
		size_t size;
		if(!next) return out;
		size = (size_t)(next - line);
		if(size >= 6 && equal_ci(line, "X-Lab:", 6)) {
			const char *value = line + 6;
			while(value < next && (*value == ' ' || *value == '\t')) value++;
			if(value + 1 != next || *value != '1' || allowed) return out;
			allowed = 1;
		}
		if(size >= 17 && equal_ci(line, "Transfer-Encoding", 17)) return out;
		if(size >= 15 && equal_ci(line, "Content-Length:", 15)) {
			const char *value = line + 15, *last = next;
			while(value < last && (*value == ' ' || *value == '\t')) value++;
			while(last > value && (last[-1] == ' ' || last[-1] == '\t')) last--;
			if(content_length || value + 1 != last || *value != '0') return out;
			content_length = 1;
		}
		line = next + 2;
	}
	if(post && !allowed) { out.status = 403; return out; }
	out.status = 200;
	if(!post && (!strcmp(path, "/status") || !strcmp(path, "/log")))
		out.command = VL_STATUS;
	else if(!post && !strcmp(path, "/xfb")) out.command = VL_XFB;
	else if(post && !strcmp(path, "/baseline")) out.command = VL_BASELINE;
	else if(post && !strcmp(path, "/trial/P1")) out.command = VL_TRIAL;
	else if(post && !strcmp(path, "/expire")) out.command = VL_EXPIRE;
	else if(post && (!strcmp(path, "/exit") || !strcmp(path, "/reset")))
		out.command = VL_EXIT;
	else if(post && !strncmp(path, "/pattern/", 9) && strlen(path) == 10 &&
		path[9] >= '0' && (unsigned char)path[9] < (unsigned)'0' + VL_PATTERN_COUNT) {
		out.command = VL_PATTERN;
		out.value = (unsigned)(path[9] - '0');
	} else out.status = 404;
	return out;
}
