# Web browser cube face

The **Web** face opens a GameCube web browser, the way Apps and Emulators
open a program. It ships as a separate DOL with Indigo v2.5.0.

## Shape: launch a bundled DOL (not compiled into ipl.dol)

The browser ships as its own `cubebrowser.dol` inside the Indigo zip; the Web
face launches it through `boot_dol`, exactly like an app in `/apps`. Chosen over
compiling it into `ipl.dol` because:

- It keeps Indigo's DOL under the 6 MiB tripwire. The browser links mbedTLS and
  would add ~1.18 MB; Indigo has ~0.86 MB of headroom. The in-process version is
  a later, separate effort that first needs the tripwire raised or mbedTLS
  trimmed (a console-tested boot-path change).
- The browser keeps its own renderer, audio, network and crash isolation, so a
  browser fault cannot corrupt Indigo.

The browser owns its GX renderer and reads the console's IPL font at runtime
for its chrome. It uses native dark panels and named Indigo menu colours;
page fonts and author styles stay independent. A versioned argv contract
passes display, motion, sound, rumble, active UTC offset and supported
config-card origin without persisting those overrides in browser settings.

## The browser

Upstream: `Arial-Bold/Cubebrowser`. We maintain a hardened fork (the creator gave
permission to integrate). The fork already carries:

- a clean Mozilla CA bundle (the upstream one shipped four TLS-interception
  roots) plus a guard that fails the build if one returns;
- pinned, checksum-verified mbedTLS 3.6.7 and stb_image, replacing unpinned
  downloads;
- fixes for the page-reachable parser memory-safety bugs found by fuzzing
  (CSS value underflow, `:is`/`:not` recursion, a `<title>` overrun);
- a parser fuzz suite (the upstream project had no tests);
- **< Indigo** at top-left of every browsing page: one click
  returns from the browser's main loop so the loader hands control back to
  the installed Indigo. Dialogs retain input ownership;
- the SD card in a GC Loader as well as an SD2SP2 or SD Gecko: with more than
  one card Indigo names its config card. Standalone fallback prefers the card
  whose `gcbrowser/` holds `cacert.pem`;
- rectangles cut to the clip box before drawing, which stopped a grey wedge
  drawn over some pages;
- a console lab, off unless the browser starts with `lab=1`. `launchWebBrowser`
  passes it while Indigo's own lab (`LabServer`) is on, so a lab session that
  opens the Web face keeps watching and driving, and its `/reset` returns to
  Indigo (docs/UPLOADS.md, Console lab).

It reads its files through libogc2's SD drivers: the SD2SP2, an SD Gecko and
the GC Loader. libogc2 has no FlippyDrive device, so on a FlippyDrive-only
console the browser has no certificates (https pages fail) and saves
nothing; the zip's README and the install guide say so.

## Launch behavior

The Web Home face dispatches to `launchWebBrowser`, which mounts the config
device to find `swiss/indigo/cubebrowser.dol` and keeps it mounted through
`boot_dol`. Missing/failed launches close their handles and release the mount.
The bundled browser stays out of Recent, so it cannot become the next automatic
launch. The existing loader stub returns to the Indigo installed on the card.

`gui/ui_browser_launch.c` formats bounded, versioned named arguments. Colour
names avoid incompatible theme enum orders. Indigo supplies the active menu
shape/video, motion, sound, rumble and local UTC offset (including DST), plus a
supported config card. Its clock is already local: use Sync Clock's applied
offset or an explicit POSIX zone, and omit an unknown offset rather than
claiming UTC. The browser retains its own text size, safe margin and
browsing preferences; inherited fields are hidden and are not written over
standalone settings. `lab=1` is added only when Indigo's own lab is enabled.
Host coverage checks the formatter, known/unknown offsets and configured POSIX time zones;
the existing routing audit keeps mount/close/Recent behavior guarded.

## Packaging

`sd_package.sh` bundles the browser when `browser/cubebrowser.dol` (and the
`browser/gcbrowser/` CA folder) are present: the DOL goes to
`swiss/indigo/cubebrowser.dol` (where `launchWebBrowser` looks, on the config
device, which it mounts first: with games on another device the config device
is mounted only while something reads it) and the CA folder to `gcbrowser/` at
the card root. mbed TLS is compiled into the DOL, so its licence goes with it:
the Build jobs copy the pinned release's `LICENSE` to
`browser/LICENSE-mbedtls.txt`, `sd_package.sh` ships it as
`swiss/indigo/LICENSE-mbedtls.txt` and won't bundle the browser without it,
and `check_package.py` fails a zip that has the browser but not the licence.
`NOTICE` names mbed TLS, stb_image and the CA bundle's MPL-2.0 terms. The
paths are overridable with `BROWSER_DOL`, `BROWSER_GCBROWSER` and
`BROWSER_MBEDTLS_LICENSE`. The browser is optional in the zip (`check_package.py`
`OPTIONAL`), so a plain build still packages; a release ships it. The zip
tripwire was raised to 9 MiB for the ~1 MiB the browser adds.

The browser is **used with permission from Arial-Bold**
(https://github.com/Arial-Bold/Cubebrowser); recorded in `NOTICE`. It ships as a
separate DOL (an aggregate), so its licence stays its own and does not touch
Indigo's GPL.

## How CI builds it

The Build jobs in `ci.yml` and `release.yml` clone the hardened fork at a pinned
commit, fetch its checksum-verified dependencies and build it, then copy the DOL,
CA bundle and mbed TLS's licence into `browser/` for `sd_package.sh`:

- fork: https://github.com/spencernorton/cubebrowser (maintained by us, used
  with permission; its own README/NOTICE credit Arial-Bold)
- pinned commit: `f3a7ca2ed92ae601d0f37251e5609fb7c4aeef89` — bump it in both
  workflows' `CUBEBROWSER_REF` to take browser changes.

It builds with the same libogc2 toolchain the runner already carries, and
`get-mbedtls.sh` fetches mbedTLS and stb from GitHub, which the CI egress proxy
allows.

## Qualification

The browser requires deterministic parser/engine sanitizer cases and two
Dolphin routes: NTSC 4:3 and PAL interlaced widescreen on controller port 4.
Each boots the produced DOL, fetches gzip HTML and PNG, drives menus,
Settings and the address keyboard, follows a link and goes Back. The exact
native browser DOL also rendered the public HTTPS start URL with the verified
certificate indicator. This establishes native TLS/page compatibility in the
emulator; real adapters, cables and return to resident Indigo still need
qualification while the console is unavailable. The earlier return route
passed on the previous browser; that result is not a new hardware pass.

The start page's five-digit weather ZIP is the only persisted cookie-like
preference. Verified responses scope it to norvitech.com/indigo/start/; its
first-party PNG receives it directly and the image helper is bypassed. Save,
restart and clear are qualified on the actual DOL/FAT fixture. General login
cookies are not added. See the fork's `docs/start-preference.md` for the site
contract and `docs/launch-context.md` for the launch arguments. Keep the
separate DOL, curated CA bundle, verified dependency pins and opt-in lab.
