# SamOs - post-Ch-150 strengthening session notes

This document captures everything that landed AFTER the book arc
completed at Ch 150. The book itself stops there; what follows is
the work that turned the book's "minimum viable kernel" into
something with end-to-end behavioural test coverage, three
additional kernel bugs caught and fixed, and a documentation pass
that links everything to its commit and its regression test.

## Why this matters

The book ships symbol-presence tests ("does the function exist?")
and integration smoke tests ("does the kernel print Testing!/Abc!?").
Neither catches a logic bug INSIDE one of the functions if its
output looks plausible.

For example: `task_list_remove` (G10) ships in the book missing the
back-link patch. The book's two-task demo (`Testing!` and `Abc!`,
never any deaths) doesn't exercise mid-list removal, so the bug is
invisible. As soon as you add a third task that dies, the schedule
list rots.

This session added behavioural round-trip tests for every post-Ch-100
syscall and feature, which in turn surfaced three latent bugs the
book ships intact.

## What landed, in commit order

| Commit (short SHA) | What |
|---|---|
| `5c7d721` | **G10**: `task_list_remove` next->prev symmetry. Doc: `docs/gotchas/G10-task-list-remove-prev-symmetry.md` |
| `b85c447` | **Extras**: VGA scroll helper, COM1 serial mirror, `tests/_lib.sh` marker-sync harness. Doc: `docs/testing-guide.md` |
| `575b164` | **Tests 45/46/47**: exception handler (Ch 147), exit syscall (Ch 148), terminal backspace (Ch 118). Test files in `tests/`. Doc: `docs/feature-walkthrough.md` |
| `515e6e7` | **Tests 48/49**: malloc/free (Ch 130/131) + putchar (Ch 109). |
| `43a024d` | **G11** + **tests 50/51/52**: `task_switch` updates `current_process` too; caps lock (Ch 149), process_load_start (Ch 138), invoke_system_command (Ch 145). Doc: `docs/gotchas/G11-task-switch-current-process.md` |
| `8cf499d` | **Tests 53/54/55**: itoa (Ch 133), printf (Ch 135), terminal_readline (Ch 136). Plus expanded symbol audits in tests 43/44. |
| `aec3925` | **Test 56**: shell-boots (Ch 137). |
| `3380eec` | **Tests 08/09**: widened EIP range to user code beyond `_start`. |
| `68c22f6` | **G12** + **tests 57/58**: `.bss`-only PHDR aliasing fix in `process_map_elf`; behavioural BSS test (Ch 132); build-flag-gated panic test (Ch 82). Doc: `docs/gotchas/G12-elf-bss-aliasing.md` |
| `01aad59` | **Tests 11/12/39**: smoke → real round-trip assertions. |
| `2590240` | **Docs**: `debugging-history.md`, `testing-guide.md`, `feature-walkthrough.md`, `gdb-guide.md`. |

## Why this session was scoped this way

Three constraints drove the order:

1. **One bug per session.** G10, G11, G12 each gated a different
   subset of behavioural tests. Until G10 landed, mid-list deaths
   silently broke the scheduler. Until G11, keystrokes never reached
   the consumer task. Until G12, .bss writes corrupted the ELF
   header. So each gotcha had to land BEFORE the test that depended
   on it.

2. **Suite-level stability.** Adding a behavioural test that flakes
   under suite load (e.g. the 1/15 keypress race for caps lock) is
   worse than no test at all, because the failure mode is "you
   stopped trusting the suite, not the kernel." Every test was
   tuned (burst sizes 80 → 200 → 300, initial sleeps 3 s → 8 s,
   timeouts 60 s → 90 s) until it passed in 3 consecutive 48-test
   suite runs.

3. **Doc parity with commits.** The book ships chapters but no
   per-feature regression test references. This session's docs
   (`debugging-history.md`, `testing-guide.md`,
   `feature-walkthrough.md`, `gdb-guide.md`) map every chapter,
   every gotcha, and every test to its commit.

## Files added in this session

### Kernel changes (3 bug fixes)

- `src/task/task.c`: G10 patch (next->prev) + G11 patch (process_switch in task_switch)
- `src/task/process.c`: G12 patch (kzalloc'd buffer for .bss-only PHDRs)
- `src/kernel.c`: `terminal_scroll`, `serial_init`, `serial_putchar`, mirror routing in `terminal_writechar`, `KERNEL_TEST_PANIC` guard, 15-task demo loading
- `src/config.h`: `SAMOS_MAX_PROCESSES` 12 → 16
- `Makefile`, `build.sh`: `EXTRA_CFLAGS` plumbing for test 58's panic-build variant

### User program changes

- `programs/blank/blank.c`: argv-dispatch ladder for CRASH/EXIT/BS/MF/PC/KEY/LD/SH/IT/PF/RL/BSS tasks

### New tests

- `tests/45..58.sh` (14 new tests)
- `tests/_lib.sh` (marker-sync helper)
- `tests/lib.sh` already existed and is unchanged; new helper is named `_lib.sh` so tests can switch incrementally

### Tests rewritten this session

- `tests/05/08/09/14/35.sh`: switched to `run_kernel_inspect` marker-sync
- `tests/10/13/16..34/37/40.sh`: switched to `run_kernel_capture` serial mirror
- `tests/11/12.sh`: smoke → real round-trip K:a/K:b/K:c assertions
- `tests/39.sh`: smoke → exact five-marker assertion
- `tests/41.sh`: pmemsave VGA → serial mirror

### New gotcha docs

- `docs/gotchas/G10-task-list-remove-prev-symmetry.md`
- `docs/gotchas/G11-task-switch-current-process.md`
- `docs/gotchas/G12-elf-bss-aliasing.md`

### Existing gotcha docs enriched

All of `docs/gotchas/G03..G09.md` got the same structured header
block (`Surfaced during / Fix / Regression test`) that `G01..G02`
and the new `G10..G12` already had.

### New top-level docs

- `docs/debugging-history.md`: the bug-hunt narrative
- `docs/testing-guide.md`: how to run the suite, what each test asserts
- `docs/feature-walkthrough.md`: post-`qemu-system-x86_64 -hda bin/os.bin` manual recipes
- `docs/gdb-guide.md`: extended GDB workflows (the cheatsheet in `docs/part6-gdb.md` stays)

## Final suite state

```
$ bash tests/run.sh
[..]
passed: 50   failed: 0
```

50 numbered tests, 50 passing, stable across 3 consecutive suite runs.

## Coverage status

Every implementation chapter Ch 22 onwards has either a direct
behavioural test or sits on the critical path of one. The complete
mapping is in `docs/testing-guide.md`.

The only chapters without a test are:

- Real-mode Ch 6/8/12/13/15: obsoleted by the Ch 22 PM transition,
  not in the final kernel image
- Procedure Ch 9/23/33/86: GDB walkthroughs, not runnable headless
- Prose chapters: by definition no shippable artifact

Every one of `G01..G12` has a regression-test sentinel.

## See also

- `docs/debugging-history.md` - how each gotcha was diagnosed
- `docs/testing-guide.md` - the suite runner
- `docs/feature-walkthrough.md` - manual QEMU recipes
- `docs/gdb-guide.md` - debugger workflows
- `docs/part6-gdb.md` - the book's own GDB cheatsheet
