#ifndef KERNEL_SELFTEST_H
#define KERNEL_SELFTEST_H
//
// Kernel-side self-tests for runtime feature coverage.
//
// kernel_selftest() runs once at the end of kernel_main, after
// every subsystem init has completed. Each check exercises one
// feature (kmalloc round trip, disk sector read, file open,
// process_malloc, etc.) and records a 1 (pass) or 0 (fail) into
// the boot-marker region. The host-side e2e test scripts then
// dump the markers and assert each feature's bit.
//
// One QEMU boot -> many e2e checks. Much faster than the
// "one boot per test" model the init-checkpoint tests used.
//
// Layout: each feature gets its own BM_STAGE_* slot (numbered
// from BM_FEATURE_BASE upward) and stores 1 in the low byte
// when the check passes. The high bytes are the standard magic
// prefix so a "not run" stage is distinguishable from "ran but
// returned 0".

#include "bootmarker.h"

// Returns 0 on success. Each individual feature is recorded
// independently of overall return so the host test can see
// partial-pass results.
int kernel_selftest(void);

#endif
