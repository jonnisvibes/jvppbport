// JVPPBPort: makes the game's structs and unions follow the GBA compiler's
// layout rule. agbcc (like ARM's old APCS ABI) gives every struct and union an
// alignment of at least 4 bytes, so their sizes are always multiples of 4 and
// they start on 4-byte boundaries inside other structs. A PC compiler would
// pack, say, a struct of three u16s into 6 bytes where the GBA uses 8.
//
// The PC build force-includes this header into the game's source files only
// (see JVPPBPort/Makefile), never into the port's own files: SDL's and the C
// library's structs must keep their normal layout.
#ifndef GUARD_JVPPBPORT_LAYOUT_H
#define GUARD_JVPPBPORT_LAYOUT_H

// The system headers the game uses, read first so the macros below don't
// touch them.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define struct struct __attribute__((aligned(4)))
#define union union __attribute__((aligned(4)))

#endif // GUARD_JVPPBPORT_LAYOUT_H
