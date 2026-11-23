#!/bin/sh
# Assemble the download for one release. Its swiss/ files keep their card
# paths; ipl.dol's name and place depend on what starts the console.
#   Indigo-<version>.zip
#     Indigo-README.txt              what goes where, in plain words
#     ipl.dol                        Indigo; rename for your chosen boot route
#     swiss/patches/apploader.img    In-Game Reset (Apploader) returns to Indigo
#     swiss/ui/                      posters.pak, if you add one
#     swiss/indigo/LICENSE.txt, NOTICE.txt
#     swiss/indigo/uk.po             Menu Language Ukrainian's words
# Usage: buildtools/sd_package.sh <version> [swiss.dol] [out dir]
# Run `make dev` first (see README, Install); run from the repository root.
set -eu

version=${1:?usage: buildtools/sd_package.sh <version> [swiss.dol] [out dir]}
dol=${2:-cube/swiss/swiss.dol}
out=$(cd "${3:-.}" && pwd)
[ -f "$dol" ] || { echo "sd_package: no DOL at $dol; build it first" >&2; exit 1; }
reboot=cube/packer/reboot.dol
[ -f "$reboot" ] || { echo "sd_package: no $reboot; build it first (make dev)" >&2; exit 1; }

card=$(mktemp -d)
trap 'rm -rf "$card"' EXIT
# No games/: a Mac's Replace would swap the card's own games folder for an empty one.
mkdir -p "$card/swiss/ui" "$card/swiss/patches" "$card/swiss/indigo"
cp "$dol" "$card/ipl.dol"
# In-Game Reset set to Apploader restarts whatever this file holds: Indigo.
python3 buildtools/dol2ipl.py "$card/swiss/patches/apploader.img" "$reboot" "*indigo-$version" >/dev/null
cp LICENSE "$card/swiss/indigo/LICENSE.txt"
cp NOTICE "$card/swiss/indigo/NOTICE.txt"
cp docs/licenses/lwip.txt "$card/swiss/indigo/LICENSE-lwip.txt"
# Menu Language Ukrainian's words, and the licence of the font its letters
# are drawn from.
cp cube/swiss/source/gui/lang/uk.po "$card/swiss/indigo/uk.po"
cp docs/licenses/noto-sans-ofl.txt "$card/swiss/indigo/LICENSE-NotoSans.txt"
cat > "$card/Indigo-README.txt" <<TXT
Indigo $version - an unofficial fork of Swiss with the interface rebuilt
Guide:  https://norvitech.com/indigo/
Source: https://github.com/spencernorton/indigo/tree/$version

INSTALL
The install guide shows each setup step by step, with a picture of the card:
https://norvitech.com/indigo/guide/install/

1. Unzip this download on your computer.
2. Back up the card: copy its swiss folder to your computer, and the
   ipl.dol or boot.dol you are about to replace. Swiss hides its swiss
   folder: show hidden folders first (Shift-Command-Period in a Mac's
   Finder; in Windows File Explorer, turn on Hidden items and File name
   extensions under View).
3. Copy two files onto the card:
   - swiss/patches/apploader.img, into the card's swiss/patches folder,
     replacing the old one. Make the folders if they aren't there.
   - ipl.dol, named and placed for what starts your GameCube:
       GC Loader: rename it boot.dol and put it at the top of the card,
         keeping boot.iso. Swiss starts it when you switch on.
       PicoBoot or PicoLoader: ipl.dol at the top of the card, replacing
         the old one. Indigo then starts when you switch on.
       FlippyDrive: rename it boot.dol and put it at the top of its
         microSD card. Indigo then starts when you switch on.
       Something else, or not sure: rename it default.dol and put it in
         apps/indigo/ on the card (make the folders if needed). Your
         console starts as it does today, or install via Dolphin Depot.
     Any setup can use apps/indigo/default.dol (or apps/indigo.dol) and open
     Indigo from Swiss. Some PicoBoot and PicoLoader chips have Swiss built in
     and never read ipl.dol: the guide shows how to check.
   Copy files, not the whole swiss folder: on a Mac, Replace on a folder
   deletes the settings and saves inside it.
4. Put the card back and switch on. If you put Indigo at the top of the
   card as ipl.dol or boot.dol, it starts. Otherwise start Swiss as usual
   and open the card (on FlippyDrive: FlippyDrive, not FlippyDrive Flash),
   then apps, then indigo.

UPDATING INDIGO
Back up the card's swiss folder. Rename the new ipl.dol to the name of the
Indigo file you start now (ipl.dol, boot.dol, apps/indigo/default.dol or
apps/indigo.dol) and copy it over that file. Then copy the new
swiss/patches/apploader.img over the old one.

WHAT EACH FILE IS FOR
  ipl.dol                      Indigo itself (renamed for some setups)
  swiss/patches/apploader.img  In-Game Reset returns to Indigo (see below)
  swiss/ui/                    the poster pack goes here, if you add it
  swiss/indigo/                licences, notices and Menu Language files

GAMES
Put your games in /games, either one folder per game or the disc images
directly:
    /games/Super Mario Sunshine [GMSE01]/game.iso
    /games/Super Mario Sunshine.iso
Disc images end in .iso, .gcm, .tgc or .fdi. The Library skips anything
else there (a text file, a cover image, an empty folder). With Library Folders
off, if /games holds no disc images, you get the File Browser, two panes of
files, instead.
With Library Folders on, an empty /games stays in Indigo.
On Home, turn the cube to Library and press A.
To sort your games into folders, turn on Settings > Setup > Library >
Library Folders: the Library then shows the folders in /games, two levels
deep. A opens a folder and B goes back. Keep a game's discs together. A PNG
beside a folder with its name (Nintendo.png for Nintendo) is its poster.

POSTERS AND CHEATS (optional)
Without posters, each game shows its disc banner and its six-character game
ID (GMSE01). The poster pack and the cheat pack are on https://norvitech.com/indigo/
- unzip one on your computer and copy the files in its swiss/ui (posters)
or swiss/cheats (cheats) folder into the card's folder of the same name,
making it if it isn't there. For Jewel Case's actual disc images, include
swiss/ui/discs.pak on the card holding the games (even if Indigo starts
from another card). Updating Indigo itself does not add the disc pack.
Restart Indigo after adding or replacing artwork packs.

IN-GAME RESET
In-Game Reset defaults to Apploader (Settings > Quick), so A + Z + START
during a game returns to Indigo out of the box when swiss/patches/apploader.img
is on the card. It replaces stock Swiss's copy, so a game started from stock
Swiss returns to Indigo too. Reboot resets the console, back to whatever your
loader boots.
TXT

rm -f "$out/Indigo-$version.zip"
# Directory entries included, so the ZIP shows the card's folders.
(cd "$card" && zip -qrX "$out/Indigo-$version.zip" Indigo-README.txt ipl.dol swiss)
shasum -a 256 "$out/Indigo-$version.zip"
