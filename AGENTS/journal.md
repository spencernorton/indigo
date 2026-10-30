## 2026-10-30 — Reduce cold-code size with isolated LTO

The compiler/linker matrix finds no saving from relaxation, multiple-word
loads/stores or tighter hot-code alignment; disabling called-once inlining
grows the DOL. Size-mode LTO saves about 38 kB. Keep the three hot renderers
and patched audio objects outside LTO so their instructions and relocations
match the previous -O2 objects. A single LTO partition gives about 37 kB net
saving and is checked across serial/parallel builds and distinct directories.

Correct the fallback banner's mismatched BNR/byte-array declaration with one
shared pixel-array header. Remove the unused BNR1 header/text and retain all
6,144 RGB5A3 pixel bytes. The host regression pins the original pixel hash;
a mutant that copies the old header instead of pixels must fail. LTO type
mismatches now fail the target link. Music, encoding models, dependencies,
exception metadata and uncompressed boot retain their contracts. Exact normal
and performance artifacts, host/emulator routes and matched native timings
remain the acceptance gates for this compiler policy.

## 2026-10-30 — Preserve hot-renderer timings in the size build

Retain -O2 for the background, Jewel Case and font emitters as well as the
audio mixer. The full -Os prototype holds display cadence but costs about
5–7% more rendering work; the selective policy restores the original sampled
tails and mean timings while keeping most of the compiled-size reduction.
Matched native samples give Home 6.131→6.162 ms, Jewel Case rest
4.058→4.058 ms and browsing 6.204→6.197 ms, with zero 25 ms cadence
exceedances. Preserve exact packaged-artifact and image qualification records;
the compiler policy removes no supported feature or dependency metadata.

## 2026-10-30 — Reduce the executable's compiled footprint

Compile application C code with -Os, retaining -O2 for the patched audio mixer
and decoder interface. SDK and portlib archives, source behavior, embedded music,
character encodings, dependency unwind metadata and uncompressed boot format
remain unchanged. The whole-application prototype saves about 305 kB; retain
source-bound build, emulator, native performance and image receipts for the
final audio-preserving policy before acceptance. Both normal and performance
artifacts now share a 4.25 MiB structure/stamp/size gate. More aggressive -Oz
produced identical bytes in the pinned target compiler, so use standard -Os.

## 2026-10-30 — Show Home, Library and game details in compact GIFs

The README explains the product with a real interaction recording or a source-grounded
architecture diagram. Recordings retain their source attribution and offer still
images for reduced-motion viewing. No runtime behaviour is changed.

## 2026-10-30 — Preserve mutation-test failure propagation

Keep the long-text equivalence check's assertions visible to the mutation
runner rather than recording nested unittest subtests. Both deliberate
regressions (formatting under the lock and clipping caller input) now fail
the native publisher harness as intended; the mutation suite passes.

## 2026-10-30 — Prepare Library text before publication

Stage each window's title/publisher/facts scales and Spotlight description
from the immutable input snapshot before taking the video mutex. Publication
copies the prepared scales and applies staged ellipsis prefixes alongside the same validated records and banner
textures. Staging uses 1,268 bytes on the menu stack, guarded by a 1,536-byte
compile-time ceiling, and adds no heap allocation. Event layout, columns,
generation and disposal still gate the final locked commit.

The GX harness now invokes the actual DrawUpdateGameflow publisher on every
layout update and rejects font measurements while the video lock is held.
Regression cases retain the complete event on stale, layout-mismatched and
disposed updates; a mutant that moves preparation inside the lock must fail.
Existing frame/typography streams remain the visual reference; guide images
retain their layout and appearance. Snapshot copying/cache flushes still run
under the lock; metadata I/O and true native timings remain separate work.

## 2026-10-30 — Publish immutable textures before video starts

Invalidate font and built-in panel textures at their DrawInit publication,
before the video thread starts. This removes per-draw pending state and keeps
the Save Details emitter independent of texture publication. Repeated-draw
and reinitialization checks enforce one invalidation per publication, and
check that initialization precedes the video thread's creation.

## 2026-10-30 — Reuse menu geometry and bound artwork work

Cache the eight authored unit-circle sizes in a bounded 1,328-byte pool,
reusing the original float calculations across frames and icon/shadow passes.
Home Library's counted math calls drop from 1,061 to 653; Source drops from
555 to 351. All 33 sampled frame hashes, vertices and EFB copy areas match.
Font and box textures invalidate once per initialization, with repeated-draw
and reinitialization tests. Jewel Case invalidates once before sampling its
published prints and immutable studio textures, down from 5 at rest and 11
in a two-case transition. The frame budget now enforces invalidation counts;
EFB-copy synchronization remains in the background renderer.

Pack artwork shares a single 16 KiB read per menu tick, fairly scheduled
among visible packs. Hidden partial records pause and resume with their CRC;
new tests cover the global budget, fairness, layout changes and final-range
publication. The separate folder-art worker and Detail's one-poster priming
keep their existing contracts. Compile only settings.o with -Os: the pinned
SDK DOL shrinks 7,744 bytes against the same changes with -O2 everywhere.
Renderers, loaders, SDK/library pins and the boot format retain their flags.
Physical timing remains unmeasured while the console is unreachable.

## 2026-10-30 — Continuous Jewel Case motion and immediate Off

Wrap the Jewel Case's sway clock at the common period of its authored
frequencies rather than at an unrelated hour. UI Motion Off freezes the
disc and snaps manual inspection to the stick or neutral without inertia;
the Z spin toggle keeps its intentional easing under enabled motion.
Native-function regression checks in the frame harness exercise the phase
boundary, immediate Off, manual control and Z easing at 50 and 60 Hz. The
clock regression was observed failing before the fix. Existing Full-motion
frame geometry and workload budgets remain the visual reference.

## 2026-10-29 — Qualify the exact packaged native browser

Pin c98a0bfb after exact package checking exposed locale-sensitive TLS object
order. The producer now sorts source inputs and compares the SDK default
locale/serial build with Indigo's C.UTF-8/parallel build at a second path.
Producer41002873917 passes all required routes. Its canonical DOL matches
the Indigo package byte-for-byte. Six public HTTPS boots prove save80202,
restart, change00501, restart, clear and restart empty on an actual emulated
FAT card; live weather and cleared state were inspected. The public CA is
unchanged. This replaces earlier binary qualification hashes, without changing
the browser's native integration behavior or claiming physical return.

## 2026-10-29 — Inherit the native browser context

The Web face passes versioned, named preferences to the separately built
browser: menu colour, screen/video shape, motion, sound, rumble, known UTC
offset and supported config-card origin. Keep mount/close ownership and the
Recent-list exclusion. Indigo stores local wall time; capture Sync Clock's
applied offset when available, otherwise resolve an explicit POSIX zone, and
omit an unknown offset. The browser normalizes a known local RTC for UTC/TLS,
keeps an unknown integrated display on the console clock and preserves its
standalone preferences. Browser context tests cover named colour mappings,
accessibility, storage/lab flags and known/unknown time zones.

Pin browser f3a7ca2e, the source-qualified native implementation and its
start-page ZIP persistence (producer40999788117). The browser's required routes include NTSC
4:3, PAL widescreen on port 4 and a three-boot TLS/FAT persistence proof. The
native public HTTPS start page rendered with the verified certificate mark;
its downloaded CI DOL matched the locally tested bytes. The guide's new
browser image is an actual Dolphin capture. Existing physical return evidence
belongs to the previous browser; the console is currently unreachable, so
adapter/cable and installed return qualification remain pending. Stable main
is not promoted by this beta development change.

## 2026-10-26 — Always show saves on Game Details

The reporter on #102 confirms stock 2.5.0 switches MemCard PRO cards and
reads their saves correctly; the disappearing row was the two-copy
visibility threshold. Add Always beside On (still the default) and Off
under Setup › Library › Saves on Details. It shows a single save's blocks,
date and game-written line, or a completed empty scan. An incomplete empty
scan says so, and card loading/failure messages retain priority. The new
Always Show Saves on Details key defaults to No; the old Hide key still
disables scanning and takes precedence. No card protocol, scan timing or
copy-selection behavior changes. Host coverage exercises zero/one/multiple
copies, pending/failed/incomplete scans, both cycle directions and settings
round trips. This is for a future release through beta.

## 2026-10-23 — Align agent instructions with the coverage policy

Remove the obsolete real-card publication hold from AGENTS.md as well as
the already-updated release procedure and workflow. Real MemCard PRO GC
access is not a release requirement. Pending coverage and strict claimed-pass
auditing remain unchanged; release approval is not hardware evidence. This
prevents a later session from restoring the superseded expectation.

## 2026-10-23 — Report unavailable card coverage without blocking release

The maintainer explicitly removes real MemCard PRO GC access as a release
requirement: that hardware is not available. The release workflow now emits
an informational coverage report and continues. The record stays pending,
bound to its historical source/SDK, and the default qualification audit
remains strict. Candidate and stable reports both expose missing coverage;
CI and GCMCE runs never become a real-card pass. The v2.5 notes disclose the
current SDK coverage gap. This supersedes the earlier publication hold in
the release-preparation entry below.

## 2026-10-23 — Final candidate and release preparation

The maintainer approves release after the combined candidate's console
validation. Pin the merged browser return-button change in both build
workflows, name the v2.5.0 changelog section and clarify disc-pack copying
on the games card. The browser's one-press return and actual disc labels
passed console checks. Indigo runtime source is unchanged from38367c3;
the card-slot/SDK qualification remains pending and is not inferred from
these results. Run the full native CI and packaged-build verification
before promotion. Stable publication still requires its recorded hardware
qualification; no release gate is removed here.

## 2026-10-22 — Wait for System before the File Browser route continues

The combined candidate's two-card route accepted a transition frame as
"File Browser closed", then sent UP and A before System was ready. Its
later save-comment assertion failed on a game's Details; the screenshots
show the intervening commands had reopened the File Browser and picked the
disc. Capture System's row text before entry and require it stable on B,
as the existing file_browser route does. No added sleep, retry or relaxed
save assertion, and no console code changes. The next pinned test build
carries this stronger oracle with the combined features.

## 2026-10-22 — Combined validation candidate

Combine the current beta with the manuals test branch (0bcfc74), the Jewel
Case/disc-art/Z-toggle test branch (4935a7b), the Deluxe widescreen default
(02d01b4), and the Swiss intake through abe21df5. Retain all current beta
clock, upload, browser and lab hardening. Manual cancellation joins the
poster/stills/discs lifecycle, and all fuzz targets and host tests remain.
The pinned test branch and CI package are for console validation before
feature acceptance. MemCard PRO qualification remains pending; no stable
promotion or card install is represented by assembling this candidate.

## 2026-10-22 — Swiss intake through abe21df5 on the pinned SDK

Account for the five upstream commits after 39a0867e. Keep Indigo's README
and removed legacy file/settings screens. Import application SDK metadata
without replacing a supplied banner description, clear TZ for programs that
do not accept an environment, and use the monotonic clock for FTP timeouts
and copy/verification progress. The SDK remains libogc2 r2482: its libraries
export __SYS_SetTime but not __SYS_InitTime, so the newer-SDK shutdown call
from 99b59e7a stays deferred and is listed in UPSTREAM. No physical-card
qualification is inferred; its pending record remains unchanged.

- 345e4d5e: upstream device-list README; Indigo's own README retained.
- b3cd55b9: SDK metadata imported; Indigo's own screens retained.
- 5341410b: TZ handoff imported.
- 99b59e7a: deferred because it requires a newer SDK ABI.
- abe21df5: monotonic FTP/copy/verification intervals imported; Indigo's
  renderer already uses its own steady clock.

## 2026-10-19 — The Jewel Case on beta

The Jewel Case, its disc art and Z moved from v2.4.0 onto beta (CI's
runners only carry the libogc2 r2482 toolchain now). Beta reads packs in
16 KiB steps and opens stills only on first Spotlight use, so `discs.pak`
follows suit: `_GameflowOpenDiscsPack` opens it on first Jewel Case use,
once per mounted device (`discsPackAttempted`, reset with the others in
`DrawGameflowCancelPosters`), and `DrawGameflowPollPosters` steps it with
`UIDiscs_PollStep` after the posters and stills. test_stills_lifecycle.py
covers discs too: only the Jewel Case opens them, once per device.

## 2026-10-18 — Z stops the Jewel Case's disc

A console tester on #139 (component, 3fedaab) found the Library smooth with
discs streaming and asked for a way to stop the spin. In the Jewel Case, Z
now flips `swissSettings.stillDiscs` (settings key `Spin Discs`, default
`Yes`) and saves the settings file at once, as the autoload's Z does: the
metadata thread is stopped and card art paused around
`config_update_spin_discs(true)` (only that key, through the autoload's
merge), then the metadata thread starts again and the
loop's `folderArtShow` resumes card art. Z there no longer reaches
`filesManage`; the other layouts keep it. `UIJewelCase_Draw` takes `spin`,
and the disc's spin rate eases to 0 through the same filter that eases it
up. The Library hint line is unchanged (one more item overflows the longest
one); the guide's Library and Controls pages, SETTINGS.md and the changelog
say it. The tester also saw NHL Hitz 2003 (US) with NHL 2K3's disc: that is
GameTDB's picture; the poster pack's builder now gives it the PAL disc.

## 2026-10-16 — Disc art for the Jewel Case

A third Library pack, `/swiss/ui/discs.pak`, gives the Jewel Case each
game's own disc: SWPK v1 with kind `discs`, one 160x160 CMPR level, 12,800
bytes a record. `poster_pack.py --discs` builds it from pictures at least
160x160, cropped square; what is clear around the disc and in its hole is
laid on grey (176, 178, 184) first, so CMPR blocks at the rim don't take
in black. ui_assets gains a third shape, `UIDiscs_*`, with three slots.
FrameBufferMagic opens the file with the posters and, while the layout is
Jewel Case, asks for the selected game's disc and its neighbours';
`_GameflowDiscTexture` hands the jewel case the disc with the whole texture
laid over it, fading in as it arrives. A game without one keeps the
poster's middle square, and one without either the bare disc. Older Indigo
never opens the file.

GameTDB's disc art (art.gametdb.com/wii/disc/<LANG>/<ID>.png) is 160x160
RGBA, the disc filling the square, its corners clear and a clear middle
26.5 px across the radius: 80 sampled, all alike. 1,321 of GameTDB's
1,708 GameCube IDs have one in their region's languages (Japan has 96 of
351), about 16.9 MB of pack; the ready-made poster pack is to carry it.

Emulator: card.py draws a disc for every demo game but three (`NO_DISC`;
Ember and Ivy shows its cover's middle instead) and builds discs.pak from
them, and the guide picture was recorded again with them. Dolphin shows the
demo discs, the cover fallback, and real GameTDB discs (swapped in locally,
never published) square on the disc, the case's hub over the clear middle.

Tests: test_poster_pack.py builds a discs pack (header, shape, the grey
under clear pixels, the minimum size); test_ui_assets.c loads discs one
texture at a time and refuses the other packs' shapes;
test_gameflow_gx_stream.py has the disc first, the poster's middle without
one, a bare disc while it loads, and a mutant that ignores discs.pak;
test_emulator.py checks `NO_DISC` against the demo games.

## 2026-10-16 — Jewel Case, a fifth Library layout

Library Layout gains **Jewel Case** (`UI_GAMEFLOW_LAYOUT_JEWEL_CASE`, saved
as `Library Layout=Jewel Case`): one clear CD jewel case at a time, the
selected game's cover printed on its spinning disc. It is a port of the
jewel case games view of portlandhodl's Swiss fork (`gui/jewelcase.c` in
github.com/portlandhodl/swiss-gc, GPL-2.0), itself a GX port of Ann
Nguyen's CD Case (bubbbly.com/cd-case, whose author asks for a mention or a
link back). NOTICE, README, the Library guide and System › Credits name
them; Credits' Spotlight label became LIBRARY DESIGNS, over both credits,
and the page's lines moved up a few pixels to make room.

`gui/ui_jewel_case.c` keeps the original's case (rods swept along paths and
clear sheets), the studio baked into a matcap and two cube-map tables, the
disc with its rainbow, the lid's sheen, the glints, the resting sway and
the keys a case leaves and arrives by. Left out: its dock of disc
thumbnails, the PNG disc art (`swiss/discs/<GAMEID>.png`), the white page
and shadows, the opening lid, the launch and the slideshow. The Library
drives it: `_GameflowDrawJewelCases` hands it the records within a place of
the middle (`relativeSlot + carouselTravel`), each with its poster, the
middle square of the 192x256 cover (s 0..0.75, t 0.125..0.875) laid over
the disc, and the frame's direction picks which keys a case follows. The
carousel spring runs at 8 instead of 15 for this layout so the swap can be
seen. No cards are drawn; the selected game's cover leaves the case for
Detail and the launch, as Spotlight's does. The C-stick turns the case
(`padInstrument`), and the cube waits off screen as for Grid and Spotlight.
With no memory for its case (`UIJewelCase_Ready`), the layout draws as the
carousel. `_DrawGameflow` no longer opens its card batches with no card.

Cost: the original made about 2,000 sqrtf a frame at rest; per-frame maths
now goes through `invLength`, an estimate with two Newton steps. The
studio and meshes (about 670 KB, kept) are built once on the menu thread
when a jewel case Library is first made, and the rods' display lists are
recorded on the video thread on the first frame. frame_budget.c runs the
real renderer: 0 sqrtf, 203 calls of trig and the like, 23,346 vertices at
rest with a printed disc; 1,116 trig with a bare disc (its rainbow);
45,668 vertices mid-swap. Dolphin shows the swap, the C-stick turn and
spring-back, and Detail; frame time on the console is unmeasured.

Tests: test_gameflow_gx_stream.py logs what the renderer is given (one case
at rest, two while moving, which way, the print and its square, the bare
disc, Detail's fade, the carousel fallback) with three mutants;
test_ui_scene.c parks the cube; frame_budget.json has the jewel case
scenes; the folders emulator route walks it as a fifth layout.

## 2026-10-16 — Game manuals: a Manual row and a page viewer

A game's manual, converted once on a computer, reads on the console from a
Manual row at the top of its Game Details (requested in the issue tracker;
the maintainer chose full width by default, the whole page as an overview).
The console can't decode the PDFs people have: scans are JPEG 2000 or
JBIG2 inside, a decoded page won't fit beside the menus, and a PDF library
would not fit the DOL's headroom.

- `buildtools/ui/manual_pack.py` (Pillow, NumPy, and pypdfium2 for a PDF)
  renders each page at 1920 wide, trims the paper's margins and a scanner's
  dark edge (the edge peel runs on the page's own edges only: inside, a band
  of content is a heading, which the prototype peeled), splits spreads,
  scales to 640 wide (a page taller than 1152 rows is scaled to that height)
  and encodes GX CMPR (or I4 with `--gray`) itself, with NumPy, so no
  devkitPro is needed. Format `SWMN` v1 in docs/MANUALS.md: the packs'
  family, one fixed canvas a file, a record a page (the page, then its
  half for the whole-page view), CRCs over the header and index and over
  each page.
- `gui/ui_manual.c` is pure: the header and index checks, a page's CRC, the
  two page slots (the page shown, then the next one the way the pages turn;
  a slot rests two frames after it stops showing before a read reuses it),
  page placement and the memory guard. Host test `test_ui_manual` (every bit
  of a header and index flipped is refused), `test_manual_pack.py` runs
  packs the converter writes through it, fuzz target `manual`.
- `gui/manual.c` finds `swiss/ui/manuals/<ID>.pak` (then the first four
  characters', as a universal pack record) on the browsed device, reads
  pages on a thread for a thread-safe device (on the menu thread between
  frames for the disc drive) and runs the viewer; FrameBufferMagic draws it
  (EV_MANUAL, a covering page), texels as they are: one texel a pixel with
  GX_NEAR at full width on a 640 x 480 frame, linear otherwise, in two
  strips past 1024 rows. Game Details has four layouts now (Saves, Manual,
  both, neither); the row table holds top/bottom pairs, since the colour
  audit reads a brace of three small numbers as a colour.
- libogc2's zlib is zlib-ng: `crc32` over more than 256 KiB takes the
  Chorba path with a 64 KiB table on the stack, and on the 32 KiB reader
  thread the emulator's stack guard stopped it (DSI). A page is summed in
  16 KiB pieces.

The emulator's manual route (GC Loader) opens a synthetic manual made from
the guide's own pictures of the demonstration data (Cobalt Harrier), turns,
scrolls, toggles and closes it, refuses a damaged one (Ember and Ivy) and
checks the viewer gave back every byte it took. Dolphin's texture cache
sees a texture rewritten in place by 128 evenly spaced samples, which in a
640-wide CMPR page all fall in its leftmost tile column, the paper margin:
a slot refilled right after it showed kept its old page on screen. The
route hashes textures whole (`SafeTextureCacheColorSamples = 0`); a console
reads them anew after `GX_InvalidateTexAll`.

Defaults a console and TV pass may change, each a constant: CMPR over I4
and trimmed margins (manual_pack.py), and with Menu Widescreen a page keeps
its 4:3 width (`UI_MANUAL_FILL_WIDESCREEN`).

## 2026-10-21 — The browser's lab ships off; cut-short DOLs; FlippyDrive

Three follow-ups from the 2.5 review:

- `boot_dol` refuses a DOL or ELF whose header names sections past the end
  of the file (#158, into beta): `boot_image_valid` keeps `valid_dol_xxh3`
  and adds wiiload's `wiiload_image_valid`, now declared in `swiss.h`.
  `SDLOADER.BIN` and branch BINs stay raw; an ELF named `SDLOADER.BIN` is
  checked as an ELF, as `boot_dol` starts it. `test_boot_image.py` runs the
  real check on every cut-short size.
- The fork's console lab ships, off unless the browser starts with `lab=1`
  (fork branch `feat/console-lab`): Indigo's parser copied unchanged with
  its test; `/open` and `/point` look for `X-Lab` only among the headers (the
  test build's `strstr` also matched a body); an idle deadline (900 s, from
  a periodic alarm, kept in 32-bit milliseconds so the interrupt never
  reads half a write) and a crash reload after 10 s; `/reset` from the main
  loop, or from the lab's thread after 3 s; `sd=0` keeps the card off. Run
  from RAM on a console: header checks, `/open`, `/point`, a virtual
  press, the deadline and `/reset` back to Indigo.
- This branch: `launchWebBrowser` passes `lab=1` while `LabServer` is on
  (`audit_four_face_home.py` pins it), and the zip README, install guide,
  `docs/UPLOADS.md` and `docs/dev/web-browser.md` say the browser can't read
  a FlippyDrive's card (libogc2 has no FlippyDrive device).

The fork's pull request (spencernorton/cubebrowser#2) merged as `412d592`,
the tree tested, and `CUBEBROWSER_REF` pins it in both workflows. Left
open: the Web face hand-off is unchecked on a console. The card holds the
released browser, which has no lab, so a launch there ends remote control.
## 2026-10-21 — Lab and Sync Clock: reset vs uploads, locked frame copy, LogHost thread, steady timers

Closes the lab and Sync Clock items the 2.5 review left open (entry below).

`POST /reset` ran an upload's hand-off (suspend the main thread, stop the
audio, `DrawShutdown`, `SYS_ResetSystem`) without wiiload's `upload_mutex`,
so a reset during an upload could run it twice. `lab_restart` takes
`wiiload_hold()` first (declared in `gui/lab.h`, so `wiiload.h` stays
upstream's): an upload under way finishes, a good one restarting the console
itself, and none starts after. `GET /xfb` copied
`VIDEO_GetCurrentFramebuffer()` unlocked while `setVideoMode` frees and
reallocates the framebuffers under `_videomutex`; it now sizes and copies
the frame inside `DrawWithVideoLocked`. The video thread, at the highest
priority, draws first after the retrace, so the copy takes the rest of the
frame.

LogHost did a synchronous `net_send` under a mutex for every `print_debug`,
on whatever thread logged. A sender thread now drains the log ring with
`IndigoLog_Read` (a position; only appends that have finished, counted in
`committed`; the same gap note as `IndigoLog_Copy`), woken by a semaphore
`lab_log` posts. A post is safe in an interrupt (libogc2's IRQ entry raises
the dispatch-disable level), so lines logged there reach LogHost too.

`clock_settime` goes through `__SYS_SetTime`, which writes the time base
and moves the boot-time bias at 0x800030d8 by as much: `gettime()` steps,
`SYS_GetSystemTime()` (CLOCK_MONOTONIC, and libogc2's own sleeps and
alarms) doesn't. Indigo's interval timers use it now: the log's stamps, the
virtual pad and its idle time, the poster cache's eviction quarantine,
Apps art, frame deltas, the perf clock and the settings-device retry. On
`gettime()` on purpose: the MMCE deadlines (`card_slots.c`,
`ui_card_slots.c`, `gameflowWaitForCardIds`, which the MemCard PRO
qualification fingerprints), the EXI probe times `load_app` hands a game
(0x800030C0/C4, read against the game's clock), upstream's copy progress
and FTP timeouts, and the SNTP round trip.

SNTP ran on the network thread `wait_network()` joins, so an unanswered
server held the first Settings open or game launch for 3 s.
`time_sync_network()` now copies its settings there and starts a detached
thread. Two places still wait for it: `init_network()` (FTP, SMB, FSP)
calls `time_sync_wait()`, since FTP times out on `gettime()` and fsplib on
`gettimeofday()`; and a game's launch calls `time_sync_close()` before
`gameflowWaitForCardIds`, after which Sync Clock doesn't step (a step
under way finishes first; the check is made with interrupts off).

## 2026-10-21 — 2.5 review on a console: Web face, Apps Video Mode, uploads

beta f8ad73b's CI build ran on a DOL-001 (WIZnet W5500, GC Loader and
SD2SP2, a card in slot A that switches by game ID), pushed over wiiload with
`LabServer=Yes` and driven through the lab: every side of a Classic cube, a
149-game Library, Game Details with a card switch, every Settings page, File
Browser, System Information, interrupted and stalled uploads (dropped at the
10 s idle timeout, with the menus answering underneath) and `/reset`. Nothing
failed on the console; a review of v2.4.0..f8ad73b found what this fixes:

- `boot_dol` looked for a DOL's `.iso`, `.cli` and `.dcp` through
  `devices[DEVICE_CUR]`, NULL when the Web face starts the browser with no
  Source; it falls back to the DOL's own device. It also put the bundled
  browser in the Recent list, whose top entry `main.c` opens at start with
  Recent List On; `bootDolSkipRecent` keeps it out.
- `boot_dol` switched to Apps Video Mode and wrote `sram60Hz` and
  `sramProgressive` before the loaders checked the image; a refused image
  restores both, as the game path's `fail:` does.
- The Library's A path called the stepped `DrawGameflowPollPosters()` where
  2.4 read a whole poster; it calls `DrawGameflowPrimePoster()`.
- wiiload's check treated an ELF named `SDLOADER.BIN` as a raw binary while
  the loader starts it as an ELF; the check follows the loader now.
- Sync Clock refuses a server time before 2000 and retries `__SYS_GetRTC`
  while EXI channel 0 is busy, since the counter bias needs it.

Left open: the zip carries no licence text for the browser's mbed TLS or
its CA bundle; a cut-short `cubebrowser.dol` still starts (`DOLtoARAM` never
bounds sections by the file); a FlippyDrive-only console has no card the
browser reads; the lab's `/reset` isn't serialised with an upload and `/xfb`
copies without the video lock; Sync Clock's step moves `gettime()`, which
the lab log, the virtual pad and MMCE switch deadlines use, and an
unanswered SNTP request holds `wait_network()` up to 3 s. Stable still waits
on `docs/validation/memcard-pro.json` (pending). The shipped browser has no
lab, so a Web face launch ends remote control until someone presses Reset.

## 2026-10-21 — Sync Clock hides without an adapter; kept zones are real

Sync Clock (Settings › Network, #153) sets the clock once the network is
up: `gui/time_sync.c` sends one SNTP request (three tries, a second each),
then sets `clock_settime`, the SRAM counter bias and `__SYS_SyncSram`, so the
IPL and games see the time and it survives a power-off. bba.c's network
thread calls it (one line, listed in UPSTREAM). libogc2's lwIP has no DNS
or SNTP and ignores `SO_RCVTIMEO`, so the server is an address and the wait
is `net_select`. `ui_time_sync.c` (packets, POSIX TZ, the kept offset) has
no console calls; it is host-tested and fuzzed.

Its three rows are now the last of `networkRows`, and Settings hides them
without a network adapter: `show_settings_view` reads `net_initialized ||
bba_exists(LOC_ANY)` once and calls `UISetLayout_HideTail`, and whatever
counted the page's rows uses `UISetLayout_RowCount`, so Save & Exit and
Discard & Exit follow the rows shown. `show_settings` still searches the
whole table; the view moves a hidden option to row 0. Sync Clock stays off.
With Time Zone empty, `UITimeSync_KeptOffset` snaps the clock's offset to
the nearest of 40 real ones instead of a quarter hour: a Denver console 8
minutes fast kept -5:45 and now keeps -6:00.

Changing Settings' action block moves `SHOW_ACTIONS_SHA256` in
`audit_settings_semantics.sh`; `audit_settings_hardware_followup.py`
anchors on the literal `if(option == UISetLayout_RowCount(view)) {` and
holds tooltip lines to 68 columns. The host suite in Docker, run from a
worktree, needs the main checkout's `.git` mounted at its own path, or
`test_font_batches.py` cannot `git show v2.4.0:`.

## 2026-10-21 — Web face mounts the config device; browser at cfe550b

On a console with games on an SD2SP2 and settings on a GC Loader, the Web
face said "The web browser isn't installed" although the GC Loader card had
`swiss/indigo/cubebrowser.dol`. `launchWebBrowser` listed
`devices[DEVICE_CONFIG]` without mounting it, and the config device stays
mounted only while something reads it unless it is the Source
(`config_unset_device`). It now calls `config_set_device()` first, lets the
device go when the DOL isn't there, and keeps it through `boot_dol`, which
reads the DOL from it (`boot_dol`'s own Recent list save mounts and lets go
again). If `boot_dol` comes back, the DOL is closed and the device let go.
The other readers of the config device already mount it (config.c's saves,
Memory Cards, Game Details' SAVES, the Save Folder chooser, the File
Browser's right pane) or, like play history, write only when it is the
Source. `audit_four_face_home.py` pins the order.

The browser is built from the fork's cfe550b: it finds the SD card in a GC
Loader too, preferring the card whose `gcbrowser/` holds `cacert.pem`, and
no longer draws a grey wedge over some pages. The workflows clone the fork
at its renamed address. Beta (Sync Clock, the console lab) is merged into
the face's branch, which now lands on beta.

## 2026-10-19 — Weekly CI runs on beta; housekeeping

The weekly scheduled CI ran main's workflow on runners built from beta, so it
failed its toolchain check (and AESND's) once beta moved to libogc2 r2482.
`.github/workflows/weekly.yml` now dispatches `ci.yml` on beta with
`long_fuzz=true`; ci.yml lost its schedule, and its concurrency group names
the event so the dispatch and a beta push never cancel each other. Schedules
fire from main, so main's old schedule keeps failing until this is released.
Main also cannot build on today's runners until then (hotfixes included).

Housekeeping: the retired norvitech-site runner pool and the `nodejs` it
needed are gone; remaining old-owner links, System › Info's address, the
zip README and release notes follow the renamed account (the MemCard PRO
evidence URL stays as recorded); one unreferenced guide picture removed.
`docs/guide/images/system-info.png` still shows the old address.

## 2026-10-18 — Swiss watch sends through the M365 mailer

Supersedes the SMTP plan in the entry below, before merge: the estate has
no SMTP account, but it has a Microsoft Graph mailer app whose token holds
only `Mail.Send`. `swiss_watch.py` now sends through Graph `sendMail` as
`SWISS_WATCH_FROM`; sender and recipient are both the accounts mailbox.
Repository secrets are set from Infisical (the mailer app's canonical
entries, plus a dedicated `API_KEY_SWISS_WATCH` copy of the generic OpenAI
key — rotate those two together). A labelled test message was delivered
through the script's own send path, and a real model dry-run over
163f8b1d..39a0867e rendered its email without sending.

## 2026-10-18 — Weekly Swiss watch (gated, email-only)

New `buildtools/ci/swiss_watch.py` + `.github/workflows/swiss-watch.yml`
(Tuesdays 14:17 UTC, self-hosted `indigo-build`; schedules run from `main`,
so it starts when this reaches a release). Gates: one `ls-remote` against
the UPSTREAM pin, a deterministic classifier reusing `check_upstream`'s
`OWN`/`parse`/`covers`, one OpenAI-compatible model call only on a real
diff, and plain-text email only when a commit is take/review. Unassessed
commits default to review; a model reply assessing nothing fails the run.
Verified against the real 163f8b1d..39a0867e range (classification matches
the 2026-10-17 manual intake) with a local fake model endpoint; a real
model call and SMTP delivery still need the workflow's secrets/vars set
(dry-run dispatch prints the would-be email first). Tests:
`buildtools/ci/test_swiss_watch.py`.

## 2026-10-17 — Verify upstream homebrew video and console identity

The existing non-default GC Loader smoke fixture now selects Apps Video Mode
480i while the menu uses Auto 480p. Its launch probe checks VI registers for
the selected app mode, with a host-tested oracle; removing the forced-mode
handoff fails that native test. All nine mode names round-trip independently
of the menu setting. The actual System model classifier is compiled under
GCC/Clang and sanitizers to check Gateway identity before the Panasonic Q
fallback and retain bounded text. The updated settings parser completed
480,033 coverage-guided executions without a failure.

## 2026-10-17 — Swiss intake through 39a0867e

UPSTREAM now names the current Swiss head, 39a0867e, with all thirteen
commits since 163f8b1d accounted for below. The patch engine and VI assembly
match that commit. Translation and No-Intro database updates are present;
Latin-1 title bytes remain ASCII C escapes. Apps Video Mode is reachable
in Display, has a choice picker, config round trips, help and documentation.
Nintendo Gateway identification uses the bounded System model helper.

The routing fixture follows the new upstream main.c. The settings fixture
keeps the pre-presentation toggle baseline and adds the new upstream Apps
arm, preserving its existing tests of Indigo’s explicit motion cycle.
Indigo’s CI, README, presentation and tooltip line wrapping remain its own.
The Settings tour picture predates the new Apps row, as the guide states.

- `42b0c707` - Fix regression from 53a6f10da2a8e5912cec318527bc49bf69a0e3d8.: already present; retained.
- `602a4160` - Update [T-En] Collection database.: imported.
- `4bf58ab5` - Update [T-En] Collection database.: imported.
- `4e8d2c24` - Update [T-En] Collection database. (it's all Giftpia): imported.
- `a047de40` - Update [T-En] Collection database.: imported.
- `cd3ddefc` - Fix 960/1152i in Need for Speed games.: imported.
- `13e5d069` - Insurance for force field rendering off.: imported.
- `9fd007a9` - Use libiconv-lite.: covered by the pinned libiconv-lite overlay and runner cache.
- `8a33d884` Update README.md: upstream service list; Indigo’s own README retained.
- `c6052c4c` - Update No-Intro database.: imported.
- `a6cde773` - Identify Nintendo Gateway GameCube.: model detection adapted to Indigo’s bounded System text.
- `ecf8fda6` - Add video mode option specific to libogc2 applications.: launch/config imported; Display row, picker and help adapted to Indigo.
- `39a0867e` - Reformat tooltips for readability.: format-only tooltip changes; Indigo help and layout retained.

## 2026-10-17 — Fetch historical CI blobs on demand

The four full-history CI checkouts now use blob:none. All commit/tag refs
remain available; current files arrive at checkout and tagged test sources
arrive on demand. A fresh filtered clone used 626,845 bytes before checkout
and about 47.4 MB after checkout, instead of preloading the guide's historical
images. Both real-font and archived tagged-renderer comparisons pass from
that clone. Workflow policy checks pass; native CI exercises the same setup
through the runner's GitHub-only proxy. Release checkouts are unchanged.

## 2026-10-17 — Preserve the first Detail poster

Native CI caught the first File Browser Detail showing its banner while the
new idle reader completed its poster across frames. Keep the original
one-poster prefetch before publishing Detail (a fixed 43 KiB); idle work
still advances by 16 KiB per pack and never drains neighbouring posters or
stills at entry. The lifecycle harness checks the separate full/step calls,
and the File Browser audit rejects a partial prefetch. Its two-frame native
poster assertion remains unchanged. Host lifecycle sanitizers, all 146
File Browser mutants and the unchanged frame budgets pass.

## 2026-10-17 — Read artwork between input checks

Poster and still caches expose a 16 KiB idle step, retaining an unpublished
record and incremental CRC until complete. Slot generations, quarantine,
window changes and cancellation still fence publication; the original
whole-record Poll API remains for callers that want it. New host cases
check each range, complete-payload publication, read/CRC failure, cancellation
and mixing step/full polls. Both cache variants pass 5,727 checks.

Non-thread-safe app/folder PNG reads also advance by at most 16 KiB per
idle call, closing their temporary file every time. A paused or scrolled-away
picture frees its bytes without marking it failed; thread-safe devices keep
their existing worker read. The real card-art harness checks the 2 MiB
boundary in 128 sequential ranges and pause/resume, with memory returned.
The complete local plain, sanitizer and contract lanes pass. The DOL builds
in the pinned toolchain; native CI and emulator routes remain the merge gate.

## 2026-10-17 — Batch font glyphs and retain hint normals

The IPL font emits runs of up to 64 glyphs on the same sheet, separately
for each weight pass. Its host stream is byte-identical to v2.4.0 across
sheets, alignments, scales, alpha, extended bytes, newlines and long text;
the sample workload uses 540 batches instead of 17,010. Hint outlines keep
exact normals in sixteen bounded geometry slots (no frame allocation or
approximate math). The geometry test compares the old formula and changed
inputs; warm calls make no square roots. The steady File Browser fixtures
now have zero square roots and 148/148/150 batches instead of
1,701/1,710/2,316, with identical vertex hashes. The budget's font stand-in
now models these bounded runs; the real font has its own stream comparison.
No controls or pictures change, and these counts are not console timings.

## 2026-10-17 — Open gameplay stills on first Spotlight use

The Library's poster opener no longer initializes the gameplay-stills cache.
Spotlight opens it on demand, with its own one-attempt flag, so it still
works after another layout or with a missing poster pack. Device changes and
cancellation retire both caches. The actual renderer lifecycle functions
run in a counted host harness covering those routes and failure, plain and
under ASan/UBSan. No pictures or controls change.

## 2026-10-17 — Reject incomplete uploads and hand off under the video lock

The wiiload decoder stops on EOF, requires Z_STREAM_END with exact input
and output counts, caps the image at 16 MiB, and gives inflate one guard byte
to consume a split checksum without accepting extra output. Arguments must
arrive whole and end in NUL. Before Sidestep, DOL/ELF tables, payloads and
the entrypoint's argv probe are bounded by the actual image; raw BIN routes
remain. TCP reads have a ten-second idle timeout and accept checks select's
return value with a fresh timeout.

Transfers serialize on their own mutex. The menu keeps running through the
receive and validation; only a complete usable image reaches suspension,
under DrawWithVideoLocked so the menu cannot be stranded owning that lock.
The actual decoder, image checks, TCP reader and handler run in a host
harness, including single-byte streams/checksums and a fake scheduler that
holds the menu's video lock at handoff. Plain and sanitizer runs pass. A
new coverage-guided target compares accepted streams with zlib's independent
uncompress2 and exercises executable bounds. Native CI remains the merge gate.

## 2026-10-17 — Keep the portlib overlay usable on native builds

The libiconv-lite helper accepts shasum when sha256sum is absent and the
system tar when bsdtar is absent. A native macOS extraction with shasum,
BSD tar and Zstd produced a libiconv.a byte-identical to the Docker overlay.
The build guide names the native tools; Docker supplies them already.

## 2026-10-17 — Pin libiconv-lite and libogc2 r2482

The SDK is f9ffb7e3 (libogc2 f0a7627, r2482). Its AESND_Reset cancellation
and callback fix replaces the older local wait patch; the copied sources
remain byte-identical and the 300-reset emulator stress test stays required.
The verified libiconv-lite 1.19-4 archive is extracted as an unprivileged
portlib overlay before make dev; CI caches it in the build image and does
not fetch packages from a job. The launch probe exercises each accepted
banner encoding with this exact PowerPC library. The SDK invalidates the
old MemCard PRO GC qualification, whose evidence remains historical; the
record is pending and stable publication remains blocked on a fresh test.

## 2026-10-16 — v2.4.0's guide pictures

Nine guide pictures were re-recorded from the v2.4.0 release zip, in CI's
emulator image with the release's own harness and demonstration data only.
The five the v2.4.0 entry named were stale, and so were four more:
`memory-cards.png` and `memory-cards-raw-icons.webp` (the same read-only
info bar as `memory-cards-raw.png`), `memory-cards-details-unknown.png`
(Source: Read-only card image) and `memory-cards-save-folder.png` (no
Library Path row, Save Folder 2 / 7 instead of 3 / 8).

- The six Memory Cards pictures come from the virtual-cards route as CI runs
  it (NTSC, component, a GC Loader card from the zip, both slots empty): the
  open image's info bar now shows the save's own line, its details say Card
  image, and with the image open on both sides Move and Copy are dimmed. The
  icon animation is 24 frames 0.3 s of console time apart, a lossless WebP
  at 300 ms each, as before. `memory-cards.png` is the same frame as
  `memory-cards-raw.png`, as before.
- `memory-cards-save-folder.png` is a new card's first screen, Storage, two
  rows down.
- The Game Details pictures have the Save Folder holding the RAW image, then
  its exported GCI, with an empty card in Slot A: "2 copies | 2 blocks" over
  the date and the save's own line, and after Right twice "Copy 2 of 2 |
  Save Folder", "Loads at launch", the date and the line.

Alt text follows the new pictures, and one sentence in memory-cards.md that
called card images read-only now says that a card image's color is kept in
Indigo's settings, never in the image (what the folder page says). Every
other picture was checked against the release tree's own emulator captures
and the settings rows and help text since it was recorded: none else
changed, apart from the File Browser pictures' lighter unfocused pane (the
2026-10-08 entry), which doesn't show at the guide's size.

## 2026-10-16 — Web browser bundled, fork published, CI wired

The Web face now ships a real browser. The hardened CubeBrowser fork is public
at github.com/spencercnorton/cubebrowser (used with permission from Arial-Bold;
credited in NOTICE). sd_package.sh bundles swiss/indigo/cubebrowser.dol and a
gcbrowser/ CA folder at the card root when browser/ is staged; check_package.py
allows them and the zip tripwire rose to 9 MiB. ci.yml and release.yml build the
browser from a pinned fork commit (CUBEBROWSER_REF) with the runner's libogc2
toolchain and stage it into browser/ before packaging.

Validated end to end on the CI image: Indigo build, then the browser built from
the published fork, then sd_package and check_package, produce a 7.33 MB zip
carrying the browser and its CA bundle, check green.

Not done: pass the theme to the browser via argv (it ignores argv today); a
console test of launching the browser and returning to Indigo; the full
public-repo ruleset suite on the fork (it has baseline settings only). Lands
after 2.4.

## 2026-10-16 — v2.4.0

Released from the console-tested build f160ec5: the Update 2.4 line, MemCard
PRO card slots handled exactly as the real card ran them, Library Path,
Memory Cards writing card images on both SD cards, and Game Details counting
saves by file. Main's guide clarifications released after v2.3.0 were merged
in first (beta already had the same wording), and the changelog's Unreleased
section became v2.4.0. The code is the tested build's; only the journal and
the changelog differ.

Known gap: five guide pictures still show earlier screens
(memory-cards-raw, memory-cards-raw-details, memory-cards-details-both-raw,
game-details-saves-after-copy, game-details-save-choice). They are
re-recorded in a follow-up.

## 2026-10-16 — Game Details counts saves by file

A console test asked why Game Details never shows a creation date and why
games showed "in multiple blocks". A GameCube directory entry has one
stamp (0x28, last modified); the date people remember is the game's own
second comment line, which the IPL shows under the block count and Memory
Cards' info bar already draws. And the SAVES scan grouped by the six-byte
game ID alone: Animal Crossing's five files read "5 save copies | 58
blocks", and one 11-block save in two places "22 blocks".

`UISaves_StatsAdd` now takes the scan's file table (`uiSavesStatsFiles_t`,
16 files, each its newest copy's entry): `UISaves_SameFile` (game, maker,
and the name compared as the card does) decides a file, `stats->files`
counts them, and blocks add each file's newest copy once; more files than
the table holds make the scan partial. The box says "copies" only for one
file in several places ("2 copies | 11 blocks"), "5 files | 58 blocks" for
different files, "2 files, 5 copies | 58 blocks" for both, and puts the
described copy's own line after the date. `statsArrange` keeps each file's
copies together after its sort. `Saves_CopyLine` (saves.c) reads a copy's
second comment line where the scan found it: a card's through `cardFind`
and `readSaveAt` (the driver's .gci header first, then the save up to the
comment's end), a .gci's head then 32 bytes, an image's through
`SavesRaw_ReadGci`. `Saves_CollectGameStats` reads every listed copy's
line after arranging them, while its cards (each slot's unmount moved out
of `statsSlot` to after the lines) and the settings device are still
mounted; the card driver's init is a no-op on a mounted card. In the
choice, copies of several files show "<file> k of m | where" (the file's
own copies counted), the question names the file, and "Slot A's own copy
goes to the Save Folder" needs Slot A to hold that same file. The summary
fits 246 px instead of 264 while « SAVES » is up.

Tests: `test_ui_saves_metadata.c` (files, newest copy's blocks, the table
filling), `test_saves_stats.py` (one file in three places is 2 blocks, a
second file of the game, grouping, each listed copy's line read with its
card and the settings device mounted), `test_saves_card_io.py`
(`Saves_CopyLine` on the test card image, a .gci, a card through the
driver, a copy gone or read from the wrong device), `test_gameflow_detail.c`
and `test_launch_gx_stream.py` (the wordings, the line, a several-files
label that clears « SAVES »), `test_save_choice.py` (the question by
file), audit pins with mutants in `audit_saves_safety.py` (106) and
`audit_game_detail_safety.py`; `detail_frame_budget.json` re-pinned for the
new text (6 shorter, 16 more for the line). Stale pictures, left for the
wording Spencer settles: `game-details-saves-after-copy.png` and
`game-details-save-choice.png`. Open for him: the box still needs two
copies, and how a choice across several files should step.

## 2026-10-16 — Game Details counts the games' card images

Memory Cards could reach the Source's card images (the entry below), but
Game Details' SAVES still counted only the settings device's Save Folder
and the card slots: with settings on a GC Loader and games on an SD2SP2,
the game's own emulated card (`swiss/saves/MemoryCard{A,B}.<region>.raw` on
the SD2SP2, written by `deviceHandler_FAT_setupFile`) was never counted or
offered.

Which device is the Source is one rule now, `Saves_SourceDevice` in
`saves_stats.c`: `devices[DEVICE_CUR]` when it isn't the settings device,
takes writes and is FatFs (`writeFile == deviceHandler_FAT_writeFile`).
Memory Cards' `show_saves` and Game Details' `Saves_CollectGameStats` both
use it, so they can't disagree. `statsFolder` takes the device, the folder
and whether it is the Save Folder; the Source is read at Swiss's fixed
`swiss/saves`, whatever the Save Folder is called. A folder the Source
can't list counts as none and never makes the scan partial (the FAT driver
returns -1 for a missing folder and a failed read alike, and most cards
have no `swiss/saves`); the Save Folder still does. Each `savesCopy_t`
keeps the `device` it was found on: `copyRead` reads FILE and IMAGE copies
from it instead of always the settings device, and `Saves_LoadCopy` still
keeps the card's own copy in the settings device's Save Folder. In the
choice a `.gci` on the Source goes by `DeviceDisplayName`, not "Save
Folder"; images keep their file names. Nothing mounts or lets go of the
Source. With Emulate Memory Card on (the usual reason for these images)
there is still no choice, only the totals.

Tests: `test_saves_stats.py` has a second FatFs device (`sdc:`) for the
Source's image and `.gci`, a renamed Save Folder, a missing `swiss/saves`,
and a Source that is the settings device, isn't FatFs, doesn't write, or
has no settings device beside it; `test_saves_card_io.py` now runs
`copyRead` and `Saves_LoadCopy` against two in-memory SD cards, loading the
public test card image's save (through saves_raw.c's own readers) and a
`.gci` from the Source onto Slot B, the card's own copy kept on the settings
device, a copy read only where it was found, nothing without a settings
device; `test_save_choice.py` checks the label and the question;
`audit_saves_safety.py` pins the rule and the scan in `saves_stats.c` and
the load's devices in `saves.c` (100 mutants). No picture changes.

## 2026-10-16 — Memory Cards reaches the games' card

With settings on a GC Loader and games on an SD2SP2, a game's card images
are on the SD2SP2: Swiss's Emulate Memory Card keeps
`swiss/saves/MemoryCard{A,B}.<region>.raw` on the device the game starts
from (`deviceHandler_FAT_setupFile`). Memory Cards' SD columns listed only
the Configuration Device, so those images were out of reach, writable or
not.

The SD columns now choose between the Configuration Device and the Source
when the Source is another FatFs device that takes writes: `writeFile ==
deviceHandler_FAT_writeFile` with `FEAT_WRITE`, the device test
`SavesRaw_Writable` makes before writing an image. Both are mounted while
the page is open (`config_set_device` mounts the settings device, and the
Source is mounted already), so the page mounts and lets go of nothing new.
The storage menu names them with `DeviceDisplayName` (four items; still
"SD card" while there is one), and `UISaves_StorageTab` takes the number of
SD devices. A column's device is its place's `device`: `folderSet` takes
one, `loadFolder` lists it, `folderOpenHome` opens a device the column
wasn't on at its Save Folder or its root and moves `folderHome` there, and
`saveTransfer`, `folderWrite`, `destinationFolder` and the folder chooser
use the other column's device. With two devices the header shows the
device-prefixed path. Game Details' own copy and Settings' Save Folder stay
on the settings device; only the card slots' guard, those two and the
page's start name `devices[DEVICE_CONFIG]`, which the audit counts.

Tests: `UISaves_StorageTab` for one and two devices; the controller harness
has a second FatFs device (`sdc:`) with the same handlers and asserts that
every handler call names a path on its own device (it declares no
`devices[]`, so code reaching for the settings device doesn't build), and
its scripted menu presses give up instead of looping when an item is
missing; 13 audit mutants; the files emulator route, now with empty slots,
makes the SD2SP2 the Source, picks it by name in Memory Cards, erases and
moves saves on `swiss/saves/MemoryCardA.USA.raw` there and reads both cards
back.

Known gap: Game Details' SAVES (`statsFolder` in `saves_stats.c`) still
counts only the settings device's Save Folder, so a game's emulated card on
the Source isn't among its copies.

## 2026-10-16 — Card images become writable in Memory Cards

A console test found Memory Cards pointless with Swiss's Emulate Memory
Card on: every save lived in a `.raw` card image, and images opened
read-only (Move and Erase dimmed, nothing copied in). Images now take
Move, Copy and Erase like a memory card, to and from Slot A, Slot B, an SD
folder or another image.

`ui_saves_raw.c` plans each change in the image's five system blocks before
anything is written (`UISavesRaw_PlanAdd`, `UISavesRaw_PlanErase`): the new
directory and block map go into the older of their two copies with the next
update count (both copies when the count would wrap), a new save's blocks
are the free ones after the map's last allocated block, and the planned
blocks must parse back with exactly the intended change or nothing is
written. The parser now accepts blocks the map holds that no save reaches,
which is what the GameCube's own write order leaves after an interruption.
`saves_raw.c` writes through FatFs opened as the file is (the device's
`writeFile` opens with `FA_CREATE_ALWAYS` and would empty the image): an
add writes the save's blocks, then the map, then the directory, an erase the
directory, then the map, each synced, then every block is read back through
a fresh read-only open; an add that doesn't read back the same is erased
again. Only FatFs devices are writable; a FlippyDrive's or KunaiGC's images
stay read-only.

Tests: planner cases in `test_ui_saves_raw.c` (allocation order and wrap,
refusals leave the buffer untouched, counter wrap, interrupted writes),
`test_saves_raw_writer.py` (a FatFs model: open modes, write order, a cut
at every write, read-back mismatch clean-up), the controller harness now
moves and erases through a planner-backed model, `audit_saves_safety.py`
pins the new order with mutants, the raw fuzzer runs both planners, and the
emulator route erases a save from the demonstration image, moves one out
and a `.gci` back in, then reads the FAT image with an independent reader.

## 2026-10-16 — Library Path: the Library finds /games itself

The Default Source prompts (Set as Default / This Session Only / Choose a
Different Path, on a Source change and on the Library's cross-device
fallback) never showed on a console test with a default already saved, and
the maintainer asked for a simpler rule: the Library defaults to the games
it finds on the storage attached, and one setting names another folder.

`homeApplyStartupSource` now mounts Settings › Setup › Storage › Library
Path's device when it is connected; otherwise (Auto, the default, or that
device missing) the boot device stays when it has the Library folder, and
`homeFindLibrary` mounts the first other storage that has one, in the
boot-device order, skipping the disc drive, the network adapter, WODE, USB
Gecko and a storage on the Source's own connector. Library from Home uses
the same search. Nothing asks and nothing is saved. The key `Default
Source` (in no release) is now `Library Path`; its chooser is Settings'
only, with Auto as its last row. Removed: `homePromptDefaultSource`,
`homeOfferDefaultSource`, the session-only root and
`config_update_default_source`.

Tests: `test_gameflow_folder_navigation.py` covers the silent switch, the
excluded devices and connector clash, a Library Path at startup and in the
Library, Auto at startup with and without /games on the boot device, a
Library Path whose card fails to mount or is missing, and the subfolder
entry; the settings audit and tests follow the rename.

## 2026-10-15 — MemCard PRO handled as the real-card test ran it

The #102 reporter tested `test/mcp-card-slots` 92b4ed8 (2.3.0 with the
card-slot code) on a MemCard PRO GC (NTSC-J, FlippyDrive, Slot A): an
existing card read in about 3 s, a new card in about 6 s, rapid switches
then a launch mid-switch loaded the right card. Its card-trace.txt, the
first real timings: the card leaves the bus at most 0.66 s after the ID, is
off it 0.57 s (existing card) to 2.17 s, and answers status 0x01 once
back. Every failed status read in the trace is libogc2's 300 ms window
after a presence edge (exi.c `__exi_probe`), not the card; 0xFF never
appeared.

At the maintainer's word, 2.4 now handles the card exactly as that build
did. `card_slots.c`, `card_slots.h`, `ui_card_slots.c`, `ui_card_slots.h`
and their two tests are the tested build's files, which reverts the
2026-10-12 status gate above (its premise, 30 s of 0xFF while a card is
made, came from the lab model and the real card never showed it) and the
#121 review fixes, which only touch paths the real run never took (failed
sends, a plain card in a slot an emulator was in, GameID turned off
mid-session). The launch around a switch was already the tested build's.
Game Detail only reads a memory card emulator: a slot its scan found
changing cards is remembered (`gameflowSaveEmulators`) and gets no save
choice, since the tested build never wrote to one; test_save_choice and the
Game Detail audit hold it. With GameID off for a slot, Indigo doesn't
switch that card and can't tell it from a plain one.

`check_memcard_qualification.py` now binds the card-slot files, the launch
around a switch in swiss.c (`gameflowWaitForCardIds`, load_app's
`initialize_card` pair) and the toolchain pin, not whole swiss.c, saves.c
and config.c, which no hardware test of an older base could match. 92b4ed8
and this tree have the same fingerprint (81e082a4…), recorded with the
reporter's evidence in `docs/validation/memcard-pro.json`. Keep the
`test/mcp-card-slots` branch: the stable release check reads its source.

## 2026-10-13 — Soak warm-boot VI reset and metadata pool fixes

Five-way 60-minute soak runs (`final936-e1/e2/m1/m2/m3`) and a code audit of the
2.4 candidate surfaced a warm-boot hang and four metadata path inefficiencies:

- Warm-booting `ipl.dol` via `boot_dol()` after running in 480p or 576p left the
  active progressive VI timing (`vireg[1]` format/non-interlace bits and
  `vireg[54]` progressive clock) in hardware registers across `DOLtoARAM()`. On
  the next startup (`vmode == NULL && !swissSettings.cubebootInvoked`),
  `libogc2`'s `VIDEO_Init()` observed stale progressive/PAL timing bits,
  preventing `VIDEO_Configure()` from arming the display interrupt and hanging
  in `VIDEO_WaitForFlush()` (`s006` in `final936-m1`). `getVideoMode()` and
  `updateVideoMode()` now clear `vireg[1]` and `vireg[54]` before `VIDEO_Init()`
  when `vmode == NULL && !swissSettings.cubebootInvoked`, and `setVideoMode()`
  skips `GX_AbortFrame()` before `GX_Init()`, while keeping `unsetVideoMode()`
  untouched so `probe.dol` handoff telemetry still observes the menu's VI mode.
- `filemeta.c` now frees evicted `file_meta` blocks back to `meta_mspace` via
  `mspace_free(meta_mspace, meta)`, breaks out of `meta_alloc()`'s eviction
  loop if an entire pass over `dirEntryCount` cannot evict any off-screen entry,
  caches resolved `.tgc` inner `dvddiskid` headers on `file->meta->diskId`, and
  fast-paths 7-bit ASCII banner descriptions in `iconvBannerDesc()`.

A launch while a MemCard PRO is switching stays as the real-card test build ran
it (2026-10-15).

## 2026-10-15 — Web browser cube face (Stage 1)

Added a ninth Home face, Web, that launches a bundled GameCube web browser
(cubebrowser.dol, a hardened fork of Arial-Bold/Cubebrowser) the way the Apps
and Emulators faces launch a program. Chosen shape: launch a separate DOL, not
compile the browser into ipl.dol — it keeps Indigo under the 6 MiB DOL tripwire
(the in-process version would add ~1.18 MB against ~0.86 MB of headroom) and
isolates a browser crash from the menu. Design note: docs/dev/web-browser.md.

Changes: UI_HOME_FACE_WEB (always available, a side face like File Browser),
its OPEN_WEB effect and a globe icon; the absent-face mask widened from 8 to 16
bits for the ninth face; homeDispatchEffect -> launchWebBrowser(), which finds
the DOL on the config device and hands it to boot_dol (which shuts the menu
down and jumps); the four Settings face-choice lists, their help and
docs/SETTINGS.md gain Web. Config still persists faces by name, so older files
load unchanged (the choice indices shift, the names do not).

Validation: the full Indigo DOL builds at 5,428,016 B (5.18 MB, under the
tripwire) on the indigo-ci/build image, and the whole host suite — plain,
sanitized and contracts, every audit and the home/gameflow/launch/stroke
GX-stream goldens — passes; the default cube renders identically. Pinned tests
updated: the motif-state size bound (640 -> 720), the stroke extraction list,
frame_budget's icons[], audit_four_face_home's enum/label/icon checks and
test_settings_file's face cycle.

Not done: packaging the browser DOL and its gcbrowser/ CA folder into the zip
(held pending the fork's license/publish path), passing the theme to the
browser through argv, and a console test of the launch and the return to
Indigo. Lands after 2.4 (this touches swiss.c, which resets the MemCard PRO
qualification 2.4 is finishing).

## 2026-10-12 — Emulated card mounts gated on known sane status

Modeled MemCard PRO traces revealed that during 30-second virtual card creation,
the memory card returns busy status byte 0xFF. An obsolete retry fallback in
`cube/swiss/source/gui/card_slots.c` previously stopped treating 0xFF as loading
after several deferred reads and initiated card mounting,
emitting premature EXI bus payload commands (`0x89`, `0x81`, `0x52`) while the
card was still creating the target image.

Card mounting for identified emulators (`slot->state.mmce`) is now strictly gated
on known sane status (`!slot->statusKnown || !slot->statusSane` holds back the mount).
Known sane status (including 0x00 and 0x41) permits a mount attempt; successful
mounting establishes READY. Persistent busy or unsupported status yields a bounded
failure at the existing 60-second FSM deadline (`UI_CARD_SLOT_FAILED`) without
blind mounting, and retries if the card is reinserted with sane
status. Host glue tests in `test_card_slots.py` verify 30-second creation
holds, sane status 0x00 and 0x41 mounting, failed status read recovery, and 60-second
bounded failure with reinsertion recovery. Physical MemCard PRO hardware
qualification remains pending (`docs/validation/memcard-pro.json` status pending).

## 2026-10-12 — Save replacement bound to card mount generation

A falsifiable host probe reproduced a vulnerability in Game Details' save
replacement (`cardReplace` in `cube/swiss/source/gui/saves.c`): when writing
or reading back a chosen save fails after a card swap to an empty replacement
card, the fallback restoration called `cardWrite` without binding the write
to the original observed mount generation. `cardWrite` treated the replacement
card's generation as fresh, wrote the original save onto the replacement
card, and claimed the original went back. Existing tests missed this by
populating replacement cards with a same-name survivor ('S'), which masked the
bug via filename collision.

`cardReplace` now captures the target card's mount generation at operation
start, validates it across the initial `saveRead`, deletion, the chosen save's
write, and any fallback restoration. If the mount generation changes at any
boundary, the replacement operation aborts without writing or deleting on the
substituted card, leaving the original copy safely in the Save Folder and
truthfully reporting that status. `cardWriteBound` enforces this generation
binding after implicit driver initialization and immediately before file
creation, while preserving unbound ordinary copies (`cardWrite`), collision
checks, and generation 0 handling.

Host tests in `test_saves_card_io.py` now include permanent empty-replacement
regression cases across first-write, readback, and pre-write remount boundaries,
alongside the same-card restoration controls and same-name survivor tests. The
structural audit (`audit_saves_safety.py`) audits `cardWriteBound` and adds
mutant coverage verifying that unbound restoration or unverified deletion is
rejected.

## 2026-10-12 — General console acceptance, doc reconciliation and the stable hold

Spencer tested the installed 2.4 final candidate (commit 80921f9, product head
8e4da9a) on a physical GameCube: "worked really well. nothing broke or has issues."
General maintainer physical-console acceptance is recorded. MemCard PRO GC switching
was not tested.

Automated candidate verification is complete: CI passed, twenty CI jobs run twice
(40 check instances), reproducible build confirmed, five 60-minute soak runs (seeds
501, 502, 601, 602, 603) passed with modeled card reads settled and zero early payload
reads, eleven trace regression cases, and three boot harness regressions passed. In
docs/validation/memcard-pro.json, status remains pending, tested_revision remains null,
and all five result checks remain false. In local coordination tracking state,
stable_release_allowed remains false and hardware_qualification_required remains true.
Stable release publication stays blocked until matching-source real-card evidence is
verified.

Starting from 54ea1b6, a documentation-only merge of beta PR #124 (commit da3f1f9)
reconciles docs/guide/source.md by pointing to the centralized File Browser controls
link (linking system.md#file-browser) rather than duplicating control bullets, while
retaining beta's clarified /games layout guidance. Native code under cube/swiss is
byte-identical across 80921f9 and the 54ea baseline, sharing qualification
fingerprint 0f09e5436b64c641fbb86b5260dba12fa7db971becde99027a7a15d7618e06e3.

## 2026-10-11 — Real-card release qualification and the 240p probe

The MemCard PRO changes remain in the test candidate, with their real-card
qualification pending. Stable publication now requires the recorded physical
card/firmware/setup, evidence and five switching/save checks, tied to both
the tested revision and the current switching source. Beta/RC testing is
allowed. Lab and ordinary-card console passes cannot clear that hold.

The final soak's seed501 reached its probe game, but reported a frozen screen:
the probe's fixed 16-line barcode rows extended beyond a 240-line framebuffer.
The captured picture showed the running probe with its report cropped, and
the probe ELF mapped the sampled PC to its normal idle thread. Its grid now
fits the actual framebuffer height and width, preserving the full480-line
layout. Guarded host draws cover 15 framebuffer sizes including 240 lines.
The failed run stays as evidence; it must be rerun with the corrected probe.

## 2026-10-11 — The combined 2.4 candidate for the final console check

The maintainer validated build 182, 5805ea2. The 2.4 branch now also includes
the fixes from the full review, the damaged-disc launch fix and #121's
memory-card switching work (8e4da9a). The card-slot change passed its twenty
CI checks and its final independent review before it was integrated.

For a MemCard PRO, Detail waits for the game's virtual card to settle before
reading its saves. The latest GameID waits through a switch, a failed send
is retried, and loading or failure stays visible even with fewer than two
copies. Choosing a save cannot write while the card is changing; a card
change during the launch question cancels that choice and refreshes the list.
The earlier save-write cleanup is bound to the card mount that received it.

Final validation is a CI build of the exact candidate and five hour-long
soaks: default and non-default ordinary-card runs, then MemCard PRO runs
with a normal switch, a temporarily invalid device ID and an immediate
detach. The resulting production zip still needs a console regression
check. Confirmation from a real MemCard PRO remains pending; the lab model
has exercised those switching behaviours.

## 2026-10-10 — Card slots: a request starts its own round of sends

Final review of #121: a request made during another game's round of failed
sends inherited its sendTries and answered, so a plain card put in after a
MemCard PRO answered A's send gave B "didn't load" for one round. A request
that sends at once (UICardSlot_Request true) now clears both. test_card_slots
adds the case (it failed on 1ad9aa9); test_ui_card_slots has 309 checks.

## 2026-10-10 — Card slots: the re-review of #121

The re-review confirmed the four fixes and found that one of them overreached:
the plain-card reset looked only at the fifth send. An emulator that answered
on tries 1-4 and was out of the slot at the fifth, or one that took the ID,
left at once and then answered Get Device ID with garbage while it made a new
card for over 11 s, became a plain card: never sent the ID again when put
back, or read mid-switch. The slot now remembers whether any send of the
round reached an emulator (answered, cleared with sendTries) and becomes plain
only when none did; otherwise it is given up on (FAILED) as before. A plain
card swapped in never answers, so that case stays fixed.

Two nits with it: Detail reads the cards again when a launch is refused (B,
or "still changing") after a card changed while one of its boxes was up
(gameflowSlotsChanged, now cleared at the top of gameflowLoadChosenSave),
and a slot that becomes a storage device is reset like a GameID-off one, so
nothing polls it any more. test_card_slots.py adds the three cases, each
failing on 4e9abad; test_ui_card_slots has 305 checks.

## 2026-10-10 — Card slots: fixes from the review of #121

An independent review of #121 (0421b74) found two majors and two minors, each
reproduced in a probe built from the glue test's stubs; the glue test reset
between scenarios, which hid the state they live in:

- A waiting ID and the slot's state got out of step. UICardSlot_Sent kept an
  older pending ID, and SendFailed left the slot READY or FAILED for the
  previous game while the new ID waited. A card that took B and left at once,
  with A reopened while it was away, had its absence credited to A; Request(A)
  matched slot->id and dropped B, and the slot ended READY for A on B's card.
  Sent now clears the pending ID (the latest went out), and a failed send waits
  in IDLE, so presence changes aren't the previous game's.
- mmce was never cleared: a PRO swapped for a plain card, or taken out, made
  every other game's Detail say "Memory card loading" for 11 s, then "didn't
  load", until a restart. Five sends with nothing answering as an emulator now
  make the slot plain (IDLE, read as it is). CardSlots_Poll reports a slot that
  stops waiting (busy -> not busy), since IDLE -> IDLE is no phase change.
- The launch question only looked at the slots once, after it closed.
  gameflowSaveAsk now polls them every frame (gameflowSlotsChanged), and a
  change during or right after the question cancels the load, says so and
  reads the cards again.
- GameID turned off for a slot left its followed state: the slot dropped out
  of other games' scans. It is reset to IDLE (a plain card) instead.
- CardSlots_Trace's 8 KB buffer had no reader in 2.4: print_debug only.
  Not changed (nits): reopening the same game keeps a READY card without a
  new switch (nothing polls the Library, so a card swapped there is seen when
  it next leaves the bus), and EXI_ProbeReset resets both slots (two
  emulators only).
- Tests: test_card_slots.py runs five scenarios one request after another
  (each failed on 0421b74), with an accounted() check that a slot is read,
  waited for or said to have failed; test_ui_card_slots (297 checks) and
  test_save_choice.py (a change during or right after the question) pin the
  rest, and audit_game_detail_safety the box's polling, with mutants.

## 2026-10-09 — Memory card emulators: follow the GameID switch (indigo#102)

The reporter's MemCard PRO test build (7bd3853: a full re-mount once a second
for 20 s) stalled the card, never read a new card, and twice left the GameID
unsent until the card was re-inserted. A lab Dolphin model of the card, whose
switch behaviour is a parameter, reproduced every report and found three
mechanisms. Detail mounted the card about 0.2 ms after Set Disc ID, so it read
the previous game's card. libogc2 answers the first probe after any presence
edge with BUSY for 300 ms (exi.c __exi_probe), shown as "Save scan
incomplete". libogc2 also caches the EXI ID until the next presence edge, so
one garbage ID read while the card switched made both MMCE_ProbeEx
(gameID_early_set) and CARD_ProbeEx (the Saves scan) say WRONGDEVICE until a
re-insertion. GameCube MMCE has no busy, done or current-card command; the
presence line and the EXI interrupt are the only signals.

- gui/ui_card_slots.c (pure, host-tested) is one slot through a switch: idle,
  sent, away, back, loading, ready, failed. A switch shows on the presence
  line within 2.5 s or none is coming; the first read is 1 s after the card
  returns; retries at 1, 2, then 4 s; given up 60 s after the ID. An ID asked
  for during a switch waits (the latest only). A presence edge after ready or
  failed starts over: a card given up on because the ID never went out is
  sent it again when put back, never read (it holds another game's card).
  A write needs a presence cycle, or 10 s, since the ID.
- gui/card_slots.c sends the ID with gameID_early_set's commands, the probe
  waited on 600 ms at most, and resets libogc2's probe (EXI_ProbeReset) when a
  card that took our ID answers as another device. Set Disc ID going out is
  what counts: the name (Set Disc Info) failing after it still leaves the
  card switching. libogc2 also calls Set Disc ID failed when the card leaves
  the bus the instant it takes it (the deselect probes it again). An emulator
  that didn't take the ID keeps it pending and is
  sent it again after 1, 2, 4 s, five sends in all, then the slot is FAILED
  (never IDLE, so never read or written as a plain card); a queued ID whose
  send fails stays queued the same way. Presence comes from
  the EXI status register (no bus traffic). While a card loads its status
  byte (0x83, three bytes) is read every 250 ms: 0xFF or an error bit skips
  the scheduled mount for three tries (0x00 counts as an answer: a card not
  yet unlocked may say just that), and turning sane reads at once.
  A read is CARD_ProbeEx, mount, unmount. Slots used as storage (an MMCE
  card's own SD, SD adapters) get the ID as before and are never read. Each
  event goes to print_debug and an 8 KB trace (CardSlots_Trace). Detail is
  told when a card is done (READY or FAILED) and when one that was done or
  given up on changes. Our own waits are bounded; libogc2's mount has its own, entered only
  after CARD_ProbeEx said READY.
- swiss.c: CardSlots_RequestGame replaces gameID_early_set; Detail's scan
  reads only CardSlots_ReadableFor slots (Saves_CollectGameStats takes a
  slot mask), polls on idle retraces and reads again when a card finishes;
  Left/Right wait; gameflowLoadChosenSave writes only when
  CardSlots_WritableFor, asked again after its question box (polling first:
  the card may have changed while it was up); a launch first sends an ID that
  waited (65 s at most, a box after 0.5 s). The Saves box says Memory card
  loading meanwhile, and Memory card didn't load for a card given up on,
  with any number of copies (2.4 shows the box from two copies otherwise:
  a new card on a MemCard PRO gave no sign it was loading); from two copies
  a card given up on makes the scan partial instead of dropping out of it.
- Lab, 2.4 plus this, under each card behaviour: the reporter's (a short
  detach, back while loading, a valid ID) sends 11 card commands during an
  existing card's 5 s load (7bd3853: 251) and shows its save about 0.1 s
  after it is ready; a garbage ID for 1.5-2 s after the card returns is never
  read; rapid opening sends 7 of 7 GameIDs (7bd3853: 1); detached for the
  whole switch, never detached, a blank new card and a plain card are right;
  a card that leaves the bus the instant it takes the ID is sent it again
  once back and shows its save about 6 s after the open (a75efd4, which
  counted that send as no emulator, read it as a plain card: "No save
  copies found" for good and an empty trace).
- Tests: test_ui_card_slots (thirteen scripted cards, 284 checks),
  test_card_slots.py (the real card_slots.c with stubbed MMCE/CARD/EXI calls,
  a fake clock and presence register: partial sends, failed and queued sends,
  a send given up on and the card put back, a card that leaves at once,
  refresh on leaving the bus, bounded probe, status-held mounts, stale ID;
  nine mutants of these all caught), test_gameflow_detail's loading and
  given-up boxes under two copies, test_saves_stats' slot mask,
  audit_game_detail_safety and audit_files_contract (the new wiring and its
  mutants). An independent review (2026-10-09) found the partial-send,
  dropped-queue and no-refresh defects in the first version; fixed here.

Not done: Memory Cards still mounts a card that is switching; the CI emulator
has no MemCard PRO yet (the lab model wants to become a 0008 patch, routes and
trace checks); the timings are the lab model's until a real card's trace.

Into 2.4 on 2026-10-10 at Spencer's word, without waiting for the reporter:
rebased onto update/2.4 after PR 119. Left and Right step through copies
while gameflowSaveChoosable(context) && !context->savesWaiting (folding the
wait into gameflowSaveChoosable would also hide « SAVES »); the launch
question keeps PR 119's wording, with the WritableFor gate before it and
again after it; the scan keeps PR 119's order, the Save Folder then only the
slots CardSlots_ReadableFor allows.

## 2026-10-10 — A damaged disc image can no longer crash a launch

An hour-long soak of 2.4 (seeded random presses in the emulator) started CI's
"Corrupt Table" disc, whose file table counts 0x0AAAAAAA entries, and Indigo
stopped on the exception screen: memcpy in parse_gcm, which took the count and
each name's offset from the table as they came, so the string table landed
0x80000004 bytes past it (DAR 0x00A48098). #14 had bounded get_fst_details for
the Library and left the launch path as upstream wrote it; upstream r2119 and
2.3.0 crash the same way.

Every walk of a file table now goes through two checks in gcm.c: fst_entries
(the count, or 0 when its entries don't fit in the table) and fst_name (a name
that starts and ends inside the string table, or NULL). get_fst_details,
parse_gcm, parse_tgc (which also used a table it couldn't read), adjust_tgc_fst
(it takes the table's size now, gcm.h and its two callers in swiss.c too) and
the DVD drive's read_fst and calc_fst_entries_size use them; read_fst also
stops at a folder that ends before it starts, which looped. A damaged table is
walked no further than it reaches, so the launch goes on with the apploader and
main DOL, as for any image.

Tests: the fst fuzzer runs fst_entries, fst_name, get_fst_details,
adjust_tgc_fst and calc_fst_entries_size; the game route launches Corrupt
Table on its way to the probe, which fails like any launch in the emulator (no
BS2) and comes back to the Library. The same step against update/2.4 at
ee8c596 ends on the exception screen. Broken Header, the header-only dump,
doesn't open its details at all (A does nothing, as before), so the route
leaves it alone.

The rest of the soak, on the 2.4 review's fixes: five seeds of an hour each
(two with non-default settings) ran without a fault; every console reset
followed a Restart, an app or a game launch. The harness had to learn the
probe's report, Restart's question and the IPv4 keypad, each of which its
first crash test took for the exception screen: a real one is white text on
black and nothing else.

## 2026-10-09 — Three data-safety fixes from an independent review

An independent review of PR 119 (87039d9) found three paths that delete or
overwrite data; each was reproduced against the code first.

- File Browser: the copy's existence check was Swiss's readFile(destFile,
  NULL, 0), so a file there that couldn't be opened for reading (a network
  share that lists it but won't open it) counted as absent. No Keep both,
  Replace or Cancel: FAT's write opens with FA_CREATE_ALWAYS and wrote over
  it, and a failed write's manageDropPartial deleted it. The check is now
  manageDestExists (statFile, else open), as Keep both's loop already was.
  The File Browser greys Copy and Move over a folder of that name, so a
  folder never reaches the question.
- File Browser: B was looked at only at the top of the copy loop, so a B
  latched during the last piece's read or write, or the file's close, was
  never seen and a Move deleted its original. It is looked at once more
  before the partial file and the original are decided.
- Memory Cards: cardWrite's clean-up (after a failed write, or a read-back
  that differs) found the save by game, maker and name and deleted it, on
  whatever card was in the slot by then. A card taken out, or changed by a
  MemCard PRO switching, is mounted again (card_removed_cb clears card_init,
  readDir remounts), so deviceHandler-CARD.c now counts mounts
  (card_mount_count) and cardWrite deletes only while the count is the one
  it wrote under; otherwise it keeps the file and says the card changed.
  A card that switches without leaving the bus can't be told apart.
- Tests: test_saves_card_io.py changes the card during a failed write and
  during the read-back (the other card's save stays; both unbound clean-ups
  fail it); audit_files_contract pins the existence check and the late B
  before the partial file and the Move, with a mutant each.

## 2026-10-09 — Fixes from the 2.4 review

Before 2.4's release candidate, six independent reviews read everything 2.4
ships (v2.3.0..update/2.4, the unreleased 2.3.1 commits included), each from
one angle: File Browser data safety, memory safety and threads, Home and
settings, Game Detail and saves, rendering, and the changelog and guide
against the code. Every finding was checked against the code before a fix;
three were blockers, all fixed here:

- A on a game in the File Browser while banners load disposed the loading
  wheel before meta_thread_stop(), and the banner thread's last
  DrawUpdateProgressLoading(loadingBox, -1) wrote into freed memory. The
  Library already stopped the thread first. audit_files_contract now holds
  every DrawDispose(loadingBox) in renderFileList to a meta_thread_stop()
  right before it.
- UIFiles_IsProgramFolder took any path deeper than the open folder for a
  folder populate_meta rewrote into its DOL. A memory card's save name is 32
  free bytes and the CARD handler pastes it under the folder, so a save named
  "ABC/DEF" was a program folder "ABC", and Delete ran deleteFileOrDir on it:
  CARD's readDir ignores the path and lists every save. The rule is now what
  the rewrite makes (one folder below, a .dol leaf), and filesKind never
  finds one on a card or the Qoob.
- Keep both looped on devices[DEVICE_DEST]->statFile, which FlippyDrive
  Flash doesn't have (a call through NULL, in 2.3.0 too). manageDestExists
  opens the file instead, as the existence check before it does.

The majors: the save choice (offered with no card or with Emulate Memory
Card on, then Launch refused until Detail was left: now gameflowSaveChoosable
gates the offer, the cycle runs through the totals, B goes back to them); the
copy list filled from the Save Folder first (now the cards first, the folder's
newest kept, newest first); Rename on a card (the driver's rename is a stub)
and Move off a card (no read-back, NOMOVE ignored), both now Memory Cards'
job; config_set_folder_color unmounting the device Memory Cards holds
(checkConfigDevice); and FatFs refusing to unlink or rename a file the left
pane still holds open from its banner read (filesCloseLeft before an
operation; every device's closeFile is a no-op on a handle that isn't open).
The minors are in the commit messages; three wait for after 2.4 (the
Library's place after the File Browser from Home, info bar names of 128+
bytes, card writes keeping permissions).

The scan keeps its order, the Save Folder and then the slots. Once the list
is full a card's copy takes the oldest folder copy's place, the rest moving
up so the list stays in scan order, and statsArrange puts the cards first.
The "Partial scan" on Detail in CI's virtual-cards route is the route's own:
its Save Folder holds two subfolders, which the scan never reads (5805ea2
shows it too), and both slots are empty, so 2.4 offers no copy there.

A second review read only these fixes and found four smaller misses, fixed
here too. The Library's carousel let the File Browser's page go without
clearing filesPage. A failed copy took a failed stat for "no file there"
(a dropped SMB link would leave a truncated file the message called
removed): only a read that fails before the first write now skips the
delete. Z on ".." in the right pane still needed File Management to turn
Autoload off. And two cards' copies could swap places on a full list.

A soak harness drove the build in the emulator with seeded random presses for
an hour at a time (lab only, not in CI). One seed crashed the same way on
every build: a DSI in dlfree under setVideoMode, after Settings had switched
the menu to 240p (TVNtsc240DsVf: EFB 480, XFB 240, GX_COPY_INTLC_EVEN). It is
Dolphin's: its XFB copy ignores the frame-to-field mode, so it writes 480
lines into each 240-line framebuffer, over the heap chunk after it. A write
watch on that chunk saw no store from the CPU, and with the copy halved as
libogc2 counts it the same seed ran on. Upstream Swiss has the same copy code;
nothing changes in Indigo, but the CI emulator needs that Dolphin fix before a
route can use a 240p menu mode.

## 2026-10-08 — Home: Idle Animation, Waves, On-screen Controls, Face Labels

Issue 116 asked for a livelier idle cube, a way to hide the waves, and a way
to hide Home's button hints and face names. Four settings on Setup > Console,
each off by default, so Home draws exactly what it did:

- Idle Animation (`Idle Animation=Calm|Sway`, a flip like Cube): Sway turns
  the cube from Home's pose (cubeYaw 0.28, ui_scene.c) to its mirror and back,
  yaw offset -CUBE_SWAY_RADIANS * (1 - cos(rate * t)) * homeIdleBlend, so
  every swing starts from the pose with no velocity and the outline is never
  wider than Home's. Its clock (swayClock, indigo_background.c) waits while
  homeIdleBlend falls and restarts below CUBE_SWAY_RESTART_BLEND (0.01): the
  blend's spring only snaps to 0 within 1e-4, and a turn's blend bottoms out
  at 0.0006-0.0023 as Home is already back at rest, so a restart at exactly 0
  never came and the swing resumed mid-arc at up to 1.4 rad/s. Not restarted
  on the blend's rising edge, which would drop up to 0.56 x blend of yaw at
  once. It steps once per seconds value (setupCubePipeline runs twice a
  frame) and caps a step at 0.1 s so a long frame holds the swing. Set per
  frame through IndigoBackground_SetIdleSway from _DrawBackground, as Wave
  Speed is. homeIdleBlend's target is UI_SCENE_HOME only (no longer the
  Source picker, which isHomeYawScene also covers), so neither Sway nor Calm
  moves the picker's cube.
- Waves (`Hide Waves`): IndigoBackground_SetWaves gates drawSilkWaves.
- On-screen Controls (`Hide On-screen Controls`) and Face Labels
  (`Hide Face Labels`): Home's renderer never reads swissSettings
  (audit_four_face_home's render scope), so they travel as
  uiHomeCapabilities_t.hideFaceLabel/hideCommands, filled by swiss.c's
  homeCapabilities as the cube's style is; _DrawHomeRoot gates the face name
  and the hint line, _DrawHomeRows its hint line. Home only:
  _DrawHintText also draws Detail's previews and other screens' essential
  prompts, and Restart's question keeps its hints.

Console has 24 rows: Waves sits before Wave Color and the other three after
Cube, so the route's Down Face is 12 DOWNs and Cube 13 (run.py). The smoke
route checks Home's hint line (HOME_HINT_BOX, level 120, measured on the
guide's home.png) shows by default and is gone with non-default.ini, which
now also has Hide Waves and Idle Animation=Sway (kept through Indigo's saves
by card_checks); it turns Face Labels off and on (fifteen DOWNs) with
flip_console(named=False), Route.quiet and an empty Settings row counter
(COUNTER_BOX), since Setup's page also has no name under the cube. Face
Labels can't go in the fixture: the route tells faces apart by name.

Tests: test_cube_render_pose runs the real setupCubePipeline through half a
swing (mirror reached, never passed, a long frame held, a restart after rest,
Calm within its drift), then through a turn with ui_scene.c's own idle blend
(restart from the pose, never faster than a swing) and a 0.3 s spring dip
(the swing resumes where it was, no step), with five mutants; test_ui_scene
holds the blend at 0 over the Source picker; test_background_gx_stream draws
the backdrop with Waves off and on (wash and rings stay), with mutants of the
gate and the setter; test_home_gx_stream draws Home with
each flag (name gone, hint gone, rows' hint gone, Restart's kept, both back);
the Home audit pins the gates,
Sway's bounds against the Home pose and _DrawBackground's SetWaves and
SetIdleSway calls; settings layout, semantics, views and
file tests take the four rows and keys; SETTINGS.md and the guide list them.
Not seen on a console yet. Settings pictures that show Console are now
stale and need recording again.

## 2026-10-08 — Emulator test: a press shorter than Settings' repeat, Settings walks read back

PR #114's smoke run failed "Down Face None: after System comes Library"
(passed on the dispatch run of the same tree and in lab runs). Its picture
`26-apps-face-off.png` shows Settings > Setup > Console on row 13 of 20,
Cube, not Down Face: the eleven blind DOWNs moved twelve rows, so the four
RIGHTs cycled Cube back to Infinite and Down Face stayed Apps. Not a lost
press, not a wrong offset, not Settings failing to save.

Cause, in the harness: `press()` holds a fresh wait of `PRESS_SECONDS`
(0.1 s) on Dolphin's TICKS reports, ten a second. It starts at the first
report after the hold and ends at the first report 0.1 s later, which with
reports a hair under 0.1 s apart is often the second: the console saw holds
of 0.2 to 0.3 s plus however late the harness let go. Settings (and every
`UI_MENU_INPUT_INITIAL_REPEAT_US` screen) repeats a held direction after
0.32 s. Lab, CI's emulator image and limits on the CI host: harness-measured
holds were 0.3 s in 162 of 332 presses; 275 blind DOWNs on an idle and a
loaded CI host all landed; holding each DOWN 30 ms longer than `press()`
means moved two rows on the first press. The failing run's Dolphin ran at
full speed through Settings (report gaps 0.08-0.11 s), so a late release
was enough.

Fix: `PRESS_SECONDS` 0.05, so a press is 0.1 to about 0.2 s of the
console's time (163 of 163 measured at 0.2) and 50 ms late still lands
(12 x 12 walks right, where 30 ms broke the old one). `flip_console` walks
Setup and Console with `walk_rows()`, which reads the focus back after every
press (`rows_moved`: focused slot plus list scroll, matched on the
unfocused labels; the focused label is bolder) and takes a double step back
with UP or presses a missed one again. With every DOWN held 150 ms too long
it corrected 9 of 9 Down Face cycles. Two full lab smoke runs: 123 of 123.
Settings itself reads held buttons each VSync and needs no latching; the
product is unchanged. The RIGHTs that change a value are still blind (no
value templates); the shorter press covers them and the route's face check
still catches a wrong value. A shorter press also shortened the machine's-time
fallback of a wait (`WALL_FACTOR` times its seconds, 0.25 s), which a slow
Dolphin would reach before its next report; `WALL_FLOOR` keeps it at 0.5 s
at least, as before.

## 2026-10-08 — Sanitized File Browser Detail test: no PIE, bounded runs

The sanitized host lane was OOM-killed twice on `test_files_detail.py
--sanitize` (PR runs of a tree that had passed on a dispatch run). Cause:
the harness built its ASan/UBSan binary as PIE, the one sanitized harness
that did not add `-fno-pie -no-pie` on Linux (the Makefile's SANFLAGS and
every other harness do, for exactly this). On the CI host's kernel
(`vm.mmap_rnd_bits=32`) with GCC 12's libasan, a PIE binary sometimes maps
inside ASan's shadow, faults before `main`, and the fault handler faults
again forever, printing `AddressSanitizer:DEADLYSIGNAL`; `capture_output`
held all of it in Python until the 6 GB container died. Not a product bug,
and not load: load only coincided.

Measured in the CI build image on the CI host: PIE detail binary 62 of 240
runs hung (4 loops in parallel); non-PIE 0 of 1200 (8 loops in parallel,
host load about 15); a trivial `int main(void){return 0;}` built PIE hung
19 of 100, non-PIE 0 of 100. The fixed harness passed 60 of 60 with GCC and
once with Clang there. Docker Desktop's arm64 VM has `mmap_rnd_bits=18`, so
this never shows on a Mac.

Safety net: `test_files_detail.py` runs every build and run through
`execute()` (120 s timeout, output to a file capped at 16 MiB by
RLIMIT_FSIZE, first 3000 bytes reported), which also replaces the old
plain-build rerun on failure. With PIE forced back in, a stuck run now fails
in about two seconds with "wrote more than 16 MiB, stopped".
`test_ui_png.py` (runs the Makefile's sanitized `test_ui_png_san`) gets the
same bounds through its own `run()`.

`buildtools/ci/source_checks.sh` now fails if a file in `buildtools` has
fewer `-no-pie` than `-fsanitize=...address` (18 files today, the Makefile
and `fuzz/run_fuzz.sh` among them); with `-no-pie` taken out of
`test_files_detail.py` it names that file.

Other harnesses that capture a subprocess without a timeout, not changed
here (none runs a sanitized binary): `audit_cheat_safety.py`,
`test_frame_budget.py`, `test_frame_copy_clear.py`, `test_save_art.py`,
`test_cube_render_pose.py` (plain C binaries); the sanitized harnesses that
run their binary uncaptured with no timeout (output goes to the job log, so
a hang ends at the job timeout, not in memory):
`test_gameflow_folder_navigation.py`, `test_gameflow_folder_visibility.py`,
`test_history_persistence.py`, `test_mp3_player.py`, `test_saves_stats.py`,
`test_saves_raw_controller.py`, `test_saves_raw_backend.py`,
`test_saves_card_io.py`.

## 2026-10-07 — File Browser: finishing touches

Seventh step of the two-pane File Browser, the part an emulator can check:
motion, the info bar after a box, the swap, the frame's cost and the
widescreen storage-menu detector. The hardware matrix is still to do.

Already in place and now pinned (audit_files_contract): a Copy's landed row
flashes under its message (filesSayPending sets UI_FILES_ROW_FLASH;
UIFiles_Flash: two pulses Full, one Reduced, none Off) and the message
fades out over 0.15 s (messageLeaving, skipped with UI Motion Off). Both
were seen in lab bursts at 60 fps: the flash on the right pane's new row,
in and out of the maroon box, per motion level.

swiss.c: filesInfo_t with filesInfoKeep/filesInfoPut. filesBox keeps the
focused entry's info bar and hints when a box opens and puts them back when
it closes, so a box's lines no longer stay under the next question or the
progress card's scrim; filesSay does the same around the message (before,
the bar sat empty with "A OK" until the next publish). The Actions item a
question follows (Swiss's Move and Delete questions) reaches it through
filesChosenLine, cleared on every press, instead of the bar's own line 1.
filesSwapSides, once the swap is allowed, sets filesSnapshot.swapping, and
waits UIFiles_SwapHalfSeconds (0.075 s Full, 0.05 s Reduced, 0 Off) so the
old sides do go even when the new ones are read within a frame;
renderFileList clears it before its first publish. The right pane's
storage hints (A Choose storage, no Z) also cover a read that failed
(readFailed), as its "isn't ready" message does.

ui_files.c/.h: UIFiles_SwapStep (the panes' contents ramp out while
swapping and back in after, from wherever they were; 1 with Off) and
UIFiles_SwapHalfSeconds; the snapshot's reserved byte is `swapping`.
FrameBufferMagic.c: _FilesShapes and _FilesWords take a content alpha (the
pane contents and the info bar's: rows, cubes, focus bars, tracks, chips,
free boxes, words; the boxes, buttons, info box and hints stay), the
banner shows only above half of it, and _DrawFiles steps it per frame. The
pane without the focus draws its device name, free space, path, counter,
row names and sizes in one pass (drawString, the light weight) instead of
drawStringMedium's two; its chips and its "isn't ready" lines stay medium.

After review: the swap's fade hid "Reading..." for the whole re-read
after Y (the published snapshot keeps swapping until renderFileList's
first publish, after scanFiles), so a slow device showed blank panes. The
reading band, its cells and the pane's message now fade with the chrome
(alpha), not the contents; the swapped-out rows stay gone under it. The
loading cells took 5 Hz in every mode; _DrawFiles now works out the lit
cell as the presentation card does (5 Hz Full, 2 Hz Reduced, the first
cell still with Off) and passes it to _FilesShapes. Both pinned, with
mutants.

Frame budget (frame_budget.c gains the light weight; re-baselined):
files 9,700 -> 8,140 vertices, files-right 9,724 -> 8,176, files-wide
12,892 -> 10,632, files-storage 10,184 -> 8,624, files-storage-right
9,748 -> 7,488, files-actions 9,800 -> 8,240, files-copy 12,168 -> 9,908,
files-message 9,644 -> 8,096; sqrtf unchanged (232 / 116 / 87). Still above
the 6,500 first estimate: the active pane's medium-weight words and the
info bar are most of what is left. The perf build on a console should
decide whether more is needed.

Opening and leaving (UIFiles_Stage, Memory Cards' timings) were checked in
bursts: Full hands the cube over and the chrome follows (about 0.5 s),
Reduced fades in 0.25 s and out 0.2 s, Off cuts. The swap cross-fades in
about 4 frames each way on Full and Reduced and cuts on Off.

Emulator: files_menu and files_box read the panes where UIFiles_Layout puts
them (files_pane_x, files_menu_edges) on the whole stage (files_stage):
in Menu Widescreen the right pane's menu sits past the 4:3 stage, where the
old FILES_MENU_EDGES and detection_frame never looked. FILES_MENU_EDGES and
FILES_ROW_PROBES are derived from them for 4:3, unchanged. New fixtures
files-storage-wide-left/right.png (this build, GC Loader card, Menu
Widescreen) and test_the_file_browser_storage_menus_in_widescreen.

Themes, by captures: Jet Black (grey structure, gold focus, typed cubes in
colour) over an Emerald backdrop, and Rose over an Azure backdrop; the
backdrop tints the paper, Wave Color shows nowhere (no waves).

Tests: test_ui_files testSwapFade; audit_files_contract 134 mutants (the
info bar kept and put back by boxes and the message, the fade-out, the
flash, swapping set after the gate and cleared before the first publish,
the draw stepping it, the light weight). Host lanes plain, contracts,
sanitized, whitespace, source checks and the upstream check pass. Lab
(CI's emulator image and limits, two at a time, this DOL in a card zip of
its own): all ten CI routes pass with the lighter unfocused pane (files,
folders, smoke on DVD and on the GC Loader with non-default settings, game
on DVD PAL with the AESND stop test, SD2SP2 and GC Loader in 40 pieces,
virtual cards in 4:3 and wide, the failing card's save).

Docs: system.md (the dimmer pane, Y's fade), CHANGELOG. The four File
Browser pictures in docs/guide/images show the unfocused pane's words at
the old, heavier weight: stale in that detail only, not re-recorded.

## 2026-10-07 — Swiss's old list styles removed

Sixth step of the two-pane File Browser: Swiss's three list renderers,
which nothing reached once the dispatch sent every folder outside the
Library to renderFileList, are deleted rather than kept as a fallback.

swiss.c: drawCurrentDevice, drawFiles, renderFileBrowser,
drawCurrentDeviceCarousel, drawFilesCarousel (already uncalled),
drawFilesFullwidth, renderFileFullwidth and gameflowSceneLayout (called
only from the Library branch, so its HORIZONTAL arm was dead; the dispatch
asks gameflowLayout()). renderFileCarousel is the Library alone: no
useGameflow; one `drawn` flag per frame (snapshot allocated, built and
taken) and `if(!drawn)` is the File Browser fallback with its re-scan, as
before; Y is always in waitButtons, posters always polled, B inside a folder
is gameflowInsideFolder() alone, filesActivate(directory, true) and
gameflowNavigateParent(true, ...). The two UIScene_RequestLibraryLayout
(HORIZONTAL) calls for the legacy carousel are gone. filesHome(void) is
`curMenuLocation = ON_OPTIONS` (no Swiss row to dim); filesManage drops its
cardArt flag (the Library is its only caller and always pauses CardArt).
filesActivate keeps its flag (false from renderFileList and
filesOpenDetail's fallback). swiss.h: renderFileBrowser and drawFiles
prototypes, FILES_PER_PAGE_FULLWIDTH. FILES_PER_PAGE (select_dest_dir) and
FILES_PER_PAGE_CAROUSEL (the Library's paging) stay; select_dest_dir,
select_recent_entry and Swiss's info_game are untouched.

FrameBufferMagic.c/.h: _DrawFileBrowserButton, DrawFileBrowserButton,
DrawFileBrowserButtonMeta, DrawFileCarouselEntry,
DrawUpdateFileBrowserButton, drawFileBrowserButtonEvent_t,
EV_FILEBROWSERBUTTON (enum, typeStrings name, dispose and draw cases),
bannerMaskTexObj, starTexObj and TEX_STAR. DrawVertScrollBar,
DrawSelectableButton, DrawEmptyBox and DrawTransparentBox stay
(select_dest_dir, the patch list, the args selector). IPLFontWrite.c/.h:
drawStringEllipsis and GetCharsThatFitInWidth (only the button used them).
images/: banner_mask, star_16 and the nine 96x32 type tags (dir, dol,
dolcli, elf, file, fpkg, gcm, mp3, tgc) leave the .scf files and the tree;
their GXTexObj globals stay, never loaded, because upstream filemeta.c still
points a file's meta at them (nothing reads fileTypeTexObj any more). The
region tags stay (info_game draws them).

Reachability: every removed name has no reference left in cube/swiss
(grep), and an ELF symbol diff against the previous head shows the
renderers were already dropped by --gc-sections; what goes from the link is
drawStringEllipsis, the two texture objects and the textures. DOL
5,532,080 -> 5,412,560 bytes (-119,520; text -6,752, data -112,812).

Settings: FileBrowserType, AppsBrowserType and GameBrowserType are still
read and written (round trip in test_settings_file.py, fileBrowserTypeStr
stays) and nothing reads them. Their rows left the pages earlier; now their
help strings (the Standard, Fullwidth and Carousel text), rowCycle cases
and toggle arms go too, with their three SET_*BROWSER_TYPE ids
(test_settings_views no longer lists them as dropped rows; nothing stores
an interface row's number), and audit_settings_semantics.sh drops the
three arms from its recorded base.
docs/examples/global.ini and SETTINGS.md's example set Library Layout=Grid
instead of GameBrowserType=Carousel.

Tests: audit_files_contract (the File Browser section now ends at
gameflowSnapshotGeneration; Swiss's list names absent from swiss.c and
FrameBufferMagic.c, their prototypes, font helper and images from the
headers and .scf files; no useGameflow in the Library; 3 more mutants, 122),
audit_gameflow_dispatch (gameflowLayout() in the dispatch; the Library's
calls as the Library; no legacy renderers), audit_home_hardware_followup
(no fullwidth consumer; the Library's stick follows gameflowLayout(), with a
mutant in place of the legacy one), audit_game_detail_safety,
test_browser_home_lifecycle (the File Browser's and the Library's B arms,
the same 400 returns), test_gameflow_folder_navigation, test_ui_color.
Host lanes plain, contracts, sanitized (clang here, gcc in the build image),
whitespace, source checks and the upstream check pass. Lab (CI's emulator
image and limits, two at a time, this DOL in a card zip of its own): all ten
CI routes pass: files (both boots, Detail and the launch; the frame-span
guard never saw Swiss's list), folders, smoke on DVD and on the GC Loader
with non-default settings, game on DVD PAL (and the AESND stop test), SD2SP2
and GC Loader in 40 pieces, virtual cards in 4:3 and wide, and the failing
card's save.

## 2026-10-07 — A game's Game Detail from the File Browser

Fifth step of the two-pane File Browser: A on a game in the left pane opens
Indigo's Game Detail, through the Library's own image path, and B comes
back to the same row.

swiss.c: renderFileList decides `detail` under the entry's lock (loads and
filesOpensDetail: IS_FILE, UIGameflowLibrary_IsGameImageName, the Source has
FEAT_BOOT_GCM), disposes its loading wheel and calls filesOpenDetail, then
returns to menu_loop, which calls it again on the same page (filePanel ==
filesPage: no opening, filesScene requested). filesOpenDetail: meta thread
stopped, filesOtherRelease, lock, gameflowFreshHandle, the disc header
(gameflowReadResolverHeader) as the eligibility proof, then
filesDetailSnapshot (a one-record Horizontal window: itemCount the listing's,
selectedIndex and libraryIndex curSelection, slot 0, gameflowSnapshotRecord
in IMAGE_FILES mode, gameId from the header), the Library's poster request
and one poll, DrawGameflow (not published), and gameflowLoadImageWithContext
with gameflowFromFiles set. In: gameflow_info_game's Detail mode and
load_game_with_context's Launch mode (Boot without prompts) go through
gameflowShowFromFiles, which, before the event was ever shown, sets the
mode with DrawSetGameflowModeNow (UI_MOTION_OFF) and DrawRepublish(page,
event); afterwards it is DrawSetGameflowMode as before. Out:
gameflowBackToFiles publishes DrawFilesSettled(&filesSnapshot) on top before
B's and the image loader's switch back to Library mode (once). The event is
disposed after, or DrawDiscard'ed when it never showed (a header mismatch);
then filesActivate (Swiss's load_file) when nothing was handled, and
UIScene_Request(filesScene). gameID_early_set is untouched: it still runs in
load_game_with_context before gameflow_info_game. gameflowPublishDetail adds
UI_GAMEFLOW_DETAIL_BACK_FILES, which ui_gameflow_detail.c draws as "B  BACK".
Recent and Autoload of a disc still go load_file → Swiss's info_game. WODE
keeps the Library's rule (no B in Detail there).

From the File Browser, Detail's Z goes through autoloadToggleConfirmed
before turning Autoload on, as Swiss's info_game asked there.
gameflowFindOppositeImage takes swissChoice (gameflowFromFiles): two
possible other discs no longer give none; the first that
UIGameflowResolver_NamedAsOppositeDisc names wins (meta_find_disc2's rule:
as long as the primary's name, the first case-insensitive difference equal
to the disc numbers'), else the last match, as Swiss chose. The info bar's
hint is A Start (kind OTHER) on a disc where filesOpensDetail is false (no
FEAT_BOOT_GCM, a `._` name). gameflowShowFromFiles keeps the page up, 60
vsyncs at most, until UIScene_Frame()->libraryReveal is 1: the Library
event draws nothing before the cube is behind it, and from a File Browser
over Home the Home cube showed for about 6 frames between the page and
Detail, which then faded in over it (a 60-a-second grab).

FrameBufferMagic.c: DrawSetGameflowModeNow (_GameflowSetMode shared with
DrawSetGameflowMode), DrawFilesSettled (seconds 60: past the opening),
DrawDiscard (frees an object never published). ui_files.c: the storage
menu's focus loop shifts an unsigned (gcc's -Wsign-conversion failed the
sanitized lane on the last step's CI run).

Tests: test_files_detail.py compiles filesDetailSnapshot and filesOpensDetail
out of swiss.c with FrameBufferMagic.c's _GameflowSnapshotValid and
gameflowSelectedRecord (first, middle, last of 2,000 rows; the header's ID
over a stale meta one; disc names on and off a booting Source), both lanes,
10 mutants; test_gameflow_detail testBackToFiles; audit_files_contract
check_detail (the lock, the release, header before posters, the flag, no
show from filesOpenDetail, in and out sites, the GameID before Detail, the
scene, the FBM pieces, the wait for the reveal, Z's question, the disc 2
choice, the hint), 28 more mutants (119). test_gameflow_resolver
testOppositeDiscNamedAsSwissDoes (disc 2 beside disc 1 over its NKit copy,
either way round, case aside; another first difference; no names).
Emulator: card.listing takes a folder; Emulator.burst grabs the screen at
60 frames a second from one ffmpeg (frame() starts a process each time,
about 0.2 s apart, which missed both the Home cube and a flying card). The
files route's second boot, while SD2SP2 still writes: files_detail (games/
on the GC Loader, DOWN to the probe's game, Flatten directory dropping
Racing.v1's row; A under an 8 s burst: DETAIL_CARD_BOX in each of the first
18 frames without the File Browser within 8.0 of the burst's last; B → the
same info-bar name and left rows, and the right pane's /backups rows), X
back up to games/; the failing copy; then files_launch (games/, A, A → the
probe with GPRE01).

Lab (CI's image; run.py starts the card zip's ipl.dol, so each build's DOL
goes into a copy of the zip): files route passes (both boots, the launch
reaches the probe). The first version's DOL fails the burst check (the
Home cube, cover 47 away), and so does the fly-in mutant
(DrawSetGameflowMode in gameflowShowFromFiles: cover 48 away for all 18
frames); an instrumented build waited 13 vsyncs for the reveal. DVD smoke
and PAL game routes passed on the first version; captures in 4:3 (GC
Loader, non-default settings) and widescreen.

After review: the first Detail after boot showed the banner, not the
poster, until it was opened again. filesOpenDetail's request opens the
pack, and cacheInit's invalidateAll quarantined every slot for 40 ms
(UI_ASSETS_EVICT_QUARANTINE_MS) although a new arena has never been drawn
from, so its one poll read nothing; and nothing polled while Detail was up.
The Library hid the first because its list polls every idle retrace before
A, but its Detail had the second: A within 40 ms of a slot being let go (a
quick scroll past 25 games, a source change) kept the banner too. Now
cacheInit stamps a new arena's slots with no quarantine (a re-Init over
drawn posters keeps it), and gameflow_info_game polls posters on its idle
retraces, one read at most, as the Library's list does; a late poster fades
in (_GameflowArrival). test_ui_assets test_fresh_arena_reads_at_once (2
failures without the fix); audit_files_contract pins the Detail poll, with a
mutant (135). Emulator: files_poster at the end of the files route's first
boot (no Detail before it): Astral Circuit's Detail, under an 8 s burst,
shows card.poster_light in DETAIL_CARD_BOX (POSTER_PIXELS 1000; the banner
has about 14, the poster about 2,600) within POSTER_FRAMES (30) of its first
frame. Lab: without the fix no frame of the 8 s has it; with only the
Detail poll it fades in 26 frames after Detail's first; with both it is on
the first. Files, DVD and GC Loader smoke and PAL game routes pass.

Second review: the idle poll put reads on Detail's menu thread (a poster,
43,648 B, and a still, 38,400 B, at most one each a retrace), and Detail
saw only held buttons, so a tap made and let go during a read was lost.
The Library's list and the File Browser already OR in padsButtonsTaken.
gameflow_info_game now passes padsButtonsHeld() | padsButtonsTaken(
detailButtons) to UIMenuAction_Update and drains the latched presses after
each UIMenuAction_Init (entry and after a modal), so a B that left Settings
isn't seen again; audit_game_detail_safety pins both, with 3 mutants. The
poll finishes every job already queued for the window, not only Detail's
poster: from the Library up to 8 neighbours and, in spotlight, 3 stills;
from the File Browser, stills a spotlight visit left pending. Each is one
bounded read on an idle retrace, Detail's own poster first unless its slot
is quarantined. Kept: it is what the Library's list would read next anyway.
files_poster now allows POSTER_FRAMES 2 (was 30, wall-clock frames of the
60 fps grab): with both fixes the poster is on Detail's first frame (41/41),
the poll-only build had it 26 frames on, which now fails, so the route
holds the fresh-arena fix too.

## 2026-10-07 — The File Browser's operations to the other side

Fourth step of the two-pane File Browser: Z opens Indigo's Actions box
beside the row, and Copy and Move go to the folder open in the other pane,
through Swiss's own file operations.

swiss.c: manage_file is manage_file_ex(MANAGE_ASK, NULL). manage_file_ex
(option, destDir) runs Swiss's Z box only for MANAGE_ASK (re-indented,
`int option = 0` now an assignment) and, with destDir, skips
select_device(DEVICE_DEST), the destination mount and select_dest_dir: the
caller has set and mounted DEVICE_DEST, and destFile->name is destDir. Both
blocks are pinned by hash (one tab in, trailing blanks off) in the contract
audit. The target name is manageDestName (stripInvalidChars, .gci off a
card), which the screen uses for the fit, the same-name lookup and the
ghost row. manageDropPartial (closeFile again, harmless, then the
destination's deleteFile) runs at the read failure, the write failure, the
final write or close failure, and on Stop (`cancelled &&`), before
free(destFile); Swiss's own callers get it too. Move's deleteFile result is
now kept: a failed delete says "Copied, but couldn't take it off ...", and a
Move without deleteFile reports as a copy. Presentation hooks, all behind
filesBoxes (set only around manage_file_ex in filesManageFrom, both Autoload
toggles and A on a firmware file): confirmAction asks filesAsk
(UIFiles_ParseQuestion; -1 falls back to Swiss's box); Delete asks
filesAskDelete with Swiss's own strings (duplicated, pinned twice); the
file-exists box is filesAskExists, whose choice becomes the button Swiss's
loop reads (A Keep both, Z Replace it, B Cancel), Keep both greyed when
only replacing makes room (filesFitsBoth); the progress card is
DrawProgressBarFiles; every result is manageTell / manageCopied →
filesSay (maroon message, 2 s or A/B, failures wait for A; the outcome
feeds the focus rule). manageLanded records where a copy landed after Keep
both. filesBox is the one box runner (storage menus now use it too): Up/
Down wrap, A or a letter chip chooses, greyed items take the focus and show
why, B or the close buttons shut it, chord = L held with A (A alone on
Delete does nothing; L, A and B let go after). filesActions builds
UIFiles_Availability from both sides (filesSide reads info() only on a
mounted side), the letters, line 1 (UIFiles_ActionLine) and line 2 (the
reason or the fit); runs only an enabled action; Copy asks "Copy to <x>?"
with the ghost row (UIFiles_LandingIndex, UIFiles_InsertGhost on the
snapshot, restored after); Move's ghost shows under Swiss's Move question.
filesManageFrom swaps the slots for a right-pane entry and restores both on
its one exit. Focus after (UIFiles_FocusAfter, by name, from the old
listing): the left pane's into curFile, the right's into focusName, the
other side on manageLanded; a right-entry action always writes the left's
own focus back. A on a file that doesn't start, and on a .fpkg off a
FlippyDrive (filesLoads), opens Actions. filesManageEntry is gone; the
legacy lists still use filesManage / filesManageFile. Two Swiss lines lost
trailing blanks (select_recent_entry, upToParent) for the whitespace gate.

ui_files.c/.h: UIFiles_ActionLine, UIFiles_Result (finished, stopped,
write/read failed, kept), UIFiles_InsertGhost, UI_FILES_ROW_GHOST;
uiFilesMenu_t gains row, rose and letter[]; uiFilesStorageMenu_t gains warn
(a storage menu's = dim); the snapshot carries message[2], its serial and
measured width. FrameBufferMagic.c: _FilesMenu beside any row, letter chips
(flat, no sqrtf), Delete's rose edge; ghost rows outlined and pulsing
(0.84 + 0.16 sin 3t, steady with less motion); _FilesMessage (one or two
lines, in over 0.10 s); the progress card's files flag (scrim, B Stop).

Tests: test_ui_files testAvailabilityTable (manage_file_ex's six permission
lines copied verbatim, held equal to swiss.c's by the audit; every device
and entry: Rename/Hide/Delete exactly Swiss's, Copy/Move only where Swiss
allows them, a Move only where Swiss renames or deletes the original),
testActionWords, testGhost, the storage menu's warn bits;
audit_files_contract 91 mutants (the four drop sites, a finished copy
deleted, the drop on the Source, Swiss's box and picker hashes, the preset
folder, the name built twice, a greyed action run, Move offered without
delete, a failed delete silent, the chord and its release, Delete's and
Move's words, filesBoxes left on, Keep both offered, the slots); the waits
audit names manage_file_ex; test_ui_color keeps the rose edge and the
message's second line. Frame budget: files-actions 9,800 vertices, 116
sqrtf; files-copy 12,168; files-message 9,644 (with a flashing row), 87
sqrtf; the five earlier scenes unchanged. Emulator: files_box (a box's top and bottom edges
across the right of a pane, not a row's outline), the Z step now reads it;
a new route `files` (GC Loader Source, card.second_card in SD2SP2): copy
(byte for byte on the second image), Keep both, Move right to left (gone,
whole on the GC Loader), Rename via the keyboard, Delete (A alone deletes
nothing, L+A does), and a 160 MB copy stopped after 0.6 s (nothing left on
the GC Loader); a CI matrix job runs it. Docs: system.md (Copy, move,
rename, hide and delete, two pictures), controls.md, CHANGELOG.

Review follow-up, before merging. Memory cards: a card's write strips the
GCI header and returns length-64, and the trailing writeFile(NULL, 0) on a
plain file asks for another block (-11), so card copies reported failure
and the cleanup's delete (by the save's internal name) removed the finished
save; Keep both onto a card wrote over the save in place. Now
manageDropPartial never deletes on a card (Swiss's own copy keeps what it
wrote, as before) and UIFiles_Availability greys Copy and Move onto a card
("Use Memory Cards to copy saves."; uiFilesDevice_t.card). manageDropPartial
also leaves a same-named folder alone (statFile says IS_DIR), and the box
greys Copy and Move when the landing name is a folder there
(uiFilesEntry_t.existsFolder). Replace it now checks deleteFile and cancels
with "Couldn't replace it." when the old file stays; a replacing copy that
stops or fails says the old file is gone (UIFiles_Result's replaced). The
result message is kept (filesSayLater) and said by renderFileList after
both panes are read again (filesSayPending; at once when nothing changed),
so it sits over the new state; a Copy's landed row flashes under it
(UI_FILES_ROW_FLASH, UIFiles_Flash: two pulses Full, one Reduced, none
Off) and the message fades out over 0.15 s (messageLeaving). The ghost row
is a light lilac fill with a 2 px white edge, unlike the focus outline; the
File Browser's boxes and message are opaque. Box titles are cut in the
middle to the pane (filesFitAt, title is UI_FILES_TEXT_CAPACITY,
UIFiles_MenuBox caps the width at the pane less 12) and message lines to
560 px. The audit pins manageDropPartial's body, the card and folder guards,
the stopped Move's `if(!cancelled)`, Replace's checked delete, the
destination argument (thereDir) and the kept message, each with a mutant.
The files route adds a stopped Move (original whole on the SD card,
nothing on the GC Loader), a copy into backups/ on the right, a progress
check before each B (the red B glyph, files_progress) and a stop message
unlike the finished one; then it boots again with the SD2SP2 card failing
writes to every cluster it had free (card.free_sectors) and copies into
backups/: the failure waits for A and no whole copy lands. On that
emulated card every write after the first failed one fails too (deleting
another file fails), so the unfinished file stays and the message says part
of it is left; deleting where the card allows rests on the audit.

Lab (CI's emulator image, one Dolphin at a time): the files route, both
boots, passes; earlier the DVD smoke and the GC Loader smoke (non-default
settings) routes passed with the Actions box.

## 2026-10-07 — The File Browser's second device

Third step of the two-pane File Browser: the right pane can hold a storage
of its own, and L and R choose each side's, as in Memory Cards.

swiss.c: filesPane_t gains mount (uiFilesMount_t), readFailed, status and
its own free text; its device is kept for the session and never stored in
devices[] (Settings' Load at startup unmounts whatever DEVICE_DEST held).
filesOtherFree drops the listing; filesOtherRelease drops it and deinits a
mount of the pane's own; filesOtherMount is manage_file's mount block
(deinit, stats off, init, stats on), SHARED when the device is the Source;
filesOtherRead's failed read on its own mount mounts once more before it
reports (the read is the mount check), then a gone folder falls back to
the top. filesOtherAcquire picks the first device (UIFiles_RightOnConfig:
the Configuration Device when set, detected and not the Source; the Source
without File Management), drops a device that clashes with a new Source,
and notices a Source change under a shared pane. Release points:
menu_loop's Library branch and empty-folder branch (from the last step),
starting a file (A on a loadable left file), both Autoload toggles (then
acquire again), START when Recent is on, the left Other devices...,
filesSourceChange, filesSwapSides, filesOtherPick, B, and the end of
renderFileList on any exit or Source change. A constructor registers
filesOtherResetInfo (priority 1, as deviceHandler.c's): on reset it deinits
an OWN mount without QUIRK_NO_DEINIT. sourceCommit (select_device_internal's
DEVICE_CUR tail; EXI_ProbeReset now runs after the slot is set, which reads
nothing of it) and sourceMount (menu_loop's confirmed-source block) are
pulled out unchanged and used for L's in-place Source change and Y.
filesStorageMenu lists the detected readable devices (allDevices order, at
most six) and Other devices...; Up/Down wrap, A chooses (greyed bumps), B,
L or R close; the info bar shows the focused device (filesMenuInfo). R's
Other devices... is filesOtherPick: release, DEVICE_DEST emptied, the A
that chose it released (the picker reads held buttons; without the wait it
took the A and chose its first device), select_device(DEVICE_DEST), the
choice taken, the slot and filesScene restored. X or A on ".." at a pane's
top (and A or X on an empty right pane that failed to mount) open that
side's menu; with no entry there the hints are "A Choose storage" and B
Home (UI_FILES_HINTS_STORAGE) and the info bar names the device. While a
menu is open, an item's second sentence goes to the info bar's second line
when it has no reason to show there. A memory card's or Qoob's free space
is in blocks (filesBlockSize, shared with the rows). renderFileList now drops stale presses after acquiring the
right pane, just before the page is published: a press made while a disc
drive reset for the pane (Y onto the disc) was otherwise taken as the next
action's. Y is
filesSwapSides, gated by UIFiles_CanSwap with "<Device> isn't ready, so the
sides can't swap." Z on a right entry on another device swaps DEVICE_CUR to
the pane's device around manage_file (filesManageEntry) and sets manageKeep
to the Source; manage_file's destination mount and its cancel unmount skip
manageKeep, so choosing the Source as the destination neither remounts nor
(on B in select_dest_dir) unmounts it. Every box in the screen goes through
filesManageFile(keep): keep is the Source for a right entry on another
device, else the right pane's own (OWN) mount, so a left Z or A on a file
that can't start never deinits the pane's device (choosing it as the
destination, B in the folder chooser, a failed init, or the picker's
"deinit the old destination" on a DEVICE_DEST a finished copy left
holding it). A DEVICE_DEST holding what is kept is emptied during the
call. filesOtherPick checks the picker's choice against the Source
(filesClash now gives the reason): a clash keeps the pane's device and the
info bar says why, as the picker greys nothing. Paging is the C-stick's only. Hints keep Y Swap
sides; the right pane's game note says "Y swaps the two sides."

ui_files.c/.h: UIFiles_RightOnConfig, UIFiles_NotReady (three message
lines: "<Device> isn't ready.", the status in brackets or "Couldn't read
<folder>.", "Press R to choose storage."), UIFiles_MenuMotion (0.92x, 0.97x
Reduced, 0.08 s in, 0.10 s out), uiFilesMenu_t and UIFiles_StorageMenu
(titles, items, greyed clashes with their reasons, what choosing does,
focus). The pane snapshot's message has three lines; the snapshot carries
the menu. FrameBufferMagic.c: _FilesMenu (Memory Cards' box look, any
count up to nine, placed by UIFiles_MenuBox below the pane's button),
drawn after the words with its own spring and open/close clock;
_SaveCubesMenu untouched.

Tests: test_ui_files testSecondDevice and the storage hints;
audit_files_contract (54 mutants: every release point, including choosing
storage and a clash reset, the reset hook's registration, priority and OWN
guard, the mount check, the picker's slot, A release, scene and clash
note, the slot swap's restore, what filesManageFile keeps and the slot it
empties, the first device without File Management, a greyed item, L's
Source mounted, X at the top, L not paging); frame budget files-storage (4:3,
left menu) and files-storage-right (wide, right menu, a pane not ready):
10,184 and 9,748 vertices, 116 sqrtf; the button words moved files,
files-right and files-wide by 96 vertices. Emulator: files_menu (title and
items boxes' top edges, 4:3), files_storage on every route with cards (R
and B; R, Memory Card - Slot A and A; RIGHT and DOWN browse, read by the
info bar's name since Jet Black's focus bar isn't text-bright; Y swaps, read
by the Source's name moving to the right pane, and Y swaps back; L and B),
files_not_ready on the DVD route (UP twice to Other devices..., Z, RIGHT
twice to KunaiGC: the pane says so, Y bumps, R puts the disc back); two
Dolphin fixtures. Docs (system.md storage section and picture, controls.md) and
CHANGELOG. No upstream file changed.

Lab, beyond the routes: Z on a Slot A save in the right pane beside a GC
Loader Source, X Copy, the GC Loader: CloudBakeryData.gci on the SD image
with the save's blocks; Move of that file from the left to Slot B: the GCI
folder gained it and the SD image lost it. With an SD2SP2 card beside the
GC Loader Source on the right: a left Copy to it cancelled in the folder
chooser, a Copy to it, then a Copy to the Source cancelled; after each the
right pane's Delete removed a file from the SD2SP2 image (before the fix
both cancels left it unmounted and Delete failed). Other devices... on the
right, FlippyDrive Flash beside the GC Loader: the pane stays and the info
bar says "GC Loader is open on the other side." Not measured: the free heap with
two 2,000-entry folders (a console job).

## 2026-10-07 — The File Browser's two panes in place of Swiss's lists

Second step of the two-pane File Browser: the screen, everywhere outside the
Library. menu_loop's dispatch is now the Library wherever
gameflowLibraryMode says so, else renderFileList; File Browser Type is no
longer read, and the scene requests moved into the Library's branch (the
File Browser asks for none, so Memory Cards' backdrop hands the cube over
where Home or the Library left it; it records which and asks for it again
on a return, after Swiss's game screen turned it). The legacy renderers
stay compiled and unreachable until they are deleted; renderFileCarousel's
fallback (no memory for its snapshot) now sets gameflowListFallback, puts
the focused name in curFile and asks for a fresh scan, since
gameflowLibraryEntries has reordered the listing; the flag lasts one
renderFileList call.

FrameBufferMagic.c: EV_FILES (appended, so EV_SAVE_DETAILS keeps its
number), DrawFiles / DrawUpdateFiles / DrawUpdateFilesReading, _FilesShapes
(boxes, buttons, focus bars, cubes, tracks), _FilesWords, _FilesCube (flat
quads only: no sqrtf, no trig, no texture), and _FrameCovered's EV_FILES.
The page draws _SaveCubesBackdrop with UIFiles_Stage's paper and handover
(Memory Cards' timings, new in ui_files.c with LeaveSeconds); B waits for
the page to leave before Home is published. The loading wheel has a
PROGRESS_BOX_FILES position in the info bar's corner (the word stays: the
clock-corner test pins that line). Colours that keep their hue (teal,
amber, pink, the bolt and the program folder's mark) are in test_ui_color's
SEMANTIC; the text cube's paper is a neutral.

swiss.c: renderFileList on the shared listing. Every string is fitted on
the menu thread into a uiFilesSnapshot_t (widths measured there too, so
the draw measures nothing). Left rows lock their entry; the idle refresh
(four times a second, for the meta thread's banners) trylocks and gives up
on a busy entry. The wait loop populates one visible left row a frame on
every device (filesMetaStep, reset per window and per listing, skipping
rows already read), as drawFiles read its visible rows: without
FEAT_THREAD_SAFE it is the only reader, and with a meta thread it is the
caller that lets meta_alloc evict rows out of view once the 512-entry pool
is full (the meta thread only kills itself there). A populates the focused
entry first, so a program folder or a second disc is known before it acts.
current_view_start/end come only from UIFiles_LeftView. Input acts on
press edges (padsButtonsTaken) plus its own Up/Down repeat (320/120 ms) and
two UIMenuInput polls, the stick (rows) and the C-stick (pages,
padsSubMenuInputPoll, new in input.c); presses are dropped on opening and
after a box, kept across a left folder change (filesKeepPresses), and
dropped by menu_loop's device-change block, since the Source picker reads
held buttons and leaves its A or B in the latch. X at the top waits for
its release before the picker, whose X toggles the EXI-speed mode. X is
PAD_BUTTON_X alone: BUTTON_X includes C-stick Right/Down. The right pane
(filesOther) is readDir + sortFiles on the Source, at its own folder, with
".." left out at the device's top (no storage to choose there yet); it is
read again when the Source changes, and released when the screen is left,
when menu_loop goes to the Library and before the empty-folder branch
deinits the Source. A, X, Z, START, B and the clap go through the shared
list-action helpers; Z on a right entry or a program folder goes through
filesManageEntry (today's Z menu), which writes the left focus name back
into curFile; filesToggleAutoload takes its folder. A change made from
either pane marks the right listing unread, so both are read again; a
right-pane change to the left's folder or one above it (filesWithin) moves
the left pane to the right pane's folder first, rather than letting
scanFiles fail and unmount the Source. The right pane goes up a folder by
the ".." entry's fileBase (a disc's readDir finds a folder by it), or the
Source's initial at the top. L and R page. DrawUpdateFilesReading(page,
pane) runs before menu_loop's scanFiles (left) and the right pane's folder
reads; the band hides the row words under it as it does their cubes. The
device name above each pane is fitted (UIFiles_FitDevice: 0.92 down to
0.64, then cut at its end) into what the SOURCE chip and the free box
leave, so "Memory Card - Slot A" no longer runs into the box.

Settings: File Browser Type's three rows left libraryRows[] (ids,
tooltips, cases, toggle arms and keys stay; UI_SETLAYOUT_ROWS_LIBRARY 11);
UIGameflowLibrary_SelectBrowser deleted with its tests.

Tests: audit_files_contract.py (new, 17 mutants); audit_gameflow_dispatch
(new dispatch shape, renderFileList among the latched waits, filesMetaStep
as I/O); audit_home_hardware_followup (renderFileList's two polls, the
C-stick port reads); test_browser_home_lifecycle (renderFileList's B arm,
EV_FILES covers the frame); test_gameflow_folder_navigation (renderFileList
stub, the fallback, releases); test_settings_views, test_ui_settings_layout
(rows), test_ui_files (UIFiles_Stage, UIFiles_FitDevice with Swiss's long
device names in both shapes), frame budget scenes files, files-right,
files-wide. The budget counts the words too, through a font stand-in (11 px
a character, a quad a character a pass, two passes) with Redump-length
names fitted into every row, and the hint line's buttons: 9,604 / 9,628 /
12,796 vertices and 232 sqrtf. That is above the first 6,500 estimate,
which assumed about 26 characters a row; drawing the inactive pane's
words in a single pass waits for a console measurement. Emulator: files_screen and active_pane replace the legacy
detector in the routes (it stays as the folder-transition guard);
file_browser checks both panes' focus, DOWN and A on a right folder and X
back to the same rows, a left folder change and X back, Z opening Swiss's
box over the rows (by the text in its band) and B closing it, and B back to
System's same label; files_text reads each pane's path line, the left one
from the capture in Menu Widescreen; files_face and
library_after_file_browser use the new detector; four Dolphin fixtures. Docs and CHANGELOG; docs/guide/images/files.png replaces
library-file-list.png. No upstream file changed.

Validation: host plain and contracts lanes; whitespace, source and
upstream checks; a preview DOL through the Emulator DVD smoke route, the GC
Loader smoke route and the folders route.

## 2026-10-07 — The two-pane File Browser's model

First step of the two-pane File Browser: gui/ui_files.c and .h, pure C
with no libogc, so the host tests build it. Nothing calls it yet; swiss.c
and FrameBufferMagic.c are unchanged, and nothing a player sees changes.

It holds: the two panes' focus (Up/Down wrap, pages stop at the ends,
Left/Right cross and each pane keeps its row, an eight-row window with a
row of margin, and the left window as current_view_start/end whichever
pane is active); UIFiles_Layout for 4:3 and Menu Widescreen (outer edges
at the stage's edge plus 40, inner edges fixed at 312 and 328); row kinds
from the extension; UIFiles_IsProgramFolder and the folder a rewritten
entry stands for; ".." row names; sizes as formatBytes writes them (a
port, since util.c needs libogc), blocks on a card or a Qoob; the middle
cut of real names keeping only the extension and a "(Disc N)" before it,
never inside a UTF-8 sequence, and the path line cut from the left at a
folder; the hint lines; UIFiles_Availability with every reason, the fit
(unknown free space never greys; replace-only when only replacing makes
room); UIFiles_FreeKnown, UIFiles_StorageClash with its reason,
UIFiles_CanSwap, UIFiles_ExistsChoices, UIFiles_MenuBox,
UIFiles_LandingIndex (fileComparator's order), the focus after an action,
and confirmAction's texts read into a title, a line and two items.

Choices made here: a card's or a Qoob's row size is "N
blocks" (Swiss's own text, "56 KiB (7 blocks)", is too wide for the size
column); Move's folder reason says "moved"; Move counts as possible when
the device renames across folders (Swiss's same-device Move) or deletes
the original after copying, so it never ends as a copy; the fit reason
puts both amounts in the larger one's unit to three figures ("0.80 GB
free; this needs 1.35 GB"); the 112..336 window holds 7 items with a
title or 8 without (UI_FILES_MENU_ITEMS), so a storage menu lists at most
6 devices (UI_FILES_STORAGE_DEVICES) and "Other devices..." reaches the
rest; a cut keeps a tail only with at least 6 characters of the start
before it (UI_FILES_NAME_MIN_HEAD), else the extension alone, so a
narrow column never shows a bare "(Disc 1).rvz"; replace-only is per
action, never set for a same-device Move (a rename); two network
devices always clash, even before the adapter's slot is known.

Tests: test_ui_files.c in TESTS and the Makefile, both lanes. The pane
reducer is checked against an independent model over every input
sequence of five on lists of 0 to 25 entries, with the window's rules
(inside the list, the margin, moving only when it must) and the left
window after every step. Layout to the pixel in 4:3, and the wide
stage's edges; long Redump-style names at both shapes' name widths with
and without a size, cut to the longest start that fits, with region and
language tags right before the extension cut too; the shrink at the
largest scale that fits and at both scale boundaries; every
availability reason, in its order of priority where two apply, and the
fit at exactly the free space and, over 4,096 combinations, never more than
manage_file's own canCopy/canMove/canRename/canHide/canDelete; every
storage pair; landing against a scan of a qsorted
listing; delete and rename at the list's edges through a re-read and the
reducer; every confirmAction text in swiss.c byte for byte. Hint lines
parse through ui_hint.c with at most five round buttons in every
context. No upstream file changed.

Validation: host plain, sanitized and contracts lanes; whitespace,
source and upstream checks.

## 2026-10-07 — One copy of the file list's actions

Behaviour-free groundwork for a new file list. renderFileBrowser,
renderFileFullwidth and renderFileCarousel each had their own copy of A,
X, Z (with Autoload on ".."), START, B and the Bongo clap. They now call
filesActivate, filesUp, filesManage (and filesToggleAutoload), filesRecent,
filesHome and filesBarrelGame in swiss.c, in the same order with the same
conditions, locks and returns. The carousel's differences are parameters:
useGameflow (".." through gameflowNavigateParent; no Swiss row to dim on
B) and cardArt (CardArt_Pause after meta_thread_stop on Z and START).
gameflowNavigateParent moved up beside them and ends in filesUp, which is
the old tail. Each helper reads directory[curSelection] as the loops did.

Tests that pinned the copies' text now pin the helpers:
audit_gameflow_dispatch (the carousel's legacy A, its three parent routes,
B inside a folder before Home), test_browser_home_lifecycle (B arms with
filesHome compiled in), test_gameflow_folder_navigation (".." taken from
filesActivate; filesUp and filesHome in the harness; older revisions still
replay), test_settings_file (three config_update_autoload calls, each
with its box). No upstream file changed.

Validation: host plain and contracts lanes, source checks, the whitespace
and upstream checks; a preview DOL through the Emulator smoke routes (DVD
and GC Loader) and the folders route, built from this commit and its
parent: the same 155, 158 and 299 checks pass on both, none fail.

## 2026-10-06 — Take latched presses around every device read; whole presses in the emulator test

Follow-up to the Library and Apps lost-press fix. A survey of every
menu-thread loop that reads padsButtonsHeld(): Home, Settings
(settingsWaitForInput and its boxes), the retained Detail, the cheat
browser, System info, the legacy lists (renderFileFullwidth, Recent,
select_dest_dir, select_alt_dol) and the confirm boxes read once per VSync
with nothing else between reads, so held buttons see any press of a frame or
more. I/O there runs between waits, where the per-wait clear drops a press
anyway. Memory Cards' boxes (save details, Actions, storage, folder page)
already read through inputNext, which takes the latch. The loops that read
held buttons around I/O were manage_file's copy and verify_game, a chunk
(up to 256 KB and 512 KB) per pass with B to cancel: both now take
padsButtonsTaken(BUTTON_B) too, after dropping an older B.

audit_gameflow_dispatch.py's check_pollers became check_waits: it finds
every loop in Indigo's own sources (check_upstream.OWN) whose own body reads
held buttons and does I/O (CardArt_Poll, artLoad, readFile, writeFile,
readDir, populate_meta), and requires a padsButtonsTaken read in it, no
clear inside it, and a clear of the same mask right before it (inputNext's
is inputInit, which audit_saves_safety checks). 15 mutants per run: each
loop reading held only, clearing inside, or keeping old presses, and a
Settings wait that starts polling card art.

The CI failure that started this (run 37541764782, GC Loader smoke, "B
closes the box") was not the console: the box reads the latch, the PC
samples show the idle thread for all ten seconds after B with no card read,
and libogc2's PAD_ScanPads keeps an edge across reads that fail. B never
reached the console. Deadline took its start from Dolphin's last TICKS
report, which is up to 0.1 s old, so press() could let go about 20 ms after
holding when it followed a screen grab (the step before reads the screen
twice and saves a picture). press() now uses fresh waits: they count from
the first report after the wait began, so a press and the release after it
each last at least their seconds of console time, at up to 0.1 s more per
press.

Validation: host plain and contracts lanes, the emulator harness tests
(new test_a_fresh_wait_counts_from_a_report_after_it_began),
check_whitespace.sh origin/beta, source_checks.sh. The audit fails on the
copy loop as it was. Not run on a console.

## 2026-10-07 — File Browser as a face of the cube

UI_HOME_FACE_FILES after Emulators, on no side until Settings gives it one
(Up, Left, Right or Down Face › File Browser); always there once placed, as
Memory Cards is. A on it and System's row both go through ui_home.c's
openFiles(): UI_HOME_EFFECT_OPEN_FILES with a source, CHANGE_SOURCE without
(hint A OPEN / A SELECT SOURCE, as Library). Its icon is Source's folder
(drawFolderIcon) at `UI_HOME_FACE_FILES * UI_HOME_ICON_CHOICES`. B from
Swiss's list needs no new code: OPEN_FILES never touches homeState, so Home
comes back on the face that opened the list (audit pins that).

Eight faces: every per-face literal in the stroke stream (including the
zero-filled icon arrays that would have drawn face 7's choice 0 on an
uninitialised basis), frame budget, the scene's faceLift/facePitch; the
motif state still fits 640 bytes (636). The scene, motif, reducer oracle and
render pose tests put File Browser on each side, Classic and Infinite, with
and without a source. The Dolphin route's Down Face flip now takes four
RIGHTs from Apps to None, then one LEFT to File Browser: RIGHT from System
turns to it, A shows Swiss's list, B returns to it; three LEFTs restore Apps.

## 2026-10-06 — Choose the save copy a game starts with

Left and Right on a game's details step through the copies the Saves box
counts. Saves_CollectGameStats now also lists where each is (Slot A or B, a
Save Folder file, or a card image and its ordinal; the first eight), and
Detail reads the cards once per open (context->savesScanned): Left and Right
republish without reading them again. The box shows "Copy N of M | where"
and In use or Loads at launch; its label becomes « SAVES ».

A on Launch Game with a copy that isn't on the card the game reads asks
first (A LOAD, B KEEP). Saves_LoadCopy then writes the card's own copy of
that save to the Save Folder (read back), removes it, writes the chosen copy
(cardWrite reads it back) and, if that fails, puts the card's own copy back
(cardReplace); Detail stays open on B or any failure. The card is the first
slot holding a copy (Slot A first), else the first with a card. Not offered
while Emulate Memory Card is on.

Validation: test_saves_stats (copy places), test_saves_card_io (replace:
success, write fails, read-back fails, restore fails, no own copy),
test_gameflow_detail, test_launch_gx_stream (the choice inside the inset),
audit_game_detail_safety (asked before the load, the load before the launch
screen, Emulate Memory Card), audit_saves_safety (kept before replaced, put
back on failure), both host lanes; end to end in Dolphin with a GC Loader
card and a GCI card in Slot A: RIGHT, A, A left Slot A holding the image's
copy and the Save Folder the card's own. With a MemCard PRO emulated in
Slot A (the issue #102 lab patch, GameID on): the game ID goes out before
Detail opens (load_game_with_context), so on the first open after a switch
Detail's read of Slot A is cut off by the switch and the box misses that
card's copies (only the image's: no box); on a reopen the card is in already,
Detail reads the game's own card, and RIGHT, A, A put the image's copy on it,
its own copy in the Save Folder, the startup card untouched. That first-open
read is issue #102's open question, not this change's. Known:
game-details-saves-after-copy.png shows the old label.

## 2026-10-06 — Show the details screen's Saves box for two or more copies

UIGameflowDetail_Build sets UI_GAMEFLOW_DETAIL_HAS_SAVES only for two or
more save copies; with fewer, Detail is drawn in its v2.2 places (no inset,
rows 20 px higher, panel 328 tall) from one layout table. Settings › Setup ›
Library › Saves on Details (on by default; `Hide Saves on Details` in
global.ini, a hide flag so zero is the default) turns the box off, and then
gameflowPublishDetail never calls Saves_CollectGameStats: opening a game's
details touches neither memory card slot.

Why (issue #102): with a MemCard PRO GC the box reads the cards straight
after Indigo sends the game ID, so it can count the previous game's virtual
card, and for one copy it says nothing useful; the reporter asked for a way
to hide it. Whether that read also stops the switch is unconfirmed on
hardware; the GameID send itself is upstream's code at the same point as in
Swiss.

Validation: host plain and contracts lanes; GX stream tests pin both
layouts and the focus frame's rows; detail_frame_budget re-baselined (124
vertices without the inset); the emulator's virtual-cards route checks no
box with one copy and the box with two (both virtual-cards jobs);
non-default.ini seeds `Hide Saves on Details=Yes` so the key survives saves.

## 2026-10-06 — GC Loader starts Indigo at power-on as boot.dol

Stock Swiss auto-starts `*/boot.dol` from the device it booted from (util.c
autoboot_dols; before r1730 any name ending in `/boot.dol`), when the device
has FEAT_AUTOLOAD_DOL, before it draws anything; no setting turns it off.
Indigo's DOL carries no Swiss commit trailer, so it qualifies. Console test
2026-10-06 on Spencer's GC Loader: stock Swiss r2119 as `boot.iso` (15 pieces)
and the v2.3.1-rc.2 `ipl.dol` as `/boot.dol` started straight into Indigo with
no Swiss screen. The guide gains "GC Loader, at power-on" as its default for
GC Loader; "from Swiss" stays. No Indigo `boot.iso` is shipped: GC Loader can't
boot one in more than 40 pieces, and new copies on macOS 27 often are.

## 2026-10-12 — Carry released guide clarifications into beta

Backport the reviewed wording for app images, reserved cheat space, folder
depth and supported Library layouts. Keep the beta’s product code, media and
release process unchanged.

## 2026-10-05 — Install in four steps, copying two files

The install guide now asks what starts the GameCube and gives four steps per
setup: GC Loader; PicoBoot or PicoLoader at power-on or from Swiss; FlippyDrive
at power-on or from Swiss; and something else, which works everywhere. Each
setup shows the card afterwards as a text tree, which norvitech.com draws as a
list; the site's two questions show one setup's section, keyed on these
headings (`ROUTES` in its `build_indigo.py`), so renaming a heading needs the
site changed with it.

Step 3 copies `ipl.dol` and `swiss/patches/apploader.img` instead of merging
the whole `swiss` folder. Finder offers Merge only when one folder holds items
the other lacks; with an existing `apploader.img` (every update) it offers only
Stop or Replace, and Replace deletes settings and saves. A sourced fact check
corrected the PicoBoot test (hold D-Pad Down with the card in and a wired
pad: gekkoboot's text screen means the chip reads `ipl.dol`), FlippyDrive's
menu versus boot mode, GC Loader's device name in Swiss, and where Apploader
reads `apploader.img` (copy it to every card used with Swiss).

Swiss creates its `swiss` folder hidden (`ensure_path(..., "swiss", ..., true)`
in files.c), so every backup step says how to show hidden folders, and the
poster and cheat packs are now copied file by file (README, posters.md,
cheats.md, the ZIP readme) for the same Finder Replace reason. The guide has
one whole-card map after the setups (where posters, stills, descriptions,
cheats, apps and folder pictures go), which the site draws under each setup
with that setup's own files merged in.

Documentation and package text only; the console source tree matches v2.3.0.

## 2026-10-06 — Memory Cards and Emulators as faces of the cube

Two faces after Apps, on no side until Settings gives them one: Memory
Cards opens the screen System's row opens (the cube's handover is generic,
from the face in front), and Emulators is the Apps screen over `/emulators`
(apps.c keeps one screen per folder: its heading, empty-folder line and
remembered program). Emulators leaves the ring while the source has no
program there, as Apps does; Home reads `/emulators` only while it is on a
side. Each new face has one icon, a memory card and a game pad, at
`face * UI_HOME_ICON_CHOICES`; the choices between are padding that draws
nothing.

Seven faces moved every per-face literal in the GX harnesses (the stroke
stream, frame budget, render pose) and the motif state's size bound
(512 to 640 bytes, two maps and an opacity per face). The Dolphin route
takes three RIGHTs from Apps to None now, past the two new values.

## 2026-10-06 — Choose the menu on each side of the cube

Setup › Console gains Up Face, Left Face, Right Face and Down Face in place
of Apps Face: each None, Source, Settings, System or Apps, by default the
cube of before. Each side's choices start at its default and go round the
faces (UIHome_SideFace), so a zeroed setting is that cube and main.c keeps
upstream's defaults. Their arms step only their own field, so the picker's
step-to-target commit and its own-field rule hold; a face named twice keeps
its first side. `Hide Apps Face=Yes` in an older file
empties Apps' side unless the file names the sides, and is no longer
written. Y on Home opens Settings from the ring in either cube, and the
hint says so while Settings has no side. Away from Home, an Infinite cube
with other sides shows Library in front of its own ring instead of the
authored four; the default sides keep the authored background.

The Dolphin route's Console offsets moved: Down Face is eleven DOWNs
(RIGHT from Apps is None, LEFT back), Cube twelve. The seeded keys
(`non-default.ini`) leave the sides at their defaults, because the
Classic walk expects the default cube.

## 2026-10-06 — Lay the Home cube's faces out by side

Home's faces now sit on Library's four sides, Up, Left, Right and Down,
through one list instead of their enum numbers. Infinite's ring is Library
followed by each face on a side that is there now, in that order; Classic
turns each side's own direction to it. The default list, Source, Settings,
System and Apps, gives exactly the cubes of before: the existing reducer,
motif and scene suites pass unchanged, except fixtures that set the ring by
hand, which now set the whole layout.

The capabilities carry the sides (zeroed capabilities are the default cube),
the state carries them with the faces on a side that are not there now (Apps
without apps), and the scene publishes both in one word of its snapshot. A
face named twice keeps its first side. With no source, A on Library turns to
Source when it is a turn away and otherwise opens the source picker; Library
alone does not turn. The Classic side table that the reducer and the glyph
placement each kept is now derived from the one list.

Nothing in Settings names other sides yet. New tests: an independent layout
oracle over twelve layouts in both cube styles, glyph placement for custom
layouts, and the scene receiving a change of sides alone.

## 2026-10-05 — Verify Memory Cards identity and save captions in native builds

Integrated the corrected Memory Cards folder controls, removed their Library
placement, and refreshed the guide with actual synthetic-data captures.
Native default and widescreen routes pass 170 and 169 checks, including the
visible Y hint, full path and contained save captions, cube colors, save,
cancel, reset and a second process that restores the saved color. Authored
animated icon textures change on screen; static and absent artwork retain
their separate meanings. RAW images, source saves and exported payloads are
unchanged after the route. No retries or fatal log lines occur.

All four Library layouts pass 299 checks and 11,432 presented frames with
no legacy-browser frames. Y on a Library folder keeps normal navigation;
there is no folder path/color dialog. The native product source is unchanged
by the subsequent host-harness and documentation corrections. The frame
budget retains its existing twenty-scene ceilings.

The format supplies a last-update date, not a separate creation date. Saves
without declared captions receive bounded, validated identity fallbacks;
missing icon data is not replaced by invented game animation. Protected CI
on the final head and release-candidate packaging remain separate gates.

## 2026-10-05 — Put folder identity on Memory Cards mini cubes

Memory Cards now offers Y Folder only for its SD folders and RAW image
cubes. Its native page shows the full device-prefixed path, direct counts
and two contained save names prepared through the bounded metadata helpers.
RAW images stay read-only. A saves a path color, B cancels, Y restores Default
and Up/Down scrolls the complete path. Saved colors mark only folder cube
rims and glyphs, independently of Menu Color, in both screen shapes.

The settings namespace is Memory Card Folder Colors. A failed settings save
keeps the page open and restores the in-memory map; old Library colors are
not imported. Saves with explicitly absent art retain their readable identity,
and checked captions use the shared bounded text normalization.

Validation: folder/cube unit tests, settings fault rollback, production
controller sequences in plain and sanitized builds, and 18 GX renderer
regressions pass. The settings reference names the new key. Native captures
and integrated target/CI checks remain pending.

## 2026-10-05 — Remove folder identity controls from the game Library

Folder paths and identity colors belong to the folders shown as mini cubes
in Memory Cards. Removed the Library's Y Folder page, controller route and
color frames. Library folder navigation and artwork remain intact; games
retain Y Settings. The Library guide now describes navigation only.

The four-layout GX regression confirms that selected game folders offer A
Open without Y Folder or the explicit folder color frame. Memory Cards
folder identity controls and save caption validation are separate changes
to be integrated before the next release candidate.

## 2026-10-05 — Cover the Library controls beneath the folder page

A native capture exposed the bottoms of the Library button glyphs below
the folder page. Its panel now covers the complete underlying control row.
The prepared content and dialog controls retain their positions.

## 2026-10-05 — Keep metadata evidence tied to current captures

The value-row regression uses the four refreshed save-details pictures.
The older themed picture remains useful for general panel detection only.
Clarification of the folder entry below: a readable settings recovery copy
is accepted; unreadable settings or recovery metadata cannot be mistaken
for an absent file when saving folder colors.

## 2026-10-05 — Integrate save explanations, folder controls and installation guides

Save details shows the recorded last-update date and the save icon's status.
Animated, static and absent icons are distinguished, with clear messages for
paused or unavailable previews. No creation date is invented: the save format
has no separate field. Original animation decoding remains unchanged.

Y on a Library folder opens its full path and color controls. A saves, B
cancels and Y resets the color. Path identity includes its device prefix;
settings writes preserve other preferences and restore prior colors on failure.
The native folder route covers save, reopen, cancel and reset in all layouts.

The README, walkthrough and package/release instructions now give exact loader
filenames and card destinations, preservation, folder merging, first launch,
updates and recovery. Focused host and sanitizer regressions pass. Fresh native
save-details captures match the revised panel in both screen shapes; full
integrated protected checks and console acceptance remain required.

# Indigo development journal

## 2026-10-05 — Bound the folder settings sanitizer regression

The Linux GCC contracts lane reached the new folder settings fault test but
stalled before main: its PIE sanitizer executable recursively emitted
AddressSanitizer DEADLYSIGNAL on a host with high ASLR entropy. Match every
other sanitizer harness with Linux non-PIE flags, and bound compilation and
execution so a startup failure cannot consume the whole job timeout. The
production folder save code and UI are unchanged.

## 2026-10-05 — Read native folder color words at their own size

Fresh Dolphin proof showed that Right correctly changed Indigo to Azure,
but the folder route rejected Azure: its 50 bright pixels are below the
Library-title reader's 60-pixel minimum. Use a scoped bounded color-word
reader and stable word comparisons for selection, revisit, cancel and
reset. Other routes retain their existing title thresholds. Native Indigo
and Azure text crops reproduce the old rejection; the new regression also
rejects unchanged labels and a single changed frame between stale samples.

## 2026-10-05 — Give Library folders a path and persistent color

Y on a selected folder opens a retained identity page with the full
device-prefixed path, wrapping and scrolling without ellipses. Left/Right
preview the folder color, Y restores Default, A saves and B cancels. Explicit
color frames remain visible on custom posters in every Library layout and
keep their chosen color when Menu Color changes.

A bounded 32-entry map is saved under Library Folder Colors in global.ini.
Paths escape separators and control bytes; display controls use printable
hex escapes. Saving changes only that key, refuses unreadable settings and
recovery copies, and restores the exact in-memory map on failure. A proven
absent file is created with only this key. The settings fuzzer exercises the
same parser. The guide and settings reference describe controls and limits.

Validation: strict C and ASan/UBSan codec/path tests, production save fault
regressions, real folder navigation, 32 settings parser/writer tests and all
23 GX renderer tests pass. The target DOL compiles and links. The folders
Dolphin route now records save, revisit, cancel and reset for all layouts;
its native captures remain to be recorded by CI on the integrated build.

## 2026-10-12 — Clarify the released user guides

Clarified image filenames and limits, reserved cheat space, folder depth and
Library source layouts. These are wording corrections to the existing released
interface. Product code, screenshots and release tags remain unchanged.

The website can publish the reviewed text as source-pinned documentation errata
for the existing release. Firmware promotion keeps its normal release checks.

## 2026-10-04 — Prepare the validated candidate for v2.3.0

The maintainer tested the final release candidate on a GameCube and approved
promotion to stable. Named the v2.3.0 changelog and corrected the download
README to describe empty game folders with Library Folders enabled.
The accepted release candidate's product source remains unchanged.

The stable release is rebuilt from its tagged main commit through the normal
release workflow. Required promotion checks and signed package provenance
remain release gates.

## 2026-10-04 — Coordinate the remaining beta compatibility branches

Memory Cards and motion are merged into beta. Metadata filtering and manual
rewind remain pending their own pull requests. The rewind branch incorporates
the pending metadata branch so the ordered merges retain the same combined
implementation. The metadata branch keeps its focused production change.
The shared journal preserves both branches' earlier entries exactly. CI for
the refreshed heads remains pending.

## 2026-10-04 — Keep the reviewed beta compatibility fixes coherent

Included the metadata filtering branch alongside the accepted beta renderer
and manual rewind fix. Both compatibility changes retain their production
regressions, and every earlier journal entry is preserved. This prepares the
same combined implementation for the ordered beta merges.

## 2026-10-04 — Preserve current beta and metadata filtering together

Incorporated the accepted Memory Cards and motion changes from beta into the
metadata filtering branch. The renderer and shared launch/tab harnesses match
the tested combined implementation. Source filtering and its production
regressions are retained, with no new filename policy or product behavior.
Both branches' previous journal entries are preserved below.

The sanitized production visibility regression, shared launch and Gameflow GX
suites, Settings tab assertions and CI tool tests pass.

## 2026-10-04 — Update the shared tab harness for the current save interface

The incorporated motion harness still tried to extract the old Memory Cards
row renderer. The current interface uses save cubes, so the extractor failed
before running any assertions. The harness now matches the tested combined
implementation: Settings tabs retain their real spring and edge checks;
current save controls and geometry remain covered by the production controller
and save-cube GX suites. The product renderer is unchanged.

Beta incorporation preserves both features and all previous journal entries.
The focused shared-tab regression and ownership gate pass. The complete
contract lane and refreshed branch CI remain pending.

## 2026-10-04 — Library folders return directly to Home

The retained Library treats `/games` as its navigation boundary. A on its
parent card and X return Home without scanning the device root; nested A,
X and B still go up one folder and focus the folder just left. Empty and
non-game-only roots stay in Indigo when Library Folders is enabled. B and X
retain the selected game or folder on reopening; the root parent card opens
on the first actual card, as the renderer normally does.

The regression executes the production file filters, directory scan,
classifier, card snapshots, navigation branches and menu handoff. Plain and
sanitized runs pass; replaying the preceding candidate reproduces the legacy
browser publication. Names such as `Racing.v1` and `Nintendo.GC`, dotted
ancestors and full matching PNG names already pass those filters. Leading
dots retain the existing hidden-file policy, and Mac metadata stays filtered.

The SD2SP2 folder route covers all four Library layouts and records presented
frames during parent and Home transitions. Its guard identifies Swiss's
path, device panel and file rows, with single-frame, indexing, registration
and delayed-tail regressions. The existing global scene budgets and pixel
thresholds are preserved.

## 2026-10-04 — Preserve motion and save details together on beta

Incorporated the accepted beta icon and motion changes into Memory Cards.
The renderer retains the save summary inset and its focus positions while
preserving the sliding detail focus. Product rendering and metadata sources
match the independently tested combined implementation. Both branches' prior
journal entries are preserved below.

The shared launch GX regression checks both the new inset and moving focus.
The global frame profiles match the combined renderer, including its opening
transition. Source checks, the upstream ownership gate, actual launch and
Gameflow GX tests and the frame budget checks pass.

## 2026-10-04 — Preserve icon and motion coverage while incorporating beta

Merged the current beta branch into the motion polish branch. The resolved
stroke GX harness retains the icon lift, shadow bounds and GX-state restoration
checks alongside the controller idle-release checks. The shared harness matches
the independently tested combined implementation.

The project journal belongs to Indigo. The upstream source gate now recognizes
its directory, with a regression that still rejects unlisted upstream edits and
a similarly named directory outside the journal namespace.

Validation: source checks, the upstream ownership check and the actual stroke GX
stream suite. Product changes from both accepted branches are retained.

## 2026-10-04 — Save panel hierarchy and native button hints

Save details now promotes the save name, separates block and KiB totals,
aligns source and date rows, and shows native A/B icons for Actions and Back.
The Library Saves inset uses larger left-aligned totals and a brighter update
line. Metadata, action handlers and Library focus positions are unchanged;
the popup owns its bounded metadata copy and prepares text fits before drawing.

The native build and package checks, full local host suite and focused footer
parser regression pass. Native default and widescreen save routes pass 84 and
83 checks without retries; the themed simulated-card route passes 144 checks.
The 55-test emulator suite passes, with two local mtools checks skipped. Its
regressions cover real captures, value-only probes, thin digits, the small
themed heading, missing fields and unsafe A retries. Global text thresholds
and steady-frame requirements remain unchanged. Only affected guide pictures
and native fixtures were refreshed.

Exact final-source CI, SD installation and console
acceptance remain pending. Release promotion still requires the maintainer's go.

## 2026-10-04 — Themed save details detection and popup input guard

The previous final-source CI gate failed only in the themed GC Loader smoke
route. Native captures from its exact binary show the size text correctly;
Jet Black desaturates the muted text below the detector's global threshold.
Only the size phrase now uses its measured threshold. Native regressions
reject erased size text and the browser beneath the popup. Popup detection
requires all four borders before accepting any fields, and A retries stop
after any context change, including a transient change that returns to the
browser. Failed checks retain their native screenshot.

The emulator harness's 52 tests pass, including an independent mutation
check for unsafe retries. Focused default and widescreen virtual-card routes
against the exact previous binary pass 76 and 75 checks, respectively. Product
sources and global frame budgets are unchanged. The themed smoke rerun, new
final-source CI, SD installation and console acceptance remain pending.

## 2026-10-04 — Save details test candidate and native captures

The console build, SD package checks and full host suite pass, including
plain, sanitizer and contract lanes. Actual Dolphin captures show the new
known and unknown date dialogs, simulated slot metadata and guarded actions,
plus Library totals changing from one copy/two blocks to two copies/four
blocks after export. The RAW icon plays two distinct authored texture frames.
The corrected widescreen route passes all 71 checks.

Widescreen detection restores authored coordinates from Dolphin's letterbox
without altering native screenshots. Actual capture regressions reject a
missing update field and distinguish the complete block/KiB phrase. Seeded
settings retention is checked separately from fresh-card Save & Exit writes.
Guide pictures now show the current cube interface and details, with an
actual lossless icon animation. Final source CI, SD installation and the
next console acceptance remain pending; release promotion is not authorized.

## 2026-10-04 — Feature branch validation

The upstream comparison recognizes the development journal as Indigo-owned,
matching the documented per-repository record. Production controller,
metadata and renderer regressions pass on the feature branch. The new
Detail inset measures 136 geometry vertices and five batches in both shapes;
its tests retain the branch's existing focus behavior.

## 2026-10-04 — Save details and Library save statistics

Selecting a save now opens its details before guarded actions, including
when both columns contain read-only images. Details show blocks, KiB, source
and the save's recorded update date. Creation dates are explicitly not
recorded; SD container dates are never substituted. Physical metadata must
match the selected slot, game, maker, filename and size. Calendar formatting
works beyond 2038 without relying on the target's time_t width.

Game Detail takes a read-only snapshot for the verified six-byte disc ID and
shows save copies, blocks and update date above Settings and Cheats. Scans
examine at most 256 folder entries and 16 RAW images; omitted subfolders,
unreadable sources and truncated scans are marked partial. Draw functions
perform no storage reads, and existing focus and launch actions are retained.

Focused production controller, collector and metadata regressions pass
plain and sanitized builds. Tests stream fragmented RAW saves through the
real adapter and decoder, including distinct animated frames. Detail GX
budgets separately measure panel geometry and medium text in both screen
shapes. Dolphin routes cover the details flow, read-only images and Library
totals before and after a verified export. Console acceptance remains the
next step before release promotion.


## 2026-10-04 — RAW adapter Linux sanitizer startup

The standalone RAW adapter harness now uses the Linux-only non-PIE sanitizer
flags already required by the main host suite. A separate minimal startup
probe reproduced high-entropy ASLR failures before main without Indigo code.
Sanitizer instrumentation and GameCube product build flags are unchanged.

## 2026-10-04 — Virtual card emulator coverage and guide captures

A dedicated CI route boots the SD package with both physical slots empty,
browses a public synthetic RAW image and exports a fragmented save into the
other SD column. FAT readback verifies the full save and unchanged image.
The named L/R menus preserve the selected source; both physical slot routes
and invalid image fallback have production-function regressions.

The no-card workflow passed 22 emulator preview checks. Updated SD, virtual
card and storage chooser guide pictures come from that actual Dolphin build;
older physical-card pictures still show the previous header controls. The
combined final console test is awaiting its frozen CI artifact.

## 2026-10-04 — SD virtual cards and storage selection

Console testing succeeded overall but found an empty SD save view and hard
to discover L/R switching. The explorer now recognizes Swiss RAW images,
validates bounded metadata and streams save blocks for artwork and verified
GCI exports. Images have no write, move, erase or format path. Physical cards
remain available, while absent slots default to independent SD columns.
Permanent L/R storage buttons open named choices instead of cycling blindly.

Regression coverage includes metadata redundancy and malformed chains,
read-only controller routing, independent SD destinations, storage defaults
and a dedicated RAW fuzzer. The coordinated test build also smooths cube
edges and refreshes the affected emulator route and guide.

## 2026-10-04 — Memory Cards cube outlines

Console feedback found jagged outlines around the small save cubes. Their
renderer now adds one native pixel of outward coverage only on silhouette
edges, with shared corners and the same fade as each face. Original fill
quads, save icon texels and banner drawing are unchanged. Both screen shapes,
small and turning cubes, partial fades, GX vertex counts and unchanged icon
geometry have host regressions. The Memory Cards guide pictures need a new
capture from the next combined test build.
## 2026-10-04 — Preserve hidden-file filtering with beta icon and motion updates

Incorporated the accepted beta icon and motion updates and preserved both
branches' development history. The Library sidecar and hidden-file filtering remains unchanged.
Source and ownership checks pass; CI will validate the combined branch.

## 2026-10-04 — Project journal ownership in the source gate

The upstream audit now recognizes the project-owned AGENTS directory,
including its development journal. This fixes the source gate rejecting the
journal added with this branch's sanitizer regression work. The product
sources and existing behavior are unchanged.

The real upstream-checker regression accepts the project journal while still
rejecting unlisted upstream code and similarly named directories. The
upstream audit and source checks pass.


## 2026-10-04 — Stabilize Linux folder-visibility sanitizer startup

The standalone folder-visibility regression harness now adds `-fno-pie`
and `-no-pie` to sanitizer builds on Linux, matching the host-test Makefile.
This avoids Clang sanitizer failures before `main` on affected Linux hosts
while retaining the address and undefined-behavior checks.

Validation: the plain and sanitized harness passes on macOS; the sanitized
harness passes with GCC and Clang in the pinned Linux build image.
## 2026-10-04 — Preserve accepted beta and manual rewind safeguards

Incorporated the accepted Memory Cards and motion changes from beta while
retaining the bounded manual rewind fix. Shared renderer and tab harnesses
match the tested combined implementation. All earlier journal entries from
both branches are preserved below.

## 2026-10-04 — Preserve seek bounds with beta icon and motion updates

Incorporated the accepted beta icon and motion updates and preserved both
branches' development history. Manual MP3 seek bounds remain unchanged.
Source and ownership checks pass; CI will validate the combined branch.

## 2026-10-04 — Stabilize Linux MP3 sanitizer startup

The standalone MP3 regression harness now adds `-fno-pie` and `-no-pie`
to sanitizer builds on Linux, matching the host-test Makefile. This avoids
Clang sanitizer failures before `main` on affected Linux hosts while
retaining the address and undefined-behavior checks.

Validation: the plain and sanitized harness passes on macOS; the sanitized
harness passes with GCC and Clang in the pinned Linux build image.
## 2026-10-05 — Normalize save captions and missing-title identities

Bounded save text now collapses control bytes and whitespace, preserves the
IPL font's Windows-1252 accents and symbols, and converts their common UTF-8
forms. Missing comments use conservative game-and-maker identity captions
for the supported fallback families, then a bounded readable directory name.
Valid comments retain precedence. This changes display text only; icon frames
and the recorded update date remain governed by their existing metadata.

Strict host and ASan/UBSan regressions cover absent comments, exact identity,
wrong makers, invalid entries, unterminated names, encoding and short buffers.
Controller integration and native screen verification remain separate gates.
