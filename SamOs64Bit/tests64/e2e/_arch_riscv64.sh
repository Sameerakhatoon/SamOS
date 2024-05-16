#!/usr/bin/env bash
# The riscv64 hook of the e2e harness (see the hook interface at
# the top of _lib.sh). Not a test itself.
#
# QEMU virt boots OpenSBI (-bios default), which starts
# bin/riscv64/kernel.img in S-mode (-kernel); bin/riscv64/os.img, the
# same GPT layout as x86's bin/os.img with the riscv64 programs, is the
# boot disk on virtio-blk over MMIO (version 2: QEMU's virt still
# defaults its virtio-mmio slots to the legacy layout, which
# virtio_mmio.c does not drive). The command line is what x86's UEFI
# loader would read: CMDLINE.TXT on partition 1, as -append. A virtio
# keyboard takes the monitor's sendkey, ramfb is the screen for
# screendump. E2E_MACHINE (pc, q35) does not apply here. E2E_SMP
# harts, started with SBI HSM (OpenSBI picks any of them to boot).

E2E_OS_IMG="$E2E_ROOT/bin/riscv64/os.img"
E2E_QEMU_BIN=qemu-system-riscv64
E2E_RISCV64_KERNEL="$E2E_ROOT/bin/riscv64/kernel.img"
E2E_ARCH_PROVIDES="riscv64 virtio-mmio smp"

e2e_arch_build() {
    bash "$E2E_ROOT/arch-build.sh" riscv64
}

e2e_arch_check() {
    [ -f "$E2E_IMAGE" ] || e2e_fail "missing $E2E_IMAGE (bash arch-build.sh riscv64)"
    [ -f "$E2E_RISCV64_KERNEL" ] || e2e_fail "missing $E2E_RISCV64_KERNEL (bash arch-build.sh riscv64)"
}

# CMDLINE.TXT as SamOs.c's LoadCommandLine makes it: CR, LF and tab
# become spaces, other control bytes go, trailing spaces too.
_e2e_riscv64_cmdline() {
    MTOOLS_SKIP_CHECK=1 mtype -i "$E2E_IMAGE@@$((2048 * 512))" ::/CMDLINE.TXT 2>/dev/null \
        | tr '\r\n\t' '   ' | tr -cd ' -~' | sed 's/ *$//'
}

# E2E_RISCV64_CPU=rv64,sv48=off runs the suite in Sv39.
e2e_arch_qemu_args() {
    E2E_QEMU_ARGS+=(
        -machine virt
        -cpu "${E2E_RISCV64_CPU:-rv64}"
        -bios default
        -kernel "$E2E_RISCV64_KERNEL"
        -append "$(_e2e_riscv64_cmdline)"
        -global virtio-mmio.force-legacy=false
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
