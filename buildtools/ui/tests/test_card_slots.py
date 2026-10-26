#!/usr/bin/env python3
"""card_slots.c, the real file, against stubbed MMCE, CARD and EXI calls.

The state machine has its own test (test_ui_card_slots.c); this one checks
what only the glue decides: what a send's outcome means for the slot, that a
queued ID survives a failed send, that Detail is told when a settled card
leaves the bus, that the probe wait is bounded, that a status byte saying
"loading" holds the mount back and a stale card ID is reset (indigo#102)."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GUI = ROOT / "cube/swiss/source/gui"

HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_card_slots.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef volatile uint32_t vu32;
#define EXI_CHANNEL_0 0
#define EXI_CHANNEL_1 1
#define EXI_CHANNEL_2 2
#define EXI_CHANNEL_MAX 3
#define EXI_DEVICE_0 0
#define EXI_SPEED16MHZ 4
#define EXI_READ 0
#define EXI_WRITE 1
#define MMCE_RESULT_READY 0
#define MMCE_RESULT_BUSY (-1)
#define MMCE_RESULT_WRONGDEVICE (-2)
#define MMCE_RESULT_NOCARD (-3)
#define MMCE_RESULT_VERSION (-4)
#define CARD_ERROR_READY 0
#define CARD_ERROR_BUSY (-1)
#define CARD_ERROR_WRONGDEVICE (-2)
#define CARD_ERROR_NOCARD (-3)
#define CARD_ERROR_BROKEN (-6)
#define GIT_COMMIT "host-test"
#define _V_STRING "stub-sdk"
#define LOC_MEMCARD_SLOT_A 1u
#define LOC_MEMCARD_SLOT_B 2u
#define DEVICE_CUR 0
#define DEVICE_CONFIG 1
typedef struct { char gamename[4]; char company[2]; u8 disknum, gamever; u8 rest[24]; } dvddiskid;
typedef struct { char id[8]; u8 rest[24]; char GameName[64]; } DiskHeader;
typedef struct { s32 status; } file_handle;
typedef struct device {
	file_handle *initial; u32 location;
	s32 (*init)(file_handle *); s32 (*deinit)(file_handle *);
} DEVICEHANDLER_INTERFACE;
static struct { unsigned disableMCPGameID; int emulateMemoryCard, hideDetailSaves; } swissSettings;
static DEVICEHANDLER_INTERFACE __device_card_a, __device_card_b;
static DEVICEHANDLER_INTERFACE *devices[2];
static file_handle initial[2];

/* A fake clock: usleep moves it, as the bounded waits do on a console. */
static u64 fakeMs = 1000u;
static u64 gettime(void) { return fakeMs; }
#define ticks_to_millisecs(t) (t)
static u32 diff_msec(u64 a, u64 b) { return (u32)(b - a); }
static int usleep_ms(unsigned us) { fakeMs += us >= 1000u ? us / 1000u : 1u; return 0; }
#define usleep usleep_ms
static void print_debug(const char *format, ...) { (void)format; }

/* The slot: its presence line, and how it answers. */
static u32 fakeCsr[3];
#define CARD_SLOTS_EXI_CSR(chan) (fakeCsr[chan])
static bool emulator[2], busyForever[2];
static s32 setIdResult[2], setInfoResult[2], cardProbe[2], cardInit[2], deviceIdResult[2];
static u8 status[2];
static unsigned setIdCalls[2], inits[2], resets, statusReads[2];
static unsigned probeCalls[2], deviceIdCalls[2];
static void present(int slot, bool on) { fakeCsr[slot] = on ? 0x1000u : 0u; }
static s32 MMCE_ProbeEx(s32 chan)
{
	if(chan > 1) return MMCE_RESULT_NOCARD;
	probeCalls[chan]++;
	if(busyForever[chan]) return MMCE_RESULT_BUSY;
	if(!(fakeCsr[chan] & 0x1000u)) return MMCE_RESULT_NOCARD;
	return MMCE_RESULT_READY;
}
static s32 MMCE_GetDeviceID(s32 chan, u32 *id) {
	deviceIdCalls[chan]++;
	if(deviceIdResult[chan] != MMCE_RESULT_BUSY) *id = 0x38420101u;
	return emulator[chan] ? deviceIdResult[chan] : MMCE_RESULT_WRONGDEVICE;
}
static s32 MMCE_SetDiskID(s32 chan, const dvddiskid *disk) { (void)disk; setIdCalls[chan]++; return setIdResult[chan]; }
static s32 MMCE_SetDiskInfo(s32 chan, const char *name) { (void)name; return setInfoResult[chan]; }
static bool MMCE_IsAttached(s32 chan) { (void)chan; return false; }
static s32 CARD_ProbeEx(s32 chan, void *a, void *b) { (void)a; (void)b; return (fakeCsr[chan] & 0x1000u) ? cardProbe[chan] : CARD_ERROR_NOCARD; }
static s32 cardInitStub(file_handle *f) { int slot = f == &initial[1]; inits[slot]++; f->status = cardInit[slot]; return cardInit[slot] == CARD_ERROR_READY ? 0 : 5; }
static s32 cardDeinitStub(file_handle *f) { (void)f; return 0; }
static int locked;
static s32 EXI_Lock(s32 chan, s32 dev, void *cb) { (void)chan; (void)dev; (void)cb; if(locked) return 0; locked = 1; return 1; }
static s32 EXI_Select(s32 chan, s32 dev, s32 speed) { (void)dev; (void)speed; return (fakeCsr[chan] & 0x1000u) != 0; }
static s32 EXI_ImmEx(s32 chan, void *data, u32 length, u32 mode) { if(mode == EXI_READ && length == 1u) { *(u8 *)data = status[chan]; statusReads[chan]++; } return 1; }
static s32 EXI_Deselect(s32 chan) { (void)chan; return 1; }
static s32 EXI_Unlock(s32 chan) { (void)chan; locked = 0; return 1; }
static void EXI_ProbeReset(void) { resets++; }
'''

MAIN = r'''
static const char A[6] = {'G', 'A', 'C', 'Z', '0', '1'};
static const char B[6] = {'G', 'C', 'H', 'Z', '0', '1'};
static DiskHeader gameA, gameB;

static void reset(void)
{
	memset(slots, 0, sizeof(slots));
	memset(&swissSettings, 0, sizeof(swissSettings));
	memset(devices, 0, sizeof(devices));
	memset(probeCalls, 0, sizeof(probeCalls));
	memset(deviceIdCalls, 0, sizeof(deviceIdCalls));
	traceText[0] = '\0'; traceLength = 0u;
	memset(setIdCalls, 0, sizeof(setIdCalls));
	memset(inits, 0, sizeof(inits));
	memset(statusReads, 0, sizeof(statusReads));
	resets = 0u;
	for(int i = 0; i < 2; i++) {
		emulator[i] = false; busyForever[i] = false;
		setIdResult[i] = setInfoResult[i] = MMCE_RESULT_READY;
		deviceIdResult[i] = MMCE_RESULT_READY;
		cardProbe[i] = CARD_ERROR_READY; cardInit[i] = CARD_ERROR_READY;
		status[i] = 0x41u; present(i, i == 0);
	}
	emulator[0] = true;
}

/* Polls a retrace at a time for ms; true if any poll said "read again". */
static bool run(u64 ms)
{
	bool told = false;
	u64 end = fakeMs + ms;

	while(fakeMs < end) {
		fakeMs += 16u;
		told |= CardSlots_Poll();
	}
	return told;
}

/* The ID out, the card off the bus 0.3 s later for 0.8 s, then loaded. */
static void switchCard(void)
{
	run(300u);
	present(0, false);
	run(800u);
	present(0, true);
	run(4000u);
}

int main(void)
{
	memcpy(gameA.id, A, 6); memcpy(gameB.id, B, 6);
	strcpy(gameA.GameName, "Astral Circuit"); strcpy(gameB.GameName, "Cobalt Harrier");
	__device_card_a.initial = &initial[0]; __device_card_b.initial = &initial[1];
	__device_card_a.init = __device_card_b.init = cardInitStub;
	__device_card_a.deinit = __device_card_b.deinit = cardDeinitStub;

	/* Capture the previously silent exits without extra bus transactions or
	 * changing their classification. A busy first identity read remains a
	 * candidate explanation, not a claimed hardware fix. */
	reset(); present(0, false);
	CardSlots_RequestGame(&gameA);
	assert(strstr(CardSlots_Trace(), "probe=-3") != NULL);
	assert(probeCalls[0] == 1u && deviceIdCalls[0] == 0u && setIdCalls[0] == 0u);
	reset(); swissSettings.disableMCPGameID = 1u;
	CardSlots_RequestGame(&gameA);
	assert(strstr(CardSlots_Trace(), "A GACZ01 skipped: GameID disabled") != NULL);
	assert(probeCalls[0] == 0u && deviceIdCalls[0] == 0u && setIdCalls[0] == 0u);
	reset(); deviceIdResult[0] = MMCE_RESULT_BUSY;
	CardSlots_RequestGame(&gameA);
	assert(strstr(CardSlots_Trace(), "device=-1 id=00000000") != NULL);
	assert(probeCalls[0] == 1u && deviceIdCalls[0] == 1u && setIdCalls[0] == 0u);
	assert(slots[0].state.phase == UI_CARD_SLOT_IDLE && !CardSlots_IdWaiting());
	reset(); deviceIdResult[0] = MMCE_RESULT_VERSION;
	CardSlots_RequestGame(&gameA);
	assert(strstr(CardSlots_Trace(), "device=-4 id=38420101") != NULL);
	assert(deviceIdCalls[0] == 1u && setIdCalls[0] == 1u && CardSlots_WaitingFor(A));
	assert(strstr(CardSlots_Trace(), "build host-test stub-sdk") != NULL);
	CardSlots_TraceScan(2u, 0u, false);
	assert(strstr(CardSlots_Trace(), "save scan slots=2 copies=0 partial=0") != NULL);

	/* The card takes the ID, the name doesn't go out: it is switching, not a
	 * plain card (review finding: it was IDLE, readable and writable). */
	reset(); setInfoResult[0] = MMCE_RESULT_NOCARD;
	CardSlots_RequestGame(&gameA);
	assert(setIdCalls[0] == 1u && slots[0].state.phase == UI_CARD_SLOT_SENT);
	assert(!CardSlots_ReadableFor(0, A) && !CardSlots_WritableFor(0, A));
	assert(CardSlots_WaitingFor(A));
	switchCard();
	assert(slots[0].state.phase == UI_CARD_SLOT_READY && CardSlots_ReadableFor(0, A));

	/* An emulator that doesn't take the ID: kept, sent again after 1 s, never
	 * read meanwhile; a launch's wait sees it waiting. */
	reset(); setIdResult[0] = MMCE_RESULT_NOCARD;
	CardSlots_RequestGame(&gameA);
	assert(setIdCalls[0] == 1u && CardSlots_IdWaiting() && CardSlots_WaitingFor(A));
	assert(!CardSlots_ReadableFor(0, A) && !CardSlots_WritableFor(0, A));
	run(500u);
	assert(setIdCalls[0] == 1u);
	setIdResult[0] = MMCE_RESULT_READY;
	run(700u);
	assert(setIdCalls[0] == 2u && slots[0].state.phase == UI_CARD_SLOT_SENT);
	assert(!CardSlots_IdWaiting());

	/* One that never takes it: five sends, then given up (FAILED, not IDLE);
	 * the launch's wait ends. */
	reset(); setIdResult[0] = MMCE_RESULT_NOCARD;
	CardSlots_RequestGame(&gameA);
	run(20000u);
	assert(setIdCalls[0] == UI_CARD_SLOT_SEND_TRIES);
	assert(!CardSlots_IdWaiting() && CardSlots_FailedFor(0, A));
	assert(!CardSlots_ReadableFor(0, A) && !CardSlots_WritableFor(0, A));

	/* B asked for while A switches; B's send fails once after A is done: B
	 * stays queued and goes out on the retry (review finding: it was lost). */
	reset();
	CardSlots_RequestGame(&gameA);
	run(300u);
	present(0, false);
	CardSlots_RequestGame(&gameB);
	assert(setIdCalls[0] == 1u && CardSlots_WaitingFor(B) && CardSlots_IdWaiting());
	setIdResult[0] = MMCE_RESULT_NOCARD;
	run(800u);
	present(0, true);
	run(3000u);
	assert(setIdCalls[0] >= 2u && CardSlots_IdWaiting() && CardSlots_WaitingFor(B));
	assert(!CardSlots_ReadableFor(0, B));
	{
		unsigned failed = setIdCalls[0];

		setIdResult[0] = MMCE_RESULT_READY;
		run(4500u);
		assert(setIdCalls[0] == failed + 1u && !CardSlots_IdWaiting());
	}
	assert(memcmp(slots[0].state.id, B, 6) == 0 && slots[0].state.phase != UI_CARD_SLOT_IDLE &&
		slots[0].state.phase != UI_CARD_SLOT_FAILED);

	/* A settled card taken off the bus: Detail is told (review finding: it
	 * kept the old saves). */
	reset();
	CardSlots_RequestGame(&gameA);
	switchCard();
	assert(slots[0].state.phase == UI_CARD_SLOT_READY);
	present(0, false);
	assert(run(100u));
	assert(!CardSlots_ReadableFor(0, A) && CardSlots_WaitingFor(A));

	/* A plain card answers no Get Device ID: read and written as it is. */
	reset(); emulator[0] = false;
	CardSlots_RequestGame(&gameA);
	assert(setIdCalls[0] == 0u && slots[0].state.phase == UI_CARD_SLOT_IDLE);
	assert(CardSlots_ReadableFor(0, A) && CardSlots_WritableFor(0, A));
	assert(!run(2000u) && inits[0] == 0u);

	/* A probe that stays busy is waited on 600 ms at most, and then not
	 * taken for an emulator. */
	reset(); busyForever[0] = true;
	{
		u64 before = fakeMs;
		CardSlots_RequestGame(&gameA);
		assert(fakeMs - before <= 700u);
	}
	assert(slots[0].state.phase == UI_CARD_SLOT_IDLE && !slots[0].state.mmce);

	/* Status 0xFF while loading holds the mount back; turning sane mounts at
	 * once. */
	reset(); status[0] = 0xFFu;
	CardSlots_RequestGame(&gameA);
	run(300u); present(0, false); run(800u); present(0, true);
	run(2500u);
	assert(inits[0] == 0u && statusReads[0] > 0u);
	status[0] = 0x41u;
	run(300u);
	assert(inits[0] == 1u && slots[0].state.phase == UI_CARD_SLOT_READY);

	/* A stale card ID (CARD says another device) is reset, then read. */
	reset(); cardProbe[0] = CARD_ERROR_WRONGDEVICE;
	CardSlots_RequestGame(&gameA);
	run(300u); present(0, false); run(800u); present(0, true);
	run(1200u);
	assert(resets >= 1u && slots[0].state.phase == UI_CARD_SLOT_LOADING);
	cardProbe[0] = CARD_ERROR_READY;
	run(3000u);
	assert(slots[0].state.phase == UI_CARD_SLOT_READY);

	/* A broken card while it loads is read again; turning fine, ready. */
	reset(); cardInit[0] = CARD_ERROR_BROKEN;
	CardSlots_RequestGame(&gameA);
	switchCard();
	assert(slots[0].state.phase == UI_CARD_SLOT_LOADING && inits[0] >= 1u);
	cardInit[0] = CARD_ERROR_READY;
	run(5000u);
	assert(slots[0].state.phase == UI_CARD_SLOT_READY);

	puts("card slots glue: partial sends, kept and retried IDs, given up as FAILED, refresh on leaving the bus, plain cards, bounded probe, status-held mounts, stale ID reset PASS");
	return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = "\n".join(line for line in (GUI / "card_slots.c").read_text().splitlines()
                       if not line.startswith("#include"))
    with tempfile.TemporaryDirectory(prefix="card-slots-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(HARNESS + source + MAIN)
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200112L",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-g",
                   "-I", str(GUI), str(path / "test.c"), str(GUI / "ui_card_slots.c"),
                   "-o", str(path / "test")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
            if sys.platform.startswith("linux"):
                command += ["-fno-pie", "-no-pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
