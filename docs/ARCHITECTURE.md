# How the port works

The game code in `src/` is pret's decompilation of Pokémon Pinball: Ruby &
Sapphire, written in C. On the GBA it talks to hardware: video registers,
VRAM, sprite memory (OAM), sound and DMA. The port compiles that same C for a
64-bit PC and supplies pretend hardware for it, plus a different way of
drawing the picture. Everything the port adds is in `JVPPBPort/`; changes to
pret's files are kept small and marked `#ifdef JVPPBPORT`.

## Pretend hardware

With `JVPPBPORT` defined, `include/gba/*.h` points the hardware addresses
(`REG_BASE`, `VRAM`, `PLTT`, `OAM`, the interrupt vector) at plain arrays in
`JVPPBPort/src/hw.c`, and DMA becomes an immediate copy. The game writes
registers and VRAM as usual; the port reads them back to draw.

- `platform.c`: SDL window, input events, frame pacing at the GBA's 59.73
  frames per second. The frame is drawn inside `VBlankIntrWait`, scanline
  by scanline, running the game's VCount handler on the right lines (the
  game changes registers mid-screen for a few effects).
- `ppu.c`: the software renderer: tiled backgrounds, normal and affine
  sprites, priorities, blending and fades.
- `bios.c`: the GBA BIOS calls the game uses (decompression, CpuSet, Sqrt,
  ArcTan2). `ArcTan2` reproduces the BIOS's own integer math: the ball's speed
  cap depends on its exact rounding.
- `sram.c`: save memory, kept in a file. The file is a real 32 KB GBA save.

## Game data from the player's ROM

Most of the game's data (graphics, sound, tables) is in pret's assembly files,
which a PC compiler can't use, and none of it may be in this repository. The
tables are rebuilt for 64-bit (pointers are 8 bytes on PC) as assembly in
`JVPPBPort/gen/`: labels, pointers between tables, and zeros where the ROM's
bytes go. `rom_fixups.s` lists every such place (address, ROM offset, size).

At first start, `setup.c` asks for the ROM, checks its size and SHA-1, and
`gamedata.c` saves the needed part of it as `gamedata.bin` in the data folder.
Every start, those bytes fill in the zeros before the game begins. The
generators that made `JVPPBPort/gen/` need the GBA build; they are not part of
this repository.

## Struct layout

The GBA's compiler aligns every struct to at least 4 bytes. The PC build
copies that rule (`JVPPBPort/include/jvppbport_layout.h`, force-included into
game files), so structs match the GBA's layout except where they hold
pointers. Saves and the attract-mode demo snapshots are converted between the
GBA layout and the PC one (`src/save_and_restore_game.c`), which is why saves
work on both.

## Sound

`src/m4a.c` (the game's sound engine, C half) is compiled as it is.
`JVPPBPort/src/m4a_port.c` is a C translation of its assembly half: the
sample mixer and the music sequencer. `audio.c` reads the mixer's buffer the
way the GBA's timer-driven DMA does, emulates the four Game Boy sound
channels, mixes and filters like the GBA, and hands 48 kHz audio to SDL. The
mixer's output was checked byte for byte against an emulator for every song,
sound effect and cry.

## The whole-table view

On the GBA the table is taller than the screen and a camera follows the ball.
The port keeps the game's camera running (so the game behaves exactly the
same) but draws differently:

- `ppu.c` draws the board backgrounds for all 448 rows from the game's full
  tile buffers, not from the window of them the GBA has in VRAM.
- `JVPPBPort/tools/portrewrite.py` runs on each game file at build time. It
  sends sprite positions through `PortOamSetX/Y` (full 16-bit values, since
  OAM's 8-bit y can't hold a position on the tall table) and makes drawing
  code see a pretend camera, so table sprites come out at table positions.
- `staticview.c` sorts each frame's sprites into table sprites, screen-fixed
  sprites (banners, panels) and hidden ones, and keeps a full copy of the
  board's light tiles (the GBA swaps rows of them in and out as the camera
  moves).
- Pop-ups made for the GBA's 240x160 screen (banners, the Mart, pause) are
  drawn as a window in the middle of the table; scenes the game lines up
  with the board (catch, evolution) stay where the game's camera points.
- Objects the GBA only animates near the camera now always animate. This
  changes no ball physics.

## Menus and layouts

`layouts.c` re-lays out the short screens (title, Field Select, Pokédex,
high scores) for the tall window: each is drawn as pieces of the GBA screen
moved to new places, over stretched backgrounds. Only drawing changes. Bonus
stages are drawn over the dimmed main table they were entered from; with
BONUS STAGE set to FULL, the stage's camera is held still and all of its
rows are drawn at once (the game's own scrolling values are put back
afterwards, so the stage plays the same).

## Input

`input.c` keeps menus and pinball apart. Menu keys press the GBA's buttons.
Pinball keys don't: the game asks `PortPinballActionHeld()` for each flipper
and tilt, so every action can have its own keys. Controllers (`pad.c`) count
as extra keys. `mouse.c` works the menus by pressing the game's own D-pad
buttons until the cursor reaches the item pointed at, so sounds and scrolling
are the game's. Options gets the port's own boxes (`screenmenu.c`) for keys
and screen settings, kept in `jvppbport.ini` (`settings.c`), so the save
file stays exactly the GBA's.

## Android

The same C code is built with the Android NDK as `libmain.so`, loaded by
SDL's Java activity (`JVPPBPort/android/`, plus a small activity of our own
for the ROM file picker and vibration in `android.c`). `touch.c` lays out the
screen for the device's shape (the table at the top, a cabinet strip with
the flipper and pause buttons below) and turns fingers into pinball actions,
tilt swipes or mouse clicks for the menus. The table pictures made at first
start are drawn by hidden copies of the program; an Android app can't start
a program file, so there they are `fork()`ed copies of the app with no window. When the app goes to the background, the
game in progress is saved as the pause menu's SAVE does.

## Checking against the GBA

The port was checked against the mGBA emulator by dumping the game's state
every frame of the attract-mode demos on both and comparing: identical, ball
physics included. `JVPPBPORT_GBAVIEW=1` gives the original scrolling
240x160 view for such comparisons.
