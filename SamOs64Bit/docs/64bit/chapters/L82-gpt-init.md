# Lecture 82 - Compile GPT, call gpt_init

L77 added `src/disk/gpt.[ch]` but never wired them into the build
(they sat behind an unreferenced source file). L82 is the small
plumbing commit that drops `disk/gpt.o` into `FILES`, adds its
compile rule, and inserts the `gpt_init()` call into `kernel_main`
right after `disk_search_and_init()`.

## Ordering matters

`gpt_init` walks every disk returned by `disk_get` and reads LBA 1
to look for the `"EFI PART"` signature. The walk depends on
`disk_search_and_init` having already enumerated the disks into the
disk vector. The new line therefore goes immediately AFTER
`disk_search_and_init()`, NOT before.

```
fs_init();
disk_search_and_init();
gpt_init();            // L82
```

The opposite ordering would always find zero disks and silently
no-op.

## What gpt_init does (recap of L77)

For each disk:

1. Read sector 1 into a stack buffer with `disk_read_block`.
2. If the first 8 bytes are not `"EFI PART"`, skip the disk.
3. Otherwise call `disk_create_new` for every entry in the
   partition array, passing the partition's starting and ending
   LBAs. Each partition becomes its own virtual disk with bounds
   checking; the parent disk keeps its identity for raw access.

The "primary filesystem disk" picked by `disk_search_and_init`
(L78) can therefore be a GPT partition rather than the whole
physical drive, which is what the UEFI flow needs (the ESP and the
data partition both live inside the same GPT).

## BIOS test status

This is a build-system + one-line wiring change. The L74 UEFI
pivot left BIOS unable to boot end-to-end, so the test
source-checks the Makefile and the kernel call ordering, then
confirms the build still links. Once UEFI is wired up end-to-end
the runtime smoke will hit gpt_init naturally.
