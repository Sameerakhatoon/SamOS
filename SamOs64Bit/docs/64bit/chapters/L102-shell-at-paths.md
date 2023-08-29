# Lecture 102 - shell invokes via "@:/" instead of "0:/"

L102 is a one-line change to `isr80h/process.c`. The
"invoke system command" syscall (command 7 in upstream) used to
build the target path with a hardcoded `0:/`:

```c
strcpy(path, "0:/");
strncpy(path + 3, program_name, sizeof(path) - 3);
```

L102 swaps that for `@:/`, which the L81 path parser resolves
to the primary filesystem disk. The shell can now exec a child
program without knowing which physical disk the kernel booted
from.

## SamOs scope

Our `isr80h/process.c` had the same hardcoded `0:/` in TWO
places: the explicit-path syscall (command 6 in our numbering)
AND the "invoke system command" path. Upstream's L81 work
already touched the L81 pparser side; the SamOs L81 port did the
same. The `0:/` literals in the syscall path slipped through
both commits. L102 fixes both call sites at once so the diff
mirrors upstream's intent.

## What this unlocks

The shell (`@:/SHELL.ELF`) can use the `system()` libc-style
helper to launch sibling programs without staging the absolute
disk number. As we add user-mode tools, every binary referenced
by name will be looked up on the primary FS disk.

## BIOS test status

Source-only. Test verifies no `"0:/"` literals remain in
`isr80h/process.c`, both `@:/` literals are present, and the
build links.
