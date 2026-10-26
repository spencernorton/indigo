#include "card_slots.h"
#include "ui_card_slots.h"
#include "deviceHandler.h"
#include "swiss.h"
#include "util.h"

#include <gctypes.h>
#include <ogc/card.h>
#include <ogc/dvd.h>
#include <ogc/exi.h>
#include <ogc/libversion.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mmce.h>
#include <ogc/system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* MMCE_ProbeEx answers BUSY for libogc2's 300 ms after a card arrives. */
#define CARD_SLOTS_PROBE_MS 600u
/* While a card loads, its status byte is looked at this often. */
#define CARD_SLOTS_STATUS_MS 250u
/* A card whose status says it is still loading isn't mounted on the retries
 * (a mount is 80 reads); from this many tries (about 4 s after it came back)
 * it is, in case its status byte says nothing useful. */
#define CARD_SLOTS_STATUS_TRIES 3u

typedef struct {
	uiCardSlot_t state;
	dvddiskid pendingDisk;	/* the ID that waits, and its game's name */
	char pendingName[64];
	bool statusKnown;	/* the status byte was read since the card came back */
	bool statusSane;
	u8 status;
	u64 statusAt;
} cardSlot_t;

static cardSlot_t slots[2];

/* What the slots did, a line an event: to the debug output (a development
 * console's, and Dolphin's log) and, while there is room, into traceText. */
static char traceText[8192];
static size_t traceLength;
static bool traceFull;
#define CARD_TRACE_FULL "[trace full; restart Indigo before another test]\n"
static const char *const phaseNames[] = {
	"idle", "sent", "away", "back", "loading", "ready", "failed"
};
static const char *const readNames[] = {
	"ok", "busy", "broken", "no card", "not a card"
};

static u64 nowMs(void)
{
	/* Sync Clock can step gettime() while Detail is open. Slot deadlines,
	 * insertion waits and the diagnostic timestamps must keep advancing. */
	return ticks_to_millisecs(SYS_GetSystemTime());
}

static void trace(s32 chan, const char *format, ...)
{
	static u64 base;
	char line[112];
	unsigned long ms;
	int head;
	va_list args;
	size_t length;

	/* Seconds from the first event. */
	if(base == 0u) base = nowMs();
	ms = (unsigned long)(nowMs() - base);
	head = snprintf(line, sizeof(line), "%lu.%03lu %c ", ms / 1000u, ms % 1000u,
		chan == EXI_CHANNEL_0 ? 'A' : chan == EXI_CHANNEL_1 ? 'B' : '2');
	va_start(args, format);
	vsnprintf(line + head, sizeof(line) - (size_t)head, format, args);
	va_end(args);
	print_debug("cards: %s\n", line);
	length = strlen(line);
	if(!traceFull && traceLength + length + 2u + sizeof(CARD_TRACE_FULL) < sizeof(traceText)) {
		memcpy(traceText + traceLength, line, length);
		traceLength += length;
		traceText[traceLength++] = '\n';
		traceText[traceLength] = '\0';
	}
	else if(!traceFull) {
		memcpy(traceText + traceLength, CARD_TRACE_FULL, sizeof(CARD_TRACE_FULL));
		traceLength += sizeof(CARD_TRACE_FULL) - 1u;
		traceFull = true;
	}
}

static void traceId(char out[7], const void *id)
{
	memcpy(out, id, 6);
	out[6] = '\0';
}

/* The channel's EXI status register (a host test supplies its own). */
#ifndef CARD_SLOTS_EXI_CSR
#define CARD_SLOTS_EXI_CSR(chan) (*(vu32 *)(0xCC006800 + (u32)(chan) * 0x14))
#endif

/* The slot's presence line: bit 12 of the channel's EXI status register.
 * Reading it puts nothing on the bus. */
static bool slotPresent(s32 chan)
{
	return (CARD_SLOTS_EXI_CSR(chan) & 0x1000) != 0;
}

/* The slot holds the game's or the settings' device (an SD adapter, or an
 * MMCE card's own SD): it is not read as a memory card. */
static bool slotInUse(s32 chan)
{
	u32 location = chan == EXI_CHANNEL_0 ? LOC_MEMCARD_SLOT_A : LOC_MEMCARD_SLOT_B;
	DEVICEHANDLER_INTERFACE *users[2] = {devices[DEVICE_CUR], devices[DEVICE_CONFIG]};
	int i;

	if(MMCE_IsAttached(chan)) {
		trace(chan, "storage: MMCE attached");
		return true;
	}
	for(i = 0; i < 2; i++) {
		if(users[i] != NULL && users[i] != &__device_card_a &&
			users[i] != &__device_card_b && (users[i]->location & location) != 0u) {
			trace(chan, "storage: %s", i == 0 ? "Source" : "settings device");
			return true;
		}
	}
	return false;
}

/* Bounded: at most the insertion window, never the unbounded wait of
 * gameID_early_set. */
static s32 probeMmce(s32 chan, u64 start)
{
	s32 ret;

	while((ret = MMCE_ProbeEx(chan)) == MMCE_RESULT_BUSY &&
		diff_msec(start, SYS_GetSystemTime()) < CARD_SLOTS_PROBE_MS) {
		usleep(1000);
	}
	return ret;
}

/* MMCE_GetDeviceID returns BUSY only when EXI_LockEx failed, before it
 * selected the card or sent a command. Give a temporary bus lock another
 * chance within the SAME insertion budget; never retry a card's negative
 * answer or extend the wait indefinitely. */
static s32 identifyMmce(s32 chan, u32 *id, u64 start, unsigned *busyRetries)
{
	s32 ret;

	*busyRetries = 0u;
	while((ret = MMCE_GetDeviceID(chan, id)) == MMCE_RESULT_BUSY &&
		diff_msec(start, SYS_GetSystemTime()) < CARD_SLOTS_PROBE_MS) {
		(*busyRetries)++;
		usleep(1000);
	}
	return ret;
}

typedef enum {
	SEND_NO_EMULATOR,	/* nothing there takes an ID: a plain card, or none */
	SEND_FAILED,		/* an emulator answered, but the ID didn't go out */
	SEND_OK			/* the card took the ID: it may be switching */
} sendResult_t;

/* The game's ID to the MMCE card in chan, the commands gameID_early_set sends.
 * Set Disc ID is what switches the card: once it went out, it is SEND_OK
 * whatever the name (Set Disc Info, for the card's screen) does after it. */
static sendResult_t sendId(s32 chan, const dvddiskid *disk, const char name[64])
{
	u32 id = 0;
	u64 start = SYS_GetSystemTime();
	unsigned busyRetries;
	s32 ret = probeMmce(chan, start);
	trace(chan, "probe=%ld after=%lums", (long)ret,
		(unsigned long)diff_msec(start, SYS_GetSystemTime()));

	/* A card that took our ID doesn't turn into another device: that is the
	 * ID libogc2 read while it switched, kept until it leaves the bus. Read
	 * it afresh instead of needing the card taken out and put back. */
	if(ret == MMCE_RESULT_WRONGDEVICE && chan <= EXI_CHANNEL_1 && slots[chan].state.mmce) {
		EXI_ProbeReset();
		ret = probeMmce(chan, start);
		trace(chan, "probe after reset=%ld", (long)ret);
	}
	/* Only an emulator that said so (Get Device ID) and then didn't take the
	 * ID is SEND_FAILED; a slot known to hold one is tried again either way
	 * (sendDone), and anything else is no emulator. */
	if(ret < 0) return SEND_NO_EMULATOR;
	ret = identifyMmce(chan, &id, start, &busyRetries);
	trace(chan, "device=%ld id=%08lx busy-retries=%u after=%lums", (long)ret,
		(unsigned long)id, busyRetries, (unsigned long)diff_msec(start, SYS_GetSystemTime()));
	if(ret == MMCE_RESULT_VERSION) ret = MMCE_RESULT_READY;
	if(ret < 0) return SEND_NO_EMULATOR;
	ret = MMCE_SetDiskID(chan, disk);
	trace(chan, "set ID=%ld", (long)ret);
	if(ret != MMCE_RESULT_READY) return SEND_FAILED;
	ret = MMCE_SetDiskInfo(chan, name);
	if(ret != MMCE_RESULT_READY) trace(chan, "name not sent=%ld", (long)ret);
	return SEND_OK;
}

/* What a send means for the slot's state. */
static void sendDone(cardSlot_t *slot, s32 chan, const char id[UI_CARD_SLOT_ID_LENGTH],
	sendResult_t result, const char *name, const char *how)
{
	if(result == SEND_OK) {
		UICardSlot_Sent(&slot->state, id, nowMs());
		trace(chan, "%s sent%s: %s", name, how, phaseNames[slot->state.phase]);
		if(chan == EXI_CHANNEL_0) swissSettings.emulateMemoryCard = 0;
		return;
	}
	if(result == SEND_FAILED || slot->state.mmce) {
		UICardSlot_SendFailed(&slot->state, id, result == SEND_FAILED, nowMs());
		trace(chan, "%s not sent%s: %s", name, how,
			slot->state.hasPending ? "tried again later" : phaseNames[slot->state.phase]);
		if(chan == EXI_CHANNEL_0) swissSettings.emulateMemoryCard = 0;
		return;
	}
	UICardSlot_NotSent(&slot->state);
}

/* The card's status byte (0x83): three bytes on the bus, no mount. */
static bool cardStatus(s32 chan, u8 *status)
{
	u8 command[2] = {0x83, 0x00};
	bool ok;

	if(!EXI_Lock(chan, EXI_DEVICE_0, NULL)) return false;
	if(!EXI_Select(chan, EXI_DEVICE_0, EXI_SPEED16MHZ)) {
		EXI_Unlock(chan);
		return false;
	}
	ok = EXI_ImmEx(chan, command, sizeof(command), EXI_WRITE) &&
		EXI_ImmEx(chan, status, 1, EXI_READ);
	ok = EXI_Deselect(chan) && ok;
	EXI_Unlock(chan);
	return ok;
}

/* No error bit (libogc2's 0x18), and something on the bus: a card still
 * loading may answer nothing (0xFF). 0x00 is an answer: a card not yet
 * unlocked may say just that. */
static bool statusSane(u8 status)
{
	return status != 0xFF && (status & 0x18) == 0;
}

/* A read of the card: probe, mount, unmount. */
static uiCardReadResult_t readCard(s32 chan)
{
	DEVICEHANDLER_INTERFACE *device = chan == EXI_CHANNEL_0 ? &__device_card_a : &__device_card_b;
	s32 result = CARD_ProbeEx(chan, NULL, NULL);
	s32 initResult;
	u64 start;

	trace(chan, "CARD probe=%ld", (long)result);
	if(result == CARD_ERROR_NOCARD) return UI_CARD_READ_NO_CARD;
	if(result == CARD_ERROR_WRONGDEVICE) return UI_CARD_READ_NOT_A_CARD;
	if(result != CARD_ERROR_READY) return UI_CARD_READ_BUSY;
	trace(chan, "mount starts");
	start = SYS_GetSystemTime();
	initResult = device->init(device->initial);
	result = initResult == 0 ? CARD_ERROR_READY : device->initial->status;
	trace(chan, "mount=%ld status=%ld after=%lums", (long)initResult, (long)result,
		(unsigned long)diff_msec(start, SYS_GetSystemTime()));
	device->deinit(device->initial);
	switch(result) {
	case CARD_ERROR_READY: return UI_CARD_READ_OK;
	case CARD_ERROR_BROKEN: return UI_CARD_READ_BROKEN;
	case CARD_ERROR_NOCARD: return UI_CARD_READ_NO_CARD;
	case CARD_ERROR_WRONGDEVICE: return UI_CARD_READ_NOT_A_CARD;
	default: return UI_CARD_READ_BUSY;
	}
}

void CardSlots_RequestGame(const DiskHeader *header)
{
	const dvddiskid *disk = (const dvddiskid *)header;
	char id[UI_CARD_SLOT_ID_LENGTH];
	char name[7];
	s32 chan;

	memcpy(id, header, sizeof(id));
	traceId(name, header);
	/* This branch diagnoses #102: log existing decisions, without adding
	 * probes, changing retries, or changing what a result means. */
	trace(EXI_CHANNEL_2, "build %s %s", GIT_COMMIT, _V_STRING);
	trace(EXI_CHANNEL_2, "%s request disabled=%u saves=%u ntp=%u source=%lx config=%lx", name,
		(unsigned)swissSettings.disableMCPGameID, (unsigned)!swissSettings.hideDetailSaves,
		(unsigned)swissSettings.ntpSync,
		(unsigned long)(devices[DEVICE_CUR] != NULL ? devices[DEVICE_CUR]->location : 0u),
		(unsigned long)(devices[DEVICE_CONFIG] != NULL ? devices[DEVICE_CONFIG]->location : 0u));
	trace(EXI_CHANNEL_2, "console clock=%llums (trace uses steady time)",
		(unsigned long long)ticks_to_millisecs(gettime()));
	for(chan = EXI_CHANNEL_0; chan < EXI_CHANNEL_MAX; chan++) {
		cardSlot_t *slot;

		if(swissSettings.disableMCPGameID & (1 << chan)) {
			trace(chan, "%s skipped: GameID disabled", name);
			continue;
		}
		/* Serial Port 2, or a slot used as a storage device: sent as before,
		 * nothing there to read as a memory card. */
		if(chan > EXI_CHANNEL_1 || slotInUse(chan)) {
			trace(chan, "%s not followed: storage slot or SP2", name);
			if(sendId(chan, disk, header->GameName) == SEND_OK) {
				trace(chan, "%s sent (not followed)", name);
				if(chan == EXI_CHANNEL_0) swissSettings.emulateMemoryCard = 0;
			}
			continue;
		}
		slot = &slots[chan];
		(void)UICardSlot_Observe(&slot->state, slotPresent(chan), nowMs());
		trace(chan, "present=%u prior=%s known-mmce=%u", (unsigned)slot->state.present,
			phaseNames[slot->state.phase], (unsigned)slot->state.mmce);
		/* Kept for a send later: behind a switch, or after a failed one. */
		memcpy(&slot->pendingDisk, disk, sizeof(slot->pendingDisk));
		memcpy(slot->pendingName, header->GameName, sizeof(slot->pendingName));
		if(!UICardSlot_Request(&slot->state, id)) {
			trace(chan, "%s waits: the card is switching (%s)", name,
				phaseNames[slot->state.phase]);
			if(chan == EXI_CHANNEL_0) swissSettings.emulateMemoryCard = 0;
			continue;
		}
		sendDone(slot, chan, id, sendId(chan, disk, header->GameName), name, "");
	}
}

bool CardSlots_Poll(void)
{
	bool settled = false;
	s32 chan;

	for(chan = EXI_CHANNEL_0; chan <= EXI_CHANNEL_1; chan++) {
		cardSlot_t *slot = &slots[chan];
		uiCardSlotPhase_t was = slot->state.phase;
		char id[UI_CARD_SLOT_ID_LENGTH];
		u64 now = nowMs();

		if(was == UI_CARD_SLOT_IDLE && !slot->state.hasPending) continue;
		(void)UICardSlot_Observe(&slot->state, slotPresent(chan), now);
		if(slot->state.phase != UI_CARD_SLOT_LOADING) {
			slot->statusKnown = slot->statusSane = false;
		}
		else if(now - slot->statusAt >= CARD_SLOTS_STATUS_MS) {
			u8 status = 0xFF;

			slot->statusAt = now;
			if(cardStatus(chan, &status)) {
				bool sane = statusSane(status);

				if(status != slot->status || !slot->statusKnown) {
					trace(chan, "status %02x", status);
				}
				if(sane && !slot->statusSane) UICardSlot_Hint(&slot->state, now);
				slot->statusKnown = true;
				slot->statusSane = sane;
				slot->status = status;
			}
		}
		if(UICardSlot_ReadDue(&slot->state, now)) {
			bool reset = false;
			bool loading = slot->statusKnown && !slot->statusSane &&
				slot->state.tries < CARD_SLOTS_STATUS_TRIES;

			if(loading) (void)UICardSlot_Read(&slot->state, UI_CARD_READ_BUSY, now, &reset);
			else {
				uiCardReadResult_t result = readCard(chan);

				trace(chan, "read: %s", readNames[result]);
				(void)UICardSlot_Read(&slot->state, result, nowMs(), &reset);
			}
			if(reset) {
				trace(chan, "stale ID: probe reset");
				EXI_ProbeReset();
			}
		}
		if(UICardSlot_TakePending(&slot->state, id, nowMs())) {
			char name[7];

			traceId(name, id);
			sendDone(slot, chan, id, sendId(chan, &slot->pendingDisk, slot->pendingName),
				name, " after the wait");
		}
		if(slot->state.phase != was) {
			trace(chan, "%s -> %s", phaseNames[was], phaseNames[slot->state.phase]);
		}
		/* Read the saves again when a card is done switching (or given up on),
		 * and when one that was settled leaves the bus: its saves go with it. */
		if(slot->state.phase != was && (slot->state.phase == UI_CARD_SLOT_READY ||
			slot->state.phase == UI_CARD_SLOT_FAILED ||
			(slot->state.phase == UI_CARD_SLOT_AWAY && (was == UI_CARD_SLOT_READY ||
			was == UI_CARD_SLOT_FAILED)))) {
			settled = true;
		}
	}
	return settled;
}

bool CardSlots_ReadableFor(int slot, const char gameId[6])
{
	return slot >= 0 && slot < 2 && UICardSlot_ReadableFor(&slots[slot].state, gameId);
}

bool CardSlots_WaitingFor(const char gameId[6])
{
	return UICardSlot_WaitingFor(&slots[0].state, gameId) ||
		UICardSlot_WaitingFor(&slots[1].state, gameId);
}

bool CardSlots_WritableFor(int slot, const char gameId[6])
{
	return slot >= 0 && slot < 2 &&
		UICardSlot_WritableFor(&slots[slot].state, gameId, nowMs());
}

bool CardSlots_FailedFor(int slot, const char gameId[6])
{
	return slot >= 0 && slot < 2 && UICardSlot_FailedFor(&slots[slot].state, gameId);
}

bool CardSlots_IdWaiting(void)
{
	return slots[0].state.hasPending || slots[1].state.hasPending;
}

const char *CardSlots_Trace(void)
{
	return traceText;
}

void CardSlots_TraceScan(unsigned slotsRead, unsigned copies, bool partial,
	bool waiting, bool failed)
{
	trace(EXI_CHANNEL_2, "save scan slots=%u copies=%u partial=%u waiting=%u failed=%u",
		slotsRead, copies, (unsigned)partial, (unsigned)waiting, (unsigned)failed);
}
