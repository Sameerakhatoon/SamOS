#!/bin/bash
# tests/36-userland-prologue.sh
#
# Ch 95+96+97 - check that the userland transition asm and the first
# user program are in place. Three checks (all static, no QEMU run):
#   1. blank.bin exists in the FAT16 volume at /blank.bin.
#   2. The kernel.bin has the task_return/user_registers/
#      restore_general_purpose_registers symbols (via i686-elf-nm on
#      kernelfull.o).
#   3. blank.bin's entry-point is at virtual 0x400000 per the linker
#      script - the first 2 bytes are EB FE (jmp short -2 = jmp $).
#
# These don't prove userland boots; they prove the build pipeline
# produced the right artifacts. End-to-end userland-execution checks
# arrive once the kernel actually calls task_run_first_ever_task.

set -e
cd "$(dirname "$0")/.."

export PATH="$HOME/opt/cross/bin:$PATH"

./build.sh > /dev/null

ok=1

mdir -i bin/os.bin ::/blank.bin > /dev/null 2>&1 \
    || { echo "FAIL: /blank.bin not present in FAT16 volume"; ok=0; }

nm_out=$(i686-elf-nm build/kernelfull.o 2>/dev/null)
echo "$nm_out" | grep -q ' task_return$'                       || { echo "FAIL: task_return symbol missing"; ok=0; }
echo "$nm_out" | grep -q ' user_registers$'                    || { echo "FAIL: user_registers symbol missing"; ok=0; }
echo "$nm_out" | grep -q ' restore_general_purpose_registers$' || { echo "FAIL: restore_general_purpose_registers symbol missing"; ok=0; }
echo "$nm_out" | grep -q ' task_run_first_ever_task$'          || { echo "FAIL: task_run_first_ever_task symbol missing"; ok=0; }

# blank.bin entry sequence (Ch 107):
#   push 20        ; 6A 14
#   push 30        ; 6A 1E
#   mov eax, 0     ; B8 00 00 00 00
#   int 0x80       ; CD 80
#   add esp, 8     ; 83 C4 08
#   jmp $          ; EB FE
expected="6a146a1eb800000000cd8083c408ebfe"
got=$(od -An -tx1 -N 16 programs/blank/blank.bin | tr -d ' \n')
[ "$got" = "$expected" ] || { echo "FAIL: blank.bin first 16 bytes = $got (expected $expected)"; ok=0; }

[ $ok -eq 1 ] || exit 1
exit 0
