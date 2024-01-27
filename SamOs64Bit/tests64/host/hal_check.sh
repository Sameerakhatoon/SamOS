#!/bin/bash
# The hardware layer's done line as a test.
#
# The plan says: `grep -r 'outb\|cr3\|idt' src/` finds nothing outside
# src/arch/x86_64/. Taken literally that grep cannot pass: 'idt' is a
# substring of 'width' (about 260 lines of graphics code) and 'cr3' of
# GPT's 'cr32_checksum'. So the words are matched whole, case
# insensitively, in code and comments alike, and the list is widened
# to the rest of x86's port I/O, descriptor-table and MSR vocabulary:
#
#   outb outw outl outdw inb inw inl insb insw insl insdw   port I/O
#   lidt idt*                                                the IDT
#   cr3 wrmsr rdmsr                                          paging root, MSRs
#
# and three structural checks back it up:
#   - no inline assembly outside src/arch/ (shared code reaches the
#     CPU only through arch/arch.h);
#   - no #include of an arch's private headers outside src/arch/
#     (only arch/arch.h, which picks the arch);
#   - the aarch64 and riscv64 user syscall stubs
#     (programs/stdlib/src/syscalls.inc) have the names and numbers of
#     x86_64's (programs/stdlib/arch/x86_64/samos.asm).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
fail=0

words='\b(outb|outw|outl|outdw|inb|inw|inl|insb|insw|insl|insdw|lidt|idt[a-z0-9_]*|cr3|wrmsr|rdmsr)\b'

# The pattern itself: it must see x86 code and not the false friends.
printf 'outb(0x80, 0);\nidt_init();\nmov %%cr3, %%rax\n' | grep -qiE "$words" ||
    { echo "FAIL: hal_check: the pattern misses x86 code"; exit 1; }
if printf 'int bits_width_per_character;\nuint32_t cr32_checksum;\n' | grep -qiE "$words"; then
    echo "FAIL: hal_check: the pattern matches width or cr32"; exit 1
fi

hits="$(grep -rnIiE "$words" src | grep -v '^src/arch/x86_64/' || true)"
if [ -n "$hits" ]; then
    echo "FAIL: x86 words outside src/arch/x86_64/:"; echo "$hits"; fail=1
fi

asm="$(grep -rnIE '__asm__|\basm\(|\basm +(volatile|__volatile__)' src --include='*.c' --include='*.h' | grep -v '^src/arch/' || true)"
if [ -n "$asm" ]; then
    echo "FAIL: inline assembly outside src/arch/:"; echo "$asm"; fail=1
fi

inc="$(grep -rnIE '#include *"arch/[a-z0-9_]+/' src | grep -v '^src/arch/' || true)"
if [ -n "$inc" ]; then
    echo "FAIL: arch-private headers included outside src/arch/:"; echo "$inc"; fail=1
fi

x86="$(awk '/^SYSCALL_WRAPPER4? /{ gsub(",", ""); print $2, $3 }' programs/stdlib/arch/x86_64/samos.asm | sort)"
gas="$(awk '/^SYSCALL /{ gsub(",", ""); print $2, $3 }' programs/stdlib/src/syscalls.inc | sort)"
if [ -z "$x86" ] || [ "$x86" != "$gas" ]; then
    echo "FAIL: syscalls.inc and samos.asm disagree:"
    diff <(echo "$x86") <(echo "$gas") || true
    fail=1
fi

[ "$fail" = 0 ] || exit 1
# Four checks (words, asm, includes, stubs) plus the pattern's two.
echo "hal host test: 6 checks, 0 failures ($(echo "$x86" | wc -l) syscall stubs compared)"
