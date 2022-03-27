# Archived per-lecture grep tests (L153-L208)

These 169 scripts were the original Module 2 test layer. Each
script greps the source tree for a fact established by one
lecture (e.g. "L165 added kheap_init", "L182 wired the keyboard
classic driver into the keyboard chain"). They run with no boot
and no QEMU, just `grep` against `src/`.

They are kept here as a historical index of which lecture
introduced which feature. They are NOT part of the active test
sweep.

## Active testing lives in `tests64/e2e/`

The supported test layer is `tests64/e2e/NN-feature.sh`, which:

- boots the kernel end to end under UEFI + QEMU
- exercises the actual runtime feature
- asserts the result via the bootmarker region at 0x6000

That matches the Module 1 `tests/` flat feature-named pattern
and gives runtime confidence the source-level grep tests cannot.

## Why archive instead of delete

The L-numbered scripts double as a curriculum index: if you
ever wonder "which lecture added the multiheap?" the answer is
in `L165-kheap-init.sh`. Searching this folder is faster than
walking `git log`.
