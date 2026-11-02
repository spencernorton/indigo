#!/usr/bin/env python3
"""Freeze future offset vectors from compiled IANA 2026f data, independently of Indigo math."""
import argparse
import ctypes as C
import datetime as D
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from zoneinfo import ZoneInfo

SOURCE = "https://data.iana.org/time-zones/releases/tzdata2026f.tar.gz"
SHA256 = "01b4a91cec5167ed48c11e8ab78f2048cead78ce94b0cafcad0ae8c5a3044d3d"
START = int(D.datetime(2026, 10, 31, tzinfo=D.timezone.utc).timestamp())
END = int(D.datetime(2028, 1, 1, tzinfo=D.timezone.utc).timestamp())


class Node(C.Structure):
    pass


class Data(C.Union):
    _fields_ = [("timezone", C.c_char_p), ("children", C.POINTER(Node))]


Node._fields_ = [("name", C.c_char_p), ("data", Data), ("count", C.c_int)]


def names(node, prefix=""):
    for index in range(node.count):
        child = node.data.children[index]
        name = prefix + child.name.decode("ascii")
        if child.count:
            yield from names(child, name)
        elif child.data.timezone:
            yield name


def offset(zone, second):
    return int(D.datetime.fromtimestamp(second, D.timezone.utc).astimezone(zone).utcoffset().total_seconds())


def points(zone):
    result = {START, END - 1, 1800014400, 1815652800}  # January/July 2027, noon UTC
    previous, previous_offset = START, offset(zone, START)
    # Future TZif files can encode recurring changes only in their POSIX tail.
    # Sample each day, then find both sides of every offset change to the second.
    for second in [*range(START + 86400, END, 86400), END - 1]:
        current = offset(zone, second)
        if current != previous_offset:
            low, high = previous, second
            while high - low > 1:
                middle = (low + high) // 2
                if offset(zone, middle) == previous_offset:
                    low = middle
                else:
                    high = middle
            result.update((high - 1, high))
        previous, previous_offset = second, current
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="the published tzdata2026f.tar.gz")
    parser.add_argument("--table-library", type=Path, required=True,
                        help="native shared library compiled from upstream tzdata.c")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("The archive is not the pinned IANA 2026f distribution")
    library = C.CDLL(str(args.table_library.resolve()))
    rows = []
    with tempfile.TemporaryDirectory(prefix="indigo-iana-reference-") as directory:
        work = Path(directory)
        sources = ("africa", "antarctica", "asia", "australasia", "europe",
                   "northamerica", "southamerica", "etcetera", "backward")
        with tarfile.open(args.archive, "r:gz") as archive:
            for name in sources:
                member = archive.getmember(name)
                if not member.isfile():
                    raise SystemExit("Expected a regular IANA source file: " + name)
                (work / name).write_bytes(archive.extractfile(member).read())
        tzdir = work / "zoneinfo"
        tzdir.mkdir()
        subprocess.run([shutil.which("zic") or "zic", "-d", str(tzdir),
                        *[str(work / name) for name in sources]], check=True)
        for name in sorted(names(Node.in_dll(library, "TZData"))):
            with (tzdir / name).open("rb") as stream:
                zone = ZoneInfo.from_file(stream, name)
            samples = points(zone)
            if name == "America/Havana":
                # A canceled transition has no offset change for the daily scan to find.
                samples.update((1793509199, 1793509200, 1825563599, 1825563600))
            rows.extend((name, second, offset(zone, second)) for second in sorted(samples))
    header = (f"# IANA 2026f; {SOURCE}\n# Archive SHA-256 {SHA256}\n"
              "# Horizon 2026-10-31T00:00:00Z through 2028-01-01T00:00:00Z\n"
              "# Location UTC-seconds seconds-east; fixed samples and transition before/at\n")
    args.out.write_text(header + "".join(f"{name}\t{second}\t{east}\n" for name, second, east in rows))
    print(f"{len(rows)} independent IANA reference vectors")


if __name__ == "__main__":
    main()
