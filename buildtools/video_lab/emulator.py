#!/usr/bin/env python3
"""Exercise the standalone diagnostic with exact GPU-to-RAM copies in Dolphin.

The report barcode carries checks performed on copied console memory. Screen
captures establish software control flow, not native timing or HDMI detail.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ui/emulator"))
import run  # noqa: E402


def decode(frame):
    gray = frame.mean(axis=2)
    for top in range(25):
        for left in range(25):
            for right in range(615, 641):
                xs = np.clip((left + (np.arange(32) + .5) * (right - left) / 32).astype(int), 0, 639)
                words = []
                for row in range(8):
                    value = 0
                    for bit in gray[top + row * 4 + 1, xs] > 120:
                        value = (value << 1) | int(bit)
                    words.append(value)
                if words[0] == 0x564c4142 and words[1] == 1 and words[3] == 480 and words[2] in (640, 720):
                    return dict(width=words[2], height=words[3], stride=words[4], pattern=words[5],
                                sequence=words[6], vi_width=words[7], left=left, right=right, top=top)
    return None


def gpu_report(frame, found):
    gray = frame.mean(axis=2)
    xs = np.clip((found["left"] + (np.arange(32) + .5) * (found["right"] - found["left"]) / 32).astype(int), 0, 639)
    words = []
    for row in range(8, 16):
        value = 0
        for bit in gray[found["top"] + row * 4 + 1, xs] > 120:
            value = (value << 1) | int(bit)
        words.append(value)
    if words[0] == 0x47585432:
        return dict(state=words[1], error=words[2], matched_frames=words[3], coordinate_crc=words[4],
                    progress=words[5], canaries_ok=words[6], frames=words[7])
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dol", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True)
    work = out / "work"
    work.mkdir()
    result = {"scope": "Dolphin software checks; not native timing or HDMI fidelity",
              "dol_sha256": hashlib.sha256(args.dol.read_bytes()).hexdigest(), "checks": [],
              "exact_gpu_ram_copies": True}
    emu = None
    try:
        emu = run.Emulator(args.dol.resolve(), None, work, out, region="ntsc", cable="component",
                           exact_textures=True, exact_copies=True)

        def until(name, width, pattern, seconds=35):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                frame = emu.frame()
                found = decode(frame)
                if found and found["width"] == width and found["pattern"] == pattern:
                    Image.fromarray(frame).save(out / (name + ".png"))
                    result["checks"].append({"name": name, "decoded": found})
                    return frame, found
                time.sleep(.1)
            Image.fromarray(frame).save(out / (name + "-failed.png"))
            raise AssertionError(f"{name}: last metadata={found}; CPU={emu.where()}")

        until("baseline", 640, 0)
        emu.pad.plug_in()
        time.sleep(1)
        emu.pad.press("RIGHT", .2)
        until("pattern", 640, 1)
        emu.pad.press("A", .2)
        until("trial", 720, 1)
        emu.pad.press("B", .2)
        until("manual-fallback", 640, 1)
        emu.pad.press("A", .2)
        until("trial-for-expiry", 720, 1)
        until("automatic-fallback", 640, 1, seconds=100)
        emu.pad.press("X", .2)
        end = time.monotonic() + 150
        previous = None
        while time.monotonic() < end:
            frame = emu.frame()
            found = decode(frame)
            report = gpu_report(frame, found) if found and found["pattern"] == 5 else None
            if report and report != previous:
                print(json.dumps(report), flush=True)
                previous = report
            if report and report["state"] >= 2:
                Image.fromarray(frame).save(out / "gx-report.png")
                result["gpu_report"] = report
                assert report["state"] == 2 and report["error"] == 0, report
                assert report["matched_frames"] == 96 and report["canaries_ok"] == 1, report
                break
            time.sleep(.15)
        else:
            raise AssertionError(f"No complete GPU report; last={previous}; CPU={emu.where()}")
        emu.pad.press("Y", .2)
        time.sleep(1)
        candidate = emu.frame()
        assert np.mean(np.abs(candidate.astype(int) - frame.astype(int)) > 10) > .05
        Image.fromarray(candidate).save(out / "gx-presented.png")
        emu.pad.press("B", .2)
        time.sleep(.5)
        for _ in range(2):
            emu.pad.press("RIGHT", .2)
            time.sleep(.5)
        until("gx-manual-fallback", 640, 5)
        log = emu.log.read_text(errors="replace")
        assert not re.search(r"DABR: write to|Invalid (?:read|write)|Unhandled Exception|Exception handler", log, re.I)
        result["status"] = "passed"
    except Exception as error:
        result["status"] = "failed"
        result["error"] = repr(error)
        raise
    finally:
        if emu:
            emu.close()
        (out / "report.json").write_text(json.dumps(result, indent=2) + "\n")
        (out / "summary.md").write_text(f"Video diagnostic: **{result['status']}**. Software checks only; native performance and HDMI fidelity require separate evidence.\n")


if __name__ == "__main__":
    main()
