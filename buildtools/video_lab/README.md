# Experimental video transport diagnostic

This standalone program is the first step in investigating higher-detail
Indigo menus. It runs from RAM and has no filesystem or game loader. It does
not change Indigo's menu, boot files, saved settings or GEM firmware.

This version tests **normal NTSC timing only**:

| Case | Source | VI active width | Purpose |
| --- | --- | --- | --- |
| P0 | 640 × 480 | 704 | Reference with Indigo's normal horizontal scaling |
| P1 | 720 × 480 | 720 | Independent source columns at existing 480p timing |

P1 requires the digital/component flag and expires to P0 after 15 seconds.
The program returns to the installed boot program after ten minutes, or on
Start / `POST /exit`. That return reloads the installed program; it is not a
resume of the previous RAM session. Preserve a known-good restoration DOL
and coordinate exclusive console access before uploading anything.

The image/recovery worker does not wait for a vertical interrupt or perform
network I/O. An OS alarm and a monotonic deadline request the fallback. The
HTTP thread only queues commands and copies bounded snapshots; slow clients
cannot hold its image mutex while sending data. This is software recovery,
not a guarantee against a hard hardware or kernel hang. No custom timing is
enabled in this version.

## Build and host checks

From the repository root:

```sh
sh buildtools/video_lab/run_tests.sh
python3 buildtools/video_lab/build.py
```

The build script uses the exact toolchain image pinned in Indigo's CI. It
records source, SDK header/library and DOL hashes in `build.json`, plus the
runtime revision. A dirty checkout is labelled explicitly. No SDK upgrade is
performed. `make -C buildtools/video_lab REVISION=<revision>` also works
inside that image.

## Controls and captures

Use an existing RAM loader after completing emulator checks. The diagnostic
uses DHCP and listens on TCP 4300. It does not contain a wiiload receiver.
Physical controller controls: A = P1 trial, B = P0, Left/Right = pattern,
Start = return to the installed boot program.

```sh
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS status
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS capture capture/p0
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS trial
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS capture capture/p1
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS baseline
```

Mutations require `X-Lab: 1`; the client supplies it after checking that the
endpoint identifies as `video-lab-1`. Use `--expect-boot <boot_nonce>` to
refuse commands after a restart. A `queued` response is not proof that the
change applied: read `/status` until the expected case and `presented=true`
are observed. `/expire` arms a 50 ms OS alarm for a separate fallback test.

Patterns 0–4 are checkerboard, single-column stripes, single-row stripes,
coordinate codes, and a circle/cross for a 16:9 display. Coordinate codes use
four grayscale levels to encode x and y independently across the entire
source grid, with a sequence stamp in the bottom-right corner. The host
decoder checks every source column and row, including P1's additional
columns. The other patterns include an eight-word barcode with source
metadata at the top. All buffers use explicit legal YCbCr422 values.

The client retains raw YUYV, metadata/CRC/SHA-256, and an optional PNG when
FFmpeg is available. `/status` includes the actual VI registers, revision,
boot nonce, current pattern/sequence, retrace and fallback/alarm counts.

**An XFB capture proves source memory contents, not what GEM transmitted or
the television displayed.** Qualifying independent HDMI samples requires a
separate capture after GEM, and visual observations must be recorded
separately. Emulator internal resolution is not native hardware evidence.

## What follows

First qualify source patterns, controller commands, ordinary mode changes
and both fallback mechanisms. Then audit complete custom timings and SDK
compatibility before enabling another case. Tiled GX rendering, retained
regions/static-layer caching, performance comparisons and real Indigo
integration remain later experiments. This program does not establish a
native 720p capability.
