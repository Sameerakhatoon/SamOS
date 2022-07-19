# Ch 27 - Enabling the A20 Line

**Book pages:** 65-66 (Part 5)
**Code updated:** `src/boot/boot.asm` - A20 enable inserted in `load32`
**Test:** `tests/06-a20-enabled.sh`

## What we built

Three instructions at the end of `load32`, just before the spin loop:

```asm
in al, 0x92      ; read System Control Port A
or al, 2         ; set bit 1 (A20 enable)
out 0x92, al     ; write back
```

## Why A20 exists at all

The original Intel 8086 (1978) had a 20-bit address bus. Addresses above 0xFFFFF wrapped around to zero. Some real-mode programs relied on this wrap. When the 80286 added a 24-bit address bus the chipset designers added a workaround: an external gate on address line 20 (A20). With the gate forced low, accesses to addresses 0x100000 and above wrapped exactly the way 8086 software expected. With the gate released (enabled), the full address range works.

For Protected Mode and 4 GiB addressing we need A20 enabled.

## How to enable it

There are several historical ways: through the keyboard controller (the slowest), through INT 0x15 AX=2401 (BIOS), through Fast A20 (System Control Port A at I/O 0x92, bit 1), or with the "Fast A20" path via port 0xEE. The Fast A20 method through 0x92 is what the book uses. It is a one-byte I/O write and works on essentially every PC made since the mid-1990s.

Bit 1 of port 0x92:
- 0 = A20 line gated (addresses wrap at 1 MiB)
- 1 = A20 line enabled

We `in` the current value of the port, OR in bit 1, then `out` it back. The `in` step preserves any other bits in the port (bit 0 is the "fast reset" bit which we definitely do not want to set by accident).

## Where in the bootloader

After the segment-register setup in `load32` and after setting up the stack, but before the spin loop. We are in 32-bit Protected Mode at this point so 16-bit and 32-bit `in`/`out` work equivalently for an 8-bit I/O port.

## QEMU note

QEMU emulates BIOS that already leaves A20 enabled by the time control reaches our boot sector. So even before this chapter the A20 line was on. The chapter is still meaningful because:

1. On real hardware A20 is sometimes left gated.
2. We need our bootloader to be self-contained.
3. The test verifies we can observe A20=1 after our explicit enable, which proves the I/O port write reached the chipset emulation.
