# Ch 21 - Entering Protected Mode

**Book pages:** 48-49 (Part 5)
**Code in this chapter:** none (book shows outline only, actual code in Ch 22)

## The seven steps the CPU needs

1. **Disable interrupts** (`cli`). The IVT format changes once the CPU is in Protected Mode (it becomes the IDT), so an interrupt during the transition would dereference the wrong table.
2. **Set up a Global Descriptor Table (GDT)** in memory. The GDT lists every segment we want available: null (required first entry), code, data, and optionally user-mode code and data, TSS, etc. Each entry is 8 bytes encoding base, limit, access rights, and flags.
3. **Load the GDT** via `lgdt [gdt_descriptor]`. The argument is a 6-byte structure: 2-byte limit (size - 1) plus 4-byte base address.
4. **Set the PE (Protection Enable) bit** in CR0 (bit 0). Read CR0 into EAX, OR with 1, write back.
5. **Far-jump** to flush the prefetch queue and load a new CS selector. After the jump CS holds an index into the GDT, no longer a 16-bit segment base.
6. **Reload DS, ES, FS, GS, SS** with the data-segment selector. Set ESP to a valid stack location.
7. **Re-enable interrupts** (`sti`) if needed. We typically wait until after the IDT is set up, several chapters from now.

## Why the far jump

In Real Mode `jmp label` only changes EIP. The CS register stays as it was. The CPU also caches a hidden "current code segment descriptor" that controls instruction decoding. After flipping CR0.PE the CPU's behavior depends on this hidden descriptor, which is still in Real Mode form. A far jump `jmp segment:label` writes a new selector into CS and forces the CPU to load the matching GDT descriptor. Only then is the CPU truly executing in 32-bit Protected Mode.

## Skeleton

```asm
cli
lgdt [gdt_descriptor]
mov eax, cr0
or  eax, 0x1
mov cr0, eax
jmp CODE_SEG:next_instr    ; far jump, flushes pipeline + loads CS

[bits 32]
next_instr:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x200000      ; or wherever we want the kernel stack
    sti                    ; optional, only after IDT is ready
```

`CODE_SEG` and `DATA_SEG` are constants computed as the byte offset of the corresponding entry within the GDT. The next chapter actually defines them.
