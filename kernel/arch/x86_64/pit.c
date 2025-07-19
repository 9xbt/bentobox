#include <stdint.h>
#include <kernel/arch/x86_64/io.h>

#define PIT_CH2_DATA    0x42
#define PIT_CMD_PORT    0x43
#define PIT_SPEAKER_PORT 0x61
#define PIT_FREQ        1193182

void pit_oneshot(uint64_t microseconds) {
    uint16_t count = (PIT_FREQ * (uint64_t)microseconds + 500000) / 1000000;
    
    uint8_t speaker_state = inb(PIT_SPEAKER_PORT);
    outb(PIT_SPEAKER_PORT, (speaker_state & 0xDD) | 0x01);
    
    outb(PIT_CMD_PORT, 0xB2);
    outb(PIT_CH2_DATA, count & 0xFF);
    outb(PIT_CH2_DATA, count >> 8);
    
    speaker_state = inb(PIT_SPEAKER_PORT);
    outb(PIT_SPEAKER_PORT, speaker_state & 0xDE);
    outb(PIT_SPEAKER_PORT, speaker_state | 0x01);
    
    if (inb(PIT_SPEAKER_PORT) & 0x20) {
        while (inb(PIT_SPEAKER_PORT) & 0x20);
    } else {
        while (!(inb(PIT_SPEAKER_PORT) & 0x20));
    }
}