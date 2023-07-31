# Lecture 86 - injecting the framebuffer into the kernel

L86 is the first commit of the graphics arc. It is entirely UEFI-
side: the kernel does not parse anything new yet. The UEFI app
locates the EFI Graphics Output Protocol, captures the
framebuffer base/pitch/resolution, paints the screen green as a
visible sanity check, and then hands those values to the kernel
through SysV-ABI registers right before jumping.

## What changes

`SamOs.c`:

- New `#include <Protocol/GraphicsOutput.h>`.
- New helper `GetFrameBufferInfo`. It calls
  `gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, ...)` and
  then `QueryMode` to confirm the current mode, then prints the
  base / size / resolution / pitch to the UEFI console.
- In `UefiMain`, right after the kernel is copied to
  `SAMOS_KERNEL_LOCATION` (0x100000):
  - Call `GetFrameBufferInfo`.
  - Walk the framebuffer and set every pixel to green
    `{R,G,B,A}={0x00, 0xff, 0x00, 0x00}`.
  - Call `gBS->ExitBootServices`.
  - Inline-asm-load the framebuffer params into the SysV
    parameter regs:
    - `rdi` = framebuffer base
    - `rsi` = pixels per scan line (stride in pixels)
    - `rdx` = horizontal resolution
    - `rcx` = vertical resolution
  - `jmp *KernelBase`.

`SamOs.inf`:

- Add `gEfiGraphicsOutputProtocolGuid` to `[Protocols]`.

The kernel side is unchanged in this commit. The kernel.asm
`_start` will save those four registers into bss slots in a
later lecture; for now the values stay live in the registers
through the long-mode pivot. (Long mode preserves r/sx/dx/cx
across the `jmp` since we did not touch them in the L74 stub.)

## Why pixels per scan line, not horizontal resolution

`PixelsPerScanLine` is the stride; it can be larger than
`HorizontalResolution` on platforms that align rows for
performance. Using horizontal resolution as the stride works on
many machines (especially QEMU's std emulator) but silently
shears the picture on real hardware. The upstream commit gets
this right; we copy the pattern verbatim.

## The green-paint smoke test

Painting the screen green is purely diagnostic. If the kernel
later crashes, the green field tells you UEFI graphics was alive
just before `ExitBootServices`. The cost is one full-screen
write; for a 1920x1080 framebuffer at 4 bytes/pixel that is
~8 MiB and is invisible to QEMU.

## BIOS test status

Same story as L74-L84: BIOS boot has been broken since the L74
pivot. This commit does not affect that. The test source-checks
the C file (the includes, the helper, the four register loads,
the green paint), the .inf file (Protocols entry), and confirms
the kernel still links.

The actual end-to-end smoke (boot SamOs.efi under OVMF, see green
screen, jump to kernel, kernel does NOT yet consume rdi/rsi/rdx/
rcx so we expect a black-after-paint) requires an EDK2
environment that we do not have wired in here. Once we do, the
runtime check is "green flash visible during boot".
