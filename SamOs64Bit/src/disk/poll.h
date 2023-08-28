#ifndef DISK_POLL_H
#define DISK_POLL_H

// Deadlines for polling disk controllers. A loop count is
// not a time: under QEMU on a loaded host one request can take
// seconds while the guest spins through millions of status reads,
// and the drivers gave up with -ETIMEOUT mid-write.
//
// The budget is raw TSC cycles because tsc_frequency() is not
// reliable (CPUID 0x15/0x16 are often zero under QEMU). 2^35
// cycles is about 7 s at 5 GHz and 34 s at 1 GHz, inside the
// 30 s the ATA spec allows a drive to spin up.

#include <stdbool.h>
#include <stdint.h>
#include "io/tsc.h"

#define DISK_POLL_CYCLES (1ULL << 35)

static inline uint64_t disk_poll_deadline(void){
    return read_tsc() + DISK_POLL_CYCLES;
}

static inline bool disk_poll_expired(uint64_t deadline){
    return (int64_t)(read_tsc() - deadline) > 0;
}

#endif
