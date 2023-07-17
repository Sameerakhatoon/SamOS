# Lecture 81 - "@" drive resolves to primary filesystem

L81 adds one tiny piece of syntactic sugar to the path parser and
one wiring change in the kernel. Userland programs that want to
open a file on "whichever disk the kernel actually shipped on" no
longer have to hard-code drive 0.

## The change

`pparser_path_valid_format` used to require `isdigit(filename[0])`.
It now accepts `'@'` as well. `pparser_get_drive_by_path` looks at
the first character: if it is `'@'`, it calls `disk_primary_fs_disk()`
and uses that disk's `id` as the drive number. Otherwise it uses
the digit as before.

```
"@:/SIMPLE.BIN"  -> drive_no = disk_primary_fs_disk()->id
"0:/SIMPLE.BIN"  -> drive_no = 0   (unchanged)
"1:/foo"         -> drive_no = 1   (unchanged)
```

If no primary filesystem was detected (`disk_search_and_init`
returned without finding any FAT16 disk) the parser returns `-EIO`.

## Why @ instead of "PRIMARY:/"

McCarthy picked `'@'` because the path parser already keys off a
single character followed by `:/`. Anything multi-character would
have meant rewriting the parser. The original `isdigit` check was
exactly one byte wide; `(isdigit(c) || c == '@')` keeps the same
shape.

## SamOs deviation in kernel.c

Upstream changes `process_load_switch("0:/shell.elf", ...)` to
`process_load_switch("@:/shell.elf", ...)`. We are still on
`SIMPLE.BIN` because the L62/L65/L66 ELF64 path is slow under
ATA-PIO + QEMU TCG (~40 seconds to read a 2 MiB ELF; CI budget is
15 seconds). So we make the equivalent edit:

```
"0:/SIMPLE.BIN" -> "@:/SIMPLE.BIN"
```

Same intent, different file. The ELF64 cutover will happen once
the L83 disk streamer improvements and L84 FAT16 changes land.

## BIOS test status

BIOS boot has been broken since L74's UEFI pivot. This change is
source-checked only. The build still links, and the parser change
is mechanical enough that a runtime test would not surface any
behaviour the source check misses. When the UEFI path is wired up
end-to-end, the `@:/SIMPLE.BIN` resolution becomes a natural part
of the first-boot smoke test.
