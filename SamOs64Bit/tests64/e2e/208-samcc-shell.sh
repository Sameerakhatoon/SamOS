#!/usr/bin/env bash
# e2e/208-samcc-shell.sh - on x86-64 the shell that BLANK.ELF
# hands over to is SamCompiler's build of programs/shell/src/shell.c
# (-target=samos-x86_64 against build/x86_64/sysroot), put on the disk
# as SHELL.ELF in place of the gcc one, with HELLOSC.ELF
# (programs/hello/src/hello.c). Its builtins are typed over the QEMU
# monitor as in e2e/126: help, echo with > and >>, cat, write, ls,
# mkdir, rm, rmdir, date, a program and an unknown command.
# tests64/samcc.sh finds the compiler (SAMCC, else a SamCompiler
# clone next to this repository); without one the test is a SKIP (e2e_skip).
# e2e-requires: x86 (-target=samos-x86_64 and the x86 disk layout; aarch64-samcc.sh and riscv64-samcc.sh run the aarch64 and riscv64 builds)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$E2E_ROOT/tests64/samcc.sh"

samcc_find x86_64 || e2e_skip "$SAMCC_SKIP"
samcc=$SAMCC_BIN
e2e_log "SamCompiler: $samcc"
sysroot="$E2E_ROOT/build/x86_64/sysroot"
[ -f "$sysroot/lib/libsamos.a" ] || e2e_fail "missing $sysroot (run ./build.sh)"

for p in SHELL:shell/src/shell.c HELLOSC:hello/src/hello.c; do
    elf="$E2E_TMP/${p%%:*}.ELF"
    (cd "$E2E_TMP" && "$samcc" -target=samos-x86_64 -sysroot="$sysroot" "$E2E_ROOT/programs/${p#*:}" "$elf" \
        > "$E2E_TMP/${p%%:*}.log" 2>&1)
    head -c 4 "$elf" 2>/dev/null | grep ELF > /dev/null || e2e_fail "SamCompiler did not build ${p#*:}: $(tail -3 "$E2E_TMP/${p%%:*}.log")"
    readelf -h "$elf" | grep 'Machine:.*X86-64' > /dev/null || e2e_fail "${p%%:*}.ELF is not x86-64"
    entry="$(readelf -h "$elf" | sed -n 's/.*Entry point address: *0x0*//p')"
    start="$(nm "$elf" | awk '$3 == "_start" { sub(/^0+/, "", $1); print $1 }')"
    [ -n "$entry" ] && [ "$entry" = "$start" ] || e2e_fail "${p%%:*}.ELF entry 0x$entry is not _start"
done

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
P2="$E2E_IMAGE@@$((716800 * 512))"
for n in SHELL HELLOSC; do
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$P2" "$E2E_TMP/$n.ELF" "::/$n.ELF" || e2e_fail "mcopy $n.ELF"
done

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'help'
    shell_cmd 'echo hi there'
    shell_cmd 'echo saved words > sc.txt'
    shell_cmd 'echo more >> sc.txt'
    shell_cmd 'cat sc.txt'
    shell_cmd 'write w.txt one two'
    shell_cmd 'cat w.txt'
    shell_cmd 'mkdir scdir'
    shell_cmd 'ls'
    shell_cmd 'rm w.txt'
    shell_cmd 'rmdir scdir'
    shell_cmd 'cat nope.txt'
    shell_cmd 'date'
    shell_cmd 'hellosc.elf a b' 60
    shell_cmd 'nosuch'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 900 _script

expect_serial "$SHELL_BANNER" "the SamCompiler-built shell started"
expect_serial '^  echo \[words\.\.\.\] \[> file \| >> file\]  print or write the words$' "help: the builtin table"
expect_serial '^Quote words with spaces: echo "a b" .c d.$' "help: escaped quotes"
expect_serial '^hi there$' "echo"
expect_serial '^saved words$' "echo > file, cat"
expect_serial '^more$' "echo >> appends"
expect_serial '^one two$' "write, cat"
expect_serial '^sc\.txt +17$' "ls: the file and its size"
expect_serial '^scdir +<DIR>$' "mkdir, ls"
expect_serial '^cat: nope\.txt: ' "cat of a missing file"
expect_serial '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} UTC$' "date (seven printf arguments)"
expect_serial 'hello: argv\[2\]=b$' "HELLOSC.ELF started from the shell"
expect_serial '^hellosc\.elf: exit status 0$' "the shell waited for it"
expect_serial '^nosuch: command not found$' "unknown command"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"
echo "PASS: e2e/208 SamCompiler-built shell"
