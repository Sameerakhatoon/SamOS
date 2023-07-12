# Lecture 78 - virtual disks + vector library

**Source commit (PeachOS64BitCourse):** `6272908`
**SamOs commit:** L78 on `module1-64bit` branch
**Regression test:** `tests64/L78-virtual-disks.sh`

## Why this chapter exists

L77 added GPT reading but couldn't compile - it called
`disk_create_new(SAMOS_DISK_TYPE_PARTITION, ...)` which
didn't exist. L78 adds:

1. A generic vector library (src/lib/vector/).
2. `SAMOS_DISK_TYPE_PARTITION` constant.
3. `starting_lba` / `ending_lba` bounds on `struct disk`.
4. `disk_create_new` + `disk_search_and_init` rewrite.

## Vector library

`struct vector` is just `void* memory + size_t e_size +
size_t t_elems + ...`. Allocates flat memory via `krealloc`
when capacity runs out. Element copies via `memcpy` (the user
hands pointer + size; we trust the size).

API:
- `vector_new(element_size, reserve, flags)`
- `vector_push(vec, elem_ptr)` -> index or `-ENOMEM`
- `vector_pop(vec)` -> decrement count
- `vector_at(vec, i, out_ptr, size_check)` -> safe copy
- `vector_overwrite(vec, i, elem_ptr, size_check)`
- `vector_pop_element(vec, mem_val, size)` -> linear search +
  shift-left
- `vector_reorder(vec, fn)` -> bubble sort by user predicate
- `vector_count(vec)`
- `vector_free(vec)`

Stored elements can be any size. For pointer storage (which is
what disk_vector does), pass `sizeof(struct disk*)`.

## SAMOS_DISK_TYPE_PARTITION

```c
#define SAMOS_DISK_TYPE_REAL        0
#define SAMOS_DISK_TYPE_PARTITION   1
```

Real disks come from disk_search_and_init (the physical ATA
primary). Partition disks come from gpt_mount_partitions (one
per GPT entry).

## starting_lba / ending_lba

```c
struct disk {
    ...
    size_t starting_lba;
    size_t ending_lba;
};
```

Set both to 0 for the primary disk - all bounds checking is
skipped. For partition disks, the values come from the GPT
entry. `disk_read_block` adds starting_lba:

```c
int disk_read_block(struct disk* idisk, unsigned int lba, ...) {
    size_t abs = idisk->starting_lba + lba;
    if (abs + total > idisk->ending_lba) {
        if (idisk->starting_lba != 0 && idisk->ending_lba != 0) {
            return -EIO;
        }
    }
    return disk_read_sector(abs, total, buf);
}
```

So a partition disk sees its own LBA 0..(ending-starting) as
the zero-based address space. The kernel filesystem layer
doesn't need to know about the GPT - it just sees a "disk"
with the partition's content at the start.

## disk_create_new

```c
int disk_create_new(int type, int starting, int ending,
                    size_t sector_size, struct disk** out) {
    disk = kzalloc(...);
    disk->type = type;
    disk->id = vector_count(disk_vector);
    disk->starting_lba = starting;
    disk->ending_lba = ending;
    disk->sector_size = sector_size;
    disk->filesystem = fs_resolve(disk);
    if (disk->filesystem) {
        // L84 - compare filesystem volume name to
        // SAMOS_KERNEL_FILESYSTEM_NAME to find primary_fs_disk.
        // SamOs guards this in #if 0 pending L84.
    }
    vector_push(disk_vector, &disk);
    if (out) *out = disk;
    return 0;
}
```

`disk_search_and_init` becomes:

```c
disk_vector = vector_new(sizeof(struct disk*), 4, 0);
disk_create_new(SAMOS_DISK_TYPE_REAL, 0, 0, SAMOS_SECTOR_SIZE,
                &disk_primary_handle);
```

## SamOs deviations

| Issue | Upstream | SamOs |
|---|---|---|
| krealloc | depends on L80 | added a temp stub in kheap.c at L78 (`kzalloc + memcpy + kfree`) |
| `filesystem->volume_name` | added in L84 | wrapped in `#if 0`; primary_fs_disk falls back to "first disk wins" |

Both deviations let the build succeed at L78 instead of
waiting for L80/L84. They're labelled "L80-stub" / "L84-stub"
in the source.

## How CI verifies

- vector.h, vector.c, types.h present
- build/lib/vector/vector.o produced (vector links in)
- disk.h has SAMOS_DISK_TYPE_PARTITION,
  SAMOS_KERNEL_FILESYSTEM_NAME, starting_lba field,
  disk_create_new prototype
- disk.c calls vector_new + vector_push
- bin/kernel.bin builds (everything links)

## Forward look

L80 - krealloc real implementation (drops our temp stub).
L81 - path parsing symlink to system filesystem.
L82 - gpt_init wired into the disk layer.
L83 - disk streamer improvements.
L84 - FAT16 changes including volume_name (drops our #if 0).
