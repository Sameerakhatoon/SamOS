#ifndef ARCH_AARCH64_SYSREG_H
#define ARCH_AARCH64_SYSREG_H

#include <stdint.h>

// System register access. The GIC CPU interface registers go by
// their generic S<op0>_<op1>_C<n>_C<m>_<op2> names, which every
// assembler takes whatever -march says.
#define sysreg_read(name) ({ uint64_t v_; __asm__ volatile("mrs %0, " name : "=r"(v_)); v_; })
#define sysreg_write(name, v) __asm__ volatile("msr " name ", %0" :: "r"((uint64_t)(v)) : "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

#define ICC_PMR_EL1     "S3_0_C4_C6_0"
#define ICC_IAR1_EL1    "S3_0_C12_C12_0"
#define ICC_EOIR1_EL1   "S3_0_C12_C12_1"
#define ICC_BPR1_EL1    "S3_0_C12_C12_3"
#define ICC_CTLR_EL1    "S3_0_C12_C12_4"
#define ICC_SRE_EL1     "S3_0_C12_C12_5"
#define ICC_IGRPEN1_EL1 "S3_0_C12_C12_7"

#endif
