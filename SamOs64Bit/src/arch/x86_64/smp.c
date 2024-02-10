// Application processors, IPIs, per-CPU GS. Not in
// the book, which runs on the BSP alone. The x86 half of the
// old cpu/smp.c; the portable half stays there.
//
// Bring-up (arch_start_cpus, BSP, after every subsystem is ready and
// before the first user process): for each enabled MADT processor
// other than the BSP, in MADT order, one at a time:
//   1. its struct cpu: kernel stack, IST stacks, TSS (cpu_tss_init);
//   2. the trampoline (ap_trampoline.asm) at a free page below 1 MiB,
//      with a temporary PML4 below 4 GiB that identity-maps that page
//      and shares the kernel half, and the AP's stack and cpu pointer;
//   3. INIT, 10 ms, STARTUP, 200 us, STARTUP (the Intel sequence);
//   4. wait up to 5 s for the AP to say it is online; else give up
//      on it so a late answer cannot share the next one's slot
//      (smp_give_up).
// The AP loads its own GDT copy with its TSS, the kernel CR3, GS
// base, the shared IDT, CR0/CR4, its LAPIC and LAPIC timer, and
// enters the scheduler (smp_ap_main); with nothing to run it idles.

#include "arch/x86_64/smp.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "cpu/smp.h"
#include "io/serial.h"
#include "acpi/acpi.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "task/task.h"
#include "string/string.h"
#include "kernel.h"
#include "bootmarker.h"
#include "boot/cmdline.h"

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

// The trampoline and its three page tables: four pages below 1 MiB,
// above the boot markers at 0x6000.
#define SMP_LOW_FIRST       0x8000
#define SMP_LOW_END         0x100000
#define SMP_LOW_PAGES       4

#define SMP_TR_CR3          8
#define SMP_TR_STACK        16
#define SMP_TR_ENTRY        24
#define SMP_TR_CPU          32

uint32_t smp_tick_hz;

extern char ap_trampoline_start[];
extern char ap_trampoline_end[];
extern struct gdt_entry gdt[];
extern void cpu_load_gdt(void* gdtr);

struct gdtr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t rdmsr(uint32_t msr){
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t v){
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

// ------------------------------------------------------------------
// Per-CPU TSS and stacks.
// ------------------------------------------------------------------

void cpu_tss_init(struct cpu* c){
    struct arch_cpu* a = &c->arch;
    // A 1 MiB ring-0 stack with an unmapped guard page at the
    // bottom, so an overflow faults (and reaches #DF on IST1).
    a->kernel_stack = kzalloc(CPU_KERNEL_STACK_SIZE);
    if(!a->kernel_stack){
        panic("cpu_tss_init: no memory for the kernel stack\n");
    }
    paging_map(kernel_desc(), a->kernel_stack, NULL, 0);

    memset(&a->tss, 0, sizeof(a->tss));
    a->tss.rsp0        = (uint64_t)(a->kernel_stack + CPU_KERNEL_STACK_SIZE);
    a->tss.iopb_offset = sizeof(a->tss);
    uint64_t ist[3];
    for(int i = 0; i < 3; i++){
        uint8_t* stack = kzalloc(KERNEL_IST_STACK_SIZE);
        if(!stack){
            panic("cpu_tss_init: no memory for IST stacks\n");
        }
        ist[i] = (uint64_t)(stack + KERNEL_IST_STACK_SIZE);
    }
    a->tss.ist1 = ist[0];   // IDT_IST_DOUBLE_FAULT
    a->tss.ist2 = ist[1];   // IDT_IST_NMI
    a->tss.ist3 = ist[2];   // IDT_IST_MACHINE_CHECK

    // The BSP keeps kernel.asm's GDT; an AP gets a copy, because ltr
    // marks the TSS descriptor busy and each CPU needs its own.
    struct gdt_entry* table = c->index ? (struct gdt_entry*)a->gdt : gdt;
    if(c->index){
        memcpy(a->gdt, gdt, KERNEL_LONG_MODE_TSS_GDT_INDEX * sizeof(uint64_t));
    }
    gdt_set_tss((struct tss_desc_64*)&table[KERNEL_LONG_MODE_TSS_GDT_INDEX],
                &a->tss, sizeof(a->tss) - 1, TSS_DESCRIPTOR_TYPE, 0x00);
}

void smp_bsp_init(void){
    struct cpu* c = &cpus[0];
    c->self   = c;
    c->index  = 0;
    c->online = 1;
    cpu_tss_init(c);
    tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);
}

// ------------------------------------------------------------------
// AP start-up.
// ------------------------------------------------------------------

// Lowest run of SMP_LOW_PAGES free pages in usable RAM below 1 MiB.
// The PFA does not manage memory below 1 MiB, so nothing else uses it.
static uint64_t smp_low_pages(void){
    size_t total = e820_total_entries();
    uint64_t best = 0;
    for(size_t i = 0; i < total; i++){
        struct e820_entry* r = e820_entry(i);
        if(r->type != 1){
            continue;
        }
        uint64_t s = (r->base_addr + 0xFFF) & ~0xFFFull;
        uint64_t e = (r->base_addr + r->length) & ~0xFFFull;
        if(s < SMP_LOW_FIRST){
            s = SMP_LOW_FIRST;
        }
        if(e > SMP_LOW_END){
            e = SMP_LOW_END;
        }
        if(e > s && e - s >= SMP_LOW_PAGES * 0x1000 && (!best || s < best)){
            best = s;
        }
    }
    return best;
}

// The start-up handshake in c->arch.start. The AP claims its
// slot (WAIT -> CLAIMED) before it touches anything outside it; the
// BSP gives up on it (WAIT -> GAVE_UP) after its start-up window.
// Exactly one of the two wins. An AP that arrives after the BSP gave
// up parks with interrupts off and writes nothing but PARKED in its
// own slot; the BSP then sends it INIT, which holds it in
// wait-for-SIPI for good, before the trampoline and the slot are
// reused for the next processor.
#define AP_START_WAIT       0
#define AP_START_CLAIMED    1
#define AP_START_GAVE_UP    2
#define AP_START_PARKED     3

#define AP_START_WINDOW_US  5000000   // generous: TCG on a loaded host starts a vCPU slowly

static void smp_ap_main(struct cpu* c){
    if(!atomic_cas(&c->arch.start, AP_START_WAIT, AP_START_CLAIMED)){
        atomic_store(&c->arch.start, AP_START_PARKED);
        while(1){
            __asm__ volatile("cli; hlt");
        }
    }
    struct gdtr64 gdtr = { sizeof(c->arch.gdt) - 1, (uint64_t)c->arch.gdt };
    cpu_load_gdt(&gdtr);
    wrmsr(MSR_GS_BASE, (uint64_t)c);
    wrmsr(MSR_KERNEL_GS_BASE, 0);
    paging_switch(kernel_desc());
    tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);
    idt_load_cpu();
    cpu_init_ap();
    lapic_init_cpu();
    lapic_timer_start(smp_tick_hz);

    c->online = 1;
    atomic_inc(&cpu_online_count);
    serial_write("smp: cpu ");
    serial_write(itoa((int)c->index));
    serial_write(" apic ");
    serial_write(itoa((int)c->hw_id));
    serial_write(" online\n");
    task_next();
}

static bool smp_wait_online(struct cpu* c, uint64_t us){
    uint64_t end = arch_time_us() + us;
    while(arch_time_us() < end){
        if(c->online){
            return true;
        }
        cpu_relax();
    }
    return c->online;
}

static bool smp_wait_parked(struct cpu* c, uint64_t us){
    uint64_t end = arch_time_us() + us;
    while(c->arch.start != AP_START_PARKED && arch_time_us() < end){
        cpu_relax();
    }
    return c->arch.start == AP_START_PARKED;
}

// The AP did not come online in its window (`test`: the knob gave up
// at once).
// True when the BSP gave up and the slot is free again; false when the
// AP claimed it meanwhile and is coming online.
static bool smp_give_up(struct cpu* c, bool test){
    if(!atomic_cas(&c->arch.start, AP_START_WAIT, AP_START_GAVE_UP) &&
       c->arch.start == AP_START_CLAIMED){
        if(smp_wait_online(c, AP_START_WINDOW_US)){
            return false;
        }
        panic("smp: an AP claimed its slot but never came online\n");
    }
    serial_write("smp: apic ");
    serial_write(itoa((int)c->hw_id));
    serial_write(test ? " did not start (samos.smp_late_ap)\n" : " did not start\n");
    // The test gave up before the AP could answer: wait for it to park.
    if(test){
        smp_wait_parked(c, AP_START_WINDOW_US);
    }
    // INIT holds it in wait-for-SIPI (no SIPI goes to it again), so it
    // cannot run the trampoline once the next processor's stack and
    // slot are written there.
    lapic_send_init(c->hw_id);
    udelay(10000);
    if(c->arch.start == AP_START_PARKED){
        serial_write("smp: apic ");
        serial_write(itoa((int)c->hw_id));
        serial_write(" answered late and parked\n");
    }
    return true;
}

int arch_start_cpus(void){
    const struct acpi_info* a = acpi_info();
    struct cpu* bsp = &cpus[0];
    bsp->hw_id = lapic_id();
    if(!apic_active() || !a->have_madt){
        return 1;
    }

    uint64_t low = smp_low_pages();
    if(!low){
        serial_write("smp: no free page below 1 MiB for the trampoline\n");
        return 1;
    }
    uint8_t*  tramp = PAGING_PHYS_TO_VIRT(low);
    uint64_t* pml4  = PAGING_PHYS_TO_VIRT(low + 0x1000);
    uint64_t* pdpt  = PAGING_PHYS_TO_VIRT(low + 0x2000);
    uint64_t* pd    = PAGING_PHYS_TO_VIRT(low + 0x3000);
    memset(tramp, 0, SMP_LOW_PAGES * 0x1000);
    memcpy(tramp, ap_trampoline_start, (int)(ap_trampoline_end - ap_trampoline_start));
    // Identity map of [0, 2 MiB) for the trampoline, kernel half
    // copied from the kernel's PML4 (its PDPTs are shared).
    pml4[0] = (low + 0x2000) | 0x3;
    pdpt[0] = (low + 0x3000) | 0x3;
    pd[0]   = 0x83;
    uint64_t* kpml4 = (uint64_t*)kernel_desc()->pml->entries;
    for(int i = PAGING_KERNEL_PML4_FIRST; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++){
        pml4[i] = kpml4[i];
    }
    *(uint64_t*)(tramp + SMP_TR_CR3)   = low + 0x1000;
    *(uint64_t*)(tramp + SMP_TR_ENTRY) = (uint64_t)smp_ap_main;

    // Test knob: give up on the first AP before it can answer.
    bool late_test = boot_cmdline_has("samos.smp_late_ap");
    int next = 1;
    for(int i = 0; i < a->cpu_count && next < SAMOS_MAX_CPUS; i++){
        if(!a->cpus[i].enabled || a->cpus[i].apic_id == bsp->hw_id){
            continue;
        }
        // A slot the BSP gave up on is reused: smp_give_up has sent
        // its AP INIT, and the stacks a parked AP sits on are left
        // allocated (not reused).
        struct cpu* c = &cpus[next];
        memset(c, 0, sizeof(*c));
        c->self  = c;
        c->index = next;
        c->hw_id = a->cpus[i].apic_id;
        cpu_tss_init(c);
        bool give_up_now = late_test;
        late_test = false;
        c->arch.start = give_up_now ? AP_START_GAVE_UP : AP_START_WAIT;
        *(uint64_t*)(tramp + SMP_TR_STACK) = c->arch.tss.rsp0;
        *(uint64_t*)(tramp + SMP_TR_CPU)   = (uint64_t)c;
        smp_mb();

        lapic_send_init(c->hw_id);
        udelay(10000);
        lapic_send_startup(c->hw_id, (uint32_t)(low >> 12));
        udelay(200);
        if(!c->online){
            lapic_send_startup(c->hw_id, (uint32_t)(low >> 12));
        }
        if(!smp_wait_online(c, give_up_now ? 0 : AP_START_WINDOW_US) && smp_give_up(c, give_up_now)){
            continue;
        }
        next++;
    }
    print("smp: ");
    print(itoa((int)cpu_online_count));
    print(" cpus online\n");
    boot_marker_set(BM_STAGE_SMP_CPUS, cpu_online_count);
    return (int)cpu_online_count;
}

// ------------------------------------------------------------------
// IPIs.
// ------------------------------------------------------------------

static const uint8_t smp_ipi_vector[] = {
    [ARCH_IPI_TLB]     = SMP_VECTOR_TLB,
    [ARCH_IPI_RESCHED] = SMP_VECTOR_RESCHED,
    [ARCH_IPI_CALL]    = SMP_VECTOR_CALL,
    [ARCH_IPI_INPUT]   = SMP_VECTOR_INPUT,
};

void arch_send_ipi(struct cpu* target, enum arch_ipi ipi){
    lapic_send_ipi(target->hw_id, smp_ipi_vector[ipi]);
}

static void smp_tlb_ipi(struct trap_frame* frame){
    smp_handle_ipi(ARCH_IPI_TLB, frame);
}

static void smp_resched_ipi(struct trap_frame* frame){
    smp_handle_ipi(ARCH_IPI_RESCHED, frame);
}

static void smp_call_ipi(struct trap_frame* frame){
    smp_handle_ipi(ARCH_IPI_CALL, frame);
}

static void smp_input_ipi(struct trap_frame* frame){
    smp_handle_ipi(ARCH_IPI_INPUT, frame);
}

void smp_register_ipis(void){
    idt_register_interrupt_callback(SMP_VECTOR_TLB, smp_tlb_ipi);
    idt_register_interrupt_callback(SMP_VECTOR_RESCHED, smp_resched_ipi);
    idt_register_interrupt_callback(SMP_VECTOR_CALL, smp_call_ipi);
    idt_register_interrupt_callback(SMP_VECTOR_INPUT, smp_input_ipi);
}

// Panic: an NMI to every other CPU; their NMI handler calls
// smp_nmi_stop.
bool arch_stop_other_cpus(void){
    if(!apic_active()){
        return false;
    }
    lapic_send_nmi_all_others();
    return true;
}

// An exception or NMI taken in ring 0 between a swapgs and its iretq
// arrives with the user's GS base: swap to the kernel's. Only
// that way, and nothing swaps back: the interrupted stub would resume
// with the bases the wrong way round. Harmless today because no such
// exception returns: idt_exception treats every ring-0 exception as
// fatal (panic), and an NMI during a panic halts in smp_nmi_stop. A
// handler that ever returns to ring 0 code here must swap back.
void smp_fix_gs(void){
    uint64_t gs = rdmsr(MSR_GS_BASE);
    if(gs < (uint64_t)&cpus[0] || gs >= (uint64_t)&cpus[SAMOS_MAX_CPUS]){
        __asm__ volatile("swapgs" ::: "memory");
    }
}
