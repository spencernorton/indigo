# Contributing to Indigo

Thanks for your interest. Indigo is a small project with one maintainer, so
the process is deliberately light — but a few things are fixed.

## How changes land

Development happens here, in the open, on two branches:

- **`beta`** is where changes land. Branch from `beta` (or fork and branch),
  open a pull request into `beta`, and it is squash-merged once CI is green
  and review is done. Betas (`vX.Y.Z-beta.N`) and release candidates
  (`vX.Y.Z-rc.N`) are published from it as pre-releases, so a change reaches
  testers within days.
- **`main`** holds releases only. When a release candidate has held up,
  `beta` is merged into `main` and tagged `vX.Y.Z`. The only other way into
  `main` is an urgent fix to a release: a pull request from a `hotfix/*`
  branch into `main`, after which `main` is merged back into `beta`
  ([docs/RELEASING.md](docs/RELEASING.md#a-fix-to-a-release)).

Every pull request runs CI: the DOL build and the checks on it, a second
build that must match it byte for byte, the emulator test (the build boots in
Dolphin and a controller walks its menus), the host test suite (plain, GCC
and Clang sanitizers, contracts) and the source checks. The build job keeps the
SD card zip as an artifact, so you can try a pull request on a console before
it merges. CI runs on the maintainer's own machines
([buildtools/ci/runner/](buildtools/ci/runner/README.md)), so a first pull
request waits for the maintainer to approve its run. The release procedure is
in [docs/RELEASING.md](docs/RELEASING.md).

## Before you start

- **Bugs** — open a [bug report](https://github.com/spencernorton/indigo/issues/new/choose).
  A report with reproduction steps, versions and a scrubbed log excerpt is
  usually fixed faster than a pull request that arrives without one.
- **Features** — open a feature request first. Indigo has strong opinions
  about staying faithful to the GameCube's own interface language
  (see the README); an idea that cuts across them needs a conversation before
  code.
- **Security** — never in a public issue. Use
  [private vulnerability reporting](https://github.com/spencernorton/indigo/security/advisories/new);
  see [SECURITY.md](SECURITY.md).

## Working on the code

```bash
# Build the DOL in the image CI uses; writes cube/swiss/swiss.dol
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565 make dev
buildtools/sd_package.sh dev cube/swiss/swiss.dol .   # the SD card zip
buildtools/ui/tests/run_tests.sh all                  # host tests
buildtools/check_whitespace.sh origin/beta            # lint; CI enforces it
python3 buildtools/ci/check_upstream.py               # upstream's files match UPSTREAM
buildtools/ci/source_checks.sh                        # scripts, CI tools, workflows
buildtools/ui/tests/fuzz/run_fuzz.sh 30               # fuzz the files read from a card
```

- The interface is drawn with the console's own GX pipeline at a fixed
  budget: a change that adds a per-frame allocation or a blocking read to the
  draw path will be sent back.
- Stay inside the interface. The loader, device handlers and patch engine are
  upstream Swiss's, at the commit [`UPSTREAM`](UPSTREAM) names, and
  `check_upstream.py` fails a change to them that `UPSTREAM` does not list.
  Ask before changing one, then list it there with the reason.
- Keep a change to one concern. A pull request that fixes a bug and
  reformats a file is two pull requests.
- Tests: a bug fix carries a regression test; a feature carries the smallest
  test that fails without it.
- Docs move with the code: a change to a screen or a setting updates its
  page in `docs/guide/`, and every change adds a line under `## Unreleased`
  in `CHANGELOG.md`.
- Commits carry a `Signed-off-by:` line (`git commit -s`, the Developer
  Certificate of Origin). There is no CLA.
- No secrets, hostnames, personal data or screenshots of a real desktop in
  the diff. Pictures come from the Dolphin emulator, only of the screens the
  change alters (a picture rewritten stays in the history for good), and a
  picture of something moving is a lossless animated WebP.

## Out of scope

So nobody wastes an evening on it, Indigo will not accept:

- anything that requires a network service or an account
- piracy-adjacent features: disc dumping conveniences, region-bypass shortcuts for retail media you do not own
- porting the interface to other consoles

## Pull request checklist

The template asks for what changed, why, and how it was tested, plus a
confirmation that the diff carries no secrets, machine names or personal
paths. Fill it in — it is what the reviewer reads first.

## Licence

By contributing you agree that your contribution is licensed under the
[GPL-2.0-or-later](LICENSE) that covers the project.
