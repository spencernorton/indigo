#!/usr/bin/env python3
"""Read and control an already-running standalone video diagnostic.

This client never uploads a DOL, changes installed files, or powers a console.
Coordinate exclusive console use before mutating commands. A successful XFB
capture proves source memory contents, not the picture after HDMI processing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request
import zlib


def exchange(host: str, path: str, *, post: bool = False) -> tuple[bytes, dict[str, str]]:
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", host) or host.startswith("-"):
        raise ValueError("use a console address or hostname, without a URL/path")
    request = urllib.request.Request(
        f"http://{host}:4300{path}", data=b"" if post else None,
        headers={"X-Lab": "1"} if post else {}, method="POST" if post else "GET")
    # An explicit console address should not pass through a web proxy.
    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request, timeout=5) as response:
        if response.headers.get("X-Video-Lab") != "1":
            raise ValueError("the endpoint is not the standalone video diagnostic")
        length = int(response.headers.get("Content-Length", "-1"))
        if length < 0 or length > 720 * 480 * 2:
            raise ValueError("response length is missing or exceeds the diagnostic limit")
        body = response.read(length + 1)
        if len(body) != length:
            raise ValueError("incomplete or overlong response")
        return body, dict(response.headers.items())


def status(host: str) -> dict:
    body, _ = exchange(host, "/status")
    result = json.loads(body)
    if result.get("protocol") != "video-lab-1" or result.get("case") not in ("P0", "P1"):
        raise ValueError("wrong diagnostic protocol or mode")
    return result


def decode_coordinates(raw: bytes, width: int, height: int, stride: int, tolerance: int = 0) -> dict:
    levels = {16: 0, 80: 1, 160: 2, 235: 3}
    def level(value):
        for expected, code in levels.items():
            if abs(value - expected) <= tolerance:
                return code
        raise ValueError(f"unexpected coordinate luma {value}")
    columns, rows = [], []
    for x in range(width):
        value = 0
        for bit in range(10):
            value |= (level(raw[((bit * 6 + 2) * stride + x) * 2]) & 1) << bit
        columns.append(value)
    for y in range(height):
        value = 0
        for bit in range(9):
            value |= (level(raw[(y * stride + bit * 6 + 2) * 2]) >> 1) << bit
        rows.append(value)
    return {"columns_match": columns == list(range(width)),
            "rows_match": rows == list(range(height)),
            "distinct_columns": len(set(columns)), "distinct_rows": len(set(rows))}


def capture(host: str, prefix: Path, *, path: str = "/xfb") -> dict:
    before = status(host)
    raw, headers = exchange(host, path)
    width, height = map(int, headers["X-Size"].split("x"))
    stride = int(headers["X-Stride-Pixels"])
    pattern, sequence = int(headers["X-Pattern"]), int(headers["X-Sequence"])
    if width not in (640, 720) or height != 480 or stride != width:
        raise ValueError("unexpected framebuffer dimensions")
    if int(headers["X-Boot-Nonce"]) != before["boot_nonce"]:
        raise ValueError("the console restarted during capture")
    if len(raw) != stride * height * 2 or zlib.crc32(raw) != int(headers["X-CRC32"]):
        raise ValueError("framebuffer length/CRC differs from the snapshot metadata")
    renderer = headers.get("X-Renderer", "cpu")
    result = {"scope": "GameCube XFB memory only; not an HDMI capture", "capture_path": path, "renderer": renderer,
              "before": before, "width": width, "height": height, "stride_pixels": stride,
              "pattern": pattern, "sequence": sequence, "sha256": hashlib.sha256(raw).hexdigest(),
              "crc32": zlib.crc32(raw), "bytes": len(raw)}
    if any(abs(value - 128) > (1 if renderer == "gx" else 0) for value in raw[1::2]):
        raise ValueError("unexpected chroma in the grayscale pattern")
    if pattern == 3:
        result["coordinate_luma_tolerance"] = 2 if renderer == "gx" else 0
        result["coordinate_decode"] = decode_coordinates(raw, width, height, stride, result["coordinate_luma_tolerance"])
        if not all(result["coordinate_decode"][key] for key in ("columns_match", "rows_match")):
            raise ValueError("source coordinate codes do not reconstruct the expected grid")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    raw_path, json_path = prefix.with_suffix(".yuyv"), prefix.with_suffix(".json")
    raw_path.write_bytes(raw)
    json_path.write_text(json.dumps(result, indent=2) + "\n")
    if shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "rawvideo", "-pixel_format", "yuyv422",
                        "-video_size", f"{stride}x{height}", "-i", str(raw_path),
                        "-frames:v", "1", "-y", str(prefix.with_suffix(".png"))], check=True, timeout=10)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("action", choices=("status", "capture", "baseline", "trial", "expire", "exit", "pattern",
                                          "gx-status", "gx-start", "gx-show", "gx-capture", "gx-scene"))
    parser.add_argument("value", nargs="?")
    parser.add_argument("--expect-boot", type=int, help="refuse a command after a different boot")
    args = parser.parse_args()
    current = status(args.host)
    if args.expect_boot is not None and current["boot_nonce"] != args.expect_boot:
        raise ValueError("boot identity changed")
    if args.action == "status":
        result = current
    elif args.action == "gx-status":
        result = json.loads(exchange(args.host, "/gx/status")[0])
    elif args.action in ("capture", "gx-capture", "gx-scene"):
        if not args.value:
            parser.error("capture needs an output prefix")
        path = "/gx/xfb" if args.action == "gx-capture" else "/gx/scene/0" if args.action == "gx-scene" else "/xfb"
        result = capture(args.host, Path(args.value), path=path)
    else:
        path = {"baseline": "/baseline", "trial": "/trial/P1", "expire": "/expire", "exit": "/exit",
                "gx-start": "/gx/start", "gx-show": "/gx/show"}
        if args.action == "pattern":
            if args.value not in tuple(map(str, range(6))):
                parser.error("pattern must be 0 through 5")
            target = f"/pattern/{args.value}"
        else:
            target = path[args.action]
        body, _ = exchange(args.host, target, post=True)
        result = {"boot_nonce": current["boot_nonce"], "reply": body.decode().strip(),
                  "note": "Queued acknowledgement; read status to verify application/presentation."}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
