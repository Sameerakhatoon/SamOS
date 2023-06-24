# SamOs - testing guide

How to run the regression suite, what every test covers, the
infrastructure helpers, and how to add a new test.

## TL;DR

```bash
cd ~/projects/samos
bash tests/run.sh
```

You should see `passed: 50   failed: 0` at the bottom. Individual
tests live in `tests/NN-name.sh` and are runnable standalone:

```bash
bash tests/45-exception-handler.sh
echo $?     # 0 = pass, non-zero = fail
```

A single suite run takes about 20 minutes on a modern laptop
because every test spins its own QEMU instance and waits for the
marker-sync helper.

## Test-suite layout

| Path | What it is |
|---|---|
| `tests/run.sh` | top-level runner; walks `tests/[0-9]*.sh` in numeric order |
| `tests/NN-name.sh` | one test per file, exits 0 on pass |
| `tests/_lib.sh` | marker-sync helpers (`run_kernel_capture`, `run_kernel_inspect`) used by most post-Ch-100 tests |
| `tests/lib.sh` | older helpers from the early chapters; superseded by `_lib.sh` |

## How tests synchronise without `sleep N` races

`tests/_lib.sh` exposes two functions. Both poll the COM1 serial
mirror (every `print()` char is dual-routed to port 0x3F8) for a
marker string, then send QEMU monitor commands. No wall-clock
guess at "is the kernel ready yet?"

### `run_kernel_capture LOG [MARKER1] [MARKER2]`

Boot the kernel, capture every COM1 byte into `LOG`, quit as soon
as `MARKER1` appears (default: `entering userland`) AND - if a
second marker is given - `MARKER2` appears after that. Used by
behavioural tests that need to grep specific strings out of the
log.

```bash
run_kernel_capture "$log" 'CRASH-RAN' 'Abc!'
# Quits when CRASH-RAN has been printed AND Abc! has printed AFTER
# that. Proves CRASH ran AND the kernel kept running afterwards.
```

### `run_kernel_inspect REGS LOG MARKER1 MARKER2 -- MONITOR-CMD [...]`

Same marker-sync, but at the sync point also pipes the monitor
commands to QEMU and captures their replies into `REGS`. Used by
the info-registers tests (05/08/09/14/35) and by the BS-XYZ VGA
check in test 47.

```bash
run_kernel_inspect "$regs" "$log" 'entering userland' '' 'info registers'
grep -oE 'CR0=[0-9a-fA-F]+' "$regs"
```

Both helpers use `-no-reboot` so a triple-fault kills QEMU instead
of looping, plus a 25-second wall-clock backstop in case the
kernel hangs before the marker.

## Every test, what it asserts

### Tooling / pre-PM

| # | File | Asserts |
|---|---|---|
| 01 | `tooling-installed.sh` | nasm, qemu-system-x86_64, mtools all present |
| 05 | `enters-protected-mode.sh` | CR0.PE = 1 AND CS in {0x08, 0x1B} |
| 06 | `a20-enabled.sh` | A20 line writeable past 1MiB |
| 07 | `cross-compiler-works.sh` | `i686-elf-gcc` exists |

### Kernel boot

| # | File | Asserts |
|---|---|---|
| 08 | `kernel-loaded.sh` | boot.bin = 512B, os.bin >= 51713B, EIP in kernel/user range |
| 09 | `kernel-main-runs.sh` | `kernel_main` symbol exists AND EIP reached steady state |
| 10 | `vga-prints-hello.sh` | "Hello world!" + "test" on COM1 |

### IRQ + keyboard

| # | File | Asserts |
|---|---|---|
| 11 | `keyboard-fires.sh` | sendkey 'a' round-trips to `K:a` on serial (full IRQ1 -> driver -> KEY task -> cmd 2 -> cmd 3 path) |
| 12 | `keyboard-fires-repeatedly.sh` | sendkey a/b/c all round-trip as K:a/K:b/K:c (proves scancode -> char map distinguishes letters) |

### Kheap / paging

| # | File | Asserts |
|---|---|---|
| 13 | `kmalloc-works.sh` | km1=`01411000`, km2=`01412000`, km3=`01411000` (exact addrs prove free+realloc reuses slot) |
| 14 | `paging-enabled.sh` | `PAGING_ON` marker AND CR0.PG=1 AND CR3 >= heap base |

### Disk + filesystem

| # | File | Asserts |
|---|---|---|
| 16 | `disk-reads-sector.sh` | `bootsig=000055AA` |
| 17 | `string-utils.sh` | `slen=00000005` |
| 19 | `disk-streamer.sh` | `ss=000055AA` |
| 20 | `fat16-bpb.sh` | FAT16 BPB headers loaded |
| 22 | `strcpy.sh` | `scp=hi00000002` |
| 23 | `fat16-registers.sh` | `fs=FAT16` |
| 24 | `fat16-struct-sizes.sh` | `fszs=00000001` |
| 25 | `disk-id-and-fs-private.sh` | `did=00000000` |
| 26 | `fat16-resolver.sh` | `pnz=00000001 rdt=00000004` |
| 27 | `string-cmp.sh` | `istr/scmp/sterm` exact values |
| 29 | `memcpy.sh` | `mcp=00000003` |
| 31 | `vfs-fread.sh` | `frd=FFFFFFFE` (-EINVARG) |
| 32 | `fat16-read.sh` | `hs=llo world!` (fopen+fseek 2+fread proves cross-sector read) |
| 33 | `fstat.sh` | `fz=0000000D ffl=00000000` |
| 34 | `fclose.sh` | `afterclose=ok` |

### TSS, userland, syscalls

| # | File | Asserts |
|---|---|---|
| 35 | `tss-loaded.sh` | `TSS_OK` AND `TR=0028` |
| 36 | `userland-prologue.sh` | userland transition asm linked in |
| 37 | `ring3-reached.sh` | `entering userland... (deferred, G04)` marker |
| 38 | `syscall-roundtrip.sh` | int 0x80 cmd 0 (sum) returns 50 in EAX |
| 39 | `syscall-print.sh` | FIVE specific cmd-1 strings all present (Testing!, Abc!, CRASH-RAN, BS-ABC, EXIT-RAN) |
| 40 | `irq-before-task.sh` | G05 sentinel - kernel survives IRQs before first user task |

### Multitasking + ELF

| # | File | Asserts |
|---|---|---|
| 41 | `multitasking.sh` | >=5 Testing! tokens AND >=5 Abc! tokens (proves both scheduled by PIT) |
| 42 | `elf-format.sh` | ELF signature, ELFCLASS32, ELFDATA2LSB, ET_EXEC, entry >= 0x400000 |
| 43 | `kernel-symbols.sh` | ~80 named kernel symbols required to link |
| 44 | `userspace-symbols.sh` | ~25 named user symbols in blank.elf and shell.elf |

### Post-Ch-100 syscalls

| # | File | Asserts |
|---|---|---|
| 45 | `exception-handler.sh` | `CRASH-RAN` AND userland keeps printing (Ch 147 idt_handle_exception works) |
| 46 | `exit-syscall.sh` | `EXIT-RAN` AND userland keeps printing (Ch 148 cmd 9 works) |
| 47 | `backspace.sh` | raw `42532d414243(0820){3}58595a` on serial AND VGA shows `BS-XYZ` rendered in place (Ch 118 backspace) |
| 48 | `malloc-free.sh` | `MF-OK` AND userland continues (Ch 130/131) |
| 49 | `putchar.sh` | `PC-` then `X`, `y`, `z` in order via cmd 3 (Ch 109) |
| 50 | `caps-lock.sh` | >=1 K:a AND >=1 K:A (Ch 149 caps_lock toggles case) |
| 51 | `process-load-start.sh` | `LD-CALL` AND `LD-RETURN` (Ch 138 cmd 6) |
| 52 | `system-command.sh` | `SH-CALL`, `blank.elf`, `SH-RETURN` (Ch 145 cmd 7) |

### Stdlib + shell + ELF .bss + panic

| # | File | Asserts |
|---|---|---|
| 53 | `itoa.sh` | exact `IT:8763` (Ch 133 itoa) |
| 54 | `printf.sh` | exact `PF:42=42 hi` (Ch 135 printf with %i and %s) |
| 55 | `readline.sh` | `RL-START` AND `RL-DONE` (Ch 136 readline returns on \n) |
| 56 | `shell-boots.sh` | `SamOs v1.0.0` AND `> ` prompt (Ch 137 shell.elf reaches its readline loop) |
| 57 | `bss-init.sh` | `BSS-OK` AND no `BSS-FAIL` (Ch 132 + G12 - .bss zero-inits) |
| 58 | `panic.sh` | `PANIC-TEST-OK` AND no userland markers after (Ch 82 panic halts) |

## Build variants

Most tests use the default `bin/os.bin` produced by `bash build.sh`.
Test 58 is the only exception: it rebuilds with `EXTRA_CFLAGS=
-DKERNEL_TEST_PANIC` to produce a kernel that hits `panic()` before
reaching userland, then restores the normal build on exit via
`trap`.

You can drive the same flag manually:

```bash
EXTRA_CFLAGS=-DKERNEL_TEST_PANIC bash build.sh
qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none \
    -serial stdio -no-reboot
# stops at PANIC-TEST-OK\n then `while(1){}`
```

## How to add a new test

1. Copy `tests/45-exception-handler.sh` as a template - it's the
   simplest "watch a marker, assert the kernel kept running" shape.
2. Pick a unique 2-digit number; numbers are NOT chapter-aligned,
   they're just the suite's run order.
3. Use `source tests/_lib.sh` and call `run_kernel_capture` (for
   pure serial-grep tests) or `run_kernel_inspect` (when you need
   info registers / pmemsave).
4. Exit 0 on pass, anything else on fail - that's all the runner
   cares about.
5. Make it chmod-executable (`chmod +x tests/NN-name.sh`).

If the marker you want only appears in one specific kernel build,
add an `EXTRA_CFLAGS=...` invocation at the top and a `trap`
restoring the default build on exit (test 58 is the template for
this).

## Triage when a test fails

1. Run it alone with `bash tests/NN-name.sh` - lots of suite-level
   flakes are timing-only and pass standalone.
2. If it still fails, dump the COM1 log directly:

   ```bash
   log=$(mktemp)
   ( sleep 10; printf 'quit\n' ) | timeout 15 qemu-system-x86_64 \
       -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
       -serial file:"$log" -monitor stdio -no-reboot > /dev/null 2>&1
   tail -c 800 "$log"
   ```

3. If the log stops before "entering userland...", the kernel
   crashed early - reach for `info registers` (see
   `docs/part6-gdb.md`).
4. If the log shows userland but your marker is missing, the user
   task either never ran or crashed; add a `print("DEBUG-X\n")`
   right before the syscall that should have emitted the marker.
5. If everything looks fine on the log but the test still fails,
   double-check the grep regex - test 47 needed updating when
   `terminal_writechar` started mirroring the backspace + space
   pair to serial.

## See also

- `docs/debugging-history.md` - the full bug-hunt narrative
- `docs/feature-walkthrough.md` - manual QEMU testing recipes
- `docs/part6-gdb.md` - GDB cheatsheet
