#!/usr/bin/env bash
# e2e/237-net-tcp-orphan.sh - a TCB in FIN_WAIT_2 whose peer
# never closes is freed after the orphan timeout. a 10.0.2.15 boots
# with samos.tcp_orphan_ms=8000 and runs HTTPD.ELF; b 10.0.2.16 fetches
# / with WGET.ELF. httpd closes first (FIN_WAIT_1, then FIN_WAIT_2 when
# b ACKs the FIN); b's wget reads to the end and closes, but the hub
# drops every FIN b sends for 14 s from the first, so a never hears it.
# a's netstat right after the fetch shows the TCB in FIN_WAIT_2; its
# netstat 8 s on shows only the listener and one orphan timed out. b's
# FIN, resent on its timer, gets through after the 14 s and finds no
# connection on a: a's RST closes b's LAST_ACK, and b ends with no TCB.
# Before the network follow-ups the orphan time was a fixed 60 s with no test (the multi-guest code's
# "Left").
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
orphan_ms=8000
fin_s=14
net_hub 2 0 0 0 "$fin_s" 1
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.tcp_orphan_ms=$orphan_ms samos.run=HTTPD.ELF 80"
net_guest_image b "samos.ip=$ip_b/24"

_mon_a() {
    shell_boot || { net_flag a.up; net_flag a.done; printf 'quit\n'; return 0; }
    net_flag a.up
    net_flags_wait $((limit * 3)) b.got
    shell_cmd 'netstat'
    net_flag a.seen
    # Past the orphan time, counted from httpd's close.
    sleep $((orphan_ms / 1000 + 2))
    shell_cmd 'netstat'
    net_flag a.done
    net_flags_wait $((limit * 2)) b.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.got; net_flag b.done; printf 'quit\n'; return 0; }
    net_flags_wait $((limit * 3)) a.up
    shell_cmd "WGET.ELF $ip_a 80 /" $((limit * 2))
    net_flag b.got
    net_flags_wait $((limit * 2)) a.done
    # Until the hub lets b's FIN through and a's RST closes b's TCB.
    sleep $((fin_s + 8))
    shell_cmd 'netstat'
    net_flag b.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 6)) _mon_a &
pa=$!
net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 6)) _mon_b
net_flag b.done
wait "$pa"
net_hub_stop
fins=$(grep -c '^hub: dropped FIN 1>0 ' "$NET_HUB_LOG")
e2e_log "hub: $fins FINs from b dropped"
[ "$fins" -ge 1 ] || e2e_fail "the hub dropped no FIN from b"

net_on b
net_nic_check pci orphan/b "$ip_b"
net_expect_no_fault orphan/b
expect_serial "wget: body 6 bytes, fnv1a " "orphan/b: the fetch worked"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/{n;p}' | tail -n 1 | grep -E '^ *\(none\)$' > /dev/null \
    || e2e_fail "orphan/b: a TCB left (a's RST never closed b's LAST_ACK): $(tr -d '\r' < "$E2E_SERIAL" | grep -a 'state=' | tail -n 2)"

net_on a
net_nic_check pci orphan/a "$ip_a"
net_expect_no_fault orphan/a
expect_serial "net: samos.tcp_orphan_ms $orphan_ms" "orphan/a: the boot option was taken"
expect_serial "httpd: request from $(net_re "$ip_b"):[0-9]+, 200, 89 bytes\$" "orphan/a: served the fetch"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/,/sockets in use/p' | sed 's/^/    /' >&2
# The first netstat: the connection in FIN_WAIT_2 (b's FIN dropped).
first=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n++ } n == 1 && /state=FIN_WAIT_2 / { c++ } END { print c + 0 }')
[ "$first" -ge 1 ] || e2e_fail "orphan/a: no TCB in FIN_WAIT_2 right after the fetch"
# The second: only the listener, one orphan timed out.
left=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n = 0; l = 0 } /^  \[[0-9]+\] state=/ { n++; if ($0 ~ /state=LISTEN /) l++ } END { print n - l }')
[ "$left" = 0 ] || e2e_fail "orphan/a: $left TCBs besides the listener after the orphan time"
orphans=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}TCP orphans timed out: \([0-9]*\)$/\2/p' | tail -n 1)
[ "${orphans:-0}" -ge 1 ] || e2e_fail "orphan/a: no orphan timed out (${orphans:-none})"
e2e_log "orphan/a: FIN_WAIT_2 seen, then freed; orphans timed out: $orphans"
echo "PASS: e2e/237 a FIN_WAIT_2 TCB whose peer never closes is freed after samos.tcp_orphan_ms"
