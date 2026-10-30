#!/usr/bin/env python3
"""Check a built DOL and record where it came from.

The DOL must be well formed (every section inside the file, the entry point
inside a text section), must name the commit it was built from exactly once
(System > System Information shows it), and must fit the size budget. The
provenance record (--out) names the commit, the toolchain image, the DOL's
SHA-256 and the CI run, so a zip can be traced back to how it was made.

usage: verify_dol.py DOL --revision <sha> --short <short sha> --toolchain <image@sha256:...> [--out JSON]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from pathlib import Path

# A tripwire, not a hardware limit. Size-focused application compilation keeps
# the uncompressed normal and diagnostic DOLs below 4.25 MiB, with headroom.
# Music, supported character encodings and dependency unwind data remain intact.
# Growth past this should be an explicit decision, not a lost build policy.
MAX_DOL_BYTES = 17 * 1024 * 1024 // 4


class Invalid(Exception):
    pass


def sections(data: bytes) -> list[dict[str, int | str]]:
    if len(data) < 0x100:
        raise Invalid(f"shorter than the 0x100-byte DOL header ({len(data)} bytes)")
    offsets = struct.unpack_from(">18I", data, 0x00)
    addresses = struct.unpack_from(">18I", data, 0x48)
    sizes = struct.unpack_from(">18I", data, 0x90)
    found: list[dict[str, int | str]] = []
    for index, (offset, address, size) in enumerate(zip(offsets, addresses, sizes)):
        if size == 0:
            continue
        kind, number = ("text", index) if index < 7 else ("data", index - 7)
        if offset < 0x100 or offset + size > len(data):
            raise Invalid(f"{kind} section {number} lies outside the file (offset {offset:#x}, {size} bytes)")
        if address == 0:
            raise Invalid(f"{kind} section {number} has no load address")
        found.append({"kind": kind, "index": number, "offset": offset, "address": address, "bytes": size})
    if not found:
        raise Invalid("no loadable sections")
    return found


def check(data: bytes, short: str) -> dict[str, object]:
    loaded = sections(data)
    bss_address, bss_size, entry = struct.unpack_from(">3I", data, 0xD8)
    if not any(s["kind"] == "text" and s["address"] <= entry < s["address"] + s["bytes"] for s in loaded):
        raise Invalid(f"entry point {entry:#010x} is outside every text section")
    if bss_size and not bss_address:
        raise Invalid("BSS has a size but no address")
    if len(data) > MAX_DOL_BYTES:
        raise Invalid(f"{len(data)} bytes is over the {MAX_DOL_BYTES}-byte budget (verify_dol.py)")
    if not re.fullmatch(r"[0-9a-f]{7,40}", short):
        raise Invalid(f"not a short commit hash: {short!r}")
    needle = b"\0" + short.encode() + b"\0"
    hits = [s["offset"] + i + 1 for s in loaded
            for i in _find_all(data[s["offset"]:s["offset"] + s["bytes"]], needle)]
    if len(hits) != 1:
        raise Invalid(f"the DOL names commit {short} {len(hits)} times inside its sections; it must be exactly once")
    return {"entry_point": f"{entry:#010x}", "sections": len(loaded), "bss_bytes": bss_size,
            "revision_offset": hits[0]}


def _find_all(haystack: bytes, needle: bytes):
    start = haystack.find(needle)
    while start >= 0:
        yield start
        start = haystack.find(needle, start + 1)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dol", type=Path)
    parser.add_argument("--revision", required=True, help="the full commit hash the DOL was built from")
    parser.add_argument("--short", required=True, help="git rev-parse --short of that commit")
    parser.add_argument("--toolchain", required=True, help="the digest-pinned image it was built in")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    try:
        if not args.revision.startswith(args.short):
            raise Invalid(f"{args.short} is not the start of {args.revision}")
        if "@sha256:" not in args.toolchain:
            raise Invalid(f"toolchain is not pinned by digest: {args.toolchain}")
        if args.dol.is_symlink() or not args.dol.is_file():
            raise Invalid(f"not a regular file: {args.dol}")
        data = args.dol.read_bytes()
        structure = check(data, args.short)
    except (Invalid, OSError, struct.error) as error:
        print(f"::error::DOL check failed: {error}", file=sys.stderr)
        return 1
    record = {
        "schema": "indigo.dol-provenance.v1",
        "revision": args.revision,
        "toolchain": args.toolchain,
        "dol": {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), **structure},
        "run": {key.lower(): os.environ.get(key) for key in
                ("GITHUB_REPOSITORY", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_EVENT_NAME", "GITHUB_REF")},
    }
    if args.out:
        args.out.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    print(f"DOL OK: {len(data)} bytes, sha256 {record['dol']['sha256']}, names {args.short}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
