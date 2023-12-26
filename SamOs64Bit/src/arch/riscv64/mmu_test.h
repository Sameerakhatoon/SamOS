#ifndef SAMOS_RISCV64_MMU_TEST_H
#define SAMOS_RISCV64_MMU_TEST_H

// Kernel-side checks of the MMU, the frame allocator and
// uaccess, for tests64/arch/riscv64-mmu.sh.

#include <stdint.h>

// Every boot: the protection of each image part and of the UART, and
// the W^X and U-bit counts over the whole kernel table.
void mmu_report(uintptr_t uart_va);
// samos.mmu_test: allocate every free frame and check it, then the
// user address space and copy_from_user tests.
void pfa_test(void);
void uaccess_test(void);
// samos.mmu_fault=wtext|wrodata|xdata|ident: one fatal fault.
void mmu_fault_test(const char* which);

#endif
