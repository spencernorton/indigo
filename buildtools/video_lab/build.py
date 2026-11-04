#!/usr/bin/env python3
"""Build with Indigo's pinned toolchain and record source/SDK/DOL hashes."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main() -> None:
    folder = Path(__file__).resolve().parent
    root = folder.parent.parent
    workflow = (root / ".github/workflows/ci.yml").read_text()
    image = re.search(r"^  TOOLCHAIN: (\S+@sha256:[a-f0-9]{64})$", workflow, re.M).group(1)
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"], text=True))
    label = revision[:12] + ("-dirty" if dirty else "")
    command = ["docker", "run", "--rm", "-u", f"{os.getuid()}:{os.getgid()}",
               "-v", f"{root}:/work", "-w", "/work", image,
               "make", "-C", "buildtools/video_lab"]
    subprocess.run(command + ["clean"], check=True)
    subprocess.run(command + [f"REVISION={label}"], check=True)
    sdk = subprocess.check_output(["docker", "run", "--rm", "--entrypoint", "sh", image, "-c", "\n".join([
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc --version | head -1",
        "sha256sum /opt/devkitpro/libogc2/gamecube/include/ogc/video_types.h /opt/devkitpro/libogc2/gamecube/include/ogc/video.h /opt/devkitpro/libogc2/gamecube/lib/libogc.a /opt/devkitpro/libogc2/gamecube/lib/libbba.a",
        "grep -n 'define VI_TVMODE(' /opt/devkitpro/libogc2/gamecube/include/ogc/video_types.h",
    ])], text=True)
    (folder / "sdk-audit.txt").write_text(sdk)
    sources = {}
    for name in ("core.c", "core.h", "main.c", "Makefile"):
        sources[name] = hashlib.sha256((folder / name).read_bytes()).hexdigest()
    dol = (folder / "video-lab.dol").read_bytes()
    result = {"revision": revision, "dirty": dirty, "runtime_revision": label, "toolchain": image,
              "source_sha256": sources, "sdk_audit": sdk, "dol_bytes": len(dol),
              "dol_sha256": hashlib.sha256(dol).hexdigest(),
              "enabled_cases": ["P0", "P1"], "custom_timings_enabled": False}
    (folder / "build.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
