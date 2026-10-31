#!/usr/bin/env python3
"""Frame budget: what one steady frame of each scene costs the console.

frame_budget.c runs the real cube renderer and the hint icons against
counting GX stubs. Its counts are compared with frame_budget.json, whose
numbers are ceilings: a change that adds work to a frame raises one on
purpose, and a change that removes work lowers it, with

    python3 test_frame_budget.py --update

so the diff of frame_budget.json is the change's cost or gain. The hash is
reported, not enforced: it changes when a change moves a visible pixel.
"""

import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GUI = ROOT / "cube/swiss/source/gui"
BUDGET = HERE / "frame_budget.json"
METRICS = ("sqrtf", "trig", "minmax", "vertices", "begins", "copy_pixels", "invalidations")
SOURCES = ("ui_scene.c", "ui_motion.c", "ui_home.c", "ui_cube_motif.c",
           "ui_color.c", "ui_stage.c", "ui_clock.c", "ui_saves.c", "ui_files.c", "ui_hint.c")


def hint_source() -> str:
    """The hint icons' shapes, out of FrameBufferMagic.c."""
    fbm = (GUI / "FrameBufferMagic.c").read_text(encoding="utf-8")
    parts = re.findall(r"^#define HINT_\w+ .*$", fbm, re.M)
    for marker in ("static void _HintVertex(", "static void _HintShape(",
                   "static void _HintRoundRect("):
        parts.append(extract_function(fbm, marker))
    return "\n\n".join(parts) + "\n"


def renderer_definition(source: str, name: str, optional: bool = False) -> str:
    """Select a definition rather than a forward declaration before a dialog.

    A semicolon cannot occur between the argument list and opening brace.
    Keep the return type so the extracted helper remains valid standalone C.
    """
    matches = list(re.finditer(r"^static\s+[^;{}\n]+\b" + re.escape(name) +
                              r"\([^;{}]*\)\s*\{", source, re.M))
    if optional and not matches:
        return ""
    if len(matches) != 1:
        raise ValueError(f"Expected one renderer definition for {name}, found {len(matches)}")
    start = matches[0].start()
    return extract_function(source[start:], source[start:matches[0].end() - 1].rstrip())


def save_cubes_source(source: str | None = None) -> str:
    """Memory Cards' cube emitter, out of FrameBufferMagic.c, and the cubes
    it draws (ui_save_cubes.c), under the counting maths."""
    fbm = source if source is not None else (GUI / "FrameBufferMagic.c").read_text(encoding="utf-8")
    start = fbm.index("/* What one frame's cubes need. */")
    parts = ['#include "ui_save_cubes.c"',
             "static void drawInit(void) {}",
             "static void _SetupRasterColor(void) {}",
             fbm[start:fbm.index("} saveCubesDraw_t;", start) + len("} saveCubesDraw_t;")]]
    for name in ("_SaveCubesShades", "_SaveCubesColour", "_SaveCubesVertex", "_SaveCubesEmit"):
        definition = renderer_definition(fbm, name, optional=name == "_SaveCubesColour")
        if definition:
            parts.append(definition)
    return "\n\n".join(parts) + "\n"


def files_source() -> str:
    """The File Browser's frame (FrameBufferMagic.c): every box, bar, track
    and cube, its words through frame_budget.c's font stand-in, and its hint
    line's buttons; all but its one banner."""
    fbm = (GUI / "FrameBufferMagic.c").read_text(encoding="utf-8")
    octagon = fbm[fbm.index("static const float filesOctagon"):]
    parts = ['#include "ui_files.h"', '#include "ui_hint.h"', octagon[:octagon.index("};") + 2]]
    parts += re.findall(r"^#define FILES_CHIP_\w+ .*$", fbm, re.M)
    parts += re.findall(r"^static const GXColor settings(?:Ink|Quiet|Accent) = .*$", fbm, re.M)
    for name in ("_putFlatVertex", "_putFlatRect", "_SaveCubesFaded", "_SaveCubesBox",
                 "_SaveCubesBar", "_FilesVertex", "_FilesQuad", "_FilesRect", "_FilesColor",
                 "_FilesEmblemVertices", "_FilesCube", "_FilesOutline", "_FilesShapes",
                 "_HintDisc", "_HintAlpha", "_HintOctagon", "_HintLetter", "_DrawHintGlyph",
                 "_DrawHintText", "_FilesWords", "_FilesMenu", "_FilesMessage"):
        parts.append(renderer_definition(fbm, name))
    return "\n\n".join(parts) + "\n"


def check_renderer_selection() -> None:
    """Forward declarations and unrelated pages must never enter the emitter."""
    current = (GUI / "FrameBufferMagic.c").read_text(encoding="utf-8")
    emitter = save_cubes_source(current)
    inserted = ("static void _SaveCubesEmit(void);\n"
                "static GXColor _SaveCubesColour(void);\n"
                "static void _SaveCubesVertex(void);\n"
                "static void unrelated_dialog(void) { unknown_page_type(); }\n" + current)
    assert save_cubes_source(inserted) == emitter
    assert "_DrawMemoryCardFolder" not in emitter and "uiDrawObj_t" not in emitter
    # Older baselines use themed shades directly and have no color helper.
    color = renderer_definition(current, "_SaveCubesColour", optional=True)
    historical = current.replace(color, "") if color else current
    historical = historical.replace("_SaveCubesColour(draw, cube, quad->role)",
                                    "draw->shades[cube->shade][quad->role]")
    historical_emitter = save_cubes_source(historical)
    assert renderer_definition(historical, "_SaveCubesVertex") in historical_emitter
    assert not renderer_definition(historical, "_SaveCubesColour", optional=True)


def measure(renderer_source: str | None = None, driver_source: str | None = None) -> dict:
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "hint_source.c").write_text(hint_source(), encoding="utf-8")
        Path(tmp, "save_cubes_source.c").write_text(save_cubes_source(renderer_source), encoding="utf-8")
        Path(tmp, "files_source.c").write_text(files_source(), encoding="utf-8")
        binary = Path(tmp, "frame_budget")
        driver = HERE / "frame_budget.c"
        if driver_source is not None:
            driver = Path(tmp, "frame_budget.c")
            driver.write_text(driver_source, encoding="utf-8")
        sources = [GUI / name for name in SOURCES]
        # Only the folder-color emitter depends on this newer helper. Keeping
        # it conditional also permits measuring pre-feature source trees.
        if (GUI / "ui_folder.c").is_file():
            sources.append(GUI / "ui_folder.c")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-O1", "-ffp-contract=off", "-Wall",
            "-I", str(HERE / "fixtures/gx"), "-I", str(GUI), "-I", tmp,
            "-o", str(binary), str(driver),
            *(str(path) for path in sources), "-lm"]
        subprocess.run(command, check=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True,
                                text=True).stdout
    return {row["scene"]: row for row in map(json.loads, output.splitlines())}


def main() -> int:
    check_renderer_selection()
    measured = measure()
    if "--update" in sys.argv[1:]:
        BUDGET.write_text(json.dumps(measured, indent=1, sort_keys=True) + "\n",
                          encoding="utf-8")
        print(f"wrote {BUDGET.name}: {len(measured)} scenes")
        return 0

    budget = json.loads(BUDGET.read_text(encoding="utf-8"))
    failures = []
    print(f"{'scene':24} " + " ".join(f"{m:>12}" for m in METRICS) + "  hash")
    for name, row in measured.items():
        ceiling = budget.get(name)
        if ceiling is None:
            failures.append(f"{name}: no budget")
            continue
        cells = []
        for metric in METRICS:
            cell = f"{row[metric]:g}"
            if row[metric] > ceiling[metric]:
                failures.append(f"{name}: {metric} {row[metric]:g} > {ceiling[metric]:g}")
                cell += "!"
            elif row[metric] < ceiling[metric]:
                cell += "-"
            cells.append(f"{cell:>12}")
        moved = "" if row["hash"] == ceiling["hash"] else "  (pixels moved)"
        print(f"{name:24} " + " ".join(cells) + f"  {row['hash']}{moved}")
    failures += [f"{name}: scene gone" for name in budget.keys() - measured.keys()]
    if failures:
        print("\nframe budget exceeded:\n  " + "\n  ".join(failures) +
              "\nA change that adds work raises the budget on purpose: "
              "test_frame_budget.py --update", file=sys.stderr)
        return 1
    print("frame budget held ('-' is under budget: --update locks the gain in)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
