#ifndef MEMTEST_H
#define MEMTEST_H

// Memory-manager self-tests. Run once from kernel_main after
// paging and the multiheap are up; each writes a BM_FEATURE_MEM_*
// boot marker (1 = pass, otherwise the failing line number).
void memtest_run(void);

#endif
