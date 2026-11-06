# JVPPBPort (Jonnisvibes PPB Port)



A full-table static view PC port of *Pokemon Pinball: Ruby \& Sapphire* (Game Boy
Advance), built on the [pret decompilation](https://github.com/pret/pokepinballrs).


Instead of the camera scrolling up and down the table, everything is shown all at once, like Space Cadet Pinball.



**This program contains no assets from the original game.** 

It needs your own original copy of the NA game supplied during first run. All assets are pulled from the ROM and saved in %APPDATA%\\JVPPBPort for use with the game.



Fan project, not affiliated with or endorsed by Nintendo, Creatures, GAME FREAK or The Pokémon Company.



## Features

* The whole table in one tall window with no Camera movement
* Plays 1:1 to the original: the game's code is functionally the same as the
original's, and the ball physics and sound match the GBA frame for frame (checked against mGBA).
* Keyboard, mouse and game controllers, all keys configurable in Options.
* Rumble on controllers via the original Gameboy Player implementation (Options → RUMBLE).
* Optional LCD / GBA screen filters and whole-number scaling (Options → SCREEN).
* Saves are real GBA saves: your save works in an emulator and the other way round.
* A new FAST ball speed for a new challenge, next to SLOW and NORMAL in Field Select.



## Getting started (Windows)

1. Download the zip from [Releases](../../releases) and unzip it anywhere.
2. Start `JVPPBPort.exe`. Windows may warn that it's from an unknown
publisher (the program isn't signed): choose **More info → Run anyway**.
3. Click **CHOOSE ROM FILE** and pick your ROM, or drop it on the window.
It must be the US release of Pokémon Pinball: Ruby \& Sapphire
(SHA-1 `9fec81ce2c5df589e0371a0bf2f92a5fe8db730b`).
4. The program prepares the tables for a few seconds, then the game starts.
You only do this once.

You can delete the ROM file afterwards if you like; the program keeps what it
needs in its data folder.

Linux: build it from source, see [docs/BUILDING.md](docs/BUILDING.md) (Untested outside of WSL)

## Default Controls

Every key below can be changed in Options (PINBALL KEYS for in game, and MENU KEYS for navigating menus, each with a KEYS tab for the keyboard and a PAD tab for controllers).

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

The mouse works in the menus too: point at an item, left click to choose it, right click to go back.

Other keys: **Alt+Enter** or **F11** switches fullscreen. **F12** while playing saves a bug snapshot (see "Reporting bugs").



## Your files

Saves, settings and the data taken from your ROM are kept in
`%APPDATA%\JVPPBPort\` (paste that into the Explorer address bar):

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

Open an issue and say what happened and what you expected. For anything you
can see, press **F12** while it's on screen: the game saves `bug_<number>.bmp`,
`.txt` and `.bin` in the data folder. Attach the `.bmp` and `.txt` to the
issue. **Don't attach the `.bin` publicly**: it contains the game's graphics
from memory; say you have it and we'll ask for it privately. If the game
crashed, attach `jvppbport.log`.

## How this was made

This port was written with AI. The platform layer (the software renderer,
sound, input, the whole-table view, the redrawn menus, the ROM setup) and the
small, clearly marked changes to the game code were written by Claude, Anthropic's AI model, using
Claude Code, directed and playtested by [jonnisvibes](https://github.com/jonnisvibes).

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
[THIRD\_PARTY\_NOTICES.md](THIRD_PARTY_NOTICES.md) for the licenses of the
code included in the program.

