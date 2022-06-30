# Ch 13 - Exception Handling Using Interrupts

**Book pages:** 33-35 (Part 4)
**Code updated:** `src/boot.asm`
**Test:** `tests/03-divzero-handler-fires.sh` (replaces the Hello World test)

## What we built

Same pattern as Ch 12, but for vector 0 (divide-by-zero exception, also known as #DE on more modern x86) instead of the software vector 0x80.

Flow:

1. Set up segment registers as before.
2. Write our handler's `(segment, offset)` to physical 0x00 / 0x02 (IVT entry for vector 0).
3. Run `mov ax, 0; div ax` which triggers #DE.
4. The CPU pushes FLAGS, CS, IP, then jumps to our `div_zero_handler`.
5. The handler prints "Divide by zero error!" and returns with `iret`.

## Real-mode CPU exception list (book's table)

Vectors 0 through 0x10 are reserved by the CPU for exceptions:

| Vector | Mnemonic | Cause |
|--------|----------|-------|
| 0 | #DE | Divide by zero |
| 1 | #DB | Single step (debugger trap) |
| 2 | NMI | Non-maskable interrupt |
| 3 | #BP | Breakpoint (`INT3`) |
| 4 | #OF | Overflow (`INTO`) |
| 5 | #BR | BOUND range exceeded |
| 6 | #UD | Invalid opcode |
| 7 | #NM | Coprocessor not available |
| 8 | #DF | Double fault |
| 9 | - | Coprocessor segment overrun |
| 0xA | #TS | Invalid TSS |
| 0xB | #NP | Segment not present |
| 0xC | #SS | Stack-segment fault |
| 0xD | #GP | General protection fault |
| 0xF | - | Reserved |
| 0x10 | #MF | Coprocessor (FPU) error |

Most of these only make sense in Protected Mode (TSS, segment-not-present, GPF, etc.). In Real Mode you mostly see #DE, #DB, #BP, #UD.

## Why the Hello World test got replaced

The bootloader's visible output is now the divide-by-zero message, not "Hello, World!". The old test `tests/02-bootloader-prints-hello.sh` would fail because that string is no longer printed. New test `tests/03-divzero-handler-fires.sh` checks the new visible behavior. Old test stays in git history (in earlier commits) for anyone who wants to revive the Ch 9 bootloader.

## Quirk: `print` returns with `ret`, not `iret`

The `div_zero_handler` calls `print` as a regular subroutine, then itself returns with `iret`. So `print` matches its caller's expectation: caller (handler) used `call`, callee uses `ret`. Only the handler-to-CPU boundary needs `iret`.
