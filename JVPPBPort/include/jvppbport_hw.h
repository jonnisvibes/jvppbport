// JVPPBPort: stand-ins for the GBA's memory-mapped hardware.
// On the GBA these live at fixed addresses (0x04000000 for I/O, 0x06000000 for
// VRAM, ...). On PC they are ordinary arrays, and include/gba/*.h point the
// game's address macros at them when JVPPBPORT is defined.
#ifndef GUARD_JVPPBPORT_HW_H
#define GUARD_JVPPBPORT_HW_H

#include <stdint.h>

#define PORT_IO_SIZE   0x400
#define PORT_PLTT_SIZE 0x400
#define PORT_VRAM_SIZE 0x18000
#define PORT_OAM_SIZE  0x400

extern uint8_t gPortIo[PORT_IO_SIZE];
extern uint8_t gPortPltt[PORT_PLTT_SIZE];
extern uint8_t gPortVram[PORT_VRAM_SIZE];
extern uint8_t gPortOam[PORT_OAM_SIZE];

extern void *gPortSoundInfoPtr;
extern uint16_t gPortIntrCheck;
extern void *gPortIntrVector;

// Performs a DMA transfer immediately (the game never uses HBlank DMA).
void PortDmaSet(int channel, const void *src, void *dest, uint32_t control);

#endif // GUARD_JVPPBPORT_HW_H
