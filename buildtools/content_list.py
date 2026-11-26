#!/usr/bin/env python3
"""Write the game content list Settings > Network > Install game content
reads: every poster, cheat and manual file in the given pack zips (or
folders laid out like a card), by SHA-256, size and card path.

The list names only what Indigo reads from a card, in its own places:
swiss/ui's packs and texts, swiss/cheats/<game ID>[_name].txt and
swiss/ui/manuals/<ID4 or ID6>.pak; anything else in a zip (a README, a
build record) is left out. The server serves each file at the folder
--server names plus its card path, over plain HTTP: the console has no
TLS and no DNS, so --server is an IPv4 address and --host the name it
sends. Without them the files sit beside the list on the update feed.

The release workflow signs the list's hash with the release (gui/
ui_content.h reads the list; docs/PACKS.md is the format).

usage: content_list.py [--server IPV4[:PORT]/FOLDER --host NAME]
       --out content.list PACK...
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

# The console's own limits (gui/ui_content.h): a list breaking them is one
# the console would refuse, so it is never written.
LIST_MAX = 512 * 1024
FILES_MAX = 4096
FILE_MAX = 128 * 1024 * 1024
PATH_MAX = 47
PATH = re.compile(
    r"swiss/ui/(posters\.pak|stills\.pak|discs\.pak|descriptions\.txt|features\.txt)"
    r"|swiss/cheats/[A-Z0-9]{6}(_[A-Za-z0-9._-]*)?\.txt"
    r"|swiss/ui/manuals/([A-Z0-9]{4}|[A-Z0-9]{6})\.pak")
SERVER = re.compile(r"(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?::(\d{1,5}))?(/[A-Za-z0-9._~/-]*)?")
HOST = re.compile(r"[a-z0-9.-]{1,63}")


class Refused(Exception):
    """Not a list the console would read."""


def _digest(read, size: int) -> str:
    sha = hashlib.sha256()
    left = size
    while left:
        piece = read(min(left, 1 << 20))
        if not piece:
            raise Refused("a file ended early")
        sha.update(piece)
        left -= len(piece)
    return sha.hexdigest()


def files(pack: Path):
    """(path, size, sha256) for each file a pack holds where a card would."""
    if pack.is_dir():
        for item in sorted(pack.rglob("*")):
            name = item.relative_to(pack).as_posix()
            if item.is_file() and PATH.fullmatch(name):
                size = item.stat().st_size
                with item.open("rb") as handle:
                    yield name, size, _digest(handle.read, size)
        return
    with zipfile.ZipFile(pack) as archive:
        for info in archive.infolist():
            if not info.is_dir() and PATH.fullmatch(info.filename):
                with archive.open(info) as handle:
                    yield info.filename, info.file_size, _digest(handle.read, info.file_size)


def build(packs: list[Path], server: str = "", host: str = "") -> bytes:
    if bool(server) != bool(host):
        raise Refused("--server and --host travel together")
    if server:
        match = SERVER.fullmatch(server)
        # The console's Update Server grammar (UIUpdate_ParseBase).
        if (not match or any(int(octet) > 255 for octet in match.groups()[:4]) or
                (match[5] and not 0 < int(match[5]) < 65536) or
                len(match[6] or "/") > 62 or "/.." in (match[6] or "")):
            raise Refused(f"--server is not IPV4[:PORT][/FOLDER]: {server}")
    if host and not HOST.fullmatch(host):
        raise Refused(f"--host is not a lower-case name: {host}")
    entries: dict[str, tuple[int, str]] = {}
    for pack in packs:
        for name, size, digest in files(pack):
            if name in entries:
                raise Refused(f"{name} is in two packs")
            if not 0 < size <= FILE_MAX or len(name) > PATH_MAX:
                raise Refused(f"{name}: {size} bytes, {len(name)} letters")
            entries[name] = (size, digest)
    if not 0 < len(entries) <= FILES_MAX:
        raise Refused(f"{len(entries)} files; the console takes 1..{FILES_MAX}")
    lines = ["# Indigo game content"]
    if server:
        lines += [f"Server={server}", f"Host={host}"]
    # Byte order, as the console's strcmp checks it.
    for name in sorted(entries, key=lambda n: n.encode()):
        size, digest = entries[name]
        lines.append(f"{digest} {size} {name}")
    text = ("\n".join(lines) + "\n").encode()
    check(text)
    return text


LINE = re.compile(rb"([0-9a-f]{64}) ([1-9][0-9]{0,9}) (.+)")


def check(text: bytes) -> None:
    """Refuses a list the console would refuse (gui/ui_content.c's rules)."""
    if not 0 < len(text) <= LIST_MAX or b"\0" in text:
        raise Refused(f"the list is {len(text)} bytes; the console reads 1..{LIST_MAX}")
    previous = b""
    seen = set()
    count = 0
    for line in text.split(b"\n"):
        line = line.removesuffix(b"\r")
        if not line or line.startswith(b"#"):
            continue
        key = line.split(b"=", 1)[0]
        if key in (b"Server", b"Host") and b"=" in line:
            if key in seen:
                raise Refused(f"{key.decode()} twice")
            seen.add(key)
            continue
        match = LINE.fullmatch(line)
        if (not match or int(match[2]) > FILE_MAX or len(match[3]) > PATH_MAX or
                not PATH.fullmatch(match[3].decode("ascii", "replace")) or match[3] <= previous):
            raise Refused(f"not a list line: {line[:80]!r}")
        previous = match[3]
        count += 1
    if len(seen) == 1 or not 0 < count <= FILES_MAX:
        raise Refused("Server and Host travel together, and a list names 1..4096 files")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("packs", nargs="+", type=Path,
                        help="pack zips, or folders laid out like a card")
    parser.add_argument("--server", default="", help="IPV4[:PORT]/FOLDER the files are under")
    parser.add_argument("--host", default="", help="the Host name that server answers to")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        text = build(args.packs, args.server, args.host)
    except (Refused, OSError, zipfile.BadZipFile) as error:
        print(f"content_list: {error}", file=sys.stderr)
        return 1
    args.out.write_bytes(text)
    count = sum(1 for line in text.splitlines() if line[:1] not in (b"#", b"S", b"H"))
    print(f"{args.out}: {count} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
