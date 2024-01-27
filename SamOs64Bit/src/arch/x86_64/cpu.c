// CPU feature checks and control registers.
//
// SamOS divergence: the book never checks CPUID and never
// writes CR0, CR4 or EFER, so WP, OSFXSR and NXE are whatever the
// firmware left. Without CR4.OSFXSR, fxsave does not save the SSE
// registers and any SSE instruction raises #UD.

#include "cpu.h"
#include "kernel.h"
#include "arch/x86_64/cpuid.h"
#include "io/serial.h"
#include <stdint.h>

#define CR0_MP   (1ull << 1)
#define CR0_EM   (1ull << 2)
#define CR0_TS   (1ull << 3)
#define CR0_NE   (1ull << 5)
#define CR0_WP   (1ull << 16)

#define CR4_OSFXSR      (1ull << 9)
#define CR4_OSXMMEXCPT  (1ull << 10)

#define MSR_EFER  0xC0000080
#define EFER_NXE  (1ull << 11)

static uint64_t rdmsr(uint32_t msr){
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t value){
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static void require(int present, const char* name){
    if(!present){
        serial_write("cpu: missing feature ");
        panic(name);
    }
}

static void cpu_setup_registers(uint64_t* cr0_out, uint64_t* cr4_out, uint64_t* efer_out);

void cpu_init(void){
    uint32_t a, b, c, d;

    cpuid(0x80000000, 0, &a, &b, &c, &d);
    require(a >= 0x80000001, "extended CPUID leaves\n");
    cpuid(0x80000001, 0, &a, &b, &c, &d);
    require(d & (1u << 29), "long mode\n");
    require(d & (1u << 20), "NX\n");

    cpuid(1, 0, &a, &b, &c, &d);
    require(d & (1u << 0),  "FPU\n");
    require(d & (1u << 5),  "MSR\n");
    require(d & (1u << 6),  "PAE\n");
    require(d & (1u << 24), "FXSR\n");
    require(d & (1u << 25), "SSE\n");
    require(d & (1u << 26), "SSE2\n");

    uint64_t cr0, cr4, efer;
    cpu_setup_registers(&cr0, &cr4, &efer);

    serial_write("cpu: cr0 ");
    serial_write_hex(cr0);
    serial_write(" cr4 ");
    serial_write_hex(cr4);
    serial_write(" efer ");
    serial_write_hex(efer);
    serial_write("\n");
}

// An AP runs the same CPU set-up as the BSP, without the
// feature checks (the BSP passed them) or the log line.
void cpu_init_ap(void){
    uint64_t cr0, cr4, efer;
    cpu_setup_registers(&cr0, &cr4, &efer);
}

static void cpu_setup_registers(uint64_t* cr0_out, uint64_t* cr4_out, uint64_t* efer_out){
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_MP | CR0_NE | CR0_WP;
    cr0 &= ~(CR0_EM | CR0_TS);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    uint64_t efer = rdmsr(MSR_EFER) | EFER_NXE;
    wrmsr(MSR_EFER, efer);

    __asm__ volatile("fninit");
    *cr0_out  = cr0;
    *cr4_out  = cr4;
    *efer_out = efer;
}
