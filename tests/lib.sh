# tests/lib.sh - shared helpers sourced by individual test scripts.
# No commands run at source time; functions only.

# qemu_boot_capture <image> <timeout_sec> <output_file>
#
# Boots <image> in QEMU TCG with serial routed to <output_file>, kills at
# <timeout_sec>. Headless. Returns 0 always - caller should grep the output
# file to verify behavior.
qemu_boot_capture() {
    local image="$1"
    local timeout_sec="$2"
    local out="$3"
    timeout "${timeout_sec}" qemu-system-x86_64 \
        -hda "$image" \
        -m 512 \
        -accel tcg \
        -display none \
        -serial "file:$out" \
        -no-reboot \
        > /dev/null 2>&1 || true
}

# qemu_boot_capture_stdio <image> <timeout_sec> <output_file>
#
# Variant that routes serial to stdio so the captured output includes anything
# the BIOS or kernel writes to COM1. Used early when the bootloader doesn't yet
# wire its own serial.
qemu_boot_capture_stdio() {
    local image="$1"
    local timeout_sec="$2"
    local out="$3"
    timeout "${timeout_sec}" qemu-system-x86_64 \
        -hda "$image" \
        -m 512 \
        -accel tcg \
        -display none \
        -serial stdio \
        -no-reboot \
        > "$out" 2>&1 || true
}

# Verify file size in bytes equals expected.
assert_file_size() {
    local file="$1"
    local want="$2"
    local got
    got=$(stat -c%s "$file")
    if [ "$got" != "$want" ]; then
        echo "FAIL: expected $file to be $want bytes, got $got" >&2
        return 1
    fi
}

# Verify the last two bytes of a file equal the given 4-hex-digit signature.
# E.g. assert_boot_signature bin/boot.bin 55aa
assert_boot_signature() {
    local file="$1"
    local want="$2"
    local got
    got=$(tail -c 2 "$file" | xxd -p)
    if [ "$got" != "$want" ]; then
        echo "FAIL: expected boot signature $want, got $got" >&2
        return 1
    fi
}
