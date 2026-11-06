#!/usr/bin/env python3
# JVPPBPort, Windows build: rewrites syms2s.py's RAM variables (ram_syms.s)
# without weak symbols. On Linux they are weak so that a variable the C code
# defines itself wins; Windows' linker doesn't do that, so instead the
# variables the C code defines (listed by nm) are left out.
#
# Usage: winsyms.py RAM_SYMS.s DEFINED_NAMES.txt OUT.s
import re
import sys

src, defined_path, out = sys.argv[1:4]
defined = set(open(defined_path).read().split())
lines = open(src).read().split('\n')
result = []
i = 0
while i < len(lines):
    m = re.match(r'\t\.weak (\w+)$', lines[i])
    if m:
        # .weak NAME / .balign 16 / NAME: / .zero SIZE
        if m.group(1) not in defined:
            result += [f'\t.globl {m.group(1)}'] + lines[i + 1:i + 4]
        i += 4
        continue
    if 'GNU-stack' not in lines[i]:
        result.append(lines[i])
    i += 1
open(out, 'w').write('\n'.join(result))
