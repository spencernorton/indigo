#!/usr/bin/env python3
"""Write one release's update files: the DOL and apploader image the
on-console updater downloads, and the manifest that names them.

The updater (Settings > Network) reads a small Key=Value manifest —
<channel>.txt beside indigo-<tag>.dol and apploader-<tag>.img — from a
plain-HTTP server or an SMB share. Everything it needs is already in the
release zip: this copies ipl.dol and swiss/patches/apploader.img out of
it under their update names, hashes them, and writes the manifest
(gui/ui_update.h reads it; docs/dev/software-update-design.md is the
format).

With --content, the release also vouches for a game content list
(content_list.py writes one): content-<tag>.list beside a content.txt
manifest of the same shape, Channel=content, naming it by its hash, for
Settings > Network > Install game content.

Manifests come only from published tags: vX.Y.Z writes stable.txt,
vX.Y.Z-beta.N and vX.Y.Z-rc.N write rc.txt (the console's Stable and
Release candidate channels; beta tags feed the candidate channel, as
the workflow's own channel split does). Anything else is refused, so a
branch build can never publish an update by accident.

usage: update_manifest.py <tag> <commit> <build-time> <date> <zip>
       [--out DIR] [--min-version TAG --min-build-time EPOCH]
       [--content LIST]
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

import content_list

REPO = "https://github.com/spencernorton/indigo"
STABLE_TAG = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+$")
CANDIDATE_TAG = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+-(beta|rc)\.[0-9]+$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")
DATE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")

# The console's own limits (gui/ui_update.h): a manifest naming more is
# one the updater would refuse, so it is never written.
DOL_MAX = 16 * 1024 * 1024
APPLOADER_MAX = 8 * 1024 * 1024
MANIFEST_MAX = 4096


class Refused(Exception):
    """Not a release this tool will write update files for."""


def channel_for(tag: str) -> str:
    if STABLE_TAG.match(tag):
        return "stable"
    if CANDIDATE_TAG.match(tag):
        return "rc"
    raise Refused(
        f"not a release tag: {tag} (vX.Y.Z, vX.Y.Z-beta.N or vX.Y.Z-rc.N)")


def member(archive: zipfile.ZipFile, name: str, limit: int) -> bytes:
    try:
        data = archive.read(name)
    except KeyError:
        raise Refused(f"the zip has no {name}") from None
    if not data or len(data) > limit:
        raise Refused(f"{name} is {len(data)} bytes; the updater takes "
                      f"1..{limit}")
    return data


def build(tag: str, commit: str, build_time: int, date: str, zip_path: Path,
          out: Path, min_version: str = "", min_build_time: int = 0,
          repo: str = REPO, content: Path | None = None) -> list[Path]:
    """Write the three update files for one tag, and the two content files
    with --content; returns their paths."""
    channel = channel_for(tag)
    if not COMMIT.match(commit):
        raise Refused(f"not a full commit: {commit}")
    if not DATE.match(date):
        raise Refused(f"not a YYYY-MM-DD date: {date}")
    if build_time < 946684800:  # 2000-01-01, where the console's clock starts
        raise Refused(f"build time {build_time} predates the console")
    if bool(min_version) != bool(min_build_time):
        raise Refused("--min-version and --min-build-time travel together")
    if min_version and not STABLE_TAG.match(min_version):
        raise Refused(f"--min-version is not a release tag: {min_version}")

    with zipfile.ZipFile(zip_path) as archive:
        dol = member(archive, "ipl.dol", DOL_MAX)
        apploader = member(archive, "swiss/patches/apploader.img",
                           APPLOADER_MAX)

    dol_name = f"indigo-{tag}.dol"
    apploader_name = f"apploader-{tag}.img"
    lines = [
        "FormatVersion=1",
        f"Channel={channel}",
        f"Version={tag}",
        f"Commit={commit}",
        f"Date={date}",
        f"BuildTime={build_time}",
        f"File={dol_name}",
        f"Size={len(dol)}",
        f"SHA256={hashlib.sha256(dol).hexdigest()}",
        f"ApploaderFile={apploader_name}",
        f"ApploaderSize={len(apploader)}",
        f"ApploaderSHA256={hashlib.sha256(apploader).hexdigest()}",
    ]
    if min_version:
        lines += [f"MinVersion={min_version}",
                  f"MinBuildTime={min_build_time}"]
    lines.append(f"NotesURL={repo}/releases/tag/{tag}")
    manifest = ("\r\n".join(lines) + "\r\n").encode()
    if len(manifest) > MANIFEST_MAX:
        raise Refused(f"manifest is {len(manifest)} bytes; the updater "
                      f"reads at most {MANIFEST_MAX}")

    files = [(dol_name, dol), (apploader_name, apploader),
             (f"{channel}.txt", manifest)]
    if content is not None:
        listing = content.read_bytes()
        try:
            content_list.check(listing)
        except content_list.Refused as error:
            raise Refused(f"{content}: {error}") from None
        list_name = f"content-{tag}.list"
        files += [(list_name, listing), ("content.txt", ("\r\n".join([
            "FormatVersion=1", "Channel=content", f"Version={tag}",
            f"Commit={commit}", f"Date={date}", f"BuildTime={build_time}",
            f"File={list_name}", f"Size={len(listing)}",
            f"SHA256={hashlib.sha256(listing).hexdigest()}"]) + "\r\n").encode())]

    out.mkdir(parents=True, exist_ok=True)
    written = []
    for name, data in files:
        path = out / name
        path.write_bytes(data)
        written.append(path)
    return written


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Write one release tag's on-console update files.")
    parser.add_argument("tag", help="the release tag, vX.Y.Z[-beta.N|-rc.N]")
    parser.add_argument("commit", help="the tagged commit, 40 hex")
    parser.add_argument("build_time", type=int,
                        help="the tagged commit's committer time, epoch "
                             "seconds (git log -1 --format=%%ct)")
    parser.add_argument("date", help="the release date, YYYY-MM-DD")
    parser.add_argument("zip", type=Path, help="the built Indigo-<tag>.zip")
    parser.add_argument("--out", type=Path, default=Path("dist/update"),
                        help="where the update files go (dist/update)")
    parser.add_argument("--min-version", default="",
                        help="a stepping-stone release consoles must run "
                             "first, with --min-build-time")
    parser.add_argument("--min-build-time", type=int, default=0,
                        help="that release's committer time, epoch seconds")
    parser.add_argument("--content", type=Path,
                        help="a game content list to sign with the release")
    args = parser.parse_args(argv)
    try:
        written = build(args.tag, args.commit, args.build_time, args.date,
                        args.zip, args.out, args.min_version,
                        args.min_build_time, content=args.content)
    except (Refused, OSError, zipfile.BadZipFile) as error:
        print(f"update_manifest: {error}", file=sys.stderr)
        return 1
    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
