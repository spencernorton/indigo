#!/usr/bin/env python3
"""poster_pack.py -- offline generator for Indigo's Library packs.

Builds /swiss/ui/posters.pak (box art), /swiss/ui/stills.pak (gameplay
stills) or /swiss/ui/discs.pak (disc art, for the Jewel Case layout) from a
local JSON manifest and user-supplied source art. Fully
offline: never scrapes, downloads, or calls any network service. Output is
byte-for-byte deterministic for the same inputs and toolchain (gxtexconv +
Pillow versions are recorded in the provenance sidecar; they are inputs to
that determinism contract).

Binary format: docs/PACKS.md (version 1).

Texture geometry: GX hardware (and gxtexconv) require power-of-two
dimensions for mipmapped textures, so the 192x256 poster content is
composited onto a 256x256 canvas (right band edge-extended to keep deep
mip levels from bleeding) and encoded as GX_TF_CMPR with a 5-level mip
chain (256..16). The runtime samples s in [0, 192/256]. A still is never
drawn smaller than itself, so it is one 320x240 GX_TF_CMPR level, 4:3. A
disc is one 160x160 GX_TF_CMPR level, square, the size GameTDB's disc art
comes in: the Jewel Case draws it about that size or larger. Whatever is
transparent around the disc and in its hole is laid on grey first; the
case's hub and rim cover both.

Usage:
  poster_pack.py --covers DIR --out posters.pak
  poster_pack.py --stills DIR --out stills.pak
  poster_pack.py --discs DIR --out discs.pak
  poster_pack.py --manifest manifest.json --out posters.pak [--art-root DIR]
                 [--gxtexconv CMD] [--staging DIR]

--covers takes a folder of front-cover images named by game ID (GMSE01.png,
GALE01.jpg; at least 192x256, cropped to 3:4); --stills a folder of gameplay
screenshots named the same way (at least 320x240, cropped to 4:3); --discs a
folder of disc pictures (at least 160x160, cropped square). Each needs
Pillow plus either gxtexconv or Docker. A manifest makes stills with
"kind": "stills" and discs with "kind": "discs".

gxtexconv resolution order: --gxtexconv / $GXTEXCONV, `gxtexconv` on PATH,
then the repo's libogc2 Docker image (one container run for the whole batch).
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

MAGIC = b"SWPK"
VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 32
INDEX_OFFSET = HEADER_SIZE
MAX_RECORDS = 2048  # UI_ASSETS_MAX_RECORDS: Indigo refuses a bigger pack

CANVAS_W, CANVAS_H = 256, 256
CONTENT_W, CONTENT_H = 192, 256
MIP_LEVELS = 5          # LOD0..LOD4: 256,128,64,32,16
TEX_FORMAT = 14         # GX_TF_CMPR
POSTER_BYTES = 43648    # sum of CMPR mip levels 256^2/2 + 128^2/2 + ... + 16^2/2


class Shape:
    """The texture every record of a pack holds, as its header declares it."""

    def __init__(self, kind, canvas, content, mips, record_bytes):
        self.kind = kind
        self.canvas_w, self.canvas_h = canvas
        self.content_w, self.content_h = content
        self.mips = mips
        self.bytes = record_bytes


POSTER = Shape("posters", (CANVAS_W, CANVAS_H), (CONTENT_W, CONTENT_H),
               MIP_LEVELS, POSTER_BYTES)
STILL = Shape("stills", (320, 240), (320, 240), 1, 320 * 240 // 2)
DISC = Shape("discs", (160, 160), (160, 160), 1, 160 * 160 // 2)
SHAPES = {shape.kind: shape for shape in (POSTER, STILL, DISC)}
# What shows through a disc picture's clear parts once it is CMPR: a grey near
# the bare disc's silver, so a clear edge pixel never prints black.
DISC_BACKDROP = (176, 178, 184)

FLAG_UNIVERSAL = 0x01

TPL_MAGIC = 0x0020AF30
# Immutable digest pin: the generator must be byte-deterministic and fully
# offline, so the docker fallback never resolves a mutable tag and never
# pulls (docker run --pull=never). The digest is recorded in provenance.
DOCKER_IMAGE = ("ghcr.io/extremscorner/libogc2@sha256:"
                "7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565")

GAME_ID_RE = re.compile(r"[A-Z0-9]{6}")
COVER_NAME_RE = re.compile(r"([A-Za-z0-9]{6})\.(?:png|jpe?g)", re.IGNORECASE)
SHA256_RE = re.compile(r"[0-9a-f]{64}")
MANIFEST_RECORD_KEYS = {
    "game_id", "source", "universal", "focal", "dominant_rgb",
    "source_sha256", "note",
}


class PackError(Exception):
    pass


def _require(cond, msg):
    if not cond:
        raise PackError(msg)


def load_manifest(path):
    """Parse and strictly validate the manifest. Returns records sorted by ID."""
    return read_manifest(path)[0]


def read_manifest(path):
    """load_manifest, plus the Shape its "kind" names (posters by default)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        raise PackError(f"manifest unreadable: {e}")

    _require(isinstance(doc, dict), "manifest root must be an object")
    _require(doc.get("version") == 1, "manifest version must be 1")
    known = {"version", "kind", "records"}
    _require(set(doc) <= known,
             f"unknown manifest keys: {sorted(set(doc) - known)}")
    kind = doc.get("kind", "posters")
    _require(kind in SHAPES, f"manifest kind must be one of {sorted(SHAPES)}")
    records = doc.get("records")
    _require(isinstance(records, list) and records, "manifest needs a non-empty records list")
    _require(len(records) <= MAX_RECORDS, f"pack overflow: {len(records)} records > {MAX_RECORDS}")

    seen = set()
    universal_prefixes = {}
    out = []
    for i, rec in enumerate(records):
        where = f"records[{i}]"
        _require(isinstance(rec, dict), f"{where}: must be an object")
        unknown = set(rec) - MANIFEST_RECORD_KEYS
        _require(not unknown, f"{where}: unknown keys {sorted(unknown)}")
        for key in ("game_id", "source", "universal", "source_sha256", "note"):
            _require(key in rec, f"{where}: missing required key '{key}'")

        gid = rec["game_id"]
        _require(isinstance(gid, str) and GAME_ID_RE.fullmatch(gid),
                 f"{where}: invalid game_id {gid!r} (need exactly six of A-Z0-9)")
        _require(gid not in seen, f"duplicate game_id {gid}")
        seen.add(gid)

        _require(isinstance(rec["universal"], bool), f"{gid}: universal must be true/false")
        if rec["universal"]:
            prefix = gid[:4]
            if prefix in universal_prefixes:
                raise PackError(
                    f"{gid}: universal 4-char prefix {prefix!r} already "
                    f"claimed by {universal_prefixes[prefix]} -- "
                    f"ambiguous fallback")
            universal_prefixes[prefix] = gid

        _require(isinstance(rec["source"], str) and rec["source"], f"{gid}: source must be a path")
        sha = rec["source_sha256"]
        _require(isinstance(sha, str) and SHA256_RE.fullmatch(sha.lower()),
                 f"{gid}: source_sha256 must be 64 hex chars")
        _require(isinstance(rec["note"], str) and rec["note"].strip(),
                 f"{gid}: provenance note is required")

        focal = rec.get("focal", [0.5, 0.5])
        _require(isinstance(focal, list) and len(focal) == 2
                 and all(isinstance(v, (int, float)) and not isinstance(v, bool)
                         and 0.0 <= v <= 1.0 for v in focal),
                 f"{gid}: focal must be [x, y] in 0..1")

        dom = rec.get("dominant_rgb")
        if dom is not None:
            _require(isinstance(dom, list) and len(dom) == 3
                     and all(isinstance(v, int) and not isinstance(v, bool)
                             and 0 <= v <= 255 for v in dom),
                     f"{gid}: dominant_rgb must be [r, g, b] 0..255")

        out.append({
            "game_id": gid,
            "source": rec["source"],
            "universal": rec["universal"],
            "focal": [float(focal[0]), float(focal[1])],
            "dominant_rgb": dom,
            "source_sha256": sha.lower(),
            "note": rec["note"],
        })

    out.sort(key=lambda r: r["game_id"])
    return out, SHAPES[kind]


def covers_manifest(covers_dir, kind="posters"):
    """--covers (or --stills, with kind="stills"): a version-1 manifest with
    one record per GAMEID.png/.jpg in covers_dir. Returns (manifest, skipped)
    where skipped lists the visible files whose names are not a game ID."""
    records, skipped = [], []
    for name in sorted(os.listdir(covers_dir)):
        m = COVER_NAME_RE.fullmatch(name)
        if not m:
            if not name.startswith("."):
                skipped.append(name)
            continue
        with open(os.path.join(covers_dir, name), "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        records.append({"game_id": m.group(1).upper(), "source": name,
                        "universal": False, "source_sha256": digest,
                        "note": f"{'covers' if kind == 'posters' else kind} "
                                f"folder: {name}"})
    return {"version": 1, "kind": kind, "records": records}, skipped


def focal_crop(im, focal, shape=POSTER):
    """Largest window of the content's aspect (3:4 poster, 4:3 still, square
    disc) inside im, positioned so the focal point stays proportionally
    placed, then resized to the content size."""
    cw, ch = shape.content_w, shape.content_h
    w, h = im.size
    if w * ch > h * cw:                     # wider than the content -- full height
        crop_h = h
        crop_w = h * cw // ch
    else:                                   # taller than the content -- full width
        crop_w = w
        crop_h = w * ch // cw
    left = min(max(round(focal[0] * w - crop_w / 2), 0), w - crop_w)
    top = min(max(round(focal[1] * h - crop_h / 2), 0), h - crop_h)
    from PIL import Image
    return im.crop((left, top, left + crop_w, top + crop_h)).resize(
        (cw, ch), Image.LANCZOS)


def dominant_color(im):
    """Deterministic dominant color: BOX-filter average of the poster content."""
    from PIL import Image
    px = im.resize((1, 1), Image.BOX).getpixel((0, 0))
    return [px[0], px[1], px[2]]


def compose_canvas(content):
    """Paste 192x256 content onto a 256x256 canvas; edge-extend the right band
    so deep mip levels don't bleed a foreign color into the poster edge."""
    from PIL import Image
    canvas = Image.new("RGB", (CANVAS_W, CANVAS_H))
    canvas.paste(content, (0, 0))
    band = content.crop((CONTENT_W - 1, 0, CONTENT_W, CONTENT_H)).resize(
        (CANVAS_W - CONTENT_W, CANVAS_H), Image.NEAREST)
    canvas.paste(band, (CONTENT_W, 0))
    return canvas


def prepare_images(records, art_root, staging, shape=POSTER):
    """Validate art, crop/compose, write staging PNG + .scf per record.
    Fills in computed dominant_rgb. Hard-errors on any bad input."""
    from PIL import Image, UnidentifiedImageError
    for rec in records:
        gid = rec["game_id"]
        src = os.path.join(art_root, rec["source"])
        _require(os.path.isfile(src), f"{gid}: missing art {src}")
        digest = hashlib.sha256()
        with open(src, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                digest.update(chunk)
        _require(digest.hexdigest() == rec["source_sha256"],
                 f"{gid}: source SHA-256 mismatch for {src} "
                 f"(manifest {rec['source_sha256'][:12]}…, file {digest.hexdigest()[:12]}…)")
        try:
            im = Image.open(src)
            im.load()
        except (UnidentifiedImageError, OSError) as e:
            raise PackError(f"{gid}: corrupt source image {src}: {e}")
        if shape is DISC:
            rgba = im.convert("RGBA")
            im = Image.alpha_composite(Image.new("RGBA", rgba.size, DISC_BACKDROP + (255,)), rgba)
        im = im.convert("RGB")
        _require(im.width >= shape.content_w and im.height >= shape.content_h,
                 f"{gid}: bad dimensions {im.width}x{im.height} "
                 f"(need at least {shape.content_w}x{shape.content_h}, "
                 f"no upscaling)")
        content = focal_crop(im, rec["focal"], shape)
        if rec["dominant_rgb"] is None:
            rec["dominant_rgb"] = dominant_color(content)
        if shape is POSTER:
            content = compose_canvas(content)
        content.save(os.path.join(staging, f"{gid}.png"))
        mips = (f"mipmap=yes minlod=0 maxlod={shape.mips - 1}"
                if shape.mips > 1 else "mipmap=no")
        with open(os.path.join(staging, f"{gid}.scf"), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(f'<filepath="{gid}.png" id="{shape.kind}" '
                    f'colfmt={TEX_FORMAT} {mips} />\n')


def resolve_gxtexconv(explicit):
    """Return ('native', cmd) or ('docker', image). Never touches the network
    beyond whatever a locally-cached docker image implies; the image is the
    same one the repo build already requires."""
    cmd = explicit or os.environ.get("GXTEXCONV")
    if cmd:
        return ("native", cmd)
    if shutil.which("gxtexconv"):
        return ("native", "gxtexconv")
    if shutil.which("docker"):
        return ("docker", DOCKER_IMAGE)
    raise PackError("gxtexconv not found: install devkitPro, set $GXTEXCONV, "
                    "or make docker available for the libogc2 image")


def run_gxtexconv(records, staging, resolver):
    """Convert every staged PNG to TPL. One docker container for the whole
    batch when dockerized; plain loop when native (using the RESOLVED
    command, so --gxtexconv/$GXTEXCONV overrides are honored)."""
    mode, target = resolver
    cmd = "gxtexconv" if mode == "docker" else shlex.quote(target)
    script = os.path.join(staging, "convert.sh")
    with open(script, "w", encoding="utf-8", newline="\n") as f:
        f.write("set -e\n")
        for rec in records:
            gid = rec["game_id"]
            f.write(f"{cmd} -s {gid}.scf -o {gid}.tpl >/dev/null\n")
    if mode == "docker":
        present = subprocess.run(["docker", "image", "inspect", target],
                                 capture_output=True)
        if present.returncode != 0:
            raise PackError("the libogc2 Docker image is not downloaded yet "
                            f"(this tool never pulls); run once: docker pull {target}")
        argv = ["docker", "run", "--rm", "--pull=never",
                "-v", f"{staging}:/work", "-w", "/work",
                target, "sh", "convert.sh"]
    else:
        argv = ["sh", "convert.sh"]
    proc = subprocess.run(argv, cwd=staging, capture_output=True, text=True)
    if proc.returncode != 0:
        raise PackError(f"gxtexconv batch failed:\n{proc.stdout}\n{proc.stderr}")


def extract_tpl_payload(path, gid, shape=POSTER):
    """Parse a single-texture TPL and return the raw CMPR mip payload,
    validating geometry and exact payload length."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise PackError(f"{gid}: TPL unreadable: {e}")
    _require(len(data) > HEADER_SIZE, f"{gid}: TPL truncated ({len(data)} bytes)")
    magic, ntex, _ = struct.unpack(">III", data[:12])
    _require(magic == TPL_MAGIC and ntex == 1, f"{gid}: unexpected TPL structure")
    tex_off = struct.unpack(">I", data[12:16])[0]
    _require(tex_off + 12 <= len(data),
             f"{gid}: TPL texture header offset {tex_off} out of bounds")
    height, width, fmt, data_off = struct.unpack(">HHII", data[tex_off:tex_off + 12])
    _require((width, height, fmt) ==
             (shape.canvas_w, shape.canvas_h, TEX_FORMAT),
             f"{gid}: TPL is {width}x{height} fmt {fmt}, expected "
             f"{shape.canvas_w}x{shape.canvas_h} fmt {TEX_FORMAT}")
    payload = data[data_off:]
    _require(len(payload) == shape.bytes,
             f"{gid}: TPL payload {len(payload)} bytes, expected {shape.bytes} "
             f"(mip chain missing? gxtexconv needs power-of-two input)")
    return payload


def build_pack(records, staging, shape=POSTER):
    """Assemble the deterministic pak bytes from converted payloads."""
    count = len(records)
    data_offset = HEADER_SIZE + count * RECORD_SIZE   # both 32-multiples
    file_length = data_offset + count * shape.bytes
    _require(file_length < 2**32, "pack overflow: file length exceeds 32 bits")

    index = bytearray()
    payloads = []
    for i, rec in enumerate(records):
        payload = extract_tpl_payload(
            os.path.join(staging, f"{rec['game_id']}.tpl"), rec["game_id"],
            shape)
        payloads.append(payload)
        crc = zlib.crc32(payload)
        rec["poster_crc32"] = f"{crc:08x}"
        dom = rec["dominant_rgb"]
        index += struct.pack(
            ">6sBBIII4sHHI",
            rec["game_id"].encode("ascii"),
            FLAG_UNIVERSAL if rec["universal"] else 0,
            0,
            data_offset + i * shape.bytes,
            shape.bytes,
            crc,
            bytes([dom[0], dom[1], dom[2], 0xFF]),
            round(rec["focal"][0] * 65535),
            round(rec["focal"][1] * 65535),
            0,
        )

    def header(crc_field):
        return struct.pack(
            ">4sIIIIIIIIHHHHBBH", MAGIC, VERSION, crc_field, count,
            INDEX_OFFSET, count * RECORD_SIZE, data_offset, file_length,
            shape.bytes, shape.canvas_w, shape.canvas_h, shape.content_w,
            shape.content_h, shape.mips, TEX_FORMAT, 0,
        ) + bytes(16)

    crc = zlib.crc32(header(0) + index)
    return header(crc) + bytes(index) + b"".join(payloads)


def toolchain_info(resolver):
    """Record the exact converter identity: the immutable image digest for
    docker, or the resolved executable path + SHA-256 (+ version banner if
    printed) for a native binary."""
    mode, target = resolver
    try:
        from PIL import __version__ as pillow_version
    except ImportError:
        pillow_version = "unknown"
    info = {"mode": mode}
    if mode == "docker":
        info["image_digest"] = target
        info["pull_policy"] = "never"
    else:
        resolved = shutil.which(target) or target
        info["path"] = resolved
        try:
            with open(resolved, "rb") as f:
                info["sha256"] = hashlib.sha256(f.read()).hexdigest()
        except OSError:
            info["sha256"] = "unresolved"
        try:
            proc = subprocess.run([resolved], capture_output=True, text=True,
                                  timeout=10)
            m = re.search(r"v\d+(?:\.\d+)+", proc.stdout + proc.stderr)
            info["version"] = m.group(0) if m else "unknown"
        except (OSError, subprocess.TimeoutExpired):
            info["version"] = "unknown"
    return {
        "gxtexconv": info,
        "pillow": pillow_version,
    }


def write_provenance(records, out_path, resolver, shape=POSTER):
    doc = {
        "pack": os.path.basename(out_path),
        "kind": shape.kind,
        "format_version": VERSION,
        "toolchain": toolchain_info(resolver),
        "records": [
            {
                "game_id": r["game_id"],
                "source": r["source"],
                "source_sha256": r["source_sha256"],
                "note": r["note"],
                "universal": r["universal"],
                "focal": r["focal"],
                "dominant_rgb": r["dominant_rgb"],
                "poster_crc32": r["poster_crc32"],
            }
            for r in records
        ],
    }
    with open(out_path + ".provenance.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2, sort_keys=True)
        f.write("\n")


def generate(manifest_path, out_path, art_root=None, gxtexconv=None, staging=None):
    """Full pipeline. Returns the pak bytes (also written to out_path)."""
    records, shape = read_manifest(manifest_path)
    art_root = art_root or os.path.dirname(os.path.abspath(manifest_path))
    resolver = resolve_gxtexconv(gxtexconv)
    own_staging = staging is None
    staging = staging or tempfile.mkdtemp(prefix="poster_pack_")
    staging = os.path.abspath(staging)  # docker -v needs an absolute path
    os.makedirs(staging, exist_ok=True)
    try:
        prepare_images(records, art_root, staging, shape)
        run_gxtexconv(records, staging, resolver)
        pack = build_pack(records, staging, shape)
    finally:
        if own_staging:
            shutil.rmtree(staging, ignore_errors=True)
    with open(out_path, "wb") as f:
        f.write(pack)
    write_provenance(records, out_path, resolver, shape)
    return pack


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--manifest")
    source.add_argument("--covers", help="folder of cover images named by game "
                                         "ID, e.g. GMSE01.png or GALE01.jpg")
    source.add_argument("--stills", help="folder of gameplay screenshots named "
                                         "by game ID, for stills.pak")
    source.add_argument("--discs", help="folder of disc pictures named by game "
                                        "ID, for discs.pak")
    ap.add_argument("--out", required=True)
    ap.add_argument("--art-root", help="base dir for manifest source paths "
                                       "(default: manifest's directory)")
    ap.add_argument("--gxtexconv", help="gxtexconv command (default: $GXTEXCONV, "
                                        "PATH, then libogc2 docker image)")
    ap.add_argument("--staging", help="keep intermediates in this directory")
    args = ap.parse_args(argv)
    try:
        folder = args.covers or args.stills or args.discs
        if folder:
            kind = "posters" if args.covers else "stills" if args.stills else "discs"
            manifest, skipped = covers_manifest(folder, kind)
            for name in skipped:
                print(f"poster_pack: skipped {name} (name it GAMEID.png or "
                      f"GAMEID.jpg)", file=sys.stderr)
            _require(manifest["records"],
                     f"no GAMEID.png/.jpg images in {folder}")
            with tempfile.TemporaryDirectory(prefix="poster_covers_") as tmp:
                manifest_path = os.path.join(tmp, "manifest.json")
                with open(manifest_path, "w", encoding="utf-8") as f:
                    json.dump(manifest, f)
                pack = generate(manifest_path, args.out, folder,
                                args.gxtexconv, args.staging)
        else:
            pack = generate(args.manifest, args.out, args.art_root,
                            args.gxtexconv, args.staging)
    except PackError as e:
        print(f"poster_pack: error: {e}", file=sys.stderr)
        return 2
    print(f"poster_pack: wrote {args.out} "
          f"({len(pack)} bytes, sha256 {hashlib.sha256(pack).hexdigest()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
