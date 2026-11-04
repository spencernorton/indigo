/* Standalone, RAM-only transport diagnostic. No filesystem or game loader.
 * Phase one uses the SDK's normal NTSC timing with two source widths.
 * P1 always expires to P0. Custom timing tables are deliberately unavailable
 * until this diagnostic's own control, capture and recovery path is proven.
 */
#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"

#ifndef VL_REVISION
#define VL_REVISION "unversioned"
#endif

_Static_assert(VI_TVMODE(VI_NTSC, VI_PROGRESSIVE) == 2,
	"The diagnostic requires the libogc2 mode encoding, not libogc-rice.");

#define MAX_FRAME (720u * 480u * 2u)
#define REQUEST_LIMIT 1024u
#define SESSION_SECONDS 600u
#define HTTP_PORT 4300

static GXRModeObj video_mode[VL_CASE_COUNT];
static uint8_t *frame[VL_CASE_COUNT][2];
static uint8_t snapshot[MAX_FRAME] ATTRIBUTE_ALIGN(32);
static char worker_stack[65536] ATTRIBUTE_ALIGN(8);
static lwp_t worker_thread;
static mutex_t image_mutex;
static syswd_t trial_alarm, session_alarm;
static _Atomic uint32_t pending_command, trial_expired, exit_requested;
static _Atomic uint32_t alarm_count;
static unsigned current_mode, current_pattern, current_buffer;
static uint32_t sequence, image_crc, fallback_count, changes, boot_nonce;
static uint32_t render_us, last_reason;
static uint64_t trial_started;
static uint64_t published_trial_started;
static int component;

enum { WHY_BOOT, WHY_COMMAND, WHY_ALARM, WHY_DEADLINE, WHY_PAD };

/* Startup-only UART logging. Once networking starts, its EXI traffic owns
 * the bus; all later diagnostics are available in /status instead. */
static void startup_uart(const char *text)
{
	vu32 *exi = (vu32 *)0xCC006800;
	size_t at = 0;
	exi[0] = 1u << 8 | 3u << 4;
	exi[4] = 0xa0010000u;
	exi[3] = 1u | 1u << 2 | 3u << 4;
	while(exi[3] & 1u) {}
	while(text[at]) {
		uint32_t value = 0;
		unsigned count = 0;
		while(count < 4 && text[at]) {
			value |= (uint32_t)(uint8_t)text[at++] << (24 - count * 8);
			count++;
		}
		exi[4] = value;
		exi[3] = 1u | 1u << 2 | (count - 1u) << 4;
		while(exi[3] & 1u) {}
	}
	exi[0] = 0;
}

static uint32_t milliseconds_since(uint64_t start)
{
	return (uint32_t)ticks_to_millisecs(diff_ticks(start, gettime()));
}

static void expire_trial(syswd_t alarm, void *argument)
{
	(void)alarm;
	(void)argument;
	alarm_count++;
	trial_expired = 1;
}

static void expire_session(syswd_t alarm, void *argument)
{
	(void)alarm;
	(void)argument;
	exit_requested = 1;
}

/* This worker owns VIDEO_Configure/Flush and the image state. Neither
 * network I/O nor a wait for VI interrupts can delay its deadline checks. */
static int apply_image(unsigned mode, unsigned pattern, unsigned reason)
{
	uint64_t started = gettime();
	unsigned next = current_buffer ^ 1u;
	size_t bytes;

	if(mode >= VL_CASE_COUNT || pattern >= VL_PATTERN_COUNT) return 0;
	bytes = vl_frame_bytes(&vl_modes[mode]);
	LWP_MutexLock(image_mutex);
	sequence++;
	if(!vl_render(frame[mode][next], bytes, &vl_modes[mode], pattern, sequence)) {
		LWP_MutexUnlock(image_mutex);
		return 0;
	}
	image_crc = vl_crc32(frame[mode][next], bytes);
	DCFlushRange(frame[mode][next], bytes);
	VIDEO_Configure(&video_mode[mode]);
	VIDEO_SetNextFramebuffer(frame[mode][next]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	current_mode = mode;
	current_pattern = pattern;
	current_buffer = next;
	last_reason = reason;
	changes++;
	render_us = (uint32_t)ticks_to_microsecs(diff_ticks(started, gettime()));
	LWP_MutexUnlock(image_mutex);
	return 1;
}

static void baseline(unsigned reason)
{
	SYS_CancelAlarm(trial_alarm);
	LWP_MutexLock(image_mutex);
	if(current_mode) fallback_count++;
	published_trial_started = 0;
	LWP_MutexUnlock(image_mutex);
	trial_started = 0;
	trial_expired = 0;
	apply_image(0, current_pattern, reason);
}

static void begin_trial(void)
{
	const struct timespec limit = {VL_TRIAL_MS / 1000u, 0};
	if(!component) return;
	SYS_CancelAlarm(trial_alarm);
	trial_expired = 0;
	trial_started = gettime();
	LWP_MutexLock(image_mutex);
	published_trial_started = trial_started;
	LWP_MutexUnlock(image_mutex);
	if(SYS_SetAlarm(trial_alarm, &limit, expire_trial, NULL) != 0) {
		trial_started = 0;
		LWP_MutexLock(image_mutex);
		published_trial_started = 0;
		LWP_MutexUnlock(image_mutex);
		return;
	}
	apply_image(1, current_pattern, WHY_COMMAND);
}

static void *video_worker(void *unused)
{
	uint64_t last_pad = gettime();
	(void)unused;
	while(!exit_requested) {
		uint32_t command = atomic_exchange(&pending_command, 0);
		if(trial_expired) baseline(WHY_ALARM);
		else if(current_mode && trial_started &&
			milliseconds_since(trial_started) >= VL_TRIAL_MS)
			baseline(WHY_DEADLINE);
		if(command) {
			switch(command & 0xffffu) {
				case VL_BASELINE: baseline(WHY_COMMAND); break;
				case VL_TRIAL: begin_trial(); break;
				case VL_PATTERN:
					apply_image(current_mode, command >> 16, WHY_COMMAND);
					break;
				case VL_EXPIRE: {
					const struct timespec soon = {0, 50000000};
					SYS_CancelAlarm(trial_alarm);
					SYS_SetAlarm(trial_alarm, &soon, expire_trial, NULL);
					break;
				}
				default: break;
			}
		}
		if(milliseconds_since(last_pad) >= 16) {
			uint32_t down;
			last_pad = gettime();
			PAD_ScanPads();
			down = PAD_ButtonsDown(0);
			if(down & PAD_BUTTON_B) baseline(WHY_PAD);
			else if(down & PAD_BUTTON_A) begin_trial();
			else if(down & PAD_BUTTON_RIGHT)
				apply_image(current_mode, (current_pattern + 1u) % VL_PATTERN_COUNT, WHY_PAD);
			else if(down & PAD_BUTTON_LEFT)
				apply_image(current_mode, (current_pattern + VL_PATTERN_COUNT - 1u) % VL_PATTERN_COUNT, WHY_PAD);
			if(down & PAD_BUTTON_START) exit_requested = 1;
		}
		usleep(1000);
	}
	baseline(WHY_COMMAND);
	usleep(100000);
	SYS_ResetSystem(SYS_HOTRESET, 0, false);
	return NULL;
}

static int send_all(s32 socket, const void *data, size_t length)
{
	const uint8_t *at = data;
	uint64_t started = gettime();
	while(length && milliseconds_since(started) < 2000) {
		s32 count = net_send(socket, at, length > 16384 ? 16384 : (s32)length, 0);
		if(count <= 0) return 0;
		at += count;
		length -= (size_t)count;
	}
	return length == 0;
}

static void response(s32 socket, unsigned status, const char *type,
	const void *data, size_t length, const char *extra)
{
	char header[384];
	int size = snprintf(header, sizeof(header),
		"HTTP/1.0 %u %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
		"Connection: close\r\nX-Video-Lab: 1\r\n%s\r\n",
		status, status == 200 ? "OK" : status == 202 ? "Accepted" : "Error",
		type, (unsigned)length, extra ? extra : "");
	if(size > 0 && size < (int)sizeof(header) && send_all(socket, header, (size_t)size))
		send_all(socket, data, length);
}

static void status_response(s32 socket)
{
	char body[1800];
	unsigned i;
	int used;
	u16 vi[60];
	size_t bytes;
	LWP_MutexLock(image_mutex);
	bytes = vl_frame_bytes(&vl_modes[current_mode]);
	for(i = 0; i < 60; i++) vi[i] = ((vu16 *)0xCC002000)[i];
	used = snprintf(body, sizeof(body),
		"{\"protocol\":\"video-lab-1\",\"revision\":\"%s\",\"boot_nonce\":%u,"
		"\"case\":\"P%u\",\"width\":%u,\"height\":480,\"stride_pixels\":%u,"
		"\"vi_width\":%u,\"progressive\":%s,\"pattern\":%u,\"sequence\":%u,"
		"\"crc32\":%u,\"frame_bytes\":%u,\"presented\":%s,"
		"\"trial_remaining_ms\":%u,\"fallback_count\":%u,\"alarm_count\":%u,"
		"\"changes\":%u,\"last_reason\":%u,\"render_and_crc_us\":%u,"
		"\"retrace_count\":%u,\"vi_registers\":[",
		VL_REVISION, boot_nonce, current_mode, vl_modes[current_mode].width,
		vl_modes[current_mode].stride, vl_modes[current_mode].vi_width,
		component ? "true" : "false", current_pattern, sequence,
		image_crc, (unsigned)bytes,
		VIDEO_GetCurrentFramebuffer() == frame[current_mode][current_buffer] ? "true" : "false",
		current_mode && published_trial_started && milliseconds_since(published_trial_started) < VL_TRIAL_MS ?
			VL_TRIAL_MS - milliseconds_since(published_trial_started) : 0,
		fallback_count, alarm_count, changes, last_reason, render_us, VIDEO_GetRetraceCount());
	for(i = 0; i < 60 && used > 0 && used < (int)sizeof(body) - 16; i++)
		used += snprintf(body + used, sizeof(body) - (size_t)used, "%s%u", i ? "," : "", vi[i]);
	if(used > 0 && used < (int)sizeof(body) - 4) {
		memcpy(body + used, "]}\n", 3);
		used += 3;
	} else used = 0;
	LWP_MutexUnlock(image_mutex);
	response(socket, used ? 200 : 500, "application/json", body, (size_t)used, NULL);
}

static void xfb_response(s32 socket)
{
	char extra[192];
	size_t bytes;
	LWP_MutexLock(image_mutex);
	bytes = vl_frame_bytes(&vl_modes[current_mode]);
	memcpy(snapshot, frame[current_mode][current_buffer], bytes);
	snprintf(extra, sizeof(extra),
		"X-Size: %ux480\r\nX-Stride-Pixels: %u\r\nX-Pattern: %u\r\n"
		"X-Sequence: %u\r\nX-CRC32: %u\r\nX-Boot-Nonce: %u\r\n",
		vl_modes[current_mode].width, vl_modes[current_mode].stride,
		current_pattern, sequence, image_crc, boot_nonce);
	LWP_MutexUnlock(image_mutex);
	response(socket, 200, "application/octet-stream", snapshot, bytes, extra);
}

static void serve(s32 server)
{
	struct sockaddr_in remote;
	socklen_t remote_size = sizeof(remote);
	struct timeval timeout = {0, 250000};
	char request[REQUEST_LIMIT];
	size_t have = 0;
	vl_request parsed;
	uint32_t expected = 0;
	s32 client = net_accept(server, (struct sockaddr *)&remote, &remote_size);
	uint64_t started = gettime();
	if(client < 0) return;
	net_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	net_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	while(have + 1 < sizeof(request) && milliseconds_since(started) < 1000) {
		s32 count = net_recv(client, request + have, sizeof(request) - have - 1, 0);
		if(count <= 0) break;
		have += (size_t)count;
		request[have] = 0;
		if(strstr(request, "\r\n\r\n")) break;
	}
	parsed = vl_parse_request(request, have);
	if(parsed.status != 200) {
		response(client, (unsigned)parsed.status, "text/plain", "request refused\n", 16, NULL);
	} else if(parsed.command == VL_STATUS) status_response(client);
	else if(parsed.command == VL_XFB) xfb_response(client);
	else if(parsed.command == VL_TRIAL && !component)
		response(client, 422, "text/plain", "P1 requires digital progressive output\n", 39, NULL);
	else if(parsed.command == VL_EXIT) {
		response(client, 202, "text/plain", "returning to boot program\n", 26, NULL);
		exit_requested = 1;
	} else if(!atomic_compare_exchange_strong(&pending_command, &expected,
		(uint32_t)parsed.command | parsed.value << 16)) {
		response(client, 409, "text/plain", "command pending\n", 16, NULL);
	} else {
		response(client, 202, "text/plain", "queued\n", 7, NULL);
	}
	net_close(client);
}

int main(void)
{
	struct in_addr ip = {0}, mask = {0}, gateway = {0};
	struct sockaddr_in address;
	const struct timespec lifetime = {SESSION_SECONDS, 0};
	s32 server;
	unsigned m, b;

	startup_uart("video-lab: main\r");
	VIDEO_Init();
	PAD_Init();
	component = (*(vu16 *)0xCC00206E & 1u) != 0;
	for(m = 0; m < VL_CASE_COUNT; m++) {
		video_mode[m] = component ? TVNtsc480Prog : TVNtsc480IntDf;
		video_mode[m].fbWidth = vl_modes[m].stride;
		video_mode[m].viWidth = vl_modes[m].vi_width;
		video_mode[m].viXOrigin = vl_modes[m].vi_origin;
		for(b = 0; b < 2; b++) {
			frame[m][b] = SYS_AllocateFramebuffer(&video_mode[m]);
			if(!frame[m][b] || !vl_render(frame[m][b], vl_frame_bytes(&vl_modes[m]),
				&vl_modes[m], 0, 0)) return 1;
			DCFlushRange(frame[m][b], vl_frame_bytes(&vl_modes[m]));
		}
	}
	if(LWP_MutexInit(&image_mutex, false) != 0 ||
		SYS_CreateAlarm(&trial_alarm) != 0 || SYS_CreateAlarm(&session_alarm) != 0)
		return 1;
	boot_nonce = (uint32_t)gettime();
	current_pattern = 0;
	apply_image(0, current_pattern, WHY_BOOT);
	startup_uart("video-lab: baseline P0 640x480 ready; no filesystem\r");
	if(SYS_SetAlarm(session_alarm, &lifetime, expire_session, NULL) != 0 ||
		LWP_CreateThread(&worker_thread, video_worker, NULL, worker_stack,
			sizeof(worker_stack), 80) != 0)
		return 1;
	/* DHCP cannot prevent the separate worker from handling the controller,
	 * trial expiration, or the bounded lifetime back to the boot program. */
	if(if_configex(&ip, &mask, &gateway, true) < 0) {
		while(!exit_requested) usleep(100000);
		return 1;
	}
	server = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(HTTP_PORT);
	address.sin_addr.s_addr = INADDR_ANY;
	if(server < 0 || net_bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 ||
		net_listen(server, 2) < 0) {
		exit_requested = 1;
		while(1) usleep(100000);
	}
	while(!exit_requested) {
		fd_set readable;
		struct timeval wait = {0, 100000};
		FD_ZERO(&readable);
		FD_SET(server, &readable);
		if(net_select(server + 1, &readable, NULL, NULL, &wait) > 0) serve(server);
	}
	net_close(server);
	while(1) usleep(100000);
}
