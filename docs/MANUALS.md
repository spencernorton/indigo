# Game manuals

A game whose manual is on the card has a **Manual** row at the top of its
[Game Details](guide/game-details.md). A on it opens the manual: its pages
at the width of the screen, one at a time.

Indigo comes with no manuals. With a network adapter, **Settings › Network ›
Install game content** downloads the ready-made English manuals for the games
you browsed last: see
[Installing game content](guide/settings.md#installing-game-content-over-the-network).
Or convert your own copy on a computer with
`buildtools/ui/manual_pack.py`, once per game, and copy the file it makes to
the card. The GameCube can't read a PDF itself: scanned manuals are often
JPEG 2000 or JBIG2 inside, and a page decoded on the console wouldn't fit
beside the menus. The converter does that work once, ahead of time.

## Convert a manual

You need Python 3 with Pillow, NumPy and pypdfium2:

```bash
pip install pillow numpy pypdfium2
```

Then, with the game's six-character ID, such as `GALE01` (the Library
shows it under the game's title and publisher; the guide says
[where it comes from](guide/posters.md#where-the-game-id-comes-from)):

```bash
python3 buildtools/ui/manual_pack.py "Super Smash Bros. Melee.pdf" --id GALE01
```

That writes `GALE01.pak` where you ran it. Copy it to `/swiss/ui/manuals/`
on the SD card the Library browses, beside `posters.pak`:

```text
/swiss/ui/manuals/GALE01.pak
```

A file named after the first four characters of the ID, such as `GALE.pak`,
stands for every copy of that game in that region, whatever the publisher
code. Indigo looks for the full ID first.

The manual can also be a folder of page pictures (PNG or JPEG), read in name
order. `--disc game.iso` reads the ID from a disc image instead of `--id`.

| Option | What it does |
| --- | --- |
| `--gray` | Stores the pages in 16 grays (GX I4) instead of colour (GX CMPR), in the same space. |
| `--no-trim` | Keeps each page's margins as scanned. By default the empty paper and a scanner's dark edge are cut away, so the text is drawn bigger. |
| `--pages 3-40` | Only these pages of the PDF or folder, counted from 1. |
| `--preview DIR` | Also writes every page as PNG, exactly as the console will show it. |
| `--out FILE` | Where to write the file. |

A picture wider than it is tall is taken for two pages side by side, as most
scans of a booklet are, and split. Each page is scaled to the screen's width,
640 pixels; a page that would then be taller than 1152 pixels is scaled to
that height instead, a little narrower. The same input gives the same bytes.
A 50-page manual takes about ten seconds and makes a file of about 23 MB.

## Read one

| Button | What it does |
| --- | --- |
| Up, Down (D-pad or stick) | Scroll the page |
| Left, Right | The page before or after, from its top |
| L, R | The first page, the last page |
| Y | The whole page, small, to see where you are; Y again for full width |
| B | Back to Game Details |

The page after the one you are reading is read ahead, so turning to it shows
it at once. Two pages are held in memory at most, under 1 MB, and closing the
manual gives all of it back. With Menu Widescreen on, a page keeps its shape
at the stage's width.

A damaged or wrong file keeps the Manual row, which says **Can't be read**,
and A on it says so instead of opening it. A page whose data is damaged says
so too, and the rest of the manual still reads.

## Format

Version 1, in the family of the Library's packs ([PACKS.md](PACKS.md)): every
number is big-endian, the header and index are CRC-checked, every page has the
same geometry, and each page's data has a CRC of its own. Indigo checks every
field below before it uses a file, and refuses a file that differs in any of
them.

### Header (64 bytes)

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | `SWMN` |
| 0x04 | 4 | version | 1 |
| 0x08 | 4 | CRC-32 | of the header (with this field as zero), then the index |
| 0x0C | 4 | page count | 1 to 256 |
| 0x10 | 4 | index offset | 64 |
| 0x14 | 4 | index length | page count × 32 |
| 0x18 | 4 | data offset | 64 + index length |
| 0x1C | 4 | file length | data offset + page count × record size; the file's real size |
| 0x20 | 4 | record size | 400 × canvas height: the page's texture, then its half |
| 0x24 | 2 | canvas width | 640 |
| 0x26 | 2 | canvas height | 16 to 1152, a multiple of 16: the tallest page, rounded up |
| 0x28 | 2 | half width | 320 |
| 0x2A | 2 | half height | canvas height ÷ 2 |
| 0x2C | 1 | levels | 2: the page, then a half-size copy for the whole-page view |
| 0x2D | 1 | texture format | 14 (`GX_TF_CMPR`) or 0 (`GX_TF_I4`) |
| 0x2E | 18 | reserved | zero |

### Index (32 bytes a page, in reading order)

| Offset | Size | Field |
| --- | --- | --- |
| 0x00 | 4 | record offset: data offset + page number × record size |
| 0x04 | 4 | record length: the record size |
| 0x08 | 4 | CRC-32 of the record |
| 0x0C | 2 | page width inside the canvas, 1 to 640 |
| 0x0E | 2 | page height inside the canvas, 1 to the canvas height |
| 0x10 | 16 | zero |

### A page's record

The page as a 640 × canvas-height GX texture, 4 bits a texel in 8 × 8 tiles
(CMPR: four 4 × 4 blocks a tile, each two RGB565 colours and 2-bit indexes),
then the same page halved, 320 × canvas-height ÷ 2 in the same format. The page
sits at the top left of its canvas; the rest is filled with the colour of the
page's edge. Records follow the index one after another, with no gaps.

A record whose CRC doesn't match is not shown: that page says it can't be
read, and the rest of the manual still works. The file is read a page at a
time, never all at once.
