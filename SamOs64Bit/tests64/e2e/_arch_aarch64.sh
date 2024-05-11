#!/usr/bin/env bash
# The aarch64 hook of the e2e harness (see the hook interface at
# the top of _lib.sh). Not a test itself.
#
# QEMU virt (GICv3, Cortex-A72) boots bin/aarch64/kernel.img directly
# (-kernel, the Linux Image header); bin/aarch64/os.img, the same GPT
# layout as x86's bin/os.img with the aarch64 programs, is the boot
# disk on virtio-blk over MMIO. The command line is what x86's UEFI
# loader would read: CMDLINE.TXT on partition 1, as -append. A
# virtio keyboard takes the monitor's sendkey, ramfb is the screen
# for screendump. E2E_MACHINE (pc, q35) does not apply here.
#
# E2E_AARCH64_FW=uefi boots the same image through AAVMF
# instead: a copy of it gets BOOTAA64.EFI (efi-build.sh) and KERNEL.BIN
# on partition 1, its ESP, where the loader also reads CMDLINE.TXT.
# acpi=off, so AAVMF hands over QEMU's device tree and the virtio-mmio
# disk and keyboard are the same as with -kernel.

E2E_OS_IMG="$E2E_ROOT/bin/aarch64/os.img"
E2E_QEMU_BIN=qemu-system-aarch64
E2E_AARCH64_KERNEL="$E2E_ROOT/bin/aarch64/kernel.img"
E2E_AARCH64_EFI="$E2E_ROOT/bin/aarch64/BOOTAA64.EFI"
E2E_AARCH64_AAVMF=/usr/share/AAVMF
# What QEMU virt has: PCIe (ECAM, enumeration only) and virtio-mmio;
# Several CPUs (PSCI CPU_ON).
E2E_ARCH_PROVIDES="aarch64 pci virtio-mmio smp"

e2e_arch_build() {
    bash "$E2E_ROOT/arch-build.sh" aarch64 || return 1
    if [ "${E2E_AARCH64_FW:-}" = uefi ]; then
        bash "$E2E_ROOT/../efi-build.sh" AARCH64 "$E2E_AARCH64_EFI"
    fi
}

e2e_arch_check() {
    [ -f "$E2E_IMAGE" ] || e2e_fail "missing $E2E_IMAGE (bash arch-build.sh aarch64)"
    [ -f "$E2E_AARCH64_KERNEL" ] || e2e_fail "missing $E2E_AARCH64_KERNEL (bash arch-build.sh aarch64)"
    if [ "${E2E_AARCH64_FW:-}" = uefi ]; then
        [ -f "$E2E_AARCH64_EFI" ] || e2e_fail "missing $E2E_AARCH64_EFI (bash efi-build.sh AARCH64 $E2E_AARCH64_EFI)"
        [ -f "$E2E_AARCH64_AAVMF/AAVMF_CODE.fd" ] || e2e_fail "missing $E2E_AARCH64_AAVMF/AAVMF_CODE.fd"
    fi
}

# CMDLINE.TXT as SamOs.c's LoadCommandLine makes it: CR, LF and tab
# become spaces, other control bytes go, trailing spaces too.
_e2e_aarch64_cmdline() {
    MTOOLS_SKIP_CHECK=1 mtype -i "$E2E_IMAGE@@$((2048 * 512))" ::/CMDLINE.TXT 2>/dev/null \
        | tr '\r\n\t' '   ' | tr -cd ' -~' | sed 's/ *$//'
}

e2e_arch_qemu_args() {
    if [ "${E2E_AARCH64_FW:-}" = uefi ]; then
        local img="$E2E_TMP/uefi.img" part
        cp --sparse=always "$E2E_IMAGE" "$img"
        part="$img@@$((2048 * 512))"
        MTOOLS_SKIP_CHECK=1 mmd -i "$part" ::/EFI ::/EFI/BOOT 2>/dev/null
        MTOOLS_SKIP_CHECK=1 mcopy -o -i "$part" "$E2E_AARCH64_EFI" ::/EFI/BOOT/BOOTAA64.EFI &&
            MTOOLS_SKIP_CHECK=1 mcopy -o -i "$part" "$E2E_AARCH64_KERNEL" ::/KERNEL.BIN ||
            e2e_fail "mcopy the loader and kernel onto the ESP"
        cp "$E2E_AARCH64_AAVMF/AAVMF_VARS.fd" "$E2E_TMP/vars.fd"
        E2E_QEMU_ARGS+=(
            -machine virt,gic-version=3,acpi=off
            -cpu cortex-a72
            -global virtio-mmio.force-legacy=false
            -drive "if=pflash,format=raw,readonly=on,file=$E2E_AARCH64_AAVMF/AAVMF_CODE.fd"
            -drive "if=pflash,format=raw,file=$E2E_TMP/vars.fd"
            -drive "file=$img,format=raw,if=none,id=os"
            -device virtio-blk-device,drive=os
            -device virtio-keyboard-device
            -device ramfb
        )
        return
    fi
    E2E_QEMU_ARGS+=(
        -machine virt,gic-version=3
        -cpu cortex-a72
        -global virtio-mmio.force-legacy=false
        -kernel "$E2E_AARCH64_KERNEL"
        -append "$(_e2e_aarch64_cmdline)"
        -drive "file=$E2E_IMAGE,format=raw,if=none,id=os,snapshot=on"
        -device virtio-blk-device,drive=os
        -device virtio-keyboard-device
        -device ramfb
    )
}

# The kernel prints where its marker page is ("boot markers at pa").
e2e_arch_marker_addr() {
    tr -d '\r' < "$E2E_SERIAL" 2>/dev/null | sed -n 's/.*boot markers at pa \(0x[0-9a-f]*\).*/\1/p' | head -n 1
}
