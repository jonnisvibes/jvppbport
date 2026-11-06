# Building

The build needs no ROM and no GBA toolchain: the game's data tables are
pre-laid-out in `JVPPBPort/gen/` (layouts and addresses only, no game data),
and the program takes the data itself from your ROM when it first starts.

Tested on Ubuntu 26.04 (under WSL2 on Windows 11).

## Linux

```sh
sudo apt install build-essential python3 libsdl2-dev zenity
make -j$(nproc)
build-JVPPBPort/jvppbport
```

`zenity` is only for the ROM file picker; without it, drop the ROM on the
window instead. If a file named `baserom.gba` is in the folder you start the
program from, it's used without asking.

On Linux the data folder (save, settings, data from your ROM, log) is
`build-JVPPBPort/`, next to the program.

## Windows (cross-compiled from Linux)

```sh
sudo apt install mingw-w64 curl
make win -j$(nproc)
```

This downloads SDL2's official MinGW development package into
`build-JVPPBPort-win/sdl/` (checked by its SHA-256) and builds
`build-JVPPBPort-win/JVPPBPort.exe`: one file with SDL2 and the C runtime
built in, needing no DLLs. The debug information is kept separately in
`build-JVPPBPort-win/JVPPBPort.debug`; keep it to read crash reports:

```sh
x86_64-w64-mingw32-addr2line -f -p -e build-JVPPBPort-win/JVPPBPort.debug ADDRESS...
```

(the addresses are the ones listed under the crash in `jvppbport.log`).

`make release` builds the Windows program and packs the release zips in
`release/`, with their SHA-256 sums.

## Clean

```sh
make clean
```

## Settings for testing

The program reads a few environment variables, mostly for automated tests
(none are needed to play). The useful ones:

| Variable | Effect |
|---|---|
| `JVPPBPORT_DATA_DIR=folder` | use another data folder (keeps tests away from your real save) |
| `JVPPBPORT_ROM=file` | take the ROM from this file at first start, without asking |
| `JVPPBPORT_GBAVIEW=1` | the original 240x160 scrolling view, as on the GBA |
| `JVPPBPORT_MUTE=1` | no sound device |
| `JVPPBPORT_CONSOLE=1` | Windows: show messages in a console instead of the log file |

The rest (scripted button presses, screenshots, game-state dumps, test
hooks) are described in the comments of `JVPPBPort/src/platform.c` and
`JVPPBPort/src/testhooks.c`.
