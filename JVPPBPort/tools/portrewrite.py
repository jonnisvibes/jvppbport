#!/usr/bin/env python3
# JVPPBPort: small automatic edits to the game's C code for the static table
# view, applied while building the PC version (after the C preprocessor, see
# JVPPBPort/Makefile). pret's source files stay untouched, and the GBA build
# never sees these edits. The functions the edits call are in
# JVPPBPort/src/staticview.c, declared in include/global.h.
#
# Rules:
#  1. Sprite positions keep their full 16-bit value ("extended OAM"):
#       gOamBuffer[I].x = E;   ->  PortOamSetX(&gOamBuffer[I], E);
#       gOamBuffer[I].y += E;  ->  PortOamAddY(&gOamBuffer[I], E);
#     (also through `struct OamData *` variables: oamData->y = E;).
#     The GBA stores only 8 bits of y and 9 of x, so the table's lower half
#     would wrap around.
#  2. Drawing code reads the camera through PortDrawCam_<field>(), which in
#     the static view returns a pretend camera at -2048 instead, so every
#     sprite that follows the table ends up at (table position + 2048), far
#     from the hidden (y = 200) and screen-fixed sprites:
#       gCurrentPinballGame->cameraYOffset  ->  PortDrawCam_cameraYOffset(gCurrentPinballGame->cameraYOffset)
#     Only reads are changed, and not in the files that run the camera itself.
#  3. "Hide it if it's below the screen" clamps leave table sprites alone:
#       if (group->baseY >= 200)  ->  if (PortScreenValue(group->baseY) && group->baseY >= 200)
#  4. Board lights the game only redraws while the camera is near them are
#     redrawn all the time (the static view draws the board's rows from its
#     own copy, see staticview.c, so these writes can't spoil a visible row):
#       hudSpriteBaseY > 83  ->  (PortTableView() || hudSpriteBaseY > 83)
#
# Usage: portrewrite.py SOURCE_NAME IN.c OUT.c
import re
import sys

CAMERA_FILES = {'all_board_process2.c', 'all_board_pinball_game_main.c', 'save_and_restore_game.c'}
CAMERA_FIELDS = r'(cameraXOffset|cameraYOffset|cameraBaseX|cameraBaseY)'


def find_close(text, i, open_ch, close_ch):
    """Index just past the bracket matching the one at text[i]."""
    depth = 0
    while i < len(text):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError('unbalanced brackets')


def statement_end(text, i):
    """Index of the `;` that ends the expression starting at text[i]."""
    depth = 0
    while True:
        c = text[i]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ';' and depth == 0:
            return i
        i += 1


def rewrite_oam(text, pointer_names):
    out = []
    pos = 0
    target = re.compile(r'gOamBuffer\s*\[|\b(' + '|'.join(map(re.escape, pointer_names)) + r')\s*->' if pointer_names
                        else r'gOamBuffer\s*\[')
    count = 0
    while True:
        m = target.search(text, pos)
        if not m:
            break
        start = m.start()
        if text.startswith('gOamBuffer', start):
            bracket = text.index('[', start)
            end = find_close(text, bracket, '[', ']')
            lvalue_ptr = '&' + text[start:end]
            rest = re.match(r'\s*\.\s*(x|y)\s*(=(?!=)|\+=|-=)', text[end:])
        else:
            end = m.end() - 2  # just before '->'
            lvalue_ptr = m.group(1)
            rest = re.match(r'\s*->\s*(x|y)\s*(=(?!=)|\+=|-=)', text[end:])
        if not rest:
            out.append(text[pos:m.end()])
            pos = m.end()
            continue
        expr_start = end + rest.end()
        semi = statement_end(text, expr_start)
        expr = text[expr_start:semi].strip()
        axis = rest.group(1).upper()
        op = rest.group(2)
        if op == '=':
            call = f'PortOamSet{axis}({lvalue_ptr}, ({expr}))'
        elif op == '+=':
            call = f'PortOamAdd{axis}({lvalue_ptr}, ({expr}))'
        else:
            call = f'PortOamAdd{axis}({lvalue_ptr}, -({expr}))'
        out.append(text[pos:start])
        out.append(call)
        pos = semi
        count += 1
    out.append(text[pos:])
    return ''.join(out), count


def rewrite_camera(text):
    # Reads only: skip `field =`, `field +=`, `field++`, `++field` and so on.
    pattern = re.compile(r'(?<![+-]{2})gCurrentPinballGame\s*->\s*' + CAMERA_FIELDS
                         + r'(?!\s*(?:=(?!=)|\+=|-=|\+\+|--))')
    return pattern.subn(lambda m: f'PortDrawCam_{m.group(1)}({m.group(0)})', text)


def rewrite_clamps(text):
    pattern = re.compile(r'if\s*\(\s*((?:\w+\s*(?:->|\.)\s*)+base[XY])\s*(>=?)\s*(-?\d+)\s*\)')
    return pattern.subn(lambda m: f'if (PortScreenValue({m.group(1)}) && {m.group(1)} {m.group(2)} {m.group(3)})', text)


def rewrite_light_checks(text):
    pattern = re.compile(r'gCurrentPinballGame\s*->\s*hudSpriteBaseY\s*(<=|>=|<|>)\s*(0x[0-9A-Fa-f]+|\d+)')
    return pattern.subn(lambda m: f'(PortTableView() || {m.group(0)})', text)


def main():
    name, src, dst = sys.argv[1:4]
    text = open(src).read()
    pointer_names = sorted(set(re.findall(r'struct\s+(?:__attribute__\s*\(\(\w+\(\d+\)\)\)\s*)?OamData\s*\*\s*(\w+)', text)))
    text, n_oam = rewrite_oam(text, pointer_names)
    n_cam = 0
    if name not in CAMERA_FILES:
        text, n_cam = rewrite_camera(text)
    text, n_clamp = rewrite_clamps(text)
    text, n_light = rewrite_light_checks(text)
    open(dst, 'w').write(text)
    if '-v' in sys.argv:
        print(f'{name}: {n_oam} sprite writes, {n_cam} camera reads, {n_clamp} clamps, {n_light} light checks')


if __name__ == '__main__':
    main()
