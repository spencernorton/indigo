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
X starts the single GPU experiment and shows its report; Y presents the
qualified GPU coordinate image in a timed P1 trial. CPU fallback and HTTP
service run separately from the GPU job. A GPU fence failure cannot make an
unqualified GPU image available for presentation.

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
Pattern 5 adds a GPU report barcode. Its lower image is a cropped preview
of the GPU coordinate test once that copy has fenced; it is not a full-width
sample-fidelity measurement.

The client retains raw YUYV, metadata/CRC/SHA-256, and an optional PNG when
FFmpeg is available. `/status` includes the actual VI registers, revision,
boot nonce, current pattern/sequence, retrace and fallback/alarm counts.

**An XFB capture proves source memory contents, not what GEM transmitted or
the television displayed.** Qualifying independent HDMI samples requires a
separate capture after GEM, and visual observations must be recorded
separately. Emulator internal resolution is not native hardware evidence.

## Tiled GPU and retained-region experiment

```sh
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS gx-start
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS gx-status
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS gx-capture capture/gx-coordinates
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS gx-scene capture/gx-reference
python3 buildtools/video_lab/client.py CONSOLE_ADDRESS gx-show
```

One job runs per boot. Wait for `state=2` before capturing or presenting its
results. State 3 is a failed test; `error` and the report identify the failing
stage. `gx-show` presents the GPU image for 15 seconds, then returns to CPU
P0. Its returned image is copied into a display buffer; this presentation
step is excluded from GPU benchmark timings.

The coordinate image is rendered through 640- and 80-column EFB passes into
a 720-pixel-stride XFB. The job decodes every copied column and row. GPU
RGB-to-YCbCr quantization permits at most two luma levels of error in the
four code levels; CPU source captures remain exact. Intensity textures use
opaque replacement, since their intensity also supplies texture alpha.

A separate synthetic scene uses twenty 160/80 × 120 tiles and 32 frames:

| Variant | Work |
| --- | --- |
| R0 | Redraw and copy every tile every frame |
| R1 | Skip only tiles proven black in both the previous and current frame |
| R2 | Keep the XFB; redraw tiles touched by old/new dynamic bounds |
| R3 | R2 plus per-tile RGBA8 textures of the static layer |

The scene moves a shadowed cursor across tile joins and panel content,
removes objects, moves/hides/restores a panel, and opens a translucent popup.
Damage includes shadows and previous bounds. A static-scene change forces
full invalidation. Every R1/R2/R3 frame must match the R0 XFB byte for byte
(96 comparisons). A final independent CPU rectangle oracle checks R0 luma
within three levels of quantization/blend rounding, and allocation canaries
must remain intact. These grayscale tests do not qualify colored chroma
edges, antialiasing, glass/blur dependencies or real Indigo scenes.

`gx-status` reports XFB copy calls/source pixels, native GPU vertices,
texels, pixels and copy clocks, time through the draw-done fence, separate
cache construction time, and separate CPU comparison time. R3 cache builds
perform twenty additional texture copies each; include `cache_us` with
`work_us` for a total, rather than comparing only its warm drawing cost.
GPU counters describe scene drawing, excluding that separately timed cache
construction. These small synthetic workloads are not menu frame-rate or
input-latency measurements, and unchanged pixels still use ordinary raster
transport through VI/GEM.

The repeatable Dolphin check uses native internal resolution, exact texture
hashing, and explicit EFB/XFB RAM copies with deferred copies and early XFB
output disabled. It reads the program's source-check report and exercises
CPU/GPU presentation and fallback. CI retains its report and screenshots:

```sh
python3 buildtools/video_lab/emulator.py buildtools/video_lab/video-lab.dol --out video-diagnostic
```

## What follows

First qualify source patterns, controller commands, ordinary mode changes
and both fallback mechanisms. Then audit complete custom timings and SDK
compatibility before enabling another case. Real Indigo integration,
representative menu performance and post-GEM fidelity remain later
experiments. This program does not establish a native 720p capability.
