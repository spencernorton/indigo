"""Compile the actual upload decoder and image checks on a host or fuzzer."""
from pathlib import Path
import re
from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "cube/swiss/source/wiiload.c").read_text()


def stream_source():
    parts = re.findall(r"^#define WIILOAD_\w+ .*$", SOURCE, re.M)
    for marker in ("static void *wiiload_read(", "static void *wiiload_read_args(",
                   "static u32 wiiload_be32(", "static u32 wiiload_be16(",
                   "static bool wiiload_range(", "static bool wiiload_cli_tail_valid(", "bool wiiload_image_valid("):
        parts.append(extract_function(SOURCE, marker))
    return "\n\n".join(parts) + "\n"
