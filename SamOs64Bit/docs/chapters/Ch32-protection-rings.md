# Ch 28 - Protection Rings in 32-bit Protected Mode

**Book pages:** 69 (Part 5)
**Code in this chapter:** none, prose

## Four rings

The x86 architecture defines four privilege levels:

| Ring | Privilege | Typical user |
|------|-----------|--------------|
| 0 | most privileged | kernel |
| 1 | second-most | device drivers (rarely used) |
| 2 | second-least | system services (rarely used) |
| 3 | least privileged | user-mode applications |

Almost every modern OS uses only ring 0 (kernel) and ring 3 (user). Ring 1 and ring 2 were planned as intermediate levels but the value-add never justified the complexity.

## What ring 0 can do that ring 3 cannot

- Execute privileged instructions: `lgdt`, `lidt`, `ltr`, `mov cr0, ...`, `mov cr3, ...`, `cli`, `sti`, `hlt`, `wbinvd`, `invlpg`, `rdmsr`, `wrmsr`, etc.
- Access ring-0-only segments and pages.
- Read and write the I/O ports listed in the TSS I/O permission bitmap (default: none for ring 3).

If a ring-3 program tries any of these the CPU raises #GP (general protection fault). The kernel decides whether to deliver a signal, kill the process, or service it as an emulated request.

## How the CPU knows what ring it is in

Two places:

1. **CPL** (Current Privilege Level): the low two bits of the CS selector. When you load a code selector via far jump, call gate, interrupt return, or `sysret`, the CPU reads bits 0-1 of the selector and that becomes the new CPL.
2. **RPL** (Requested Privilege Level): the low two bits of any data selector (DS, SS, etc.) reflect the privilege level the loader claimed when writing it.

A ring 3 task has its code segment selector ending in `0x03` (binary `11`), data selectors also ending in `0x03`. Ring 0 segments end in `0x00`.

## Where SamOs will use this

Two natural places:

1. **GDT extension**: today our GDT has only ring 0 code (0x9A) and ring 0 data (0x92). Later we add ring 3 code (`0xFA`, DPL=3) and ring 3 data (`0xF2`, DPL=3) so user programs have selectors to run under.
2. **TSS and `iret` to ring 3**: when we want to drop from kernel to user mode we push a ring 3 selector and an `iret` jumps there. We need a TSS so the CPU knows where to put the kernel stack on the next interrupt back.

Both will happen many chapters from now once we have user programs to run.

## Paging is a different protection mechanism

Ring-based protection happens at the segment level (or the syscall instruction). Paging adds a per-4 KiB-page User/Supervisor bit and per-page write protection. The two protections compose: a user program can read or write a page only if both the segment descriptor and the page table entry agree the access is allowed.
