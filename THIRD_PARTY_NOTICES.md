# Third-party notices

The Windows program (`JVPPBPort.exe`) and the Android app (`.apk`) have these
libraries built into them.

## SDL2

Simple DirectMedia Layer 2.32.10, https://www.libsdl.org/ (in the Android app
also SDL's Java code), under the zlib license:

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

```

## stb_image (Windows)

stb_image v2.32 by Sean Barrett, https://github.com/nothings/stb (file
`JVPPBPort/src/stb_image.h`, used to read `wallpaper.png`; not in the
Android app). It is available under two licenses, public domain
(Unlicense) or MIT, whichever you prefer; we use it under the MIT license:

```
Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## MinGW-w64 runtime and GCC runtime library (Windows)

The Windows program is built with MinGW-w64 (https://www.mingw-w64.org/), whose C
runtime start-up and support code is linked into it. Those files are in the
public domain or under permissive licenses (Zope Public License 2.1, MIT
style). Parts of GCC's runtime library (libgcc) are included under the GNU
GPL with the GCC Runtime Library Exception, which lets programs built with
GCC carry it under any license.

## Credits without included code

- GBA screen colors (the GBA filter): the color measurements by Pokefan531,
  as used by the "gba-color" shaders in RetroArch and other emulators.
- [pret/pokepinballrs](https://github.com/pret/pokepinballrs): the game
  code the port is built on (see LICENSE.md).
