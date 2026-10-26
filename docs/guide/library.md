[Indigo guide](README.md) › Library

# Library

The Library shows the games on your card as posters: in a row, in a column
or in a grid, or as a gameplay still over a row of disc banners, as you
choose. On Home, turn the cube to **Library** and press A.

<p align="center">
  <img alt="The Library: moving right through the James Bond games to 1080° Avalanche, each cover raised in turn between two covers either side, with its title and publisher below; A opens its details, B returns to the Library, and 1080° Avalanche is still selected." src="images/library-browse.png" width="640">
</p>

For a game with two or more save copies, the [game details](game-details.md)
screen also shows **Saves** above Settings and Cheats: its files or copies,
total blocks and the latest recorded update date from memory cards and the
Save Folder, including RAW images, and from `swiss/saves` on the games' card
when your settings are on another. Incomplete scans are marked. Settings ›
Setup › Library › **Saves on Details** also offers **Always**, to keep the
box visible with one copy or none, and **Off**, to stop reading the cards.
Open System ›
[Memory Cards](memory-cards.md) for an individual save’s size, date and
actions.

## Choose a layout

**Library Layout**, the first row of [Settings](settings.md) › Setup ›
Library, sets how the Library lays out your games. Left and Right on it step
through the five, A lists them, and the line above the buttons says how each
one moves.

<p align="center">
  <img alt="Settings, Setup, Library: Library Layout, the first row, reads Horizontal, with Library Folders, Saves on Details, Recent List, Load at startup and Flatten directory below it; the line above the buttons reads A row of covers; Left and Right move (default)." src="images/library-layout-setting.png" width="640">
</p>

- **Horizontal**, the default: a row of covers, two either side of the
  game in the middle. That one is raised, with its title and publisher
  underneath, as the disc's own banner gives them.
- **Vertical**: a column of covers down the left of the screen, turning like
  a wheel. The selected cover is large, with its title, publisher and game ID
  beside it.
- **Grid**: rows of five covers, three rows on screen. The selected cover is
  lit and a little larger, and its title and publisher are shown above the
  controls.
- **Spotlight**: the selected game's gameplay still, large, with its title,
  publisher, a description and its game ID beside it, over a row of disc
  banners: the selected game's in the middle, a little larger, and two
  either side. The still comes from the
  [stills pack](posters.md#gameplay-stills); a game without one shows its
  cover there, or its banner card. The description comes from the
  [descriptions file](posters.md#game-descriptions), else from the game's
  disc banner. Spotlight's design comes from mvizensk's
  [Gameplay Spotlight](https://github.com/mvizensk/gameplay-spotlight), with their
  permission.
- **Jewel Case**: one game at a time as a clear CD jewel case, its disc
  spinning inside, and its title and publisher underneath. The disc is the
  game's own, from the [disc art](posters.md#disc-art) in the poster pack;
  without it the disc carries the middle of the game's cover, and without a
  cover it is bare silver. Moving to the next game, the case falls back as
  the next one swings in from the side. The C-stick turns the case in your
  hands; let go and it springs back. Z stops the disc spinning, and Z again
  starts it; Indigo remembers which. The jewel case comes from the jewel
  case games view of portlandhodl's
  [Swiss](https://github.com/portlandhodl/swiss-gc), itself after Ann
  Nguyen's [CD Case](https://www.bubbbly.com/cd-case).

<p align="center">
  <img alt="The Jewel Case layout: a clear CD jewel case in the middle of the screen, tilted a little, holding the demonstration game Dune Rally's disc, green with rings, a pale block and a row of small marks, turning slowly, with GAME LIBRARY above and the game's title and publisher below. Right swaps it: the case falls back to the left and fades as Ember and Ivy's case swings out to the right and lands in front, its disc printed with the middle of the game's blue cover, as the pack has no disc for it; Right again brings in Glacier Point's magenta disc the same way." src="images/library-jewel-case.png" width="640">
</p>

<p align="center">
  <img alt="The Spotlight layout, moving right through four games and back one: each game's gameplay still fills a framed panel at the left, with its title, publisher, a few sentences about it and its game ID and size beside it; below, a row of disc banners, the selected one in the middle a little larger and framed brighter. James Bond 007 in Agent Under Fire, 007: Everything or Nothing, From Russia With Love, 007: NightFire and 1080° Avalanche in turn, each still fading into the next; the controls are shown as GameCube buttons along the bottom." src="images/library-spotlight.png" width="640">
</p>

<p align="center">
  <img alt="The Vertical layout: the selected cover large at the left, the covers before and after it tipped back above and below, and its title, publisher, game ID and size beside it. Down moves through 007: Everything or Nothing, From Russia With Love, NightFire and 1080° Avalanche; Right jumps nine games to The Legend of Zelda: Ocarina of Time, and Down moves on to The Wind Waker." src="images/library-vertical.png" width="640">
</p>

<p align="center">
  <img alt="The Grid layout: five covers across and three rows on screen, the selected cover lit by a bright frame and a little larger, with its title and publisher above the controls. The frame slides right along the row, the rows scroll as it moves down and back up, and R moves three rows at a time." src="images/library-grid.png" width="640">
</p>

Every layout wraps round: after the last game comes the first.

## Moving around

| To move | Horizontal, Spotlight, Jewel Case | Vertical | Grid |
| --- | --- | --- | --- |
| To the next or previous game | Left, Right | Up, Down | Left, Right |
| To the game above or below | | | Up, Down |
| A page at a time | Up, Down, L, R | Left, Right, L, R | L, R |

The control stick moves along the layout too, and in the grid in every
direction; in Jewel Case the C-stick turns the case. A page is nine games,
or three rows of the grid; it stops at the first or last game (or row)
before it wraps round.

In the grid, Left and Right run on from the end of one row to the start of
the next. Up and Down keep to the column. The last row can be short: from a
column it doesn't have, Up or Down lands on its last game.

- **A** opens the game's [details](game-details.md), where you launch it.
- **Y** opens the game's [own settings](game-details.md#this-games-own-settings),
  as X does on its details. When you leave them you are on the game's
  details, and B takes you back to the Library. Y never starts the game,
  even with **Boot without prompts** on.
- **B** goes back to Home.
- A small **sliders mark** in the corner of a cover means that game has
  [settings of its own](game-details.md#this-games-own-settings).

<p align="center">
  <img alt="Y on 007: Agent Under Fire in the grid opens its own settings. A on Force Video Mode lists the modes; 480p is chosen, the row is marked Custom and the top right reads 1 custom. B shows the game's details, whose Settings line reads 1 custom and Force Video Mode: 480p; B again returns to the grid with the same game selected." src="images/library-game-settings.png" width="640">
</p>

The Library remembers where you were. Come back from a game's details, or
from Home, and the same game is still selected.

## Set up the games folder

The Library shows the games in one folder, `/games` at the root of the card
(or of whichever device you chose on [Source](source.md)). Put each game in
its own folder, or put the disc images there directly:

```text
/games/Super Mario Sunshine [GMSE01]/game.iso
/games/Super Mario Sunshine.iso
```

Disc images end in `.iso`, `.gcm`, `.tgc` or `.fdi`, in capitals or not.
The Library skips anything else in `/games` or in a game's folder, such as a
text file, a cover image or an empty folder.
Mac metadata files whose names start with `._` are skipped too. Hidden files
in a game's folder follow **Show hidden files**, as they do in the
[File Browser](system.md#file-browser).

<p align="center">
  <img alt="The File Browser with the games folder open in its left pane: a cube, the file name and the size on each row." src="images/files.png" width="640">
</p>

If `/games` holds no disc images, the Library face opens the File Browser,
above, instead of posters. System › [File Browser](system.md#file-browser)
opens it whenever you want it.
Compressed images (`.rvz`, `.gcz`) don't count: Swiss can't start them, and
choosing one tells you to decompress it with NKit or Dolphin.

Games on two discs appear twice, once for each disc.

## Folders of games

With **Library Folders** off (the default), the Library is one list of every
game in `/games`, however you have sorted them into folders there. To browse
by folder instead, turn on Settings › Setup › Library › **Library Folders**.
The Library then shows the folders in `/games` as cards beside the games:

```text
/games/Pikmin.iso
/games/Nintendo/Super Mario Sunshine [GMSE01]/game.iso
/games/Nintendo/Zelda/The Wind Waker.iso
/games/Nintendo/Zelda/Older/Ocarina of Time.iso
```

Here `/games` shows Pikmin and a **Nintendo** folder; Nintendo shows Super
Mario Sunshine and a **Zelda** folder.

- **A** on a folder opens it. The heading names the folder you are in, such
  as `NINTENDO / ZELDA`.
- **B**, **X**, or A on the parent card goes back up one folder. From
  `/games`, each returns directly to Home; opening Library again keeps
  the current game or folder. Returning from the parent card starts at
  the first game or folder on your next visit.
- Folders can be nested two levels deep. At the second level, Indigo shows all
  games below that folder together, including games in deeper subfolders. In
  the example above, Zelda shows both The Wind Waker and Ocarina of Time.
- A folder named like a game, `Title [GAMEID]`, is a game, not a folder of
  games: it shows as that game, as it does in `/games`.
- Keep a game's discs in the same folder. Indigo looks for the other disc
  next to the one you start.
- A folder with no games in it shows only the way back. This includes an
  empty `/games` while Library Folders is on.
- Folder names can contain periods, such as `Nintendo.GC` or `Racing.v1`.
  Names beginning with a period follow **Show hidden files**.

While Library Folders is on, it sets **Flatten directory** itself, and the
row can't be changed. Turn Library Folders off and your own value comes
back.

### Give a folder a picture

Put a PNG beside the folder, with the folder's name: `Nintendo.png` in
`/games` for the Nintendo folder, `Zelda.png` in `/games/Nintendo` for Zelda.
Keep periods in the full name too: `Nintendo.GC.png` for `Nintendo.GC`.
It becomes the folder's poster, made on the console the first time the
folder is shown, as an app's picture is (see
[Give an app a picture](apps.md#give-an-app-a-picture)):

- Any size up to 2048 × 2048 pixels, and up to 2 MB.
- Any PNG, but not interlaced.
- A picture shaped like a poster (3:4, such as 600 × 800) fills the card;
  any other shape sits whole in the middle of it.

A folder without a picture, or with one Indigo can't read, gets a poster of
its name. Indigo never reads a picture over 2 MB, and makes one poster at a
time, so even a very large picture can't use up the console's memory.

## Posters

The box art comes from a poster pack, which is optional. Without one, each
game shows its disc banner and its six-character game ID, such as `GMSE01`:

<p align="center">
  <img alt="The Library without a poster pack: 1080° Avalanche is a card with its disc banner and its game ID, GTEE01, instead of box art." src="images/library-no-posters.png" width="640">
</p>

Download a ready-made pack or build your own: see [Posters](posters.md).

## Read the games again

Indigo reads the games when it opens a device. If what's on it changes while
Indigo is running, for example when you swap the game disc, turn to
**Source** on Home and choose **Refresh Library**.

---

<p align="center"><a href="home.md">← Home</a> · <a href="game-details.md">Game details →</a></p>
