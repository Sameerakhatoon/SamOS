# Lecture 58 - simple user program on the disk

**Source commit (PeachOS64BitCourse):** `ba20f93`
**SamOs commit:** L58 on `module1-64bit` branch
**Regression test:** `tests64/L58-simple-bin-on-disk.sh`

## Why this chapter exists

To prove the user-program loading path will work in L59 - L60,
upstream creates the simplest possible user binary:

```nasm
[BITS 64]
jmp $
```

Two bytes: `EB FE`. When the kernel loads this at the user
virtual entry point and `iretq`'s into it, the user task runs
the jmp-to-self forever - long enough for us to observe via
QEMU monitor that we actually reached ring 3.

## How SamOs installs it

Upstream uses `sudo mount` + `cp` to drop the binary onto a
loop-mounted FAT image. SamOs avoids root by using `mtools`
(`mformat` + `mcopy`).

The tricky part is the boot sector. mformat by default lays
down its own boot sector + BPB; doing so on `bin/os.bin`
would clobber both our boot.asm code AND the BPB our boot
code expects.

The working recipe:

1. Build `os.bin = boot.bin + kernel.bin + 16 MiB zero pad`
   (the existing flow).
2. `mformat -R 200 -c 4 -i bin/os.bin ::`:
   - `-R 200` reserved sectors so the FAT lands above
     kernel.bin (which sits at sectors 1..N, N < 200).
   - `-c 4` sectors-per-cluster gives ~8192 clusters - inside
     the FAT16 range, so mformat doesn't auto-pick FAT32.
   - mformat writes a fresh boot sector + BPB.
3. `dd if=bin/boot.bin of=bin/os.bin bs=1 skip=62 seek=62
    count=450 conv=notrunc`:
   - Re-overlay bytes 62..511 of our SamOs boot.bin (the boot
     code + the 0xAA55 boot signature). The BPB at bytes
     0..61 stays as mformat wrote it - that's what the disk
     driver will parse later.
4. `mcopy -n -i bin/os.bin programs/simple/build/simple.bin
    ::SIMPLE.BIN`:
   - drop the user binary into the FAT16 root.

## Why the split

- BIOS at boot reads ONLY bytes 0..511 of the disk. It cares
  about: the boot code (starts at offset 62) and the boot
  signature (offsets 510..511). It doesn't parse the BPB.
- The KERNEL's disk driver, once running, reads the BPB to
  understand FAT geometry. That's the one mformat wrote, so
  the geometry is real.
- mformat's BPB describes a real FAT16 with reserved=200,
  so kernel.bin (which sits in the reserved area at sectors
  1..N < 200) coexists with the FAT data above.

The 450-byte dd overlay catches both the executable code at
offset 62 and the signature at 510. The 62 leading bytes
(jmp instruction + BPB) come from mformat.

## kernel.c addition: keyboard_init

L58 also calls `keyboard_init()` after
`isr80h_register_commands()`. It registers the PS/2 driver
with the keyboard subsystem. The driver doesn't actually
receive scancodes until IRQ1 is unmasked at the PIC AND
interrupts are enabled - both still pending.

## Trade-offs

- mformat's default geometry choices may differ from SamOs
  conventions. We accept them because the kernel's disk
  driver re-parses on every boot.
- The 450-byte overlay relies on the exact layout of our
  boot.asm: jmp+nop (3 bytes), BPB+EBPB (59 bytes), then
  CODE (offset 62 onward). If we ever reorganise boot.asm
  this dd will need to update.

## How we verified

`tests64/L58-simple-bin-on-disk.sh` asserts:
- `mdir bin/os.bin` lists SIMPLE.BIN
- boot signature `55 aa` at offset 510
- VGA still shows the L57 token sequence

All three pass.

## Forward look

L59 - L60 actually load `0:/SIMPLE.BIN` via
`process_load_switch`, build a process around it, and
`task_run_first_ever_task` to enter ring 3. The 2-byte
`jmp $` will then run forever in user mode.
