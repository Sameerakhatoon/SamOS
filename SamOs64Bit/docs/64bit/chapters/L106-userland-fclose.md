# Lecture 106 - userland fclose

L106 adds the matching fclose to the L105 fopen.

## Kernel-side

- `SYSTEM_COMMAND11_FCLOSE` joins the enum.
- `isr80h_command11_fclose` reads stack item 0 as an `int64_t`
  and calls `process_fclose(task_current()->process, fd)`.
- `process_fclose` looks the descriptor up in the per-process
  `file_handles` vector, calls the kernel `fclose`, pops the
  handle via `vector_pop_element`, and `kfree`s it. Returns
  `-EINVARG` when no handle matches.

## Userland-side

- `samos.asm` gains `samos_fclose`.
- `samos.h` and `stdlib/file.h` declare the C wrappers.
- `stdlib/file.c::fclose(fd)` casts to `size_t` and calls
  `samos_fclose`.
- `programs/blank/blank.c` calls `fclose(fd)` after the success
  print.

## Upstream bug preserved

The upstream `peachos_fclose` trampoline pushes the fd onto the
stack, then has `add rsp, 8` and `ret` WITHOUT issuing
`int 0x80` in between. The syscall is never actually executed;
the kernel-side handler never runs and the per-process file
record stays around until process termination wipes the vector.

Our `samos_fclose` ports the upstream form verbatim. The fix is
mechanical (insert `int 0x80` between the push and the rsp
restore) and lands in a follow-up commit; this lecture mirrors
the upstream commit shape so the per-lecture diff stays clean.

Documented inline in `samos.asm` with an explicit
"Upstream bug preserved" comment.

## Effect on the boot test

`programs/blank/blank.c` calls `fclose(fd)` on the success
branch. With the upstream bug intact, control still reaches the
while(1) loop after the print; the kernel-side close just never
fires. The user-facing observation (text printed, program
halts) is identical to L105.

## BIOS test status

Source-only. Test asserts every kernel + userland symbol exists,
the blank smoke calls fclose, and the build links.
