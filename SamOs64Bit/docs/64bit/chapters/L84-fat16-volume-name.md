# Lecture 84 - FAT16 changes and running the user program again

L84 closes the L78/L80 loop on volume identification. It adds a
`volume_name` slot to the filesystem vtable, has FAT16 cache its
BPB volume label at resolve time, and drops the L78 `#if 0` guard
in `disk_search_and_init` that was blocking the upstream "primary
FS by name" logic.

This commit also folds in the L83 streamer-rewiring for
`fat16_resolve` (the constructor call that L83 missed because the
upstream L83 commit only patched `init_private`).

## Pieces

### file.h - vtable slot

A new typedef and a new slot:

```c
typedef int (*FS_VOLUME_NAME_FUNCTION)(void* private, char* name_out, size_t max);
struct filesystem {
    ...
    FS_VOLUME_NAME_FUNCTION volume_name;
    char                    name[20];
};
```

Existing initialisers that did not set `.volume_name` see NULL by
default thanks to designated-initialiser zeroing. `disk.c`
null-checks before calling.

### fat16.c - cache + accessor

`fat_private` gets `char name[11]` (FAT16 volume labels are
11 bytes, space-padded, no NUL). `fat16_resolve` copies it out of
the extended BPB after the signature check:

```c
strncpy(fat_private->name,
        fat_private->header.shared.extended_header.volume_id_string,
        sizeof(fat_private->name));
```

The accessor is trivial:

```c
int fat16_volume_name(void* private, char* out, size_t max){
    struct fat_private* p = private;
    strncpy(out, p->name, max);
    return 0;
}
```

`fat16_fs.volume_name = fat16_volume_name`.

### disk.c - drop the guard

The L78 commit had to stub the primary-FS selection because
`volume_name` did not exist yet. The body became "first disk
wins". With the slot live, we restore the real logic:

```c
if(disk->filesystem->volume_name){
    disk->filesystem->volume_name(disk->fs_private, fs_name, sizeof(fs_name));
    if(strncmp(fs_name, primary_drive_fs_name, sizeof(fs_name)) == 0){
        primary_fs_disk = disk;
    }
}
if(primary_fs_disk == NULL){
    primary_fs_disk = disk;
}
```

The `SAMOS_KERNEL_FILESYSTEM_NAME` (a build-time constant) selects
the canonical disk. If no disk's label matches, we still fall
back to first-disk-wins so the kernel boots on disks without a
label.

### fat16_resolve - streamer constructor

L83 changed `fat16_init_private` to `diskstreamer_new_from_disk`
but not the `fat16_resolve` body, which had its own
`diskstreamer_new(disk->id)` call. L84 finishes the job. There
are no more id-based streamer constructions in FAT16.

## Why the streamer change is here, not in L83

Upstream put the resolve-side streamer fix in L84 too (one of the
two file changes in `a3ff2f3`). We follow that grouping verbatim
so the per-commit diff matches upstream's even though the change
would logically have fit in L83. Worth noting that this also
means L83 alone leaves an unused `diskstreamer_new` call site that
L84 cleans up.

## Note on commit-to-lecture mapping

The L80 upstream commit was titled "Building the krealloc
function" but also slipped in the `volume_name` typedef, the
vtable slot, and the fat16 plumbing. The SamOs L80 port focused
on krealloc and deferred the volume_name pieces, leaving the L78
`#if 0` in place. L84 (this commit) gets us back in sync: the
volume_name machinery lands here, and from here on the upstream
diffs apply cleanly again.

## BIOS test status

Source-only. The build links; the test verifies the typedef, the
slot, the FAT private field, the cached copy, the accessor, the
vtable wiring, the removal of the `#if 0` guard, and the disk-side
call. UEFI runtime checks remain blocked on the L86+ work.
