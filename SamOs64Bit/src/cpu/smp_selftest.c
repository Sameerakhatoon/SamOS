// SMP self-test (marker BM_FEATURE_SMP_SELFTEST, e2e/190).
// Not in the book. Runs on the BSP with every AP idle:
//   1. call: each AP runs a function in its IPI handler and reports
//      its own cpu index, so the IPI reached the right CPU and GS
//      there points at that CPU's struct cpu;
//   2. TLB shootdown: a kernel-half page is mapped to frame A, each AP
//      reads it (its TLB now holds A), the page is remapped to frame
//      B, and each AP must read B. Without the shootdown IPI an idle
//      AP keeps the old translation: it never switches tables in its idle
//      loop;
//   3. locks and atomics: every CPU at once adds to one counter under
//      a spinlock and to another with atomic_inc; neither may lose a
//      count.
// Value 1 is a pass, any other the failing line.

#include "smp_selftest.h"
#include "smp.h"
#include "percpu.h"
#include "arch/arch.h"
#include "lib/spinlock/spinlock.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "arch/arch.h"
#include "bootmarker.h"
#include "string/string.h"
#include "kernel.h"

#define SMP_TEST_VA      0xFFFFE00000000000ull   // an otherwise unused kernel-half slot
#define SMP_TEST_ROUNDS  20000
#define SMP_PATTERN_A    0xAAAA5555AAAA5555ull
#define SMP_PATTERN_B    0xBBBB6666BBBB6666ull

static volatile uint64_t seen[SAMOS_MAX_CPUS];

static void report_index(void* arg){
    seen[this_cpu()->index] = this_cpu()->index + 100;
}

static void read_test_page(void* arg){
    seen[this_cpu()->index] = *(volatile uint64_t*)SMP_TEST_VA;
}

static struct spinlock test_lock = SPINLOCK_INIT("smp_test", LOCK_LEVEL_SCHED, 0);
static volatile uint64_t locked_count;
static volatile uint32_t atomic_count;
static volatile uint32_t go;
static volatile uint32_t finished;

static void count_together(void* arg){
    while(!atomic_load(&go)){
        cpu_relax();
    }
    for(int i = 0; i < SMP_TEST_ROUNDS; i++){
        spin_lock(&test_lock);
        locked_count = locked_count + 1;
        spin_unlock(&test_lock);
        atomic_inc(&atomic_count);
    }
    atomic_inc(&finished);
}

static void panic_on_ap(void* arg){
    panic("smp panic test\n");
}

static int smp_selftest(void){
    int n = (int)cpu_online_count;

    for(int i = 1; i < n; i++){
        smp_call(&cpus[i], report_index, 0);
        if(seen[i] != (uint64_t)i + 100){
            return __LINE__;
        }
    }

    uint64_t* a = kpage_zalloc(PAGING_PAGE_SIZE);
    uint64_t* b = kpage_zalloc(PAGING_PAGE_SIZE);
    if(!a || !b){
        return __LINE__;
    }
    a[0] = SMP_PATTERN_A;
    b[0] = SMP_PATTERN_B;
    int flags = PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE;
    if(paging_map(kernel_desc(), (void*)SMP_TEST_VA, (void*)paging_kernel_virt_to_phys(a), flags) < 0){
        return __LINE__;
    }
    for(int i = 1; i < n; i++){
        smp_call(&cpus[i], read_test_page, 0);
        if(seen[i] != SMP_PATTERN_A){
            return __LINE__;
        }
    }
    uint64_t tlb_before = cpus[1].ipi_tlb;
    if(paging_map(kernel_desc(), (void*)SMP_TEST_VA, (void*)paging_kernel_virt_to_phys(b), flags) < 0){
        return __LINE__;
    }
    for(int i = 1; i < n; i++){
        smp_call(&cpus[i], read_test_page, 0);
        if(seen[i] != SMP_PATTERN_B){
            return __LINE__;
        }
    }
    if(cpus[1].ipi_tlb == tlb_before){
        return __LINE__;
    }
    paging_map(kernel_desc(), (void*)SMP_TEST_VA, NULL, 0);
    kfree(a);
    kfree(b);

    locked_count = 0;
    atomic_count = 0;
    finished = 0;
    go = 0;
    for(int i = 1; i < n; i++){
        smp_call_async(&cpus[i], count_together, 0);
    }
    atomic_store(&go, 1);
    count_together(0);
    while(atomic_load(&finished) < (uint32_t)n){
        cpu_relax();
    }
    for(int i = 1; i < n; i++){
        smp_call_wait(&cpus[i]);
    }
    if(locked_count != (uint64_t)n * SMP_TEST_ROUNDS || atomic_count != (uint32_t)n * SMP_TEST_ROUNDS){
        return __LINE__;
    }
    return 1;
}

void smp_selftest_run(bool panic_test){
    if(cpu_online_count < 2){
        return;
    }
    int res = smp_selftest();
    boot_marker_set(BM_FEATURE_SMP_SELFTEST, (uint32_t)res);
    print(res == 1 ? "smp selftest: call, tlb shootdown, lock and atomic counts ok\n"
                   : "smp selftest: FAIL at line ");
    if(res != 1){
        print(itoa(res));
        print("\n");
    }
    if(panic_test){
        print("smp: panic test on cpu 1\n");
        smp_call_async(&cpus[1], panic_on_ap, 0);
        while(1){
            cpu_relax();
        }
    }
}
