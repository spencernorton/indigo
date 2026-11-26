# Library packs

The Library's pictures and descriptions come from optional files in
`/swiss/ui/` on the device you browse:

| File | What it holds |
| --- | --- |
| `posters.pak` | Box art: a 192×256 front cover per game. |
| `stills.pak` | Gameplay stills: a 320×240 screenshot per game. |
| `discs.pak` | Disc art: a 160×160 picture of each game's disc, for the Jewel Case layout. |
| `descriptions.txt` | A few sentences about each game, for Spotlight. See [below](#game-descriptions). |
| `features.txt` | Badges for each game, for Game Details. See [below](#feature-badges). |

The three packs are built with `buildtools/ui/poster_pack.py` and share one format,
described here. Indigo checks every field below before it uses a pack, and
refuses a pack that differs in any of them.

## Build one

```bash
python3 buildtools/ui/poster_pack.py --covers ~/covers --out posters.pak
python3 buildtools/ui/poster_pack.py --stills ~/screenshots --out stills.pak
python3 buildtools/ui/poster_pack.py --discs ~/discs --out discs.pak
```

Name each image after its game ID, such as `GMSE01.png` or `GALE01.jpg`.
Covers are cropped to 3:4 from the center and must be at least 192×256;
screenshots are cropped to 4:3 and must be at least 320×240. Disc pictures
are cropped square and must be at least 160×160: the disc filling the
picture edge to edge, as GameTDB's disc art is. What is clear around the disc
and in its hole is laid on grey; the jewel case's hub and rim cover both.
Nothing is scaled up.

For more control, pass `--manifest manifest.json` instead of a folder:

```json
{
  "version": 1,
  "kind": "stills",
  "records": [
    {"game_id": "GMSE01", "source": "snaps/sunshine.png", "universal": true,
     "source_sha256": "<the file's SHA-256>", "note": "where it came from",
     "focal": [0.5, 0.5]}
  ]
}
```

`kind` is `posters` (the default), `stills` or `discs`. Every record needs
`game_id`, `source` (relative to the manifest, or to `--art-root`),
`universal` (below), `source_sha256` and a `note`; `focal` (where to keep
the crop, 0 to 1 across and down, default the center) and `dominant_rgb`
(otherwise the picture's average color) are optional. Any other key is an
error.

The builder needs Pillow and `gxtexconv`, from devkitPro or from the
libogc2 Docker image Indigo's build uses. It works offline and gives the
same bytes for the same inputs. Next to the pack it writes
`<pack>.provenance.json`, which records every picture's source and SHA-256
and the exact converter used.

## Format

Version 1. Every number is big-endian, and every offset is from the start
of the file.

### Header (64 bytes)

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | `SWPK` |
| 0x04 | 4 | version | 1 |
| 0x08 | 4 | CRC-32 | of the header (with this field as zero), then the index |
| 0x0C | 4 | record count | 1 to 2,048 (Indigo 2.0 and earlier: 1,024) |
| 0x10 | 4 | index offset | 64 |
| 0x14 | 4 | index length | record count × 32 |
| 0x18 | 4 | data offset | 64 + index length |
| 0x1C | 4 | file length | data offset + record count × record size; the file's real size |
| 0x20 | 4 | record size | bytes per texture (below) |
| 0x24 | 2 | canvas width | the texture's width |
| 0x26 | 2 | canvas height | the texture's height |
| 0x28 | 2 | content width | the picture's width inside the canvas |
| 0x2A | 2 | content height | the picture's height inside the canvas |
| 0x2C | 1 | levels | mip levels, the full-size one included |
| 0x2D | 1 | texture format | 14 (`GX_TF_CMPR`) |
| 0x2E | 18 | reserved | zero |

### What each pack declares

| | `posters.pak` | `stills.pak` | `discs.pak` |
| --- | --- | --- | --- |
| Record size | 43,648 | 38,400 | 12,800 |
| Canvas | 256×256 | 320×240 | 160×160 |
| Content | 192×256 | 320×240 | 160×160 |
| Levels | 5 (256 down to 16) | 1 | 1 |

A mipmapped GX texture must be a power of two on each side, so a cover sits
on a 256×256 canvas with its last column repeated to the right; Indigo draws
only the left 192 columns. A still is never drawn smaller than it is, so it
has no smaller levels and no padding; nor has a disc, which the jewel case
draws about its own size or larger.

### Index (32 bytes a record, sorted by game ID)

| Offset | Size | Field |
| --- | --- | --- |
| 0x00 | 6 | game ID: six of `A`–`Z` and `0`–`9` |
| 0x06 | 1 | flags: 0x01 = universal (below); every other bit zero |
| 0x07 | 1 | zero |
| 0x08 | 4 | texture offset: data offset + position × record size |
| 0x0C | 4 | texture length: the record size |
| 0x10 | 4 | CRC-32 of the texture |
| 0x14 | 4 | dominant color: red, green, blue, then 0xFF |
| 0x18 | 2 | focal point x, 0 to 65,535 across the source picture |
| 0x1A | 2 | focal point y |
| 0x1C | 4 | zero |

IDs are strictly increasing: no ID appears twice. The textures follow the
index in the same order, one after another with no gaps.

## Finding a game's picture

Indigo looks up a game's full six-character ID first. If it's not there, a
record flagged **universal** stands for every game whose ID starts with the
same four characters: the game and its region, whatever the publisher code.
At most one universal record may share those four characters.

A texture whose CRC doesn't match is not shown: that game looks as if the
pack had no picture for it, and the rest of the pack still works. The file is read in pieces as the
Library needs them, never all at once: 25 posters, 3 stills and 3 discs at
most are held in memory. Stills open only on first Spotlight use, and discs
on first Jewel Case use. Thread-safe devices use one background reader with
a 16 KiB read limit and a 16 KiB worker stack. It runs at menu priority below
video, sleeping for 1 ms after each bounded step so other work can run.
It shares reads fairly between
the visible packs, pauses hidden layouts, and checks a texture's complete CRC
before publishing it. Input actions and cache/device changes join the reader
before continuing. Uploads and restarts drain it before suspending the menu;
a rejected upload resumes loading. Other devices, or a failed worker allocation, retain one
shared 16 KiB read per idle menu frame. Changing device discards an unfinished
texture. A game with no disc in `discs.pak` (or no
`discs.pak` at all) has the middle of its cover printed on its disc in the
jewel case, and a game with neither has a bare silver disc.

## Install from the console

**Settings › Network › Install game content** downloads these files (and
the cheat and manual packs) onto the card: see
[Installing game content](guide/settings.md#installing-game-content-over-the-network).
It reads a list of them that a release signs, written by
`buildtools/content_list.py` from the pack zips:

```text
# Indigo game content
Server=203.0.113.7/console/2026-11-26
Host=content.example.net
<sha256> 8348 swiss/cheats/GALE01.txt
<sha256> 13401712 swiss/ui/manuals/GALE.pak
<sha256> 70849024 swiss/ui/posters.pak
```

One file a line: its SHA-256 in lower-case hex, its length in bytes (1 to
128 MiB, no leading zero) and its path on the card, a space apart, in
strictly increasing byte order, at most 4,096 of them in 512 KiB. A path is
one of `swiss/ui/posters.pak`, `stills.pak`, `discs.pak`, `descriptions.txt`
or `features.txt`, `swiss/cheats/<game ID>.txt` or
`swiss/cheats/<game ID>_<name>.txt`, or `swiss/ui/manuals/<ID4 or ID6>.pak`,
47 letters at most. `Server` (an IPv4 address with an optional port and
folder) and `Host` (the name it answers to) come together or not at all;
without them the files sit beside the list. Lines starting with `#` are
comments. Indigo refuses the whole list if any line breaks a rule.

## Game descriptions

`descriptions.txt` is plain text: one game per line, its six-character game
ID, a space (or a tab), then its description, in Windows-1252 as a disc
banner's text is. Lines starting with `#` are comments.

```text
# Descriptions for my card
GMSE01 Clean up Isle Delfino with FLUDD, a water pack with a mind of its own.
GAFE01 Move into a village where something happens every day.
```

The order doesn't matter, and a game's first line wins. A line whose ID isn't
six of `A`–`Z` and `0`–`9` is skipped. The file can be up to 1 MiB and hold up
to 4,096 games. Spotlight shows what fits in six lines of its column, about
250 characters, and ends with "..." when a description runs on.

For a game the file doesn't list, Spotlight uses the first line for another
disc of the same game and region (the same first four characters of its ID),
then its disc banner's own description. Without either, it says the game has
no description rather than leaving the space blank.

## Feature badges

`features.txt` gives Game Details a few badges for each game: how many can
play, its age rating, and the controllers and accessories it uses. It is laid
out as `descriptions.txt` is, above: one game per line, its six-character
game ID, a space (or a tab), then its badges separated by `|`, in
Windows-1252. Lines starting with `#` are comments.

```text
# Feature badges for my card
GKGE01 1-4 PLAYERS|ESRB E|BONGOS
GMSE01 1 PLAYER|ESRB E
```

Game Details shows a game's first four badges on its GAME DETAIL line, in the
file's order and as written, as many as fit beside the heading. Spaces around
a badge are dropped, and an empty badge or one longer than 15 characters is
left out. Indigo reads the first 127 characters of a line; a badge that runs
past them is left out. The file's size and game limits, and how a game it
doesn't list takes another disc's line, are those of `descriptions.txt`. A
game with no line has no badges.
