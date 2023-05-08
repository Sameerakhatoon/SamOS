# Lecture 41 - task.asm 64-bit + selector macro fix

**Source commit (PeachOS64BitCourse):** `1635d5f`
**SamOs commit:** L41 on `module1-64bit` branch
**Regression test:** `tests64/L41-task-asm-64bit.sh`

## Why this chapter exists

L40 ported task.c / task.h. The three asm helpers it depends
on (`task_return`, `restore_general_purpose_registers`,
`user_registers`) still need to be rewritten for 64-bit. L41
does that.

It's also when the upstream macro-naming bug from L40 becomes
visible enough to fix in SamOs.

## The macro-naming fix

L39's GDT lays out:
- 0x28: 64-bit user code segment (L=1, access=0xFA)
- 0x30: user data segment (access=0xF2)

With RPL=3 OR'd in for ring-3 selectors:
- 0x2B = user code + RPL=3
- 0x33 = user data + RPL=3

Upstream's L40 defines:
```c
#define USER_DATA_SEGMENT 0x2B   // <-- 0x2B is CODE, not data
#define USER_CODE_SEGMENT 0x33   // <-- 0x33 is DATA, not code
```

The names are inverted relative to the underlying selectors.
upstream's task.c then does:

```c
task->registers.cs = USER_CODE_SEGMENT;  // = 0x33 = data seg
task->registers.ss = USER_DATA_SEGMENT;  // = 0x2B = code seg
```

So the task's CS gets a data-seg selector and SS gets a code-
seg selector. The kernel "works" under QEMU because long
mode's segment validation is loose for SS (it mostly cares
about RPL matching CPL, not about descriptor type), and
because CS hasn't been actually used for execution yet (we
have not launched a user process). But the moment iretq fires
with CS=0x33, the CPU will try to fetch user instructions
from a DATA descriptor and #GP.

SamOs flips the macros:

```c
#define USER_CODE_SEGMENT 0x2B
#define USER_DATA_SEGMENT 0x33
```

task.c's `cs = USER_CODE_SEGMENT` now lands the right value;
same for ss. task.asm's `push qword 0x2B` (the CS slot in
the iretq frame) stays - that's user code, which is what
iretq wants for CS.

## task.asm rewritten

### task_return

```nasm
task_return:
    push qword [rdi + 88]    ; SS
    push qword [rdi + 80]    ; RSP
    mov   rax, [rdi + 72]    ; RFLAGS
    or    rax, 0x200         ; force IF=1
    push  rax
    push  qword 0x2B         ; CS = user code + RPL=3
    push  qword [rdi + 56]   ; RIP
    call  restore_general_purpose_registers
    iretq
```

The push order is bottom-up of the iretq frame: SS first, then
RSP, then flags, CS, RIP. iretq pops in the reverse order.

`or rax, 0x200` sets IF=1 in the saved RFLAGS so the task
runs with interrupts enabled (otherwise a freshly-launched
task could only be preempted by NMI / SMI).

The hardcoded 0x2B sidesteps a NASM include of config.h; we
take the deviation but document it.

### restore_general_purpose_registers

```nasm
restore_general_purpose_registers:
    mov rsi, [rdi + 8]
    mov rbp, [rdi + 16]
    mov rbx, [rdi + 24]
    mov rdx, [rdi + 32]
    mov rcx, [rdi + 40]
    mov rax, [rdi + 48]
    mov rdi, [rdi + 0]
    ret
```

Note rdi is loaded LAST - while we're still using it as the
file base pointer. The struct registers offsets are
rdi=0, rsi=8, rbp=16, rbx=24, rdx=32, rcx=40, rax=48 (8 bytes
each).

### user_registers

```nasm
user_registers:
    mov ax, 0x33   ; USER_DATA_SEGMENT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret
```

ds/es/fs/gs need to be data selectors with DPL=3. SS comes
from the iretq frame; CS is set by iretq itself.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | swap USER_CODE_SEGMENT and USER_DATA_SEGMENT values (now match the L39 GDT layout). Comment explains the upstream bug. |
| `src/task/task.asm` | full rewrite under `[BITS 64]`. task_return builds the iretq frame, restore_general_purpose_registers loads the struct, user_registers loads ds/es/fs/gs with USER_DATA_SEGMENT. |

task.asm is still NOT in the build. The dependency chain
(L42 process, L43 paging_desc_free, L44 kernel_desc) needs to
finish first.

## How we verified

VGA tokens unchanged from L38 - L40:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

## Forward look

L42 rewrites the process subsystem. L42-extra is a small
follow-up. L43 implements paging_desc_free. L44 adds
kernel_desc() and shuffles a paging prototype. L45 wires
everything into the build and tests the full process-launch
path.
