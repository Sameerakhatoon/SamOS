#include "serial.h"
#include "lib/spinlock/spinlock.h"
#include "cpu/percpu.h"
#include "arch/arch.h"
#include <stdint.h>

// The serial console. The UART itself is the arch's
// early console (x86: COM1, arch_console_putc); this file keeps the
// locking and per-CPU line buffering.
static int serial_ready = 0;

void serial_init(void)
{
    serial_ready = arch_console_init();
}

static void serial_out(char c)
{
    if (c == '\n')
    {
        arch_console_putc('\r');
    }
    arch_console_putc(c);
}

// One CPU at a time on the UART. Once the APs run,
// each CPU collects its output up to a newline (or a full buffer)
// and sends the line in one hold of the lock, so the lines tests
// grep for are never cut by another CPU's output. serial_flush
// sends a partial line (a shell prompt); the scheduler calls it
// before a CPU returns to ring 3 or halts.
static struct spinlock serial_lock = SPINLOCK_INIT("serial", LOCK_LEVEL_SERIAL, 0);

static void serial_flush_cpu(struct cpu* c)
{
    if (!c->line_len)
    {
        return;
    }
    spin_lock(&serial_lock);
    for (uint32_t i = 0; i < c->line_len; i++)
    {
        serial_out(c->line[i]);
    }
    c->line_len = 0;
    spin_unlock(&serial_lock);
}

void serial_flush(void)
{
    if (serial_ready)
    {
        serial_flush_cpu(this_cpu());
    }
}

// The boot's output until the APs start, UART or not, for
// samos.bootlog (kernel.c) on machines without a serial port.
static char serial_boot_log[SERIAL_BOOT_LOG_SIZE];
static unsigned int serial_boot_log_len;

const char* serial_boot_log_get(unsigned int* len)
{
    *len = serial_boot_log_len;
    return serial_boot_log;
}

// Framebuffer logging: while set, every character written here
// (UART or not) also goes to `fn` (kernel.c: the system terminal).
static void (*serial_mirror)(char c);

void serial_set_mirror(void (*fn)(char c))
{
    serial_mirror = fn;
}

bool serial_present(void)
{
    return serial_ready;
}

void serial_putc(char c)
{
    if (!smp_active() && serial_boot_log_len < SERIAL_BOOT_LOG_SIZE)
    {
        serial_boot_log[serial_boot_log_len++] = c;
    }
    void (*mirror)(char) = serial_mirror;
    if (mirror)
    {
        mirror(c);
    }
    if (!serial_ready)
    {
        return;
    }
    if (!smp_active())
    {
        spin_lock(&serial_lock);
        serial_out(c);
        spin_unlock(&serial_lock);
        return;
    }
    uint64_t flags = arch_irq_save();
    struct cpu* cpu = this_cpu();
    cpu->line[cpu->line_len++] = c;
    if (c == '\n' || cpu->line_len == CPU_SERIAL_LINE)
    {
        serial_flush_cpu(cpu);
    }
    arch_irq_restore(flags);
}

void serial_write(const char* str)
{
    while (str && *str)
    {
        serial_putc(*str++);
    }
}

void serial_write_hex(unsigned long long value)
{
    static const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        serial_putc(digits[(value >> shift) & 0xF]);
    }
}

// Panic path: no lock (its holder may be stopped for good), and this
// CPU's buffered partial line goes out first.
void serial_write_unlocked(const char* str)
{
    if (!serial_ready)
    {
        return;
    }
    struct cpu* c = this_cpu();
    for (uint32_t i = 0; i < c->line_len; i++)
    {
        serial_out(c->line[i]);
    }
    c->line_len = 0;
    while (str && *str)
    {
        serial_out(*str++);
    }
}

void serial_write_hex_unlocked(unsigned long long value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[19] = "0x";
    for (int i = 0; i < 16; i++)
    {
        buf[2 + i] = digits[(value >> (60 - 4 * i)) & 0xF];
    }
    buf[18] = 0;
    serial_write_unlocked(buf);
}
