# Releasing Indigo

Two channels, one direction: changes land on `beta`, betas and release
candidates are tested, and the last candidate's code is promoted to `main`.
Tags are immutable; a mistake is fixed by the next version, never by moving a
tag.

| Channel | Branch | Tag | GitHub Release |
| --- | --- | --- | --- |
| Beta | `beta` | `vX.Y.Z-beta.N` | Pre-release |
| Release candidate | `beta` | `vX.Y.Z-rc.N` | Pre-release |
| Stable | `main` | `vX.Y.Z` | Latest |

Pushing a tag runs `.github/workflows/release.yml`. It checks that the tag
sits on its branch, builds the DOL from the tagged commit in the pinned
image, checks that the DOL names that commit, packages the drag-and-drop zip
(`buildtools/sd_package.sh`), writes the on-console updater's files from
that zip (`buildtools/update_manifest.py`: `indigo-<tag>.dol`,
`apploader-<tag>.img` and the channel's `stable.txt` or `rc.txt`), and
publishes the release with the zip, `SHA256SUMS.txt`, the provenance
record, those update files, a build provenance attestation and notes from
`CHANGELOG.md` (`buildtools/release_notes.sh`). To check the pipeline without
publishing, run the workflow by hand with an existing tag, and read the
dry run’s manifest before tagging.

The tag-only `indigo-release-signing` environment supplies the dedicated
Ed25519 key. The console’s pinned public key is in `update_trust.h`.
The publish job checks the key, signs the exact manifest bytes and attaches the
result as `stable.signed` or `rc.signed`. After the release exists, the feed
job commits the channel and versioned payloads together on `gh-pages`, asks
Pages to build and checks that the live manifest matches. A retry of the same
tag must match the assets already published. An older version or timestamp
cannot move the feed backwards. The feed is served at
`indigo-updates.norvitech.com/updates/`.

When `buildtools/content/content.list` is committed, each release also signs
it for Settings › Network › Install game content (`content.txt`,
`content.signed` and `content-<tag>.list`, beside the channel’s files). To
publish new packs, put their files on the content server under a new dated
folder, laid out as on a card, then commit the list for them by pull request:

```bash
python3 buildtools/content_list.py --server <IPv4>/console/<YYYY-MM-DD> \
  --host <server name> --out buildtools/content/content.list \
  indigo-posters-all.zip indigo-cheats.zip indigo-manuals-en.zip
```

The next release carries it. Keep each dated folder until no published
release names it. The server must answer plain HTTP on that address with a
Content-Length and no redirect: the console has no TLS and no DNS.

The packs' server is `indigo.norvitech.com`, behind Cloudflare, which answers
plain HTTP under `/console/` only. `--server` is one of the addresses the name
resolves to (`dig +short indigo.norvitech.com`). If Cloudflare moves the name
to other addresses, build the list again for the next release.

## A beta

1. `beta` is green in CI and `CHANGELOG.md` has the changes under
   `## Unreleased`.
2. Tag the tip of `beta` and push the tag:
   ```bash
   git fetch origin
   git tag v1.26.0-beta.1 origin/beta
   git push origin v1.26.0-beta.1
   ```
3. The pre-release appears with the `## Unreleased` notes. Later betas of
   the same version count up: `-beta.2`, `-beta.3`.

## A release candidate

When a version is complete and has held up in the betas before it (they
need not carry its version number), tag the tip of `beta` `vX.Y.Z-rc.1` the
same way. It is a pre-release like a beta, with the same notes, opening with
"Release candidate" instead of "Beta". A fix found in it lands on `beta` as
usual and ships as `-rc.2`. The release's code is the last candidate's,
unchanged: after that candidate only documentation may change on `beta`, the
release notes and the zip's README included (step 1 of a release). A change
to the code needs another candidate.

## A release

MemCard PRO test coverage is reported during the release build. A real-card
run is useful evidence when available, but access to that hardware is not a
publication requirement. `docs/validation/memcard-pro.json` keeps the actual
tested revision, firmware/setup and results. Keep a pending record pending;
emulator or GCMCE tests do not become a MemCard PRO GC pass.

`buildtools/ci/check_memcard_qualification.py --fingerprint` binds the card-slot
code, launch around a switch and pinned SDK. Its default mode audits a claimed
qualification strictly. The release workflow uses `--report`, which records
missing, pending or stale coverage in its summary and continues publication.
Release notes disclose the current coverage gap. A new real-card result can
update the record later, without requiring hardware the maintainer does not
own for every release.

1. On `beta`, by pull request: rename `## Unreleased` to
   `## vX.Y.Z — <what this release is about>`, and make sure the README and
   the guide describe what ships.
2. Open a pull request from `beta` into `main` titled `Release vX.Y.Z`. CI runs
   on it; merge it with **Create a merge commit** once the maintainer says go.
3. Tag the merge commit and push the tag:
   ```bash
   git fetch origin
   git tag vX.Y.Z origin/main
   git push origin vX.Y.Z
   ```
4. The release appears as Latest, titled from its changelog heading.
5. Refresh [norvitech.com/indigo](https://norvitech.com/indigo/): its guide,
   pictures and download button are regenerated from the new tag.

## A fix to a release

A fix that can't wait for the next beta: branch from `main`, open a pull
request into `main`, merge it, tag `vX.Y.(Z+1)` on the result, then open a
pull request from `main` into `beta` so the fix is in the next beta too.

## Versions

[Semantic versioning](https://semver.org/): a patch release fixes, a minor
release adds, a major release changes something a user relied on (a settings
key, a folder on the card). The version is the tag; nothing in the tree needs
bumping, and System › System Information shows the commit a build came from.
