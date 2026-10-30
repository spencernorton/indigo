# The emulator test

CI boots every build in [Dolphin](https://dolphin-emu.org/) and walks its
menus with a controller, the way someone would on a console, then launches a
game and an app all the way to a program that reports what the launch handed
it. The host tests prove the interface's parts; this proves the DOL they end
up in starts, draws, answers the controller and hands over cleanly.

| File | What it is |
| --- | --- |
| [`run.py`](run.py) | The test: starts Dolphin, plugs a controller in, walks the route, checks every step |
| [`card.py`](card.py) | The demonstration disc it boots with: fictitious games with banners, two damaged images, posters, and apps with pictures |
| [`dsu_pad.py`](dsu_pad.py) | The controller: a pad served to Dolphin over its DSU protocol |
| [`probe/`](probe/probe.c) | The program the launches reach: it reports what the hand-off left it |
| [`aesnd/`](aesnd/aesnd.c), [`aesnd_test.py`](aesnd_test.py) | AESND stopped hundreds of times, as a launch stops the menu's audio: see [AESND](#aesnd) |
| [`test_emulator.py`](test_emulator.py) | Tests for those three, without Dolphin |

`card.py` also makes the SD card (`build_card`): see [An SD card](#an-sd-card).

## What it checks

Dolphin runs headless, drawing with software OpenGL on a virtual X server,
and emulates the MMU, so an invalid memory access stops Indigo on its
exception screen as it would on a console. The route:

1. **Boot.** Home appears, with a face's name under the cube.
2. **Turn.** RIGHT five times: each turn shows another name, the fifth
   comes back to the first, and the five names differ (the disc has apps, so
   Apps is a fifth face). LEFT turns back.
3. **Every face.** A opens it and B comes back to the same face. In the
   Library, RIGHT twice and LEFT twice move between games and back, and A
   opens a game's details. There UP moves to Settings and A opens them, B
   comes back to the details, and B again to the same game. A launch of that
   game then fails (a stand-in game has no program of its own, so Swiss falls
   back to the IPL's, and Dolphin has no IPL ROM): Indigo
   must say so and come back to the Library once A dismisses it. On the Source
   face, A on Change Source opens the device picker, RIGHT shows another
   device and B leaves the picker. On the System face, Memory Cards opens
   with a memory card in each slot: see [Memory Cards](#memory-cards). Then
   DOWN and A on File Browser open the File Browser's two panes, the left one
   focused, though the disc has games: RIGHT focuses the right pane, DOWN and
   A there open a folder and X comes back to the same listing, and LEFT
   focuses the left pane again; A on /games, where the left pane opens, reads
   it into that pane and X comes back up; Z opens Swiss's box and B closes
   it; B comes back to System's rows. On the
   Settings face, Setup › Console › Down Face None takes Apps off the cube,
   File Browser puts its own face after System (A there opens the File
   Browser and B comes back to that face) and Apps puts Apps back; Setup › Console › Cube
   at Classic lays the faces out as the GameCube's menu does, and the route
   walks it (LEFT goes nowhere from Settings, RIGHT twice is Library then
   System, B is Library, UP Source, DOWN Library and DOWN Apps) before setting
   it back to Infinite; Setup › Console › Face Labels Off leaves no name under
   the cube, and On puts the Settings face's name back; and Setup › Library
   › Library Folders On shows the disc's folders in the Library: an empty
   folder opens with only its way back, A opens a folder and a folder in it,
   which also lists the game a level further down, and B goes back up a
   folder at a time to the same card, then Home.
4. **Library again.** From Apps, RIGHT comes round to Library, and A opens
   the Library on a game, not the File Browser: File Browser is over once Home
   is back. B and LEFT return to Apps.
5. **Apps, last.** RIGHT and LEFT move between the disc's apps and back, and
   A on the probe brings up the launch screen and then the probe itself,
   which must report that the menu music stopped before the hand-off, that
   nothing still writes to memory after it, and that the app was started
   with its own path (`dvd:/apps/Probe.dol`), which homebrew uses to find
   its files.
6. **Nothing crashed.** No step lands on the exception screen or a black
   screen, and Dolphin's log reports no exception or invalid access.

The **game route** (`--route game`) boots, opens the Library, moves to the
probe's game and launches it from its details: the probe must see the game's
own disc ID, the 24 MB a game is promised, the music stopped and memory
quiet.

CI runs ten jobs, the first of them the required **Emulator** check:

| Job | Console | Video | Storage |
| --- | --- | --- | --- |
| Emulator (smoke) | PAL, composite | 576i | disc |
| game, PAL, component | PAL, component | 480p | disc |
| smoke, NTSC, component, GC Loader | NTSC, component | 480p | GC Loader, the card with `non-default.ini` |
| game, NTSC, SD2SP2 | NTSC, composite | 480i | SD2SP2, a new card |
| folders, NTSC, component, SD2SP2 | NTSC, component | 480p | SD2SP2, a new card, all five Library layouts |
| virtual cards, NTSC, GC Loader | NTSC, component | 480p | GC Loader, both memory-card slots empty, 4:3 |
| virtual cards, NTSC, GC Loader, widescreen | NTSC, component | 480p | same route with `save-details-wide.ini`, 16:9 |
| save, NTSC, SD2SP2, failing card | NTSC, composite | 480i | SD2SP2, writes failing after 13 |
| game, NTSC, component, GC Loader | NTSC, component | 480p | GC Loader, the game in 40 pieces |
| files, NTSC, GC Loader and SD2SP2 | NTSC, component | 480p | GC Loader, a new card, both memory-card slots empty; a second SD card in SD2SP2, made the Source for Memory Cards, its writes failing on the second boot, which ends launching the probe's game from its Detail in the File Browser |

`--region pal|pal60|ntsc` is the console: the region Dolphin starts the video
hardware in, and an SRAM (`GC/SRAM.raw`) with the same video format, as a
real console's IPL leaves it; Dolphin's own SRAM is an NTSC console's
whatever the region. `--cable component` makes Dolphin report a component
cable, and Swiss's Auto video mode is then 480p. At the end of the smoke
route the probe reports the mode the menu was in, which must be the one the
console and cable ask for. That route's failed launch is of a game from the
console's other region, which switches to that region's video mode first, so
the check catches a menu that doesn't switch back.

Every check compares the screen with itself earlier in the same run (the
text under the cube, a game's title), never with stored pictures, so a
redesign does not break the test and a crash, a hang, a black screen or a
dead control does. Only where that text sits is fixed (`LABEL_BOX`,
`TITLE_BOX` and `DETAIL_TITLE_BOX` in `run.py`): a change that moves it
updates them. Text is the same text a pair of rows at a time: a slow frame
can leave the 480i picture half a line higher. The test waits for what it
expects to see, not for a fixed time, and counts the console's own seconds,
which the runner's Dolphin reports, for every wait and press, so a busy
machine makes it slower, not flaky. A thread whose stack overflows crashes,
as on a console: libogc
guards the lowest doubleword of the running thread's stack with the CPU's data
address breakpoint, which the runner's Dolphin emulates (patch 0007), so the
overrun raises a DSI and libogc's exception screen, and Dolphin logs the hit.
A press the menu was too busy to see, loading something, changes
nothing on the screen: for a turn of the cube or a step along a row, the route
presses again, as a person would, and the report lists each such press
(`pressed_again`), so the misses stay in sight. A press that changed the
screen is never repeated. A failed step says why, names the crash when there
is one, and where the console's CPU was.

## Folder return transitions

The **folders route** (`--route folders`) uses the build's exact SD-card zip
on a fresh SD2SP2 FAT image, an NTSC console and component video. Through
Settings it walks Horizontal, Vertical, Grid and Spotlight. `Nintendo.GC`
is empty; `Racing.v1/Classics.Set/Old.Saves.v2` holds direct and flattened
games. Each dotted ancestor and the multiple dots in the final component
are ordinary folder names. A sibling `Racing.v1.png` becomes the folder's
poster; oversized and damaged sibling pictures remain absent.

Before changing a layout, the route observes each Settings tab title, counts
the focused rows into Setup's Library section, and checks the section title
and return to Setup. A missed tab press can be retried only within the
existing bounded input policy and is recorded in `pressed_again`; a tab
control that never responds fails before any setting is changed.

In each layout A on Return to parent, B and X return to the selected child
folder at both nested levels. At `/games` all three return Home. Reopening
after A on the root parent chooses the first actual folder, as Library
entry normally skips the parent card. Reopening after B or X keeps the
exact selected non-first folder, compared with its own earlier title.

A final steady title alone could miss a brief legacy-browser flash. This
route enables Dolphin's sequential **raw XFB PNG dumps** and classifies
every presented framebuffer in each folder action, from before the press
through its settled result. Two advancing dump indices cover the pending
readback/encoder tail; the next PNG proves the final file has completed.
Every inclusive index must be consumed. The classifier identifies Swiss's
actual device panel and long bordered file rows; Indigo fades and blank
transition frames are allowed. A single legacy frame fails the action.
It uses its own geometry predicate; global text thresholds, waits and
button retry rules are unchanged.

`folder-transitions.json` records each index range, frame count and digest
of the index/RGB sequence. `folder-transitions/` retains representative
first, middle and last images and every rejected legacy frame. Other dump
files are consumed and deleted as they arrive; each span keeps three RGB
buffers and the whole run is limited to 60,000 frames. Actual native
legacy/Library fixtures and one-frame, delayed-tail and missing-index
controls test the recorder without Dolphin.

```bash
python3 buildtools/ui/emulator/run.py cube/swiss/swiss.dol \
  --route folders --region ntsc --cable component --storage sd2sp2 \
  --card-zip Indigo-<version>.zip --out emulator-folders
```

## Running it

In CI it is the **Emulator** jobs. Each summary lists every check and what
the probe reported, and the `emulator-<commit>` artifacts hold a picture of
every step (`sheet.png` has them all), Dolphin's output and `report.json`.

On a machine with Docker, in the emulator runner's image
([`buildtools/ci/runner/emulator.Dockerfile`](../../ci/runner/emulator.Dockerfile)),
after `make dev`:

```bash
make -C buildtools/ui/emulator/probe        # in the toolchain image, as make dev
docker build -t indigo-emulator --build-arg RUNNER_VERSION=2.337.0 \
  --build-arg RUNNER_SHA256=<its linux-x64 checksum> \
  -f buildtools/ci/runner/emulator.Dockerfile buildtools/ci/runner
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work --entrypoint python3 \
  indigo-emulator buildtools/ui/emulator/run.py cube/swiss/swiss.dol \
  --probe buildtools/ui/emulator/probe/probe.dol --route game --region ntsc --out emulator
```

Without `--probe` the smoke route stops at the launch screen, as it did
before the probe.

Or anywhere with Dolphin (`dolphin-emu-nogui`), Xvfb, FFmpeg, genisoimage and
Python 3 with Pillow and NumPy. The posters need gxtexconv; without it the
disc has none and every card shows its banner.

## An SD card

With `--storage sd2sp2 --card-zip Indigo-<version>.zip` there is no disc.
`card.build_card` unpacks the release zip onto an 8 GB FAT32 card image, as
someone sets up a card, and puts the same games, packs and apps beside it.
Indigo boots from the zip's own `ipl.dol`, and Dolphin serves the card as an
SD2SP2 in Serial Port 2: an SD card adapter the emulator runner's Dolphin
adds (`buildtools/ci/runner/dolphin/`), which speaks the SD protocol a byte
at a time, so libogc2's driver, FatFs and Swiss's device code run as they do
on a console. `--storage sdgecko-b` puts the card in Memory Card Slot B
instead.

A new card has no settings, so the route starts in Settings: it must open on
its own, and Save & Exit must write the file and go Home. With `--settings
<name>` the card starts with [`settings/<name>.ini`](settings/) as its
`global.ini` instead and Indigo goes straight Home; at the end every line of
it must still be on the card, through Indigo's own saves. CI's GC Loader smoke
job starts with [`non-default.ini`](settings/non-default.ini): colours, icons,
the clock on the left, no menu music or sounds, reduced motion, In-Game Reset,
no waves, the Sway idle animation and no button hints on Home, which the route
checks as Home appears (with the default settings it checks they show). At the end the
test reads the card back: the settings Indigo saved (with Down Face as the
route left it, and Library Folders saved on with the card's own
`FlattenDir=*/games` kept beside it), and after the game route the launched
game first in the recent list and in Indigo's play history. The card holds
the same folders as the disc, so the Library Folders step runs on FAT too.

A card can fail: `--sd-faults` sets which sectors fail to read or write
(`read-error=FIRST-LAST`, `write-error=FIRST-LAST`) or that every write fails
once some have succeeded (`write-error-after=N`), comma separated. The save
route (`--route save`, with `--settings`) changes a setting on such a card,
then powers off and boots the same card again, with no faults: Indigo must
still start at Home with its settings, in the colours they chose (a broken
settings file boots Home too, on the defaults). The second boot's output is
in `next-boot/`. CI's save job fails writes after 13, the moment of a save
when only `global.ini.new` is whole on the card.

## Memory Cards

The smoke route plugs a memory card into each slot, as Dolphin's GCI folder
cards (`SlotA` and `SlotB` = 8): folders of `.gci` files, one per save, made
afresh for every run by
[`../qa/make_test_saves.py`](../qa/make_test_saves.py). Every game on them is
made up and every icon and banner is drawn by the script: Slot A has 21
saves with every kind of icon a save can have, Slot B 18, so both stacks
scroll. On the System face, DOWN and A open Memory Cards, and the route
reads the screen as it does elsewhere: the focused save's name in the info
bar (`INFO_BOX`), each stack's header, the buttons along the bottom, the
arrow above a stack that has scrolled, and the maroon box an operation ends
with. It checks that:

- the screen opens on Slot A's first save, with both cards' headers;
- RIGHT moves along a row, a different save each time, and from the last
  column on to Slot B: LEFT then comes back to that column's save, not the
  bump of a stack's edge, and on back to the first;
- DOWN past the window's last row scrolls the stack (the arrow above it
  shows), and UP scrolls it back;
- R opens the named storage menu; DOWN and A choose SD, then R, UP and A
  restore Slot B. L, DOWN twice and A choose SD on the left, then L, UP
  twice and A restore Slot A. On the disc, SD has no configuration device:
  it is dimmed, A keeps the card displayed, and B cancels the menu;
- A opens save details, B returns without an action; another A then
  A Actions opens the box beside the save, and B closes it;
- Copy and Yes copy the save to Slot B: the maroon box comes and closes by
  itself, and Slot B's folder gains the save, with the same game, maker and
  name and the same blocks as on Slot A (Dolphin writes a card's folder a
  second after the card's last write);
- A, then A Actions on the copied save dims Move, Slot B having it, and the buttons
  say why;
- Erase, then Yes over the No it starts on, erases the save, and Slot A's
  folder loses it (Dolphin renames it `.gci.deleted`);
- B leaves, back to the System face.

On the disc the SD card's stack has no device, so it shows why; in the GC
Loader job it shows the Save Folder on the card. An SD Gecko in Slot B
(`--storage sdgecko-b`) leaves the cards and this step out.

The **virtual-cards route** (`--route virtual-cards --storage gcloader`, or
`sd2sp2`, with `--card-zip`) leaves both physical slots empty explicitly.
The release zip's SD image also holds `swiss/saves/Demo Card.raw`, a 59-block
memory-card image made by the same public demonstration generator. It has
two fictitious saves, with generated icon/banner art; its first save's
blocks are deliberately separated at blocks 5 and 9. `Backups` beside it
contains a synthetic save with an opaque filename, readable comment and
one static icon texture, plus a synthetic racing profile whose header
advertises neither comments nor art. Its public game ID supplies the
readable title; arbitrary text inside its generated payload is ignored.

Y on the selected folder cube must have a visible contextual hint and open
its full path, contents and color page. The route previews Indigo and Azure,
saves Azure, verifies the cube's native rim pixels and the device-prefixed
`Memory Card Folder Colors` FAT settings entry, reopens the page, cancels a
new preview, and resets to Default. It saves Azure again, restarts Dolphin
with a fresh user directory and the same SD image, verifies the restored
path, contents and color, then resets. Y also opens the RAW image's path and
read-only contents. Browsing these pages preserves every source save and
image byte. Library folders retain their ordinary appearance and navigation;
the four-layout route verifies that Y opens no folder color page there.

Memory Cards must start with both columns on SD. The route opens Library
first: with one save copy, the synthetic game's details leave SAVES out.
After the export there are two copies, and the SAVES inset shows their count
and recorded update (Saves on Details is on by default; a card seeded with
`Hide Saves on Details=Yes` would still leave it out). Renderer host
contracts check the exact values; the route checks that the real fields
appear, or that the inset is absent.

A on each RAW save opens details first: the known `2024-02-29 12:34` date
and an unknown date have different text, their stored icon statuses differ,
and their block counts differ. A save has no separate creation-date field. B cancels without an action. Opening RAW
independently in both columns still allows details and Back; an unusable
Copy stays in its action menu. Before the real Copy, the source RAW is
byte-identical and no exported GCI exists. The first icon's generated
texture contains red and green patches in separate frames; captures must
show both patches on its selected cube, so cube motion or backdrop changes
cannot stand in for icon playback. The same route runs in 4:3 and 16:9. Dolphin letterboxes 16:9 inside its
640×480 capture. Only detection frames normalize the authored centre with
nearest-neighbor sampling; native screenshots remain untouched, and text
thresholds are unchanged.

The details probes measure values separately from their labels and metric
captions. Single-digit metrics use bounded ink (at least 24 bright pixels,
horizontal span 3 and height 8); word fields keep their 60-pixel rule. Native
capture tests distinguish 1 from 2 in both shapes and reject erased values,
captions alone, small noise and the browser beneath the dialog. The opening
guard requires all four borders and the fixed SAVE DETAILS eyebrow, so a
short save name cannot hide an open dialog. Only that small fixed marker
uses a 40-pixel minimum with bounded word span and glyph height; Jet Black
retains 49 pixels at the unchanged threshold 160. Field steadiness remains 0.95.

A opens the RAW image in
the left column; RIGHT and LEFT browse its saves, and the right column keeps
its own SD folder. Copy exports the selected save as a GCI there. The route
reads the actual FAT file back and checks its game, maker, name and every
payload byte in BAT order, then checks the entire RAW image is unchanged.
B closes the image to its containing folder and B again leaves Memory Cards.
The artifact keeps the exported demonstration GCI and pictures of the route.

The **files route** ends its first boot in Memory Cards with settings on the
GC Loader and the SD card in SD2SP2 made the Source (L, DOWN and A in the
File Browser). That card holds the same public image at
`swiss/saves/MemoryCardA.USA.raw`, where Swiss's Emulate Memory Card keeps
Slot A's card for a game started from it. With both slots empty, both
columns start on the GC Loader's Save Folder; L lists Slot A, Slot B and
both cards by name, and DOWN and A put the SD2SP2's Save Folder on the left.
A opens the image, Erase takes its second save off and Move its first to
the GC Loader's Save Folder on the right. Then both cards are read back:
the image reads as a card with no save left (`card_image_saves`), the GC
Loader has the first save whole, and the SD2SP2 has no copy of it.

## A GC Loader

`--storage gcloader` puts the same card in a GC Loader, the drive
replacement that serves games from the files on its SD card. The runner's
Dolphin answers as one (HW2, firmware 1.0.1, writes enabled;
`buildtools/ci/runner/dolphin/0006-gcloader.patch`): Indigo finds it by
asking the drive, reads and writes the card through the drive's commands,
and launches a game by sending the drive the game file's fragments, which it
then serves as the disc, 40 at most. The card has a `boot.iso`, the zip's
`ipl.dol` made into a disc, which the drive serves until a game's fragments
are set, as a GC Loader does at power on; Dolphin starts `ipl.dol` itself.

A game file in many pieces is what a copy onto a card that has seen
deletions can leave, and neither Swiss nor a GC Loader can launch one in more
than 40. `--fragments N` moves the probe's game into N pieces on the card
(`card.fragment`): up to 40 the game route launches it, past that Indigo
must refuse it, say why, and come back to the Library. CI's GC Loader job
launches it in 40.

## AESND

A launch stops the menu's music and sounds, and once froze there until the
console was switched off: libogc2's `AESND_Reset` waited, interrupts off, for
a DSP that takes no mail while an answer of its own is unread. Swiss now
builds AESND from `cube/swiss/aesnd`, with a fix (see its README).
[`aesnd/aesnd.c`](aesnd/aesnd.c) links the `aesndlib.o` that build made and
stops AESND 300 times, each a varying part of an audio period after a voice
started; `aesnd_test.py` fails if its round count stops. libogc2's own
`AESND_Reset` froze within 50 rounds. CI runs it in the PAL game job.

## The probe

[`probe/probe.c`](probe/probe.c) is a small libogc2 program, built with the
toolchain (`make -C buildtools/ui/emulator/probe`) and put on the
demonstration disc twice: as a game image whose boot program it is
(`card.probe_image`), and as `/apps/Probe.dol`. It reads what the hand-off
left before libogc touches anything, in `__SYS_PreInit`:

- the disc ID, memory size, video mode, bus and core clocks, arena and top
  of memory in low memory;
- the video mode the screen was left in (the display configuration and
  the 27/54 MHz clock with the component cable bit);
- whether the audio interface's DMA is still running (the menu music);
- the path it was started with (`argv[0]`);
- and, once up, whether any word of an 8 MB block it filled changes in a
  second and a half: a DMA or a thread the hand-off failed to stop.

Then it turns the screen azure and draws its results as a strip of black and
white blocks, 32 to a row, one row per word, ending in a CRC-32. `run.py`'s
`probe_report` finds the strip in a picture of the screen, whatever its scale,
and refuses a misread through the CRC. It writes each step to the debug UART
too ("probe: started", "probe: video", "probe: reported"), which Dolphin's log
shows. On a console it draws the same screen.

Dolphin reports 16 MB to programs started from Apps where a console reports
24 MB: Swiss reads the memory size from a register Dolphin leaves at zero.
Games are given 24 MB by Swiss itself, so the game route checks it.

## What Dolphin needs

The emulator runner builds Dolphin from source with three patches
(`buildtools/ci/runner/dolphin/`): the SD card adapter, its writes flushed,
and a controller that answers like one. The disc routes' menus run in any
Dolphin; the launches and the SD card need the runner's.

- **A controller answers like one.** A console's controller ignores serial
  commands it doesn't know, and the hardware reports no response. Dolphin
  answered nothing, and the transfer then never ended: libogc's steering
  wheel probe at startup and the GameID packet Swiss sends before every
  launch both hung with a controller connected. The patched Dolphin answers
  no response, so the controller stays connected through a launch. It is
  still plugged in only once Home is up, which works in any Dolphin; that is
  why the controller is a DSU pad, which Dolphin notices arriving.
- **The DSP runs its microcode, in step with the CPU** (`DSPHLE = False`,
  `DSPThread = False`). Dolphin's high-level stand-ins know libogc's audio
  library but not libogc2's (it logs "Unknown ucode (CRC = 8d527c50) -
  forcing AX"), so the menu music's stop before a launch was never answered.
  On a thread of its own the DSP sometimes missed the mail that stop sends
  (`AESND_Reset` waits for it with interrupts off) when the machine was busy.
- **Games need a file table.** The Library reads a game's banner through the
  file table its disc header points to, so each game on the demonstration
  disc has one, with an `opening.bnr`, as a real game does. Two more images
  are damaged on purpose, a header-only dump and a table that counts more
  entries than it holds, because the Library must list both without
  crashing.
