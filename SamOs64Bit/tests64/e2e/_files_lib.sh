#!/usr/bin/env bash
# The storage done criterion driven through the shell:
# "the shell can mkdir, write a file, reboot and read it back, on
# both IDE and AHCI, and on both FAT16 and FAT32 volumes. A file
# written by SamOS is readable by Linux (mtools) and the reverse."
# Sourced by e2e/180 (pc, IDE) and e2e/181 (q35, AHCI) after _lib.sh
# and _shell_lib.sh; set E2E_MACHINE first. Not a test itself.
#
# Two data disks in E2E_TMP, next to the usual (snapshot) boot image:
#   fat16.img  64 MiB FAT16, whole disk, label F16VOL  -> drive 1:
#   fat32.img  64 MiB FAT32, whole disk, label F32VOL  -> drive 2:
# (disk ids: the three REAL disks in controller order, then the boot
# image's two GPT partitions.) pc: IDE primary slave and secondary
# master; q35: AHCI ports 1 and 2. Neither is labelled SMTEST, so the
# kernel's storage selftest leaves them alone.
#
#   1. Linux (mtools) writes HOSTF16.TXT / HOSTF32.TXT.
#   2. Boot 1: the shell makes directories, writes files (write,
#      echo >, echo >>, a long name), lists and cats them, reads the
#      Linux file, removes a file and a directory, and checks the
#      error cases (mkdir existing, rmdir non-empty, rm a directory,
#      cat a missing file).
#   3. Boot 2 on the same disk images: the shell cats them back.
#   4. Host: fsck.fat -n is clean, mtype reads what SamOS wrote.

FILES_VOLS="1:16 2:32"   # drive:FAT type

# files_cmd <line>: shell_cmd, then check the shell echoed the line as
# typed. Under a loaded host a sendkey now and then never reaches the
# guest (seen once on q35: "cat 2:/IR/NOTE.TXT"); the command is
# typed again once. A garbled command names a path that does not
# exist, so running it is harmless. The echoed line may or may not
# carry the "> " in front: shell_cmd returns as soon as the prompt
# starts a line, so the next command's log can begin after it.
files_cmd() {
    local before
    before=$(shell_log_size)
    shell_cmd "$1"
    if ! tail -c +"$((before + 1))" "$E2E_SERIAL" | tr -d '\r' | grep -qxF -e "> $1" -e "$1"; then
        e2e_log "retyping: $1"
        shell_cmd "$1"
    fi
}

files_make_disks() {
    export MTOOLS_SKIP_CHECK=1
    truncate -s 64M "$E2E_TMP/fat16.img"
    mkfs.fat -F 16 -n F16VOL "$E2E_TMP/fat16.img" >/dev/null || e2e_fail "mkfs FAT16"
    truncate -s 64M "$E2E_TMP/fat32.img"
    mkfs.fat -F 32 -s 1 -n F32VOL "$E2E_TMP/fat32.img" >/dev/null || e2e_fail "mkfs FAT32"
    local v
    for v in 16 32; do
        printf 'written by linux on fat%s\n' "$v" > "$E2E_TMP/host.txt"
        mcopy -i "$E2E_TMP/fat$v.img" "$E2E_TMP/host.txt" "::HOSTF$v.TXT" || e2e_fail "mcopy host file"
    done
    if [ "${E2E_MACHINE:-pc}" = q35 ]; then
        E2E_EXTRA_QEMU_ARGS="-drive file=$E2E_TMP/fat16.img,format=raw,if=none,id=f16 -device ide-hd,drive=f16,bus=ide.1"
        E2E_EXTRA_QEMU_ARGS+=" -drive file=$E2E_TMP/fat32.img,format=raw,if=none,id=f32 -device ide-hd,drive=f32,bus=ide.2"
    else
        E2E_EXTRA_QEMU_ARGS="-drive file=$E2E_TMP/fat16.img,format=raw,if=none,id=f16 -device ide-hd,drive=f16,bus=ide.0,unit=1"
        E2E_EXTRA_QEMU_ARGS+=" -drive file=$E2E_TMP/fat32.img,format=raw,if=none,id=f32 -device ide-hd,drive=f32,bus=ide.1,unit=0"
    fi
}

_files_boot1() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    local dv d v
    for dv in $FILES_VOLS; do
        d=${dv%%:*} v=${dv##*:}
        files_cmd "ls $d:/"
        files_cmd "cat $d:/HOSTF$v.TXT"
        files_cmd "mkdir $d:/SAMDIR"
        files_cmd "write $d:/SAMDIR/NOTE.TXT hello from samos f$v"
        files_cmd "echo second line >> $d:/SAMDIR/NOTE.TXT"
        files_cmd "echo replaced f$v > $d:/SAMDIR/ECHO.TXT"
        files_cmd "write \"$d:/SAMDIR/long name f$v.txt\" lfn ok"
        files_cmd "ls $d:/SAMDIR"
        files_cmd "cat $d:/SAMDIR/NOTE.TXT"
    done
    # Removing, on FAT16; the error cases, on FAT32.
    files_cmd "mkdir 1:/GONE"
    files_cmd "write 1:/GONE/X.TXT x"
    files_cmd "rm 1:/GONE/X.TXT"
    files_cmd "rmdir 1:/GONE"
    files_cmd "mkdir 2:/SAMDIR"
    files_cmd "rmdir 2:/SAMDIR"
    files_cmd "rm 2:/SAMDIR"
    files_cmd "cat 2:/NOPE.TXT"
    files_cmd "echo boot1-done"
    sleep 1
    printf 'quit\n'
}

_files_boot2() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    local dv d v
    for dv in $FILES_VOLS; do
        d=${dv%%:*} v=${dv##*:}
        files_cmd "cat $d:/SAMDIR/NOTE.TXT"
        files_cmd "cat $d:/SAMDIR/ECHO.TXT"
        files_cmd "cat \"$d:/SAMDIR/long name f$v.txt\""
    done
    files_cmd "ls 1:/"
    files_cmd "echo boot2-done"
    sleep 1
    printf 'quit\n'
}

files_fails=0
files_ok()  { echo "  ok: $1"; }
files_bad() { echo "  FAIL: $1"; files_fails=$((files_fails + 1)); }

# files_line <log> <exact line> <what>
files_line() {
    if tr -d '\r' < "$1" | grep -qxF -- "$2"; then files_ok "$3"; else files_bad "$3 (no line '$2')"; fi
}
# files_re <log> <ERE> <what>
files_re() {
    if tr -d '\r' < "$1" | grep -Eq -- "$2"; then files_ok "$3"; else files_bad "$3 (no /$2/)"; fi
}

files_e2e_main() {
    local tag=$1 v log1="$E2E_TMP/serial-1.log" log2="$E2E_TMP/serial-2.log"
    files_make_disks

    e2e_run_qemu 900 _files_boot1
    cp "$E2E_SERIAL" "$log1" 2>/dev/null
    tr -d '\r' < "$log1" | grep -q '^boot1-done$' || e2e_fail "$tag: boot 1 did not finish"

    e2e_run_qemu 600 _files_boot2
    cp "$E2E_SERIAL" "$log2" 2>/dev/null
    tr -d '\r' < "$log2" | grep -q '^boot2-done$' || e2e_fail "$tag: boot 2 did not finish"

    echo "boot 1 ($tag):"
    for v in 16 32; do
        files_re   "$log1" "^HOSTF$v\.TXT +26$" "fat$v: ls shows the Linux file"
        files_line "$log1" "written by linux on fat$v" "fat$v: cat reads the Linux file"
        files_re   "$log1" "^NOTE\.TXT +33$" "fat$v: ls lists NOTE.TXT (33 bytes)"
        files_re   "$log1" "^long name f$v\.txt +7$" "fat$v: ls lists the long name"
        files_line "$log1" "hello from samos f$v" "fat$v: cat NOTE.TXT, first line"
    done
    files_line "$log1" "second line" "echo >> appended"
    files_line "$log1" "mkdir: 2:/SAMDIR: already exists" "mkdir of an existing directory refused"
    files_line "$log1" "rmdir: 2:/SAMDIR: directory not empty" "rmdir of a non-empty directory refused"
    files_line "$log1" "rm: 2:/SAMDIR: is a directory" "rm of a directory refused"
    files_line "$log1" "cat: 2:/NOPE.TXT: no such file or directory" "cat of a missing file"
    # Exactly the three refusals above, no other command failed.
    if [ "$(tr -d '\r' < "$log1" | grep -Ec '^(mkdir|write|echo|rm|rmdir|ls): ')" -eq 3 ]; then
        files_ok "no other command failed"
    else
        files_bad "an unexpected command failed:"
        tr -d '\r' < "$log1" | grep -E '^(mkdir|write|echo|rm|rmdir|ls): ' | sed 's/^/    /'
    fi

    echo "boot 2 ($tag, same disks):"
    for v in 16 32; do
        files_line "$log2" "hello from samos f$v" "fat$v: NOTE.TXT survived the reboot"
        files_line "$log2" "replaced f$v" "fat$v: ECHO.TXT survived the reboot"
    done
    files_line "$log2" "second line" "the appended line survived"
    files_line "$log2" "lfn ok" "the long-name file survived"
    files_re   "$log2" "^HOSTF16\.TXT +26$" "ls after the reboot"
    files_re   "$log2" "^SAMDIR +<DIR>$" "ls shows the directory"
    if tr -d '\r' < "$log2" | grep -q '^GONE '; then files_bad "GONE came back"; else files_ok "rmdir'd GONE stays gone"; fi

    echo "host (mtools, fsck.fat):"
    export MTOOLS_SKIP_CHECK=1
    for v in 16 32; do
        local img="$E2E_TMP/fat$v.img"
        if fsck.fat -n "$img" > "$E2E_TMP/fsck$v.log" 2>&1; then
            files_ok "fat$v: fsck.fat -n clean"
        else
            files_bad "fat$v: fsck.fat found errors"; sed 's/^/    /' "$E2E_TMP/fsck$v.log" | head -20
        fi
        [ "$(mtype -i "$img" ::SAMDIR/NOTE.TXT 2>/dev/null)" = "hello from samos f$v
second line" ] && files_ok "fat$v: Linux reads NOTE.TXT" || files_bad "fat$v: Linux reads NOTE.TXT"
        [ "$(mtype -i "$img" ::SAMDIR/ECHO.TXT 2>/dev/null)" = "replaced f$v" ] \
            && files_ok "fat$v: Linux reads ECHO.TXT" || files_bad "fat$v: Linux reads ECHO.TXT"
        [ "$(mtype -i "$img" "::SAMDIR/long name f$v.txt" 2>/dev/null)" = "lfn ok" ] \
            && files_ok "fat$v: Linux reads the long name" || files_bad "fat$v: Linux reads the long name"
    done
    if mdir -i "$E2E_TMP/fat16.img" ::GONE >/dev/null 2>&1; then
        files_bad "Linux still sees GONE"
    else
        files_ok "Linux does not see GONE"
    fi

    for log in "$log1" "$log2"; do
        if tr -d '\r' < "$log" | grep -Eq 'KERNEL PANIC|KERNEL FAULT|USER FAULT'; then
            files_bad "fault in $(basename "$log")"
        fi
    done
    [ "$files_fails" -eq 0 ] || e2e_fail "$tag shell files: $files_fails check(s) failed"
}
