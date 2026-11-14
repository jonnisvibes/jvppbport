# JVPPBPort (Jonnisvibes PPB Port)

A full-table static view port of *Pokémon Pinball: Ruby & Sapphire* (Game Boy
Advance) for **Windows** and **Android**, built on the
[pret decompilation](https://github.com/pret/pokepinballrs).

Instead of the camera scrolling up and down the table, everything is shown
all at once, like Space Cadet Pinball.

**This program contains no assets from the original game.** It needs your own
copy of the US game (a ROM file), which you choose the first time you start it.
The program takes what it needs from the ROM and keeps it in its data folder.

Fan project, not affiliated with or endorsed by Nintendo, Creatures, GAME FREAK
or The Pokémon Company.

> **New in 0.2.0:** an Android version (an experimental public playtest), full-size bonus stages,
> Tate mode for a sideways monitor, and safer screen settings. See
> [CHANGELOG.md](CHANGELOG.md) for everything.

## Features

* The whole table on one tall screen, with no camera movement.
* Bonus stages shown in full (or as on the GBA, if you prefer).
* Plays 1:1 with the original: the game's code is functionally the same as the
  original's, and the ball physics and sound match the GBA frame for frame
  (checked against mGBA) at SLOW and NORMAL speed.
* Windows: keyboard, mouse and game controllers, all keys configurable in Options.
* Android: on-screen flipper buttons or touch zones, swipe to tilt, and
  controller support.
* Rumble on controllers (and the phone's vibration on Android) through the
  game's own Game Boy Player rumble (Options → RUMBLE).
* Optional LCD / GBA screen filters, whole-number scaling and, on Windows, Tate
  mode for a monitor turned on its side (Options → SCREEN).
* Saves are real GBA saves: your save works in an emulator and the other way round.
* A new **FAST** ball speed for a new challenge, next to SLOW and NORMAL in
  Field Select (experimental, see [Ball speed](#ball-speed)).
* The e-Reader exclusive Pokémon can be unlocked (see
  [Super-rare Pokémon](#super-rare-pokémon-and-the-e-reader-exclusives)).

## Getting started (Windows)

1. Download the Windows zip from [Releases](../../releases) and unzip it anywhere.
2. Start `JVPPBPort.exe`. Windows may warn that it's from an unknown
   publisher (the program isn't signed): choose **More info → Run anyway**.
3. Click **CHOOSE ROM FILE** and pick your ROM, or drop it on the window.
   It must be the US release of Pokémon Pinball: Ruby & Sapphire
   (SHA-1 `9fec81ce2c5df589e0371a0bf2f92a5fe8db730b`).
4. The program prepares the tables for a few seconds, then the game starts.
   You only do this once.

You can delete the ROM file afterwards if you like; the program keeps what it
needs in its data folder.

Updating: unzip the new version over the old one (or anywhere else). Your save
and settings are in the data folder, not next to the `.exe`, so they carry over.

Linux: build it from source, see [docs/BUILDING.md](docs/BUILDING.md)
(untested outside of WSL).

## Getting started (Android)

> **The Android version is an experimental public playtest.** It has only been
> tested on a handful of Samsung devices running Android 16 and 17. If something doesn't work or
> looks wrong on your device, please
> [open an issue](../../issues) and say which device and Android version you
> have (see [Reporting bugs](#reporting-bugs)).

You need Android 8.0 or newer on a 64-bit ARM device (almost every phone and
tablet from the last several years).

1. On your phone or tablet, download the `.apk` from [Releases](../../releases).
2. Open the downloaded file. Android will ask you to allow your browser (or
   file manager) to **install unknown apps**: allow it, go back, and tap
   **Install**. Google Play Protect may warn that it doesn't recognize the
   app, because it isn't from the Play Store: choose **More details → Install
   anyway** (the wording differs between phones).
3. Open **JVPPBPort**, tap **CHOOSE ROM FILE** and pick your ROM in Android's
   file picker (the same US ROM as above).
4. The app prepares the tables for a few seconds, then the game starts.
   You only do this once.

**Updating:** install the new `.apk` over the old one. Your save is kept.
**Don't uninstall the app first:** Android deletes the app's data folder,
and your save with it, when an app is uninstalled. To be safe, copy your
save to a computer first (see [Your files](#your-files)).

If you leave the app (Home button, a phone call), the game is saved at once.
If Android closes the app while it's in the background, pick **CONTINUE** on
the title screen to carry on.

## Default controls (Windows)

Every key below can be changed in Options (PINBALL KEYS for in game, and MENU
KEYS for navigating menus, each with a KEYS tab for the keyboard and a PAD tab
for controllers).

|Pinball|Keyboard|Controller|
|-|-|-|
|Left flipper|Z|D-pad Left|
|Right flipper|/|right face button (B on Xbox, Circle on PlayStation)|
|Launch|/|right face button|
|Tilt left / right|X / .|LB / RB|
|Tilt up|Up|LB + RB together|
|Pause|Esc|Start|

|Menus|Keyboard|Controller|
|-|-|-|
|Move|Arrow keys|D-pad or left stick|
|OK|Enter|A|
|Back|Esc|B or Start|

The mouse works in the menus too: point at an item, left click to choose it,
right click to go back. The left and right mouse buttons can also be set as
pinball keys.

Other keys: **Alt+Enter** or **F11** switches fullscreen.

## Touch controls (Android)

**While playing** (default layout, TOUCH → BUTTONS):

|Action|Touch|
|-|-|
|Left / right flipper|The round buttons under the table, left and right|
|Launch|The right flipper button, or hold a finger on Spoink (the launcher)|
|Tilt left / right / up|Swipe on the table: left, right or up|
|Pause|The button between the flipper buttons, or the phone's Back|
|Yes / no prompts (bonus stage, travel)|Tap the answer, or tap the table for yes|
|Mart and Evolution Select|Tap the big arrows to page, tap the picture to buy / pick, tap BACK under the Mart to leave|

**In menus:** tap an item to choose it. Swipe to step through lists and
letters (the Pokédex list can also be dragged). Use the **BACK** button in the
top-left corner, or the phone's Back.

**Options → PINBALL KEYS → TOUCH** picks the on-screen layout:

* **BUTTONS** (default): the flipper buttons and pause button under the table.
* **PLAIN**: the same buttons without the flipper pictures.
* **ZONES**: no buttons. Touch anywhere on the left or right half of the
  screen for that flipper (the right one also launches); swipe from there to
  tilt. Pause is the small button in the top-right corner.
* **OFF**: no on-screen controls, for playing with a controller or keyboard.
  Taps still work the menus and prompts.

**Controllers and keyboards** work on Android too, with the same defaults as
on Windows; change them in Options (the PAD tab is shown first on Android).

**Options → SCREEN → SCALE** sets how big the picture is: FILL (default, as
big as fits), SNAP FILL (the biggest whole-number size, sharper with the
filters) or a fixed size such as 3X. With a fixed size the table stays the
same size and the cabinet and menus get more room around it.

## Screen options

Options → SCREEN has:

* **FULLSCREEN** (Windows): the same as Alt+Enter or F11.
* **TATE MODE** (Windows): for a monitor stood on its side, with Windows left
  in landscape. ON = the monitor turned clockwise, REVERSE = turned the other
  way. The picture is turned so it fills the monitor.
* **FILTER**: OFF, LCD (the screen's pixel grid) or GBA (the grid plus the
  GBA screen's colors). Filters look best at SNAP FILL or 2X and above.
* **SCALE**: FILL, SNAP FILL or a whole number (1X, 2X, …).
* **BONUS STAGE**: FULL (default) shows each bonus stage in full; ORIGINAL
  shows the GBA's window onto it, which scrolls a little. The ball plays the
  same either way.
* **CONFIRM CHANGES**: changes in this box take effect only when you pick
  this. If the picture changes, a **KEEP CHANGES?** window counts down from
  10 seconds; if you don't answer, the old settings come back, so a setting
  that doesn't work on your screen undoes itself. Leaving Options with
  unconfirmed changes asks whether to **SAVE** or **DISCARD** them.

## Ball speed

Field Select (and Bonus Field Select) has **SLOW**, **NORMAL** and **FAST**.
SLOW and NORMAL are the original game's speeds and play exactly like the GBA.

**FAST is experimental and a work in progress.** It makes every ball value
35% stronger than NORMAL (speed limits, gravity, bounces), which is faster
than the original game was ever built for. The ball can sometimes squeeze
into a wall or leave the table; when it leaves, the game puts it back on the
launcher. Expect the odd glitch, and please
[report anything strange](../../issues) with the table and where on it it
happened. FAST only exists in this port: if you load your save in an
emulator, it won't play at FAST there.

## Super-rare Pokémon and the e-Reader exclusives

The game has six super-rare Pokémon that can turn up in Catch 'Em mode:
**Latios** (Ruby table) or **Latias** (Sapphire table), and four e-Reader
exclusives: **Chikorita, Cyndaquil, Totodile and Aerodactyl** (they can't
evolve, both in the original and in JVPPBPort). This port changes none of the rules below; they are
the original game's.

**When they can appear:**

* You have at least **100 Pokémon registered as caught** in the Pokédex.
  The table only checks this when it loads: a new game, entering and leaving
  a bonus stage, or saving and continuing.
* You have caught, hatched or evolved **5 Pokémon in the current game**.
* Then each Catch 'Em mode has a **1% chance** of a super-rare Pokémon, or
  **2%** after you clear the bonus stages all the way to Rayquaza (until you
  close the game).

**Which one:** always the first one you haven't caught yet, in this order:
Latios / Latias, then Cyndaquil, Totodile, Chikorita, Aerodactyl. Once you
have them all, it's a random one of them.

**The e-Reader exclusives only join if they're in your Pokédex.** In the
original game, that meant the *GET Special Guests* e-Reader card (09-A002):
a demo card placed in selected stores in Japan during September 2003 only.
It wasn't given out: the cards were laminated and chained to the in-store
kiosks, so players scanned them there. Everyone else needed a Pokédex link
(over a Game Link Cable) with someone who already had them.

**Options → E-READER → UNLOCK E-READER EXCLUSIVES** does what that link did:
it marks the four as shared in your Pokédex, so they can appear by the rules
above. **LOCK** takes the marks away again (any you've already caught stay
caught). Latios and Latias need no unlock.

## Your files

**Windows:** saves, settings and the data taken from your ROM are kept in
`%APPDATA%\JVPPBPort\` (paste that into the Explorer address bar).

**Android:** `Android/data/com.jvppbport/files/` on the device's storage.
The easiest way to reach it is to connect the device to a computer by USB
(choose "File transfer" on the device). Many phones can't open
`Android/data` on the device itself.

* `jvppbport.sav`: your save (a GBA save file, works in emulators).
* `jvppbport.ini`: the port's settings (keys, screen, window).
* `gamedata.bin`, `tables.bin`: taken from your ROM at the first start. They
  contain game data: don't share them.
* `jvppbport.log`: messages and crash reports.

### Hidden settings

A few rarely needed settings are only in `jvppbport.ini`. Close the game
before editing it, then add the line under its section (add the section if
it isn't there).

**Older controllers on Windows:**

```ini
[Controls]
DirectInput=1
```

Xbox, PlayStation and Switch controllers work without this. Some older or
generic USB controllers (cheap SNES-style pads, some arcade sticks) only
work through Windows' old DirectInput system, which is off by default
because searching for DirectInput devices can freeze the game for a few
seconds at every start on some PCs. If your controller isn't recognized,
set `DirectInput=1`. Startup may be slower.

## Reporting bugs

[Open an issue](../../issues) and say what happened and what you expected,
plus the version (the first line of `jvppbport.log`).

For anything you can see, attach a normal screenshot of the problem. If the
game crashed, attach `jvppbport.log` from the data folder (see
[Your files](#your-files)). On Android, also say which device and Android
version you have.

## Planned

* More support for squarer screens (such as 4:3 monitors and Android
  handhelds) via an expanded dynamic camera view mode: more of the table than
  the GBA showed, following the ball.

## How this was made

This port was written with AI. The platform layer (the software renderer,
sound, input, touch controls, the whole-table view, the redrawn menus, the
ROM setup) and the small, clearly marked changes to the game code were
written by Claude, Anthropic's AI model, using Claude Code, directed and
playtested by [jonnisvibes](https://github.com/jonnisvibes).

The game code itself (`src/`, `include/`) is the work of the people of
[pret](https://github.com/pret/pokepinballrs), who decompiled the game by
hand. This project is not part of pret, and pret did not review it: please
don't take questions about this port to them.

## Credits and license

* [pret/pokepinballrs](https://github.com/pret/pokepinballrs): the
  decompilation this port is built on.
* [SDL2](https://www.libsdl.org/): window, input, sound and controllers.
* Pokefan531: the measured GBA screen colors used by the GBA filter.

See [LICENSE.md](LICENSE.md) for what license covers which files, and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the licenses of the
code included in the program.
