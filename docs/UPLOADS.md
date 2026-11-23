# Uploading an executable

Indigo accepts the wiiload version 5 protocol over the network or a USB
Gecko. A transfer contains one zlib stream and optional NUL-terminated
arguments. One transfer runs at a time; the menus continue while it arrives.

The decoded image is at most 16 MiB. Its stream must finish with exactly
the input and output sizes the header advertises, with no trailing bytes.
A network read with no data for ten seconds ends the transfer. The USB
Gecko keeps its existing receive timeout. An incomplete image or arguments
are discarded, leaving the menus running.

Before launching a DOL or ELF, Indigo checks that the header, executable
tables, section/segment payloads and the entrypoint's argument probe fit
inside the received image. Existing raw BIN routes still use their explicit
load addresses. The loader itself stays Swiss's.

`buildtools/ui/tests/test_wiiload.py` runs the real receive and handoff
functions with counted socket and scheduler stand-ins. It covers EOF,
truncated checksums, output mismatch, trailing bytes, one-byte reads, image
bounds, arguments and suspension while the menu would own the video mutex.
`--sanitize` runs the same cases under AddressSanitizer and UBSan. The
`wiiload` target in `buildtools/ui/tests/fuzz/run_fuzz.sh` fuzzes the actual
decoder and image checks, comparing accepted output with `uncompress2`.

## Console lab

`LabServer=Yes`, off by default, opens TCP port 4300 to a test run on the
LAN, so it can watch and drive Indigo on a real console with nobody there.
Pass it as a wiiload argument, which lasts until the next restart, or save
it in `global.ini`.

    curl -D head.txt -o frame.yuyv http://<console>:4300/xfb
    ffmpeg -f rawvideo -pix_fmt yuyv422 -s 640x480 -i frame.yuyv frame.png
    curl http://<console>:4300/log
    curl -X POST -H 'X-Lab: 1' 'http://<console>:4300/pad?press=A&hold=150'
    curl -X POST -H 'X-Lab: 1' http://<console>:4300/reset

- `GET /xfb` is the framebuffer on screen, raw YUYV; `X-Size` in the reply
  gives its size for `ffmpeg -s`.
- `GET /log` is every debug line since Indigo started: the first 16 KiB
  and the latest 48 KiB.
- `GET /pad` says how long since a real controller's button went down, so
  a test run can wait until nobody is playing.
- `POST /pad?press=<buttons>&hold=<ms>` presses buttons on a virtual pad:
  A, B, X, Y, Z, L, R, Start, Up, Down, Left and Right, comma separated,
  held 1 to 5000 ms (100 by default).
- `POST /reset` restarts the console into its boot program. An upload
  under way goes first; if it starts a program, that is the restart.

A POST needs the `X-Lab` header. A browser sends a header of its own only
after a preflight this server never answers, so no web page can press or
restart.

`LogHost=<address>[:port]` (port 4405 by default) sends each debug line over
UDP, starting with the lines from before the network came up. A thread of
its own sends them, so a thread that logs never waits for the network.

`gui/ui_lab.c` reads the requests and settings; `test_ui_lab` and the `lab`
target in `buildtools/ui/tests/fuzz/run_fuzz.sh` cover it.
