#!/usr/bin/env bash
# The x86_64 hook of the e2e harness (see the hook interface at
# the top of _lib.sh): UEFI (OVMF) boots bin/os.img from build.sh on
# QEMU pc or q35. Not a test itself.

E2E_OS_IMG="$(cd "$E2E_ROOT/.." 2>/dev/null && pwd)/bin/os.img"
E2E_QEMU_BIN=qemu-system-x86_64
E2E_OVMF="${E2E_OVMF:-/usr/share/ovmf/OVMF.fd}"
# Every feature a test can ask for but virtio-mmio (QEMU virt only).
E2E_ARCH_PROVIDES="x86 pci ps2 ide ahci nvme acpi apic legacy-pic vga uefi smp virtio-pci xhci"

e2e_arch_build() {
    echo "x86_64: build bin/os.img with ./build.sh at the repo root" >&2
    [ -f "$E2E_OS_IMG" ]
}

e2e_arch_check() {
    [ -f "$E2E_IMAGE" ] || e2e_fail "missing $E2E_IMAGE (run ./build.sh)"
    [ -f "$E2E_OVMF" ]  || e2e_fail "missing OVMF firmware $E2E_OVMF (apt install ovmf)"
}

e2e_arch_qemu_args() {
    E2E_QEMU_ARGS+=(
        -machine "${E2E_MACHINE:-pc}"
        -cpu Skylake-Server
        -bios "$E2E_OVMF"
    )
    if [ "${E2E_BOOT_BUS:-}" = virtio ]; then
        # Boot from virtio-blk (OVMF's VirtioBlkDxe); the kernel
        # then reads everything through virtio_blk.c.
        E2E_QEMU_ARGS+=(-drive "file=$E2E_IMAGE,format=raw,if=none,id=os,snapshot=on"
                        -device virtio-blk-pci,drive=os,bootindex=0)
    elif [ "${E2E_MACHINE:-pc}" = q35 ]; then
        # q35 has AHCI, not IDE: attach the image to the ICH9 AHCI port.
        E2E_QEMU_ARGS+=(-drive "file=$E2E_IMAGE,format=raw,if=none,id=os,snapshot=on"
                        -device ide-hd,drive=os,bus=ide.0)
    else
        E2E_QEMU_ARGS+=(-drive "file=$E2E_IMAGE,format=raw,if=ide,snapshot=on")
    fi
}

# The loader's fixed boot data area (src/bootmarker.h).
e2e_arch_marker_addr() {
    echo 0x6000
}
