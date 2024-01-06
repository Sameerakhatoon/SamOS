#ifndef SMP_SELFTEST_H
#define SMP_SELFTEST_H
// Checks that need more than one CPU, run by the BSP right
// after the APs start (marker 189). With panic_test (samos.smp_panic_test)
// CPU 1 then panics, and every CPU must stop.
#include <stdbool.h>
void smp_selftest_run(bool panic_test);
#endif
