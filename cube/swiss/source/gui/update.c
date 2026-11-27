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
#include "filelock.h"
#include "filemeta.h"
#include "files.h"
#include "FrameBufferMagic.h"
#include "input.h"
#include "IPLFontWrite.h"
#include "sha256.h"
#include "swiss.h"
#include "ui_content.h"
#include "ui_gameflow_library.h"
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
	UPDATE_HASH_MISMATCH, UPDATE_IMAGE_INVALID, UPDATE_CONTENT_CONFLICT
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
	bool quiet;
};

/* The screen: tell, ask, and show progress. A box holds three lines.  */
/* ------------------------------------------------------------------ */

/* The next screen reads both current and latched presses. Consume the
 * dialogue's answering buttons before returning control to it. */
static void releaseUpdateButtons(void)
{
	const u32 mask = BUTTON_A | BUTTON_B;
	while(padsButtonsHeld() & mask)
		VIDEO_WaitVSync();
	(void)padsButtonsTaken(mask);
}

static void updateTell(int type, const char *message)
{
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(type, message));
	wait_press_A();
	releaseUpdateButtons();
	DrawDispose(box);
}

/* A/B with the choosing press let go first (settingsConfirmReset's rule),
 * and the answering press let go before anything else reads the pad. */
static bool updateAsk(const char *message)
{
	u32 held;
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_INFO, message));
	releaseUpdateButtons();
	while(!(held = padsButtonsHeld() & (BUTTON_A | BUTTON_B)))
		VIDEO_WaitVSync();
	releaseUpdateButtons();
	DrawDispose(box);
	return (held & BUTTON_A) != 0;
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

/* Writes one staged piece at the handle's own offset, on its own device:
 * the Configuration Device for a release, the browsed card for content. */
static bool updateStageWrite(file_handle *target, const void *buffer,
	uint32_t length)
{
	return target->device->writeFile(target, buffer, length) ==
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
				net_close(socket);
				return UPDATE_HTTP_UNSUPPORTED;
			}
			if(status != 200) {
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

static update_result_t httpReadText(update_source_state_t *source, const char *leaf,
	uint32_t expected, char *buffer, size_t capacity, size_t *length)
{
	const char *carry;
	size_t carryLength;
	uint32_t total;
	size_t done;
	u64 started;
	s32 socket;
	update_result_t result;

	result = httpOpenBody(source, leaf, expected, expected == 0, &socket, &total,
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

static update_result_t httpFetchManifest(update_source_state_t *source,
	const char *channel, char *buffer, size_t capacity, size_t *length)
{
	char leaf[24];
	snprintf(leaf, sizeof(leaf), "%s.signed", channel);
	return httpReadText(source, leaf, 0, buffer, capacity, length);
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
static bool trialHome;
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
	if(result == UPDATE_OK && memcmp(digest, release->sha256, sizeof(digest))) {
		print_debug("Update: card readback differs from signed digest\n");
		result = UPDATE_HASH_MISMATCH;
	}
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

static bool matchesRunningTrial(char *scratch)
{
	ui_update_manifest_t trial;
	return scratch && slotRecord(runningSlot, "trial", scratch, &trial) &&
		strcmp(trial.commit, INDIGO_BUILD_COMMIT) == 0 && trial.buildTime == GIT_TIMESTAMP;
}

bool updateStartupTrial(void)
{
	return trialHome;
}

static void confirmTrial(void)
{
	if(!mountSlots())
		return;
	char *scratch = malloc(UPDATE_ENVELOPE_MAX + 1);
	if(matchesRunningTrial(scratch)) {
		file_handle from, to;
		slotFile(&from, runningSlot, "trial");
		slotFile(&to, runningSlot, "good");
		if(updateDevice()->renameFile && updateDevice()->renameFile(&from, to.name) == 0)
			print_debug("Update: confirmed %.7s at Home\n", INDIGO_BUILD_COMMIT);
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
			/* A trial must reach its first Home even when Recent or Autoload
			 * would normally resume another path. A confirmed slot keeps it. */
			if((swissSettings.autoload[0] || (swissSettings.recent[0][0] &&
				swissSettings.recentListLevel > 1)) && mountSlots()) {
				char *scratch = malloc(UPDATE_ENVELOPE_MAX + 1);
				trialHome = matchesRunningTrial(scratch);
				free(scratch);
				config_unset_device();
			}
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

/* Fetch and close one complete payload. A digest failure here precedes
 * the separate readback of the card and publication of the trial. */
static update_result_t fetchPayload(update_check_t *check, const char *name,
	file_handle *file, uint32_t size, const uint8_t digest[32], const char *message)
{
	DEVICEHANDLER_INTERFACE *device = file->device;
	sha256_ctx sha;
	uint8_t hash[32];
	sha256_init(&sha);
	uiDrawObj_t *bar = DrawPublish(DrawProgressBar(false, 0, message));
	update_result_t result = check->source.fetchFile(&check->source, name, file, size, &sha, bar);
	DrawDispose(bar);
	if(result == UPDATE_OK && device->writeFile(file, NULL, 0) != 0)
		result = UPDATE_WRITE_FAIL;
	if(device->closeFile(file) != 0 && result == UPDATE_OK)
		result = UPDATE_WRITE_FAIL;
	sha256_final(&sha, hash);
	if(result == UPDATE_OK && memcmp(hash, digest, sizeof(hash))) {
		print_debug("Update: received data differs from signed digest\n");
		return UPDATE_HASH_MISMATCH;
	}
	return result;
}

static update_result_t downloadSlotPayload(update_check_t *check, file_handle *file, bool retry)
{
	char message[96];
	snprintf(message, sizeof(message), retry ? "Retrying Indigo update to %s" : "Updating Indigo to %s",
		check->manifest.version);
	check->source.chunk = memalign(32, UPDATE_CHUNK);
	if(!check->source.chunk)
		return UPDATE_NO_SPACE;
	update_result_t result = fetchPayload(check, check->manifest.file, file,
		check->manifest.size, check->manifest.sha256, message);
	free(check->source.chunk);
	check->source.chunk = NULL;
	return result;
}

static update_result_t downloadSlot(update_check_t *check, file_handle *file, int slot, bool automatic)
{
	slotFile(file, slot, "dol");
	update_result_t result = downloadSlotPayload(check, file, false);
	if(!automatic || result != UPDATE_HASH_MISMATCH)
		return result;

	/* Keep the authenticated manifest and discard the closed, unconfirmed
	 * payload before requesting a replacement in the same inactive slot. */
	if(updateCancelPressed())
		return UPDATE_CANCELLED;
	if(file->device->deleteFile(file) != 0)
		return UPDATE_WRITE_FAIL;
	slotFile(file, slot, "dol");
	print_debug("Update: retrying signed download once\n");
	return downloadSlotPayload(check, file, true);
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

	device_info *space = d->info ? d->info(d->initial) : NULL;
	if(space && space->totalSpace && space->freeSpace < (u64)check->manifest.size + check->length) {
		result = UPDATE_NO_SPACE;
		goto done;
	}
	result = downloadSlot(check, &file, slot, automatic);
	if(result != UPDATE_OK)
		goto done;
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
		case UPDATE_HASH_MISMATCH: return "A file failed its SHA-256 check.";
		case UPDATE_IMAGE_INVALID: return "The new Indigo could not start.";
		case UPDATE_READ_FAIL: case UPDATE_WRITE_FAIL: return "The card could not read or write a file.";
		case UPDATE_SIZE_MISMATCH: return "A file has the wrong size.";
		default: return "The update source could not supply the release.";
	}
}

static update_check_t *newCheck(enum update_source source, bool quiet)
{
	update_check_t *check = calloc(1, sizeof(*check));
	if(!check)
		return NULL;
	/* Settings can leave a B press latched when returning from a submenu. */
	if(!quiet)
		(void)padsButtonsTaken(BUTTON_B);
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
		snprintf(message, sizeof(message), "Indigo %s is available.\nInstall and restart?\nA  INSTALL    B  CANCEL",
			check->manifest.version);
		apply = updateAsk(message);
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

/* ------------------------------------------------------------------ */
/* Game content: posters, cheats and manuals, signed like a release.  */
/* ------------------------------------------------------------------ */

#define CONTENT_GAMES_MAX 2048u

static void contentFile(file_handle *file, DEVICEHANDLER_INTERFACE *device,
	const char *path, const char *suffix)
{
	memset(file, 0, sizeof(*file));
	file->device = device;
	concatf_path(file->name, device->initial->name, "%s%s", path, suffix);
}

enum { CONTENT_LIVE, CONTENT_PART, CONTENT_OLD, CONTENT_PATHS };

/* Inspect every name before mutating any of them. FatFs creates the new
 * directory entry before removing the source during a rename. A power cut
 * can therefore leave two names for the same chain; unlinking either would
 * free the other's data. Zero is the unallocated cluster of an empty file. */
static update_result_t contentPaths(DEVICEHANDLER_INTERFACE *device, const char *path,
	file_handle files[CONTENT_PATHS])
{
	static const char *const suffix[] = {"", ".part", ".indigo-old"};
	for(int i = 0; i < CONTENT_PATHS; i++) {
		file_handle *f = &files[i];
		contentFile(f, device, path, suffix[i]);
		errno = 0;
		int result = device->statFile(f);
		if(result) {
			/* FatFs and FlippyDrive use FRESULT, littlefs uses negative
			 * errno and the network filesystems use POSIX stat. */
			if(result != FR_NO_FILE && result != FR_NO_PATH && result != -ENOENT &&
				!(result == -1 && errno == ENOENT))
				return UPDATE_READ_FAIL;
			f->fileType = IS_SPECIAL;
			continue;
		}
		if(f->fileType != IS_FILE || (i != CONTENT_LIVE && (f->fileAttrib & ATTRIB_READONLY)))
			return UPDATE_CONTENT_CONFLICT;
		if(device->statFile == deviceHandler_FAT_statFile && f->fileBase)
			for(int j = 0; j < i; j++)
				if(files[j].fileBase == f->fileBase)
					return UPDATE_CONTENT_CONFLICT;
	}
	return UPDATE_OK;
}

/* A short read is a media error, not permission to replace a local file. */
static int contentMatch(file_handle *file, const ui_content_file_t *item, char *chunk)
{
	if(file->fileType != IS_FILE || file->size != item->size)
		return 0;
	sha256_ctx sha;
	uint8_t hash[32];
	uint32_t done = 0;
	file->offset = 0;
	sha256_init(&sha);
	while(done < item->size) {
		uint32_t want = item->size - done;
		if(want > UPDATE_CHUNK)
			want = UPDATE_CHUNK;
		if(file->device->readFile(file, chunk, want) != (s32)want)
			break;
		sha256_update(&sha, (const uint8_t *)chunk, want);
		done += want;
	}
	if(file->device->closeFile(file) || done != item->size)
		return -1;
	sha256_final(&sha, hash);
	return !memcmp(hash, item->sha256, sizeof(hash));
}

/* Recover leftovers before checking free space, including when the live
 * content already matches. Keep both originals if neither can be identified
 * as the installed version. */
static update_result_t contentPrepare(DEVICEHANDLER_INTERFACE *device,
	const ui_content_file_t *item, char *chunk, bool *installed)
{
	file_handle files[CONTENT_PATHS];
	update_result_t result = contentPaths(device, item->path, files);
	if(result != UPDATE_OK)
		return result;
	file_handle *live = &files[CONTENT_LIVE], *old = &files[CONTENT_OLD];
	if(live->fileType != IS_FILE && old->fileType == IS_FILE) {
		if(device->renameFile(old, live->name) || device->statFile(live))
			return UPDATE_WRITE_FAIL;
		old->fileType = IS_SPECIAL;
	}
	int match = contentMatch(live, item, chunk);
	if(match < 0)
		return UPDATE_READ_FAIL;
	if(!match && ((live->fileAttrib & ATTRIB_READONLY) || old->fileType == IS_FILE))
		return UPDATE_CONTENT_CONFLICT;
	if(files[CONTENT_PART].fileType == IS_FILE && device->deleteFile(&files[CONTENT_PART]))
		return UPDATE_WRITE_FAIL;
	if(old->fileType == IS_FILE && device->deleteFile(old))
		return UPDATE_WRITE_FAIL;
	*installed = match != 0;
	return UPDATE_OK;
}

/* The live file stays in place until its staged replacement reads back
 * correctly. On a rename error re-inspect all names before recovery. */
static update_result_t contentFetch(update_check_t *check, DEVICEHANDLER_INTERFACE *device,
	const ui_content_file_t *item, const char *message)
{
	file_handle files[CONTENT_PATHS];
	update_result_t result = contentPaths(device, item->path, files);
	if(result != UPDATE_OK)
		return result;
	file_handle *live = &files[CONTENT_LIVE], *part = &files[CONTENT_PART], *old = &files[CONTENT_OLD];
	if(part->fileType == IS_FILE || old->fileType == IS_FILE || (live->fileAttrib & ATTRIB_READONLY))
		return UPDATE_CONTENT_CONFLICT;
	result = fetchPayload(check, item->path, part, item->size, item->sha256, message);
	if(result != UPDATE_OK)
		return result;
	if(device->statFile(part) || contentMatch(part, item, check->source.chunk) != 1)
		return UPDATE_READ_FAIL;
	bool replace = live->fileType == IS_FILE;
	if(replace) {
		if(device->renameFile(live, old->name))
			return UPDATE_WRITE_FAIL;
		contentFile(live, device, item->path, "");
	}
	if(device->renameFile(part, live->name)) {
		bool installed;
		contentPrepare(device, item, check->source.chunk, &installed);
		return UPDATE_WRITE_FAIL;
	}
	return replace && device->deleteFile(old) ? UPDATE_WRITE_FAIL : UPDATE_OK;
}

/* The games in the folder browsed last: each disc's own ID, or a game
 * folder's. Known gap: that one folder only; a Library kept in several
 * folders gets its manuals one folder at a time. */
static size_t contentGameIds(char (*ids)[7], size_t capacity)
{
	file_handle *entries = getCurrentDirEntries();
	int count = getCurrentDirEntryCount();
	size_t found = 0;

	for(int i = 0; entries != NULL && i < count && found < capacity; i++) {
		file_handle *entry = &entries[i];
		lockFile(entry);
		if(entry->fileType == IS_DIR) {
			if(UIGameflowLibrary_ParseGameFolderName(getRelativeName(entry->name),
				ids[found], NULL, 0))
				found++;
		}
		else {
			if(entry->meta == NULL)
				populate_meta(entry);
			if(entry->meta != NULL && entry->meta->diskId.gamename[0]) {
				memcpy(ids[found], entry->meta->diskId.gamename, 4);
				memcpy(ids[found] + 4, entry->meta->diskId.company, 2);
				ids[found][6] = '\0';
				found++;
			}
		}
		unlockFile(entry);
	}
	return found;
}

/* The signed list, then every listed file the card lacks or holds a
 * different copy of. Tells the user itself unless something fails. */
static update_result_t contentInstall(update_check_t *check,
	DEVICEHANDLER_INTERFACE *device, uint32_t *done, uint32_t *total)
{
	update_result_t result = UPDATE_OK;
	ui_content_list_t list;
	ui_content_file_t file;
	size_t received;
	sha256_ctx sha;
	uint8_t hash[32];
	uint32_t counts[3] = {0, 0, 0};
	uint32_t length = check->manifest.size;
	uint32_t at;
	u64 bytes = 0;
	size_t games;
	char message[192];
	char sizeText[32];
	uiDrawObj_t *box;
	device_info *space;

	if(length == 0 || length > UI_CONTENT_LIST_MAX)
		return UPDATE_MANIFEST_INVALID;
	char (*ids)[7] = malloc(CONTENT_GAMES_MAX * sizeof(*ids));
	char *text = malloc(length + 1u);
	uint8_t *needed = calloc(UI_CONTENT_FILES_MAX / 8u, 1);
	check->source.chunk = memalign(32, UPDATE_CHUNK);
	if(!ids || !text || !needed || !check->source.chunk) {
		result = UPDATE_NO_SPACE;
		goto done;
	}
	box = DrawPublish(DrawProgressBar(true, 0, "Reading the game content list"));
	result = httpReadText(&check->source, check->manifest.file, length, text, length + 1u, &received);
	DrawDispose(box);
	if(result != UPDATE_OK)
		goto done;
	/* Authenticate the list in memory before interpreting any path or
	 * touching the card. A retry can reclaim staging on a full card. */
	sha256_init(&sha);
	sha256_update(&sha, (const uint8_t *)text, received);
	sha256_final(&sha, hash);
	if(memcmp(hash, check->manifest.sha256, sizeof(hash))) {
		result = UPDATE_HASH_MISMATCH;
		goto done;
	}
	if(!UIContent_Open(&list, text, received)) {
		result = UPDATE_MANIFEST_INVALID;
		goto done;
	}
	/* The official feed names where its files are; a mirror serves its own. */
	if(list.hasServer && !check->server[0]) {
		check->source.base = list.server;
		check->source.hostHeader = list.host;
	}

	DrawGameflowCancelPosters();
	ensure_path(DEVICE_CUR, "swiss", NULL, true);
	ensure_path(DEVICE_CUR, "swiss/ui", NULL, false);
	ensure_path(DEVICE_CUR, "swiss/ui/manuals", NULL, false);
	ensure_path(DEVICE_CUR, "swiss/cheats", NULL, false);
	box = DrawPublish(DrawProgressBar(true, 0, "Checking installed game content"));
	games = contentGameIds(ids, CONTENT_GAMES_MAX);
	for(at = 0; UIContent_Next(&list, &file); at++) {
		bool installed;
		if(!UIContent_Wanted(&file, (const char (*)[7])ids, games))
			continue;
		result = contentPrepare(device, &file, check->source.chunk, &installed);
		if(result != UPDATE_OK)
			break;
		if(installed)
			continue;
		needed[at / 8u] |= (uint8_t)(1u << (at % 8u));
		counts[file.kind]++;
		bytes += file.size;
		(*total)++;
	}
	DrawDispose(box);
	if(result != UPDATE_OK)
		goto done;
	if(*total == 0) {
		updateTell(D_INFO, games ? "Game content is up to date." :
			"Posters and cheats are up to date.\nBrowse your games first to add their manuals.");
		goto done;
	}
	space = device->info ? device->info(device->initial) : NULL;
	if(space && space->totalSpace && space->freeSpace < bytes) {
		result = UPDATE_NO_SPACE;
		goto done;
	}
	formatBytes(sizeText, (off_t)bytes, 0, false);
	snprintf(message, sizeof(message),
		"Install %u game content files (%s)?\n%u poster files, %u cheats, %u manuals\nA  INSTALL    B  CANCEL",
		(unsigned)*total, sizeText, (unsigned)counts[UI_CONTENT_POSTERS],
		(unsigned)counts[UI_CONTENT_CHEATS], (unsigned)counts[UI_CONTENT_MANUALS]);
	if(!updateAsk(message))
		goto done;

	list.cursor = 0;
	for(at = 0; UIContent_Next(&list, &file); at++) {
		if(!(needed[at / 8u] & (1u << (at % 8u))))
			continue;
		snprintf(message, sizeof(message), "Installing game content: %u of %u",
			(unsigned)*done + 1u, (unsigned)*total);
		result = contentFetch(check, device, &file, message);
		if(result != UPDATE_OK)
			goto done;
		(*done)++;
	}
	updateTell(D_INFO, "Game content installed. The Library shows\nthe posters and manuals, and Y on a game's\ndetails opens its cheats.");
done:
	free(check->source.chunk);
	check->source.chunk = NULL;
	free(needed);
	free(text);
	free(ids);
	return result;
}

static const char *contentFailureText(update_result_t result)
{
	switch(result) {
		case UPDATE_CANCELLED: return "Installation cancelled.";
		case UPDATE_CONTENT_CONFLICT: return "Protected or conflicting game files.\nCheck the card on a computer.";
		case UPDATE_MANIFEST_INVALID: return "Untrusted or invalid game content list.";
		case UPDATE_HASH_MISMATCH: case UPDATE_SIZE_MISMATCH:
		case UPDATE_READ_FAIL: case UPDATE_WRITE_FAIL:
		case UPDATE_NO_NETWORK: case UPDATE_NO_SPACE: case UPDATE_BAD_SERVER: return failureText(result);
		default: return "The update source could not supply game content.";
	}
}

void updateInstallContent(void)
{
	DEVICEHANDLER_INTERFACE *device = devices[DEVICE_CUR];
	uint32_t done = 0;
	uint32_t total = 0;

	if(device == NULL || device->initial == NULL || !(device->features & FEAT_WRITE) ||
		!device->writeFile || !device->deleteFile || !device->renameFile ||
		!device->makeDir || !device->statFile || !device->readFile) {
		updateTell(D_WARN, "Browse a writable card, such as SD, first.");
		return;
	}
	update_check_t *check = newCheck(UPDATE_SOURCE_HTTP, false);
	if(!check) {
		updateTell(D_WARN, "Insufficient memory to install game content.");
		return;
	}
	strcpy(check->channel, "content");
	if(check->result == UPDATE_OK) {
		uiDrawObj_t *box = DrawPublish(DrawProgressBar(true, 0, "Checking for game content"));
		if(!init_network())
			check->result = UPDATE_NO_NETWORK;
		fetchCheck(check);
		DrawDispose(box);
	}
	if(check->result == UPDATE_OK)
		check->result = contentInstall(check, device, &done, &total);
	if(check->result != UPDATE_OK) {
		char message[192];
		print_debug("Content: %s\n", contentFailureText(check->result));
		snprintf(message, sizeof(message), done ?
			"%s\nInstalled %u of %u files; press again to finish." : "%s",
			contentFailureText(check->result), (unsigned)done, (unsigned)total);
		updateTell(D_WARN, message);
	}
	free(check->source.scratch);
	free(check);
}

static void *probeThread(void *arg)
{
	fetchCheck(arg);
	atomic_store(&checkFinished, true);
	return NULL;
}

bool updateStartupPoll(bool atHome)
{
	if(atHome && confirmPending) {
		confirmPending = false;
		confirmTrial();
		trialHome = false;
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
		update_check_t *check = pendingCheck;
		/* A change made in Settings while the request was out wins. */
		bool selected = enabled && !strcmp(check->channel, UIUpdate_ChannelName(swissSettings.updateChannel)) &&
			!strcmp(check->server, swissSettings.updateServer);
		if(!atHome && selected && check->result == UPDATE_OK &&
			UIUpdate_Decide(&check->manifest, INDIGO_BUILD_COMMIT, GIT_TIMESTAMP) == UI_UPDATE_DECIDE_NEWER)
			return true; /* The browser releases its readers before Home installs. */
		LWP_JoinThread(checkThread, NULL);
		pendingCheck = NULL;
		if(!selected) {
			free(check->source.scratch);
			free(check);
		} else {
			finishCheck(check, true);
		}
	}
	return false;
}
