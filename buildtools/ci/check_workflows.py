#!/usr/bin/env python3
"""Hold the workflows to how Indigo's CI works.

- Every job runs on our own runners (runs-on names self-hosted and a pool).
- Every action is pinned to a full commit SHA.
- No job asks for Docker (container:, services:): our runners have none.
- No pull_request_target: a fork's code never runs with this repository's rights.
- The toolchain a workflow names is the one the build runner image is built on,
  and so is every other copy of its digest, in the docs and scripts too:
  Dependabot leaves it alone, and it moves by hand, all at once.

usage: check_workflows.py [repository root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

POOLS = {"indigo-build", "indigo-emulator"}
# A copy of the toolchain's digest, written whole or split over two string
# literals.
DIGEST = re.compile(r"libogc2@sha256:[\"'\s]*([0-9a-f]{64})")
COPIES = (".md", ".py", ".sh", ".yml", ".yaml", ".Dockerfile")
FORBIDDEN = {
    "pull_request_target": "pull_request_target runs fork code with this repository's rights",
    "container:": "our runners have no Docker; the build runner already is the toolchain image",
    "services:": "our runners have no Docker",
}


def runs_on_values(lines: list[str]) -> list[tuple[int, set[str]]]:
    found = []
    for number, line in enumerate(lines):
        match = re.match(r"^\s*runs-on:\s*(.*?)\s*(#.*)?$", line)
        if not match:
            continue
        value = match.group(1)
        if value.startswith("["):
            labels = {part.strip().strip("'\"") for part in value.strip("[]").split(",")}
        elif value:
            labels = {value.strip("'\"")}
        else:  # a block list on the following lines
            labels = set()
            for following in lines[number + 1:]:
                item = re.match(r"^\s*-\s*(\S+)", following)
                if not item:
                    break
                labels.add(item.group(1).strip("'\""))
        found.append((number + 1, labels))
    return found


def fuzz_gate_problems(text: str) -> list[str]:
    """The fuzz job must survive a skipped ancestor, after CI succeeds only."""
    job = re.search(r"^  fuzz:\s*\n(.*?)(?=^  [\w-]+:|\Z)", text, re.M | re.S)
    if not job:
        return ["ci.yml: missing Fuzz job"]
    fields = dict(re.findall(r"^    (needs|if):\s*(.*?)\s*$", job.group(1), re.M))
    found = []
    if fields.get("needs") not in {"passed", "[passed]"}:
        found.append("ci.yml: Fuzz must wait for CI passed")
    condition = fields.get("if", "")
    if condition.startswith("${{") and condition.endswith("}}"):
        condition = condition[3:-2]
    terms = set(re.sub(r"\s+", "", condition).split("&&"))
    required = {"!cancelled()", "needs.passed.result=='success'"}
    if terms not in (required, required | {"always()"}):
        found.append("ci.yml: Fuzz needs an explicit cancellation check and successful CI gate "
                     "so a skipped ancestor does not suppress it")
    return found


def problems(root: Path) -> list[str]:
    found: list[str] = []
    dockerfile = (root / "buildtools/ci/runner/build.Dockerfile").read_text()
    base = re.search(r"^FROM (\S+)", dockerfile, re.M)
    if not base or "@sha256:" not in base.group(1):
        found.append("build.Dockerfile: FROM must pin the toolchain by digest")
    elif f'echo "{base.group(1)}"' not in dockerfile:
        found.append("build.Dockerfile: /etc/indigo-ci/toolchain must record its FROM image")
    workflows = sorted((root / ".github/workflows").glob("*.y*ml"))
    if not workflows:
        found.append("no workflows found")
    for path in workflows:
        name = path.relative_to(root)
        text = path.read_text()
        lines = text.splitlines()
        if path.name == "ci.yml":
            found.extend(fuzz_gate_problems(text))
        for number, line in enumerate(lines, 1):
            code = line.split("#", 1)[0]
            key = re.match(r"^\s*([\w-]+):", code)
            for needle, why in FORBIDDEN.items():
                if needle.rstrip(":") in code and (needle == "pull_request_target" or
                                                    (key and key.group(1) + ":" == needle)):
                    found.append(f"{name}:{number}: {needle.rstrip(':')} — {why}")
        values = runs_on_values(lines)
        if not values:
            found.append(f"{name}: no jobs found")
        for number, labels in values:
            if "self-hosted" not in labels or not labels & POOLS:
                found.append(f"{name}:{number}: runs-on {sorted(labels)} is not one of our pools "
                             f"(self-hosted plus {' or '.join(sorted(POOLS))})")
        for number, line in enumerate(lines, 1):
            use = re.match(r"^\s*(?:-\s*)?uses:\s*(\S+)", line)
            if use and not use.group(1).startswith("./") and \
                    not re.fullmatch(r"[\w.-]+/[\w./-]+@[0-9a-f]{40}", use.group(1)):
                found.append(f"{name}:{number}: {use.group(1)} is not pinned to a full commit SHA")
        toolchain = re.search(r"^\s*TOOLCHAIN:\s*(\S+)", text, re.M)
        if toolchain and base and toolchain.group(1) != base.group(1):
            found.append(f"{name}: TOOLCHAIN {toolchain.group(1)} is not build.Dockerfile's {base.group(1)}")
    if base and "@sha256:" in base.group(1):
        pinned = base.group(1).rsplit("@sha256:", 1)[1]
        for path in sorted(root.rglob("*")):
            if ".git" in path.parts or path.suffix not in COPIES or not path.is_file():
                continue
            for digest in DIGEST.findall(path.read_text(errors="replace")):
                if digest != pinned:
                    found.append(f"{path.relative_to(root)}: libogc2 {digest[:12]} is not "
                                 f"build.Dockerfile's {pinned[:12]}")
    return found


def main(argv: list[str]) -> int:
    root = Path(argv[1] if len(argv) > 1 else ".")
    found = problems(root)
    for problem in found:
        print(f"::error::{problem}", file=sys.stderr)
    if not found:
        print("workflows: every job on our runners, actions pinned, toolchain matches the runner image")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
