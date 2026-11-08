# Experimental 720 x 540 Indigo menus

This is an opt-in build of the actual Indigo interface, separate from the
standalone video diagnostic. Its first target is **720 x 540 progressive at
about 60 frames per second**, with a 16:9 presentation and a normal-mode
fallback. It is not 1280 x 720 rendering. A scaler's HDMI output resolution
does not establish the source resolution or how much source detail survives.

**Retro GEM setup:** use **System > GCDigital Settings > Use Console DE >
On (16:9)**. On the tested setup, the previous setting gave a 720 x 480 input
readout and clipped the upper-left temperature indicator. Selecting 16:9
corrected the framing and changed GEM's input readout to **720 x 540**. The
cube's proportions were reported correct. Record the previous setting before
changing it, plus the GEM firmware and input/output readouts when testing
another setup. Firmware-specific and other-scaler compatibility remain open.

Pixel FX documents the console-boundary control in its
[compatibility guide](https://wiki.pixelfx.co/index.php/Morph_4K_System_Compatibility#Swiss).
The input readout and visual check support transport compatibility on that
setup; they are not a pixel-exact capture of the HDMI output.

![The experimental Home menu with its session-only 540p footer](images/experimental-540p-home.webp)

Lossless Dolphin demonstration capture, displayed in its 640 x 480 window.
This shows the UI presentation, not Retro GEM's HDMI result.

## Selecting 540p60 and building

`UI_540P` defaults to `1` in `cube/swiss/Makefile`, so standard `make dev` /
release builds (`swiss.dol` / `ipl.dol`) include **NTSC 540p60** directly. When
the console's digital/component cable detection flag is active, open
**Settings › Setup › Display › Swiss Video Mode**, choose **NTSC 540p60**, and
press **A**. Indigo switches to `720 x 540` progressive and shows the standard
10-second confirmation prompt (**A** to keep, **B** or wait 10 seconds to
return).

At any time while in `540p60`, hold **L + R + B** to return immediately to
`480p` and reset the in-memory `Swiss Video Mode` setting to `Auto`. For
automated RAM-loader testing, passing `Experimental540p=Yes` starts directly in
`540p60`, `Experimental540p=Trial` enables the 15-second boot countdown (**L +
R + A** to keep), and `Experimental540p=No` disables boot-argument activation.
While `540p60` is active, the drawing stage uses 16:9 presentation; set your
scaler/TV to 16:9.

A watchdog requests the fallback independently of rendering. If the render
thread cannot process that request within two seconds, it requests a hot
reset to the boot program. A hard GPU/kernel failure can still require the
console's reset button. Before a game or another application starts, the
interface requests its normal menu mode and stops the tiled renderer.

## Rendering

The UI animation, controller sampling and draw functions run once per frame.
Their GX commands are recorded in a bounded 512 KiB display list. That list
is replayed across four rectangles, copied into alternating 720 x 540 YUYV
framebuffers. Each rectangle stays inside a 400 x 274 EFB working area; the
whole image never has to fit in the EFB at once. Horizontal and vertical halos
keep the copied edges away from the working area's edges. Every destination
pixel is written each frame, and each copy destination is 32-byte aligned.

The logical 640 x 480 stage and existing widescreen projection are retained.
This renders geometry and text at the larger source size; it does not resize
a completed 480-line frame. Indexed Jewel Case geometry is emitted into the
outer frame list directly, because nested GX display lists are unsupported.

This first version does **not** use the diagnostic's dirty-region or cached
tile strategies. Animated backgrounds can change most of the image. Reusing
those pixels needs per-buffer history and correct invalidation before it can
be enabled in the interface.

Before `UI540_RecordBegin()` opens the tiled frame display list,
`IndigoBackground_Prepare540p()` runs a full-stage (`640 x 480`) EFB pre-pass
that captures the backdrop wash, globe grid and silk waves into
`s_glassSceneTex`, extracts the two-level bloom pyramid (`s_glassBloomATex` and
`s_glassBloomBTex`), and renders the `4 x 2` supersampled Home face pictures
(`s_homeFaceSlot`) when the cube is settled. During the four-tile `720 x 540`
replay, the cube's front and visible side faces sample `s_glassSceneTex` and
`s_glassBloomATex` through the 3-channel RGB prismatic dispersion and
screen-space refraction shader (including view-ray refraction on angled side
faces and camera-facing smoked plate weighting) without issuing mid-tile EFB
copies.

The `540p` build also enables an 8-bit (`GX_TF_I8`, `512 x 512`) contrast-shaped
font sheet, 1:1 native-phase `GX_TF_RGB565` artwork decoding with flat-gradient
block-seam deblocking and `GX_ANISO_4` mipmap filtering, finer Jewel Case curve
tessellation (`128 x 128` radial glow and `256`-texel sheen), softened silk
wave highlight dispersion, and hardware 6-bit RGB dithering
(`GX_SetDither(GX_ENABLE)`).

## Timing and source provenance

The experimental driver in `buildtools/video_lab/sdk/` adds one timing to the
pinned libogc2 driver. Its source and hashes are recorded in `provenance.json`.
The parameters come from libogc-rice's HD60 enhanced non-interlaced timing
(`video_timing[31]`), translated into this SDK's mode encoding and horizontal
field layout. It uses **800 x 563 total**, 720 x 540 active, about **59.9467 Hz**
at the nominal 27 MHz pixel rate. The normal timing table is unchanged.

Primary sources:

- [libogc2 VIDEO driver](https://github.com/extremscorner/libogc2/blob/84fec1435413e72e31e7d3d61ed817970bc7f6c3/libogc/video.c)
- [libogc-rice timing table](https://github.com/extremscorner/libogc-rice/blob/5f3be28e50ca9ca2d1689a52139fd9ded78b6a5b/libogc/video.c)

## Qualification

`buildtools/video_lab/run_tests.sh` checks complete tile coverage, EFB bounds,
sample phase, destination alignment, timing blanking/rate, and reconstruction
of the original driver after removing the two marked extensions.

Console-lab `GET /pad` includes a `ui540` snapshot in this build: state,
source size, completed frames, mean/max CPU-to-GPU-fence work, count over
16,682 us, mean completed-frame period and peak display-list size. These are
software measurements, not an HDMI analyzer. State 2 means trial, 3 kept,
5 returned and 6 allocation/start failure. Capturing `/xfb` stalls the draw
thread briefly; measure performance in a window without framebuffer reads.

Qualification must separately establish source bytes, real menu operation,
manual/timed fallback, native frame cadence and the picture after the scaler.
Dolphin captures use RAM XFB/EFB copies (`exact_copies=True`), because a fast
texture-only XFB path can hide an incorrect multi-copy assembly. A console
framebuffer capture proves source memory only; a stable TV picture alone
does not prove all 540 rows survive processing.
