#!/usr/bin/env python3
# JVPPBPort: draws the Windows icon (a red and blue ball, for Ruby and
# Sapphire) as a .ico file with 16, 24, 32, 48, 64 and 256 pixel versions.
# Drawn from shapes, not taken from the game's graphics.
#
# Usage: makeicon.py OUT.ico
#        makeicon.py --android-foreground SIZE OUT.png   (front layer of the
#        Android launcher icon: the ball within the middle 60%, the part
#        every launcher shape shows)
import math
import struct
import sys
import zlib

RED = (226, 40, 32)     # top: Ruby
BLUE = (36, 92, 204)    # bottom: Sapphire
WHITE = (250, 250, 250)
OUTLINE = (24, 24, 24)
SHADE = 0.82   # how dark the lower-right edge gets


def pixel(x, y):
    """Color (r, g, b, a) at (x, y) on a ball of radius 1 centered at 0."""
    d = math.hypot(x, y)
    if d > 1:
        return (0, 0, 0, 0)
    if d > 0.88:
        return OUTLINE + (255,)                 # rim
    if d < 0.34:
        if d > 0.24:
            return OUTLINE + (255,)             # ring around the button
        return (WHITE if d < 0.16 else (214, 214, 214)) + (255,)
    if abs(y) < 0.08:
        return OUTLINE + (255,)                 # band
    if y < 0 and math.hypot(x + 0.42, y + 0.48) < 0.14:
        return (255, 255, 255, 255)             # shine
    base = RED if y < 0 else BLUE
    light = 1 - (1 - SHADE) * max(0.0, (x + y) / 1.6 + 0.2)  # lit from the top left
    return tuple(min(255, int(c * light)) for c in base) + (255,)


def render(size, zoom=1.04):
    """size x size RGBA, 4x4 samples per pixel for smooth edges."""
    out = bytearray()
    n = 4
    for py in range(size):
        out.append(0)  # PNG filter: none
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(n):
                for sx in range(n):
                    x = ((px + (sx + 0.5) / n) / size) * 2 - 1
                    y = ((py + (sy + 0.5) / n) / size) * 2 - 1
                    r, g, b, a = pixel(x * zoom, y * zoom)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            a = acc[3] / (n * n)
            if acc[3]:
                out += bytes([int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(a)])
            else:
                out += bytes(4)
    return bytes(out)


def png(size, zoom=1.04):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(render(size, zoom), 9)) + chunk(b'IEND', b''))


def main():
    if sys.argv[1] == '--android-foreground':
        with open(sys.argv[3], 'wb') as f:
            f.write(png(int(sys.argv[2]), 1 / 0.6))
        return
    sizes = [16, 24, 32, 48, 64, 256]
    images = [png(s) for s in sizes]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = b''
    for s, img in zip(sizes, images):
        entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    with open(sys.argv[1], 'wb') as f:
        f.write(header + entries + b''.join(images))


if __name__ == '__main__':
    main()
