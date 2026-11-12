# Licenses

This repository mixes code from different sources, under different terms.

## Our code: CC0 1.0 (public domain)

Everything written for this port is dedicated to the public domain under
[CC0 1.0 Universal](LICENSE-CC0.txt). You may copy, change and use it for
any purpose, without asking and without credit (credit is still welcome).
That covers:

- `JVPPBPort/src/`, `JVPPBPort/include/`, `JVPPBPort/tools/`,
  `JVPPBPort/windows/`, `JVPPBPort/android/` and `JVPPBPort/Makefile`;
- `docs/`, the top-level `Makefile`, `README.md`, `CHANGELOG.md` and this file;
- the changes made to pret's files for the port: the code inside
  `#ifdef JVPPBPORT` / `#ifndef JVPPBPORT` blocks in `src/` and `include/`.

## The game code: pret's decompilation (no license)

`src/`, `include/` and `charmap.txt` (apart from the port's changes above)
come from [pret/pokepinballrs](https://github.com/pret/pokepinballrs), which
has no license. They are not ours to license and are not covered by CC0.
The same goes for `JVPPBPort/gen/`, which is generated from pret's data
files (table layouts and addresses only; no game data).

## tools/preproc: MIT

`tools/preproc/` is pret's text preprocessor, by YamaArashi, under the MIT
license in [tools/preproc/LICENSE](tools/preproc/LICENSE).

## The game itself

No graphics, sound, text or other data from the game is in this
repository or in the program. Pokémon Pinball: Ruby & Sapphire is © Nintendo,
Creatures and GAME FREAK; Pokémon is a trademark of Nintendo, Creatures and
GAME FREAK. This is a fan project, not affiliated with or endorsed by them.

## Libraries in the program

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
