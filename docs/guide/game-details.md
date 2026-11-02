[Indigo guide](README.md) › Game details

# Game details

A on a game in the [Library](library.md) opens its details: everything about
that one game, and every way to start it, on one screen. A on a game in the
left pane of the [File Browser](system.md#file-browser) opens the same
screen, and **B** there goes back to the File Browser, on the same row.

<p align="center">
  <img alt="Astral Circuit’s game details: its demonstration cover on the left; Last played says No play recorded, above Settings and Cheats. Launch Game is selected." src="images/game-details.png" width="640">
</p>

## What's on the screen

- **The cover**: the game's poster, or its disc banner if you have no
  [poster pack](posters.md).
- **Title and publisher**, from the disc's banner. Under them, notes when
  they apply: **Audio Streaming** for a game that plays music streamed from
  the disc, **Disc 2 Ready** when Indigo has found the game's second disc,
  and **Autoload** (below).
- **Last played**: the date you last started the game from Indigo, or "No
  play recorded". Indigo records it when your settings live on the card the
  game starts from, as with a single SD card. With no settings device to
  read it from, it says "History unavailable".
- **Saves**, above Settings and Cheats, when the game has two or more save
  copies, or at any count with **Saves on Details › Always**. The first line
  counts them as the GameCube does: different files
  of the game are files (**5 files | 58 blocks** for a game that keeps its
  town, letters and options apart), one file in more than one place is
  copies (**2 copies | 11 blocks**), and each file's blocks count once, as
  its newest copy has them. Beneath it, **Updated** is the newest date
  recorded in those saves, then that save's own line: the one the
  GameCube's memory card screen shows under the block count, such as "Last
  saved on 1/6". A save records no creation date. These totals come from
  inserted memory cards and the configured Save Folder, including readable
  RAW images there. With your games on another card than your settings (an
  SD2SP2 beside a GC Loader, say), they also count the card images and saves
  in `swiss/saves` on the games' card, where Swiss's Emulate Memory Card
  keeps each game's card. It matches the full game and maker ID, not the
  title; this isn't a progress score. A scan that couldn't read everything
  says **Partial scan**. Settings › Setup › Library › **Saves on Details**
  offers **On** (the default, two or more copies), **Always** (any count) and
  **Off** (the box is hidden and the details don't read your memory cards).
  System › [Memory Cards](memory-cards.md) opens the individual saves and
  their details.
- **Manual**, at the top of the rows, for a game whose manual is on the
  card: its page count. A on it opens the manual, at the width of the
  screen; Up and Down scroll, Left and Right turn the page, Y shows the
  whole page and B comes back here. Indigo comes with no manuals: see
  [Game manuals](../MANUALS.md) to convert your own.
- **Settings**: "Game Defaults" while the game follows them, or how many of
  its settings are its own and the first of them, such as "1 custom" and
  "Force Video Mode: 480p". See [below](#this-games-own-settings).
- **Cheats**: how many of the game's cheats are on and the names of the first
  ones, or "Y Choose cheats" when none are. It reads "No cheats found" when
  there's no cheat file for the game. See [Cheats](cheats.md).
- **Launch Game**, where the screen opens (below).
- **Shortcuts**: the extra ways to start or check the game (below).

With a memory card that changes to each game's own card, such as a MemCard
PRO GC, the box also shows while that card loads, however many copies there
are: **Memory card loading**, then the saves once it is ready. A card that
never finishes says **Memory card didn't load**, or **Partial scan** under
two or more copies. If the card's status remains unconfirmed for a minute,
Indigo stops waiting. Remove and reinsert the card to try again.

With **On**, a game with one save copy shows no Saves box. **Always** keeps
it visible: **1 save | 2 blocks**, with the recorded update and the save's
own line, or **No save copies found** when a completed scan finds none.
An incomplete scan with no saves found says **Save scan incomplete** instead.
This setting changes only what the box shows; card loading, scan timing and
the rules for choosing a copy stay the same.

Copying a save from a RAW image into the Save Folder makes a second copy of
that file, and opening the game's details again shows it with On too:
**2 copies | 2 blocks**, with the recorded update and the save's own line.

<p align="center">
  <img alt="Astral Circuit’s details after exporting one GCI: the Saves box, labelled « SAVES » since Left and Right choose a copy, reads 2 copies | 2 blocks over Updated 2024-02-29 12:34 and the save's own line, SD card image." src="images/game-details-saves-after-copy.png" width="640">
</p>

### Start with another copy of the save

With two or more copies and a memory card in a slot, **Left** and **Right**
step through them, and the label reads **« SAVES »**. The box shows one copy
at a time, such as **Copy 2 of 3 | Save Folder**, with its date, **In use**
for the copy on the memory card the game reads or **Loads at launch**, and
the save's own line. A card image goes by its file name, such as
**MemoryCardA.USA.raw**, and a save file in the games' card's `swiss/saves`
by that card's name, such as **SD Card - SD2SP2**. When the copies are of
more than one of the game's files, each shows its file's name and its place
among that file's copies instead, such as **DobutsunomoriP_MURA 2 of 2 |
MemoryCardA.USA.raw**, and each file's copies come together. After the last
copy the totals show again: launching then leaves the card as it is. The
card's own copies come first, then the SD cards', newest first; with more
than eight, the oldest of those aren't offered.

<p align="center">
  <img alt="Astral Circuit’s details after Right twice: « SAVES » shows one copy, Copy 2 of 2 | Save Folder, over Loads at launch | 2024-02-29 12:34 | SD card image." src="images/game-details-save-choice.png" width="640">
</p>

Launch the game with another copy shown and Indigo asks first, naming the
file when the game has more than one. **A** puts that copy on the memory
card: the card's own copy of that file, if it has one, goes to the Save
Folder first (on your settings card, wherever the new copy came from), the
new one is read back from the card,
and if anything goes wrong the card's own copy goes back and the game
doesn't start. **B** leaves the card as it is and shows the totals
again. The copy goes on the card that holds the game's save, Slot A first;
with no copy on a card, on the card in Slot A, or in Slot B when Slot A is
empty.

It isn't offered with no memory card in a slot, or while **Emulate Memory
Card** is on, since the game then reads a card image on the SD card rather
than the card in the slot. Nor is it offered for a memory card that changes
to each game's own card, such as a MemCard PRO GC: Indigo only reads that
card, once it has loaded this game's card, and the game starts with the
card's own copy.

## Start the game

The screen opens with a bright frame on **Launch Game**: press **A** to
start the game. The details make way for the game's cover, centred in a
ring that fills as Indigo applies the game's settings and your cheats,
prepares the game and loads it. One line under the title says which step
it's on, such as "Checking game" or "Loading game". If the game's patches
live on another card than the game, that card is named under it ("Do not
remove SD Card - SD2SP2"): leave it in until the game is running. When the
ring is full the screen fades to black and the game starts. With
[UI Motion](personalize.md#motion) set to Off, the ring fills in steps
without its glint, and the screen goes straight to black.

<p align="center">
  <img alt="A on Launch Game for 1080° Avalanche: the details have made way for its cover, centred in a ring that is filling, with the title, the publisher and the step under it, Loading game." src="images/game-details-launch.png" width="640">
</p>

If the game can't start, a message says why, and then the Library (or the
File Browser) comes back.

Up and down on the D-pad or the control stick move the frame between
**Launch Game**, **Cheats** and **Settings**, and **A** opens the one it's
on. Cheats is passed over when the game has none. The other buttons on this
page (X, Y, Z, R, L + A and B) work wherever the frame is, and the frame is
still where you left it when you come back from Settings or Cheats.

**L + A** is a clean boot, shown when the game is in the disc drive (or a
drive replacement that sits in its place). The console resets and starts the
game the normal way, with nothing changed: no patches, cheats or forced
video modes. Region restrictions apply. If a game should always start this
way, turn on **Prefer Clean Boot** in its own settings; Launch Game then
reads **Clean Boot**.

To start games straight from the Library without this screen, turn on
**Boot without prompts** in Settings › Quick; the cover and its ring show
all the same. Hold B while you choose a game to see its details anyway; that
turns Boot without prompts off until you restart.

## This game's own settings

**X** opens settings that apply to this game only: its video mode,
widescreen, language, polling rate and the rest. In the
[Library](library.md#moving-around), **Y** on the game's cover opens them
straight away, and leaving them shows these details. Anything you change is marked
**Custom**, and the detail screen's **Settings** line counts them and names
the first, for example "1 custom" and "Force Video Mode: 480p". With none, it
reads "Game Defaults". In the Library, a game with any Custom setting shows
a small sliders mark in the corner of its cover.

<p align="center">
  <img alt="X on the detail screen for 1080° Avalanche opens its own settings. A on Force Video Mode lists the modes; 480p is chosen and the row is marked Custom. B returns to the detail screen, whose Settings line now reads 1 custom and Force Video Mode: 480p." src="images/detail-game-settings.png" width="640">
</p>

In a game's own settings, **X** puts the highlighted setting back to the
Game Defaults value, and **B** leaves. The rest works like the main
[Settings](settings.md#a-games-own-settings).

## Autoload

**Z** marks the game to open every time Indigo starts, so it is the first
thing you see, on Swiss's own game screen. With **Boot without prompts** on,
the game starts straight away instead. The shortcut then reads **Z Autoload On**; press Z
again to turn it off.

Autoload is saved in your settings file, so it appears only when Indigo has
a device to save to.

## Verify

**R** reads the whole disc image, compares it with the known good copy of
that disc and tells you whether it passed. It appears for discs Indigo can
check. Use it when a game crashes or won't start, to rule out a damaged file.

---

<p align="center"><a href="library.md">← Library</a> · <a href="cheats.md">Cheats →</a></p>
