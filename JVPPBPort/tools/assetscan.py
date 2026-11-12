#!/usr/bin/env python3
# JVPPBPort: checks that a built program contains none of the ROM's data.
# Every 32-byte block of the ROM (at 32-byte steps) that has at least 8
# different byte values is looked for in the program, at every offset; any
# ROM data copied in of 63 bytes or more contains such a block. Blocks
# with fewer values (blank space, plain fills) are skipped, since they show
# up anywhere by chance and carry no picture or sound. Blocks that only
# count up or down in equal steps (8, 9, 10 ... as 8-, 16- or 32-bit numbers)
# are skipped too: they are plain number tables any program can contain
# (SDL2 for Android has 8 to 15 as 32-bit numbers, as the ROM does).
#
# Usage: assetscan.py PROGRAM [ROM]   (ROM defaults to baserom.gba)
# PROGRAM can also be a folder: then every file in it (and below) is checked.
# Prints what it finds; exits 1 if anything was found.
import os
import sys

BLOCK = 32
MIN_VALUES = 8


def counting(b):
    for size in (1, 2, 4):
        n = [int.from_bytes(b[i:i + size], 'little') for i in range(0, BLOCK, size)]
        steps = {(n[i + 1] - n[i]) % (1 << (8 * size)) for i in range(len(n) - 1)}
        if len(steps) == 1:
            return True
    return False


def scan(path, blocks):
    program = open(path, 'rb').read()
    found = []
    for i in range(len(program) - BLOCK + 1):
        off = blocks.get(program[i:i + BLOCK])
        if off is not None:
            found.append((i, off))
    return len(program), found


def main():
    rom = open(sys.argv[2] if len(sys.argv) > 2 else 'baserom.gba', 'rb').read()
    blocks = {}
    for off in range(0, len(rom) - BLOCK + 1, BLOCK):
        b = rom[off:off + BLOCK]
        if len(set(b)) >= MIN_VALUES and not counting(b):
            blocks.setdefault(b, off)
    target = sys.argv[1]
    if os.path.isdir(target):
        files = sorted(os.path.join(d, f) for d, dirs, names in os.walk(target)
                       if '.git' not in d.split(os.sep) for f in names)
        total, bad = 0, 0
        for path in files:
            size, found = scan(path, blocks)
            total += size
            if found:
                bad += 1
                print(f'{path}: {len(found)} found, e.g. offset {found[0][0]:#x} = ROM offset {found[0][1]:#x}')
        print(f'{target}: {len(files)} files, {total} bytes, {len(blocks)} ROM blocks looked for, '
              f'{bad} files with ROM data found')
        sys.exit(1 if bad else 0)
    size, found = scan(target, blocks)
    print(f'{target}: {size} bytes, {len(blocks)} ROM blocks looked for, {len(found)} found')
    for i, off in found[:20]:
        print(f'  program offset {i:#x} = ROM offset {off:#x}')
    sys.exit(1 if found else 0)


if __name__ == '__main__':
    main()
