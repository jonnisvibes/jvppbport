// JVPPBPort: the GBA's memory-mapped hardware as plain arrays, plus DMA.
#include <string.h>
#include "global.h"

__attribute__((aligned(16))) uint8_t gPortIo[PORT_IO_SIZE];
__attribute__((aligned(16))) uint8_t gPortPltt[PORT_PLTT_SIZE];
__attribute__((aligned(16))) uint8_t gPortVram[PORT_VRAM_SIZE];
__attribute__((aligned(16))) uint8_t gPortOam[PORT_OAM_SIZE];

void *gPortSoundInfoPtr;
uint16_t gPortIntrCheck;
void *gPortIntrVector;

// On the GBA this is the ARM interrupt dispatcher copied into IWRAM at boot.
// The port calls the game's handlers directly, so it is only a dummy source.
u32 IntrMain[0x200];

#include <stdio.h>
#include <stdlib.h>
#include "jvppbport.h"

// Debug aid: JVPPBPORT_WATCH_VRAM=0x7FE0 prints a call stack whenever a copy
// changes the 32 bytes at that VRAM offset.
void PortWatchWrite(const void *dest, uint32_t size)
{
    static long watch = -2;
    static u8 last[32];
    const u8 *d = dest;

    if (watch == -2)
        watch = getenv("JVPPBPORT_WATCH_VRAM") ? strtol(getenv("JVPPBPORT_WATCH_VRAM"), NULL, 0) : -1;
    if (watch < 0)
        return;
    if (d + size <= &gPortVram[watch] || d >= &gPortVram[watch] + 32)
        return;
    if (memcmp(last, &gPortVram[watch], 32) != 0)
    {
        int i;
        memcpy(last, &gPortVram[watch], 32);
        fprintf(stderr, "JVPPBPort: VRAM %#lx changed to", watch);
        for (i = 0; i < 8; i++)
            fprintf(stderr, " %02x", last[i]);
        fprintf(stderr, "... call stack:\n");
        PortPrintCallStack();
    }
}

void PortDmaSet(int channel, const void *src, void *dest, uint32_t control)
{
    u16 flags = control >> 16;
    u32 count = control & 0xFFFF;
    int unit, srcStep, destStep;
    const u8 *s = src;
    u8 *d = dest;
    u32 i;

    if (!(flags & DMA_ENABLE))
        return;
    // Only immediate transfers do anything. Timed ones (sound FIFO) are
    // ignored until audio exists.
    if ((flags & DMA_START_MASK) != DMA_START_NOW)
        return;

    if (count == 0)
        count = channel == 3 ? 0x10000 : 0x4000;
    unit = (flags & DMA_32BIT) ? 4 : 2;

    // The game sometimes copies from address 0 (the GBA BIOS) to blank memory.
    // DMA can't read the BIOS, so the hardware gives back junk; zeros do here.
    if ((uintptr_t)src < 0x4000)
    {
        static const u32 zero;
        s = (const u8 *)&zero;
        flags = (flags & ~(DMA_SRC_DEC | DMA_SRC_FIXED)) | DMA_SRC_FIXED;
    }

    switch (flags & (DMA_SRC_DEC | DMA_SRC_FIXED))
    {
    case DMA_SRC_DEC:   srcStep = -unit; break;
    case DMA_SRC_FIXED: srcStep = 0; break;
    default:            srcStep = unit; break;
    }
    switch (flags & DMA_DEST_RELOAD)
    {
    case DMA_DEST_DEC:   destStep = -unit; break;
    case DMA_DEST_FIXED: destStep = 0; break;
    default:             destStep = unit; break;
    }

    if (srcStep == unit && destStep == unit)
    {
        memmove(d, s, count * unit);
    }
    else
    {
        for (i = 0; i < count; i++)
        {
            memcpy(d, s, unit);
            s += srcStep;
            d += destStep;
        }
    }
    PortWatchWrite(dest, count * unit);
    if (srcStep == unit && destStep == unit)
        PortStreamedWrite(s, dest, count * unit);
    if (dest == gPortOam)
        PortSnapshotOam();
}
