// 8254 PIT: the 100 Hz scheduler tick and the stopwatch the
// TSC is calibrated against.
//
// Not in the book: it never programs the PIT, so the 32-bit kernel
// ran at the BIOS default of 18.2 Hz and the 64-bit one had no timer.

#include "pit.h"
#include "io.h"
#include "tsc.h"

#define PIT_CHANNEL0   0x40
#define PIT_CHANNEL2   0x42
#define PIT_COMMAND    0x43
#define PIT_GATE_PORT  0x61        // bit 0 gate 2, bit 1 speaker, bit 5 OUT2

// One calibration window: 1193182 / 20 input clocks = 50 ms.
#define PIT_CAL_DIVISOR  20
#define PIT_CAL_ROUNDS   3
#define PIT_CAL_MAX_POLLS 50000000u

// Channel 0, lobyte/hibyte, mode 2 (rate generator): one IRQ0 every
// PIT_INPUT_HZ / hz input clocks.
void pit_init(uint32_t hz){
    uint32_t divisor = (PIT_INPUT_HZ + hz / 2) / hz;
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

// Channel 2 in mode 0 (interrupt on terminal count): OUT2, readable
// as bit 5 of port 0x61, goes high when the count runs out. Count the
// cycles of `read` (the TSC, or the LAPIC timer) in between.
static uint64_t pit_measure_once(uint64_t (*read)(void)){
    uint16_t latch = PIT_INPUT_HZ / PIT_CAL_DIVISOR;
    uint8_t gate = insb(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, (gate & ~0x02) | 0x01);     // gate on, speaker off
    outb(PIT_COMMAND, 0xB0);                         // channel 2, lo/hi, mode 0
    outb(PIT_CHANNEL2, latch & 0xFF);
    outb(PIT_CHANNEL2, latch >> 8);                  // counting starts here
    uint64_t start = read();
    uint32_t polls = 0;
    while(!(insb(PIT_GATE_PORT) & 0x20)){
        if(++polls == PIT_CAL_MAX_POLLS){
            outb(PIT_GATE_PORT, gate);
            return 0;
        }
    }
    uint64_t end = read();
    outb(PIT_GATE_PORT, gate);
    return (end - start) * PIT_CAL_DIVISOR;
}

// Is there an 8254 at all? Channel 2, mode 0, counting down
// from 0xFFFF with the gate on: a latched read must change within a
// few thousand reads. Without a PIT the ports float (0xFF) and port
// 0x61 never shows OUT2, which pit_measure_once would only notice
// after PIT_CAL_MAX_POLLS reads (seconds on real hardware).
bool pit_present(void){
    uint8_t gate = insb(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, (gate & ~0x02) | 0x01);
    outb(PIT_COMMAND, 0xB0);
    outb(PIT_CHANNEL2, 0xFF);
    outb(PIT_CHANNEL2, 0xFF);
    uint16_t first = 0;
    bool moved = false;
    for(int i = 0; i < 100000 && !moved; i++){
        outb(PIT_COMMAND, 0x80);                     // latch channel 2
        uint16_t v = insb(PIT_CHANNEL2);
        v |= (uint16_t)insb(PIT_CHANNEL2) << 8;
        if(i == 0){
            first = v;
        } else if(v != first){
            moved = true;
        }
    }
    outb(PIT_GATE_PORT, gate);
    return moved;
}

// Median of three windows, so one window stretched by the host
// descheduling the VM does not decide the frequency.
uint64_t pit_measure_hz(uint64_t (*read)(void)){
    uint64_t r[PIT_CAL_ROUNDS];
    for(int i = 0; i < PIT_CAL_ROUNDS; i++){
        r[i] = pit_measure_once(read);
        if(!r[i]){
            return 0;
        }
    }
    for(int i = 0; i < PIT_CAL_ROUNDS; i++){
        for(int j = i + 1; j < PIT_CAL_ROUNDS; j++){
            if(r[j] < r[i]){
                uint64_t t = r[i];
                r[i] = r[j];
                r[j] = t;
            }
        }
    }
    return r[PIT_CAL_ROUNDS / 2];
}

uint64_t pit_measure_tsc_hz(void){
    return pit_measure_hz(read_tsc);
}
