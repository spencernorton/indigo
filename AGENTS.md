# AGENTS.md

Guidance for AI coding agents (and a quick orientation for people) working in
this repository. [CONTRIBUTING.md](CONTRIBUTING.md) is the contract; this file
is the working detail behind it.

## What this is

Indigo is a fork of [Swiss](https://github.com/emukidid/swiss-gc) for the
Nintendo GameCube with the interface rebuilt. The fork changes what you look
at and nothing underneath: device handlers, the patch engine and the loader
are upstream's, and a change that reaches them is out of scope unless a
maintainer asked for it. [`UPSTREAM`](UPSTREAM) names the upstream commit
Indigo is built on and lists the upstream files Indigo does change, with why.

- The interface: `cube/swiss/source/gui/` (Home cube, Library, Game Detail,
  cheats, Settings, Apps), wired in through `swiss.c`, `main.c` and
  `config/`. Apps (`apps.c`) draws with the Library's renderer: a card
  flagged `UI_GAMEFLOW_CARD_APP` takes its poster from Apps, not the pack:
  the app's own picture, or its name drawn in the IPL font (`ui_png.c`).
- Host tests: `buildtools/ui/tests/` (C unit tests, GX vertex-stream checks
  and source audits; no console needed).
- User documentation: `README.md`, `docs/guide/` (every screen and setting,
  with pictures), `docs/SETTINGS.md` (the settings files), `CHANGELOG.md`.

## Build and test

```bash
# The DOL, in the pinned image CI uses (writes cube/swiss/swiss.dol):
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2 make dev

# The performance build, after make dev: frame times and the GPU's counters
# (vertices, pixels, texels, copy clocks) over the menus, to measure a change
# on a console (writes cube/swiss/swiss-perf.dol; CI keeps one as dol-perf-*):
#   make -C cube/swiss BUILD=build-perf TARGET=swiss-perf UI_PERF=1

# The SD card zip for that build (after make dev):
buildtools/sd_package.sh dev cube/swiss/swiss.dol .

# Host tests: lanes plain, sanitized, contracts, or all. Needs Python 3 with
# Pillow and NumPy, a C compiler and zlib; the poster tests also need
# gxtexconv (in the image above), and the contracts lane builds the fuzzers
# when clang has libFuzzer. CI runs every lane on that toolchain, and the
# sanitized lane twice: CC=gcc and CC=clang. A new C test is one name in
# TESTS and its sources in buildtools/ui/tests/Makefile; both lanes run it.
buildtools/ui/tests/run_tests.sh all

# Source checks CI runs on a pull request into beta:
buildtools/check_whitespace.sh origin/beta
python3 buildtools/ci/check_upstream.py   # fetches the upstream commit UPSTREAM names
buildtools/ci/source_checks.sh   # shell and Python syntax, CI tool tests, workflow policy

# Fuzz the files Indigo reads from a card (poster packs, play history, saves,
# settings files, disc images' file tables) for 30 s each; needs clang with
# its fuzzer runtime:
buildtools/ui/tests/fuzz/run_fuzz.sh 30
```

CI (`.github/workflows/ci.yml`) also checks the DOL (`buildtools/ci/verify_dol.py`:
structure, size budget, the commit it names) and the zip's exact layout
(`buildtools/ci/check_package.py`), builds a second time in a fresh
container to prove the build is reproducible, and boots the DOL in Dolphin
and walks its menus with a controller (`buildtools/ui/emulator/`: every
face, the Library, no crash). "CI passed" sums every job up.
Every job runs on self-hosted runners, one throwaway container per job:
[buildtools/ci/runner/README.md](buildtools/ci/runner/README.md).

Run tests from a git clone with tags: some checks compare today's renderer
with a tagged release.

The production DOL's size tripwire remains 4.25 MiB. CI verifies a performance
DOL with `verify_dol.py --performance-baseline <normal.dol>`: that normal
artifact must pass its original size, structure and revision checks, and
diagnostic instrumentation may add at most 4 KiB. Its provenance records the
normal artifact's hash and the measured overhead.

## Branches, versions and releases

- `beta` is where work lands. Branch from `beta`, open a pull request into
  `beta`, and squash-merge it once CI is green. Never push to `beta` or
  `main` directly; both are protected.
- `main` holds releases only. A release is a pull request from `beta` into
  `main`, merged with a merge commit, then tagged. Promotion to `main` needs
  the maintainer's explicit go.
- Tags: `vX.Y.Z-beta.N` or `vX.Y.Z-rc.N` on `beta` publishes a pre-release;
  `vX.Y.Z` on `main` publishes a release. `.github/workflows/release.yml`
  builds the zip from the tag, attaches it with its SHA-256 and a build
  provenance attestation, and writes the notes from `CHANGELOG.md`. Tags are
  immutable.
- Versions follow [semantic versioning](https://semver.org/). Every pull
  request adds its user-facing change to `## Unreleased` at the top of
  `CHANGELOG.md`; the release renames that heading.
- The full procedure, including hotfixes, is [docs/RELEASING.md](docs/RELEASING.md).

MemCard PRO GC coverage is informational during publication. Access to a
real card is not a release requirement. Keep `docs/validation/memcard-pro.json`
accurate: pending coverage stays pending, and emulator or ordinary-card
GameCube results do not establish a real MemCard PRO GC pass. The release
workflow uses the helper's `--report` mode to disclose missing or stale
coverage and continue; its default mode still audits claimed qualification
strictly. Never prefill a hardware pass or infer one from release approval.

## Rules

- **Documentation moves with the code.** A change that alters a screen, a
  control or a setting updates its page in `docs/guide/` (and `docs/SETTINGS.md`
  for a settings key) in the same pull request. Pictures are recorded in the
  Dolphin emulator from a real build; say in the pull request which ones are
  stale if you cannot record them. Record only the screens the change alters:
  every picture rewritten stays in the repository's history for good. A
  picture of something moving is a lossless animated WebP.
- **Upstream's files match `UPSTREAM`.** Outside Indigo's own paths (`OWN` in
  `buildtools/ci/check_upstream.py`), every file matches the upstream commit
  `UPSTREAM` names, line endings aside, or is listed there with the reason.
  CI fails an unlisted change, and a listed file that matches upstream again.
  Changing an upstream file lists it in the same pull request. Moving to
  another upstream commit is a pull request of its own:
  `buildtools/upstream_merge.sh <commit>` merges upstream's changes with their
  carriage returns stripped (Indigo's text files end lines with LF, upstream's
  often with CRLF) and updates the `commit` line. Resolve what it lists, refresh
  the two fixtures that copy upstream code for the audits
  (`buildtools/ui/tests/fixtures/main.base.c`, `settings_toggle.base.c`), and
  say in `CHANGELOG.md` what the move brings. The whitespace check covers
  Indigo's own paths only; upstream's lines keep upstream's whitespace.
- **Keep the draw path cheap.** The interface is drawn with the console's GX
  pipeline at a fixed per-frame budget: no per-frame allocation, no blocking
  read in a draw function.
- **Draw for both screen shapes.** Menu Widescreen squeezes every projection
  through `UIStage_Project` (`gui/ui_stage.h`), so the frame shows the 640 x 480
  stage plus a margin at each side. Load a new projection through it, keep
  layouts in 0..640, and reach the screen's edges with `UIStage_Left()` and
  `UIStage_Right()` rather than 0 and 640 (a full-screen page goes through
  `_PagePanel`).
- **CI stays on our runners.** Every job names `[self-hosted, indigo-build]`
  or `[self-hosted, indigo-emulator]`, pins its actions to a full commit SHA,
  and uses no `container:`, `services:` or `pull_request_target`;
  `buildtools/ci/check_workflows.py` fails CI otherwise. A change to a runner
  image (`buildtools/ci/runner/`) applies from the first run after it merges.
- **The emulator test reads the screen.** It finds the face's name under the
  cube, a game's title in the Library and on its details, and Memory Cards'
  info bar, headers and buttons by where they sit (`LABEL_BOX`, `TITLE_BOX`,
  `DETAIL_TITLE_BOX`, `INFO_BOX` and the boxes beside it in
  `buildtools/ui/emulator/run.py`); a change that moves them
  updates those boxes, and a change that adds a screen or a control can add a
  step to the route.
- **A file from the card is untrusted.** Code that reads a poster pack, the
  play history, a save, a settings file, a disc image's file table or an
  app's picture (a PNG) has a fuzzer in `buildtools/ui/tests/fuzz/`; a new
  format gets one too. A crash
  the fuzzer finds is fixed with the input kept in `corpus/<target>/`.
- **Tests with every change.** A bug fix carries a regression test; a feature
  carries the smallest test that fails without it. Pinned hashes in the audits
  (for example `SHOW_ACTIONS_SHA256`) are updated on purpose, never to make a
  red test go green.
- **Public-repository hygiene.** Nothing in a commit, a file or a pull request
  may name a private host, an internal tracker or ticket, a personal path, a
  credential, or a real person other than the maintainer, except a credit
  the maintainer asks for, which goes in `NOTICE` and System › Credits.
  Screenshots come
  from Dolphin with demonstration data. GitHub push protection scans for
  secrets; do not rely on it.
- **Never contact upstream.** Indigo sends the Swiss project nothing: no pull
  requests, issues, comments or forum posts. Indigo problems are Indigo's.
- Commits are signed off (`git commit -s`, the Developer Certificate of
  Origin) under a GitHub noreply address (`<id>+<login>@users.noreply.github.com`),
  never a work or internal one: author, committer and sign-off lines are public.
