#!/usr/bin/env python3
"""Verify PowerPC per-function stack usage emitted by -fstack-usage.

Bare-metal libogc2 threads run on fixed stacks without an MMU guard page. This
check fails closed if .su files are missing, if any function uses a dynamic
(VLA/alloca) frame, or if any static frame exceeds its ceiling:
  - 16 KiB across Indigo's UI, configuration, boot and launch paths
  - 28 KiB across inherited upstream patcher and audio routines
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

INDIGO_CEILING = 16 * 1024
TOTAL_CEILING = 28 * 1024

INDIGO_SOURCE = re.compile(
    r"(?:^|/)(?:gui|config|images)/"
    r"|(?:^|/)(?:swiss|main|input|mp3|wiiload|cheats|cheat_policy)\.c$"
    r"|video_lab/"
)
LOCATION_RE = re.compile(r"^(.*?):(\d+):(\d+):(.*)$")


@dataclass(frozen=True)
class FrameRecord:
    source: str
    line: int
    function: str
    bytes_used: int
    qualifier: str
    su_file: str


def is_indigo_source(source: str) -> bool:
    return bool(INDIGO_SOURCE.search(source.replace("\\", "/")))


def parse_su_text(text: str, su_name: str = "<memory>") -> list[FrameRecord]:
    records: list[FrameRecord] = []
    for line_no, raw in enumerate(text.splitlines(), 1):
        if not raw.strip():
            continue
        parts = raw.split("\t")
        if len(parts) < 3:
            raise ValueError(f"{su_name}:{line_no}: malformed .su record: {raw!r}")
        loc, size_text, qualifier = parts[0], parts[1], parts[2].strip()
        match = LOCATION_RE.match(loc)
        if match:
            src, line_s, _, func = match.groups()
            line_num = int(line_s)
        else:
            src, line_num, func = loc, 0, loc
        records.append(
            FrameRecord(
                source=src,
                line=line_num,
                function=func,
                bytes_used=int(size_text),
                qualifier=qualifier,
                su_file=su_name,
            )
        )
    return records


def collect_su_files(swiss_dir: Path, target: str = "swiss", build_dir: str = "build") -> list[Path]:
    ltrans = sorted(swiss_dir.glob(f"{target}.elf*.su"))
    objs = sorted((swiss_dir / build_dir).glob("*.su"))
    return ltrans + objs


def check_records(
    records: list[FrameRecord],
    indigo_ceiling: int = INDIGO_CEILING,
    total_ceiling: int = TOTAL_CEILING,
) -> list[str]:
    if not records:
        return ["no stack-usage records found (was -fstack-usage dropped from CFLAGS?)"]
    errors: list[str] = []
    for rec in records:
        if rec.qualifier != "static":
            errors.append(
                f"{rec.source}:{rec.line}:{rec.function}: non-static stack frame "
                f"({rec.qualifier}, {rec.bytes_used} bytes) is forbidden on bare-metal PowerPC"
            )
        limit = indigo_ceiling if is_indigo_source(rec.source) else total_ceiling
        scope = "Indigo" if is_indigo_source(rec.source) else "total"
        if rec.bytes_used > limit:
            errors.append(
                f"{rec.source}:{rec.line}:{rec.function}: {rec.bytes_used} bytes exceeds "
                f"{scope} stack ceiling ({limit} bytes)"
            )
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("swiss_dir", type=Path, nargs="?", default=Path("cube/swiss"))
    parser.add_argument("--target", default="swiss")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--indigo-ceiling", type=int, default=INDIGO_CEILING)
    parser.add_argument("--total-ceiling", type=int, default=TOTAL_CEILING)
    args = parser.parse_args(argv)

    su_files = collect_su_files(args.swiss_dir, args.target, args.build_dir)
    if not su_files:
        print(
            f"::error::no .su files found for {args.target} in {args.swiss_dir} / {args.build_dir}",
            file=sys.stderr,
        )
        return 1

    records: list[FrameRecord] = []
    try:
        for path in su_files:
            records.extend(parse_su_text(path.read_text(encoding="utf-8"), str(path.name)))
    except ValueError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 1

    errors = check_records(records, args.indigo_ceiling, args.total_ceiling)
    if errors:
        for err in errors:
            print(f"::error::{err}", file=sys.stderr)
        return 1

    indigo_max = max((r.bytes_used for r in records if is_indigo_source(r.source)), default=0)
    total_max = max((r.bytes_used for r in records), default=0)
    print(
        f"stack usage OK: {len(records)} functions across {len(su_files)} .su files "
        f"(Indigo max {indigo_max}/{args.indigo_ceiling} B, total max {total_max}/{args.total_ceiling} B, "
        "0 dynamic frames)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
