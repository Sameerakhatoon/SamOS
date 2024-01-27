#ifndef CPU_H
#define CPU_H

// CPU feature checks and control registers.
void cpu_init(void);
// The same CR0/CR4/EFER set-up on an AP.
void cpu_init_ap(void);

#endif
