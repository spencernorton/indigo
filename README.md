<h1 align="center">Indigo</h1>

<p align="center">
  <strong>A fork of Swiss for the Nintendo GameCube with the interface rebuilt.</strong><br>
  An animated Home, a poster library and game details, drawn in the console's own visual language.
</p>

<p align="center">
  <strong><a href="https://norvitech.com/indigo/guide/install/">Install Guide →</a></strong><br>
  Pick what starts your GameCube and follow four steps.
</p>

<p align="center">
  <a href="https://norvitech.com"><img alt="NorviTech Suite" src="https://img.shields.io/badge/NorviTech-Suite-FD8024.svg"></a>
  <a href="https://github.com/spencernorton/indigo/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/spencernorton/indigo/actions/workflows/ci.yml/badge.svg?branch=main"></a>
  <a href="https://github.com/spencernorton/indigo/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/spencernorton/indigo?label=release"></a>
  <a href="https://github.com/spencernorton/indigo/releases/latest"><img alt="Download for your SD card" src="https://img.shields.io/badge/download-SD%20card%20zip-2D2D2D.svg"></a>
  <a href="LICENSE"><img alt="Licence" src="https://img.shields.io/badge/licence-GPL--2.0--or--later-blue.svg"></a>
  <a href="https://buy.stripe.com/8x26oH2U44f65TRe574wM04"><img alt="Donate" src="https://img.shields.io/badge/donate-Stripe-635bff.svg?logo=stripe&logoColor=white"></a>
</p>

<p align="center">
  <img alt="The Indigo Home screen: a clear glass cube turning sideways and tipping up and down between its Library, Source, Settings and System faces over dark waves. Its far edges bend through the front glass, soft lights mirrored in the glass slide across it as it turns, the rounded edges show faint prism colours, and the icons sit sharp on the glass. The Library face shows a GameCube controller, only the face you are on is named underneath, and its controls are shown as GameCube buttons." src="docs/screenshots/home.png" width="640">
</p>

Captured in the Dolphin emulator. The library and game detail pictures show a real poster pack and cheat file in use; box art belongs to its publishers.

**New to Indigo?** Start with the [Install Guide](https://norvitech.com/indigo/guide/install/),
then explore every screen and setting in the [Indigo guide](docs/guide/README.md).
Watch it in motion: [the video tour](https://norvitech.com/indigo/#videos) on norvitech.com.

Indigo is an unofficial fork of [Swiss](https://github.com/emukidid/swiss-gc),
the homebrew utility that boots and patches games on a Nintendo GameCube. The
fork changes one thing: what you look at. Everything underneath — the device
handlers, the patch engine, the loader — is upstream's work, and this fork
does not try to improve it. It is for people who use Swiss daily on real
hardware and want it to feel like it belongs on the console.

## What it does

**Home is an animated cube.** Four faces, one destination each, and a fifth,
Apps, when your card has programs in `/apps`: left and right turn it
sideways, up and down tip it over. When Indigo starts, the cube
flies in from the distance, spinning, and comes to rest on the face you
start on. It is glass, like the
GameCube's own menu, and it treats light the way glass does. Through the clear
front you see the cube's far edges, bent and slightly magnified, and a smoked
panel under each face's icon; the rounded edges part the light into colour
like a prism. Soft lights mirrored in the glass slide across it as it turns.
Highlights glow, a fine rim lights the edges that face the light, and the face
you are on lifts its icon a little off the glass, sharp, with a soft shadow
under it. Each face carries its own
emblem, and only the face you are on is named, under the cube. The Library face is a GameCube
controller that mirrors yours: its sticks lean with your sticks and its
buttons light as you press them, and when you leave it alone it plays by
itself. The GameCube's own interface language — the idle cube, the typeface,
the palette — is the reference, not a desktop launcher.

<p align="center">
  <img alt="Indigo starting: a small cube spins in from the distance in the middle of the screen, tumbles toward you and comes to rest on the Library face; the glass lights up, and LIBRARY and the controls fade in." src="docs/screenshots/intro.png" width="640">
</p>

**It has its own music.** The menus play "Up in the Sky" by Memoraphile
(CC0; see [`NOTICE`](NOTICE)): a quiet intro once, then a section that loops.
Settings › Quick › Menu Music turns it off.

**The library retains its posters.** The games in `/games` on the current
source, with artwork kept across navigation rather than re-read per frame,
and your selection restored when you come back from a game's details. It
opens as a row, with two covers either side of the game in the middle; it
can also be a column with the title beside the cover, a grid five covers
wide, Spotlight: the selected game's gameplay still and description over
a row of disc banners, a design that comes from mvizensk's
[Gameplay Spotlight](https://github.com/mvizensk/gameplay-spotlight), with
their permission, or Jewel Case: one clear CD jewel case at a time, the
game's own disc spinning inside, from portlandhodl's
[Swiss](https://github.com/portlandhodl/swiss-gc) after Ann Nguyen's
[CD Case](https://www.bubbbly.com/cd-case) (Setup › Library › Library
Layout). Y on a game opens its own settings. Cover art comes from a
ready-made pack or one you build yourself (see [Posters](#posters)); without
one, each game gets a card with its disc banner.

<p align="center">
  <img alt="The game library in its four layouts in turn. Horizontal: a row of GameCube box art, the selected cover raised between two covers either side, with its title and publisher below it; two steps right. Vertical: a column of covers down the left, the selected one large with its title, publisher and game ID beside it; three steps down. Grid: five covers across and three rows on screen, a lit frame moving right, right, down and down. Spotlight: the selected game's gameplay still fills a framed panel, with its title, publisher, a few sentences about it and its game ID beside it, over a row of disc banners; two steps right bring 007: Everything or Nothing's and From Russia With Love's stills and text." src="docs/screenshots/library.png" width="640">
</p>

**Apps keeps your other programs apart.** Emulators, Game Boy Interface,
tools: put their `.dol` files in `/apps` and they get their own face of the
cube, shown as posters and started the way games are, never mixed in with
your games. Each app's picture is any PNG you put beside it (`gbi.png` for
`gbi.dol`); Indigo turns it into a poster on the console. See
[Apps](docs/guide/apps.md).

**Change Source shows the devices themselves.** Choose it on the Source
face: the cube lifts out of the way and the devices Indigo found line up
under it on glass tiles, each with its picture. Under the one in the middle, the picker says
what it can do, where it plugs in and whether Indigo detected it; the stick,
the D-pad, L and R slide the row. See [Source](docs/guide/source.md).

**Game details are a surface, not a dialogue.** Artwork, last played, save
data, cheats and the boot options for that title in one place, with the same
controller grammar as every other screen. A bright frame shows where you are:
the D-pad or the stick moves it between Launch Game, Cheats and Settings, and
A selects.

<p align="center">
  <img alt="The game detail screen for LEGO Star Wars II: Y opens the cheat browser, three cheats are switched on and one switched back off, and the detail screen then reads 2 of 130 enabled." src="docs/screenshots/game-detail.png" width="640">
</p>

**Starting a game from the Library shows its cover**, not Swiss's progress
boxes. The cover sits in a ring that fills as Indigo checks, prepares and
loads the game, one line under the title says which step it's on, and the
screen fades to black as the game starts.

**Cheats read clearly.** The cheat browser shows per-cheat state plainly, and
the runtime handling around it is stricter about what it will apply.

**Settings opens on what you change between games**, and reads like the
cheat browser: cards with ON and OFF switches, a line that says what the
highlighted setting does, and the buttons that work shown as icons. Its
first tab, Quick, holds nine: menu music and sounds, UI motion, rumble,
In-Game Reset, the GameCube main menu, memory-card emulation, auto-loaded
cheats and booting without prompts. R moves to the Defaults tab: Game Defaults, what every game
starts with. R again moves to Setup, which holds video, console, storage,
network, library and developer options in six sections. X on a game's detail
screen, or Y on its cover in the Library, opens that game's own settings, where
anything that differs from Game Defaults is marked Custom and X puts it back.
Holding the D-pad scrolls, A changes a value
(or, for a setting with many choices, lists them all), and B leaves and keeps
your changes. A new video mode waits for A, and only stays if you press
A again within ten seconds. Settings can also be written ahead of time in a file on
the SD card: see [docs/SETTINGS.md](docs/SETTINGS.md). Setup › Storage says
whether that file loaded.

<p align="center">
  <img alt="Settings opens on Quick, its settings as cards with ON and OFF switches and a line above the buttons saying what the highlighted one does. R moves to Game Defaults and R again to Setup, whose six sections are listed with a summary each; A opens Display, its rows go by, and B returns to the list of sections." src="docs/guide/images/settings-tour.png" width="640">
</p>

<p align="center">
  <img alt="Y on 007: Agent Under Fire in the grid opens its own settings. A on Force Video Mode lists the modes; 480p is chosen, the row is marked Custom and the top right reads 1 custom. B shows the game's details, whose Settings line reads 1 custom and Force Video Mode: 480p; B again returns to the grid with the same game selected." src="docs/guide/images/library-game-settings.png" width="640">
</p>

**Indigo comes in eight colors.** Setup › Console › Menu Color recolors the
cube, its light, the panels and the text: Indigo, the default, or Azure,
Emerald, Gold, Spice, Crimson, Rose or Jet Black. Every color keeps Indigo's
brightness, and cover art, the button icons, warnings and enabled cheats keep
their own colors.

<p align="center">
  <img alt="The Home cube on its Library face in each Menu Color in turn: Indigo, Azure, Emerald, Gold, Spice, Crimson, Rose and Jet Black. The glass, its light, the waves behind it and the label change color; the green A button in the hint line stays green." src="docs/screenshots/colors.png" width="640">
</p>

A on Menu Color lists the colors, and the whole screen takes each one as you
move through the list. B keeps the color you had; A chooses the one you are on.

<p align="center">
  <img alt="Settings, Setup, Console: A on Menu Color opens a list of the eight colors with Indigo marked Current. Moving down, the whole screen turns Azure, Emerald, Gold, Spice, Crimson, Rose and Jet Black in turn; moving back up to Emerald and pressing A chooses it, and the row reads Emerald." src="docs/screenshots/color-menu.png" width="640">
</p>

Backdrop Color and Wave Color, under Menu Color, give the backdrop behind the
cube and the waves in front of it a color of their own, or let them follow
Menu Color as before: a Gold cube can sit over Azure waves on a Jet Black
backdrop.

<p align="center">
  <img alt="The Home cube in Gold on its Library face, over a Jet Black backdrop with Azure waves drifting behind it." src="docs/screenshots/layer-colors.png" width="640">
</p>

**Widescreen TVs get the whole screen.** Setup › Display › Menu Widescreen
draws Indigo for a TV set to 16:9: the background and its waves reach both
edges, and the cube, the text and every menu keep their shape in the middle.
Games have their own setting, Force Widescreen.

**Each face of the cube shows the picture you choose.** Setup › Console has a
row per face (Library Icon, Source Icon, Settings Icon and System Icon), and
each face has four icons of its own. Library has Controller, Books, Covers
and Play; Source has Hub, Disc, SD Card and Folder; Settings has Sliders,
Gear, Toggles and Dial; System has Clock, Info, Power and Chip. The time
and the temperature sit in the top right corner; Setup › Console › Clock
and Temperature each move theirs to the top left or hide it.

**Memory Cards moves your saves, like the GameCube's own screen.** System ›
Memory Cards shows two stacks of cubes side by side, Slot A's saves and Slot
B's, each cube with its game's animated icon. L and R choose storage;
with no physical cards, both columns open on SD. RAW virtual cards open to
browse and export their saves. A on a save shows its size, source and
recorded update date; A Actions opens Move, Copy and Erase. Move and Copy
send its cube flying to the other stack. Every copy is read back
before it counts, and a Move removes the original only then. Setup ›
Storage › Save Folder sets the folder the SD card's stack opens on
(`swiss/saves` until you choose one). See
[Memory Cards](docs/guide/memory-cards.md).

<p align="center">
  <img alt="Memory Cards with Demo Card.raw open on the left and an independent SD folder on the right. Copper Archive’s save cube is selected; its banner and two-block size appear below." src="docs/guide/images/memory-cards.png" width="640">
</p>

<a id="installation"></a>

## Install

**[Install Guide →](https://norvitech.com/indigo/guide/install/)** Pick what
starts your GameCube and follow four steps, with a picture of the card for
each setup.

| What starts your GameCube? | Copy the download's `ipl.dol` to | Indigo starts |
| --- | --- | --- |
| [GC Loader](https://norvitech.com/indigo/guide/install/?loader=gcloader) | `boot.dol` at the top of the card, keeping `boot.iso` (Swiss starts it) | When you switch on |
| [PicoBoot or PicoLoader](https://norvitech.com/indigo/guide/install/?loader=picoboot) | `ipl.dol` at the top of the card, replacing the old one | When you switch on |
| [FlippyDrive](https://norvitech.com/indigo/guide/install/?loader=flippy) | `boot.dol` at the top of its microSD card | When you switch on |
| [Something else, or not sure](https://norvitech.com/indigo/guide/install/?loader=other) | `apps/indigo.dol` (make `apps` if needed) | From Swiss |

First back up the card's `swiss` folder (Swiss hides it, so show hidden
folders) and the `ipl.dol` or `boot.dol` you replace. Then also copy the
download's `swiss/patches/apploader.img` into the card's `swiss/patches`
folder. Any setup can use `apps/indigo.dol` and open Indigo from Swiss. Some
PicoBoot and PicoLoader chips have Swiss built in and never read `ipl.dol`:
the [install guide](docs/guide/install.md) shows how to check, and has every
step, updating and going back to stock Swiss.

### Any platform — from source

The build runs in the same container image the project's CI uses, so no
toolchain is installed on your machine. `make dev` also downloads and
SHA-256-verifies the pinned libiconv-lite package into `.build-deps/` on the
first build; subsequent builds reuse it. CI preloads that same archive in
its runner image. The overlay needs no administrator access. Native builds
need curl, a SHA-256 utility (`sha256sum` or `shasum`) and tar with Zstd
support; the pinned Docker image includes them:


```bash
git clone https://github.com/spencernorton/indigo.git
cd indigo
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2 make dev
# writes cube/swiss/swiss.dol inside this folder, on your computer
```

The download above is this build at the release tag, packaged by
`buildtools/sd_package.sh`.

Upstream's packaging targets (`make all`, `make package`) are not supported
here: they need prebuilt tools and device firmware images that this fork does
not redistribute. `make dev` builds the executable, which is what the fork
changes.

`cube/swiss/swiss.dol` is the build output on your computer. Package it
with `buildtools/sd_package.sh dev`, then follow the install guide with the
ZIP's `ipl.dol`.

## Set up your library

### Games

The Library shows the games in one folder, `/games` at the root of the card.
Put each game in its own folder or put the disc images there directly:

```text
/games/Super Mario Sunshine [GMSE01]/game.iso
/games/Super Mario Sunshine.iso
```

Disc images end in `.iso`, `.gcm`, `.tgc` or `.fdi`. The Library skips
anything else there, such as a text file, a cover image or an empty folder.
With **Library Folders** off, if `/games` holds no disc images at all, the
Library face opens the File Browser, two panes of files, instead. With it
on, an empty `/games` stays in the Library. System › File Browser opens it on
purpose, with Swiss's copy, move, rename and delete. Programs belong in `/apps` (see [Apps](#apps)). On Home, turn
the cube to Library and press A.

To sort your games into folders, turn on Settings › Setup › Library ›
**Library Folders**: the Library then shows the folders in `/games` as cards
you open, two levels deep. See
[Folders of games](docs/guide/library.md#folders-of-games).

### Posters

Without posters, each game shows its disc banner and its six-character game
ID, such as `GMSE01`. For box art like the screenshots above, download
[the poster pack](https://indigo.norvitech.com/indigo-posters-all.zip) and
unzip it on your computer, then copy the files in its `swiss/ui` folder into
the card's `swiss/ui` folder (make it if it isn't there; Swiss hides its
`swiss` folder, so show hidden folders first). One pack covers games from
every region,
so there is nothing to choose; it holds `swiss/ui/posters.pak` and the stills
and descriptions Spotlight shows (below). Checksums are in
[SHA256SUMS.txt](https://indigo.norvitech.com/SHA256SUMS.txt); the
[Indigo page](https://norvitech.com/indigo/) has the details.

To make your own, name front-cover images after the game IDs (`GMSE01.png`,
`GALE01.jpg`, at least 192×256) in one folder, build a pack from this
repository, and copy it to `/swiss/ui/posters.pak` on the card:

```bash
python3 -m pip install pillow
docker pull ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2
python3 buildtools/ui/poster_pack.py --covers ~/covers --out posters.pak
```

Covers are cropped to 3:4 from the centre. Files not named by a game ID are
skipped and listed.

The Spotlight layout also shows each game's gameplay still from
`swiss/ui/stills.pak` and a few sentences about it from
`swiss/ui/descriptions.txt`, and the Jewel Case layout each game's disc
from `swiss/ui/discs.pak`; the poster pack carries all three beside the
posters. Copy `discs.pak` to `swiss/ui/discs.pak` on the games card too:
updating Indigo itself does not install the disc images. Restart Indigo
after updating the pack. Its covers, descriptions and discs come from
[GameTDB](https://www.gametdb.com/), and its stills from
[libretro-thumbnails](https://github.com/libretro-thumbnails/Nintendo_-_GameCube),
as do the screenshots above. The descriptions file is plain text you can
edit (see [Posters](docs/guide/posters.md#game-descriptions)).
To build stills from your own screenshots (at least 320×240, named like the
covers): `poster_pack.py --stills ~/screenshots --out stills.pak`; disc art
from pictures of the discs (at least 160×160):
`poster_pack.py --discs ~/discs --out discs.pak`. See
[Posters](docs/guide/posters.md#gameplay-stills).

### Apps

Put other programs (`.dol`, `.dol+cli` or `.elf`) in `/apps` at the root of
the card, loose or each in a folder of its own, and a picture beside each,
a PNG with the program's name. Home then gets an Apps face:

```text
/apps/gbi.dol
/apps/gbi.png
/apps/Genesis Plus GX/genplus_cube.dol
/apps/Genesis Plus GX/icon.png
```

A file called `boot.dol` is left out: in the Wii's Homebrew Channel layout it
is the Wii program. Settings › Setup › Console › Down Face set to None
takes the face off the cube. [Apps](docs/guide/apps.md) has the rest.

### Cheats

Indigo runs Gecko codes from `/swiss/cheats/<game ID>.txt`, or from
`<game ID>_v102.txt` and the like for one revision of a disc.
[Download the cheat pack](https://indigo.norvitech.com/indigo-cheats.zip)
(every region), unzip it on your computer and copy the files in its
`swiss/cheats` folder into the card's `swiss/cheats` folder (make it if it
isn't there; Swiss hides its `swiss` folder, so show hidden folders first),
then open a game and press Y. Cheats that the files show
would break Indigo are switched off and marked;
[the download page](https://norvitech.com/indigo/#downloads) says what can't
be checked in advance.

## Documentation

- [Indigo guide](docs/guide/README.md) — install, controls, and every screen and setting, with pictures.
- [`CHANGELOG.md`](CHANGELOG.md) — what each release contains.
- [`NOTICE`](NOTICE) — upstream provenance and the third-party components in this tree.
- [`UPSTREAM`](UPSTREAM) — the upstream Swiss commit Indigo is built on, and the few upstream files it changes.
- [`docs/screenshots/`](docs/screenshots) — the pictures above, captured in Dolphin.
- Upstream [Swiss documentation](https://github.com/emukidid/swiss-gc) covers every device handler, patch and boot option; none of it changed here.

## Contributing and support

- Bugs and feature requests: [open an issue](https://github.com/spencernorton/indigo/issues/new/choose). Questions: [Discussions](https://github.com/spencernorton/indigo/discussions). Do not report fork issues to the upstream project.
- Security reports: [private vulnerability reporting](https://github.com/spencernorton/indigo/security/advisories/new) — see [SECURITY.md](SECURITY.md). There is no e-mail address; that is deliberate.
- Pull requests are welcome: they go to the `beta` branch, and CI builds a test zip for each one. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; [docs/RELEASING.md](docs/RELEASING.md) says how betas and release candidates become releases.
- If Indigo saves you time, you can [support its development](https://buy.stripe.com/8x26oH2U44f65TRe574wM04).

## Development

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2 make dev   # the DOL, as CI builds it
buildtools/ui/tests/run_tests.sh all          # host tests: plain, sanitized, contracts
buildtools/check_whitespace.sh origin/beta    # lint
python3 buildtools/ci/check_upstream.py       # upstream's files match UPSTREAM
```

Work lands on `beta` by pull request and ships as `vX.Y.Z-beta.N` betas and
`vX.Y.Z-rc.N` release candidates, both pre-releases; a release candidate
that holds up is promoted to `main` as a release. See
[AGENTS.md](AGENTS.md) for the working detail.

## Licence

[GPL-2.0-or-later](LICENSE) © Spencer Norton

Indigo is a modified version of [Swiss](https://github.com/emukidid/swiss-gc)
(© emukidid and the Swiss contributors, GPL-2.0-or-later) and inherits that
licence. Provenance, the modified surface and the third-party components in
this tree are recorded in [`NOTICE`](NOTICE). This fork is unofficial and is
not endorsed by or affiliated with the Swiss project.

The Library's Spotlight layout takes its design from
[Gameplay Spotlight](https://github.com/mvizensk/gameplay-spotlight) by mvizensk
(GPL-2.0), with its author's permission; Indigo's code for it is its own.
The Jewel Case layout is ported from the jewel case games view of
portlandhodl's [Swiss](https://github.com/portlandhodl/swiss-gc) (GPL-2.0),
itself a port of [CD Case](https://www.bubbbly.com/cd-case) by Ann Nguyen.

Indigo's interface was built with [Claude Code](https://claude.com/claude-code).

---

<p align="center">
  <a href="https://norvitech.com"><img alt="Part of the NorviTech Suite — open-source apps for the Linux desktop and the self-hosted stack" src="https://norvitech.com/assets/banner.svg" width="640"></a>
</p>

<p align="center">
  <a href="https://github.com/spencernorton/helios">Helios</a> ·
  <a href="https://github.com/spencernorton/DH-Waypoint">DH-Waypoint</a> ·
  <a href="https://github.com/spencernorton/armiger">Armiger</a> ·
  <a href="https://github.com/spencernorton/xnote">XNote</a> ·
  <a href="https://github.com/spencernorton/xnote-placement">XNote Placement</a> ·
  <a href="https://github.com/spencernorton/snipsnap">SnipSnap</a> ·
  <a href="https://github.com/spencernorton/conductor">Conductor</a> ·
  <a href="https://github.com/spencernorton/norvi-os">NorviOS</a> ·
  <a href="https://github.com/spencernorton/indigo">Indigo</a> ·
  <a href="https://github.com/spencernorton/roadtrack">Road Track</a> ·
  <a href="https://norvitech.com">norvitech.com</a>
</p>
