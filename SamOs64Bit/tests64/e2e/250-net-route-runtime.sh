#!/usr/bin/env bash
# e2e/250-net-route-runtime.sh - routes added and deleted at run
# time (syscalls 76 and 77, the shell's route add and route del). The
# layout of 232:
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# but c boots with no route to a's subnet. From c's shell: ping a gets
# nothing (no route); route add 10.1.0.0/24 10.2.0.1, and route shows
# it and the ping is answered; route del 10.1.0.0/24, and it is not
# again; route add default 10.2.0.1 answers it once more; route del
# default. Then the refusals: a gateway on no connected subnet, a
# connected subnet's route, a route that is not there. x86 pc, aarch64
# and riscv64 virt.
# e2e-requires: net virtio-pci (two virtio-net-pci NICs on the router)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
. "$(dirname "$0")/_net_route_lib.sh"

_r3_b_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    _net_wait_file c.done c.gone
    shell_cmd 'route'
    printf 'quit\n'
}
_r3_c_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    touch "$E2E_TMP/c.up"
    _net_wait_file a.up a.gone
    shell_cmd "ping $R3_IP_A 1" 30
    shell_cmd "route add 10.1.0.0/24 $R3_IP_B1"
    shell_cmd 'route'
    shell_cmd "ping $R3_IP_A 3" 60
    shell_cmd 'route del 10.1.0.0/24'
    shell_cmd "ping $R3_IP_A 1" 30
    shell_cmd "route add default $R3_IP_B1"
    shell_cmd "ping $R3_IP_A 2" 30
    shell_cmd 'route del default'
    shell_cmd 'route add 10.9.0.0/24 10.7.0.1'
    shell_cmd "route add 10.2.0.0/24 $R3_IP_B1"
    shell_cmd 'route del 10.9.0.0/24'
    shell_cmd 'route del 10.2.0.0/24'
    shell_cmd 'route'
    touch "$E2E_TMP/c.done"
    printf 'quit\n'
}
_r3_a_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    touch "$E2E_TMP/a.up"
    _net_wait_file c.done c.gone
    printf 'quit\n'
}

tag=pci
net_router3 pci $tag "" "" ""
r3_guests_ok $tag
ra=$(net_re $R3_IP_A)
rb1=$(net_re $R3_IP_B1)

net_on c
expect_no_serial '^net: route ' "$tag/c: no route to a's subnet at boot"
expect_serial "^route: added 10\\.1\\.0\\.0/24 via $rb1\$" "$tag/c: route add"
expect_serial "10\\.1\\.0\\.0 +24 +$rb1 +eth0" "$tag/c: route shows 10.1.0.0/24 via b on eth0"
expect_serial '^route: deleted 10\.1\.0\.0/24$' "$tag/c: route del"
expect_serial "^route: added default via $rb1\$" "$tag/c: route add default"
expect_serial '^route: deleted default$' "$tag/c: route del default"
expect_serial '^route: 10\.7\.0\.1 is not on a connected subnet$' "$tag/c: a gateway on no subnet refused"
expect_serial '^route: 10\.2\.0\.0/24 is a connected subnet$' "$tag/c: a connected subnet's route not replaced"
expect_serial '^route: no route 10\.9\.0\.0/24$' "$tag/c: deleting a route that is not there"
expect_serial '^route: no route 10\.2\.0\.0/24$' "$tag/c: a connected subnet's route not deleted"
got=$(r3_values c "s/^$ra: [0-9]* sent, \\([0-9]*\\) received\$/\\1/p" | tr '\n' ' ')
e2e_log "$tag/c: echo replies from a per ping: $got"
set -- $got
[ "$#" -eq 4 ] || e2e_fail "$tag/c: $# pings of a finished, expected 4"
[ "$1" -eq 0 ] || e2e_fail "$tag/c: $1 replies before route add, expected 0"
[ "$2" -ge 2 ] || e2e_fail "$tag/c: $2 replies after route add, expected at least 2"
[ "$3" -eq 0 ] || e2e_fail "$tag/c: $3 replies after route del, expected 0"
[ "$4" -ge 1 ] || e2e_fail "$tag/c: $4 replies with the default route, expected at least 1"
# The last table: no 10.1.0.0/24 and no default route left.
last=$(tr -d '\r' < "$E2E_TMP/c.serial" | awk '/^L3 Routing Table:/ { t = "" } { t = t $0 "\n" } END { printf "%s", t }')
printf '%s' "$last" | grep -qE '^[[:space:]]*(10\.1\.0\.0|0\.0\.0\.0) ' && e2e_fail "$tag/c: a deleted route is still in the table"

net_on b
expect_serial '^IP forwarding on: [1-9][0-9]* forwarded' "$tag/b: forwarded c's pings and a's replies"
echo "PASS: e2e/250 route add and route del change what c reaches through b at run time, and the bad requests are refused"
