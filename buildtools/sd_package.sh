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
#     swiss/indigo/cubebrowser.dol   the web browser the Web face launches (if bundled)
#     gcbrowser/cacert.pem           the browser's trusted certificates (if bundled)
#     swiss/indigo/LICENSE-mbedtls.txt  the licence of mbed TLS, built into the browser
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
# Web browser (optional): a separate program the Web face launches, built from
# the hardened fork (docs/dev/web-browser.md). Its CA bundle goes in gcbrowser/
# at the card root, where the browser looks for it. Without it the Web face
# reports the browser isn't installed.
browser_dol=${BROWSER_DOL:-browser/cubebrowser.dol}
browser_gcbrowser=${BROWSER_GCBROWSER:-browser/gcbrowser}
# mbed TLS is built into the browser, and its licence goes with it.
browser_mbedtls_license=${BROWSER_MBEDTLS_LICENSE:-browser/LICENSE-mbedtls.txt}
browser_zip=
if [ -f "$browser_dol" ]; then
	if [ ! -f "$browser_mbedtls_license" ]; then
		echo "sd_package: $browser_dol needs mbed TLS's licence at $browser_mbedtls_license" >&2
		exit 1
	fi
	cp "$browser_dol" "$card/swiss/indigo/cubebrowser.dol"
	cp "$browser_mbedtls_license" "$card/swiss/indigo/LICENSE-mbedtls.txt"
	if [ -d "$browser_gcbrowser" ]; then
		mkdir -p "$card/gcbrowser"
		cp -R "$browser_gcbrowser/." "$card/gcbrowser/"
		browser_zip=gcbrowser
	fi
	echo "sd_package: bundled the web browser ($browser_dol)"
else
	echo "sd_package: no web browser at $browser_dol; the Web face will report it is not installed" >&2
fi
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
       Something else, or not sure: rename it indigo.dol and put it in
         the card's apps folder (make it if it isn't there). Your
         console starts as it does today.
     Any setup can use apps/indigo.dol and open Indigo from Swiss. Some
     PicoBoot and PicoLoader chips have Swiss built in and never read
     ipl.dol: the guide shows how to check.
   Copy files, not the whole swiss folder: on a Mac, Replace on a folder
   deletes the settings and saves inside it.
4. Put the card back and switch on. If you put Indigo at the top of the
   card as ipl.dol or boot.dol, it starts. Otherwise start Swiss as usual
   and open the card (on FlippyDrive: FlippyDrive, not FlippyDrive Flash),
   then apps, then indigo.dol.

UPDATING INDIGO
Back up the card's swiss folder. Rename the new ipl.dol to the name of the
Indigo file you start now (ipl.dol, boot.dol or indigo.dol) and copy it
over that file. Then copy the new swiss/patches/apploader.img over the old
one. If you use the Web face, copy the new cubebrowser.dol and gcbrowser
folder too (see WEB BROWSER).

WHAT EACH FILE IS FOR
  ipl.dol                      Indigo itself (renamed for some setups)
  swiss/patches/apploader.img  In-Game Reset returns to Indigo (see below)
  swiss/ui/                    the poster pack goes here, if you add it
  swiss/indigo/                licences, notices and the web browser
  gcbrowser/                   the web browser's certificates

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

WEB BROWSER
If this download has swiss/indigo/cubebrowser.dol and a gcbrowser folder,
copy both: cubebrowser.dol into the swiss/indigo folder of the card that
holds Indigo's settings (make the folder if it isn't there), and the
gcbrowser folder to the top of the card beside ipl.dol. The Web face on
the Home cube (give it a side under Settings > Setup > Console) opens the
browser; it needs a network adapter. The browser reads gcbrowser from an
SD2SP2, an SD Gecko or a GC Loader, not from a FlippyDrive's card: with a
FlippyDrive, put gcbrowser on a card in an SD2SP2 or SD Gecko. With only a
FlippyDrive, secure (https) pages don't open and the browser saves nothing.
To leave the browser, select Return to Indigo at bottom-left and press A
once. It restarts the Indigo installed on the card. A network/Apps test
build does not replace that startup file.

IN-GAME RESET
With In-Game Reset set to Apploader (Settings > Quick), A + Z + START
during a game returns to Indigo: swiss/patches/apploader.img is Indigo.
It replaces stock Swiss's copy, so a game started from stock Swiss returns
to Indigo too. Reboot resets the console, back to whatever your loader boots.
TXT

rm -f "$out/Indigo-$version.zip"
# Directory entries included, so the ZIP shows the card's folders.
(cd "$card" && zip -qrX "$out/Indigo-$version.zip" Indigo-README.txt ipl.dol swiss ${browser_zip})
shasum -a 256 "$out/Indigo-$version.zip"
