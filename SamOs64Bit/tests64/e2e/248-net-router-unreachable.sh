#!/usr/bin/env bash
# e2e/248-net-router-unreachable.sh - a router with no route
# says so, and a traceroute through it. The layout of 232 (three
# QEMUs, b routes between a's subnet and c's):
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# b has no default route. a pings 10.3.0.9 with PING.ELF: b answers
# Destination Unreachable (net unreachable, code 0), which PING.ELF
# prints with b's address. TRACERT.ELF 10.2.0.2 on a: hop 1 is b
# (Time Exceeded from 10.1.0.1), hop 2 is c (the echo reply), reached
# in 2 hops; TRACERT.ELF 10.3.0.9 ends at hop 1 with b's unreachable.
# b's route shows the no-route datagrams, the unreachables it sent and
# the TTL expiries of the traceroute. x86 pc, aarch64 and riscv64 virt.
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
    printf 'quit\n'
}
_r3_a_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    shell_cmd "PING.ELF $R3_IP_C 1" 30
    shell_cmd 'PING.ELF 10.3.0.9 2' 30
    shell_cmd "TRACERT.ELF $R3_IP_C" 90
    shell_cmd 'TRACERT.ELF 10.3.0.9' 60
    touch "$E2E_TMP/a.done"
    printf 'quit\n'
}

tag=pci
net_router3 pci $tag "" "samos.route=10.1.0.0/24@$R3_IP_B1" ""
r3_guests_ok $tag
rb0=$(net_re $R3_IP_B0)
rc=$(net_re $R3_IP_C)

net_on a
expect_serial "^ping: reply from $rc seq=1 .* ttl=63\$" "$tag/a: c answers through b"
expect_serial "^ping: unreachable from $rb0 seq=1 code=0\$" "$tag/a: b's net unreachable for 10.3.0.9, first ping"
expect_serial "^ping: unreachable from $rb0 seq=2 code=0\$" "$tag/a: and the second"
expect_serial '^ping: 10\.3\.0\.9 2 sent, 0 received$' "$tag/a: no echo reply from 10.3.0.9"
expect_serial "^tracert: to $rc, 8 hops max\$" "$tag/a: TRACERT.ELF started"
expect_serial "^tracert: 1 $rb0 [0-9]+\\.[0-9]{3} ms\$" "$tag/a: hop 1 is b"
expect_serial "^tracert: 2 $rc [0-9]+\\.[0-9]{3} ms reached\$" "$tag/a: hop 2 is c"
expect_serial "^tracert: $rc reached in 2 hops\$" "$tag/a: c reached in 2 hops"
expect_serial "^tracert: 1 $rb0 unreachable code=0\$" "$tag/a: the trace to 10.3.0.9 ends at b"
expect_no_serial '^tracert: [0-9]+ \*$' "$tag/a: every hop answered"

net_on b
expect_serial '^IP forwarding on: [0-9]+ forwarded, [1-9][0-9]* TTL expired, 0 refused, [1-9][0-9]* no route, [1-9][0-9]* unreachable sent, ' \
    "$tag/b: route counts the TTL expiry, the no-route datagrams and the unreachables"
n=$(r3_values b 's/^IP forwarding on: .* \([0-9]*\) unreachable sent.*/\1/p' | tail -n 1)
[ "${n:-0}" -ge 3 ] || e2e_fail "$tag/b: ${n:-no} unreachables sent, expected at least 3"
e2e_log "$tag/b: $n unreachables sent"
echo "PASS: e2e/248 a router with no route answers Destination Unreachable (net), and TRACERT.ELF finds b then c"
