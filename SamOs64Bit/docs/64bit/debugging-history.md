# SamOs Module 2 (64-bit) - debugging history

A chronological tour of every fault, panic, silent miscompile,
preserved-upstream typo, NULL-deref, link error, runtime hang,
and behavioural mystery this branch hit while porting Lectures
105 through 208 plus the BIOS-boot rescue at the tail of the
port.

If a future regression smells like one of these, this is the
place to start looking. The Module 1 file at
`docs/debugging-history.md` numbers gotchas G01..G12 (Ch01
to ~Ch150). This file picks up the numbering at G13 and runs
through G45.

Every row links to a chapter doc under
`docs/64bit/chapters/Lxx-*.md` and (where the bug ships in a
specific commit) the SamOs commit it landed in. Where the bug
was an upstream typo we preserved verbatim, the row is tagged
`(sic)` and the fix-commit column points at the later lecture
that upstream itself fixed it in.

---

## Master table

| # | What broke | When | Symptom | Found with | Lives in / Fix |
|---|---|---|---|---|---|
| G13 | L74 deleted the 32->64 PAE / CR3 / EFER.LME / CR0.PG dance from `kernel.asm` | L74 | BIOS QEMU triple-faults at the very first `mov rax, ...` (REX-prefixed = garbage in 32-bit) before `kernel_main` runs | QEMU `-d int,cpu_reset` showed EFER=0, IDTR=BIOS-IVT, RIP in low memory | Accepted as deviation per upstream. Boot.asm rescue added at end of port; see G43 |
| G14 | `grpahics_setup_stage_two` upstream typo (r/a swap) | L141 | Tests look for the corrected spelling; source still has the typo | `grep -rn grpahics src/` | Renamed at L150 to `graphics_setup_stage_two`. Earlier sessions had used a static inline forwarder, retired at L150 |
| G15 | L145 helper decls never reached `window.h` (silent `Edit` failure) | L145..L149 | Link error when L150 tries to call `window_owns_graphics`, `window_focused`, `window_get_from_graphics`, `window_redraw_region`, `window_redraw_body_region`, `window_title_set` | Linker undefined symbols + `git diff` showed the header was unchanged | L149 cleanup commit lands the missed decls together with upstream's own L149 fixes |
| G16 | `graphics_redraw_region` width clip + child intersect MIN/MAX inversion + transparency_key write to wrong field + `>=` vs `>` left-edge inconsistency | L93/L99/L116/L141 (introduced) | Wide redraws silently dropped; child compositor blends wrong region; transparency key never applied | Walking the L150 commit message + reading the four chapter docs together | `chapters/L150-fix-runtime-bugs.md`. All four bugs retired in one cleanup |
| G17 | `vector_Free` (capital F) in `process_free_process` | L161 | Link error: undefined symbol `vector_Free` | gcc unresolved symbol on first commit attempt | Use correct `vector_free`; code comment preserves the upstream identifier in the audit trail. `chapters/L161-window-event-proc-part2.md` |
| G18 | Window-events ring: producer writes at `index % MAX` with `index` never incremented, consumer always reads slot 0 | L161 | Multiple producers overwrite each other; consumer only sees the latest; at most one slot ever holds meaningful data | Reading the body; the math is wrong for any push count > 1 | Preserved verbatim per port rule. Documented as inert (a single producer + single consumer happens to work for sparse traffic). `chapters/L161-...md` |
| G19 | `isr90h_command24_update_window_title` (90h vs 80h) | L173 | Identifier diverges from the rest of the isr80h family | Reading the diff: every other handler is `isr80h_*`, this one isn't | Preserved verbatim at L173. Upstream's own L180 renames it to `isr80h_*`; SamOs follows the fix at L180 |
| G20 | `disk_driver_register(driver)` self-recursion (uniqueness check calls itself, not `_registered`) | L184 | Infinite recursion -> stack overflow -> triple fault on the first driver registration | Reading the body; the call name doesn't match the intent | Preserved verbatim at L184 with a comment. Inert because the load-drivers helper is an empty stub at L184. Upstream fixes at L189; SamOs follows |
| G21 | `disk_read_block` post-L186 dispatches through `idisk->driver->functions.read`, but bootstrap disk0 has `driver = NULL` until L189 | L186 | NULL deref -> triple fault during very early boot before the L181 "Total PCI devices" print | Boot did not produce the L181 banner under QEMU even after a successful build | SamOs deviation: keep the legacy `disk_read_sector` fallback when `idisk->driver == NULL`. Same idea for `disk_create_new`'s inline FS mount when driver is NULL. Both paths drop out once L189 PATA registers. `chapters/L186-disk-rewrite-for-driver.md` |
| G22 | `PCI_BASE_CLASS(bus, slot, func)` macro forgets the `func` arg when calling `pci_cfg_read_byte`; `PCI_BASE_SUBCLASS` supplies it | L177 | Inconsistent macro arity; the former always reads function 0's class even when func != 0 | Reading the macros side by side | Preserved verbatim. `chapters/L177-pci-headers.md` |
| G23 | `pci_ecam_cfg_ptr` range match: `bus < r->start_bus \|\| bus < r->end_bus` (should be `bus > r->end_bus` on the second half) | L178 | ECAM path always misses, every config-space access silently falls back to legacy 0xCF8/0xCFC | Reading the body; the second clause makes no sense | Preserved verbatim with a comment that the ECAM path stays dormant. `chapters/L178-pci-source-part1.md` |
| G24 | `pci_cfg_reaD_dword` call site (capital D on the second D) inside `pci_size_bars` | L179 | Link error: undefined symbol `pci_cfg_reaD_dword` | gcc unresolved symbol | Static-inline shim under the typo'd name forwards to `pci_cfg_read_dword`. Call site stays verbatim. Upstream fixes at L180; SamOs drops the shim. `chapters/L179-pci-source-part2.md` |
| G25 | `pci_size_bar` forward decl (singular) vs `pci_size_bars` definition (plural) | L178/L179 | Orphan static forward decl; under `-Werror=missing-prototypes` would fail | `-Wno-unused-function` flag silences it; manual code-review caught the mismatch | Preserved verbatim; suppressed by the project's `-Wno-unused-function` flag. `chapters/L179-pci-source-part2.md` |
| G26 | `pci_cfg_write_dword` declared twice in pci.h with different value types (`uint32_t` and `uint16_t`) | L177 | Duplicate decl is technically legal C (overload-style not), but the intent of the second is `pci_cfg_write_word` | Reading the header carefully | Preserved verbatim at L177. Upstream fixes at L180 to `pci_cfg_write_word`; SamOs follows. `chapters/L180-pci-source-part3.md` |
| G27 | `pata_disk_ctrl_drive_address(struct disk disk, int offset)` takes the disk by value, then passes it to `disk_private_data_driver` which expects a pointer | L188 | Compile error: cannot pass `struct disk` where `struct disk *` is expected | gcc type-mismatch error on first build | L189 fixes the arg to `struct disk *`. SamOs lands the L189 fix at L188 so the file builds, with a comment marking the deferred upstream fix. `chapters/L188-pata-source.md` |
| G28 | `pata_private_new(PATA_DISK_DRIVE type, ...)` body ignores `type` and unconditionally writes `PATA_PRIMARY_DRIVE` | L188 | PARTITION-typed private records get labelled PRIMARY; subsequent address resolution returns the wrong base IO port | Reading the body | Preserved verbatim. `chapters/L188-pata-source.md` |
| G29 | `disk_private_data_driver` referenced in L188 source but decl + body don't land until L189 | L188 | Link error: undefined symbol | gcc unresolved symbol | Hoist the decl + body into L188 so the file links. Documented as a SamOs early-port. `chapters/L188-...md` and `chapters/L189-...md` |
| G30 | `nvme_compeltion_queue_entry` (typo'd struct name, missing `l`) | L190 | Pointers to the typo'd struct co-exist with `nvme_completion_queue_entry*` (correct), both legal incomplete types; the data union is forever inaccessible from the correctly-spelled name | Reading the struct defs side by side | Preserved verbatim at L190. Upstream fixes at L192 (renames the struct). SamOs follows |
| G31 | `NVME_COMPLETION_QUEUE_STATUS` macro has a trailing space after the line-continuation `\` | L190 | gcc rejects under `-Werror`: "backslash-newline at end of file" / "trailing whitespace after \\" | gcc warning -> error during first build | Strip the stray space; macro shape preserved. Comment in `chapters/L190-nvme-header.md` |
| G32 | `nvme_pci_mmio_base` low-mask is `0xFFFFFFFF0ULL` (one trailing 0; an extra hex digit beyond the 32 bits the BAR architecture asks for) | L193 | Suspect constant; in practice aligns BAR low half to 16 bytes which is harmless on real BAR layouts but obviously not the intent | Reading the constant carefully | Preserved verbatim with a comment. `chapters/L193-nvme-source-part2.md` |
| G33 | `nvme_admin_completion_queue_init` checks `p->submission_queue.ptr` rather than `completion_queue.ptr` | L193 | The CQ init readiness check is wrong; in practice the check is only called after both queues are allocated, so the bug is inert | Reading the body | Preserved verbatim with a comment. `chapters/L193-...md` |
| G34 | `nvme_disk_driver_umount` (missing 'n') call site inside `nvme_disk_driver_mount_for_device` | L193 | Link error: undefined symbol `nvme_disk_driver_umount` | gcc unresolved symbol | Static shim under the typo'd name forwards to `nvme_disk_driver_unmount`; call site stays verbatim. Upstream fixes at L195; SamOs drops the shim. `chapters/L193-...md`, `chapters/L195-nvme-finalize.md` |
| G35 | `slba + nlb;` dead expression in NVME read chunk loop (should be `slba += nlb;`) | L194 | Every chunk after the first re-reads sector 0 of the user's logical request | Reading the loop body | Wrapped in explicit `(void)` cast so `-Werror=unused-value` accepts it; the bug is preserved (every chunk after the first still re-reads sector 0). Upstream fixes at L195; SamOs follows. `chapters/L194-nvme-source-part3.md` |
| G36 | `keyboard_init()` called after `window_system_initialize_stage2()` -> window keyboard listener registration silently bails | L175 + L196 | Userland windows never receive `WINDOW_EVENT_TYPE_KEY_PRESS`. No crash, no log, just nothing | Worked through stage-2 init: `keyboard_register_handler(NULL, ...)` calls `keyboard_default()` which is the head of the inserted-keyboards list; `keyboard_init` runs the insert; if it hasn't run, the list head is NULL and registration returns `-EINVARG` | L196 hoists `keyboard_init` to before `window_system_initialize_stage2`. Original call site left as a comment. `chapters/L196-keyboard-events-userland-fix.md` |
| G37 | `task_sleep` deadline is `tsc_microseconds() * microseconds` (multiplication) | L197 | Almost any caller sleeps for years; scheduler skips that task indefinitely; whole-ring asleep -> `task_next` udelay-spins forever | Code review of L197 body; one-microsecond sleep would still set a 1e6-tick deadline at 1 MHz TSC | Preserved verbatim at L197. Upstream fixes at L198 part 2 (`+ microseconds`). SamOs squashes both L198 parts so the broken-multiply state never lives in a single SamOs commit. `chapters/L197-task-sleep.md` |
| G38 | `process_realloc(NULL, n)` returns -ENOTFOUND instead of `malloc(n)` | pre-L201 | Userland `realloc(NULL, n)` always fails; no allocation ever happens | Reading C standard side by side with the body: `realloc(NULL, n)` must equal `malloc(n)` | L201 lands an early-out `if (old_virt_ptr == NULL) return process_malloc(...)`. `chapters/L201-process-realloc-bugfix.md` |
| G39 | `disk_Stream_cache_bucket_level1` (capital S) typo inside a `sizeof()` in the L204 cache level-1 allocator | L204 | gcc accepts it as a forward decl of an unrelated struct; sizeof(pointer-to-pointer-to-struct) is `sizeof(void *)`, so the byte-count math accidentally works | Reading the sizeof; the struct name looks wrong | Preserved verbatim at L204. Upstream fixes at L206; SamOs follows. `chapters/L204-disk-stream-cache-part1.md`, `chapters/L206-disk-stream-cache-part3.md` |
| G40 | L205 ships with a missing semicolon (`res = cache_res` instead of `res = cache_res;`) inside the rewritten `diskstreamer_read` | L205 | Compile error: expected `;` before `goto` | gcc syntax error on first build | L206 adds the missing semicolon. SamOs holds the new `diskstreamer_read` body back from L205 to L206 so the L205 commit builds cleanly without inheriting the broken body. Documented in `chapters/L205-disk-stream-cache-part2.md` |
| G41 | The new L205 cached `diskstreamer_read` references `stream->sector_size` but the field lives on the struct from L206 only | L205 | Compile error: no member `sector_size` in `struct disk_stream` | gcc field-not-found error | Hoist the field decl to L205 so the L205 body would compile. SamOs takes the conservative path: keep the old iterative `diskstreamer_read` at L205, land both the field AND the new body at L206. `chapters/L205-...md` |
| G42 | The new L205 cached `diskstreamer_read` calls `disk_real_offset` which doesn't exist until L206 | L205 | Compile error: implicit declaration of `disk_real_offset` | gcc warning-as-error | Same resolution as G40/G41: the new body lands at L206 alongside the helper. `chapters/L205-...md`, `chapters/L206-...md` |
| G43 | Late-session investigation: every QEMU `-hda` boot test triple-faults | post-L208 | A 169-test sweep showed ~53 boot-trace tests failing identically: "kernel did not write expected token to 0xb8000" | QEMU `-d int,cpu_reset -D /tmp/qemu.log` showed EFER=0 + IDTR=BIOS IVT, traced to the L74 deviation (G13) | Wrote a PAE / CR3 / EFER.LME / CR0.PG transition + 64-bit GDT slot in `boot.asm`, plus a 64-bit far jump to 0x100000 |
| G44 | Even with G43 in place, kernel's `.data` section was past the 250-sector load and never reached RAM | G43 follow-up | First print() wrote one char from an obviously-uninit `static` then the kernel halted | Compared `wc -c bin/kernel.bin` to boot.asm's `mov ecx, 250` (250 * 512 = 128 KiB < 169 KiB kernel) | Two back-to-back LBA28 PIO reads of 200 sectors each (LBA28 single-command cap = 255 sectors) |
| G45 | Post-L100 the kernel's `print()` path writes to the graphics framebuffer, not to 0xb8000 | introduced L100 | Boot-trace regression tests that `pmemsave 0xb8000` to scrape "Hello 64-bit!" et al find only SeaBIOS leftovers | Comparison: `print()` calls `terminal_writechar` -> `terminal_write` (graphics terminal) since L100 | The right reformulation is `grep -a 'token' bin/kernel.bin` -- the print path is intact in the source, just no longer landing at 0xb8000. The boot-trace tests need to be reformulated when revived |

---

## Cluster A: PCI source bugs (L177-L181)

PCI source ships with five preserved-typo / preserved-bug
markers across L177-L181, all of which compile cleanly with
a static-inline shim plus a comment. The cluster matters
because three of the five are "would not link" bugs that
require a shim to keep the build green per-commit.

| Gotcha | Form | Workaround |
|---|---|---|
| G22 | Macro arity | Macro stays as-is |
| G23 | Wrong comparison in ECAM range match | Preserved; documented as "ECAM path stays dormant" |
| G24 | Capital D in `reaD_dword` call site | Static inline `pci_cfg_reaD_dword` shim |
| G25 | `pci_size_bar` vs `pci_size_bars` mismatch | `-Wno-unused-function` silences the orphan decl |
| G26 | Double-decl of `pci_cfg_write_dword` | Preserved verbatim until L180 fix |

After L180, the cluster reduces to G22 + G23 (macro arity +
ECAM range match), both still live but inert (G22 is a single
macro the kernel doesn't expand at link time; G23 makes the
ECAM path always miss but the legacy path covers it).

---

## Cluster B: disk driver abstraction (L183-L189)

The disk abstraction lands across seven lectures. The boundary
case that bit us is "lecture introduces a vtable call before
a later lecture wires the driver". Specifically:

```c
// L186 disk_read_block
if (!idisk->driver->functions.read) return -EIO;
return idisk->driver->functions.read(idisk, lba, total, buf);
```

When the bootstrap disk has `driver = NULL` (until L189 PATA
registers), this NULL-derefs.

**SamOs port deviation (G21):**

```c
// L186 SamOs - keep legacy fallback alive while driver is NULL
if (idisk->driver) {
    if (!idisk->driver->functions.read) return -EIO;
    return idisk->driver->functions.read(idisk, lba, total, buf);
}
return disk_read_sector(lba, total, buf);
```

The fallback drops out the moment L189 lands, with no source
change required: from then on `idisk->driver` is non-NULL.
The audit trail is preserved as a `if (!driver)` block in
`disk_create_new` too, where the inline FS mount stays in
place when driver is NULL.

---

## Cluster C: NVME source bugs (L190-L195)

NVME is the typo-densest module of the entire port. Six
gotchas across six lectures (G30-G35):

| Gotcha | Form | Resolution at port time |
|---|---|---|
| G30 | Struct name typo `nvme_compeltion_queue_entry` | Preserved; upstream fixes at L192 |
| G31 | Trailing space after macro line-continuation `\` | Strip the space; not preservable since gcc rejects |
| G32 | BAR low-mask `0xFFFFFFFF0ULL` (extra 0) | Preserved; documented as "happens to work" |
| G33 | CQ-init check reads SQ pointer | Preserved; documented as "inert (caller orders allocs)" |
| G34 | `nvme_disk_driver_umount` (missing n) | Static shim under the typo'd name |
| G35 | `slba + nlb;` dead expression | `(void)` cast; preserved; fixed by L195 |

Of the six, G31, G34, G35 are "would not link" / "would not
compile" bugs that need a workaround to keep the build green.
G30, G32, G33 are preservable.

Lesson from this cluster: when a multi-lecture series ships
this many typos, the cheapest port strategy is "static shim
under the typo, drop the shim at the fix-commit". Three of
the six gotchas were retired this way; the audit trail is a
single comment plus a one-line shim per typo.

---

## Cluster D: disk stream cache compile gap (L205-L206)

L205 ships with a new `diskstreamer_read` body that depends
on three things only present at L206:

- `stream->sector_size` field (G41)
- `disk_real_offset` accessor (G42)
- A missing semicolon (G40)

All three would prevent the L205 commit from compiling
in isolation. SamOs lets the L205 commit land the new
cache helpers (`diskstreamer_cache_round_robin_add`,
`diskstreamer_cache_find`) but keeps the OLD iterative
`diskstreamer_read` in place. The new body lands at L206
together with `disk_real_offset` and the field. Both SamOs
L205 and L206 commits compile cleanly; the audit trail in
`chapters/L205-...md` explains the deferral.

This is a generalisation of the Cluster B technique: when
an upstream commit ships a body that depends on a
later-lecture helper, hold the body for the lecture that
brings the helper in. The lecture's other changes (new
helpers in this case) still land at the lecture's commit.

---

## Cluster E: keyboard listener boot-order (G36)

L175 wires `keyboard_register_handler(NULL, window_keyboard_listener)`
inside `window_system_initialize_stage2`. The NULL keyboard
arg means "use `keyboard_default()`" which is the head of the
inserted-keyboards list. `keyboard_init` (which does the
insert) was called much later in `kernel_main`. Result:
registration silently early-returns with `-EINVARG`, the
window listener never fires, userland windows see no
keystrokes.

There is no crash. There is no log line. The only signal is
"keystrokes don't reach userland", which is invisible until
you actually have a window and a focused process to type in.

**How we found it:** When porting L196 we read the upstream
commit message ("Fixing the keyboard events on userspace") and
the diff (one line change: `keyboard_init()` hoisted up).
Working backwards through the registration order made the
cause obvious.

**Lesson:** silent registration failures are the worst kind
of bug. Always check the return value of `*_register_handler`
calls during dev. The fix is one line; the diagnosis took
us tracing back through L174's listener definition AND L175's
stage-2 registration AND L196's hoist.

---

## Cluster F: BIOS-boot rescue (G13, G43, G44, G45)

The L74 deviation (G13) declared upstream-only-UEFI explicitly:
the 32->64 transition got removed from `kernel.asm` because
UEFI does it for the loader. SamOs's `boot.asm` still hands
off in 32-bit protected mode; result is an immediate triple
fault at the first REX-prefixed instruction.

We landed the deviation per port rule and didn't run the boot
tests at the time. At the end of the port, a 169-test sweep
showed ~53 boot-trace tests failing because the kernel never
reached `print()`.

**Diagnosis chain:**

1. `bash tests64/L20-multiheap.sh` -> "missing 'Hello 64-bit!'"
2. Manual QEMU: `( sleep 3; printf 'pmemsave 0xb8000 4096 "/tmp/vga.dump"\nquit\n' ) | qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std -monitor stdio -no-reboot` -> dump shows only SeaBIOS leftovers, no kernel output.
3. Add `-d int,cpu_reset -D /tmp/qemu.log` -> register dump on triple fault shows EFER=0, IDTR=BIOS IVT, RIP in low memory. Diagnosis: kernel never reached long mode.
4. Read `kernel.asm` header comment (L74) -> "SamOs DEVIATION: this breaks the BIOS boot path".

**Repair:**

```
boot.asm:
  - Add a 64-bit code segment (L=1) to the GDT
  - After load32 + ata_lba_read, transition:
      CR4.PAE = 1
      CR3 = page tables identity-mapping 0..1GB at 0x9000
      EFER.LME = 1 (write MSR 0xC0000080)
      CR0.PG = 1
  - Far jmp CODE_SEG64:long_mode_jump_kernel
  - In long_mode_jump_kernel (64-bit): reload segs, jmp 0x100000
```

**Follow-up bug (G44):** With G43 in place the kernel triple-
faulted later because the boot.asm sector-count `mov ecx, 250`
only loads 128 KiB. `bin/kernel.bin` is ~169 KiB after PCI +
PATA + NVME + disk stream cache. The `.data` section lives
past the 250-sector boundary and never makes it into RAM. The
fix is two back-to-back LBA28 PIO reads of 200 sectors each
(LBA28 single-command cap is 255 sectors).

**Follow-up observation (G45):** Even with G43 + G44 in place
the boot-trace tests still fail because `print()` since L100
writes to the graphics framebuffer, NOT to 0xb8000. The
tokens make it into the link (verified by `grep -a 'token'
bin/kernel.bin`) but the runtime stream never lands at the
address the tests scrape. The boot-trace tests need to be
reformulated to use either (a) binary-string presence, or
(b) a real framebuffer-pixel scraper, before they will pass
at HEAD post-L100.

---

## Anatomy of the bug-hunt loop we used

Every diagnosis above followed the same five-step loop, with
QEMU `-d int,cpu_reset -D <log>` as the primary tool because
most of our faults were silent triple-faults (no panic
message, just CPU reset).

### 1. Is the build green?

The cheapest probe. About one-third of the gotchas in this
file were caught at build time:

| Symptom at build | Likely gotcha class |
|---|---|
| undefined symbol `foo` | Typo'd identifier (G17, G24, G29, G34) or hoist missing decl (G42) |
| expected `;` before `goto` | Missing semicolon in upstream source (G40) |
| no member `sector_size` in `struct disk_stream` | Forward-port hoist needed (G41) |
| statement with no effect | `-Werror=unused-value` on a preserved dead expression (G35) |
| trailing whitespace after `\` | gcc rejects line-continuation slop (G31) |

Resolution pattern:

- **Typo with link error**: static-inline shim under the typo'd
  name. Two lines of code, one comment, build is green.
- **Compile error from upstream-source slop**: minimal touch
  (strip whitespace, add semicolon). Document the upstream
  state in the chapter doc.
- **Compile error from forward-reference**: hoist the missing
  decl into the current lecture with a "SamOs early-port"
  comment. Drop the hoist when the upstream lecture lands it.

### 2. Is the kernel even alive in QEMU?

Cheapest probe: dump VGA text buffer after a few seconds.

```bash
( sleep 4; printf 'pmemsave 0xb8000 4096 "/tmp/vga.dump"\nquit\n' ) \
  | timeout 12 qemu-system-x86_64 \
      -drive file=bin/os.bin,format=raw,if=ide \
      -m 256 -accel tcg -display none -vga std \
      -monitor stdio -no-reboot >/dev/null 2>&1
od -An -v -tx1 -w1 /tmp/vga.dump | awk 'NR%2==1' | xxd -r -p | head -c 240
```

If the dump shows the SeaBIOS banner and nothing else, the
kernel never wrote to 0xb8000 (G45) OR never reached `print()`
(G13, G43, G44).

If you see kernel output up to a known checkpoint and then
nothing, you have a binary-search target.

### 3. Where is the CPU right now?

Once you know whether the kernel is alive (or stuck in a
triple-fault reset loop), enable QEMU's CPU trace:

```bash
qemu-system-x86_64 \
    -drive file=bin/os.bin,format=raw,if=ide \
    -m 256 -accel tcg -display none -vga std \
    -monitor stdio -no-reboot \
    -d int,cpu_reset -D /tmp/qemu.log
```

In the log, every exception dumps registers + IDTR + CR0/3/4
+ EFER. The classic post-G13 signature:

```
EAX=00106700 ... CR0=00000011 CR2=00000000 CR3=00101000
CR4=00000000 EFER=0000000000000000
check_exception old: 0x8 new 0xd
Triple fault
```

- `CR0=0x11` = PE on, PG OFF -> not in paging mode.
- `EFER=0` -> not in long mode despite CS being CS64.
- `IDT=00000000 000003ff` -> BIOS IVT, not the kernel's IDT.
- The kernel never reached `kernel_main`.

### 4. What does the offending instruction decode to?

Match the EIP/RIP in the log against `bin/kernel.bin`:

```bash
# Find the function whose address contains the fault RIP
nm bin/kernel.bin 2>/dev/null | sort -k1,1 | awk -v ip=$RIP '$1 <= ip { fn=$3 } END { print fn }'
```

For our triple faults the RIP is usually low memory (boot
sector / BIOS shadow) because the kernel never moved past
boot. Once the kernel actually boots, you start seeing RIPs
in kernel.bin's address range.

### 5. Does the fix break anything?

Three of the gotchas above (G21 SamOs deviation, G36 keyboard
boot order, G43+G44+G45 boot rescue) interact with each other
or with later lectures.

Re-run `bash tests64/run-all.sh` after any kernel patch.
Snapshot-pin tests will fail at HEAD by design (see
`running-tests.md`), but a new failure (i.e. a test that was
passing before your patch) is a genuine regression worth
chasing.

---

## Lessons that paid back

- **Trust upstream's commit boundary.** When a lecture's
  source won't compile, the next lecture usually fixes it.
  Adding a one-line shim or a `(void)` cast at port time is
  almost always cleaner than reworking the entire commit.
- **Static-inline shims for preserved typos are free.** Two
  lines of code, one comment, the audit trail is intact, the
  build is green. Cluster C (NVME) retired three gotchas
  this way.
- **`if (!driver)` fallback paths for vtable-NULL gaps.**
  When a lecture introduces a vtable call before a later
  lecture wires the driver, keep the pre-vtable path alive
  inside an `if (!driver)` block. The path drops out the
  moment the driver registers; no source change required.
- **Hoist forward-references with a comment.** When an
  upstream lecture's body references a helper defined in a
  later lecture, land the helper early with a "SamOs early-
  port" comment. The audit trail is preserved; the build is
  green per-commit.
- **`git status` after every `Edit`.** WSL pathing + the
  Read-before-Write guard sometimes drops edits silently.
  G15 (L145 helper decls never reached `window.h`) was
  caused by exactly this. A quick `git status` catches the
  file that didn't get written.
- **Silent registration failures are the worst kind of bug.**
  G36 took us tracing back through three lectures because
  `keyboard_register_handler` returned `-EINVARG` and
  nobody logged it. Defensive logging of "I returned an
  error" status codes during dev would have caught it
  immediately.
- **The CPU-reset trace is your friend.** Adding `-d
  int,cpu_reset -D <log>` to QEMU costs nothing and dumps
  registers + IDTR + CR0/3/4 + EFER on every triple fault.
  G13 (long-mode never enabled) was diagnosed in 30 seconds
  this way.

---

## Quick reference - common SamOs-only deviation patterns

| Pattern | Where it shows up | Why |
|---|---|---|
| Static-inline shim under typo'd name | `pci.c`, `nvme.c`, `disk.c` | Preserve upstream identifier verbatim, keep build green |
| Explicit `(void)` cast on dead expression | `nvme.c` (G35) | Same; `-Werror=unused-value` requires it |
| `if (!driver)` fallback path | `disk_read_block`, `disk_create_new` (G21) | Keep boot alive when an upstream lecture introduces a vtable call before a later lecture wires a driver |
| "SamOs early-port" hoist | `disk_private_data_driver` (G29), `disk_real_offset` (G42), struct field (G41) | An upstream lecture's body references a helper defined in a later lecture; land the helper early |
| Comment-tagged "upstream typo preserved" | Throughout | Audit trail; `grep -rn 'upstream typo' src/` lists every divergence |

To audit every deviation in the tree:

```bash
grep -rn '[Uu]pstream typo\|sic\|verbatim per the project\|SamOs DEVIATION' src/
```

The match count tells you the number of preserved-upstream
markers currently live. After every lecture port, this number
should monotonically increase (new markers added) and decrease
only when a later lecture fixes the typo (markers retired).

---

## The post-L208 cluster

The post-L208 work (the regression sweep + boot rescue) found
three real port-quality issues:

- **G43** L74 BIOS deviation re-discovered at runtime
- **G44** kernel.bin grew past the 250-sector load cap
- **G45** post-L100 `print()` doesn't write 0xb8000 anymore

All three only surface when you actually boot the kernel under
QEMU `-hda`. The per-lecture tests under `tests64/` are source-
level audits that don't boot the kernel, so they miss G43-G45
by construction. The boot-trace tests (~53 of them) WOULD have
caught them, but those tests have been failing since L74
introduced G13 and nobody re-ran them until the end of the
port.

**Lesson:** when an upstream lecture intentionally breaks the
BIOS boot path, mark the boot-trace tests as expected-to-fail
at that lecture's commit so a regression sweep at HEAD doesn't
mask the deviation. The right behaviour is "the sweep tells
you exactly what's expected-broken vs newly-broken", not
"the sweep produces a wall of failures that look identical".

---

## See also

- `docs/64bit/chapters/Lxx-*.md` - one chapter doc per lecture
  recording every deviation in the lecture's commit
- `docs/64bit/running-tests.md` - test runner, snapshot mode,
  QEMU monitor reference
- `docs/64bit/end-to-end-feature-tests.md` - manual feature
  smoke tests once the kernel boots
- `docs/debugging-history.md` - Module 1 (32-bit) debugging
  history (G01-G12)
- `docs/gotchas/` - one file per Module 1 gotcha, full
  diagnosis + fix per file
- Upstream reference repo:
  `~/work/PeachOS64BitModuleTwo/PeachOS64Bit/` -- every
  preserved-typo gotcha has an upstream commit you can
  `git show` for the original form
