# tests64/arch/samcc.inc - programs compiled by SamCompiler
# (-target=samos-<arch>) run under SamOS. Sourced by aarch64-samcc.sh
# and riscv64-samcc.sh with $arch set; x86_64 is e2e/208.
#
# SamCompiler builds, against build/<arch>/sysroot (the gcc-built
# crt0, libsamos.a and user.ld the Makefile exports):
#   HELLOSC.ELF  programs/hello/src/hello.c
#   SHTEST.ELF   programs/samcc/shtest.c: programs/shell/src/shell.c
#                with its main renamed, running "+"-separated shell
#                lines from argv through the shell's shell_execute
#                (no keyboard on these ports before the aarch64 device work/the riscv64 device work)
#   SHELLSC.ELF  programs/shell/src/shell.c as it is
# Each is checked to be a static ELF for the machine with _start as
# its entry, then copied onto the RAM disk and started with samos.run=.
# tests64/samcc.sh finds the compiler (SAMCC, else a SamCompiler
# clone next to this repository); without one the test is a SKIP.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
. "$root/tests64/samcc.sh"
if ! samcc_find "$arch"; then
    echo "$arch samcc arch test: SKIP ($SAMCC_SKIP)"
    exit 0
fi
samcc=$SAMCC_BIN
echo "$arch samcc: $samcc"
case "$arch" in
aarch64) image="$root/bin/aarch64/ramdisk.img" machine=AArch64 ;;
riscv64) image="$root/bin/riscv64/initrd.img" machine=RISC-V ;;
esac
sysroot="$root/build/$arch/sysroot"
if [ ! -f "$root/bin/$arch/kernel.img" ] || [ ! -f "$image" ] || [ ! -f "$sysroot/lib/libsamos.a" ]; then
    bash "$root/arch-build.sh" "$arch" > /dev/null 2>&1 || { echo "FAIL: $arch build"; exit 1; }
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
checks=0
fails=0
pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }
expect() { grep -aEq "$1" "$tmp/boot.log"; check $? "$2"; }
expect_count() {
    local n
    n=$(grep -aEc "$1" "$tmp/boot.log")
    [ "$n" -eq "$2" ]
    check $? "$3 (/$1/ x$n, want $2)"
}

echo "$arch samcc build"
cp "$image" "$tmp/initrd.img"
# Without the gcc SHELL.ELF, BLANK.ELF's hand-over fails and the one
# shell banner is SHELLSC.ELF's.
MTOOLS_SKIP_CHECK=1 mdel -i "$tmp/initrd.img" ::/SHELL.ELF
for p in HELLOSC:hello/src/hello.c SHTEST:samcc/shtest.c SHELLSC:shell/src/shell.c; do
    name="${p%%:*}"
    elf="$tmp/$name.ELF"
    (cd "$tmp" && "$samcc" -target=samos-$arch -sysroot="$sysroot" "$root/programs/${p#*:}" "$elf" > "$tmp/$name.log" 2>&1)
    head -c 4 "$elf" 2>/dev/null | grep ELF > /dev/null
    check $? "$name.ELF compiled by SamCompiler"
    head -c 4 "$elf" 2>/dev/null | grep ELF > /dev/null || { tail -5 "$tmp/$name.log"; continue; }
    readelf -h "$elf" | grep "Machine: *$machine" > /dev/null && readelf -h "$elf" | grep "Type: *EXEC" > /dev/null
    check $? "$name.ELF: static ELF64 executable for $machine"
    entry="$(readelf -h "$elf" | sed -n 's/.*Entry point address: *0x0*//p')"
    start="$(nm "$elf" | awk '$3 == "_start" { sub(/^0+/, "", $1); print $1 }')"
    [ -n "$entry" ] && [ "$entry" = "$start" ]
    check $? "$name.ELF: entry is the SamOS crt0 _start (0x${entry:-none})"
    ! nm "$elf" | grep -E '__libc_start_main|GLIBC' > /dev/null
    check $? "$name.ELF: no glibc"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$tmp/initrd.img" "$elf" "::/$name.ELF"
done

# The shell lines SHTEST.ELF runs, from SHTEST.TXT (the command line
# holds 255 characters); each prints "shtest> LINE", the builtin's
# output and "shtest: status N". Two more come from argv.
printf '%s\n' help 'echo hi there' 'echo saved words > SC.TXT' 'cat SC.TXT' 'echo more >> SC.TXT' 'cat SC.TXT' \
    'write W.TXT one two' 'cat W.TXT' 'mkdir SCDIR' ls 'ls SCDIR' 'rm W.TXT' 'rmdir SCDIR' ls \
    'cat NOPE.TXT' date 'HELLO.ELF via shell' nosuch > "$tmp/SHTEST.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$tmp/initrd.img" "$tmp/SHTEST.TXT" ::/SHTEST.TXT
append="samos.run=HELLOSC.ELF one two;SHTEST.ELF echo from argv + echo second line;SHTEST.ELF @SHTEST.TXT;SHELLSC.ELF"

echo "$arch samcc boot"
boot_samos "$append" '^shtest> nosuch$' 'shtest: status 127' 'SamOs v1.0.0 - type help'
tr -d '\r' < "$tmp/boot.raw" > "$tmp/boot.log"
expect '(^|!|> )samos.run: loaded @:/HELLOSC.ELF$' "HELLOSC.ELF loaded"
# The programs share the console (BLANK.ELF's selftest too), so a line
# may start after another one's "!" or "> ".
expect '(^|!|> )hello: argc=3$' "HELLOSC.ELF: argc"
expect '(^|!|> )hello: argv\[1\]=one$' "HELLOSC.ELF: argv[1]"
expect '(^|!|> )hello: argv\[2\]=two$' "HELLOSC.ELF: argv[2]"
expect '(^|!|> )samos.run: loaded @:/SHTEST.ELF$' "SHTEST.ELF loaded"
expect '(^|!|> )  echo \[words\.\.\.\] \[> file \| >> file\]  print or write the words$' "help: the builtin table (struct array, function pointers)"
expect '(^|!|> )Quote words with spaces: echo "a b" .c d.$' "help: escaped quotes in a string"
expect '(^|!|> )hi there$' "echo"
expect_count '(^|!|> )saved words$' 2 "echo > file, then cat"
expect '(^|!|> )more$' "echo >> appends"
expect '(^|!|> )one two$' "write, then cat"
expect '(^|!|> )SCDIR +<DIR>$' "mkdir, ls shows the directory"
expect '(^|!|> )SC\.TXT +17$' "ls: the file and its size"
expect '(^|!|> )cat: NOPE\.TXT: ' "cat of a missing file fails with a reason"
expect '(^|!|> )([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} UTC|date: no clock)$' "date"
expect '(^|!|> )hello: argv\[2\]=shell$' "a program started by the shell"
expect '(^|!|> )HELLO\.ELF: exit status 0$' "the shell waited for it"
expect '(^|!|> )nosuch: command not found$' "an unknown command"
# 18 lines: cat NOPE.TXT fails, nosuch is 127, date fails without a
# clock (where the kernel reads none).
failed=1
grep -aE '(^|!|> )date: no clock$' "$tmp/boot.log" > /dev/null && failed=2
expect '(^|!|> )from argv$' "argv mode: first line"
expect '(^|!|> )second line$' "argv mode: second line"
expect_count 'shtest: status 0$' $((16 - failed + 1 + 2)) "the other lines succeeded"
expect_count 'shtest: status 1$' $failed "cat NOPE.TXT (and date with no clock) failed"
expect_count 'shtest: status 127$' 1 "nosuch: 127"
expect '(^|!|> )samos.run: loaded @:/SHELLSC.ELF$' "SHELLSC.ELF loaded"
expect_count 'SamOs v1\.0\.0 - type help$' 1 "SHELLSC.ELF prints the shell banner (the gcc SHELL.ELF is off the disk)"
expect_count 'KERNEL FAULT|KERNEL PANIC|USER FAULT|^exception:' 0 "no fault"
if [ $fails -ne 0 ]; then echo "---- serial ----"; cat "$tmp/boot.log"; fi
echo "$arch samcc arch test: $checks checks, $fails failures"
[ $fails -eq 0 ]
