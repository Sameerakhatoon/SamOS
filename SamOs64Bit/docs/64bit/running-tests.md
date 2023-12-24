# SamOs Module 2 - running the tests

There are three layers of testing in this branch:

1. **Per-lecture tests** under `tests64/Lxx-name.sh`. One per
   committed lecture. Each one validates the kernel state at
   the moment that lecture's commit landed.
2. **Build smoke** (`bash build.sh`). The toolchain is GCC
   under `-Werror -Wall`, so a successful build is a strong
   signal on its own.
3. **End-to-end feature smoke** (see `end-to-end-feature-tests.md`)
   for runtime sanity-checks under QEMU.

This doc covers layers 1 and 2. Use it together with
`debugging-history.md` when something fails.

---

## TL;DR

```bash
cd SamOs64Bit

# Single lecture
bash tests64/L153-userland-pointers.sh

# All lectures
bash tests64/run-all.sh
```

A passing test prints `PASS: Lxx ...`. A failing test prints
`FAIL: ...` to stderr and exits non-zero.

---

## What each test does

Every `tests64/Lxx-name.sh` is a small bash script that:

1. Compiles the tree via `bash build.sh`.
2. greps the source files that the lecture touched for the
   identifiers the lecture introduced (declarations,
   definitions, syscall enum values, vtable entries, etc.).
3. Optionally asserts the kernel binary embeds expected
   strings via `grep -a bin/kernel.bin '...'`.
4. Echoes `PASS: ...` on success.

Test scripts are short and self-contained - open one to see
exactly what it's checking. The header comment of every test
names the upstream commit it ports.

---

## Running one test

```bash
cd SamOs64Bit
bash tests64/L184-disk-driver.sh
```

If you're hunting a regression:

```bash
# Re-run with all output visible
bash -x tests64/L184-disk-driver.sh
```

`set -x` shows every command. The first command after the
`PASS:` echo that returns non-zero is the assertion that
failed.

---

## Running all tests

`tests64/run-all.sh` walks every `Lxx*.sh` in the directory:

```bash
bash tests64/run-all.sh
```

Output:

```
N passed, M failed
FAIL: L67-irq-c-api.sh
FAIL: L141-graphics-click-handlers.sh
...
```

When `M > 0`, the failing test names go to stdout in
alphabetic order so you can diff against the previous sweep.

The sweep is sequential - each test does its own `build.sh`,
but make is incremental so the second run onward is
just `nasm` + `ld` + `mformat` (~ 2 seconds per test). A
full clean sweep of all 169 lectures takes about 5-7 minutes.

---

## Interpreting failures

The same failure at HEAD can mean any of:

| Failure shape | What it usually is |
|---|---|
| `FAIL: source string '...' not present` | Real regression - a recent commit removed code another test depends on. |
| `FAIL: ... not preserved` on a pinned typo | **Expected at HEAD.** The test was written against a snapshot where the upstream typo was deliberately preserved verbatim, but a later lecture fixed the typo. See `debugging-history.md` for the typo registry. |
| `FAIL: ... registered prematurely` | Same pattern - a snapshot-pin test for a multi-step lecture series. Pass at its own commit. |
| `FAIL: build.sh failed` | Compile or link error in the current tree. Run `bash build.sh` directly for the error message. |
| `qemu-system-x86_64: ... terminating on signal 15` | The boot-trace test timed out because QEMU never reached its monitor prompt. Most often this means the kernel triple-faulted. See the QEMU monitor section. |

Snapshot-pin tests are a feature of the per-lecture
porting workflow: they validate intermediate state that
matters for the audit trail, even though a later commit
intentionally moves past that state. Either:

- Run the test at its own commit:
  ```bash
  git checkout <commit-of-Lxx> -- tests64/Lxx-name.sh src/...
  bash tests64/Lxx-name.sh
  git checkout HEAD -- tests64/Lxx-name.sh src/...
  ```
- Or accept the failure as expected; the test still serves
  its archival purpose.

A relaxation pattern that lets the same test pass at both
its own commit and HEAD:

```bash
# Accept either the pinned-typo state OR the post-fix state.
grep -qE 'if\(disk_driver_register(ed)?\(driver\)\)' src/disk/driver.c \
    || fail "uniqueness check missing"
```

---

## Adding a test for a new lecture

Template:

```bash
#!/usr/bin/env bash
# tests64/Lxx-short-name.sh
#
# Lecture xx - what this lecture added.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# 1. Source-level assertions: files exist, decls + bodies present.
[ -f "$ROOT/src/path/to/new.h" ] || fail "new.h missing"
grep -q 'my_new_symbol' "$ROOT/src/path/to/new.h" \
    || fail "new.h: my_new_symbol decl missing"
grep -q 'my_new_symbol' "$ROOT/src/path/to/new.c" \
    || fail "new.c: my_new_symbol body missing"

# 2. Makefile wiring (if the lecture adds a new .c).
grep -q 'build/path/to/new.o' "$ROOT/Makefile" \
    || fail "Makefile: new.o missing"

# 3. Build must succeed.
( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: Lxx short-name"
```

Make it executable: `chmod +x tests64/Lxx-short-name.sh`.

---

## Running tests at a specific commit (snapshot mode)

If you want to verify that a lecture's test passed at the
commit it was written for (instead of at HEAD):

```bash
# Checkout the lecture's tree into a temp worktree
git worktree add /tmp/samos-Lxx <commit-of-Lxx>
cd /tmp/samos-Lxx/SamOs64Bit
bash tests64/Lxx-name.sh
cd -
git worktree remove /tmp/samos-Lxx
```

The per-commit verifier `tests64/verify-per-commit.sh` (if
present in the tree) automates this for an entire range:

```bash
bash tests64/verify-per-commit.sh <start-commit> <end-commit>
```

It checks out each lecture's commit, runs its test, and
collects pass/fail. This is the strict reading of "every
lecture's test passed at its commit", which is the
invariant the per-lecture porting workflow maintains.

---

## QEMU monitor - the swiss army knife

If a test boots the kernel and you suspect a triple fault:

```bash
qemu-system-x86_64 \
    -drive file=bin/os.bin,format=raw,if=ide \
    -m 256 -accel tcg -display none -vga std \
    -monitor stdio -no-reboot \
    -d int,cpu_reset -D /tmp/qemu.log
```

Useful in-monitor commands once it's up:

| Command | Use |
|---|---|
| `info status` | Is the CPU running, halted, or shut down? |
| `info registers` | CR0/CR3/CR4/EFER + RIP/RSP/segment regs. EFER bit 8 = long mode active. |
| `pmemsave <addr> <size> "<file>"` | Dump physical memory (e.g. `pmemsave 0xb8000 4096 "/tmp/vga.dump"` for legacy VGA text). |
| `info mtree` | The memory tree QEMU advertised to the guest. |
| `quit` | Stop QEMU cleanly. |

The `-d int,cpu_reset` log file gets a full register dump on
every exception and on every CPU reset. Grep for
`check_exception` to find the moment things went wrong, then
`grep` upward for the last successful instruction fetch.

---

## CI-style invocations

For automation (or to grep `FAIL:` lines into a follow-up
script):

```bash
# Pass/fail summary only
bash tests64/run-all.sh | head -1

# List of failures (one per line)
bash tests64/run-all.sh | grep '^FAIL:'

# Exit non-zero if any failure
bash tests64/run-all.sh | tee /tmp/sweep.log
grep -q '^FAIL:' /tmp/sweep.log && exit 1 || exit 0
```
