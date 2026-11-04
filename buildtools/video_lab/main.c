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
#include "gx_lab.h"

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
static int gx_enabled, current_gx;

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
	int use_gx = mode && gx_enabled && vg_state() == 2;
	size_t bytes;

	if(mode >= VL_CASE_COUNT || pattern >= VL_PATTERN_COUNT) return 0;
	bytes = vl_frame_bytes(&vl_modes[mode]);
	LWP_MutexLock(image_mutex);
	sequence++;
	if(use_gx) {
		memcpy(frame[mode][next], vg_coordinates(), bytes);
		pattern = 3;
	} else if(!vl_render(frame[mode][next], bytes, &vl_modes[mode], pattern, sequence)) {
		LWP_MutexUnlock(image_mutex);
		return 0;
	}
	if(!use_gx && pattern == 5) {
		uint32_t words[8] = {0x47585432u, vg_state(), 0, 0, 0, vg_progress(), 0, VS_FRAMES};
		if(words[1] >= 2) {
			const vg_result *result = vg_results();
			words[2] = result->error;
			words[3] = result->matched_frames;
			words[4] = result->coordinate_crc;
			words[6] = result->canaries_ok;
			if(result->coordinate_fenced) {
				unsigned y;
				for(y = 64; y < 480; y++)
					memcpy(frame[mode][next] + y * vl_modes[mode].stride * 2u,
						vg_coordinates() + y * VS_WIDTH * 2u, vl_modes[mode].width * 2u);
			}
		}
		vl_report(frame[mode][next], &vl_modes[mode], words);
	}
	image_crc = vl_crc32(frame[mode][next], bytes);
	DCFlushRange(frame[mode][next], bytes);
	VIDEO_Configure(&video_mode[mode]);
	VIDEO_SetNextFramebuffer(frame[mode][next]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	current_mode = mode;
	current_pattern = pattern;
	current_gx = use_gx;
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
	gx_enabled = 0;
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
	unsigned last_gpu_state = 0;
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
				case VL_GX_SHOW:
					if(vg_state() == 2) { gx_enabled = 1; begin_trial(); }
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
			if(down & PAD_BUTTON_X) {
				baseline(WHY_PAD);
				apply_image(0, 5, WHY_PAD);
				vg_start();
			}
			if(down & PAD_BUTTON_Y && vg_state() == 2) { gx_enabled = 1; begin_trial(); }
		}
		if(last_gpu_state != vg_state()) {
			last_gpu_state = vg_state();
			if(!current_mode && current_pattern == 5) apply_image(0, 5, WHY_COMMAND);
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
		"\"retrace_count\":%u,\"gx_state\":%u,\"gx_progress\":%u,\"renderer\":\"%s\",\"vi_registers\":[",
		VL_REVISION, boot_nonce, current_mode, vl_modes[current_mode].width,
		vl_modes[current_mode].stride, vl_modes[current_mode].vi_width,
		component ? "true" : "false", current_pattern, sequence,
		image_crc, (unsigned)bytes,
		VIDEO_GetCurrentFramebuffer() == frame[current_mode][current_buffer] ? "true" : "false",
		current_mode && published_trial_started && milliseconds_since(published_trial_started) < VL_TRIAL_MS ?
			VL_TRIAL_MS - milliseconds_since(published_trial_started) : 0,
		fallback_count, alarm_count, changes, last_reason, render_us, VIDEO_GetRetraceCount(),
		vg_state(), vg_progress(), current_gx ? "gx" : "cpu");
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
		"X-Sequence: %u\r\nX-CRC32: %u\r\nX-Boot-Nonce: %u\r\nX-Renderer: %s\r\n",
		vl_modes[current_mode].width, vl_modes[current_mode].stride,
		current_pattern, sequence, image_crc, boot_nonce, current_gx ? "gx" : "cpu");
	LWP_MutexUnlock(image_mutex);
	response(socket, 200, "application/octet-stream", snapshot, bytes, extra);
}

static void gx_status_response(s32 socket)
{
	char body[4096];
	unsigned state = vg_state(), v;
	const vg_result *r = vg_results();
	int used = snprintf(body, sizeof(body),
		"{\"state\":%u,\"progress\":%u,\"error\":%u,\"matched_frames\":%u,"
		"\"coordinate_crc\":%u,\"canaries_ok\":%u,\"oracle_max_error\":%u,\"variants\":[",
		state, vg_progress(), state >= 2 ? r->error : 0,
		state >= 2 ? r->matched_frames : 0, state >= 2 ? r->coordinate_crc : 0,
		state >= 2 ? r->canaries_ok : 0, state >= 2 ? r->oracle_max_error : 0);
	if(state >= 2) for(v = 0; v < VS_VARIANTS; v++) {
		const vg_metric *m = &r->variant[v];
		used += snprintf(body + used, sizeof(body) - (size_t)used,
			"%s{\"id\":%u,\"frames\":%u,\"copies\":%u,\"copy_pixels\":%u,"
			"\"vertices\":%u,\"texels\":%u,\"gpu_pixels_in\":%u,\"gpu_copy_clocks\":%u,"
			"\"work_us\":%u,\"cache_us\":%u,\"compare_us\":%u,\"cache_builds\":%u,"
			"\"crc32\":%u,\"mismatches\":%u}", v ? "," : "", v,
			m->frames, m->copies, m->copy_pixels, m->vertices, m->texels,
			m->gpu_pixels_in, m->gpu_copy_clocks, m->work_us, m->cache_us,
			m->compare_us, m->cache_builds, m->crc32, m->mismatches);
	}
	if(used > 0 && used < (int)sizeof(body) - 4) { memcpy(body + used, "]}\n", 3); used += 3; }
	else used = 0;
	response(socket, used ? 200 : 500, "application/json", body, (size_t)used, NULL);
}

static void gx_xfb_response(s32 socket, int scene, unsigned variant)
{
	const uint8_t *image;
	char extra[192];
	if(vg_state() != 2) {
		response(socket, 409, "text/plain", "GPU job not qualified\n", 22, NULL);
		return;
	}
	image = scene ? vg_scene(variant) : vg_coordinates();
	snprintf(extra, sizeof(extra),
		"X-Size: 720x480\r\nX-Stride-Pixels: 720\r\nX-Pattern: %u\r\nX-Sequence: 0\r\n"
		"X-CRC32: %u\r\nX-Boot-Nonce: %u\r\nX-Renderer: gx\r\n",
		scene ? 6 : 3, vl_crc32(image, MAX_FRAME), boot_nonce);
	response(socket, 200, "application/octet-stream", image, MAX_FRAME, extra);
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
	else if(parsed.command == VL_GX_STATUS) gx_status_response(client);
	else if(parsed.command == VL_GX_XFB) gx_xfb_response(client, 0, 0);
	else if(parsed.command == VL_GX_SCENE) gx_xfb_response(client, 1, parsed.value);
	else if(parsed.command == VL_GX_START) {
		int started = vg_start();
		response(client, started ? 202 : 409, "text/plain", "single job per boot\n", 20, NULL);
	}
	else if(parsed.command == VL_GX_SHOW && (vg_state() != 2 || !component))
		response(client, 409, "text/plain", "GPU job not qualified\n", 22, NULL);
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
	if(!vg_init()) return 1;
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
