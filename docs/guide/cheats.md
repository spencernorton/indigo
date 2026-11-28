[Indigo guide](README.md) › Cheats

# Cheats

Indigo runs Gecko codes: cheats that change a game while it runs, from
infinite health to widescreen fixes. You switch them on per game, and Indigo
remembers your choice.

<p align="center">
  <img alt="Y on the detail screen for LEGO Star Wars II opens the cheat browser; three cheats are switched on and one switched back off, and the detail screen then reads 2 of 130 enabled." src="../screenshots/game-detail.png" width="640">
</p>

## Get cheat files

Indigo reads one file per game, `/swiss/cheats/<game ID>.txt`, such as
`/swiss/cheats/GALE01.txt` for Super Smash Bros. Melee. With a network
adapter, **Settings › Network › Install game content** puts every game's
file on the card for you: see
[Installing game content](settings.md#installing-game-content-over-the-network).
Otherwise, the quickest way to get them is the ready-made pack, which covers
every region:

- [Download the cheat pack](https://indigo.norvitech.com/indigo-cheats.zip)
  and unzip it on your computer. Copy the files in its `swiss/cheats`
  folder into the card's `swiss/cheats` folder; make the folder if it isn't
  there. Swiss hides its `swiss` folder, so first show hidden folders:
  press Shift-Command-Period in a Mac's Finder, or turn on **Hidden items**
  under **View** in Windows File Explorer.

Cheats that the pack's checks show would break Indigo are switched off in
the file and marked; the
[Indigo page](https://norvitech.com/indigo/#downloads) says what the checks
can't cover.

A game without a file says "No cheats found" on its detail screen.

## Switch cheats on

1. Open a game's [details](game-details.md) and press **Y**.
2. Move to a cheat and press **A** to switch it **On** or **Off**.
3. Press **B** when you're done. The detail screen now shows how many are
   on, and names them.
4. Press **A** to start the game with those cheats.

<p align="center">
  <img alt="The cheat browser for 1080° Avalanche: Enable 8-Bit Mode and Super Jump are On and the others Off; the top right reads 2 enabled and the list position 5 / 12; the hint line reads A Toggle, B Done, X Enabled only, Z Advanced." src="images/cheats.png" width="640">
</p>

In the browser:

- The top right counts the cheats that are on, and the line above the list
  shows where you are in it, such as "5 / 12".
- **X** shows only the cheats that are on; X again shows them all.
- **Z** opens **Advanced** (below).

Indigo saves which cheats are on in a small `.chtsel` file beside the
game's cheat file, one per disc revision, so they're still on next time.
Cheats on a read-only device, such as a data disc, work but aren't saved.

## Start with your cheats every time

Normally the cheats that are on apply when you start the game after opening
the browser. Turn on **Auto-load cheats** in Settings › Quick and Indigo
applies your saved cheats whenever the game starts, without opening the
browser, and says "Applying cheats" as the game starts.

## Advanced

<p align="center">
  <img alt="Advanced in the cheat browser: the selected cheat, Super Jump; a WiiRD Debug switch that is Off; and a bar showing the cheat memory used, 16 of 5448 bytes." src="images/cheats-advanced.png" width="640">
</p>

- **Cheat memory** shows how much of the space for cheats is used. All the
  cheats that are on share it; if it's full, Indigo says "Not enough room.
  Turn off another cheat first."
- **WiiRD Debug** starts the game with the WiiRD debugger, for compatible
  debugging tools. It uses cheat memory too.

B goes back to the list.

---

<p align="center"><a href="game-details.md">← Game details</a> · <a href="settings.md">Settings →</a></p>
