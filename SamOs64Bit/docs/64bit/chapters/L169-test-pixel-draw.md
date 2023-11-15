# Lecture 169 - testing pixel drawing in userspace

Upstream: PeachOS64BitCourse 644e7d7.

L169 is entirely userland: upstream's `blank.c` calls
`peachos_window_get_graphics`, `peachos_graphic_pixels_get`,
loops over the pixel buffer painting blue, then calls
`peachos_window_redraw`. The stdlib gets the
`peachos_window_redraw` asm stub.

## SamOs kernel-side delta

None. SamOs userland is not tracking upstream stdlib commits in
this branch; the L165/L166/L167 kernel-side syscalls are in
place and a future userland pass will exercise them.

## Test

The test asserts the kernel-side syscall ABI we need (commands
19, 20, and 21) is still wired correctly so a future userland
walkthrough can drive them.

## BIOS test status

Source + link. Build links.
