# Lecture 105 - userland fopen (module 2 starts)

L105 opens module 2 by exposing the kernel's existing
`fopen`/`fclose`/`fread`/`fseek`/`fstat` to user mode. This
commit lands the first hop: `fopen`, syscall command 10.

## Kernel-side

`src/isr80h/file.{c,h}` is new. `isr80h_command10_fopen`:

1. Reads the userland stack item 0 (`filename`) and stack item
   1 (`mode`), both task-virtual.
2. Translates each through
   `task_virtual_address_to_physical(task_current(), ...)`.
3. Calls `process_fopen(task_current()->process, filename, mode)`
   and returns the resulting fd as the syscall result.

`isr80h.h` gets `SYSTEM_COMMAND10_FOPEN`; `isr80h.c` registers
the handler; the Makefile compiles `isr80h/file.o`.

`src/task/process.h` gains `struct process_file_handle`
(fd + file_path + mode) and a `struct vector* file_handles` slot
on `struct process`. `process_init` allocates the vector;
`process_terminate` calls a new
`process_close_file_handles` that walks the vector, calls
`fclose` on each fd, and frees the records.

`process_fopen(process, path, mode)` calls the kernel `fopen`,
records the descriptor on the vector, and returns the fd. On
non-positive fd it returns `-EIO` (and `fclose(fd)` is invoked
in the cleanup block; if `fd <= 0`, fclose-on-error is a no-op
on negative descriptors).

## Userland-side

`programs/stdlib/src/samos.asm` adds `samos_fopen`: pushes
mode then filename onto the trampoline stack, sets `rax = 10`,
`int 0x80`, restores rsp. The push order matches command 10's
`task_get_stack_item(., 0)` -> filename,
`task_get_stack_item(., 1)` -> mode.

`programs/stdlib/src/file.{c,h}` is the public C wrapper:
`int fopen(const char* filename, const char* mode)` returns
`(int)samos_fopen(filename, mode)`.

`samos.h` exposes `samos_fopen` for callers that prefer the
raw trampoline.

## Test program

`programs/blank/blank.c` is rewritten. The L66 "print hello
forever" loop is replaced by:

```c
int fd = fopen("@:/blank.elf", "r");
if(fd > 0){
    printf("File blank.elf opened\n");
}else{
    printf("File blank.elf opened failed\n");
}
while(1){ }
```

Visible effect on a working boot: blank.elf prints one of the
two messages and stops. The "opened" branch confirms command 10
round-trip + process_fopen + kernel fopen succeeded.

## Upstream bug preserved

`process_fopen` allocates the handle with:

```c
struct process_file_handle* handle =
    kzalloc(sizeof(struct process_file_handler*));
```

Two mistakes verbatim from upstream:

1. The struct name is `process_file_handle`; this typed cast
   uses `process_file_handler` (extra `r`).
2. `sizeof(struct ... *)` is the size of a POINTER (8 bytes on
   x86_64), not the struct.

Result: the allocation is 8 bytes; `handle->fd` (first 8 bytes,
because of zero offset + 4-byte int + 4-byte pad) fits;
`file_path` and `mode` writes overflow into the next heap
block. The kernel allocator's bitmap accounting still says the
handle owns one block. We preserve the bug because the
upstream diff against L105+L108+L109 (where memory management
gets reworked) stays linear.

Documented inline in `process.c` with an "Upstream bug
preserved" comment.

## BIOS test status

Source-only. The test asserts every new file exists, the
syscall enum/register/handler chain is wired, the userland
trampoline + wrapper exist, the blank.c smoke is in place,
the Makefiles include the new objects, and the build links.
