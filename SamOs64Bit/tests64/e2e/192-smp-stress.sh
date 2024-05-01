#!/usr/bin/env bash
# e2e/192-smp-stress.sh - done criterion "a stress test (1000
# process create/exit cycles plus parallel file reads) runs 10 times
# with no hang, corruption or leak".
#
# From the shell (so nothing else allocates while it runs; the shell
# waits in waitpid): KTEST.ELF stress 10 1000 3 with BLANK.ELF and
# SHELL.ELF and their FNV-1a hashes, computed here from the image.
# Each round, on four CPUs at once:
#   - four spawners run 250 create/exit cycles each (1000 in all):
#     start "ktest quiet CODE", waitpid, the status must be CODE;
#   - three readers read both files 20 times, every read hashed and
#     compared with the host's hash.
# After each round the heap's frames in use, slab objects in use and
# rejected frees must equal the baseline taken after a warm-up round
# (which fills the sector cache and grows the process table once).
#   hang:       ten "STRESS round N ok" lines and the shell prompt
#               after them, within the time limit;
#   corruption: no child status or hash mismatch ("FAIL");
#   leak:       "frames 0 slab 0 bad frees 0" in every round line.
# e2e-requires: smp (four CPUs; aarch64 starts its secondary CPUs in the aarch64 SMP work)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

export E2E_SMP=4
t="$E2E_TMP/files"
mkdir -p "$t"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" ::/BLANK.ELF ::/SHELL.ELF "$t/" \
    || e2e_fail "mcopy the programs out of the image"
fnv() {
    python3 -c 'import sys
h = 2166136261
for b in open(sys.argv[1], "rb").read():
    h = ((h ^ b) * 16777619) & 0xffffffff
print("%x" % h)' "$1"
}
hb=$(fnv "$t/BLANK.ELF")
hs=$(fnv "$t/SHELL.ELF")
cmd="ktest.elf stress 10 1000 3 blank.elf $hb shell.elf $hs"
e2e_log "command: $cmd"

_script() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd "$cmd" 2400
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 2700 _script

expect_serial '^smp: 4 cpus online$' "four CPUs online"
log=$(tr -d '\r' < "$E2E_SERIAL")
for r in 1 2 3 4 5 6 7 8 9 10; do
    line=$(printf '%s\n' "$log" | grep -E "^STRESS round $r " | head -n 1)
    [ -n "$line" ] || e2e_fail "round $r never finished (hang?)"
    printf '%s\n' "$line" | grep -E "^STRESS round $r ok cycles 1000 reads 60 ms [0-9]+ frames 0 slab 0 bad frees 0$" > /dev/null \
        || e2e_fail "round $r: $line"
    e2e_log "$line: ok"
done
expect_serial '^STRESS OK 10 rounds in [0-9]+ ms$' "ten rounds"
expect_serial '^ktest\.elf: exit status 0$' "ktest exited 0 to the shell"
off=$(serial_offset 'STRESS OK 10 rounds')
[ "$off" -ge 0 ] || e2e_fail "no STRESS OK line"
tail -c +"$((off + 1))" "$E2E_SERIAL" | tr -d '\r' | grep '^> ' > /dev/null || e2e_fail "no shell prompt after the stress test"
e2e_log "shell prompt back after the stress test: ok"
expect_no_serial 'STRESS .*FAIL|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no failure or fault"
echo "PASS: e2e/192 stress: 10 x (1000 create/exit cycles + parallel reads), no hang, corruption or leak"
