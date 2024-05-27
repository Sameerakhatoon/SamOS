#ifndef ARCH_ARCH_H
#define ARCH_ARCH_H
// The hardware layer. Not in the book.
//
// Everything outside src/arch/ reaches the CPU, the interrupt
// controller, the timer, the MMU and port I/O through this header and
// nothing else. Each architecture implements it in src/arch/<arch>/:
// arch_impl.h holds the types and the inline parts (trap frame,
// per-CPU pointer, atomics, MMIO, mapping flags), the .c/.asm/.S files
// the rest. x86_64, aarch64 and riscv64 implement all
// of it; on the two ports some device and SMP functions are still
// stubs.
//
// Conventions:
//  - IRQ numbers are the controller's own (x86: ISA IRQ 0-15, GICv3:
//    INTID, PLIC: source id). Shared code never does arithmetic on them.
//  - Acknowledging an interrupt (EOI, claim/complete) happens in the
//    arch's dispatcher, never in a handler.
//  - Times are microseconds (TIME_MICROSECONDS) or raw counter ticks
//    (arch_counter, arch_counter_hz).

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__x86_64__)
#include "arch/x86_64/arch_impl.h"
#elif defined(__aarch64__)
#include "arch/aarch64/arch_impl.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "arch/riscv64/arch_impl.h"
#else
#error "SamOS: unsupported architecture"
#endif

// arch_impl.h provides, besides what is declared here:
//   ARCH_NAME, ARCH_ELF_MACHINE, ARCH_HAS_PORT_IO, ARCH_FPU_STATE_SIZE
//   struct trap_frame and its accessors:
//     trap_frame_pc, trap_frame_sp, trap_frame_from_user,
//     trap_frame_syscall_nr, trap_frame_arg(f, n), trap_frame_set_ret,
//     trap_frame_init_user(f, entry, sp), trap_frame_syscall_restart
//   struct arch_cpu (embedded in struct cpu), arch_cpu_self()
//   arch_irq_save/restore/enable/disable, arch_fpu_init/save/restore
//   barrier, smp_mb, smp_rmb, smp_wmb, cpu_relax, atomic_* (atomic.h)
//   mmio_read8/16/32/64, mmio_write8/16/32/64
//   arch_memset, arch_memcpy (memory.c's block fill and copy)
//   PAGING_PAGE_SIZE and the mapping flags PAGING_IS_PRESENT,
//   PAGING_IS_WRITEABLE, PAGING_ACCESS_FROM_ALL, PAGING_NO_EXECUTE,
//   PAGING_CACHE_DISABLED, phys_to_virt, PAGING_PHYS_TO_VIRT,
//   PAGING_IS_DIRECT_MAP

struct cpu;
struct samos_boot_info;

typedef void (*INTERRUPT_CALLBACK_FUNCTION)(struct trap_frame* frame);

// ------------------------------------------------------------------
// Boot, CPU set-up, halting
// ------------------------------------------------------------------

// The loader's handoff block (boot/boot_info.h), set by the arch's
// entry code before kernel_main.
extern struct samos_boot_info* samos_boot_info_ptr;

// Before the heap: CPU features and control registers, the cycle
// counter's frequency.
void arch_early_init(void);
// Trap vectors and this CPU's trap stacks (x86: the TSS and the
// interrupt descriptor table).
void arch_trap_init(void);
// Halt this CPU for good with interrupts off.
void arch_halt(void) __attribute__((noreturn));
// Idle: open interrupts, wait for one, close them again.
void arch_wait_for_interrupt(void);
// Register dump of a trap frame on the console.
void arch_dump_frame(struct trap_frame* frame);
// Running under a hypervisor (x86: CPUID.1:ECX[31]). The storage
// policy writes only to virtual disks.
bool arch_on_hypervisor(void);
// The CPU identifies itself (x86: a CPUID vendor string), for the
// boot self-test.
bool arch_cpu_identified(void);

// e2e hooks (samos.early_fault_test and friends).
#define ARCH_FAULT_TEST_UNDEFINED  1   // an undefined instruction
#define ARCH_FAULT_TEST_BAD_STACK  2   // a fault that cannot push its frame
void arch_fault_test(int kind);

// ------------------------------------------------------------------
// Early console (x86: COM1, aarch64: PL011, riscv64: SBI then 16550)
// ------------------------------------------------------------------

// False when there is no UART; io/serial.c then drops output.
bool arch_console_init(void);
void arch_console_putc(char c);

// ------------------------------------------------------------------
// Traps, syscalls, return to user
// ------------------------------------------------------------------

// The arch's trap code calls these shared entry points:
//   isr80h_handler(frame)    a syscall (isr80h/isr80h.h)
//   task_clock(frame)        the scheduler tick (task/task.h)
//   smp_handle_ipi(ipi, f)   an IPI (cpu/smp.h)
//   smp_nmi_stop()           the panic stop (cpu/smp.h)

// State the kernel needs before running code from a trap from user
// mode, and the user's before going back (x86: data segments; nothing
// on aarch64 and riscv64).
void arch_kernel_context(void);
void arch_user_context(void);
// Load `frame` and drop to user mode. The address space is already
// switched.
void arch_task_return(struct trap_frame* frame) __attribute__((noreturn));

// ------------------------------------------------------------------
// Interrupt controller
// ------------------------------------------------------------------

// Bring up the controller (x86: IOAPIC + LAPIC, or the 8259s with
// samos.legacy_pic or without a MADT).
void arch_irq_init(void);
// Lines can be routed to any CPU and given a GSI (x86: the APIC is up).
bool arch_irq_routable(void);
int  arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION handler);
void arch_irq_unmask(int irq);
void arch_irq_mask(int irq);

// Device completion lines. `slot` 0-15 is the driver's own
// line; the arch routes firmware interrupt `gsi` with `flags` (ACPI
// MPS INTI polarity and trigger) to it, masked or not. Returns the
// line number for the log, or < 0. Their acknowledgement waits for the
// handler, which must have quietened the device.
int  arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                         INTERRUPT_CALLBACK_FUNCTION handler);
// Deliver `gsi` to CPU `c` from now on.
int  arch_irq_set_cpu(uint32_t gsi, struct cpu* c);
// A message-signalled completion line (PCI MSI, MSI-X) on
// `slot`: the device writes *data to *addr to interrupt CPU `c`.
// `handler` NULL keeps the one registered (retargeting). Returns the
// line number, or < 0. Ports without it: a weak -EUNIMP in
// usb/xhci.c, so the xHCI driver falls back to INTx or polling.
// S-PAR - `dev` is the function that writes it (aarch64's GICv3 ITS
// translates by its DeviceID; x86 ignores it).
struct pci_device;
int  arch_irq_msi(struct pci_device* dev, unsigned slot, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler,
                  uint64_t* addr, uint32_t* data);
// A driver's wait in the middle of a syscall: between begin and end
// only completion lines are taken, each time arch_irq_wait_window
// opens interrupts for one instruction.
uint64_t arch_irq_wait_begin(void);
void     arch_irq_wait_window(void);
void     arch_irq_wait_end(uint64_t state);

// ------------------------------------------------------------------
// Timer and time
// ------------------------------------------------------------------

typedef uint64_t TIME_MICROSECONDS;
typedef uint64_t TIME_MILISECONDS;     // sic - upstream spelling
typedef uint64_t TIME_SECONDS;

// Start the scheduler tick at `hz` on this CPU (the BSP). True when
// every CPU gets a tick of its own, which SMP needs (x86: the LAPIC
// timer; false on the 8259 + PIT path).
bool arch_timer_start(uint32_t hz);
// The free-running cycle counter (x86: TSC) and its frequency.
uint64_t          arch_counter(void);
uint64_t          arch_counter_hz(void);
// Microseconds of the counter, and since arch_early_init.
TIME_MICROSECONDS arch_time_us(void);
TIME_MICROSECONDS arch_uptime_us(void);
void              udelay(TIME_MICROSECONDS microseconds);

// ------------------------------------------------------------------
// MMU (x86: 4-level tables; aarch64: TTBR0/1; riscv64: Sv39/Sv48)
// ------------------------------------------------------------------

// One address space. The kernel half is built once
// (paging_kernel_desc_new) and shared by every descriptor
// paging_desc_new makes. `flags` are the PAGING_* bits above; a map
// with flags 0 and phys NULL unmaps.
struct paging_desc;
struct paging_desc* paging_kernel_desc_new(void);
struct paging_desc* paging_desc_new(void);
void   paging_desc_free(struct paging_desc* desc);
void   paging_switch(struct paging_desc* desc);
struct paging_desc* paging_current_descriptor(void);
int    paging_desc_levels(struct paging_desc* desc);

int    paging_map(struct paging_desc* desc, void* virt, void* phys, int flags);
int    paging_map_to(struct paging_desc* desc, void* virt, void* phys, void* phys_end, int flags);
int    paging_map_range(struct paging_desc* desc, void* virt, void* phys, size_t count, int flags);
// Physical address behind `virt` in `desc`, NULL when unmapped; and
// the PAGING_* flags of its page, 0 when unmapped.
void*  paging_get_physical_address(struct paging_desc* desc, void* virt);
int    paging_get_flags(struct paging_desc* desc, void* virt);
// Physical address behind a kernel virtual address, 0 if unmapped.
uint64_t paging_kernel_virt_to_phys(void* virt);
// Device registers or firmware tables at `phys`, in the direct map.
void*  paging_map_mmio(uint64_t phys, uint64_t size, int flags);
// A user access to `virt` through `desc`'s tables: 0 and the physical
// address of the byte, or -EFAULT (the uaccess contract).
int    paging_user_translate(struct paging_desc* desc, uintptr_t virt, bool write, uint64_t* phys_out);
// Flush `va` (0: everything) from this CPU's TLB.
void   arch_tlb_flush_local(uint64_t va);

void*    paging_align_address(void* ptr);
void*    paging_align_to_lower_page(void* addr);
uint64_t paging_align_value_to_upper_page(uint64_t val);
bool     paging_is_aligned(void* addr);

// ------------------------------------------------------------------
// CPUs and IPIs
// ------------------------------------------------------------------

// Start every other CPU the firmware lists. Returns the number online.
int  arch_start_cpus(void);
enum arch_ipi {
    ARCH_IPI_TLB,        // smp_tlb_shootdown
    ARCH_IPI_RESCHED,    // a task was made ready for the target
    ARCH_IPI_CALL,       // smp_call
    ARCH_IPI_INPUT,      // smp_input_register's work; held off between
                         // arch_irq_wait_begin and _end, like the keyboard
};
void arch_send_ipi(struct cpu* target, enum arch_ipi ipi);
// Panic: stop every other CPU (x86: NMI). False when that cannot be
// done (one CPU, no APIC).
bool arch_stop_other_cpus(void);
// 1 when the arch's paging flushes every CPU's TLB itself
// (aarch64: tlbi ...is, which the hardware broadcasts): its paging
// never calls smp_tlb_shootdown, so no ARCH_IPI_TLB is sent. The
// shootdown still works by IPI for a caller that wants one.
#ifndef ARCH_TLB_BROADCAST
#define ARCH_TLB_BROADCAST 0
#endif

// ------------------------------------------------------------------
// I/O space and device discovery
// ------------------------------------------------------------------

// The firmware's description of the machine, from boot_info, before
// the interrupt controller and PCI need it (x86: the ACPI tables from
// the RSDP, acpi_init; aarch64 and riscv64: the device tree,
// firmware/fdt.c). PCI then enumerates through ECAM where the
// firmware gives one (ACPI MCFG, or a DT pci node).
void arch_firmware_init(void);

// Port I/O (ARCH_HAS_PORT_IO). Elsewhere these read all ones and drop
// writes; drivers reach both kinds of register through io/ioregion.h.
uint8_t  arch_pio_read8(uint16_t port);
uint16_t arch_pio_read16(uint16_t port);
uint32_t arch_pio_read32(uint16_t port);
void     arch_pio_write8(uint16_t port, uint8_t v);
void     arch_pio_write16(uint16_t port, uint16_t v);
void     arch_pio_write32(uint16_t port, uint32_t v);

// PCI configuration space without ECAM (x86: ports 0xCF8/0xCFC). All
// ones when the arch has no such mechanism. io/pci.c uses ECAM first.
uint32_t arch_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
void     arch_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v);

// Platform power-off and reset when the firmware tables' way did not
// work (acpi/acpi.c). They return if nothing worked.
void arch_shutdown_fallback(void);
void arch_reboot_fallback(void);

#endif
