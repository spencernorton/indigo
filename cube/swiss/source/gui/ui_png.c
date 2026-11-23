/* ui_png.c - an app's or a folder's picture, from a PNG on the card to a
   Library poster.

   See ui_png.h. The PNG is decoded a row at a time: each row is inflated,
   unfiltered, expanded to RGBA and added into a picture reduced by a whole
   factor, so only that picture (at most about twice the poster's size) and
   two rows are ever held. The poster is then drawn from it with bilinear
   filtering, its mipmaps made by halving, and each level encoded as GX CMPR
   (DXT1 in 8x8 tiles) the way the hardware decodes it. */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ui_png.h"

/* Around a picture that isn't poster-shaped, and how far a small one grows. */
#define PNG_FIT_MARGIN 14.0f
#define PNG_MAX_UPSCALE 4.0f
/* The reduced picture's largest size in bytes (RGBA). */
#define PNG_MAX_REDUCED (1024u * 1024u)
/* The most a chunk's CRC is taken over at once. zlib-ng's CRC of a longer
 * run takes a table of 32 to 128 KB on the stack (its Chorba methods: over
 * 8 KB on a 64-bit build, over 256 KB on the console's), and posters are
 * made on a thread with 32 KB of stack: a 2 MB chunk overran it. */
#define PNG_CRC_PIECE 8192u
#define PNG_MIP_LEVELS 5u

typedef struct {
	uint32_t width;
	uint32_t height;
	uint8_t depth;
	uint8_t colorType;
	uint32_t rowBytes;	/* one row's samples, without its filter byte */
	uint32_t filterBpp;	/* whole bytes per pixel, at least 1 */
	uint32_t paletteCount;
	uint8_t palette[256][4];	/* RGBA */
	bool hasKey;		/* gray or RGB: tRNS names the one transparent colour */
	uint16_t key[3];
} pngHeader_t;

typedef struct {
	const uint8_t *type;
	const uint8_t *data;
	uint32_t length;
} pngChunk_t;

/* Where the picture lands in the poster, and how much it is reduced first. */
typedef struct {
	float scale;	/* poster pixels per picture pixel */
	float left;	/* the picture's edges in the poster; negative when cropped */
	float top;
	uint32_t reduce;
	uint32_t reducedW;
	uint32_t reducedH;
} pngFit_t;

/* Whether another thread asked the work to stop (stop NULL: never). */
static bool stopped(const volatile bool *stop)
{
	return stop != NULL && *stop;
}

/* zlib's memory comes from the same calloc and free as the rest, so all a
 * poster holds is ui_png's own, and counted as UI_PNG_MAX_WORK. */
static voidpf zlibAlloc(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	return calloc(items, size);
}

static void zlibFree(voidpf opaque, voidpf address)
{
	(void)opaque;
	free(address);
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
		(uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/* The chunk at *offset, inside size and matching its CRC; moves *offset on. */
static bool nextChunk(const uint8_t *png, size_t size, size_t *offset,
	pngChunk_t *chunk)
{
	size_t at = *offset;
	size_t done, piece;
	uint32_t length;
	uLong crc;

	if(at > size || size - at < 12u) {
		return false;
	}
	length = be32(png + at);
	if(length > 0x7FFFFFFFu || (size_t)length > size - at - 12u) {
		return false;
	}
	crc = crc32(0L, Z_NULL, 0);
	for(done = 0u; done < (size_t)length + 4u; done += piece) {
		piece = (size_t)length + 4u - done;
		if(piece > PNG_CRC_PIECE) {
			piece = PNG_CRC_PIECE;
		}
		crc = crc32(crc, png + at + 4u + done, (uInt)piece);
	}
	if((uint32_t)crc != be32(png + at + 8u + length)) {
		return false;
	}
	chunk->type = png + at + 4u;
	chunk->data = png + at + 8u;
	chunk->length = length;
	*offset = at + 12u + length;
	return true;
}

static bool isType(const pngChunk_t *chunk, const char *type)
{
	return memcmp(chunk->type, type, 4) == 0;
}

static bool readHeader(const uint8_t *png, size_t size, pngHeader_t *header,
	size_t *next)
{
	pngChunk_t chunk;
	size_t offset = 8u;
	uint32_t channels;
	uint32_t bits;
	bool depthOk;

	if(png == NULL || size < 8u || memcmp(png, "\211PNG\r\n\032\n", 8) != 0 ||
		!nextChunk(png, size, &offset, &chunk) || !isType(&chunk, "IHDR") ||
		chunk.length != 13u) {
		return false;
	}
	memset(header, 0, sizeof(*header));
	header->width = be32(chunk.data);
	header->height = be32(chunk.data + 4);
	header->depth = chunk.data[8];
	header->colorType = chunk.data[9];
	/* Compression, filter method and interlace: only the one each allows,
	 * and no Adam7. */
	if(header->width == 0u || header->height == 0u ||
		header->width > UI_PNG_MAX_SIDE || header->height > UI_PNG_MAX_SIDE ||
		chunk.data[10] != 0u || chunk.data[11] != 0u || chunk.data[12] != 0u) {
		return false;
	}
	switch(header->colorType) {
		case 0:	/* gray */
			channels = 1u;
			depthOk = header->depth == 1u || header->depth == 2u ||
				header->depth == 4u || header->depth == 8u ||
				header->depth == 16u;
			break;
		case 2:	/* RGB */
			channels = 3u;
			depthOk = header->depth == 8u || header->depth == 16u;
			break;
		case 3:	/* palette */
			channels = 1u;
			depthOk = header->depth == 1u || header->depth == 2u ||
				header->depth == 4u || header->depth == 8u;
			break;
		case 4:	/* gray and alpha */
			channels = 2u;
			depthOk = header->depth == 8u || header->depth == 16u;
			break;
		case 6:	/* RGBA */
			channels = 4u;
			depthOk = header->depth == 8u || header->depth == 16u;
			break;
		default:
			return false;
	}
	if(!depthOk) {
		return false;
	}
	bits = channels * header->depth;
	header->rowBytes = (header->width * bits + 7u) / 8u;
	header->filterBpp = bits >= 8u ? bits / 8u : 1u;
	*next = offset;
	return true;
}

bool UIPng_Info(const uint8_t *png, size_t size, uint32_t *width,
	uint32_t *height)
{
	pngHeader_t header;
	size_t next;

	if(!readHeader(png, size, &header, &next)) {
		return false;
	}
	if(width != NULL) *width = header.width;
	if(height != NULL) *height = header.height;
	return true;
}

/* Walks every chunk after the header: takes the palette and transparency,
 * and finds the image data, which must be one unbroken run of IDATs before
 * IEND. */
static bool readChunks(const uint8_t *png, size_t size, size_t offset,
	pngHeader_t *header, size_t *idatStart, size_t *idatEnd)
{
	pngChunk_t chunk;
	bool seenPalette = false;
	bool seenData = false;
	bool dataEnded = false;
	uint32_t i;

	*idatStart = 0u;
	*idatEnd = 0u;
	for(;;) {
		size_t at = offset;

		if(!nextChunk(png, size, &offset, &chunk)) {
			return false;
		}
		if(isType(&chunk, "IDAT")) {
			if(dataEnded) {
				return false;
			}
			if(!seenData) {
				*idatStart = at;
				seenData = true;
			}
			*idatEnd = offset;
			continue;
		}
		if(seenData) {
			dataEnded = true;
		}
		if(isType(&chunk, "IEND")) {
			break;
		}
		if(isType(&chunk, "PLTE")) {
			if(seenPalette || seenData || chunk.length == 0u ||
				chunk.length % 3u != 0u || chunk.length > 768u) {
				return false;
			}
			seenPalette = true;
			/* A suggested palette for RGB is not needed to read it. */
			if(header->colorType != 3u) {
				continue;
			}
			header->paletteCount = chunk.length / 3u;
			for(i = 0u; i < header->paletteCount; ++i) {
				header->palette[i][0] = chunk.data[i * 3u];
				header->palette[i][1] = chunk.data[i * 3u + 1u];
				header->palette[i][2] = chunk.data[i * 3u + 2u];
				header->palette[i][3] = 255u;
			}
		}
		else if(isType(&chunk, "tRNS")) {
			if(seenData) {
				return false;
			}
			if(header->colorType == 3u) {
				if(!seenPalette || chunk.length > header->paletteCount) {
					return false;
				}
				for(i = 0u; i < chunk.length; ++i) {
					header->palette[i][3] = chunk.data[i];
				}
			}
			else if(header->colorType == 0u && chunk.length == 2u) {
				header->hasKey = true;
				header->key[0] = (uint16_t)(chunk.data[0] << 8 | chunk.data[1]);
			}
			else if(header->colorType == 2u && chunk.length == 6u) {
				header->hasKey = true;
				for(i = 0u; i < 3u; ++i) {
					header->key[i] = (uint16_t)(chunk.data[i * 2u] << 8 |
						chunk.data[i * 2u + 1u]);
				}
			}
			else {
				return false;
			}
		}
		else if((chunk.type[0] & 0x20u) == 0u) {
			/* An unknown critical chunk: the image depends on it. */
			return false;
		}
	}
	return seenData && (header->colorType != 3u || header->paletteCount > 0u);
}

static void fitPicture(uint32_t width, uint32_t height, pngFit_t *fit)
{
	const float posterW = (float)UI_PNG_POSTER_W;
	const float posterH = (float)UI_PNG_POSTER_H;
	float w = (float)width;
	float h = (float)height;
	float aspect = w / h;
	float poster = posterW / posterH;
	float inverse;
	uint32_t reduce;

	if(aspect >= poster * 0.875f && aspect <= poster / 0.875f) {
		/* Poster-shaped: fill it, cropping what is over. */
		fit->scale = fmaxf(posterW / w, posterH / h);
	}
	else {
		fit->scale = fminf((posterW - 2.0f * PNG_FIT_MARGIN) / w,
			(posterH - 2.0f * PNG_FIT_MARGIN) / h);
		if(fit->scale > PNG_MAX_UPSCALE) {
			fit->scale = PNG_MAX_UPSCALE;
		}
	}
	fit->left = (posterW - w * fit->scale) * 0.5f;
	fit->top = (posterH - h * fit->scale) * 0.5f;
	/* Reduce by the whole factor that keeps at least the poster's detail. */
	inverse = 1.0f / fit->scale;
	reduce = inverse >= 2.0f ? (uint32_t)inverse : 1u;
	for(;;) {
		fit->reduce = reduce;
		fit->reducedW = (width + reduce - 1u) / reduce;
		fit->reducedH = (height + reduce - 1u) / reduce;
		if(fit->reducedW * fit->reducedH * 4u <= PNG_MAX_REDUCED) {
			break;
		}
		reduce++;
	}
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
	int p = (int)a + (int)b - (int)c;
	int pa = abs(p - (int)a);
	int pb = abs(p - (int)b);
	int pc = abs(p - (int)c);

	if(pa <= pb && pa <= pc) return a;
	return pb <= pc ? b : c;
}

static bool unfilter(uint8_t *row, const uint8_t *prev, uint32_t length,
	uint32_t bpp, uint8_t filter)
{
	uint32_t i;

	switch(filter) {
		case 0:
			return true;
		case 1:
			for(i = bpp; i < length; ++i)
				row[i] = (uint8_t)(row[i] + row[i - bpp]);
			return true;
		case 2:
			for(i = 0u; i < length; ++i)
				row[i] = (uint8_t)(row[i] + prev[i]);
			return true;
		case 3:
			for(i = 0u; i < length; ++i) {
				uint32_t left = i >= bpp ? row[i - bpp] : 0u;
				row[i] = (uint8_t)(row[i] + (uint8_t)((left + prev[i]) / 2u));
			}
			return true;
		case 4:
			for(i = 0u; i < length; ++i) {
				uint8_t left = i >= bpp ? row[i - bpp] : 0u;
				uint8_t corner = i >= bpp ? prev[i - bpp] : 0u;
				row[i] = (uint8_t)(row[i] + paeth(left, prev[i], corner));
			}
			return true;
		default:
			return false;
	}
}

/* Sample x of a row at depth bits per sample (1, 2, 4, 8 or 16). */
static uint32_t sampleAt(const uint8_t *row, uint32_t index, uint8_t depth)
{
	if(depth == 16u) {
		return (uint32_t)row[index * 2u] << 8 | row[index * 2u + 1u];
	}
	if(depth == 8u) {
		return row[index];
	}
	{
		uint32_t bit = index * depth;
		uint32_t shift = 8u - depth - (bit & 7u);
		return (uint32_t)(row[bit >> 3] >> shift) & ((1u << depth) - 1u);
	}
}

static uint8_t to8(uint32_t sample, uint8_t depth)
{
	if(depth == 16u) return (uint8_t)(sample >> 8);
	if(depth == 8u) return (uint8_t)sample;
	return (uint8_t)(sample * 255u / ((1u << depth) - 1u));
}

/* One unfiltered row as RGBA. False for a palette index past the palette. */
static bool expandRow(const pngHeader_t *header, const uint8_t *row,
	uint8_t *rgba)
{
	uint32_t x;

	for(x = 0u; x < header->width; ++x) {
		uint8_t *out = rgba + x * 4u;

		switch(header->colorType) {
			case 0: {
				uint32_t gray = sampleAt(row, x, header->depth);
				out[0] = out[1] = out[2] = to8(gray, header->depth);
				out[3] = (uint8_t)(header->hasKey && gray == header->key[0] ?
					0u : 255u);
				break;
			}
			case 2: {
				uint32_t r = sampleAt(row, x * 3u, header->depth);
				uint32_t g = sampleAt(row, x * 3u + 1u, header->depth);
				uint32_t b = sampleAt(row, x * 3u + 2u, header->depth);
				out[0] = to8(r, header->depth);
				out[1] = to8(g, header->depth);
				out[2] = to8(b, header->depth);
				out[3] = (uint8_t)(header->hasKey && r == header->key[0] &&
					g == header->key[1] && b == header->key[2] ? 0u : 255u);
				break;
			}
			case 3: {
				uint32_t index = sampleAt(row, x, header->depth);
				if(index >= header->paletteCount) {
					return false;
				}
				memcpy(out, header->palette[index], 4);
				break;
			}
			case 4:
				out[0] = out[1] = out[2] =
					to8(sampleAt(row, x * 2u, header->depth), header->depth);
				out[3] = to8(sampleAt(row, x * 2u + 1u, header->depth),
					header->depth);
				break;
			default:
				out[0] = to8(sampleAt(row, x * 4u, header->depth), header->depth);
				out[1] = to8(sampleAt(row, x * 4u + 1u, header->depth),
					header->depth);
				out[2] = to8(sampleAt(row, x * 4u + 2u, header->depth),
					header->depth);
				out[3] = to8(sampleAt(row, x * 4u + 3u, header->depth),
					header->depth);
				break;
		}
	}
	return true;
}

/* The reduction: sums of premultiplied colour, alpha and pixels per reduced
 * column, over the source rows of the reduced row being made. */
typedef struct {
	uint32_t r, g, b, a, n;
} pngSum_t;

static void addRow(const uint8_t *rgba, uint32_t width, uint32_t reduce,
	pngSum_t *sums)
{
	uint32_t x;

	for(x = 0u; x < width; ++x) {
		const uint8_t *p = rgba + x * 4u;
		pngSum_t *sum = &sums[x / reduce];
		uint32_t alpha = p[3];

		sum->r += p[0] * alpha;
		sum->g += p[1] * alpha;
		sum->b += p[2] * alpha;
		sum->a += alpha;
		sum->n++;
	}
}

/* A reduced row, premultiplied RGBA, from the sums; clears them. */
static void emitRow(pngSum_t *sums, uint32_t reducedW, uint8_t *out)
{
	uint32_t x;

	for(x = 0u; x < reducedW; ++x) {
		pngSum_t *sum = &sums[x];
		uint32_t n = sum->n ? sum->n : 1u;
		uint32_t alpha = (sum->a + n / 2u) / n;
		uint32_t whole = n * 255u;
		uint32_t c[3];
		uint32_t k;

		c[0] = (sum->r + whole / 2u) / whole;
		c[1] = (sum->g + whole / 2u) / whole;
		c[2] = (sum->b + whole / 2u) / whole;
		for(k = 0u; k < 3u; ++k) {
			out[x * 4u + k] = (uint8_t)(c[k] > alpha ? alpha : c[k]);
		}
		out[x * 4u + 3u] = (uint8_t)alpha;
		memset(sum, 0, sizeof(*sum));
	}
}

/* Inflates the IDAT run into rows and reduces them into reduced. */
static bool decodeRows(const uint8_t *png, size_t size, size_t idatStart,
	size_t idatEnd, const pngHeader_t *header, const pngFit_t *fit,
	uint8_t *reduced, const volatile bool *stop)
{
	uint32_t stride = header->rowBytes + 1u;
	uint8_t *current = calloc(stride, 1u);
	uint8_t *previous = calloc(stride, 1u);
	uint8_t *rgba = malloc((size_t)header->width * 4u);
	pngSum_t *sums = calloc(fit->reducedW, sizeof(pngSum_t));
	z_stream zs;
	size_t at = idatStart;
	uint32_t filled = 0u;
	uint32_t rows = 0u;
	uint32_t stalls = 0u;
	bool ok = false;
	bool streamOpen = false;

	memset(&zs, 0, sizeof(zs));
	zs.zalloc = zlibAlloc;
	zs.zfree = zlibFree;
	if(current == NULL || previous == NULL || rgba == NULL || sums == NULL ||
		inflateInit(&zs) != Z_OK) {
		goto done;
	}
	streamOpen = true;
	while(rows < header->height) {
		uint32_t before, beforeIn;
		int result;

		if(zs.avail_in == 0u) {
			pngChunk_t chunk;

			if(at >= idatEnd || !nextChunk(png, size, &at, &chunk)) {
				goto done;	/* the data ran out before the last row */
			}
			zs.next_in = (Bytef *)(uintptr_t)chunk.data;
			zs.avail_in = chunk.length;
			continue;
		}
		zs.next_out = current + filled;
		zs.avail_out = stride - filled;
		before = zs.avail_out;
		beforeIn = zs.avail_in;
		result = inflate(&zs, Z_NO_FLUSH);
		filled += before - zs.avail_out;
		if(filled == stride) {
			uint8_t *swap;

			if(!unfilter(current + 1, previous + 1, header->rowBytes,
				header->filterBpp, current[0]) ||
				!expandRow(header, current + 1, rgba)) {
				goto done;
			}
			addRow(rgba, header->width, fit->reduce, sums);
			rows++;
			if(rows % fit->reduce == 0u || rows == header->height) {
				emitRow(sums, fit->reducedW, reduced +
					(size_t)((rows - 1u) / fit->reduce) * fit->reducedW * 4u);
			}
			swap = previous;
			previous = current;
			current = swap;
			filled = 0u;
			stalls = 0u;
			if(stopped(stop)) {
				goto done;
			}
			continue;
		}
		if(result == Z_STREAM_END) {
			goto done;	/* the stream ended before the last row */
		}
		if(result != Z_OK && result != Z_BUF_ERROR) {
			goto done;
		}
		/* Neither read nor wrote: never loop on it. */
		if(before == zs.avail_out && beforeIn == zs.avail_in &&
			++stalls > 4u) {
			goto done;
		}
	}
	ok = true;
done:
	if(streamOpen) {
		inflateEnd(&zs);
	}
	free(current);
	free(previous);
	free(rgba);
	free(sums);
	return ok;
}

/* Premultiplied RGBA at (u, v) in reduced pixels, pixel centres on whole
 * numbers, edges repeated. */
static void sampleReduced(const uint8_t *reduced, uint32_t w, uint32_t h,
	float u, float v, float out[4])
{
	float maxU = (float)(w - 1u);
	float maxV = (float)(h - 1u);
	uint32_t x0, y0, x1, y1, k;
	float tx, ty;

	u = u < 0.0f ? 0.0f : (u > maxU ? maxU : u);
	v = v < 0.0f ? 0.0f : (v > maxV ? maxV : v);
	x0 = (uint32_t)u;
	y0 = (uint32_t)v;
	x1 = x0 + 1u < w ? x0 + 1u : x0;
	y1 = y0 + 1u < h ? y0 + 1u : y0;
	tx = u - (float)x0;
	ty = v - (float)y0;
	for(k = 0u; k < 4u; ++k) {
		float top = (float)reduced[(y0 * w + x0) * 4u + k] * (1.0f - tx) +
			(float)reduced[(y0 * w + x1) * 4u + k] * tx;
		float bottom = (float)reduced[(y1 * w + x0) * 4u + k] * (1.0f - tx) +
			(float)reduced[(y1 * w + x1) * 4u + k] * tx;
		out[k] = top * (1.0f - ty) + bottom * ty;
	}
}

static uint8_t clampByte(float value)
{
	if(!(value > 0.0f)) return 0u;
	if(value >= 255.0f) return 255u;
	return (uint8_t)(value + 0.5f);
}

/* Row y of a backdrop in colour: the colour deepened towards Indigo's
 * night blue, lighter at the top. */
static void backdropRow(const float colour[3], uint32_t y, float back[3])
{
	static const float night[3] = {16.0f, 12.0f, 40.0f};
	float down = (float)y / (float)(UI_PNG_POSTER_H - 1u);
	float tint = 0.42f - 0.26f * down;
	uint32_t k;

	for(k = 0u; k < 3u; ++k) {
		back[k] = colour[k] * tint + night[k] * (1.0f - tint);
	}
}

/* The right band of a canvas row: its last poster column, repeated. */
static void repeatLastColumn(uint8_t *line)
{
	uint32_t x;

	for(x = UI_PNG_POSTER_W; x < UI_PNG_CANVAS; ++x) {
		memcpy(line + x * 3u, line + (UI_PNG_POSTER_W - 1u) * 3u, 3);
	}
}

/* The poster canvas, 256x256 RGB: the picture over its backdrop in the left
 * 192 columns, and the last of them repeated across the rest. */
static bool composeCanvas(const uint8_t *reduced, const pngFit_t *fit,
	uint32_t width, uint32_t height, uint8_t *canvas,
	const volatile bool *stop)
{
	/* The backdrop is in the picture's own average colour. */
	float mean[3] = {96.0f, 80.0f, 170.0f};
	float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float scale = fit->scale * (float)fit->reduce;
	uint32_t count = fit->reducedW * fit->reducedH;
	uint32_t i, x, y, k;

	for(i = 0u; i < count; ++i) {
		for(k = 0u; k < 4u; ++k) {
			sum[k] += (float)reduced[i * 4u + k];
		}
	}
	if(sum[3] > 0.5f) {
		for(k = 0u; k < 3u; ++k) {
			mean[k] = sum[k] * 255.0f / sum[3];
		}
	}
	for(y = 0u; y < UI_PNG_POSTER_H; ++y) {
		float back[3];
		float v = ((float)y + 0.5f - fit->top) / fit->scale;
		uint8_t *line = canvas + y * UI_PNG_CANVAS * 3u;

		if(stopped(stop)) {
			return false;
		}
		backdropRow(mean, y, back);
		for(x = 0u; x < UI_PNG_POSTER_W; ++x) {
			float u = ((float)x + 0.5f - fit->left) / fit->scale;
			float pixel[4] = {0.0f, 0.0f, 0.0f, 0.0f};

			if(u >= 0.0f && u < (float)width && v >= 0.0f &&
				v < (float)height) {
				sampleReduced(reduced, fit->reducedW, fit->reducedH,
					((float)x + 0.5f - fit->left) / scale - 0.5f,
					((float)y + 0.5f - fit->top) / scale - 0.5f, pixel);
			}
			for(k = 0u; k < 3u; ++k) {
				line[x * 3u + k] = clampByte(pixel[k] +
					back[k] * (1.0f - pixel[3] / 255.0f));
			}
		}
		repeatLastColumn(line);
	}
	return true;
}

static void halve(const uint8_t *from, uint32_t size, uint8_t *to)
{
	uint32_t half = size / 2u;
	uint32_t x, y, k;

	for(y = 0u; y < half; ++y) {
		for(x = 0u; x < half; ++x) {
			for(k = 0u; k < 3u; ++k) {
				uint32_t a = from[((y * 2u) * size + x * 2u) * 3u + k];
				uint32_t b = from[((y * 2u) * size + x * 2u + 1u) * 3u + k];
				uint32_t c = from[((y * 2u + 1u) * size + x * 2u) * 3u + k];
				uint32_t d = from[((y * 2u + 1u) * size + x * 2u + 1u) * 3u + k];
				to[(y * half + x) * 3u + k] = (uint8_t)((a + b + c + d + 2u) / 4u);
			}
		}
	}
}

static uint16_t pack565(const float color[3])
{
	static const float top[3] = {31.0f, 63.0f, 31.0f};
	uint32_t v[3];
	uint32_t k;

	for(k = 0u; k < 3u; ++k) {
		float c = !(color[k] > 0.0f) ? 0.0f :
			(color[k] > 255.0f ? 255.0f : color[k]);

		v[k] = (uint32_t)(c * top[k] / 255.0f + 0.5f);
	}
	return (uint16_t)(v[0] << 11 | v[1] << 5 | v[2]);
}

/* As the hardware widens a 565 colour. */
static void unpack565(uint16_t value, uint32_t out[3])
{
	uint32_t r = (uint32_t)(value >> 11) & 31u;
	uint32_t g = (uint32_t)(value >> 5) & 63u;
	uint32_t b = (uint32_t)value & 31u;

	out[0] = r << 3 | r >> 2;
	out[1] = g << 2 | g >> 4;
	out[2] = b << 3 | b >> 2;
}

/* One DXT1 block of 16 opaque pixels: the ends of the colours' principal
 * axis, and each pixel's nearest of the four colours the GameCube blends
 * from them (5/8 and 3/8, not thirds). A poster is 1364 blocks, and the
 * GameCube converts integers to floats slowly and has no square root, so
 * the statistics are integers and the axis is found without a length. */
static void encodeBlock(const uint8_t pixels[16][3], uint8_t out[8])
{
	int32_t sum[3] = {0, 0, 0};
	int32_t dev[16][3];	/* 16 times each pixel's distance from the mean */
	int32_t cov[3][3] = {{0}};
	int32_t step[3];
	int32_t low = 0, high = 0;
	float axis[3] = {1.0f, 1.0f, 1.0f};
	float c[3][3];
	float ends[2][3];
	float scale;
	uint32_t palette[4][3];
	uint16_t c0, c1;
	uint32_t i, j, k, iteration;

	for(i = 0u; i < 16u; ++i)
		for(k = 0u; k < 3u; ++k)
			sum[k] += pixels[i][k];
	for(i = 0u; i < 16u; ++i)
		for(k = 0u; k < 3u; ++k)
			dev[i][k] = (int32_t)pixels[i][k] * 16 - sum[k];
	/* At most 16 * 4080 * 4080: well inside 32 bits. */
	for(i = 0u; i < 16u; ++i)
		for(j = 0u; j < 3u; ++j)
			for(k = j; k < 3u; ++k)
				cov[j][k] += dev[i][j] * dev[i][k];
	for(j = 0u; j < 3u; ++j)
		for(k = 0u; k < 3u; ++k)
			c[j][k] = (float)(j <= k ? cov[j][k] : cov[k][j]);
	/* Power iteration, each step scaled so its largest part is 1. */
	for(iteration = 0u; iteration < 4u; ++iteration) {
		float next[3];
		float largest = 0.0f;

		for(j = 0u; j < 3u; ++j) {
			float size;

			next[j] = c[j][0] * axis[0] + c[j][1] * axis[1] + c[j][2] * axis[2];
			size = next[j] < 0.0f ? -next[j] : next[j];
			if(size > largest) largest = size;
		}
		if(!(largest > 1e-3f)) break;
		for(j = 0u; j < 3u; ++j) axis[j] = next[j] / largest;
	}
	for(k = 0u; k < 3u; ++k)
		step[k] = (int32_t)(axis[k] * 1024.0f);
	for(i = 0u; i < 16u; ++i) {
		int32_t t = dev[i][0] * step[0] + dev[i][1] * step[1] +
			dev[i][2] * step[2];

		if(i == 0u || t < low) low = t;
		if(i == 0u || t > high) high = t;
	}
	/* t is 16 * 1024 times the distance along the axis, times its length
	 * squared: back to colours on the line through the mean. */
	scale = 1.0f / (16384.0f * (axis[0] * axis[0] + axis[1] * axis[1] +
		axis[2] * axis[2]));
	for(k = 0u; k < 3u; ++k) {
		float mean = (float)sum[k] * 0.0625f;

		ends[0][k] = mean + axis[k] * (float)high * scale;
		ends[1][k] = mean + axis[k] * (float)low * scale;
	}
	c0 = pack565(ends[0]);
	c1 = pack565(ends[1]);
	if(c0 < c1) {
		uint16_t swap = c0;
		c0 = c1;
		c1 = swap;
	}
	out[0] = (uint8_t)(c0 >> 8);
	out[1] = (uint8_t)c0;
	out[2] = (uint8_t)(c1 >> 8);
	out[3] = (uint8_t)c1;
	/* Equal ends select three colours and transparency: index 0 is still
	 * the one colour. */
	unpack565(c0, palette[0]);
	unpack565(c1, palette[1]);
	for(k = 0u; k < 3u; ++k) {
		palette[2][k] = (palette[0][k] * 5u + palette[1][k] * 3u) >> 3;
		palette[3][k] = (palette[0][k] * 3u + palette[1][k] * 5u) >> 3;
	}
	for(j = 0u; j < 4u; ++j) {
		uint8_t line = 0u;

		for(i = 0u; i < 4u; ++i) {
			const uint8_t *pixel = pixels[j * 4u + i];
			uint32_t best = 0u;
			int bestDistance = -1;
			uint32_t p;

			for(p = 0u; p < (c0 == c1 ? 1u : 4u); ++p) {
				int distance = 0;
				for(k = 0u; k < 3u; ++k) {
					int d = (int)pixel[k] - (int)palette[p][k];
					distance += d * d;
				}
				if(bestDistance < 0 || distance < bestDistance) {
					bestDistance = distance;
					best = p;
				}
			}
			line = (uint8_t)(line | best << (6u - i * 2u));
		}
		out[4u + j] = line;
	}
}

static bool encodeCmpr(const uint8_t *rgb, uint32_t size, uint8_t *out,
	const volatile bool *stop)
{
	uint32_t tileX, tileY, block, y, x;

	for(tileY = 0u; tileY < size; tileY += 8u) {
		if(stopped(stop)) {
			return false;
		}
		for(tileX = 0u; tileX < size; tileX += 8u) {
			for(block = 0u; block < 4u; ++block) {
				uint8_t pixels[16][3];
				uint32_t left = tileX + (block & 1u) * 4u;
				uint32_t top = tileY + (block >> 1) * 4u;

				for(y = 0u; y < 4u; ++y)
					for(x = 0u; x < 4u; ++x)
						memcpy(pixels[y * 4u + x],
							rgb + ((top + y) * size + left + x) * 3u, 3);
				encodeBlock((const uint8_t (*)[3])pixels, out);
				out += 8;
			}
		}
	}
	return true;
}

void UIPng_EncodeCmpr(const uint8_t *rgb, uint32_t size, uint8_t *out)
{
	(void)encodeCmpr(rgb, size, out, NULL);
}

/* The poster from its canvas (levels[0], 256x256 RGB): 256, 128, 64, 32
 * and 16, each level halved from the one before into the other buffer
 * (levels[1], 128x128 RGB), and each encoded into out. */
static bool encodeLevels(uint8_t *levels[2], uint8_t *out,
	const volatile bool *stop)
{
	uint32_t level, side;

	for(level = 0u, side = UI_PNG_CANVAS; level < PNG_MIP_LEVELS;
		++level, side /= 2u) {
		uint8_t *from = levels[level & 1u];

		if(!encodeCmpr(from, side, out, stop)) {
			return false;
		}
		out += side * side / 2u;
		if(level + 1u < PNG_MIP_LEVELS) {
			halve(from, side, levels[(level + 1u) & 1u]);
		}
	}
	return true;
}

/* -- a poster of a name ---------------------------------------------------- */

#define NAME_MAX_CHARS 63u
#define NAME_MAX_LINES 4u
#define NAME_BOX_W 164.0f	/* the widest line, in poster pixels */
#define NAME_BOX_H 200.0f	/* every line together */
#define NAME_SCALE_MAX 3.0f	/* poster pixels a font pixel */
#define NAME_SCALE_MIN 1.0f
#define NAME_LINE_GAP 0.1f	/* between lines, in lines */
#define NAME_RISE 10.0f		/* how far above the middle the text sits */
#define NAME_BOLD 1.25f		/* the second pass's offset, in poster pixels */
#define NAME_SHADOW 2u		/* the shadow's offset, in poster pixels */

typedef struct {
	uint8_t coverage[UI_PNG_GLYPH_MAX * UI_PNG_GLYPH_MAX];
	int width;	/* 0 not asked yet, -1 none */
} nameGlyph_t;

typedef struct {
	char text[NAME_MAX_LINES][NAME_MAX_CHARS + 2u];
	uint32_t count;
} nameLines_t;

/* name's words, uppercased, one space between: a word ends at a space, '-',
 * '_' or '.', and a '+' starts one of its own ("OSSC +CARBY"). */
static size_t nameWords(const char *name, char text[NAME_MAX_CHARS + 1u])
{
	size_t n = 0u;
	bool gap = false;

	for(; *name != '\0'; ++name) {
		unsigned char c = (unsigned char)*name;

		if(c == ' ' || c == '-' || c == '_' || c == '.') {
			gap = n > 0u;
			continue;
		}
		if(c == '+' && n > 0u) {
			gap = true;
		}
		if(n + (gap ? 2u : 1u) > NAME_MAX_CHARS) {
			break;
		}
		if(gap) {
			text[n++] = ' ';
			gap = false;
		}
		text[n++] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
	}
	text[n] = '\0';
	return n;
}

static nameGlyph_t *glyphOf(const uiPngFont_t *font, nameGlyph_t *glyphs,
	unsigned char c)
{
	nameGlyph_t *glyph = &glyphs[c];

	if(glyph->width == 0) {
		int width = 0;

		memset(glyph->coverage, 0, sizeof(glyph->coverage));
		glyph->width = font->glyph(font->context, c, glyph->coverage, &width) &&
			width >= 1 && width <= UI_PNG_GLYPH_MAX ? width : -1;
	}
	return glyph->width > 0 ? glyph : NULL;
}

/* How far the pen moves past c: neighbours overlap by a column. */
static int advanceOf(const uiPngFont_t *font, nameGlyph_t *glyphs,
	unsigned char c)
{
	const nameGlyph_t *glyph = glyphOf(font, glyphs, c);

	return glyph != NULL ? glyph->width - 1 : 0;
}

static int widthOf(const uiPngFont_t *font, nameGlyph_t *glyphs,
	const char *text, size_t length)
{
	int width = 0;
	size_t i;

	for(i = 0u; i < length; ++i) {
		width += advanceOf(font, glyphs, (unsigned char)text[i]);
	}
	return width;
}

/* text's words in lines at most maxWidth font pixels wide, each line as
 * many whole words as fit. False when a word alone is wider or the words
 * take more than NAME_MAX_LINES lines. */
static bool wrapWords(const uiPngFont_t *font, nameGlyph_t *glyphs,
	const char *text, size_t length, int maxWidth, nameLines_t *lines)
{
	int space = advanceOf(font, glyphs, ' ');
	size_t at = 0u;

	lines->count = 0u;
	while(at < length) {
		size_t lineStart = at, lineEnd = at;
		int width = 0;

		while(at < length) {
			size_t end = at;
			int word;

			while(end < length && text[end] != ' ') ++end;
			word = widthOf(font, glyphs, text + at, end - at);
			if(lineEnd > lineStart) word += space;
			if(width + word > maxWidth) break;
			width += word;
			lineEnd = end;
			at = end < length ? end + 1u : end;
		}
		if(lineEnd == lineStart || lines->count == NAME_MAX_LINES) {
			return false;
		}
		memcpy(lines->text[lines->count], text + lineStart, lineEnd - lineStart);
		lines->text[lines->count][lineEnd - lineStart] = '\0';
		lines->count++;
	}
	return lines->count > 0u;
}

/* text in lines at most maxWidth font pixels wide, broken anywhere; what
 * doesn't fit in NAME_MAX_LINES lines ends in an ellipsis. */
static void wrapLetters(const uiPngFont_t *font, nameGlyph_t *glyphs,
	const char *text, size_t length, int maxWidth, nameLines_t *lines)
{
	int dots = advanceOf(font, glyphs, 0x85u);
	size_t at = 0u;

	lines->count = 0u;
	while(at < length && lines->count < NAME_MAX_LINES) {
		char *line = lines->text[lines->count];
		bool last = lines->count + 1u == NAME_MAX_LINES;
		size_t n = 0u;
		int width = 0;

		while(at < length && text[at] == ' ') ++at;
		while(at < length) {
			int next = advanceOf(font, glyphs, (unsigned char)text[at]);

			if(n > 0u && width + next > maxWidth) break;
			line[n++] = text[at++];
			width += next;
		}
		if(last && at < length) {
			/* The rest doesn't fit: make room for the ellipsis. */
			while(n > 0u && width + dots > maxWidth) {
				width -= advanceOf(font, glyphs, (unsigned char)line[--n]);
			}
			line[n++] = (char)0x85;
		}
		line[n] = '\0';
		if(n > 0u) lines->count++;
	}
}

/* A glyph's coverage at (u, v) in its own pixels, bilinear, 0 outside. u
 * and v are above -8 (inkLines keeps them so), so truncating after adding
 * 8 is their floor: floorf is a library call on the GameCube. */
static float glyphAt(const nameGlyph_t *glyph, int cellHeight, float u,
	float v)
{
	int x0 = (int)(u + 8.0f) - 8, y0 = (int)(v + 8.0f) - 8;
	float tx = u - (float)x0, ty = v - (float)y0;
	float sum = 0.0f;
	int dx, dy;

	for(dy = 0; dy < 2; ++dy) {
		for(dx = 0; dx < 2; ++dx) {
			int x = x0 + dx, y = y0 + dy;
			float weight = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty);

			if(x >= 0 && x < glyph->width && y >= 0 && y < cellHeight) {
				sum += weight * (float)glyph->coverage[y * UI_PNG_GLYPH_MAX + x];
			}
		}
	}
	return sum;
}

/* The lines' coverage in ink (the poster, 192x256), scale poster pixels a
 * font pixel, each line centred, the block a little above the middle. */
static void inkLines(const uiPngFont_t *font, nameGlyph_t *glyphs,
	const nameLines_t *lines, float scale, uint8_t *ink)
{
	float lineHeight = (float)font->cellHeight * scale;
	float step = lineHeight * (1.0f + NAME_LINE_GAP);
	float block = lineHeight + step * (float)(lines->count - 1u);
	float top = ((float)UI_PNG_POSTER_H - block) * 0.5f - NAME_RISE;
	uint32_t l;

	for(l = 0u; l < lines->count; ++l) {
		const char *text = lines->text[l];
		size_t length = strlen(text);
		float pen = ((float)UI_PNG_POSTER_W -
			(float)widthOf(font, glyphs, text, length) * scale) * 0.5f;
		float lineTop = top + step * (float)l;
		size_t i;

		for(i = 0u; i < length; ++i) {
			const nameGlyph_t *glyph = glyphOf(font, glyphs,
				(unsigned char)text[i]);
			int x0, x1, y0, y1, x, y;

			if(glyph == NULL) continue;
			x0 = (int)floorf(pen);
			x1 = (int)ceilf(pen + ((float)glyph->width + 1.0f) * scale);
			y0 = (int)floorf(lineTop);
			y1 = (int)ceilf(lineTop + lineHeight);
			if(x0 < 0) x0 = 0;
			if(y0 < 0) y0 = 0;
			if(x1 > (int)UI_PNG_POSTER_W) x1 = (int)UI_PNG_POSTER_W;
			if(y1 > (int)UI_PNG_POSTER_H) y1 = (int)UI_PNG_POSTER_H;
			for(y = y0; y < y1; ++y) {
				float v = ((float)y + 0.5f - lineTop) / scale - 0.5f;

				for(x = x0; x < x1; ++x) {
					float u = ((float)x + 0.5f - pen) / scale - 0.5f;
					/* Two passes a little apart, as drawStringMedium
					 * sets a medium weight. */
					float c = glyphAt(glyph, font->cellHeight, u, v);
					float bold = glyphAt(glyph, font->cellHeight,
						u - NAME_BOLD / scale, v);
					uint8_t *dot = &ink[(size_t)y * UI_PNG_POSTER_W + (size_t)x];

					if(bold > c) c = bold;
					if(c > (float)*dot) *dot = clampByte(c);
				}
			}
			pen += (float)(glyph->width - 1) * scale;
		}
	}
}

/* The backdrop's colour for a name: a hue from its first word, the
 * saturation and brightness of the Library's own colours. */
static void nameColour(const char *text, float colour[3])
{
	uint32_t hash = 2166136261u;
	float h, f;
	float v = 230.0f, p = v * 0.45f;
	int sector;

	for(; *text != '\0' && *text != ' '; ++text) {
		hash = (hash ^ (uint8_t)*text) * 16777619u;
	}
	h = (float)(hash % 360u) / 60.0f;
	sector = (int)h;
	f = h - (float)sector;
	switch(sector) {
		case 0: colour[0] = v; colour[1] = p + (v - p) * f; colour[2] = p; break;
		case 1: colour[0] = v - (v - p) * f; colour[1] = v; colour[2] = p; break;
		case 2: colour[0] = p; colour[1] = v; colour[2] = p + (v - p) * f; break;
		case 3: colour[0] = p; colour[1] = v - (v - p) * f; colour[2] = v; break;
		case 4: colour[0] = p + (v - p) * f; colour[1] = p; colour[2] = v; break;
		default: colour[0] = v; colour[1] = p; colour[2] = v - (v - p) * f; break;
	}
}

bool UIPng_NamePoster(const char *name, const uiPngFont_t *font, uint8_t *out)
{
	return UIPng_NamePosterUntil(name, font, out, NULL);
}

bool UIPng_NamePosterUntil(const char *name, const uiPngFont_t *font,
	uint8_t *out, const volatile bool *stop)
{
	static const float letters[3] = {242.0f, 238.0f, 255.0f};
	nameGlyph_t *glyphs = NULL;
	nameLines_t lines;
	uint8_t *ink = NULL;
	uint8_t *levels[2] = {NULL, NULL};
	char text[NAME_MAX_CHARS + 1u];
	size_t length;
	float colour[3];
	float scale;
	uint32_t x, y, k;
	bool ok = false;

	if(name == NULL || font == NULL || font->glyph == NULL || out == NULL ||
		font->cellHeight < 1 || font->cellHeight > UI_PNG_GLYPH_MAX ||
		(length = nameWords(name, text)) == 0u) {
		return false;
	}
	glyphs = calloc(256u, sizeof(nameGlyph_t));
	ink = calloc(UI_PNG_POSTER_W * UI_PNG_POSTER_H, 1u);
	levels[0] = malloc(UI_PNG_CANVAS * UI_PNG_CANVAS * 3u);
	levels[1] = malloc((UI_PNG_CANVAS / 2u) * (UI_PNG_CANVAS / 2u) * 3u);
	if(glyphs == NULL || ink == NULL || levels[0] == NULL || levels[1] == NULL) {
		goto done;
	}
	/* The largest size at which the words fit their lines; failing that,
	 * the smallest, broken anywhere. */
	for(scale = NAME_SCALE_MAX; scale >= NAME_SCALE_MIN - 0.001f; scale -= 0.05f) {
		if(wrapWords(font, glyphs, text, length, (int)(NAME_BOX_W / scale),
			&lines) && (float)font->cellHeight * scale *
			((float)lines.count + NAME_LINE_GAP * (float)(lines.count - 1u)) <=
			NAME_BOX_H) {
			break;
		}
	}
	if(scale < NAME_SCALE_MIN - 0.001f) {
		scale = NAME_SCALE_MIN;
		wrapLetters(font, glyphs, text, length, (int)(NAME_BOX_W / scale),
			&lines);
	}
	inkLines(font, glyphs, &lines, scale, ink);
	/* A name of letters the font lacks draws nothing: no poster. */
	for(x = 0u; x < UI_PNG_POSTER_W * UI_PNG_POSTER_H && ink[x] == 0u; ++x) {
	}
	if(x == UI_PNG_POSTER_W * UI_PNG_POSTER_H) {
		goto done;
	}
	nameColour(text, colour);
	for(y = 0u; y < UI_PNG_POSTER_H; ++y) {
		uint8_t *line = levels[0] + y * UI_PNG_CANVAS * 3u;
		float back[3];

		backdropRow(colour, y, back);
		for(x = 0u; x < UI_PNG_POSTER_W; ++x) {
			float a = (float)ink[y * UI_PNG_POSTER_W + x] / 255.0f;
			float shade = x >= NAME_SHADOW && y >= NAME_SHADOW ?
				0.5f * (float)ink[(y - NAME_SHADOW) * UI_PNG_POSTER_W +
				x - NAME_SHADOW] / 255.0f : 0.0f;

			for(k = 0u; k < 3u; ++k) {
				line[x * 3u + k] = clampByte(back[k] * (1.0f - shade) *
					(1.0f - a) + letters[k] * a);
			}
		}
		repeatLastColumn(line);
	}
	ok = encodeLevels(levels, out, stop);
done:
	free(glyphs);
	free(ink);
	free(levels[0]);
	free(levels[1]);
	return ok;
}

bool UIPng_Poster(const uint8_t *png, size_t size, uint8_t *out)
{
	return UIPng_PosterUntil(png, size, out, NULL);
}

bool UIPng_PosterUntil(const uint8_t *png, size_t size, uint8_t *out,
	const volatile bool *stop)
{
	pngHeader_t *header;
	uint8_t *reduced = NULL;
	uint8_t *levels[2] = {NULL, NULL};
	size_t next, idatStart, idatEnd;
	pngFit_t fit;
	bool ok = false;

	if(size > UI_PNG_MAX_FILE) {
		return false;
	}
	header = malloc(sizeof(pngHeader_t));
	if(header == NULL || out == NULL || !readHeader(png, size, header, &next) ||
		!readChunks(png, size, next, header, &idatStart, &idatEnd)) {
		goto done;
	}
	fitPicture(header->width, header->height, &fit);
	reduced = malloc((size_t)fit.reducedW * fit.reducedH * 4u);
	levels[0] = malloc(UI_PNG_CANVAS * UI_PNG_CANVAS * 3u);
	levels[1] = malloc((UI_PNG_CANVAS / 2u) * (UI_PNG_CANVAS / 2u) * 3u);
	if(reduced == NULL || levels[0] == NULL || levels[1] == NULL ||
		!decodeRows(png, size, idatStart, idatEnd, header, &fit, reduced, stop)) {
		goto done;
	}
	ok = composeCanvas(reduced, &fit, header->width, header->height,
		levels[0], stop) && encodeLevels(levels, out, stop);
done:
	free(header);
	free(reduced);
	free(levels[0]);
	free(levels[1]);
	return ok;
}

bool UIPng_RgbaPoster(const uint8_t *rgba, uint32_t width, uint32_t height,
	uint8_t *out)
{
	return UIPng_RgbaPosterUntil(rgba, width, height, out, NULL);
}

bool UIPng_RgbaPosterUntil(const uint8_t *rgba, uint32_t width, uint32_t height,
	uint8_t *out, const volatile bool *stop)
{
	uint8_t *reduced = NULL;
	uint8_t *levels[2] = {NULL, NULL};
	pngSum_t *sums = NULL;
	pngFit_t fit;
	uint32_t y;
	bool ok = false;

	if(rgba == NULL || out == NULL || width == 0u || height == 0u ||
		width > UI_PNG_MAX_SIDE || height > UI_PNG_MAX_SIDE) {
		return false;
	}
	fitPicture(width, height, &fit);
	reduced = malloc((size_t)fit.reducedW * fit.reducedH * 4u);
	levels[0] = malloc(UI_PNG_CANVAS * UI_PNG_CANVAS * 3u);
	levels[1] = malloc((UI_PNG_CANVAS / 2u) * (UI_PNG_CANVAS / 2u) * 3u);
	sums = calloc(fit.reducedW, sizeof(pngSum_t));
	if(reduced == NULL || levels[0] == NULL || levels[1] == NULL ||
		sums == NULL) {
		goto done;
	}
	for(y = 0u; y < height; ++y) {
		if(stopped(stop)) {
			goto done;
		}
		addRow(rgba + (size_t)y * width * 4u, width, fit.reduce, sums);
		if((y + 1u) % fit.reduce == 0u || y + 1u == height) {
			emitRow(sums, fit.reducedW, reduced +
				(size_t)(y / fit.reduce) * fit.reducedW * 4u);
		}
	}
	ok = composeCanvas(reduced, &fit, width, height, levels[0], stop) &&
		encodeLevels(levels, out, stop);
done:
	free(sums);
	free(reduced);
	free(levels[0]);
	free(levels[1]);
	return ok;
}
