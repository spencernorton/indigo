#!/usr/bin/env python3
"""Advance one channel in a checkout of the static release feed."""
import argparse
import hashlib
import re
import shutil
from pathlib import Path

from update_manifest import channel_for


def version_order(tag: str) -> tuple[int, ...]:
    match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)(?:-(beta|rc)\.(\d+))?", tag)
    if not match:
        raise ValueError("invalid release tag")
    return (*map(int, match.groups()[:3]), {"beta": 0, "rc": 1, None: 2}[match[4]],
            int(match[5] or 0))


def fields(raw: bytes) -> dict[str, str]:
    return dict(line.split("=", 1) for line in raw.decode("ascii").splitlines())


def stage(tag: str, source: Path, destination: Path) -> None:
    channel = channel_for(tag)
    manifest = source / f"{channel}.txt"
    raw = manifest.read_bytes()
    envelope = source / f"{channel}.signed"
    signed = envelope.read_bytes()
    if len(signed) != 64 + len(raw) or signed[64:] != raw:
        raise ValueError("signature envelope and release manifest differ")
    release = fields(raw)
    if release["Channel"] != channel or release["Version"] != tag:
        raise ValueError("release manifest does not match the tag and channel")
    destination.mkdir(exist_ok=True, parents=True)
    current = destination / manifest.name
    if current.exists():
        old = fields(current.read_bytes())
        if old["Version"] == tag and (current.read_bytes() != raw or
                                      (destination / envelope.name).read_bytes() != signed):
            raise ValueError("same tag with different release data")
        if old["Version"] != tag and (version_order(tag) <= version_order(old["Version"]) or
                                      int(release["BuildTime"]) <= int(old["BuildTime"])):
            raise ValueError("channel already contains a newer release")
    for name, key in ((f"indigo-{tag}.dol", ""), (f"apploader-{tag}.img", "Apploader")):
        payload = source / name
        data = payload.read_bytes()
        if (release[key + "File"] != name or int(release[key + "Size"]) != len(data) or
                release[key + "SHA256"] != hashlib.sha256(data).hexdigest()):
            raise ValueError("payload differs from the signed manifest")
        target = destination / name
        if target.exists() and target.read_bytes() != data:
            raise ValueError("refusing to replace a versioned release file")
        shutil.copyfile(payload, target)
    shutil.copyfile(manifest, current)
    shutil.copyfile(envelope, destination / envelope.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    stage(args.tag, args.source, args.destination)
