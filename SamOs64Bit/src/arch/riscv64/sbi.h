#ifndef SAMOS_RISCV64_SBI_H
#define SAMOS_RISCV64_SBI_H

// SBI calls into OpenSBI (M-mode). See the RISC-V SBI spec:
// a7 = extension id, a6 = function id, a0..a5 = arguments;
// returns a0 = error, a1 = value.

#include <stdbool.h>
#include <stdint.h>

#define SBI_EXT_LEGACY_PUTCHAR 0x01
#define SBI_EXT_LEGACY_SHUTDOWN 0x08
#define SBI_EXT_BASE  0x10
#define SBI_EXT_TIME  0x54494D45   // "TIME"
#define SBI_EXT_IPI   0x735049     // "sPI"
#define SBI_EXT_RFNC  0x52464E43   // "RFNC"
#define SBI_EXT_HSM   0x48534D     // "HSM"
#define SBI_EXT_SRST  0x53525354   // "SRST"
#define SBI_EXT_DBCN  0x4442434E   // "DBCN"

#define SBI_ERR_INVALID_PARAM (-3)

#define SBI_HSM_STARTED 0
#define SBI_HSM_STOPPED 1

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_call(long eid, long fid, long arg0, long arg1, long arg2);

// Reads the spec version and probes DBCN. Call before any output.
void sbi_init(void);
bool sbi_probe(long eid);
void sbi_console_putc(char c);
// One line each: spec and implementation, then the extensions found.
void sbi_print_info(void);
// HSM state of a hart, or a negative SBI error (no such hart).
long sbi_hart_status(unsigned long hartid);
// SRST shutdown, legacy shutdown as fallback. Returns only if both fail.
void sbi_shutdown(void);

#endif
