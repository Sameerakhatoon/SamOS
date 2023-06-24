# SamOs - feature walkthrough

What to do, and what to expect, after `qemu-system-x86_64 -hda bin/os.bin`
to verify each book chapter's user-visible behaviour by hand. Pair
with `docs/testing-guide.md` (automated suite) and
`docs/debugging-history.md` (when something goes wrong).

The recipe is always the same shape:

1. Boot `qemu-system-x86_64 -hda bin/os.bin` with the right flags.
2. Watch the VGA window (or serial mirror) for an expected marker.
3. Optionally drive input via the QEMU monitor (`Ctrl-Alt-2` to
   switch the QEMU window into monitor mode, `Ctrl-Alt-1` to switch
   back to the guest console).

Or, much easier, run headless with the serial mirror that the test
suite uses, and grep:

```bash
log=$(mktemp)
( sleep 10; printf 'quit\n' ) | timeout 15 qemu-system-x86_64 \
    -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
    -serial file:"$log" -monitor stdio -no-reboot > /dev/null 2>&1
tail -c 2000 "$log"
```

Every section below first lists the marker you should see, then -
for the more interactive features - how to drive the kernel.

---

## Kernel boot banners

What you should see, in order, in the first ~400 bytes of COM1:

```
Hello world!
test
TSS_OK
PAGING_ON
bootsig=000055AA slen=00000005 scp=hi00000002 istr=00000001 ...
ss=000055AA frd=FFFFFFFE
hs=llo world!
 fz=0000000D ffl=00000000 afterclose=ok fs=FAT16 fszs=00000001 ...
km1=01411000 km2=01412000 km3=01411000

entering userland... (deferred, G04)
```

Each marker is one smoke probe inside `kernel_main`. Decoding:

| Marker | Proves |
|---|---|
| `Hello world!\ntest` | VGA driver works (Ch 38) |
| `TSS_OK` | `ltr 0x28` succeeded (Ch 90) |
| `PAGING_ON` | `enable_paging()` returned, CR3 is non-zero (Ch 51) |
| `bootsig=000055AA` | sector 0 read via ATA PIO + new disk API (Ch 54/55) |
| `slen=00000005` | `strnlen("hello", 100) == 5` (Ch 58) |
| `scp=hi00000002` | `strcpy()` worked, returned length 2 (Ch 64) |
| `istr/scmp/sterm` | case-insensitive cmp + termchar strnlen (Ch 69) |
| `mcp=00000003` | `memcpy()` worked, dest strlen 3 (Ch 71) |
| `ss=000055AA` | disk streamer reads boot sig from position 0x1FE (Ch 60) |
| `frd=FFFFFFFE` | `fread()` on fd=0 returns -EINVARG (Ch 73) |
| `hs=llo world!` | FAT16 fopen + fseek 2 + fread 11 (Ch 75/77) |
| `fz=0000000D` | `fstat()` reports hello.txt is 13 bytes (Ch 78/79) |
| `afterclose=ok` | `fclose()` succeeded (Ch 80/81) |
| `fs=FAT16` | FAT16 driver registered with VFS (Ch 65) |
| `fszs=00000001` | FAT16 packed structs are the spec sizes (Ch 66) |
| `did=00000000` | `disk.id` is 0 (Ch 67) |
| `pnz=00000001 rdt=00000004` | `fat16_resolve` populated fs_private + saw 4 root entries (Ch 68) |
| `km1/km2/km3` | kmalloc returns specific heap addresses, free+realloc reuses slot (Ch 48) |
| `entering userland... (deferred, G04)` | All kernel-init done, about to call `task_run_first_ever_task()` |

If you don't see one of these, the failure happened between the
previous marker and the next.

---

## Multitasking (Ch 150 + Testing!/Abc!)

Without sending any keys, you should see "Testing!" and "Abc!"
prints interleave on screen forever. Both come from `blank.elf`
instances loaded by `kernel_main` with different argv.

```bash
log=$(mktemp)
( sleep 5; printf 'quit\n' ) | qemu-system-x86_64 -hda bin/os.bin \
    -m 256 -accel tcg -display none -vga std -serial file:"$log" \
    -monitor stdio -no-reboot > /dev/null 2>&1
grep -oE 'Testing!|Abc!' "$log" | sort | uniq -c
```

Expect counts in the hundreds for each. If either is 0, see G07 /
G09 / G10 in `docs/debugging-history.md`.

## Exception handler (Ch 147)

The kernel also loads a `CRASH` task that prints "CRASH-RAN" then
null-derefs. `idt_handle_exception` is supposed to terminate the
faulting task and let the scheduler continue.

```bash
grep -c CRASH-RAN "$log"        # = 1
# AND check userland keeps printing AFTER CRASH-RAN
awk '/CRASH-RAN/{found=1; next} found' "$log" | grep -c 'Abc!'
```

## Exit syscall (Ch 148)

The `EXIT` task prints "EXIT-RAN" then returns from main; `c_start`
calls `samos_exit()` which dispatches cmd 9. Same pattern as
CRASH:

```bash
grep -c EXIT-RAN "$log"         # = 1
awk '/EXIT-RAN/{found=1; next} found' "$log" | grep -c 'Abc!'
```

## Backspace (Ch 118)

The `BS` task prints `"BS-ABC\b\b\bXYZ\n"`. On serial you see the
raw bytes; on VGA the row reads `BS-XYZ` because backspace walked
the cursor back to column 3 before XYZ landed.

```bash
xxd -p "$log" | tr -d '\n' | grep -qE '42532d414243(0820){3}58595a' \
    && echo "serial bytes OK"
```

To verify the VGA rendering manually, run with display:

```bash
qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -vga std
```

and look near the top of the screen for `BS-XYZ`.

## malloc / free (Ch 130/131)

The `MF` task does `samos_malloc(100) + samos_malloc(8000) +
samos_free(first) + samos_malloc(100)` then prints "MF-OK".

```bash
grep -c MF-OK "$log"            # = 1
```

`MF-NULL` instead means one of the mallocs returned NULL - check
kheap size and `process_malloc`.

## putchar (Ch 109)

The `PC` task prints "PC-" via cmd 1 then sends 'X', 'y', 'z', '\n'
through cmd 3 putchar (one int 0x80 each):

```bash
# Tolerant grep: other tasks may print between the cmd 3 calls,
# but X/y/z must appear in order somewhere after PC-:
grep -aoE '(PC-)|[Xyz]' "$log" | tr -d '\n' | grep -q 'PC-Xyz' \
    && echo "putchar OK"
```

## process_load_start, cmd 6 (Ch 138)

The `LD` task prints "LD-CALL", invokes `samos_process_load_start(
"0:/blank.elf")`, then prints "LD-RETURN" when the scheduler rolls
back to it.

```bash
grep -c LD-CALL "$log"   # = 1
grep -c LD-RETURN "$log" # = 1
```

## invoke_system_command, cmd 7 (Ch 145)

The `SH` task prints "SH-CALL", calls `samos_system_run("blank.elf
SHRAN")` which loads blank.elf with argv `["blank.elf", "SHRAN"]`
(falls into the default `print(argv[0])` loop), then prints
"SH-RETURN":

```bash
grep -c SH-CALL "$log"          # = 1
grep -c 'blank.elf' "$log"      # > 1 (the spawned task printing)
grep -c SH-RETURN "$log"        # = 1
```

## itoa, printf, readline (Ch 133/135/136)

The IT, PF, RL tasks emit specific markers:

```bash
grep -c 'IT:8763' "$log"        # = 1 (itoa)
grep -c 'PF:42=42 hi' "$log"    # = 1 (printf %i + %s)
grep -c 'RL-START' "$log"       # = 1 (RL task did reach main)
```

For RL-DONE you have to actually send keys (see "Driving the KEY
task" below).

## Caps lock + keyboard round-trip (Ch 116 + 149 + G11)

The `KEY` task loops `samos_getkeyblock()` + putchar(K), putchar(:),
putchar(c), putchar(\n). With G11, sendkeys delivered while KEY is
the current task land in KEY's buffer.

```bash
log=$(mktemp)
(
    sleep 8
    for i in $(seq 1 200); do printf 'sendkey a\n'; done
    sleep 0.5
    printf 'sendkey caps_lock\n'; sleep 0.3
    for i in $(seq 1 200); do printf 'sendkey a\n'; done
    sleep 1; printf 'quit\n'
) | qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg \
    -display none -vga std -serial file:"$log" -monitor stdio \
    -no-reboot > /dev/null 2>&1

grep -oE 'K:.' "$log" | sort | uniq -c
```

Expect mixed `K:a` (lowercase, pre-caps_lock) and `K:A`
(uppercase, post-caps_lock). If you only see one case, caps lock
isn't toggling - check `classic_keyboard_handle_interrupt`'s 0x3A
branch.

## Shell (Ch 137)

The shell.elf banner should be on serial near the top of the
userland section:

```bash
grep -c 'SamOs v1.0.0' "$log"   # = 1
grep -q '> ' "$log" && echo "prompt OK"
```

Drive the shell by sendkeys (race-y because of multitask):

```bash
( sleep 8; printf 'sendkey b\n'; printf 'sendkey l\n'; ... ) | qemu ...
```

In practice it's much easier to send each character with a
`sleep 0.05` between them and just hope the shell is the current
task often enough.

## ELF .bss zero-init (Ch 132 + G12)

The BSS task does `static int c; c++; print("BSS-OK") if c == 1`.

```bash
grep -c BSS-OK "$log"    # = 1
grep -c BSS-FAIL "$log"  # = 0
```

If you see `BSS-FAIL` here, G12 is missing - `process_map_elf` is
aliasing the .bss PHDR to the ELF buffer start. See
`docs/gotchas/G12-elf-bss-aliasing.md`.

## Kernel panic (Ch 82)

The normal kernel doesn't panic. To exercise the panic path:

```bash
EXTRA_CFLAGS=-DKERNEL_TEST_PANIC bash build.sh
log=$(mktemp)
( sleep 2; printf 'quit\n' ) | qemu-system-x86_64 -hda bin/os.bin \
    -m 256 -accel tcg -display none -vga std -serial file:"$log" \
    -monitor stdio -no-reboot > /dev/null 2>&1
tail -c 200 "$log"
```

Expect the log to end with `PANIC-TEST-OK` and NO subsequent
userland markers. Restore the normal build with `bash build.sh`.

---

## Driving QEMU directly

If you want to interact with the kernel by hand:

```bash
qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -vga std
```

This opens the VGA window. To send keys you can:

- Click into the VGA window and type. The keypress goes through
  the PS/2 emulation and SamOs's IRQ1 path.
- `Ctrl-Alt-2` to switch to the QEMU monitor, where you can
  `info registers`, `info pic`, `pmemsave`, `sendkey`, etc.
  `Ctrl-Alt-1` to switch back to the VGA console.

The QEMU window's caption tells you which input mode you're in
("guest" vs "console"). If keys appear to vanish into the host
terminal instead of the VM, you're in console mode.

---

## See also

- `docs/testing-guide.md` - the automated suite
- `docs/debugging-history.md` - every bug we hit and how
- `docs/part6-gdb.md` - GDB usage for live debugging
