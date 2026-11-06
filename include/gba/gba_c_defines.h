#ifndef GUARD_GBA_C_DEFINES
#define GUARD_GBA_C_DEFINES

#include <stddef.h>

#ifdef JVPPBPORT
#include "jvppbport_hw.h"
#define IWRAM_DATA
#define EWRAM_DATA
#else
#define IWRAM_DATA __attribute__((section("iwram_data")))
#define EWRAM_DATA __attribute__((section("ewram_data")))
#endif

#define ALIGNED(n) __attribute__((aligned(n)))

#ifdef JVPPBPORT
#define SOUND_INFO_PTR (*(struct SoundInfo **)&gPortSoundInfoPtr)
#define INTR_CHECK     gPortIntrCheck
#define INTR_VECTOR    gPortIntrVector
#else
#define SOUND_INFO_PTR (*(struct SoundInfo **)0x3007FF0)
#define INTR_CHECK     (*(u16 *)0x3007FF8)
#define INTR_VECTOR    (*(void **)0x3007FFC)
#endif

#endif