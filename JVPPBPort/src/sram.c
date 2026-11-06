// JVPPBPort: cartridge save memory, kept in a file.
// Replaces src/agb_sram.c, which copies its own machine code into RAM to run
// it and so can't work off the GBA. The game addresses save memory as
// SRAM + offset (0x0E000000 + offset); here that offset indexes a buffer that
// is loaded from and written back to jvppbport.sav in the data folder
// (system.c), or to the file named by JVPPBPORT_SAVE (test runs use that to
// leave the real save alone).
//
// The file is the cartridge's 32 KB, in the GBA's format: emulators and flash
// carts can use it, and their saves work here. (The one part whose layout
// differs on PC, a game saved from the pause menu, is converted when it's
// written and read; see save_and_restore_game.c.) Older 64 KB files from the
// PC build still load.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "agb_sram.h"
#include "jvppbport.h"

#define PORT_SRAM_SIZE 0x8000

static u8 sSram[PORT_SRAM_SIZE];
static bool8 sSramLoaded;
static char sSavePath[1100];

const char *PortSavePath(void)
{
    if (!sSavePath[0])
    {
        if (getenv("JVPPBPORT_SAVE"))
            snprintf(sSavePath, sizeof(sSavePath), "%s", getenv("JVPPBPORT_SAVE"));
        else
            PortDataPath(sSavePath, sizeof(sSavePath), "jvppbport.sav");
    }
    return sSavePath;
}

// Forgets the save file's name and contents (a forked copy of the program
// with its own save, see gamedata.c).
void PortSaveForget(void)
{
    sSavePath[0] = '\0';
    sSramLoaded = FALSE;
}

static u8 *SramAddr(const void *gbaAddr, u32 size)
{
    uintptr_t offset = (uintptr_t)gbaAddr - SRAM;

    if (!sSramLoaded)
    {
        FILE *f = fopen(PortSavePath(), "rb");

        memset(sSram, 0xFF, sizeof(sSram));  // blank cartridge memory reads as 0xFF
        if (f)
        {
            if (fread(sSram, 1, sizeof(sSram), f) == 0)
                memset(sSram, 0xFF, sizeof(sSram));
            fclose(f);
        }
        sSramLoaded = TRUE;
    }
    if (offset + size > PORT_SRAM_SIZE)
    {
        fprintf(stderr, "JVPPBPort: save access out of range (offset %#lx, size %#x)\n",
                (unsigned long)offset, size);
        return NULL;
    }
    return &sSram[offset];
}

static void PortReadSram(const u8 *src, u8 *dest, u32 size)
{
    u8 *sram = SramAddr(src, size);

    if (sram)
        memcpy(dest, sram, size);
}

void SetSramFastFunc(void)
{
    ReadSramFast = PortReadSram;
}

// Writes the whole file again (to a temporary file first, then swapped in,
// so a crash or power cut mid-write can't leave a half-written save).
u32 WriteAndVerifySramFast(const u8 *src, u8 *dest, u32 size)
{
    u8 *sram = SramAddr(dest, size);
    char tmp[1110];
    FILE *f;

    if (!sram)
        return 1;
    memcpy(sram, src, size);
    snprintf(tmp, sizeof(tmp), "%s.tmp", PortSavePath());
    f = fopen(tmp, "wb");
    if (!f || fwrite(sSram, 1, sizeof(sSram), f) != sizeof(sSram))
    {
        fprintf(stderr, "JVPPBPort: could not write the save file %s\n", tmp);
        if (f)
            fclose(f);
        return 1;
    }
    fclose(f);
    if (!PortReplaceFile(tmp, PortSavePath()))
    {
        fprintf(stderr, "JVPPBPort: could not replace the save file %s\n", PortSavePath());
        return 1;
    }
    return 0;
}
