/* Updating Indigo from Settings > Network: the screens, the two ways the
 * bytes arrive, and the careful part — checking them, then swapping them
 * in for the program the console starts.
 *
 * One press owns the whole run, like the file copy it borrows its shape
 * from (swiss.c's manage_file): check, download beside the file it will
 * replace, prove the download twice (its SHA-256 as it lands, and again
 * read back whole, with wiiload_image_valid over it), ask, and only then
 * rename. Until that last rename the install on the card is untouched,
 * and the file it replaces stays beside it as .bak.
 *
 * The console has no TLS and no DNS (docs/SETTINGS.md), so the server is a
 * plain-HTTP IPv4 address and the manifest's SHA-256 — shown before
 * installing so it can be compared with the release page — proves the
 * download matches the list it arrived with. List and file travel the
 * same channel, so that is integrity, not authentication: it does not
 * prove who published them. The reading
 * and deciding live in gui/ui_update.c; the design is
 * docs/dev/software-update-design.md. */

#include <malloc.h>
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
#include "util.h"

#define UPDATE_CHUNK (256u * 1024u)  /* the copy core's piece */
#define UPDATE_HEAD_MAX 4096u        /* an HTTP head, whole */
#define UPDATE_IDLE_SECONDS 10       /* wiiload's read patience */
/* Whole-wait ceilings beside the idle one: a server dribbling a byte
 * inside every idle window still finishes or is given up, and B is
 * watched while waiting. The blocking connect and a single SMB read
 * keep the stack's and the device's own bounds (the design says so). */
#define UPDATE_HEAD_SECONDS 60       /* an HTTP head, whole */
#define UPDATE_MANIFEST_SECONDS 60   /* the update list's body */
#define UPDATE_TRANSFER_SECONDS 600  /* one whole file, either transport */
#define UPDATE_APPLOADER_DIR "swiss/patches"
#define UPDATE_APPLOADER_NAME "swiss/patches/apploader.img"
#define UPDATE_SMB_DIR "indigo-updates"

/* How a run can end, each with its own words for the screen. The order is
 * only this file's; nothing saves these. */
typedef enum {
	UPDATE_OK = 0,
	UPDATE_CANCELLED,
	UPDATE_NOT_CONFIGURED,
	UPDATE_BAD_SERVER,
	UPDATE_NO_NETWORK,
	UPDATE_CONNECT_FAIL,
	UPDATE_TIMEOUT,
	UPDATE_HTTP_STATUS,
	UPDATE_HTTP_UNSUPPORTED,
	UPDATE_SMB_REFUSED,
	UPDATE_SMB_NOT_FOUND,
	UPDATE_MANIFEST_TOO_BIG,
	UPDATE_MANIFEST_INVALID,
	UPDATE_FORMAT_UNSUPPORTED,
	UPDATE_CHANNEL_MISMATCH,
	UPDATE_MIN_BUILD,
	UPDATE_NO_CONFIG_DEVICE,
	UPDATE_TARGET_NOT_FOUND,
	UPDATE_TARGET_AMBIGUOUS,
	UPDATE_NO_SPACE,
	UPDATE_READ_FAIL,
	UPDATE_WRITE_FAIL,
	UPDATE_SIZE_MISMATCH,
	UPDATE_HASH_MISMATCH,
	UPDATE_IMAGE_INVALID,
	UPDATE_SWAP_FAIL,
	UPDATE_SWAP_FAIL_MANUAL
} update_result_t;

/* One file's places on the card: where it lives, where its replacement is
 * staged, and where the replaced one is kept. */
typedef struct {
	file_handle file;
	file_handle staged;
	file_handle backup;
	bool present; /* the file there now (an apploader may not be) */
} update_target_t;

/* The two transports behind one pair of verbs. `display` names the source
 * for the checking box; `detail` carries one error's specifics. */
typedef struct update_source_state update_source_state_t;
struct update_source_state {
	update_result_t (*fetchManifest)(update_source_state_t *source,
		const char *channel, char *buffer, size_t capacity, size_t *length);
	update_result_t (*fetchFile)(update_source_state_t *source,
		const char *name, update_target_t *target, uint32_t expectedSize,
		sha256_ctx *sha, uiDrawObj_t *progress);
	union {
		struct {
			ui_update_base_t base;
			char *chunk;
			char *scratch;
		} http;
		struct {
			char *chunk;
		} smb;
	} state;
	char display[72];
	char detail[128];
};

/* The run's working pieces, all owned by updateRun. */
typedef struct {
	update_source_state_t source;
	ui_update_manifest_t manifest;
	update_target_t dol;
	update_target_t apploader;
	bool mounted; /* this run mounted the Configuration Device */
} update_run_t;

static const char *const BOOT_NAMES[] = {"ipl.dol", "boot.dol", "apps/indigo.dol"};

/* ------------------------------------------------------------------ */
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

/* "<name><suffix>" that must fit: the names here are short and checked. */
static bool updateNameWith(char out[PATHNAME_MAX], const char *name,
	const char *suffix)
{
	size_t used = strlen(name);
	size_t extra = strlen(suffix);

	if(used + extra >= PATHNAME_MAX)
		return false;
	memcpy(out, name, used);
	memcpy(out + used, suffix, extra + 1);
	return true;
}

static void updateTargetInit(update_target_t *target, const char *fullName)
{
	memset(target, 0, sizeof(*target));
	strncpy(target->file.name, fullName, PATHNAME_MAX - 1);
	target->file.device = updateDevice();
	updateNameWith(target->staged.name, fullName, ".new");
	target->staged.device = updateDevice();
	updateNameWith(target->backup.name, fullName, ".bak");
	target->backup.device = updateDevice();
}

static bool updateExists(file_handle *file)
{
	return updateDevice()->statFile(file) == 0;
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

/* The pure swap's hands (ui_update.h): each call dresses a fresh handle
 * in the snapshot name the swap filled in, so what a handler's rename
 * copies back into its handle (the FAT, KunaiGC and FlippyDrive
 * handlers all do) lands in this throwaway, never in a name the swap
 * still has to read. */
static int updateSwapDelete(void *ctx, char *handle, size_t capacity)
{
	DEVICEHANDLER_INTERFACE *device = (DEVICEHANDLER_INTERFACE *)ctx;
	file_handle file;

	(void)capacity;
	memset(&file, 0, sizeof(file));
	strncpy(file.name, handle, PATHNAME_MAX - 1);
	file.device = device;
	return (int)device->deleteFile(&file);
}

static int updateSwapRename(void *ctx, char *handle, size_t capacity,
	const char *newName)
{
	DEVICEHANDLER_INTERFACE *device = (DEVICEHANDLER_INTERFACE *)ctx;
	file_handle file;
	int renamed;

	memset(&file, 0, sizeof(file));
	strncpy(file.name, handle, PATHNAME_MAX - 1);
	file.device = device;
	renamed = (int)device->renameFile(&file, (char *)newName);
	/* Mirror the handler exactly: whatever name it left in its handle
	 * is what the swap's handle now holds (and never reads back). */
	strncpy(handle, file.name, capacity - 1u);
	handle[capacity - 1u] = '\0';
	return renamed;
}

/* A file already replaced, put back; false when even that failed. */
static bool updateRollbackOne(update_target_t *target)
{
	ui_update_swap_ops_t ops;

	ops.ctx = updateDevice();
	ops.deleteFile = updateSwapDelete;
	ops.renameFile = updateSwapRename;
	return UIUpdate_Rollback(&ops, target->file.name, target->backup.name,
		PATHNAME_MAX, target->present);
}

/* current -> .bak, then .new -> current (UIUpdate_Swap's order). */
static update_result_t updateSwapOne(update_target_t *target)
{
	ui_update_swap_ops_t ops;

	ops.ctx = updateDevice();
	ops.deleteFile = updateSwapDelete;
	ops.renameFile = updateSwapRename;
	switch(UIUpdate_Swap(&ops, target->file.name, target->staged.name,
		target->backup.name, PATHNAME_MAX, target->present)) {
		case UI_UPDATE_SWAP_OK:
			return UPDATE_OK;
		case UI_UPDATE_SWAP_MANUAL:
			return UPDATE_SWAP_FAIL_MANUAL;
		default:
			return UPDATE_SWAP_FAIL;
	}
}

static void updateDropStaging(update_run_t *run)
{
	DEVICEHANDLER_INTERFACE *device = updateDevice();

	if(device == NULL)
		return;
	device->deleteFile(&run->dol.staged);
	if(run->manifest.hasApploader)
		device->deleteFile(&run->apploader.staged);
}

/* Writes one staged piece at the handle's own offset. */
static bool updateStageWrite(update_target_t *target, const void *buffer,
	uint32_t length)
{
	return updateDevice()->writeFile(&target->staged, buffer, length) ==
		(s32)length;
}

/* The copy core's finish: a zero write flushes, then the close is checked. */
static bool updateStageFinish(update_target_t *target)
{
	DEVICEHANDLER_INTERFACE *device = updateDevice();

	if(device->writeFile(&target->staged, NULL, 0) != 0) {
		device->closeFile(&target->staged);
		return false;
	}
	return device->closeFile(&target->staged) == 0;
}

/* Read a staged file back whole (boot_dol's pattern) and prove it: the
 * hash of what the card kept, and, for the DOL, a whole program's shape. */
static update_result_t updateVerifyStaged(update_target_t *target,
	uint32_t expectedSize, const uint8_t expectedSha[32], bool isDol,
	uiDrawObj_t *progress)
{
	DEVICEHANDLER_INTERFACE *device = updateDevice();
	update_progress_t pace;
	sha256_ctx sha;
	uint8_t digest[32];
	u8 *buffer;
	u32 done = 0;
	update_result_t result = UPDATE_OK;

	buffer = memalign(32, (size_t)expectedSize + 0x400u);
	if(buffer == NULL)
		return UPDATE_READ_FAIL;
	sha256_init(&sha);
	updateProgressStart(&pace);
	while(done < expectedSize) {
		u32 piece = expectedSize - done;
		if(piece > 131072u)
			piece = 131072u;
		device->seekFile(&target->staged, (s64)done, DEVICE_HANDLER_SEEK_SET);
		if(device->readFile(&target->staged, buffer + done, piece) != (s32)piece) {
			/* Once more, as the copy core retries a read. */
			device->seekFile(&target->staged, (s64)done, DEVICE_HANDLER_SEEK_SET);
			if(device->readFile(&target->staged, buffer + done, piece) != (s32)piece) {
				result = UPDATE_READ_FAIL;
				break;
			}
		}
		sha256_update(&sha, buffer + done, piece);
		done += piece;
		if(!updateProgress(&pace, progress, done, expectedSize)) {
			result = UPDATE_CANCELLED;
			break;
		}
	}
	device->closeFile(&target->staged);
	if(result == UPDATE_OK) {
		sha256_final(&sha, digest);
		if(memcmp(digest, expectedSha, 32) != 0)
			result = UPDATE_HASH_MISMATCH;
		else if(isDol && !wiiload_image_valid(buffer, expectedSize, false))
			result = UPDATE_IMAGE_INVALID;
	}
	free(buffer);
	return result;
}

/* ------------------------------------------------------------------ */
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
	ui_update_base_t *base = &source->state.http.base;
	struct sockaddr_in address;
	char request[192];
	char *head = source->state.http.scratch;
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
	/* One blocking connect: the stack's own timeout bounds it, and B
	 * cannot interrupt this one call (the design says so plainly). */
	if(net_connect(socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
		net_close(socket);
		return UPDATE_CONNECT_FAIL;
	}
	snprintf(request, sizeof(request),
		"GET %s%s HTTP/1.0\r\nHost: %s\r\nAccept: */*\r\n\r\n",
		base->path, leaf, base->host);
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

		if(updateCancelPressed()) {
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
				if(contentLength > UI_UPDATE_MANIFEST_MAX) {
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

	snprintf(leaf, sizeof(leaf), "%s.txt", channel);
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

		if(updateCancelPressed()) {
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
	const char *name, update_target_t *target, uint32_t expectedSize,
	sha256_ctx *sha, uiDrawObj_t *progressBar)
{
	const char *carry;
	size_t carryLength;
	uint32_t total;
	uint32_t done = 0;
	s32 socket;
	update_progress_t pace;
	update_result_t result;
	char *chunk = source->state.http.chunk;

	result = httpOpenBody(source, name, expectedSize, false, &socket,
		&total, &carry, &carryLength);
	if(result != UPDATE_OK)
		return result;
	target->staged.offset = 0;
	updateProgressStart(&pace);
	/* Bytes that arrived with the head count toward the body, never
	 * past its length (the manifest read clamps the same way). */
	if(carryLength > (size_t)total)
		carryLength = (size_t)total;
	if(carryLength > 0) {
		if(!updateStageWrite(target, carry, (uint32_t)carryLength)) {
			net_close(socket);
			return UPDATE_WRITE_FAIL;
		}
		sha256_update(sha, (const uint8_t *)carry, carryLength);
		done = (uint32_t)carryLength;
	}
	while(done < total) {
		uint32_t want = total - done;
		int got;
		if(updateDeadlineHit(pace.started, UPDATE_TRANSFER_SECONDS)) {
			net_close(socket);
			return UPDATE_TIMEOUT;
		}
		if(want > UPDATE_CHUNK)
			want = UPDATE_CHUNK;
		got = httpRecvSome(socket, chunk, want);
		if(got == -1) {
			net_close(socket);
			return UPDATE_TIMEOUT;
		}
		if(got <= 0) {
			net_close(socket);
			return UPDATE_SIZE_MISMATCH;
		}
		if(!updateStageWrite(target, chunk, (uint32_t)got)) {
			net_close(socket);
			return UPDATE_WRITE_FAIL;
		}
		sha256_update(sha, (const uint8_t *)chunk, (size_t)got);
		done += (uint32_t)got;
		if(!updateProgress(&pace, progressBar, done, total)) {
			net_close(socket);
			return UPDATE_CANCELLED;
		}
	}
	net_close(socket);
	if(!updateStageFinish(target))
		return UPDATE_WRITE_FAIL;
	return UPDATE_OK;
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
	snprintf(leaf, sizeof(leaf), "%s.txt", channel);
	smbLeaf(file.name, leaf);
	file.device = &__device_smb;
	if(__device_smb.statFile(&file) != 0) {
		snprintf(source->detail, sizeof(source->detail), "%s", file.name);
		return UPDATE_SMB_NOT_FOUND;
	}
	if(file.size > UI_UPDATE_MANIFEST_MAX || (size_t)file.size >= capacity)
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
	const char *name, update_target_t *target, uint32_t expectedSize,
	sha256_ctx *sha, uiDrawObj_t *progressBar)
{
	file_handle file;
	update_progress_t pace;
	uint32_t done = 0;
	char *chunk = source->state.smb.chunk;

	memset(&file, 0, sizeof(file));
	smbLeaf(file.name, name);
	file.device = &__device_smb;
	if(__device_smb.statFile(&file) != 0) {
		snprintf(source->detail, sizeof(source->detail), "%s", file.name);
		return UPDATE_SMB_NOT_FOUND;
	}
	if(file.size != expectedSize)
		return UPDATE_SIZE_MISMATCH;
	target->staged.offset = 0;
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
	if(!updateStageFinish(target))
		return UPDATE_WRITE_FAIL;
	return UPDATE_OK;
}


/* ------------------------------------------------------------------ */
/* The run itself.                                                     */
/* ------------------------------------------------------------------ */

/* MB as the boxes say it: "4.4 MB". */
static void updateFormatSize(char *out, size_t capacity, uint32_t bytes)
{
	uint32_t tenthMb = (uint32_t)(((uint64_t)bytes * 10u) / 1000000u);

	snprintf(out, capacity, "%u.%u MB", tenthMb / 10u, tenthMb % 10u);
}

/* Which file this console starts is known only by looking: the card
 * is probed for the names a loader starts (sd_package.sh's three). */
static update_result_t updateResolveTargets(update_run_t *run)
{
	char full[PATHNAME_MAX];
	int wanted = swissSettings.updateBootFile;
	int found = -1;
	size_t i;

	if(wanted < 0 || wanted > 3)
		wanted = 0;
	if(wanted != 0) {
		concat_path(full, updateDevice()->initial->name, BOOT_NAMES[wanted - 1]);
		updateTargetInit(&run->dol, full);
		if(!updateExists(&run->dol.file))
			return UPDATE_TARGET_NOT_FOUND;
		run->dol.present = true;
	}
	else {
		int matches = 0;
		for(i = 0; i < sizeof(BOOT_NAMES) / sizeof(BOOT_NAMES[0]); i++) {
			file_handle probe;
			memset(&probe, 0, sizeof(probe));
			concat_path(probe.name, updateDevice()->initial->name, BOOT_NAMES[i]);
			probe.device = updateDevice();
			if(updateExists(&probe)) {
				found = (int)i;
				matches++;
			}
		}
		if(matches == 0)
			return UPDATE_TARGET_NOT_FOUND;
		if(matches > 1)
			return UPDATE_TARGET_AMBIGUOUS;
		concat_path(full, updateDevice()->initial->name, BOOT_NAMES[found]);
		updateTargetInit(&run->dol, full);
		run->dol.present = true;
	}
	concat_path(full, updateDevice()->initial->name, UPDATE_APPLOADER_NAME);
	updateTargetInit(&run->apploader, full);
	run->apploader.present = updateExists(&run->apploader.file);
	return UPDATE_OK;
}

/* The card must be able to hold the staged files beside the live ones.
 * The backup is a rename, so it costs nothing. */
static update_result_t updateCheckSpace(update_run_t *run, uint32_t needed)
{
	device_info *info;

	if(updateDevice()->info == NULL)
		return UPDATE_OK; /* unknown is not a refusal */
	info = updateDevice()->info(updateDevice()->initial);
	/* Nothing free on a card of a known size is a full card, not an
	 * unknown one; only free and total both unknown (0) is unknown. */
	if(info == NULL || (info->freeSpace == 0 && info->totalSpace == 0))
		return UPDATE_OK;
	if(info->freeSpace < (uint64_t)needed) {
		char have[16];
		char need[16];
		updateFormatSize(have, sizeof(have),
			info->freeSpace > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)info->freeSpace);
		updateFormatSize(need, sizeof(need), needed);
		snprintf(run->source.detail, sizeof(run->source.detail),
			"%s free, %s needed", have, need);
		return UPDATE_NO_SPACE;
	}
	return UPDATE_OK;
}

/* One staged file, fetched and proven twice over. */
static update_result_t updateStageOne(update_run_t *run,
	update_target_t *target, const char *name, uint32_t expectedSize,
	const uint8_t expectedSha[32], bool isDol)
{
	char label[80];
	sha256_ctx sha;
	uint8_t digest[32];
	uiDrawObj_t *bar;
	update_result_t result;

	snprintf(label, sizeof(label), "Downloading %s\205", name);
	bar = DrawPublish(DrawProgressBar(false, 0, label));
	sha256_init(&sha);
	result = run->source.fetchFile(&run->source, name, target,
		expectedSize, &sha, bar);
	DrawDispose(bar);
	if(result != UPDATE_OK)
		return result;
	sha256_final(&sha, digest);
	if(memcmp(digest, expectedSha, 32) != 0)
		return UPDATE_HASH_MISMATCH;
	snprintf(label, sizeof(label), "Checking %s\205", name);
	bar = DrawPublish(DrawProgressBar(false, 0, label));
	result = updateVerifyStaged(target, expectedSize, expectedSha, isDol, bar);
	DrawDispose(bar);
	return result;
}

/* The words for how a run ended badly. One cause, one next step. */
static void updateReportFailure(update_run_t *run, update_result_t result)
{
	char message[300];
	const char *detail = run->source.detail;

	switch(result) {
		case UPDATE_CANCELLED:
			updateTell(D_INFO, "Update cancelled.\nNothing was changed.");
			return;
		case UPDATE_NOT_CONFIGURED:
			if(run->source.fetchManifest == smbFetchManifest)
				updateTell(D_FAIL, "Set SMB Host IP and SMB Share\nabove first, then try again.");
			else
				updateTell(D_FAIL, "No update server is set.\nFill in Update Server above.");
			return;
		case UPDATE_BAD_SERVER:
			snprintf(message, sizeof(message), "Update Server isn't usable:\n%s", detail);
			break;
		case UPDATE_NO_NETWORK:
			snprintf(message, sizeof(message), "%s",
				"The network didn't start.\nCheck the adapter and cable.");
			break;
		case UPDATE_CONNECT_FAIL:
			snprintf(message, sizeof(message), "%s",
				"Couldn't reach the server.\nCheck its address, then try again.");
			break;
		case UPDATE_TIMEOUT:
			snprintf(message, sizeof(message), "%s",
				"The server stopped answering.\nNothing was installed.");
			break;
		case UPDATE_HTTP_STATUS:
			snprintf(message, sizeof(message),
				"The server answered %s.\nNothing was installed.", detail);
			break;
		case UPDATE_HTTP_UNSUPPORTED:
			snprintf(message, sizeof(message),
				"The server sent %s.\nServe plain static files instead.", detail);
			break;
		case UPDATE_SMB_REFUSED:
			snprintf(message, sizeof(message), "%s",
				"The share refused the connection.\nIts server must allow SMB 1.0.");
			break;
		case UPDATE_SMB_NOT_FOUND:
			snprintf(message, sizeof(message), "Not on the share:\n%s", detail);
			break;
		case UPDATE_MANIFEST_TOO_BIG:
			snprintf(message, sizeof(message), "%s",
				"The update list is too big\nto be an update list.");
			break;
		case UPDATE_MANIFEST_INVALID:
			snprintf(message, sizeof(message), "%s",
				"The update list isn't readable.\nNothing was installed.");
			break;
		case UPDATE_FORMAT_UNSUPPORTED:
			snprintf(message, sizeof(message), "%s",
				"This update needs a newer Indigo.\nUpdate from a computer once.");
			break;
		case UPDATE_CHANNEL_MISMATCH:
			snprintf(message, sizeof(message), "%s",
				"The update list is for\nanother channel.");
			break;
		case UPDATE_MIN_BUILD:
			snprintf(message, sizeof(message), "Install %s first:\nthis update builds on it.",
				run->manifest.minVersion[0] ? run->manifest.minVersion : "an older release");
			break;
		case UPDATE_NO_CONFIG_DEVICE:
			snprintf(message, sizeof(message), "%s",
				"No settings device to update on.\nSet Configuration Device first.");
			break;
		case UPDATE_TARGET_NOT_FOUND:
			snprintf(message, sizeof(message), "%s",
				"No Indigo file to replace here.\nUpdate from a computer instead.");
			break;
		case UPDATE_TARGET_AMBIGUOUS:
			snprintf(message, sizeof(message), "%s",
				"More than one Indigo file here.\nSet Update Boot File above.");
			break;
		case UPDATE_NO_SPACE:
			snprintf(message, sizeof(message), "Not enough room on the card:\n%s.", detail);
			break;
		case UPDATE_READ_FAIL:
			snprintf(message, sizeof(message), "%s",
				"The download couldn't be read back.\nNothing was installed.");
			break;
		case UPDATE_WRITE_FAIL:
			snprintf(message, sizeof(message), "%s",
				"The card wouldn't take the download.\nNothing was installed.");
			break;
		case UPDATE_SIZE_MISMATCH:
			snprintf(message, sizeof(message), "%s",
				"The download is the wrong size.\nNothing was installed.");
			break;
		case UPDATE_HASH_MISMATCH:
			snprintf(message, sizeof(message), "%s",
				"The download doesn't match\nits update list. Not installed.");
			break;
		case UPDATE_IMAGE_INVALID:
			snprintf(message, sizeof(message), "%s",
				"The download isn't a whole\nIndigo program. Not installed.");
			break;
		case UPDATE_SWAP_FAIL:
			/* "Nothing was changed" is only ever said of a boot file
			 * proven still there; otherwise name what survives. */
			if(updateExists(&run->dol.file))
				snprintf(message, sizeof(message), "%s",
					"The install couldn't finish.\nNothing was changed.");
			else
				snprintf(message, sizeof(message),
					"The install couldn't finish.\n%s is missing; rename %s.bak\nback on a computer.",
					run->dol.file.name, run->dol.file.name);
			break;
		default:
			snprintf(message, sizeof(message), "%s", "The update failed.");
			break;
	}
	updateTell(D_FAIL, message);
}

static bool updateAdapterPresent(void)
{
	return net_initialized || bba_exists(LOC_ANY);
}

/* Everything before the network: the card, its writable device, and the
 * file this console starts. */
static update_result_t updatePrepare(update_run_t *run)
{
	DEVICEHANDLER_INTERFACE *device;
	update_result_t result;

	if(!updateAdapterPresent())
		return UPDATE_NO_NETWORK;
	if(devices[DEVICE_CONFIG] == NULL) {
		if(!config_set_device())
			return UPDATE_NO_CONFIG_DEVICE;
		run->mounted = devices[DEVICE_CONFIG] != devices[DEVICE_CUR];
	}
	device = updateDevice();
	if(!(device->features & FEAT_WRITE) ||
		!(device->features & FEAT_CONFIG_DEVICE) ||
		device->statFile == NULL || device->readFile == NULL ||
		device->writeFile == NULL || device->closeFile == NULL ||
		device->deleteFile == NULL || device->renameFile == NULL)
		return UPDATE_NO_CONFIG_DEVICE;
	result = updateResolveTargets(run);
	if(result != UPDATE_OK)
		return result;
	/* Leftovers of an interrupted run are nobody's install. */
	device->deleteFile(&run->dol.staged);
	device->deleteFile(&run->apploader.staged);
	return UPDATE_OK;
}

void updateRun(enum update_source source)
{
	update_run_t *run = calloc(1, sizeof(*run));
	update_source_state_t *transport;
	update_result_t result = UPDATE_OK;
	uiDrawObj_t *box = NULL;
	char message[300];
	char sizeText[16];
	char hex16[17];
	char *manifestText = NULL;
	char *chunk = NULL;
	char *scratch = NULL;
	const char *channel;
	bool answered;
	bool stagingLive = false;
	update_target_t *failedTarget = NULL; /* the file a swap stopped on */

	if(run == NULL) {
		updateTell(D_FAIL, "Not enough memory to update.");
		return;
	}
	transport = &run->source;
	channel = UIUpdate_ChannelName(swissSettings.updateChannel);
	print_debug("Update: %s channel, build %s\n",
		source == UPDATE_SOURCE_SMB ? "SMB" : "HTTP", GIT_COMMIT);

	/* The source, ready before anything is asked of the network. */
	if(source == UPDATE_SOURCE_SMB) {
		transport->fetchManifest = smbFetchManifest;
		transport->fetchFile = smbFetchFile;
		snprintf(transport->display, sizeof(transport->display),
			"the SMB share");
		if(!swissSettings.smbShare[0] || !swissSettings.smbServerIp[0]) {
			result = UPDATE_NOT_CONFIGURED;
			goto report;
		}
	}
	else {
		const char *serverText = swissSettings.updateServer[0] ?
			swissSettings.updateServer : UPDATE_DEFAULT_SERVER;
		ui_update_base_err_t why = UI_UPDATE_BASE_BAD;

		transport->fetchManifest = httpFetchManifest;
		transport->fetchFile = httpFetchFile;
		if(serverText[0] == '\0') {
			result = UPDATE_NOT_CONFIGURED;
			goto report;
		}
		if(!UIUpdate_ParseBase(serverText, &transport->state.http.base, &why)) {
			snprintf(transport->detail, sizeof(transport->detail), "%s",
				why == UI_UPDATE_BASE_HTTPS ?
					"HTTPS can't be used:\nthere is no TLS in Indigo." :
				why == UI_UPDATE_BASE_NAME ?
					"Use an IPv4 address:\nthe console has no DNS." :
					"Not an IPv4 address,\nport and folder.");
			result = UPDATE_BAD_SERVER;
			goto report;
		}
		snprintf(transport->display, sizeof(transport->display),
			"%s%s", transport->state.http.base.host,
			transport->state.http.base.path);
		scratch = malloc(UPDATE_HEAD_MAX + 1u);
		transport->state.http.scratch = scratch;
		if(scratch == NULL) {
			result = UPDATE_READ_FAIL;
			goto report;
		}
	}

	result = updatePrepare(run);
	if(result != UPDATE_OK)
		goto report;

	chunk = memalign(32, UPDATE_CHUNK);
	manifestText = malloc(UI_UPDATE_MANIFEST_MAX + 1u);
	if(chunk == NULL || manifestText == NULL) {
		result = UPDATE_READ_FAIL;
		goto report;
	}
	if(source == UPDATE_SOURCE_SMB)
		transport->state.smb.chunk = chunk;
	else
		transport->state.http.chunk = chunk;

	/* The network, then the manifest. */
	box = DrawPublish(DrawProgressBar(true, 0, "Starting network\205"));
	if(source == UPDATE_SOURCE_SMB) {
		result = smbStart(transport);
	}
	else if(!init_network()) {
		result = UPDATE_NO_NETWORK;
	}
	if(result != UPDATE_OK) {
		DrawDispose(box);
		box = NULL;
		goto report;
	}
	snprintf(message, sizeof(message), "Checking %s\205", transport->display);
	box = DrawRepublish(box, DrawProgressBar(true, 0, message));
	{
		size_t manifestLength = 0;
		ui_update_err_t parsed;

		result = transport->fetchManifest(transport, channel, manifestText,
			UI_UPDATE_MANIFEST_MAX + 1u, &manifestLength);
		DrawDispose(box);
		box = NULL;
		if(result != UPDATE_OK)
			goto report;
		parsed = UIUpdate_ParseManifest(manifestText, manifestLength,
			channel, &run->manifest);
		if(parsed != UI_UPDATE_OK) {
			result = parsed == UI_UPDATE_FORMAT ? UPDATE_FORMAT_UNSUPPORTED :
				parsed == UI_UPDATE_CHANNEL ? UPDATE_CHANNEL_MISMATCH :
				UPDATE_MANIFEST_INVALID;
			goto report;
		}
	}

	/* Newer, the same, older, or not yet. */
	switch(UIUpdate_Decide(&run->manifest, GIT_COMMIT,
		(int64_t)GIT_TIMESTAMP)) {
		case UI_UPDATE_DECIDE_MIN_BUILD:
			result = UPDATE_MIN_BUILD;
			goto report;
		case UI_UPDATE_DECIDE_CURRENT:
			snprintf(message, sizeof(message),
				"Indigo is up to date.\nBuild %s is the newest here.", GIT_COMMIT);
			updateTell(D_INFO, message);
			goto done;
		case UI_UPDATE_DECIDE_OLDER:
			snprintf(message, sizeof(message),
				"This build is older than yours.\n"
				"%s (%.7s) onto %s?\n"
				"A  DOWNLOAD    B  CANCEL",
				run->manifest.version, run->manifest.commit,
				run->dol.file.name);
			if(!updateAsk(D_WARN, message, &answered)) {
				result = UPDATE_CANCELLED;
				goto report;
			}
			break;
		case UI_UPDATE_DECIDE_NEWER:
		default:
			updateFormatSize(sizeText, sizeof(sizeText),
				run->manifest.size + run->manifest.apploaderSize);
			snprintf(message, sizeof(message),
				"Indigo %s (%.7s), %s\n"
				"%s to %s (now %s)\n"
				"A  DOWNLOAD    B  CANCEL",
				run->manifest.version, run->manifest.commit,
				run->manifest.date, sizeText, run->dol.file.name, GIT_COMMIT);
			if(!updateAsk(D_INFO, message, &answered)) {
				result = UPDATE_CANCELLED;
				goto report;
			}
			break;
	}
	if(!run->manifest.hasApploader) {
		updateTell(D_WARN,
			"This update has no apploader.img.\nIn-Game Reset keeps the old one.");
	}

	/* Room first, then the downloads beside what they replace. */
	result = updateCheckSpace(run,
		run->manifest.size + run->manifest.apploaderSize);
	if(result != UPDATE_OK)
		goto report;
	stagingLive = true;
	result = updateStageOne(run, &run->dol, run->manifest.file,
		run->manifest.size, run->manifest.sha256, true);
	if(result == UPDATE_OK && run->manifest.hasApploader) {
		ensure_path(DEVICE_CONFIG, "swiss", NULL, true);
		ensure_path(DEVICE_CONFIG, UPDATE_APPLOADER_DIR, NULL, false);
		result = updateStageOne(run, &run->apploader,
			run->manifest.apploaderFile, run->manifest.apploaderSize,
			run->manifest.apploaderSha256, false);
	}
	if(result != UPDATE_OK)
		goto report;

	/* Matched against the list it came with (same channel, so this is
	 * integrity, not proof of who published it); the hash is there to
	 * cross-check against the release page. Last way back. */
	UIUpdate_Hex16(run->manifest.sha256, hex16);
	snprintf(message, sizeof(message),
		"Matches its update list (SHA-256 %s).\nInstall over %s?\nA  INSTALL    B  CANCEL",
		hex16, run->dol.file.name);
	if(!updateAsk(D_WARN, message, &answered)) {
		result = UPDATE_CANCELLED;
		goto report;
	}

	/* The swap: apploader first, the boot file last, and no cancelling. */
	box = DrawPublish(DrawProgressBar(true, 0,
		"Installing\205 do not switch off."));
	if(run->manifest.hasApploader) {
		result = updateSwapOne(&run->apploader);
		if(result != UPDATE_OK)
			failedTarget = &run->apploader;
	}
	if(result == UPDATE_OK) {
		update_result_t dolResult = updateSwapOne(&run->dol);
		if(dolResult != UPDATE_OK) {
			failedTarget = &run->dol;
			if(run->manifest.hasApploader &&
				dolResult != UPDATE_SWAP_FAIL_MANUAL &&
				!updateRollbackOne(&run->apploader)) {
				/* The pair travels together, and the apploader
				 * would not go back either: its name is the one
				 * now needing a computer (the boot file was
				 * restored by its own swap). */
				failedTarget = &run->apploader;
				dolResult = UPDATE_SWAP_FAIL_MANUAL;
			}
		}
		result = dolResult;
	}
	DrawDispose(box);
	box = NULL;
	stagingLive = false;
	if(result == UPDATE_SWAP_FAIL)
		updateDropStaging(run); /* old files restored; .new discarded */
	if(result == UPDATE_SWAP_FAIL_MANUAL) {
		/* Say exactly what is left, from looking, so the rename on a
		 * computer is mechanical. The handles name their files again
		 * (the swap restores them), so these probes are truthful. */
		update_target_t *target = failedTarget != NULL ?
			failedTarget : &run->dol;
		bool hasBackup = updateExists(&target->backup);
		bool hasStaged = updateExists(&target->staged);

		if(hasBackup)
			snprintf(message, sizeof(message),
				"Install stopped: %s is missing.\nStill there: %s%s.\nRename it back on a computer.",
				target->file.name, target->backup.name,
				hasStaged ? " and the .new beside it" : "");
		else
			snprintf(message, sizeof(message),
				"Install stopped: %s is missing.\n%sPut Indigo back from a computer.",
				target->file.name,
				hasStaged ? "Its .new is still beside it.\n" : "");
		updateTell(D_FAIL, message);
		goto done;
	}
	if(result != UPDATE_OK)
		goto report;

	/* Restart through the path just replaced, or not at all. */
	snprintf(message, sizeof(message),
		"Indigo %s is installed.\nRestart to start it.\nA  RESTART NOW    B  LATER",
		run->manifest.version);
	if(updateAsk(D_INFO, message, &answered)) {
		DrawShutdown();
		SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
		__builtin_unreachable();
	}
	goto done;

report:
	if(stagingLive)
		updateDropStaging(run);
	updateReportFailure(run, result);
done:
	if(run->mounted)
		config_unset_device();
	free(chunk);
	free(manifestText);
	free(scratch);
	free(run);
}
