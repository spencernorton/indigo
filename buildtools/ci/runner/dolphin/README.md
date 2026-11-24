# Dolphin for the emulator test

The emulator runner (`../emulator.Dockerfile`) builds Dolphin from source:
the commit the SD card adapter below was written on (master of 2026-03-18,
release 2603 and 81 commits), plus the patches here, applied in order. Only
the headless front end is built (`dolphin-emu-nogui`).

| Patch | What it does |
| --- | --- |
| [`0001-sd2sp2-adapter.patch`](0001-sd2sp2-adapter.patch) | An SD card adapter on the EXI bus, the emulator core of [dfederspiel/dolphin#1](https://github.com/dfederspiel/dolphin/pull/1) by CodeFly: SD2SP2 in Serial Port 2 (`SerialPort2 = 15`), or an SD Gecko in a memory card slot (`SlotB = 15`), reading and writing the card image `SP2SDCardImage` names. It speaks the SD protocol a byte at a time, SDHC, with CRCs, so libogc2's own driver, FatFs and Swiss's device code run as on a console. |
| [`0002-flush-sd-writes.patch`](0002-flush-sd-writes.patch) | Each write reaches the card image at once, so the test reads back what Indigo wrote once Dolphin stops. |
| [`0003-controller-no-response.patch`](0003-controller-no-response.patch) | A controller answers the serial commands a real one ignores with no response. Dolphin answered nothing, which kept the transfer pending forever: Swiss stalled starting up with a controller connected (libogc's steering wheel probe, `0x30`), and again before a launch (the GameID packet Swiss sends for BlueRetro adapters). |
| [`0004-sd-faults.patch`](0004-sd-faults.patch) | Faults a test asks for in `DOLPHIN_SD_FAULTS`: sectors that fail to read or write, or every write after a number of them, so the test can see what Indigo does with a failing card. A write the card could not make is answered as one, where 0001 answered "accepted" whatever happened. |
| [`0005-emulated-clock.patch`](0005-emulated-clock.patch) | With `DOLPHIN_TICKS` set, the emulated clock and the CPU's PC and LR on stderr ten times a second: the emulator test times its waits in the console's own seconds, so a busy machine slows a run instead of failing it, and a hang says where it is. |
| [`0006-gcloader.patch`](0006-gcloader.patch) | With `DOLPHIN_GCLOADER` naming a card image, the drive is a GC Loader (HW2, firmware 1.0.1, writes enabled): Swiss and libogc2 read and write its SD card through the drive's commands, and it serves up to two discs from fragment tables of at most 40 entries each, as [its API](https://github.com/danielkraak/GC-Loader/blob/master/API.md) describes. Until a table is in use it serves the inserted disc, as a GC Loader serves its `boot.iso`. |
| [`0007-dabr.patch`](0007-dabr.patch) | With `DOLPHIN_DABR` set, the data address breakpoint works as on a 750: libogc points it at the lowest doubleword of the running thread's stack, so a stack that overflows into it raises a DSI and libogc's exception screen comes up, as on a console. Dolphin had ignored it, and the overrun went on corrupting memory. Each hit is logged (`DABR: write to …`). |

All seven are under Dolphin's licence, the GNU General Public License
version 2 or later.

Without them CI could boot Indigo only from a disc: Dolphin has no GameCube
SD adapter of its own, and the card is where every setting, save, poster and
game of a real installation lives.

## Moving to another Dolphin

Change `DOLPHIN_COMMIT` in `emulator.Dockerfile`, check that both patches
still apply (`git apply --check`), and let a pull request's CI run the
emulator test on the new image, bootstrapped as the runner README describes.

Release 2609 takes both patches, but its headless front end built this way
boots nothing ("Could not boot the specified file", for any DOL), so the
image stays on the adapter's own base until that is understood. An SD card
in Memory Card Slot A works but takes minutes to mount in this emulation;
the test uses SD2SP2.

SD writes accept the single-block and multi-block start tokens. A sector-sized
DMA carries all 512 payload bytes, including any leading FC, FD or FE.
The signed updater reads the complete staged DOL back before launching it.
