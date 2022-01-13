# Lecture 130 - QEMU CPU = Skylake-Server, fixed TSC

L130 is a one-line `run.sh` change. The QEMU CPU model is
swapped from `qemu64` to `Skylake-Server` with a hard-coded
`tsc-frequency=2800000000` (2.8 GHz).

## Why

The L128 `tsc_frequency` path reads CPUID leaves 0x15 and
0x16. `qemu64` does NOT expose those leaves, so both queries
return zero and the cached frequency lands at 0. `udelay` then
divides by zero (or, on x86, returns 0 cycles to wait) and
the L129 smoke loop runs as fast as the print path can flush.

`Skylake-Server` does expose 0x15 / 0x16. With
`tsc-frequency=2800000000` the guest also sees a stable rate
the host won't drift against, so `udelay(1000000)` actually
takes one second.

## Combined with L128 bug

The L128 trailing `tsc_freq *= 1000000` overshoot is still in
the source. With Skylake-Server + 2.8 GHz, `tsc_frequency`
returns 2.8e15 instead of 2.8e9, so `cycles_to_wait` is
divided by 10^6 too many and `udelay(1000000)` waits 1
microsecond instead of 1 second. The L129 "Another second"
messages will still burst out together. Documented as a
follow-up; an explicit "drop the L128 overshoot" commit will
land later.

## BIOS test status

Source-only. Test asserts `run.sh` mentions Skylake-Server +
the 2.8 GHz tsc-frequency and that the stale `qemu64` line is
gone. The build still links.
