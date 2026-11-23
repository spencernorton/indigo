[Indigo guide](README.md) › Troubleshooting

# Troubleshooting

- [I see the File Browser instead of the Library](#i-see-the-file-browser-instead-of-the-library)
- [Home starts on Source, or Library says Select Source](#home-starts-on-source-or-library-says-select-source)
- [Some games have no box art](#some-games-have-no-box-art)
- [A message says the file is a bad dump](#a-message-says-the-file-is-a-bad-dump)
- [The screen went dark after I changed a video mode](#the-screen-went-dark-after-i-changed-a-video-mode)
- [A game's picture is stretched, shifted or wrong](#a-games-picture-is-stretched-shifted-or-wrong)
- [My settings don't stick](#my-settings-dont-stick)
- [My cheats don't show up, or don't work](#my-cheats-dont-show-up-or-dont-work)
- [Indigo starts, then stock Swiss appears](#indigo-starts-then-stock-swiss-appears)
- [In-Game Reset takes me to stock Swiss](#in-game-reset-takes-me-to-stock-swiss)
- [The same game opens every time Indigo starts](#the-same-game-opens-every-time-indigo-starts)
- [There's no Apps face, or an app or its picture is missing](#theres-no-apps-face-or-an-app-or-its-picture-is-missing)
- [Still stuck](#still-stuck)

<a id="i-see-swisss-file-list-instead-of-the-library"></a>

## I see the File Browser instead of the Library

<p align="center">
  <img alt="The File Browser's two panes of files, which the Library face opens when it has no games to show." src="images/files.png" width="640">
</p>

The File Browser, two panes of files, is part of Indigo: System ›
[File Browser](system.md#file-browser) opens it on purpose. The Library face
shows the poster Library when `/games` holds at least one disc image
(`.iso`, `.gcm`, `.tgc`, `.fdi`), directly or in a game's folder, and it
skips anything else there. It opens the File Browser instead when:

- `/games` holds no disc images, for example only compressed `.rvz` or `.gcz`
  images, which must be decompressed with NKit or Dolphin first.
- Your games are on another card or drive: choose it on the Source face.
- **Flatten directory** in Settings › Setup › Library was changed from
  `*/games`, and `/games` holds loose disc images beside game folders. Set it
  back to `*/games`.

See [Set up the games folder](library.md#set-up-the-games-folder).

## Home starts on Source, or Library says Select Source

Indigo didn't find a device to read games from when it started. Check that
the SD card or drive is seated, then choose it on the Source face: see
[Change the source](source.md#change-the-source). In the source picker, **Z**
also lists the devices that weren't detected, and A on one tries again.

## Some games have no box art

A game shows its banner card when its game ID isn't in your poster pack.
The card shows the ID, so you can check:

- GameTDB has no cover for it, or it's a homebrew or translated disc with an
  ID of its own: [build your own pack](posters.md#build-your-own-pack).
- The card still has a pack from before Indigo 2.1, which covered one region:
  replace it with [the poster pack](posters.md#download-the-pack), which
  covers every region.
- Every game shows its banner: check that `/swiss/ui/posters.pak` is there
  and that Indigo is 2.1 or later. 2.0 and earlier refuse a pack this big.

## A message says the file is a bad dump

<p align="center">
  <img alt="A message over the Library: File is a bad dump, but may be playable. Please attempt recovery using NKit." src="images/troubleshoot-bad-dump.png" width="640">
</p>

Swiss knows the exact size of each GameCube disc. "File is a bad dump, but may
be playable" means your image is a different size, so something is probably
missing or damaged. It shows for five seconds, then the game opens anyway.
"File is a bad dump and is not playable" stops the game from opening. Try
NKit's recovery, as the message suggests, or make a fresh copy of the disc.
[Verify](game-details.md#verify) (R on game details) checks an image's data.

## The screen went dark after I changed a video mode

Wait ten seconds. When A switches **Swiss Video Mode**, **System Video**,
**AVE Compatibility**, **Force DTV Status** or **RetroTINK-4K HDMI Input**
to a new value, Indigo puts the old mode back by itself unless you press A
again to keep it. See
[Video changes ask first](settings.md#video-changes-ask-first).

If a game starts in a mode your TV can't show, open the game's details, press
X, highlight **Force Video Mode** and press X to put it back to the default.

## A game's picture is stretched, shifted or wrong

Look at the game's own settings (X on its details) for anything marked
**Custom**: **Force Widescreen**, **Force Horizontal Scale** and **Force
Vertical Offset** change the picture. X puts a setting back to Game Defaults.
If every game looks wrong, check Game Defaults instead (Settings, the
Defaults tab).

## My settings don't stick

Open Settings › Setup › Storage and read the line under the title:

- **"No device to save settings to."** Indigo found nowhere to save. Check
  your SD card, then set **Configuration Device** and leave with Save &
  Exit.
- **"No swiss/settings/global.ini yet: using defaults."** There's no file
  yet, or it couldn't be read. Leaving Settings with B or Save & Exit
  writes one.
- **"Settings are saved in swiss/settings/global.ini."** Saving works. If a
  setting you typed into the file on a computer is ignored, check its spelling
  against [Settings files](../SETTINGS.md): keys and values are
  case-sensitive, and only `Yes` turns a setting on.

Leaving with **Discard & Exit** throws away the changes you made.

## My cheats don't show up, or don't work

- **"No cheats found" on game details:** the file must be
  `/swiss/cheats/<game ID>.txt`, named with the six-character ID exactly.
  The [cheat pack](cheats.md#get-cheat-files) has them for every region.
- **The cheats don't apply:** cheats apply when you open the cheat browser
  before starting the game. To have them apply every time, turn on
  **Auto-load cheats** in Settings › Quick.
- **"Not enough room. Turn off another cheat first.":** the active cheats have
  filled the reserved cheat space. Turn some off; press Z in the cheat browser
  to see how much is used.

## Indigo starts, then stock Swiss appears

Indigo 1.25.0 and earlier start a stock Swiss they find in the root of the
card as `z.dol`, `a.dol`, `b.dol`, `x.dol`, `y.dol`, `start.dol`, `boot.dol`
or `swiss_r….dol`, in place of themselves, and In-Game Reset lands there too.
Update Indigo: later releases don't look for them, so stock Swiss can stay on
the card as `z.dol`.

## In-Game Reset takes me to stock Swiss

In-Game Reset set to **Apploader** returns to whatever is in
`/swiss/patches/apploader.img`. Copy that file from the zip of the Indigo
you run: the one stock Swiss ships is stock Swiss. **Reboot** resets the
console instead, which returns to Indigo only when your loader boots it
(GC Loader boots `boot.iso`).

## The same game opens every time Indigo starts

That game is set to Autoload. Press B to go to the Library, open the game
again and press **Z**: the shortcut changes from "Autoload On" back to
"Autoload".

## There's no Apps face, or an app or its picture is missing

The Apps face shows only when `/apps`, at the root of the source, holds a
program: a `.dol`, `.dol+cli` or `.elf` that isn't called `boot.dol` (the
Wii's), including Dolphin Depot's `/apps/<app>/default.dol` packages, and
Settings › Setup › Console puts Apps on a side (Down Face, by default).
Programs two folders deep, hidden ones and names starting with a dot
are left out. A picture can be a PNG with the program's name, its
folder's `icon.png` (up to 2048 pixels a side and 2 MB, not interlaced),
or its folder's GameCube `opening.bnr` banner.
[Apps](apps.md#when-apps-doesnt-look-right) has the details.

## Still stuck

- Questions: [Discussions](https://github.com/spencernorton/indigo/discussions).
- Bugs: [open an issue](https://github.com/spencernorton/indigo/issues/new/choose),
  with the version you downloaded, or the commit System › System
  Information › About Indigo shows, your loader and what you pressed.
- Report problems with Indigo here, not to the Swiss project: Indigo is an
  unofficial fork.

---

<p align="center"><a href="apps.md">← Apps</a> · <a href="README.md">Guide</a></p>
