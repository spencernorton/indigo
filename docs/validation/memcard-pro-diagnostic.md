# MemCard PRO GC diagnostic test

This test branch investigates the new report on
[issue #102](https://github.com/spencernorton/indigo/issues/102#issuecomment-6409292360).
It is based on v2.5.0, adds tracing and fixes two tested failure paths. The
reporter's physical-card failure still needs confirmation.

The trace records the requested game ID, GameID-disable setting, storage
locations, Sync Clock state, SDK/build revision, probe/identity/command
results and elapsed times, presence changes, raw mount results and save-scan
counts/wait/failure flags. It contains no save contents. Its timestamps use
elapsed time; clock corrections do not change them.

Card-switch timers and the pending-GameID launch wait now use steady time.
Initial identification also retries BUSY lock results within the existing
600 ms insertion budget. The SDK returns BUSY before any identity command
was sent. Definitive wrong-device/no-card answers are not retried. The later
card-loading backoff and Game Detail visibility rules are unchanged.

## Collecting a trace

1. Keep the installed release's boot files so it can be restored. Install
   this test build the same way as that release, including its matching
   `swiss/patches/apploader.img`. Keep the existing settings and saves.
2. Start Indigo again. Open a game whose virtual card already exists and
   differs from the card currently displayed on the MemCard PRO. Wait on
   Game Detail for about 15 seconds. Record whether the card's own display
   changes, whether the Saves row appears/disappears and approximately when.
3. Press B to return to the Library. This writes
   `swiss/settings/card-trace.txt` to the settings device. Copy that file
   before another restart. If no file appears, note the settings device:
   export deliberately skips SD/MMCE devices in either memory-card slot.
   FlippyDrive, GC Loader and SD2SP2 settings devices are supported.
4. Separately, repeat with a game with no virtual card, then try quick
   Details changes between games. Return to the Library and retain each
   trace before restarting. Do not erase any existing saves to make a test.
5. Include the exact release that failed, whether stock v2.4.0 still works
   with the same settings, the card firmware version, slot and loader, and
   the observed card-display result with the trace. Also say whether Sync
   Clock is enabled; its value is included in the log too.

## Reading the evidence

- A request marked `skipped: GameID disabled` made no probe or ID request
  on that slot. A storage-slot bypass is recorded as `not followed`.
- `probe` and `device` record the SDK's numeric results. In this SDK,
  0 is ready, -1 busy, -2 wrong device, -3 no card and -4 an older protocol.
  The build accepts -4 from the identity command. `busy-retries` counts lock
  failures before any identity command was sent; probe and identity share
  one 600 ms retry budget. Other failed/expired identification can still be
  classified as no emulator and is logged for investigation.
- `set ID=0` means the transfer completed. It does not prove the hardware
  selected the requested virtual card; compare its own display and subsequent
  presence/read events.
- `save scan slots` is a bit mask: 1 includes Slot A, 2 Slot B, 3 both and
  0 neither. The Saves row hides after a settled scan with fewer than two
  copies in both v2.4.0 and v2.5.0. That rule is unchanged here.
- A `[trace full; restart Indigo before another test]` line means only the
  first events fit. Keep that file, then restart before collecting another
  short case. Copy each trace before restarting; the next export replaces it.

The model runs are emulator evidence only. The diagnostic build and returned
trace must not be recorded as a real-card pass until someone actually tests
the three switching cases on the card and the results are reviewed.
