# Lecture 14 - restructuring build files

**Source commit (PeachOS64BitCourse):** `7f146da`
**SamOs commit:** L14 on `module1-64bit` branch
**Regression test:** `tests64/L14-clean-wipes-stale-o.sh`

## Why this chapter exists

L7..L13 grew the Makefile linearly: one explicit rule per .c / .asm
file, plus an entry in `FILES`. The list is still small enough to
read but the `clean` target was lagging behind. PeachOS64's L14
tightens that up in two ways:

1. `clean` learns to wipe **every** .o under `build/`, not just the
   ones listed in `FILES`. This guards against stale objects from
   dropped or renamed modules silently linking back in.
2. `build.sh` calls `mkdir -p` for every build subdirectory so a
   fresh checkout builds without first hand-creating them.

Upstream also drops the `user_programs_clean` dependency from
`clean` because the user-programs directory does not exist yet. We
never had that dependency in the first place (no user programs in
SamOs's 64-bit branch yet), so that part is a no-op for us.

Upstream also commits empty `build/<dir>/.keep` files so the empty
directory layout survives a fresh git clone. SamOs does not need
those: our `build.sh` runs `mkdir -p` for every layer it cares
about, and the tree is recreated on every build.

## Why a stale-.o safety net matters

Our link line is

```
x86_64-elf-ld -g -relocatable $(FILES) -o ./build/kernelfull.o
```

`-relocatable` (`-r`) pulls together exactly the files listed in
`$(FILES)`. If a file is in `$(FILES)` but no longer compiled (for
example we removed the rule for it but forgot the FILES entry),
the link breaks loudly - that case is safe.

The dangerous case is the reverse: a file is removed from
`$(FILES)` and its rule, but the .o from a previous build still
exists. The next time we add a similarly-named file back, or any
script does `find build -name "*.o"`, the stale object can be
picked up. Worse: the kernel symbol table in `kernelfull.o` lives
under `build/`, so any garbage .o under build/ contributes to
disk noise and is visible to debug tooling.

L14's `find build -type f -name "*.o" -delete` makes `make clean`
idempotent against this class of bug.

## How the change lands in our tree

| File | Change |
|---|---|
| `Makefile` | `clean` target gains `find build -type f -name "*.o" -print -delete 2>/dev/null || true`. The `\|\| true` guard makes a missing `build/` dir non-fatal. |

Nothing else needs porting. SamOs's `build.sh` already does the
`mkdir -p` work (lines 11 of build.sh, present since L13). SamOs
has no user programs, so the `user_programs_clean` dependency
removal is moot.

## How we verified

`tests64/L14-clean-wipes-stale-o.sh` plants a forged
`build/dummy_module/forged.o` and asserts `make clean` removes it.

Observed: the forged file is gone after `make clean`. Before L14
this test would have failed (clean only removed files listed in
FILES).

We also re-run `tests64/L13-c-paging.sh` after the Makefile edit
to confirm the L13 regression still passes - it does.

## Forward look

L15 abstracts kernel paging functionality into a separate kernel
descriptor so the rest of the kernel does not have to touch the
PML4 directly. L16 lets the kernel heap grow dynamically.

The Makefile will keep growing with one-rule-per-file until much
later in the course - PeachOS64 does not introduce a pattern rule
(`%.o: %.c`) for the C files. We keep parity for now.
