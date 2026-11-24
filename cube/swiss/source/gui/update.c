/* On-console updates. The installed program and return apploader remain the
 * recovery entry points. Each can launch a confirmed, signed update slot. */
#include <argz.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdatomic.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bba.h"
#include "config.h"
#include "deviceHandler.h"
#include "files.h"
#include "FrameBufferMagic.h"
#include "input.h"
#include "IPLFontWrite.h"
#include "sha256.h"
#include "swiss.h"
#include "ui_update.h"
#include "update.h"
#include "update_trust.h"
#include "ui_update_slot.h"
#include "menuaudio.h"
#include "mp3.h"
#include "main.h"
#include "sidestep.h"
#include "util.h"

#define UPDATE_CHUNK (32u * 1024u)
#define UPDATE_HEAD_MAX 4096u        /* an HTTP head, whole */
#define UPDATE_IDLE_SECONDS 10       /* wiiload's read patience */
/* Whole-wait ceilings beside the idle one: a server dribbling a byte
 * inside every idle window still finishes or is given up, and B is
 * watched while waiting. The blocking connect and a single SMB read
 * keep the stack's and the device's own bounds (the design says so). */
#define UPDATE_HEAD_SECONDS 60       /* an HTTP head, whole */
#define UPDATE_MANIFEST_SECONDS 60   /* the update list's body */
#define UPDATE_TRANSFER_SECONDS 600  /* one whole file, either transport */
#define UPDATE_DIR "swiss/indigo/updates"
#define UPDATE_SMB_DIR "indigo-updates"

typedef enum {
	UPDATE_OK = 0, UPDATE_CANCELLED, UPDATE_NOT_CONFIGURED, UPDATE_BAD_SERVER,
	UPDATE_NO_NETWORK, UPDATE_CONNECT_FAIL, UPDATE_TIMEOUT, UPDATE_HTTP_STATUS,
	UPDATE_HTTP_UNSUPPORTED, UPDATE_SMB_REFUSED, UPDATE_SMB_NOT_FOUND,
	UPDATE_MANIFEST_TOO_BIG, UPDATE_MANIFEST_INVALID, UPDATE_NO_CONFIG_DEVICE,
	UPDATE_NO_SPACE, UPDATE_READ_FAIL, UPDATE_WRITE_FAIL, UPDATE_SIZE_MISMATCH,
	UPDATE_HASH_MISMATCH, UPDATE_IMAGE_INVALID
} update_result_t;

typedef struct update_source_state update_source_state_t;
struct update_source_state {
	update_result_t (*fetchManifest)(update_source_state_t *, const char *, char *, size_t, size_t *);
	update_result_t (*fetchFile)(update_source_state_t *, const char *, file_handle *, uint32_t,
		sha256_ctx *, uiDrawObj_t *);
	ui_update_base_t base;
	const char *hostHeader;
	char *chunk;
	char *scratch;
	char detail[128];
	bool quiet;
};

/* The screen: tell, ask, and show progress. A box holds three lines.  */
/* ------------------------------------------------------------------ */

static void updateTell(int type, const char *message)
{
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(type, message));
	wait_press_A();
	DrawDispose(box);
}

/* A/B with the choosing press let go first (settingsConfirmReset's rule),
 * and the answering press let go before anything else reads the pad. */
static bool updateAsk(int type, const char *message, bool *pressedA)
{
	bool released = false;
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(type, message));

	for(;;) {
		u32 held = padsButtonsHeld() & (BUTTON_A | BUTTON_B);
		if(!released) {
			released = held == 0;
		}
		else if(held & BUTTON_A) {
			*pressedA = true;
			break;
		}
		else if(held & BUTTON_B) {
			*pressedA = false;
			break;
		}
		VIDEO_WaitVSync();
	}
	while(padsButtonsHeld() & (BUTTON_A | BUTTON_B))
		VIDEO_WaitVSync();
	DrawDispose(box);
	return *pressedA;
}

/* Progress with a speed and the time left, refreshed as the copy core
 * does it; true while B hasn't cancelled. */
typedef struct {
	u64 started;
	u64 lastTick;
	u32 lastDone;
	u32 lastDrawMs;
	int speed;
} update_progress_t;

static void updateProgressStart(update_progress_t *progress)
{
	memset(progress, 0, sizeof(*progress));
	progress->started = SYS_GetSystemTime();
	progress->lastTick = progress->started;
	/* A B pressed to get here isn't a cancel. */
	(void)padsButtonsTaken(BUTTON_B);
}

static bool updateProgress(update_progress_t *state, uiDrawObj_t *bar,
	uint32_t done, uint32_t total)
{
	u32 sinceTick = diff_msec(state->lastTick, SYS_GetSystemTime());
	u32 elapsed = diff_msec(state->started, SYS_GetSystemTime());
	int remain = 0;

	if(sinceTick >= 1000 && done > state->lastDone) {
		state->speed = (int)((float)(done - state->lastDone) /
			((float)sinceTick / 1000.0f));
		state->lastTick = SYS_GetSystemTime();
		state->lastDone = done;
	}
	if(done < total && elapsed - state->lastDrawMs < 100)
		return ((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) == 0;
	state->lastDrawMs = elapsed;
	if(state->speed > 0)
		remain = (int)((total - done) / (uint32_t)state->speed);
	DrawUpdateProgressBarDetail(bar, total ?
		(int)((float)done / (float)total * 100.0f) : 0,
		state->speed, (int)(elapsed / 1000), remain);
	return ((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) == 0;
}

/* ------------------------------------------------------------------ */
/* The Configuration Device: targets, staging, proof, the swap.       */
/* ------------------------------------------------------------------ */

static DEVICEHANDLER_INTERFACE *updateDevice(void)
{
	return devices[DEVICE_CONFIG];
}

/* B while waiting somewhere with no progress bar of its own — the same
 * press updateProgress watches between chunks. */
static bool updateCancelPressed(void)
{
	return ((padsButtonsHeld() | padsButtonsTaken(BUTTON_B)) & BUTTON_B) != 0;
}

static bool updateDeadlineHit(u64 started, unsigned int seconds)
{
	return diff_msec(started, SYS_GetSystemTime()) > seconds * 1000u;
}

/* Writes one staged piece at the handle's own offset. */
static bool updateStageWrite(file_handle *target, const void *buffer,
	uint32_t length)
{
	return updateDevice()->writeFile(target, buffer, length) ==
		(s32)length;
}

/* Plain HTTP: one connection per file, bounded reads, no redirects.  */
/* ------------------------------------------------------------------ */

/* Sends the whole request, however many pieces the stack takes. */
static bool httpSendAll(s32 socket, const char *data, size_t length)
{
	size_t sent = 0;

	while(sent < length) {
		int piece = net_send(socket, data + sent, length - sent, 0);
		if(piece <= 0)
			return false;
		sent += (size_t)piece;
	}
	return true;
}

/* Reads until the buffer fills or the stream does; -1 is the idle time
 * out, -2 a dead connection. B is the caller's to watch between reads. */
static int httpRecvSome(s32 socket, char *buffer, size_t capacity)
{
	fd_set readable;
	struct timeval wait = {UPDATE_IDLE_SECONDS, 0};

	FD_ZERO(&readable);
	FD_SET(socket, &readable);
	if(net_select(socket + 1, &readable, NULL, NULL, &wait) <= 0)
		return -1;
	{
		int got = net_recv(socket, buffer, capacity, 0);
		return got < 0 ? -2 : got;
	}
}

/* Connects and asks for one file beside the manifest. On success the
 * socket is at the body's first fresh byte, `*lengthOut` is the body's
 * size, and bytes that arrived with the head are at `*carryOut` (inside
 * the source's scratch, good until the next read). */
static update_result_t httpOpenBody(update_source_state_t *source,
	const char *leaf, uint32_t expectedSize, bool manifestWanted,
	s32 *socketOut, uint32_t *lengthOut, const char **carryOut,
	size_t *carryLength)
{
	ui_update_base_t *base = &source->base;
	struct sockaddr_in address;
	char request[256];
	char *head = source->scratch;
	size_t headLength = 0;
	u64 started;
	s32 socket;

	*socketOut = -1;
	*carryLength = 0;
	*carryOut = head;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(base->port);
	if(!inet_aton(base->host, &address.sin_addr))
		return UPDATE_BAD_SERVER;
	socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if(socket < 0)
		return UPDATE_CONNECT_FAIL;
	int nonblocking = 1;
	struct timeval timeout = {5, 0};
	if(net_ioctl(socket, FIONBIO, &nonblocking) < 0) {
		net_close(socket);
		return UPDATE_CONNECT_FAIL;
	}
	int connected = net_connect(socket, (struct sockaddr *)&address, sizeof(address));
	if(connected < 0) {
		fd_set writable;
		int error = 0;
		socklen_t errorSize = sizeof(error);
		FD_ZERO(&writable);
		FD_SET(socket, &writable);
		if(net_select(socket + 1, NULL, &writable, NULL, &timeout) <= 0 ||
			net_getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &errorSize) < 0 || error) {
			net_close(socket);
			return UPDATE_CONNECT_FAIL;
		}
	}
	nonblocking = 0;
	if(net_ioctl(socket, FIONBIO, &nonblocking) < 0) {
		net_close(socket);
		return UPDATE_CONNECT_FAIL;
	}
	snprintf(request, sizeof(request),
		"GET %s%s HTTP/1.0\r\nHost: %s\r\nAccept: */*\r\n\r\n",
		base->path, leaf, source->hostHeader);
	if(!httpSendAll(socket, request, strlen(request))) {
		net_close(socket);
		return UPDATE_CONNECT_FAIL;
	}
	head[0] = '\0';
	/* The head as a whole is bounded, and B is watched between reads:
	 * idle patience alone lets a dribbling server wait out the user. */
	started = SYS_GetSystemTime();
	for(;;) {
		int status;
		uint32_t contentLength;
		bool hasLength;
		bool chunked;

		if(!source->quiet && updateCancelPressed()) {
			net_close(socket);
			return UPDATE_CANCELLED;
		}
		if(updateDeadlineHit(started, UPDATE_HEAD_SECONDS)) {
			net_close(socket);
			return UPDATE_TIMEOUT;
		}
		if(UIUpdate_ParseHttpHead(head, headLength, &status,
			&contentLength, &hasLength, &chunked)) {
			/* The body starts after the head's blank line. */
			const char *crlf = strstr(head, "\r\n\r\n");
			const char *lf = strstr(head, "\n\n");
			const char *body;
			if(crlf != NULL && (lf == NULL || crlf < lf))
				body = crlf + 4;
			else if(lf != NULL)
				body = lf + 2;
			else {
				net_close(socket);
				return UPDATE_HTTP_UNSUPPORTED;
			}
			if(chunked || !hasLength) {
				snprintf(source->detail, sizeof(source->detail), "%s",
					chunked ? "a chunked reply" : "no length sent");
				net_close(socket);
				return UPDATE_HTTP_UNSUPPORTED;
			}
			if(status != 200) {
				snprintf(source->detail, sizeof(source->detail),
					"HTTP %d", status);
				net_close(socket);
				return UPDATE_HTTP_STATUS;
			}
			if(manifestWanted) {
				if(contentLength > UPDATE_ENVELOPE_MAX) {
					net_close(socket);
					return UPDATE_MANIFEST_TOO_BIG;
				}
			}
			else if(contentLength != expectedSize) {
				net_close(socket);
				return UPDATE_SIZE_MISMATCH;
			}
			*carryOut = body;
			*carryLength = headLength - (size_t)(body - head);
			*lengthOut = contentLength;
			*socketOut = socket;
			return UPDATE_OK;
		}
		if(headLength >= UPDATE_HEAD_MAX) {
			net_close(socket);
			return UPDATE_HTTP_UNSUPPORTED;
		}
		{
			int got = httpRecvSome(socket, head + headLength,
				UPDATE_HEAD_MAX - headLength);
			if(got == -1) {
				net_close(socket);
				return UPDATE_TIMEOUT;
			}
			if(got <= 0) {
				net_close(socket);
				return UPDATE_CONNECT_FAIL;
			}
			headLength += (size_t)got;
			head[headLength] = '\0';
		}
	}
}

static update_result_t httpFetchManifest(update_source_state_t *source,
	const char *channel, char *buffer, size_t capacity, size_t *length)
{
	char leaf[24];
	const char *carry;
	size_t carryLength;
	uint32_t total;
	size_t done;
	u64 started;
	s32 socket;
	update_result_t result;

	snprintf(leaf, sizeof(leaf), "%s.signed", channel);
	result = httpOpenBody(source, leaf, 0, true, &socket, &total,
		&carry, &carryLength);
	if(result != UPDATE_OK)
		return result;
	if((size_t)total >= capacity) {
		net_close(socket);
		return UPDATE_MANIFEST_TOO_BIG;
	}
	done = carryLength < (size_t)total ? carryLength : (size_t)total;
	memcpy(buffer, carry, done);
	started = SYS_GetSystemTime();
	while(done < (size_t)total) {
		int got;

		if(!source->quiet && updateCancelPressed()) {
			net_close(socket);
			return UPDATE_CANCELLED;
		}
		if(updateDeadlineHit(started, UPDATE_MANIFEST_SECONDS)) {
			net_close(socket);
			return UPDATE_TIMEOUT;
		}
		got = httpRecvSome(socket, buffer + done, (size_t)total - done);
		if(got == -1) {
			net_close(socket);
			return UPDATE_TIMEOUT;
		}
		if(got <= 0) {
			net_close(socket);
			return UPDATE_SIZE_MISMATCH;
		}
		done += (size_t)got;
	}
	net_close(socket);
	buffer[total] = '\0';
	*length = total;
	return UPDATE_OK;
}

static update_result_t httpFetchFile(update_source_state_t *source,
	const char *name, file_handle *target, uint32_t expectedSize,
	sha256_ctx *sha, uiDrawObj_t *progressBar)
{
	const char *carry;
	size_t carryLength;
	uint32_t total;
	uint32_t done = 0;
	s32 socket;
	update_progress_t pace;
	update_result_t result;
	char *chunk = source->chunk;

	result = httpOpenBody(source, name, expectedSize, false, &socket,
		&total, &carry, &carryLength);
	if(result != UPDATE_OK)
		return result;
	target->offset = 0;
	updateProgressStart(&pace);
	/* Fill sector-aligned card writes across short network reads. Bytes
	 * received with the headers participate in the same first block. */
	uint32_t buffered = carryLength < total ? (uint32_t)carryLength : total;
	memcpy(chunk, carry, buffered);
	done = buffered;
	for(;;) {
		if(buffered == UPDATE_CHUNK || done == total) {
			if(!updateStageWrite(target, chunk, buffered)) {
				result = UPDATE_WRITE_FAIL;
				break;
			}
			sha256_update(sha, (const uint8_t *)chunk, buffered);
			buffered = 0;
		}
		if(done == total)
			break;
		if(updateDeadlineHit(pace.started, UPDATE_TRANSFER_SECONDS)) {
			result = UPDATE_TIMEOUT;
			break;
		}
		uint32_t want = total - done;
		if(want > UPDATE_CHUNK - buffered)
			want = UPDATE_CHUNK - buffered;
		int got = httpRecvSome(socket, chunk + buffered, want);
		if(got <= 0) {
			result = got == -1 ? UPDATE_TIMEOUT : UPDATE_SIZE_MISMATCH;
			break;
		}
		buffered += (uint32_t)got;
		done += (uint32_t)got;
		if(!updateProgress(&pace, progressBar, done, total)) {
			result = UPDATE_CANCELLED;
			break;
		}
	}
	net_close(socket);
	return result;
}

/* ------------------------------------------------------------------ */
/* The SMB share: the browsing device, at fixed paths, never listed.  */
/* ------------------------------------------------------------------ */

/* init's own answers, in the updater's words. */
static update_result_t smbStart(update_source_state_t *source)
{
	file_handle *initial = __device_smb.initial;

	(void)source;
	if(__device_smb.init(initial) == 0)
		return UPDATE_OK;
	if(initial->status == E_NONET)
		return UPDATE_NO_NETWORK;
	if(initial->status == E_CHECKCONFIG)
		return UPDATE_NOT_CONFIGURED;
	return UPDATE_SMB_REFUSED;
}

static void smbLeaf(char out[PATHNAME_MAX], const char *leaf)
{
	concat_path(out, __device_smb.initial->name, UPDATE_SMB_DIR);
	concat_path(out, out, leaf);
}

static update_result_t smbFetchManifest(update_source_state_t *source,
	const char *channel, char *buffer, size_t capacity, size_t *length)
{
	file_handle file;
	char leaf[24];

	(void)source;
	memset(&file, 0, sizeof(file));
	snprintf(leaf, sizeof(leaf), "%s.signed", channel);
	smbLeaf(file.name, leaf);
	file.device = &__device_smb;
	if(__device_smb.statFile(&file) != 0) {
		snprintf(source->detail, sizeof(source->detail), "%s", file.name);
		return UPDATE_SMB_NOT_FOUND;
	}
	if(file.size > UPDATE_ENVELOPE_MAX || (size_t)file.size >= capacity)
		return UPDATE_MANIFEST_TOO_BIG;
	__device_smb.seekFile(&file, 0, DEVICE_HANDLER_SEEK_SET);
	if(__device_smb.readFile(&file, buffer, file.size) != (s32)file.size) {
		__device_smb.closeFile(&file);
		return UPDATE_READ_FAIL;
	}
	__device_smb.closeFile(&file);
	buffer[file.size] = '\0';
	*length = file.size;
	return UPDATE_OK;
}

static update_result_t smbFetchFile(update_source_state_t *source,
	const char *name, file_handle *target, uint32_t expectedSize,
	sha256_ctx *sha, uiDrawObj_t *progressBar)
{
	file_handle file;
	update_progress_t pace;
	uint32_t done = 0;
	char *chunk = source->chunk;

	memset(&file, 0, sizeof(file));
	smbLeaf(file.name, name);
	file.device = &__device_smb;
	if(__device_smb.statFile(&file) != 0) {
		snprintf(source->detail, sizeof(source->detail), "%s", file.name);
		return UPDATE_SMB_NOT_FOUND;
	}
	if(file.size != expectedSize)
		return UPDATE_SIZE_MISMATCH;
	target->offset = 0;
	updateProgressStart(&pace);
	while(done < expectedSize) {
		uint32_t want = expectedSize - done;
		s32 got;
		/* Between chunks is all this transport can promise: one SMB
		 * read blocks in the device until it answers. */
		if(updateDeadlineHit(pace.started, UPDATE_TRANSFER_SECONDS)) {
			__device_smb.closeFile(&file);
			return UPDATE_TIMEOUT;
		}
		if(want > UPDATE_CHUNK)
			want = UPDATE_CHUNK;
		__device_smb.seekFile(&file, (s64)done, DEVICE_HANDLER_SEEK_SET);
		got = __device_smb.readFile(&file, chunk, want);
		if(got != (s32)want) {
			/* Once more, as the copy core retries a read. */
			__device_smb.seekFile(&file, (s64)done, DEVICE_HANDLER_SEEK_SET);
			got = __device_smb.readFile(&file, chunk, want);
			if(got != (s32)want) {
				__device_smb.closeFile(&file);
				return UPDATE_READ_FAIL;
			}
		}
		if(!updateStageWrite(target, chunk, want)) {
			__device_smb.closeFile(&file);
			return UPDATE_WRITE_FAIL;
		}
		sha256_update(sha, (const uint8_t *)chunk, want);
		done += want;
		if(!updateProgress(&pace, progressBar, done, expectedSize)) {
			__device_smb.closeFile(&file);
			return UPDATE_CANCELLED;
		}
	}
	__device_smb.closeFile(&file);
	return UPDATE_OK;
}


/* ------------------------------------------------------------------ */
/* Only the foreground touches card handlers or GUI objects. The network
 * worker owns its request until the foreground joins it at an idle Home. */
typedef struct {
	update_source_state_t source;
	ui_update_manifest_t manifest;
	char envelope[UPDATE_ENVELOPE_MAX + 1];
	size_t length;
	char channel[8];
	char server[sizeof(swissSettings.updateServer)];
	update_result_t result;
} update_check_t;

static int runningSlot = -1;
static bool recoveryBoot;
static bool confirmPending;
static bool checkStarted;
static update_check_t *pendingCheck;
static lwp_t checkThread = LWP_THREAD_NULL;
static atomic_bool checkFinished;

static void slotFile(file_handle *file, int slot, const char *suffix)
{
	memset(file, 0, sizeof(*file));
	file->device = updateDevice();
	concatf_path(file->name, file->device->initial->name, UPDATE_DIR "/%c.%s",
		slot == 0 ? 'a' : 'b', suffix);
}

static bool mountSlots(void)
{
	if(!config_set_device())
		return false;
	DEVICEHANDLER_INTERFACE *d = updateDevice();
	if(d->statFile && d->readFile && d->closeFile)
		return true;
	config_unset_device();
	return false;
}

static bool slotRecord(int slot, const char *suffix, char *envelope, ui_update_manifest_t *out)
{
	file_handle file;
	slotFile(&file, slot, suffix);
	if(file.device->statFile(&file) != 0 || file.fileType != IS_FILE ||
		file.size <= UPDATE_SIGNATURE_BYTES || file.size > UPDATE_ENVELOPE_MAX)
		return false;
	uint32_t size = file.size;
	s32 got = file.device->readFile(&file, envelope, size);
	int closed = file.device->closeFile(&file);
	return got == (s32)size && closed == 0 && updateReadSigned(envelope, size, NULL, out);
}

static void slotInventory(ui_update_slots_t *slots, char *scratch)
{
	ui_update_manifest_t record;
	memset(slots, 0, sizeof(*slots));
	for(int i = 0; i < 2; ++i) {
		if(slotRecord(i, "good", scratch, &record))
			slots->confirmed[i] = record.buildTime;
		if(slotRecord(i, "trial", scratch, &record))
			memcpy(slots->tried[i], record.commit, sizeof(record.commit));
	}
}

/* Authenticate the exact buffer that sidestep will start. The final 1 KiB
 * is reserved for the loader's command-line area. */
static update_result_t slotImage(int slot, const ui_update_manifest_t *release, u8 **out)
{
	file_handle file;
	slotFile(&file, slot, "dol");
	if(file.device->statFile(&file) != 0 || file.fileType != IS_FILE || file.size != release->size)
		return UPDATE_SIZE_MISMATCH;
	u8 *image = memalign(32, (size_t)release->size + 0x400u);
	if(!image)
		return UPDATE_NO_SPACE;
	update_result_t result = UPDATE_OK;
	sha256_ctx sha;
	uint8_t digest[32];
	sha256_init(&sha);
	for(uint32_t done = 0; done < release->size;) {
		uint32_t want = release->size - done;
		if(want > 128u * 1024u)
			want = 128u * 1024u;
		if(file.device->readFile(&file, image + done, want) != (s32)want) {
			result = UPDATE_READ_FAIL;
			break;
		}
		sha256_update(&sha, image + done, want);
		done += want;
	}
	if(file.device->closeFile(&file) != 0)
		result = UPDATE_READ_FAIL;
	sha256_final(&sha, digest);
	if(result == UPDATE_OK && memcmp(digest, release->sha256, sizeof(digest)))
		result = UPDATE_HASH_MISMATCH;
	if(result == UPDATE_OK && !wiiload_image_valid(image, release->size, false))
		result = UPDATE_IMAGE_INVALID;
	if(result != UPDATE_OK) {
		free(image);
		return result;
	}
	memset(image + release->size, 0, 0x400u);
	*out = image;
	return UPDATE_OK;
}

static update_result_t startSlot(int slot, u8 *image)
{
	file_handle file;
	slotFile(&file, slot, "dol");
	char *args = getExternalPath(file.name);
	if(!args) {
		free(image);
		return UPDATE_NO_SPACE;
	}
	size_t length = strlen(args) + 1;
	if(argz_add(&args, &length, slot == 0 ? "--indigo-slot-a" : "--indigo-slot-b")) {
		free(args);
		free(image);
		return UPDATE_NO_SPACE;
	}
	bool mp3Live = mp3_player_initialized();
	bool hadAudio = menuaudio_shutdown();
	DOLtoARAM(image, args, length);
	free(args);
	free(image);
	if(mp3Live) {
		DrawShutdown();
		SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
	}
	if(hadAudio)
		menuaudio_init();
	return UPDATE_IMAGE_INVALID;
}

static void confirmTrial(void)
{
	if(!mountSlots())
		return;
	char *scratch = malloc(UPDATE_ENVELOPE_MAX + 1);
	ui_update_manifest_t trial;
	if(scratch && slotRecord(runningSlot, "trial", scratch, &trial) &&
		strcmp(trial.commit, INDIGO_BUILD_COMMIT) == 0 && trial.buildTime == GIT_TIMESTAMP) {
		file_handle from, to;
		slotFile(&from, runningSlot, "trial");
		slotFile(&to, runningSlot, "good");
		if(updateDevice()->renameFile && updateDevice()->renameFile(&from, to.name) == 0)
			print_debug("Update: confirmed %.7s at Home\n", trial.commit);
	}
	free(scratch);
	config_unset_device();
}

void updateStartupBoot(int argc, char **argv)
{
	u32 buttons = padsButtonsHeld();
	recoveryBoot = (buttons & (BUTTON_X | BUTTON_Y)) == (BUTTON_X | BUTTON_Y);
	if(recoveryBoot)
		return;
	for(int i = 1; i < argc; ++i) {
		if(strcmp(argv[i], "--indigo-slot-a") == 0 || strcmp(argv[i], "--indigo-slot-b") == 0) {
			runningSlot = argv[i][strlen("--indigo-slot-")] == 'a' ? 0 : 1;
			confirmPending = true;
			return;
		}
	}
	if(!mountSlots())
		return;
	char *scratch = malloc(UPDATE_ENVELOPE_MAX + 1);
	if(scratch) {
		ui_update_manifest_t records[2];
		ui_update_slots_t slots = {0};
		for(int i = 0; i < 2; ++i) {
			if(slotRecord(i, "good", scratch, &records[i]) &&
				GIT_TIMESTAMP > 0 && records[i].buildTime > GIT_TIMESTAMP)
				slots.confirmed[i] = records[i].buildTime;
		}
		int slot = UIUpdate_FirstSlot(&slots);
		for(int n = 0; n < 2; ++n, slot = 1 - slot) {
			u8 *image = NULL;
			if(slots.confirmed[slot] && slotImage(slot, &records[slot], &image) == UPDATE_OK)
				startSlot(slot, image);
		}
		free(scratch);
	}
	config_unset_device();
}

/* Clear only a file in the inactive slot. A stat error on a readable
 * existing file must not let us overwrite its recovery record. */
static bool clearSlotFile(int slot, const char *suffix)
{
	file_handle file;
	slotFile(&file, slot, suffix);
	if(file.device->statFile(&file) != 0)
		return true;
	return file.fileType == IS_FILE && file.device->deleteFile(&file) == 0;
}

static update_result_t installSlot(update_check_t *check, bool automatic)
{
	DEVICEHANDLER_INTERFACE *d = updateDevice();
	if(!(d->features & FEAT_WRITE) || !d->writeFile || !d->deleteFile || !d->renameFile || !d->makeDir)
		return UPDATE_NO_CONFIG_DEVICE;

	char *scratch = malloc(UPDATE_ENVELOPE_MAX + 1);
	if(!scratch)
		return UPDATE_NO_SPACE;
	ui_update_slots_t slots;
	slotInventory(&slots, scratch);
	int slot = UIUpdate_InstallSlot(&slots, runningSlot, &check->manifest, automatic);
	if(slot < 0) {
		free(scratch);
		return UPDATE_OK;
	}

	ensure_path(DEVICE_CONFIG, "swiss", NULL, true);
	ensure_path(DEVICE_CONFIG, "swiss/indigo", NULL, false);
	ensure_path(DEVICE_CONFIG, UPDATE_DIR, NULL, false);
	update_result_t result = UPDATE_WRITE_FAIL;
	file_handle file;
	u8 *image = NULL;
	if(!clearSlotFile(slot, "good") || !clearSlotFile(slot, "trial") || !clearSlotFile(slot, "dol"))
		goto done;

	slotFile(&file, slot, "dol");
	device_info *space = d->info ? d->info(d->initial) : NULL;
	if(space && space->totalSpace && space->freeSpace < (u64)check->manifest.size + check->length) {
		result = UPDATE_NO_SPACE;
		goto done;
	}
	check->source.chunk = memalign(32, UPDATE_CHUNK);
	if(!check->source.chunk) {
		result = UPDATE_NO_SPACE;
		goto done;
	}
	sha256_ctx sha;
	uint8_t hash[32];
	sha256_init(&sha);
	char message[96];
	snprintf(message, sizeof(message), "Updating Indigo to %s", check->manifest.version);
	uiDrawObj_t *bar = DrawPublish(DrawProgressBar(false, 0, message));
	result = check->source.fetchFile(&check->source, check->manifest.file, &file,
		check->manifest.size, &sha, bar);
	DrawDispose(bar);
	free(check->source.chunk);
	check->source.chunk = NULL;
	if(result == UPDATE_OK && d->writeFile(&file, NULL, 0) != 0)
		result = UPDATE_WRITE_FAIL;
	if(d->closeFile(&file) != 0 && result == UPDATE_OK)
		result = UPDATE_WRITE_FAIL;
	if(result != UPDATE_OK)
		goto done;
	sha256_final(&sha, hash);
	if(memcmp(hash, check->manifest.sha256, sizeof(hash))) {
		result = UPDATE_HASH_MISMATCH;
		goto done;
	}
	result = slotImage(slot, &check->manifest, &image);
	if(result != UPDATE_OK)
		goto done;

	/* Publish the signed trial record last. A cold boot ignores trials.
	 * Only the matching new executable reaching Home can promote one. */
	slotFile(&file, slot, "trial");
	s32 written = d->writeFile(&file, check->envelope, check->length);
	int flushed = written == (s32)check->length ? d->writeFile(&file, NULL, 0) : -1;
	int closed = d->closeFile(&file);
	ui_update_manifest_t trial;
	if(flushed || closed || !slotRecord(slot, "trial", scratch, &trial) ||
		strcmp(trial.commit, check->manifest.commit)) {
		result = UPDATE_WRITE_FAIL;
		goto done;
	}
	free(scratch);
	return startSlot(slot, image);
done:
	free(image);
	free(scratch);
	return result;
}

static const char *failureText(update_result_t result)
{
	switch(result) {
		case UPDATE_CANCELLED: return "Update cancelled.";
		case UPDATE_NO_CONFIG_DEVICE: return "Choose a writable Configuration Device.";
		case UPDATE_NOT_CONFIGURED: return "Set the SMB host and share first.";
		case UPDATE_BAD_SERVER: return "Use an IPv4 address for a custom mirror.";
		case UPDATE_NO_NETWORK: return "The network is unavailable.";
		case UPDATE_NO_SPACE: return "Insufficient free storage or memory.";
		case UPDATE_MANIFEST_INVALID: return "Untrusted or invalid release manifest.";
		case UPDATE_HASH_MISMATCH: return "The update failed its SHA-256 check.";
		case UPDATE_IMAGE_INVALID: return "The new Indigo could not start.";
		case UPDATE_READ_FAIL: case UPDATE_WRITE_FAIL: return "The card could not complete the update.";
		case UPDATE_SIZE_MISMATCH: return "The release file has the wrong size.";
		default: return "The update source could not supply the release.";
	}
}

static update_check_t *newCheck(enum update_source source, bool quiet)
{
	update_check_t *check = calloc(1, sizeof(*check));
	if(!check)
		return NULL;
	update_source_state_t *transport = &check->source;
	transport->quiet = quiet;
	strcpy(check->channel, UIUpdate_ChannelName(swissSettings.updateChannel));
	if(source == UPDATE_SOURCE_SMB) {
		transport->fetchManifest = smbFetchManifest;
		transport->fetchFile = smbFetchFile;
		if(!swissSettings.smbShare[0] || !swissSettings.smbServerIp[0])
			check->result = UPDATE_NOT_CONFIGURED;
	} else {
		transport->fetchManifest = httpFetchManifest;
		transport->fetchFile = httpFetchFile;
		strlcpy(check->server, swissSettings.updateServer, sizeof(check->server));
		const char *server = check->server;
		ui_update_base_err_t why;
		if(!UIUpdate_ParseBase(*server ? server : UPDATE_DEFAULT_SERVER, &transport->base, &why))
			check->result = UPDATE_BAD_SERVER;
		transport->hostHeader = *server ? transport->base.host : UPDATE_DEFAULT_HOST;
		transport->scratch = malloc(UPDATE_HEAD_MAX + 1u);
		if(!transport->scratch)
			check->result = UPDATE_NO_SPACE;
	}
	return check;
}

static void fetchCheck(update_check_t *check)
{
	if(check->result == UPDATE_OK)
		check->result = check->source.fetchManifest(&check->source, check->channel,
			check->envelope, sizeof(check->envelope), &check->length);
	if(check->result == UPDATE_OK && !updateReadSigned(check->envelope, check->length,
		check->channel, &check->manifest))
		check->result = UPDATE_MANIFEST_INVALID;
}

static void finishCheck(update_check_t *check, bool automatic)
{
	bool apply = false;
	if(check->result == UPDATE_OK) {
		switch(UIUpdate_Decide(&check->manifest, INDIGO_BUILD_COMMIT, GIT_TIMESTAMP)) {
			case UI_UPDATE_DECIDE_NEWER:
				apply = true;
				break;
			case UI_UPDATE_DECIDE_MIN_BUILD:
				if(!automatic) updateTell(D_INFO, "Install the required base release from the SD download.");
				break;
			default:
				if(!automatic) updateTell(D_INFO, "You already have this release or a newer build.");
				break;
		}
	}
	if(apply && !automatic) {
		char message[128];
		bool answered;
		snprintf(message, sizeof(message), "Indigo %s is available.\nInstall and restart?\nA  INSTALL    B  CANCEL",
			check->manifest.version);
		apply = updateAsk(D_INFO, message, &answered);
	}
	if(apply) {
		check->source.quiet = false;
		if(mountSlots()) {
			check->result = installSlot(check, automatic);
			config_unset_device();
		} else {
			check->result = UPDATE_NO_CONFIG_DEVICE;
		}
	}
	if(check->result != UPDATE_OK) {
		print_debug("Update: %s\n", failureText(check->result));
		if(!automatic || apply)
			updateTell(D_WARN, failureText(check->result));
	}
	free(check->source.scratch);
	free(check);
}

void updateRun(enum update_source source)
{
	update_check_t *check = newCheck(source, false);
	if(!check) {
		updateTell(D_WARN, "Insufficient memory to check for updates.");
		return;
	}
	if(check->result == UPDATE_OK) {
		uiDrawObj_t *box = DrawPublish(DrawProgressBar(true, 0, "Checking for Indigo updates"));
		if(!init_network())
			check->result = UPDATE_NO_NETWORK;
		else if(source == UPDATE_SOURCE_SMB)
			check->result = smbStart(&check->source);
		fetchCheck(check);
		DrawDispose(box);
	}
	finishCheck(check, false);
}

static void *probeThread(void *arg)
{
	fetchCheck(arg);
	atomic_store(&checkFinished, true);
	return NULL;
}

void updateStartupPoll(void)
{
	if(confirmPending) {
		confirmPending = false;
		confirmTrial();
	}
	bool enabled = !recoveryBoot && !swissSettings.skipStartupUpdates;
	if(enabled && !checkStarted && update_network_ready()) {
		checkStarted = true;
		pendingCheck = newCheck(UPDATE_SOURCE_HTTP, true);
		if(pendingCheck && LWP_CreateThread(&checkThread, probeThread, pendingCheck,
			NULL, 32 * 1024, LWP_PRIO_LOWEST) != 0) {
			free(pendingCheck->source.scratch);
			free(pendingCheck);
			pendingCheck = NULL;
		}
	}
	if(pendingCheck && atomic_load(&checkFinished)) {
		LWP_JoinThread(checkThread, NULL);
		update_check_t *check = pendingCheck;
		pendingCheck = NULL;
		/* A change made in Settings while the request was out wins. */
		if(!enabled || strcmp(check->channel, UIUpdate_ChannelName(swissSettings.updateChannel)) ||
			strcmp(check->server, swissSettings.updateServer)) {
			free(check->source.scratch);
			free(check);
		} else {
			finishCheck(check, true);
		}
	}
}
