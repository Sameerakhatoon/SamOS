# Lecture 128 - TSC frequency and udelay (source-only)

L128 adds the first cut of `src/io/tsc.[ch]` plus a one-line
fix to `window.c` (the L123 `vecotr_at` typo).

## tsc.h

Type aliases for time units (`TIME_TSC`, `TIME_MICROSECONDS`,
`TIME_MILISECONDS` (sic), `TIME_SECONDS`) and prototypes:

- `tsc_frequency()`
- `read_tsc()`
- `tsc_seconds() / tsc_miliseconds()`
- `tsc_microseconds()` (defined in tsc.asm at L129)
- `udelay(us)`

## tsc.c

`tsc_frequency`:

1. Cache short-circuit on `tsc_freq_val`.
2. CPUID leaf 0x15 - bus/TSC ratio. `tsc_freq = (ecx * ebx) / eax`.
3. Fallback CPUID leaf 0x16 - base frequency in MHz, scale by
   1e6 to Hz.
4. Stash the value, return it.

`read_tsc` is a serialising `lfence; rdtsc` that recombines
edx:eax into a 64-bit value.

`udelay(us)` reads `tsc_frequency`, samples a start TSC,
computes `cycles_to_wait = (us * freq) / 1e6`, and spins
`pause`ing until the difference exceeds the threshold.

## Upstream bugs preserved verbatim

1. **`tsc_freq *= 1000000` overshoot** - both CPUID branches
   already produce Hz; the trailing multiply makes the cached
   frequency 10^6 times too large. Every `udelay` then waits
   10^6 times fewer cycles than asked for, effectively
   becoming a no-op on a 3 GHz core.
2. **`tsc_seconds` missing parens** - the body reads
   `tsc_miliseconds` without `()`, so the assignment captures
   the function pointer's address instead of its return value.
   Dividing the address by 1000 yields garbage. We preserve
   the shape with an explicit `(TIME_MILISECONDS)(uintptr_t)`
   cast so the build stays clean; the value remains wrong.

Both bugs are documented inline. Neither is observable yet -
`tsc.o` is not in `FILES` (L128 leaves the Makefile alone) and
`tsc_microseconds` is not defined until L129's tsc.asm.

## window.c

The `vecotr_at` typo introduced in L123 (and worked-around
with the static-inline wrapper) is corrected to `vector_at`.
The wrapper is removed.

## BIOS test status

Source-only. Test asserts both files exist, every type +
function name lands, the CPUID leaves are referenced, the
rdtsc+lfence + pause spin are wired, the typo fix from L123 is
in, and tsc.o is NOT yet in FILES.
