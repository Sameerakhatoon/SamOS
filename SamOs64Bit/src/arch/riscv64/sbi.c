// SBI wrapper and the first console.
//
// The debug console extension (DBCN, SBI 2.0) is preferred; firmware
// without it gets the legacy putchar call (extension 0x01), which is
// deprecated but still served by OpenSBI.

#include "arch/riscv64/sbi.h"
#include "arch/riscv64/console.h"

static long spec_version;
static bool has_dbcn;

struct sbiret sbi_call(long eid, long fid, long arg0, long arg1, long arg2)
{
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a2), "r"(a6), "r"(a7) : "memory");
    return (struct sbiret){ a0, a1 };
}

bool sbi_probe(long eid)
{
    if (spec_version == 0) {
        return false;
    }
    struct sbiret r = sbi_call(SBI_EXT_BASE, 3, eid, 0, 0);
    return r.error == 0 && r.value != 0;
}

void sbi_init(void)
{
    // SBI 0.1 has no base extension and returns an error here.
    struct sbiret r = sbi_call(SBI_EXT_BASE, 0, 0, 0, 0);
    spec_version = r.error ? 0 : r.value;
    has_dbcn = sbi_probe(SBI_EXT_DBCN);
}

void sbi_console_putc(char c)
{
    if (has_dbcn) {
        sbi_call(SBI_EXT_DBCN, 2, (unsigned char)c, 0, 0);
    } else {
        sbi_call(SBI_EXT_LEGACY_PUTCHAR, 0, (unsigned char)c, 0, 0);
    }
}

static const char* impl_name(long id)
{
    static const char* const names[] = {
        "BBL", "OpenSBI", "Xvisor", "KVM", "RustSBI", "Diosix", "Coffer",
    };
    return id >= 0 && id < (long)(sizeof(names) / sizeof(names[0])) ? names[id] : "unknown";
}

void sbi_print_info(void)
{
    if (spec_version == 0) {
        console_printf("sbi: legacy 0.1, console putchar\n");
        return;
    }
    long impl = sbi_call(SBI_EXT_BASE, 1, 0, 0, 0).value;
    long impl_ver = sbi_call(SBI_EXT_BASE, 2, 0, 0, 0).value;
    console_printf("sbi: spec %ld.%ld, %s", (spec_version >> 24) & 0x7f, spec_version & 0xffffff, impl_name(impl));
    if (impl == 1) {
        // OpenSBI encodes its version as major << 16 | minor.
        console_printf(" %ld.%ld", impl_ver >> 16, impl_ver & 0xffff);
    }
    console_printf(", console %s\n", has_dbcn ? "dbcn" : "legacy putchar");

    static const struct { long eid; const char* name; } exts[] = {
        { SBI_EXT_TIME, "time" }, { SBI_EXT_IPI, "ipi" }, { SBI_EXT_RFNC, "rfence" },
        { SBI_EXT_HSM, "hsm" }, { SBI_EXT_SRST, "srst" }, { SBI_EXT_DBCN, "dbcn" },
    };
    console_printf("sbi: extensions");
    for (unsigned i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (sbi_probe(exts[i].eid)) {
            console_printf(" %s", exts[i].name);
        }
    }
    console_printf("\n");
}

long sbi_hart_status(unsigned long hartid)
{
    struct sbiret r = sbi_call(SBI_EXT_HSM, 2, hartid, 0, 0);
    return r.error ? r.error : r.value;
}

void sbi_shutdown(void)
{
    if (sbi_probe(SBI_EXT_SRST)) {
        sbi_call(SBI_EXT_SRST, 0, 0, 0, 0);
    }
    sbi_call(SBI_EXT_LEGACY_SHUTDOWN, 0, 0, 0, 0);
}
