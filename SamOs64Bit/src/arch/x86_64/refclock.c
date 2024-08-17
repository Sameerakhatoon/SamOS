// The reference clock for the TSC and LAPIC timer
// calibrations: HPET, else the ACPI PM timer, else PIT channel 2.
//
// Measured both against PIT channel 2 only. Many PCs
// since about 2018 have no 8254 left (or hide it behind a firmware
// option), and port 0x61 then never shows OUT2: the LAPIC timer
// measured 0, the kernel fell back to a PIT tick that never came, and
// udelay ran on a guessed 1 GHz. The HPET and the PM timer are
// in the ACPI tables of every PC that has them, and both run from
// fixed crystals:
//   - HPET: a 32- or 64-bit main counter at 0xF0, its period in
//     femtoseconds in the top half of the capabilities register;
//     ENABLE_CNF (general configuration bit 0) starts it. Firmware
//     often leaves it stopped. Legacy replacement routing (bit 1) and
//     the comparators are left alone.
//   - PM timer: 3.579545 MHz, 24 bits (32 with FADT TMR_VAL_EXT), in
//     I/O space on every PC seen so far (memory space handled too).
// Under QEMU TCG all three follow the virtual clock, as the LAPIC
// timer does (the TSC follows the host's).
//
// Each candidate must be seen counting before it is chosen: a stopped
// or absent counter falls through to the next one.

#include "arch/x86_64/refclock.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "arch/arch.h"
#include "acpi/acpi.h"
#include "boot/cmdline.h"
#include "io/serial.h"
#include "string/string.h"

#define HPET_REG_CAP        0x000
#define HPET_REG_CONFIG     0x010
#define HPET_REG_COUNTER    0x0F0
#define HPET_CAP_64BIT      (1ull << 13)
#define HPET_CONFIG_ENABLE  1ull
#define HPET_MAX_PERIOD_FS  100000000ull    // spec: at most 100 ns
#define FS_PER_SECOND       1000000000000000ull

#define PMTIMER_HZ          3579545ull

#define REF_WINDOW_DIVISOR  20               // 50 ms windows
#define REF_ROUNDS          3
#define REF_MAX_POLLS       50000000u
#define REF_PROBE_POLLS     1000000u

static enum refclock_source source;
static uint64_t ref_hz;                      // ticks per second
static uint64_t ref_mask;                    // counter width
static volatile uint8_t* hpet_mmio;
static volatile uint32_t* pmtmr_mmio;        // PM timer in memory space
static uint16_t pmtmr_port;

static uint64_t hpet_read(void){
    if(ref_mask == 0xFFFFFFFFull){
        return *(volatile uint32_t*)(hpet_mmio + HPET_REG_COUNTER);
    }
    return *(volatile uint64_t*)(hpet_mmio + HPET_REG_COUNTER);
}

static uint64_t pmtimer_read(void){
    uint32_t v = pmtmr_mmio ? *pmtmr_mmio : insdw(pmtmr_port);
    return v & ref_mask;
}

static uint64_t ref_read(void){
    return source == REFCLOCK_HPET ? hpet_read() : pmtimer_read();
}

// The counter moves within REF_PROBE_POLLS reads.
static bool ref_counts(void){
    uint64_t first = ref_read();
    for(uint32_t i = 0; i < REF_PROBE_POLLS; i++){
        if(ref_read() != first){
            return true;
        }
    }
    return false;
}

static bool hpet_try(void){
    const struct acpi_info* a = acpi_info();
    if(!a->hpet_phys){
        return false;
    }
    hpet_mmio = paging_map_mmio(a->hpet_phys, 0x400, PAGING_CACHE_DISABLED);
    if(!hpet_mmio){
        return false;
    }
    uint64_t cap = *(volatile uint64_t*)(hpet_mmio + HPET_REG_CAP);
    uint64_t period = cap >> 32;
    if(cap == ~0ull || period == 0 || period > HPET_MAX_PERIOD_FS){
        serial_write("refclock: hpet at ");
        serial_write_hex(a->hpet_phys);
        serial_write(" has a bad period, not used\n");
        return false;
    }
    volatile uint64_t* config = (volatile uint64_t*)(hpet_mmio + HPET_REG_CONFIG);
    if(!(*config & HPET_CONFIG_ENABLE)){
        *config |= HPET_CONFIG_ENABLE;
    }
    source   = REFCLOCK_HPET;
    ref_mask = (cap & HPET_CAP_64BIT) ? ~0ull : 0xFFFFFFFFull;
    ref_hz   = FS_PER_SECOND / period;
    if(!ref_counts()){
        serial_write("refclock: hpet does not count, not used\n");
        source = REFCLOCK_NONE;
        return false;
    }
    serial_write("refclock: hpet at ");
    serial_write_hex(a->hpet_phys);
    serial_write(", ");
    serial_write(itoa((int)(ref_hz / 1000)));
    serial_write(ref_mask == ~0ull ? " kHz, 64-bit\n" : " kHz, 32-bit\n");
    return true;
}

static bool pmtimer_try(void){
    const struct acpi_info* a = acpi_info();
    const struct acpi_gas* g = &a->pm_tmr;
    if(!a->have_fadt || !g->address){
        return false;
    }
    pmtmr_mmio = NULL;
    if(g->space_id == ACPI_GAS_IO && g->address <= 0xFFFC){
        pmtmr_port = (uint16_t)g->address;
    } else if(g->space_id == ACPI_GAS_MEMORY){
        pmtmr_mmio = paging_map_mmio(g->address, 4, PAGING_CACHE_DISABLED);
        if(!pmtmr_mmio){
            return false;
        }
    } else {
        return false;
    }
    source   = REFCLOCK_PMTIMER;
    ref_mask = a->pm_tmr_32bit ? 0xFFFFFFFFull : 0xFFFFFFull;
    ref_hz   = PMTIMER_HZ;
    if(!ref_counts()){
        serial_write("refclock: pm timer does not count, not used\n");
        source = REFCLOCK_NONE;
        return false;
    }
    serial_write("refclock: acpi pm timer at ");
    serial_write(pmtmr_mmio ? "memory " : "port ");
    serial_write_hex(g->address);
    serial_write(a->pm_tmr_32bit ? ", 3579 kHz, 32-bit\n" : ", 3579 kHz, 24-bit\n");
    return true;
}

void refclock_init(void){
    const char* want = boot_cmdline_value("samos.refclock");
    int start = REFCLOCK_HPET;
    if(want && strncmp(want, "pmtimer", 7) == 0){
        start = REFCLOCK_PMTIMER;
    } else if(want && strncmp(want, "pit", 3) == 0){
        start = REFCLOCK_PIT;
    }
    source = REFCLOCK_NONE;
    if(start <= REFCLOCK_HPET && hpet_try()){
        return;
    }
    if(start <= REFCLOCK_PMTIMER && pmtimer_try()){
        return;
    }
    if(pit_present()){
        source = REFCLOCK_PIT;
        serial_write("refclock: pit channel 2\n");
        return;
    }
    serial_write("refclock: none (no hpet, pm timer or pit)\n");
}

enum refclock_source refclock_source(void){
    return source;
}

const char* refclock_short_name(void){
    switch(source){
    case REFCLOCK_HPET:    return "hpet";
    case REFCLOCK_PMTIMER: return "pmtimer";
    case REFCLOCK_PIT:     return "pit";
    default:               return "none";
    }
}

const char* refclock_long_name(void){
    switch(source){
    case REFCLOCK_HPET:    return "HPET";
    case REFCLOCK_PMTIMER: return "ACPI PM timer";
    case REFCLOCK_PIT:     return "PIT channel 2";
    default:               return "no clock";
    }
}

// One window: wait for a reference tick edge, sample `read`, wait
// until a twentieth of a second of reference ticks has passed, sample
// again. The reference may wrap once (24-bit PM timer: every 4.7 s).
static uint64_t ref_measure_once(uint64_t (*read)(void)){
    uint64_t window = ref_hz / REF_WINDOW_DIVISOR;
    uint32_t polls = 0;
    uint64_t r0 = ref_read(), r;
    while((r = ref_read()) == r0){
        if(++polls == REF_MAX_POLLS){
            return 0;
        }
    }
    uint64_t start = read();
    uint64_t elapsed;
    while((elapsed = (ref_read() - r) & ref_mask) < window){
        if(++polls == REF_MAX_POLLS){
            return 0;
        }
    }
    uint64_t end = read();
    // (end - start) is at most a few 10^8 here and ref_hz at most
    // 10^9 (a 1 ns HPET period), so the product fits.
    return (end - start) * ref_hz / elapsed;
}

uint64_t refclock_measure_hz(uint64_t (*read)(void)){
    if(source == REFCLOCK_PIT){
        return pit_measure_hz(read);
    }
    if(source == REFCLOCK_NONE){
        return 0;
    }
    uint64_t r[REF_ROUNDS];
    for(int i = 0; i < REF_ROUNDS; i++){
        r[i] = ref_measure_once(read);
        if(!r[i]){
            return 0;
        }
    }
    for(int i = 0; i < REF_ROUNDS; i++){
        for(int j = i + 1; j < REF_ROUNDS; j++){
            if(r[j] < r[i]){
                uint64_t t = r[i];
                r[i] = r[j];
                r[j] = t;
            }
        }
    }
    return r[REF_ROUNDS / 2];
}
