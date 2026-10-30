[Indigo guide](README.md) › Install

# Install

Indigo runs on a GameCube that already runs Swiss. You need that console's SD
card, a computer that can read it, and about five minutes. Nothing is
formatted or flashed, and your games, settings and saves stay where they are.

Already using Indigo? Go to [Update Indigo](#update-indigo).

**Find your setup and follow its four steps.** **At power-on**, Indigo starts
when you switch on. **From Swiss**, your console starts as it does today and
you open Indigo from Swiss: that works with every setup.

| What starts your GameCube? | When Indigo starts |
| --- | --- |
| GC Loader | [At power-on](#gc-loader-at-power-on) or [from Swiss](#gc-loader-from-swiss) |
| PicoBoot or PicoLoader | [At power-on](#picoboot-or-picoloader-at-power-on) or [from Swiss](#picoboot-or-picoloader-from-swiss) |
| FlippyDrive | [At power-on](#flippydrive-at-power-on) or [from Swiss](#flippydrive-from-swiss) |
| Something else, or not sure | [From Swiss](#something-else-or-not-sure) |

## GC Loader, at power-on

GC Loader starts Swiss from the `boot.iso` (or `boot.gcm`) on its SD card,
and Swiss starts a `boot.dol` at the top of that card by itself, before it
shows anything. Indigo takes that name, so Indigo starts every time you
switch on. Leave `boot.iso` as it is: this needs Swiss there, as on most GC
Loader cards.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put GC Loader's SD card in your computer. Swiss hides
   its `swiss` folder, so first show hidden folders: press
   Shift-Command-Period in a Mac's Finder; on Windows, turn on **Hidden
   items** and **File name extensions** under **View** in File Explorer (View
   › Show on Windows 11). Then copy its `swiss` folder, and its `boot.dol` if
   it has one, to a folder on your computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `boot.dol` and copy it to the top of the
     card (not into a folder), replacing the old `boot.dol` if there is one.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss. If you also use an SD2SP2 or SD
     Gecko, copy it into that card's `swiss/patches` folder too.

   The card then looks like this:

   ```text
   SD card
   ├── boot.iso               unchanged
   ├── boot.dol               new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and switch on. Indigo
   starts straight away.

Your card's files are in Indigo too: System › File Browser. To start
stock Swiss again, take `boot.dol` off the card or rename it: while it is
there, Swiss starts it every time.

## GC Loader, from Swiss

GC Loader starts Swiss from the `boot.iso` (or `boot.gcm`) on its SD card.
Leave that file as it is, and don't rename anything to `boot.iso`. Your
console starts as it does today, and you open Indigo from Swiss.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put GC Loader's SD card in your computer. Swiss hides
   its `swiss` folder, so first show hidden folders: press
   Shift-Command-Period in a Mac's Finder; on Windows, turn on **Hidden
   items** and **File name extensions** under **View** in File Explorer (View
   › Show on Windows 11). Then copy its `swiss` folder to a folder on your
   computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `indigo.dol` and put it in the card's `apps`
     folder. Make the folder if there isn't one.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss. If you also use an SD2SP2 or SD
     Gecko, copy it into that card's `swiss/patches` folder too.

   The card then looks like this:

   ```text
   SD card
   ├── boot.iso               unchanged
   ├── apps/
   │   └── indigo.dol         new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and switch on. Swiss starts
   as usual. Go to the top of GC Loader's card (Swiss may open your last
   game's folder first: choose `..` at the top of the list to go up), then
   open `apps`, then `indigo.dol`. If Swiss shows a list of devices, choose
   **GC Loader**.

## PicoBoot or PicoLoader, at power-on

Most PicoBoot chips, and PicoLoaders with its gekkoboot firmware, run
gekkoboot, which starts the `ipl.dol` at the top of the SD card in your
SD2SP2 or SD Gecko. Indigo takes its place, so Indigo starts every time you
switch on. Ready-made solderless PicoLoaders have Swiss built in instead.

**Check first:** with the card in and a wired controller, hold D-Pad Down
while you switch on. If a screen of text stays up while you hold it, your
chip reads `ipl.dol`: carry on. If there is no text (Swiss, the GameCube
animation or its menu appears straight away), follow
[PicoBoot or PicoLoader, from Swiss](#picoboot-or-picoloader-from-swiss)
instead. Do the same if the card's `ipl.dol` is something you want to keep
starting, such as cubeboot.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put the SD card in your computer. Swiss hides its
   `swiss` folder, so first show hidden folders: press Shift-Command-Period in
   a Mac's Finder; on Windows, turn on **Hidden items** and **File name
   extensions** under **View** in File Explorer (View › Show on Windows 11).
   Then copy its `ipl.dol` and its `swiss` folder to a folder on your
   computer. Your old `ipl.dol` is then safe, and you can always put it back.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** copy it to the top of the card (not into a folder),
     replacing the old `ipl.dol`.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss.

   The card then looks like this:

   ```text
   SD card
   ├── ipl.dol                new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and switch on without holding
   any buttons. Indigo starts.

**Keep the normal Swiss one button away (optional).** If the old `ipl.dol`
was Swiss and the card has no `z.dol` yet, copy the old file from your backup
to the top of the card and name it `z.dol`. Hold Z on a wired controller
while you switch on to start it. Other buttons are in
[gekkoboot's table](https://github.com/redolution/gekkoboot#usage).

## PicoBoot or PicoLoader, from Swiss

Your console starts as it does today, and you open Indigo from Swiss.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put the SD card from your SD2SP2 or SD Gecko in your
   computer. Swiss hides its `swiss` folder, so first show hidden folders:
   press Shift-Command-Period in a Mac's Finder; on Windows, turn on **Hidden
   items** and **File name extensions** under **View** in File Explorer (View
   › Show on Windows 11). Then copy its `swiss` folder to a folder on your
   computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `indigo.dol` and put it in the card's `apps`
     folder. Make the folder if there isn't one. Leave any `ipl.dol` at the
     top of the card as it is.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss.

   The card then looks like this:

   ```text
   SD card
   ├── apps/
   │   └── indigo.dol         new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and start Swiss the way you
   do now. Open the card, then `apps`, then `indigo.dol`.

## FlippyDrive, at power-on

FlippyDrive starts the `boot.dol` at the top of its microSD card. Indigo
takes its place, so Indigo starts every time you switch on, instead of
FlippyDrive's own menu.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put FlippyDrive's microSD card in your computer.
   Swiss hides its `swiss` folder, so first show hidden folders: press
   Shift-Command-Period in a Mac's Finder; on Windows, turn on **Hidden
   items** and **File name extensions** under **View** in File Explorer (View
   › Show on Windows 11). Then copy its `swiss` folder, and its `boot.dol` if
   it has one, to a folder on your computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `boot.dol` and copy it to the top of the card
     (not into a folder), replacing the old `boot.dol` if there is one.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss.

   The card then looks like this:

   ```text
   FlippyDrive microSD
   ├── boot.dol               new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

   Indigo's Library shows the games in a `games` folder. If yours sit loose
   at the top of the card, make a `games` folder and move them into it.

4. **Start Indigo.** Eject the card, put it back and switch on without holding
   any buttons. Indigo starts.

If FlippyDrive's own menu appears instead of Indigo, it didn't find
`boot.dol`: check the name and that it is at the top of the card. If the
GameCube menu or a disc starts, FlippyDrive is set to skip its card: open
`config.ini` at the top of the card and set
[`boot_mode`](https://docs.flippydrive.com/configuration.html#confval-boot_mode)
to `normal`. To get FlippyDrive's own menu back, delete `boot.dol`; to start
as you did before, put your old `boot.dol` back.

## FlippyDrive, from Swiss

Your console starts as it does today, and you open Indigo from Swiss.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put FlippyDrive's microSD card in your computer.
   Swiss hides its `swiss` folder, so first show hidden folders: press
   Shift-Command-Period in a Mac's Finder; on Windows, turn on **Hidden
   items** and **File name extensions** under **View** in File Explorer (View
   › Show on Windows 11). Then copy its `swiss` folder to a folder on your
   computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `indigo.dol` and put it in the card's `apps`
     folder. Make the folder if there isn't one. Leave any `boot.dol` at the
     top of the card as it is.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss.

   The card then looks like this:

   ```text
   FlippyDrive microSD
   ├── apps/
   │   └── indigo.dol         new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and start Swiss the way you
   do now. If FlippyDrive starts its own menu, start Swiss instead by holding
   X on a wired controller in port 1 while you switch on: in the bootloader
   that opens, choose **Boot Onboard DOL**, then **swiss-gc**. In Swiss, open
   **FlippyDrive** (not FlippyDrive Flash), then `apps`, then `indigo.dol`.

## Something else, or not sure

This works with any console that already starts Swiss: Swiss built into a
chip, cubeboot, Swiss on a Wii, or any setup above. Your console
starts as it does today, and you open Indigo from Swiss.

1. **Download and unzip.** [Download the Indigo
   ZIP](https://github.com/spencernorton/indigo/releases/latest)
   (`Indigo-vX.Y.Z.zip` under Assets, not the Source code ZIP) and unzip it on
   your computer. Inside are `ipl.dol`, a `swiss` folder and a readme. Older
   readmes say to drag the whole `swiss` folder onto the card: don't.
2. **Back up the card.** Put the SD card you open in Swiss in your computer.
   On a Wii, use the card in your SD Gecko, not the Wii's own SD card: Swiss
   can't read the Wii's slot in GameCube mode. Swiss hides its `swiss` folder,
   so first show hidden folders: press Shift-Command-Period in a Mac's Finder;
   on Windows, turn on **Hidden items** and **File name extensions** under
   **View** in File Explorer (View › Show on Windows 11). Then copy its
   `swiss` folder to a folder on your computer.
3. **Copy two files onto the card.**
   - **`ipl.dol`:** rename it to `indigo.dol` and put it in the card's `apps`
     folder (on a Wii, the SD Gecko card's). Make the folder if there isn't
     one. Leave any `ipl.dol`, `boot.dol` or `boot.iso` at the top of the card
     as it is.
   - **`apploader.img`:** it is in the download's `swiss/patches` folder. Copy
     it into the card's `swiss/patches` folder, replacing the old one; make
     the folders if they aren't there. With **In-Game Reset** set to Apploader
     (Settings › Quick), leaving a game then returns to Indigo, even from
     games you start in the normal Swiss.

   The card then looks like this:

   ```text
   SD card
   ├── apps/
   │   └── indigo.dol         new
   ├── swiss/
   │   └── patches/
   │       └── apploader.img  new
   └── games/                 unchanged
   ```

4. **Start Indigo.** Eject the card, put it back and start Swiss the way you
   do now. Open the card (on a Wii, the SD Gecko), then `apps`, then
   `indigo.dol`.

## Where everything goes

The card with everything Indigo can use. Only Indigo's own file,
`apploader.img` and the web browser come from the download: the rest are
your files, and downloads you can add any time. Indigo's own file and
`boot.iso` aren't drawn here, because they depend on your setup: its step 3
shows them. Most
people keep everything on one card; if you use two, put the poster pack and
your apps on the card with your games.

```text
SD card
├── apps/                    programs for the Apps face
│   ├── gbi.dol              a homebrew program
│   └── gbi.png              its picture
├── games/                   games for the Library
│   ├── Pikmin.iso           a game
│   ├── Nintendo/            a folder of games
│   │   └── The Wind Waker.iso
│   └── Nintendo.png         the folder's picture
├── gcbrowser/               the web browser's certificates
└── swiss/
    ├── cheats/              cheats, one file per game
    ├── indigo/
    │   └── cubebrowser.dol  the web browser, for the Web face
    ├── patches/
    │   └── apploader.img    returns to Indigo after a game
    ├── saves/               saves copied off memory cards
    ├── settings/            your settings
    └── ui/
        ├── posters.pak      box art for the Library
        ├── stills.pak       pictures for Spotlight
        ├── descriptions.txt  descriptions for Spotlight
        └── discs.pak        disc art for Jewel Case
```

- **`ipl.dol`, `boot.dol` or `indigo.dol`:** Indigo itself. Step 3 of your
  setup says which name it has and where it goes.
- **`boot.iso`:** On a GC Loader card, the disc image that starts Swiss.
  Leave it as it is.
- **`apps`:** Programs for the Apps face on Home: `.dol`, `.dol+cli` or
  `.elf` files. A PNG beside a program, with its name, is its picture. On a
  from-Swiss setup, Indigo's own `indigo.dol` is here too and shows as an
  app. See [Apps](apps.md#set-up-the-apps-folder).
- **`games`:** Your games, loose or one folder per game. To browse your own
  folders of games, turn on **Library Folders** (Settings › Setup › Library).
  A PNG beside a folder, with its name, is its picture. See
  [Folders of games](library.md#folders-of-games).
- **`cheats`:** The [cheat pack](cheats.md#get-cheat-files) fills this
  folder, one file per game, such as `GMSE01.txt`. Unzip the pack on your
  computer and copy the files in its `swiss/cheats` folder into this one;
  make it if it isn't there.
- **`gcbrowser` and `swiss/indigo/cubebrowser.dol`:** From the download,
  for the Web face: the web browser and its certificates. Put
  `cubebrowser.dol` on the card with your settings. Indigo names that card to
  the browser (GC Loader, SD2SP2 or SD Gecko); standalone launches retain
  automatic card detection. The browser reads
  `gcbrowser` from an SD2SP2, an SD Gecko or a GC Loader, not from a
  FlippyDrive's card: with a FlippyDrive, put `gcbrowser` on a card in an
  SD2SP2 or SD Gecko. With only a FlippyDrive, secure (`https`) pages don't
  open and the browser saves nothing.
- **`apploader.img`:** From the download. It makes leaving a game return to
  Indigo; copy it to every card you use with Swiss. See
  [Leaving a game](#leaving-a-game).
- **`saves` and `settings`:** Indigo makes these. Memory Cards copies saves
  into `saves`, and your settings are in `settings/global.ini`.
- **`ui`:** The [poster pack](posters.md#download-the-pack) goes here:
  `posters.pak` for box art, `stills.pak` and `descriptions.txt` for
  Spotlight, and `discs.pak` for Jewel Case. Unzip the pack on your computer
  and copy all four files from its `swiss/ui` folder into this one on the
  card holding your games; make the folder if it isn't there. Restart
  Indigo after updating artwork packs.

## After the first start

Indigo opens on Home, with a cube and the name of the face underneath.

<p align="center">
  <img alt="Home on the Library face: the glass cube shows a GameCube controller, LIBRARY is written underneath, and the hint line reads Turn and A Open." src="images/home-library-face.png" width="640">
</p>

1. Turn the cube to **Source**, press A and choose the card that holds your
   games (on FlippyDrive, choose **FlippyDrive**, not FlippyDrive Flash). It
   can be a different card from the one Indigo started from.
2. Open **Library**. It shows the games in the `games` folder at the top of
   that card, loose or one folder per game. Games anywhere else on the card
   don't show in the Library: move them into `games` to see them there.

```text
SD card
└── games/
    ├── Super Mario Sunshine.iso
    └── The Wind Waker/
        └── game.iso
```

Indigo plays `.iso`, `.gcm`, `.tgc` and `.fdi` disc images. Convert `.rvz`
and `.gcz` files, and unzip `.zip` files, first. To check which version you
are running, open **System › System Information** and press R until
**About Indigo**: it shows the commit, which each release's page on GitHub
lists too.

Next: [set up the library](library.md#set-up-the-games-folder),
[learn the controls](controls.md), and add [posters](posters.md) and
[cheats](cheats.md) when you want them.

## Leaving a game

Set **In-Game Reset** (Settings › Quick) to **Apploader**; it starts at
Disabled. Then hold **A + Z + START** to leave a game and return to Indigo,
or **R + Z + START** to restart it. Apploader starts the `apploader.img` in
`swiss/patches`, so copy the download's `apploader.img` to every card you use
with Swiss. **Reboot** restarts the console the normal way instead, which can
start the normal Swiss rather than Indigo.

## Didn't start?

| What you see | What to do |
| --- | --- |
| Swiss, or your usual menu | With a **from Swiss** route that is expected: open `apps`, then `indigo.dol`. At power-on, check the file's name (`ipl.dol` or `boot.dol`) and that it is at the top of the card, not in a folder. With PicoBoot or PicoLoader, Swiss may be built into your chip: use the **from Swiss** route. |
| An error, a black screen or the GameCube menu | Check that you unzipped the download, and that the name is not `boot.dol.dol` or `indigo.dol.dol`. At power-on, put your old `ipl.dol` or `boot.dol` back from the backup to start as before. |
| Indigo starts, but there are no games | On **Source**, choose the card with your games. Check that they are in `games` at the top of that card, in a format listed above. |
| Leaving a game goes back to the normal Swiss | Set In-Game Reset to **Apploader**, not Reboot. Check that the download's `apploader.img` is in `swiss/patches` on every card you use with Swiss. |

More help: [Troubleshooting](troubleshooting.md). When you ask for help, say
which Indigo version you have, what starts your GameCube, and where you copied
Indigo.

## Update Indigo

1. Download and unzip the new release, and back up the card's `swiss` folder.
   Swiss hides that folder: show hidden folders first (Shift-Command-Period
   in a Mac's Finder; **Hidden items** and **File name extensions** under
   **View** in Windows File Explorer).
2. Rename the new `ipl.dol` to the name of the Indigo file you start now
   (`ipl.dol`, `boot.dol` or `indigo.dol`), then copy it over that file, in
   the same place. On Windows, show file name extensions first.
3. Copy the new `apploader.img` from the download's `swiss/patches` folder
   into the card's `swiss/patches` folder, replacing the old one.
4. Start Indigo. **About Indigo** (System › System Information, then R)
   shows the new release's commit.

This works for Indigo 1.x and 2.x. If you renamed stock Swiss to `swiss.dol`
to get round [issue 3](https://github.com/spencernorton/indigo/issues/3) in
1.25.0, you can rename it back to `z.dol`: since 2.0, Indigo no longer starts
a `z.dol` by itself.

## Go back to stock Swiss

Stock Swiss is the normal Swiss, without Indigo's menus. Show hidden folders
on your computer first, as in Update Indigo, to see the card's `swiss` folder.

- **At power-on:** put your old `ipl.dol` or `boot.dol` back from the
  backup. If the card had no `boot.dol` before, delete Indigo's. With a
  `z.dol` shortcut, you can also hold Z while you switch on.
- **From Swiss:** start Swiss as usual and don't open `indigo.dol`. Delete
  `apps/indigo.dol` to remove Indigo.
- **Leaving a game:** Indigo's `apploader.img` makes In-Game Reset return to
  Indigo, even after games started in stock Swiss. Put back the
  `swiss/patches/apploader.img` from your backup, or delete it if the card
  had none. Without a backup, stock Swiss's copy is in
  `Apploader/EXTRACT_TO_ROOT.zip`, inside `swiss_rNNNN.7z` from a
  [Swiss release](https://github.com/emukidid/swiss-gc/releases/latest); open
  a `.7z` with 7-Zip or The Unarchiver.

Both programs read the same settings file. Stock Swiss drops Indigo-only
settings when it saves, such as Menu Color and Library Layout: copy
`swiss/settings/global.ini` somewhere first if you want them back later.

## Build it yourself

Most people should use the ready-made ZIP. For a source build, run the same
container image as CI:

```bash
git clone https://github.com/spencernorton/indigo.git
cd indigo
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2 make dev
buildtools/sd_package.sh dev
```

`sd_package.sh` creates `Indigo-dev.zip` with the same files as a release:
follow the steps above with its `ipl.dol`. Ignore the `cube/swiss/swiss.dol`
the build leaves on your computer. Upstream's `make all` and `make package`
targets need device firmware and prebuilt tools this fork does not
redistribute; use `make dev` here.

---

<p align="center"><a href="README.md">← Guide</a> · <a href="controls.md">Controls →</a></p>
