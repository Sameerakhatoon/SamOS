#ifndef SAMOS_RISCV64_H
#define SAMOS_RISCV64_H

// riscv64 entry points shared by the files in this directory.

#include <stdbool.h>
#include <stdint.h>

// samos.test on the command line: halting powers QEMU off instead.
extern bool riscv64_test_mode;

void riscv64_main(uint64_t hartid, uint64_t dtb);
__attribute__((noreturn)) void riscv64_halt(void);

#endif
