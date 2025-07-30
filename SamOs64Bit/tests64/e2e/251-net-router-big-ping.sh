#!/usr/bin/env bash
# e2e/251-net-router-big-ping.sh - IP reassembly past 1600 bytes
# (the socket limit), through a router. The layout of 232:
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# a's PING.ELF -s 8000 and -s 30000 to c: a sends each request in 1480-
# byte fragments (6 and 21), b forwards them, c puts each together and
# answers with as many; a puts the reply together and PING.ELF checks
# its 8000 or 30000 bytes against what it sent. c's and a's route show
# the datagrams reassembled and none in progress. x86 pc, aarch64 and
# riscv64 virt.
# e2e-requires: net virtio-pci (two virtio-net-pci NICs on the router)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
. "$(dirname "$0")/_net_route_lib.sh"

_r3_b_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    _net_wait_file a.done a.gone
    shell_cmd 'route'
    printf 'quit\n'
}
_r3_c_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    touch "$E2E_TMP/c.up"
    _net_wait_file a.done a.gone
    shell_cmd 'route'
    printf 'quit\n'
}
_r3_a_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    shell_cmd "PING.ELF $R3_IP_C 1" 30
    shell_cmd "PING.ELF -s 8000 $R3_IP_C 3" 60
    shell_cmd "PING.ELF -s 30000 $R3_IP_C 3" 90
    shell_cmd 'route'
    touch "$E2E_TMP/a.done"
    printf 'quit\n'
}

tag=pci
net_router3 pci $tag "" "samos.route=10.1.0.0/24@$R3_IP_B1" ""
r3_guests_ok $tag
rc=$(net_re $R3_IP_C)

net_on a
expect_serial "^ping: reply from $rc seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms ttl=63 size=8000 data ok\$" \
    "$tag/a: an 8000-byte echo, reassembled at c and at a, data as sent"
expect_serial "^ping: reply from $rc seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms ttl=63 size=30000 data ok\$" \
    "$tag/a: a 30000-byte echo, reassembled at c and at a, data as sent"
expect_no_serial 'data BAD' "$tag/a: no reply with other data"
got=$(r3_values a "s/^ping: $rc 3 sent, \\([0-9]*\\) received\$/\\1/p" | tr '\n' ' ')
e2e_log "$tag/a: replies to -s 8000 and -s 30000: $got"
set -- $got
[ "$#" -eq 2 ] && [ "$1" -ge 2 ] && [ "$2" -ge 2 ] || e2e_fail "$tag/a: replies $got, expected at least 2 of 3 each"
n=$(r3_values a 's/^IP reassembly: \([0-9]*\) reassembled.*/\1/p' | tail -n 1)
[ "${n:-0}" -ge 4 ] || e2e_fail "$tag/a: ${n:-no} datagrams reassembled, expected at least 4"
expect_serial '^IP reassembly: [0-9]+ reassembled, [0-9]+ timed out, 0 dropped, ' "$tag/a: no set dropped as malformed or for room"

net_on c
n=$(r3_values c 's/^IP reassembly: \([0-9]*\) reassembled.*/\1/p' | tail -n 1)
[ "${n:-0}" -ge 4 ] || e2e_fail "$tag/c: ${n:-no} datagrams reassembled, expected at least 4"
e2e_log "$tag/c: $n datagrams reassembled"

net_on b
expect_serial '^IP forwarding on: [1-9][0-9]* forwarded' "$tag/b: forwarded the fragments"
expect_serial '^IP reassembly: 0 reassembled' "$tag/b: the router forwards fragments, it does not reassemble them"
echo "PASS: e2e/251 8000- and 30000-byte pings through a router, reassembled at both ends with the data as sent"
