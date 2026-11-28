#!/usr/bin/env python3
"""Sign the exact release manifest bytes with the console's pinned Ed25519 key."""
import argparse
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "cube/swiss/source/gui/update_trust.h"


def sign(manifest: Path, key: bytes, public_key: bytes) -> Path:
    public_der = subprocess.check_output(
        ["openssl", "pkey", "-pubout", "-outform", "DER"], input=key)
    if public_der != bytes.fromhex("302a300506032b6570032100") + public_key:
        raise ValueError("release key differs from the one installed on consoles")
    data = manifest.read_bytes()
    if not 0 < len(data) <= 4096:
        raise ValueError("manifest outside the console size limit")
    signature = subprocess.check_output(
        ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", "/dev/stdin", "-in", str(manifest)],
        input=key)
    if len(signature) != 64:
        raise ValueError("invalid Ed25519 signature size")
    result = manifest.with_suffix(".signed")
    result.write_bytes(signature + data)
    return result


def pinned_key() -> bytes:
    value = re.search(r'^#define INDIGO_UPDATE_PUBLIC_KEY "([0-9a-f]{64})"$',
                      HEADER.read_text(), re.M)
    if not value:
        raise ValueError("missing pinned release key")
    return bytes.fromhex(value[1])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, nargs="+",
                        help="the channel's manifest, and content.txt with it")
    args = parser.parse_args()
    key = os.environ.pop("INDIGO_UPDATE_SIGNING_KEY").encode()
    for manifest in args.manifest:
        sign(manifest, key, pinned_key())
