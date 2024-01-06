// Application processors, IPIs, TLB shootdown, panic stop.
// Not in the book, which runs on the BSP alone.
//
// Bring-up (smp_start_aps, BSP, after every subsystem is ready and
// before the first user process): for each enabled MADT processor
// other than the BSP, in MADT order, one at a time:
//   1. its struct cpu: kernel stack, IST stacks, TSS (cpu_tss_init);
//   2. the trampoline (ap_trampoline.asm) at a free page below 1 MiB,
//      with a temporary PML4 below 4 GiB that identity-maps that page
//      and shares the kernel half, and the AP's stack and cpu pointer;
//   3. INIT, 10 ms, STARTUP, 200 us, STARTUP (the Intel sequence);
//   4. wait for the AP to say it is online.
// The AP loads its own GDT copy with its TSS, the kernel CR3, GS
// base, the shared IDT, CR0/CR4, its LAPIC and LAPIC timer, and
// enters the scheduler (smp_ap_main); with nothing to run it idles.

#include "smp.h"
#include "percpu.h"
#include "cpu.h"
#include "arch/x86_64/atomic.h"
#include "lib/spinlock/spinlock.h"
#include "io/apic.h"
#include "io/pit.h"
#include "io/tsc.h"
#include "io/serial.h"
#include "acpi/acpi.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "gdt/gdt.h"
#include "idt/idt.h"
#include "task/task.h"
#include "string/string.h"
#include "config.h"
#include "kernel.h"
#include "bootmarker.h"

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

struct cpu cpus[SAMOS_MAX_CPUS];
volatile uint32_t cpu_online_count = 1;

extern char ap_trampoline_start[];
extern char ap_trampoline_end[];
extern struct gdt_entry gdt[];
extern void cpu_load_gdt(void* gdtr);

struct gdtr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// One shootdown or cross-CPU call at a time.
static struct spinlock smp_lock  = SPINLOCK_INIT("smp", LOCK_LEVEL_SMP, 0);
static struct spinlock call_lock = SPINLOCK_INIT("smp_call", LOCK_LEVEL_SMP, 0);
static volatile uint64_t tlb_va;
static volatile uint32_t tlb_acks;

static volatile uint32_t panicking;
static volatile uint32_t panic_cpu;
static volatile uint32_t stopped;

static uint64_t rdmsr(uint32_t msr){
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t v){
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static void smp_halt_forever(void){
    while(1){
        __asm__ volatile("cli; hlt");
    }
}

// ------------------------------------------------------------------
// Per-CPU TSS and stacks.
// ------------------------------------------------------------------

void cpu_tss_init(struct cpu* c){
    // A 1 MiB ring-0 stack with an unmapped guard page at the
    // bottom, so an overflow faults (and reaches #DF on IST1).
    c->kernel_stack = kzalloc(CPU_KERNEL_STACK_SIZE);
    if(!c->kernel_stack){
        panic("cpu_tss_init: no memory for the kernel stack\n");
    }
    paging_map(kernel_desc(), c->kernel_stack, NULL, 0);

    memset(&c->tss, 0, sizeof(c->tss));
    c->tss.rsp0        = (uint64_t)(c->kernel_stack + CPU_KERNEL_STACK_SIZE);
    c->tss.iopb_offset = sizeof(c->tss);
    uint64_t ist[3];
    for(int i = 0; i < 3; i++){
        uint8_t* stack = kzalloc(KERNEL_IST_STACK_SIZE);
        if(!stack){
            panic("cpu_tss_init: no memory for IST stacks\n");
        }
        ist[i] = (uint64_t)(stack + KERNEL_IST_STACK_SIZE);
    }
    c->tss.ist1 = ist[0];   // IDT_IST_DOUBLE_FAULT
    c->tss.ist2 = ist[1];   // IDT_IST_NMI
    c->tss.ist3 = ist[2];   // IDT_IST_MACHINE_CHECK

    // The BSP keeps kernel.asm's GDT; an AP gets a copy, because ltr
    // marks the TSS descriptor busy and each CPU needs its own.
    struct gdt_entry* table = c->index ? (struct gdt_entry*)c->gdt : gdt;
    if(c->index){
        memcpy(c->gdt, gdt, KERNEL_LONG_MODE_TSS_GDT_INDEX * sizeof(uint64_t));
    }
    gdt_set_tss((struct tss_desc_64*)&table[KERNEL_LONG_MODE_TSS_GDT_INDEX],
                &c->tss, sizeof(c->tss) - 1, TSS_DESCRIPTOR_TYPE, 0x00);
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

static void smp_ap_main(struct cpu* c){
    struct gdtr64 gdtr = { sizeof(c->gdt) - 1, (uint64_t)c->gdt };
    cpu_load_gdt(&gdtr);
    wrmsr(MSR_GS_BASE, (uint64_t)c);
    wrmsr(MSR_KERNEL_GS_BASE, 0);
    paging_switch(kernel_desc());
    tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);
    idt_load_cpu();
    cpu_init_ap();
    lapic_init_cpu();
    lapic_timer_start(PIT_TICK_HZ);

    c->online = 1;
    atomic_inc(&cpu_online_count);
    serial_write("smp: cpu ");
    serial_write(itoa((int)c->index));
    serial_write(" apic ");
    serial_write(itoa((int)c->apic_id));
    serial_write(" online\n");
    task_next();
}

static bool smp_wait_online(struct cpu* c, uint64_t us){
    uint64_t end = tsc_microseconds() + us;
    while(tsc_microseconds() < end){
        if(c->online){
            return true;
        }
        cpu_relax();
    }
    return c->online;
}

int smp_start_aps(void){
    const struct acpi_info* a = acpi_info();
    struct cpu* bsp = &cpus[0];
    bsp->apic_id = lapic_id();
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

    int next = 1;
    for(int i = 0; i < a->cpu_count && next < SAMOS_MAX_CPUS; i++){
        if(!a->cpus[i].enabled || a->cpus[i].apic_id == bsp->apic_id){
            continue;
        }
        struct cpu* c = &cpus[next];
        memset(c, 0, sizeof(*c));
        c->self    = c;
        c->index   = next;
        c->apic_id = a->cpus[i].apic_id;
        cpu_tss_init(c);
        *(uint64_t*)(tramp + SMP_TR_STACK) = c->tss.rsp0;
        *(uint64_t*)(tramp + SMP_TR_CPU)   = (uint64_t)c;
        smp_mb();

        lapic_send_init(c->apic_id);
        udelay(10000);
        lapic_send_startup(c->apic_id, (uint32_t)(low >> 12));
        udelay(200);
        if(!c->online){
            lapic_send_startup(c->apic_id, (uint32_t)(low >> 12));
        }
        // Generous: TCG on a loaded host starts a vCPU slowly.
        if(!smp_wait_online(c, 5000000)){
            serial_write("smp: apic ");
            serial_write(itoa((int)c->apic_id));
            serial_write(" did not start\n");
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

void smp_send_resched(struct cpu* target){
    if(target != this_cpu() && target->online){
        lapic_send_ipi(target->apic_id, SMP_VECTOR_RESCHED);
    }
}

static void smp_tlb_flush_local(uint64_t va){
    if(va){
        __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
    } else {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) : : "memory");
    }
}

void smp_tlb_service(struct cpu* c){
    if(atomic_xchg(&c->tlb_pending, 0)){
        smp_tlb_flush_local(tlb_va);
        c->ipi_tlb++;
        atomic_dec(&tlb_acks);
    }
}

static void smp_tlb_ipi(struct trap_frame* frame){
    smp_tlb_service(this_cpu());
}

// Busy-wait for *v to become 0, answering requests meanwhile. A stuck
// target means a CPU spinning with interrupts off somewhere that does
// not poll: report it.
static void smp_wait_zero(volatile uint32_t* v, const char* what){
    struct cpu* me = this_cpu();
    uint64_t start = tsc_microseconds();
    while(atomic_load(v)){
        cpu_relax();
        smp_poll(me);
        if(SAMOS_LOCK_DEBUG && tsc_microseconds() - start > SAMOS_LOCK_TIMEOUT_US){
            serial_write_unlocked("smp: no answer to ");
            serial_write_unlocked(what);
            panic("smp: a CPU did not answer an IPI\n");
        }
    }
}

void smp_tlb_shootdown(struct paging_desc* desc, uint64_t va){
    if(!smp_active()){
        return;
    }
    bool kernel_half = va >= SAMOS_DIRECT_MAP_BASE;
    struct cpu* me = this_cpu();
    spin_lock(&smp_lock);
    tlb_va = va & ~0xFFFull;
    uint32_t n = 0;
    bool target[SAMOS_MAX_CPUS] = {0};
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        struct cpu* c = &cpus[i];
        if(c != me && c->online && (kernel_half || c->user_desc == desc)){
            target[i] = true;
            n++;
        }
    }
    tlb_acks = n;
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        if(target[i]){
            atomic_store(&cpus[i].tlb_pending, 1);
            lapic_send_ipi(cpus[i].apic_id, SMP_VECTOR_TLB);
        }
    }
    smp_wait_zero(&tlb_acks, "a TLB shootdown");
    spin_unlock(&smp_lock);
}

static void smp_call_ipi(struct trap_frame* frame){
    struct cpu* c = this_cpu();
    void (*fn)(void*) = c->call_fn;
    if(!fn){
        return;
    }
    c->call_fn = 0;
    fn(c->call_arg);
    atomic_store(&c->call_done, 1);
}

void smp_call_async(struct cpu* target, void (*fn)(void*), void* arg){
    target->call_done = 0;
    target->call_arg  = arg;
    smp_wmb();
    target->call_fn   = fn;
    lapic_send_ipi(target->apic_id, SMP_VECTOR_CALL);
}

void smp_call_wait(struct cpu* target){
    struct cpu* me = this_cpu();
    while(!atomic_load(&target->call_done)){
        cpu_relax();
        smp_poll(me);
    }
}

void smp_call(struct cpu* target, void (*fn)(void*), void* arg){
    if(target == this_cpu()){
        fn(arg);
        return;
    }
    spin_lock(&call_lock);
    smp_call_async(target, fn, arg);
    smp_call_wait(target);
    spin_unlock(&call_lock);
}

static void smp_resched_ipi(struct trap_frame* frame){
    this_cpu()->ipi_resched++;
    task_resched(frame);
}

void smp_register_ipis(void){
    idt_register_interrupt_callback(SMP_VECTOR_TLB, smp_tlb_ipi);
    idt_register_interrupt_callback(SMP_VECTOR_RESCHED, smp_resched_ipi);
    idt_register_interrupt_callback(SMP_VECTOR_CALL, smp_call_ipi);
}

// ------------------------------------------------------------------
// Panic: every other CPU stops in its NMI handler.
// ------------------------------------------------------------------

bool smp_panicking(void){
    return panicking != 0;
}

void smp_panic_begin(void){
    uint32_t me = this_cpu()->index;
    if(atomic_xchg(&panicking, 1)){
        if(panic_cpu != me){
            smp_halt_forever();   // another CPU is already reporting
        }
        return;
    }
    panic_cpu = me;
    if(!smp_active() || !apic_active()){
        return;
    }
    lapic_send_nmi_all_others();
    uint64_t end = read_tsc() + tsc_frequency() / 10;
    while(stopped < cpu_online_count - 1 && read_tsc() < end){
        cpu_relax();
    }
    serial_write_unlocked("panic: stopped ");
    serial_write_unlocked(itoa((int)stopped));
    serial_write_unlocked(" other cpus\n");
}

bool smp_nmi_stop(void){
    if(!panicking || panic_cpu == this_cpu()->index){
        return false;
    }
    atomic_inc(&stopped);
    smp_halt_forever();
    return true;
}

void smp_fix_gs(void){
    uint64_t gs = rdmsr(MSR_GS_BASE);
    if(gs < (uint64_t)&cpus[0] || gs >= (uint64_t)&cpus[SAMOS_MAX_CPUS]){
        __asm__ volatile("swapgs" ::: "memory");
    }
}
