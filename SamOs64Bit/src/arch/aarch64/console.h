#ifndef ARCH_AARCH64_CONSOLE_H
#define ARCH_AARCH64_CONSOLE_H

// Printf on the early console (pl011.h).
// %s %c %d %u %x, an optional l (64-bit) and a zero-padded width
// ("%016lx"), and %%. Callers that may be preempted wrap a line in
// irq_save/irq_restore (trap.h) so it is never cut in half.
void console_printf(const char* fmt, ...);

#endif
