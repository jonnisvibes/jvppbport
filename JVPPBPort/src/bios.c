// JVPPBPort: C versions of the GBA BIOS routines the game calls
// (src/libagbsyscall.s on the GBA).
#include <math.h>
#include <string.h>
#include "global.h"
#include "jvppbport.h"

void CpuSet(const void *src, void *dest, u32 control)
{
    u32 count = control & 0x1FFFFF;
    int fixed = (control & CPU_SET_SRC_FIXED) != 0;
    u32 i;

    if (control & CPU_SET_32BIT)
    {
        const u32 *s = src;
        u32 *d = dest;
        for (i = 0; i < count; i++)
            d[i] = fixed ? *s : s[i];
    }
    else
    {
        const u16 *s = src;
        u16 *d = dest;
        for (i = 0; i < count; i++)
            d[i] = fixed ? *s : s[i];
    }
    PortWatchWrite(dest, count * ((control & CPU_SET_32BIT) ? 4 : 2));
}

void CpuFastSet(const void *src, void *dest, u32 control)
{
    // Like CpuSet in 32-bit mode, but the count rounds up to 8 words.
    u32 count = ((control & 0x1FFFFF) + 7) & ~7u;

    CpuSet(src, dest, CPU_SET_32BIT | (control & CPU_FAST_SET_SRC_FIXED) | count);
}

void LZ77UnCompWram(const void *src, void *dest)
{
    const u8 *s = src;
    u8 *d = dest;
    u32 size = (s[1] | (s[2] << 8) | (s[3] << 16));
    u32 out = 0;
    int bit;

    s += 4;
    while (out < size)
    {
        u8 flags = *s++;
        for (bit = 7; bit >= 0 && out < size; bit--)
        {
            if (flags & (1 << bit))
            {
                int len = (s[0] >> 4) + 3;
                int disp = (((s[0] & 0xF) << 8) | s[1]) + 1;
                s += 2;
                while (len-- && out < size)
                {
                    d[out] = d[out - disp];
                    out++;
                }
            }
            else
            {
                d[out++] = *s++;
            }
        }
    }
    PortWatchWrite(dest, size);
    PortBoardBuffersWritten(dest, size);
}

void LZ77UnCompVram(const void *src, void *dest)
{
    LZ77UnCompWram(src, dest);
}

void RLUnCompWram(const void *src, void *dest)
{
    const u8 *s = src;
    u8 *d = dest;
    u32 size = (s[1] | (s[2] << 8) | (s[3] << 16));
    u32 out = 0;

    s += 4;
    while (out < size)
    {
        u8 flag = *s++;
        if (flag & 0x80)
        {
            int len = (flag & 0x7F) + 3;
            u8 value = *s++;
            while (len-- && out < size)
                d[out++] = value;
        }
        else
        {
            int len = (flag & 0x7F) + 1;
            while (len-- && out < size)
                d[out++] = *s++;
        }
    }
    PortWatchWrite(dest, size);
}

void RLUnCompVram(const void *src, void *dest)
{
    RLUnCompWram(src, dest);
}

u16 Sqrt(u32 num)
{
    u32 root = 0, bit = 1u << 30;

    while (bit > num)
        bit >>= 2;
    while (bit)
    {
        if (num >= root + bit)
        {
            num -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

// The BIOS's arctangent of a ratio between -1 and 1 (1.14 fixed point), in
// 1/0x10000 turns: a polynomial in -ratio^2, worked in integers. Its rounding
// differs slightly from a real atan(), and the ball physics depend on it
// (speed capping), so it must be the same integer steps.
static s32 BiosArcTan(s32 ratio)
{
    static const s32 sCoeffs[] = {0x91C, 0xFB6, 0x16AA, 0x2081, 0x3651, 0xA2F9};
    s32 a = -((ratio * ratio) >> 14);
    s32 b = ((0xA9 * a) >> 14) + 0x390;
    unsigned i;

    for (i = 0; i < sizeof(sCoeffs) / sizeof(sCoeffs[0]); i++)
        b = ((b * a) >> 14) + sCoeffs[i];
    return (ratio * b) >> 16;
}

u16 ArcTan2(s16 x, s16 y)
{
    // The angle of (x, y) with a full turn = 0x10000, the way the BIOS
    // computes it: the arctangent of y/x or x/y (whichever is at most 1),
    // moved into the right eighth of the circle.
    if (y == 0)
        return x >= 0 ? 0 : 0x8000;
    if (x == 0)
        return y >= 0 ? 0x4000 : 0xC000;
    if (y >= 0)
    {
        if (x >= 0 ? x >= y : -x >= y)
            return BiosArcTan((y << 14) / x) + (x >= 0 ? 0 : 0x8000);
        return 0x4000 - BiosArcTan((x << 14) / y);
    }
    if (x <= 0 ? -x > -y : x >= -y)
        return BiosArcTan((y << 14) / x) + (x <= 0 ? 0x8000 : 0x10000);
    return 0xC000 - BiosArcTan((x << 14) / y);
}

void RegisterRamReset(u32 resetFlags)
{
    if (resetFlags & RESET_PALETTE)
        memset(gPortPltt, 0, sizeof(gPortPltt));
    if (resetFlags & RESET_VRAM)
        memset(gPortVram, 0, sizeof(gPortVram));
    if (resetFlags & RESET_OAM)
        memset(gPortOam, 0, sizeof(gPortOam));
    if (resetFlags & RESET_REGS)
    {
        memset(gPortIo, 0, sizeof(gPortIo));
        REG_DISPCNT = DISPCNT_FORCED_BLANK;
    }
    REG_KEYINPUT = KEYS_MASK;  // all buttons released (the register is active-low)
    // EWRAM/IWRAM are ordinary C globals on PC and start zeroed.
}

void SoftReset(u32 resetFlags)
{
    PortSoftReset();
}

void VBlankIntrWait(void)
{
    PortVBlankIntrWait();
}
