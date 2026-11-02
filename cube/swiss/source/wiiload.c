/* 
 * Copyright (c) 2014-2026, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gccore.h>
#include <malloc.h>
#include <network.h>
#include <ogc/mutex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include "aram/sidestep.h"
#include "bba.h"
#include "elf.h"
#include "gui/FrameBufferMagic.h"
#include "gui/lab.h"
#include "gui/menuaudio.h"
#include "mp3.h"
#include "patcher.h"
#include "swiss.h"
#include "wiiload.h"

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t args_size;
	uint32_t deflate_size;
	uint32_t inflate_size;
} ATTRIBUTE_PACKED wiiload_header_t;

typedef int wiiload_read_function_t(int, void *, size_t);

/* One transfer at a time, bounded by the GameCube's available address space. */
#define WIILOAD_MAX_IMAGE (16u * 1024u * 1024u)
#define WIILOAD_MAX_DEFLATE (WIILOAD_MAX_IMAGE + 65536u)
#define WIILOAD_IDLE_SECONDS 10
static mutex_t upload_mutex = LWP_MUTEX_NULL;

static lwp_t main_thread = LWP_THREAD_NULL;
static lwp_t tcp_thread = LWP_THREAD_NULL;
static lwp_t usb_thread = LWP_THREAD_NULL;

static struct {
	struct {
		int sd;
		union {
			struct sockaddr sa;
			struct sockaddr_in sin;
		};
		struct timeval tv;
	} sv;

	struct {
		int sd;
		union {
			struct sockaddr sa;
			struct sockaddr_in sin;
		};
	} cl;
} wiiload = {
	.sv.sd = INVALID_SOCKET,

	.sv.sin.sin_family = AF_INET,
	.sv.sin.sin_port = 4299,
	.sv.sin.sin_addr.s_addr = INADDR_ANY,

	.sv.tv.tv_sec = 1,
	.sv.tv.tv_usec = 0,

	.cl.sd = INVALID_SOCKET,
};

static int tcp_read(int sd, void *buf, size_t size)
{
	int pos = 0;

	while ((size_t)pos < size) {
		fd_set readable;
		struct timeval timeout = {WIILOAD_IDLE_SECONDS, 0};
		FD_ZERO(&readable);
		FD_SET(sd, &readable);
		if (net_select(sd + 1, &readable, NULL, NULL, &timeout) <= 0)
			return -1;
		int ret = net_read(sd, (u8 *)buf + pos, size - (size_t)pos);
		if (ret < 1) return ret < 0 ? ret : pos;
		pos += ret;
	}
	return pos;
}

static int usb_read(int chn, void *buf, size_t size)
{
	return usb_recvbuffer_safe_ex(chn, buf, size, 65536);
}

static void *wiiload_read(int sd, size_t insize, size_t outsize, wiiload_read_function_t read, uiDrawObj_t *progress)
{
	Byte inbuf[4096];
	z_stream zstream = {0};
	size_t pos = 0;
	void *outbuf;
	bool complete = false;

	if (insize == 0u || insize > WIILOAD_MAX_DEFLATE ||
	    outsize < 4u || outsize > WIILOAD_MAX_IMAGE) return NULL;
	/* One guard byte lets inflate consume a split checksum after filling
	 * the exact output. Any use of that byte is an oversized stream. */
	outbuf = malloc(outsize + 1u);
	if (!outbuf) return NULL;
	if (inflateInit(&zstream) != Z_OK) {
		free(outbuf);
		return NULL;
	}
	zstream.next_out = outbuf;
	zstream.avail_out = (uInt)(outsize + 1u);
	while (pos < insize) {
		size_t take = MIN(insize - pos, sizeof(inbuf));
		int got = read(sd, inbuf, take);
		if (got <= 0 || (size_t)got > take) break;
		pos += (size_t)got;
		zstream.next_in = inbuf;
		zstream.avail_in = (uInt)got;
		int ret = inflate(&zstream, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			complete = pos == insize && zstream.total_in == insize &&
				zstream.total_out == outsize;
			break;
		}
		if (ret != Z_OK || zstream.total_out > outsize) break;
		DrawUpdateProgressBar(progress, (int)(pos * 100u / insize));
	}
	inflateEnd(&zstream);
	if (!complete) {
		free(outbuf);
		return NULL;
	}
	DrawUpdateProgressBar(progress, 100);
	return outbuf;
}

static void *wiiload_read_args(int sd, size_t size, wiiload_read_function_t read)
{
	if (size == 0u) return NULL;
	void *args = malloc(size);

	if (!args)
		goto fail;
	if (read(sd, args, size) != (int)size || ((const u8 *)args)[size - 1u] != 0u)
		goto fail;

	return args;

fail:
	free(args);
	return NULL;
}

static u32 wiiload_be32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u32 wiiload_be16(const u8 *p)
{
	return (u32)p[0] << 8 | p[1];
}

static bool wiiload_range(size_t offset, size_t length, size_t size)
{
	return offset <= size && length <= size - offset;
}

/* Sidestep has no input-length argument. Prove its header/table/payload
 * reads fit before calling it, including its entrypoint's argv probe.
 * boot_dol checks a file from a card with it too. */
bool wiiload_image_valid(const u8 *image, size_t size, bool raw)
{
	bool entryFound = false;
	u32 entry;

	if (image == NULL || size < 4u || size > WIILOAD_MAX_IMAGE) return false;
	if (raw) return true;
	if (memcmp(image, ELFMAG, SELFMAG) == 0) {
		if (size < 52u || image[4] != 1u || image[5] != 2u ||
		    wiiload_be16(image + 16) != 2u || wiiload_be16(image + 18) != 20u ||
		    wiiload_be16(image + 42) != 32u) return false;
		u32 offset = wiiload_be32(image + 28), count = wiiload_be16(image + 44);
		if ((offset & 3u) || !wiiload_range(offset, (size_t)count * 32u, size)) return false;
		entry = wiiload_be32(image + 24);
		for (u32 i = 0u; i < count; ++i) {
			const u8 *ph = image + offset + i * 32u;
			if (wiiload_be32(ph) != 1u) continue;
			u32 file = wiiload_be32(ph + 4), address = wiiload_be32(ph + 12);
			u32 bytes = wiiload_be32(ph + 16), memory = wiiload_be32(ph + 20);
			if (bytes > memory || !wiiload_range(file, bytes, size) ||
			    !wiiload_range(address & 0x3fffffffu, memory, 24u * 1024u * 1024u)) return false;
			if (entry >= address && entry - address < bytes) {
				size_t at = (size_t)file + (entry - address);
				if ((at & 3u) || !wiiload_range(at, 40u, size)) return false;
				entryFound = true;
			}
		}
		return entryFound;
	}
	if (size < 256u || (wiiload_be32(image) != 0x100u && wiiload_be32(image) != 0x620u))
		return false;
	if (wiiload_be32(image + 0xdc) != 0u &&
	    !wiiload_range(wiiload_be32(image + 0xd8) & 0x3fffffffu,
	                   wiiload_be32(image + 0xdc), 24u * 1024u * 1024u)) return false;
	entry = wiiload_be32(image + 0xe0);
	for (u32 i = 0u; i < 18u; ++i) {
		u32 file = wiiload_be32(image + i * 4u);
		u32 address = wiiload_be32(image + 0x48u + i * 4u);
		u32 bytes = wiiload_be32(image + 0x90u + i * 4u);
		if (file == 0u || bytes == 0u) continue;
		if (!wiiload_range(file, bytes, size) ||
		    !wiiload_range(address & 0x3fffffffu, bytes, 24u * 1024u * 1024u)) return false;
		if (i < 7u && entry >= address && entry - address < bytes) {
			size_t at = (size_t)file + (entry - address);
			if ((at & 3u) || !wiiload_range(at, 40u, size)) return false;
			entryFound = true;
		}
	}
	return entryFound;
}

/* Acquire the video mutex before suspending its possible owner. The upload
 * cannot strand that owner while its progress or shutdown waits for it. */
static void wiiload_suspend_main(void *unused)
{
	(void)unused;
	LWP_SuspendThread(main_thread);
}

static void wiiload_handler(int sd, wiiload_read_function_t read, const char *message, const char *host)
{
	wiiload_header_t header;

	if (read(sd, &header, sizeof(header)) != (int)sizeof(header))
		return;
	if (memcmp(&header.magic, "HAXX", 4))
		return;
	if (header.version != 5)
		return;

	if (header.deflate_size == 0u || header.deflate_size > WIILOAD_MAX_DEFLATE ||
	    header.inflate_size < 4u || header.inflate_size > WIILOAD_MAX_IMAGE ||
	    upload_mutex == LWP_MUTEX_NULL) return;
	LWP_MutexLock(upload_mutex);
	int priority = LWP_SetThreadPriority(LWP_THREAD_NULL, LWP_PRIO_NORMAL);
	bool suspended = false;
	uiDrawObj_t *progress = DrawPublish(DrawProgressBar(false, 0, message));
	void *buffer = wiiload_read(sd, header.deflate_size, header.inflate_size, read, progress);
	void *args = buffer ? wiiload_read_args(sd, header.args_size, read) : NULL;
	if (header.args_size != 0u && args == NULL) {
		free(buffer);
		buffer = NULL;
	}

	static const char sdloaderName[] = "SDLOADER.BIN";
	size_t argNameLen = args ? strnlen(args, header.args_size) : 0;
	bool isSdLoader = argNameLen == sizeof(sdloaderName) - 1 &&
	                 !strncasecmp(args, sdloaderName, argNameLen);
	bool isBranchBin = buffer != NULL && branchResolve(buffer, PATCH_BIN, 0) != 0;
	/* An ELF loads as an ELF whatever its name, so it is checked as one. */
	bool raw = buffer != NULL && (isSdLoader || isBranchBin) &&
	           memcmp(buffer, ELFMAG, SELFMAG) != 0;
	if (buffer != NULL && ((isSdLoader && header.inflate_size > 1024u * 1024u) ||
	    !wiiload_image_valid(buffer, header.inflate_size, raw))) {
		free(buffer);
		buffer = NULL;
	}

	bool restartMenuAudio = false;
	bool resetOnLoadFailure = false;
	if (buffer) {
		DrawGameflowBlockPosters();
		DrawWithVideoLocked(wiiload_suspend_main, NULL);
		suspended = true;
		setenv("WIILOAD", host, 1);
		// Stop the shared MP3 decoder before sidestep resets AESND's ARAM.
		resetOnLoadFailure = mp3_player_initialized();
		MP3Player_Stop();
		restartMenuAudio = menuaudio_shutdown();
		if (!memcmp(buffer, ELFMAG, SELFMAG))
			ELFtoARAM(buffer, args, header.args_size);
		else if (isSdLoader)
			BINtoARAM(buffer, header.inflate_size, 0x81700000, 0x81700000);
		else if (isBranchBin)
			BINtoARAM(buffer, header.inflate_size, 0x80003100, 0x80003100);
		else
			DOLtoARAM(buffer, args, header.args_size);
	}

	free(args);
	free(buffer);
	DrawDispose(progress);
	// A rejected sidestep load leaves libaesnd's private MP3 voice stale.
	// Hot reset only if MP3Player had initialized; early no-MP3 loads can resume.
	if (resetOnLoadFailure) {
		DrawShutdown();
		SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
		__builtin_unreachable();
	}
	// A valid image never returns. Resume only after a rejected upload.
	if (restartMenuAudio) menuaudio_init();

	if (suspended) {
		DrawGameflowUnblockPosters();
		LWP_ResumeThread(main_thread);
	}
	LWP_SetThreadPriority(LWP_THREAD_NULL, priority);
	LWP_MutexUnlock(upload_mutex);
}

static void *tcp_thread_func(void *arg)
{
	wiiload.sv.sd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

	if (wiiload.sv.sd == INVALID_SOCKET)
		goto fail;
	if (net_bind(wiiload.sv.sd, &wiiload.sv.sa, sizeof(wiiload.sv.sin)) < 0)
		goto fail;
	if (net_listen(wiiload.sv.sd, 0) < 0)
		goto fail;

	while (tcp_thread == LWP_GetSelf()) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(wiiload.sv.sd, &readset);
		struct timeval timeout = wiiload.sv.tv;
		int ready = net_select(wiiload.sv.sd + 1, &readset, NULL, NULL, &timeout);

		if (ready > 0 && FD_ISSET(wiiload.sv.sd, &readset)) {
			socklen_t addrlen = sizeof(wiiload.cl.sin);
			wiiload.cl.sd = net_accept(wiiload.sv.sd, &wiiload.cl.sa, &addrlen);

			if (wiiload.cl.sd != INVALID_SOCKET) {
				char addrstr[20] = "TCP:";
				char *message = NULL;
				asprintf(&message, "Receiving from %s:%hu", inet_ntoa_r(wiiload.cl.sin.sin_addr, addrstr + strlen(addrstr), sizeof(addrstr) - strlen(addrstr)), wiiload.cl.sin.sin_port);
				wiiload_handler(wiiload.cl.sd, tcp_read, message, addrstr);
				free(message);

				net_close(wiiload.cl.sd);
				wiiload.cl.sd = INVALID_SOCKET;
			}
		}
	}

fail:
	net_close(wiiload.sv.sd);
	wiiload.sv.sd = INVALID_SOCKET;
	return NULL;
}

static void *usb_thread_func(void *arg)
{
	while (usb_thread == LWP_GetSelf()) {
		int chn = *(int *)arg - USBGECKO_MEMCARD_SLOT_A;

		if (usb_isgeckoalive(chn) && usb_checkrecv(chn)) {
			wiiload_handler(chn, usb_read, "Receiving over USB Gecko", "USBGECKO");
			usb_flush(chn);
		}

		usleep(1000);
	}

	return NULL;
}

__attribute((constructor))
static void init_wiiload(void)
{
	main_thread = LWP_GetSelf();
	LWP_MutexInit(&upload_mutex, false);
}

void init_wiiload_tcp_thread(void)
{
	if (net_initialized && tcp_thread == LWP_THREAD_NULL)
		LWP_CreateThread(&tcp_thread, tcp_thread_func, NULL, NULL, 0, LWP_PRIO_NORMAL);
	/* Indigo: the network is up, so the console lab starts too (gui/lab.c). */
	if (net_initialized)
		lab_start();
}

void init_wiiload_usb_thread(void *arg)
{
	LWP_CreateThread(&usb_thread, usb_thread_func, arg, NULL, 0, LWP_PRIO_LOWEST);
}

/* Indigo: the console lab's POST /reset runs an upload's hand-off too
 * (gui/lab.c), so it takes the upload's lock first and keeps it. */
void wiiload_hold(void)
{
	LWP_MutexLock(upload_mutex);
}
