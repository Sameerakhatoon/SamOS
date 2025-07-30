#!/usr/bin/env bash
# e2e/249-net-router-mtu.sh - a router fragments what does not
# fit its next link, or with DF answers Fragmentation Needed. The
# layout of 232:
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# with the b to c link at MTU 1000 (samos.mtu.eth1=1000 on b,
# samos.mtu=1000 on c). a's PING.ELF -s 1400 to c (1428-byte
# datagrams, DF clear): b splits each in two, c puts them together and
# answers with its own fragments; the replies come back with the 1400
# bytes as sent. PING.ELF -D -s 1400: b drops it and answers
# Fragmentation Needed with the next-hop MTU 1000 (RFC 1191), which
# PING.ELF prints. PING.ELF -D -s 900 fits and is answered. b's route
# counts the fragmented and the refused datagrams, c's the reassembled
# ones. x86 pc, aarch64 and riscv64 virt.
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
    shell_cmd "PING.ELF -s 1400 $R3_IP_C 3" 60
    shell_cmd "PING.ELF -D -s 1400 $R3_IP_C 1" 30
    shell_cmd "PING.ELF -D -s 900 $R3_IP_C 2" 30
    shell_cmd 'route'
    touch "$E2E_TMP/a.done"
    printf 'quit\n'
}

tag=pci
net_router3 pci $tag "samos.mtu.eth1=1000" "samos.mtu=1000 samos.route=10.1.0.0/24@$R3_IP_B1" ""
r3_guests_ok $tag
rb0=$(net_re $R3_IP_B0)
rc=$(net_re $R3_IP_C)

net_on b
expect_serial '^net: eth1 mtu 1000$' "$tag/b: eth1's MTU 1000"
expect_serial '^IP forwarding on: [0-9]+ forwarded, .* [1-9][0-9]* fragmented, [1-9][0-9]* frag needed, ' \
    "$tag/b: route counts fragmented datagrams and a Fragmentation Needed"
n=$(r3_values b 's/^IP forwarding on: .* \([0-9]*\) fragmented,.*/\1/p' | tail -n 1)
[ "${n:-0}" -ge 2 ] || e2e_fail "$tag/b: ${n:-no} datagrams fragmented, expected at least 2"
e2e_log "$tag/b: $n datagrams fragmented"

net_on c
expect_serial '^net: eth0 mtu 1000$' "$tag/c: eth0's MTU 1000 (samos.mtu)"
n=$(r3_values c 's/^IP reassembly: \([0-9]*\) reassembled.*/\1/p' | tail -n 1)
[ "${n:-0}" -ge 2 ] || e2e_fail "$tag/c: ${n:-no} datagrams reassembled, expected at least 2"
e2e_log "$tag/c: $n datagrams reassembled"

net_on a
expect_serial "^ping: reply from $rc seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms ttl=63 size=1400 data ok\$" \
    "$tag/a: 1400 bytes through the MTU 1000 link and back, as sent"
n=$(r3_values a "s/^ping: $rc 3 sent, \\([0-9]*\\) received\$/\\1/p" | head -n 1)
[ "${n:-0}" -ge 2 ] || e2e_fail "$tag/a: ${n:-no} replies to PING.ELF -s 1400, expected at least 2"
expect_no_serial 'data BAD' "$tag/a: no reply with other data"
expect_serial "^ping: frag needed from $rb0 seq=1 mtu=1000\$" "$tag/a: DF, 1428 bytes: Fragmentation Needed from b, MTU 1000"
expect_serial "^ping: reply from $rc seq=[0-9]+ .* ttl=63 size=900 data ok\$" "$tag/a: DF, 928 bytes: answered"
echo "PASS: e2e/249 a router fragments for a smaller link, answers Fragmentation Needed (MTU 1000) for DF, and the pings come back whole"
