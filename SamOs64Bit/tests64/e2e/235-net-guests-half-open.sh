#!/usr/bin/env bash
# e2e/235-net-guests-half-open.sh - a connection that never
# completes leaves nothing behind on the server. net_host.py's hub
# drops every SYN-ACK for 26 s from the first. b 10.0.2.16 runs
# WGET.ELF against a 10.0.2.15's HTTPD.ELF: a's passive TCB sits in
# SYN_RECEIVED resending its SYN-ACK, b's connect gives up after 20 s
# and its TCB is gone, so the next SYN-ACK to get through is answered
# with a RST, which closes a's TCB. A second WGET.ELF then works. Both
# netstats at the end: no TCB but a's listener, sockets back to the
# baseline (the closed passive TCB was never freed).
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
net_hub 2 0 0 26
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80"
net_guest_image b "samos.ip=$ip_b/24"

_mon_a() {
    shell_boot || { net_flag a.up; net_flag a.done; printf 'quit\n'; return 0; }
    shell_cmd 'netstat'
    net_flag a.up
    net_flags_wait $((limit * 6)) b.done
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.done; printf 'quit\n'; return 0; }
    net_flags_wait $((limit * 3)) a.up
    shell_cmd "WGET.ELF $ip_a 80 /" $((limit * 2))
    # Until the hub lets a's SYN-ACK through and b answers it.
    E2E_SERIAL="$E2E_TMP/hub.log" e2e_wait_serial '^hub: dropped SYN-ACK' 10
    sleep 20
    shell_cmd "WGET.ELF $ip_a 80 /" $((limit * 2))
    sleep 3
    shell_cmd 'netstat'
    net_flag b.done
    net_flags_wait $((limit * 2)) a.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 10)) _mon_a &
pa=$!
net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 10)) _mon_b
net_flag b.done
wait "$pa"
net_hub_stop
e2e_log "$(grep -c '^hub: dropped SYN-ACK' "$NET_HUB_LOG") SYN-ACKs dropped"

net_on b
net_nic_check pci half/b "$ip_b"
net_expect_no_fault half/b
expect_serial "wget: connect $(net_re "$ip_a"):80 failed \\(-[0-9]+\\)" "half/b: the first connect gave up"
expect_serial "wget: body 6 bytes, fnv1a " "half/b: the second fetch worked"
[ "$(net_netstat_sockets b)" = 0 ] || e2e_fail "half/b: sockets left in use"

net_on a
net_nic_check pci half/a "$ip_a"
net_expect_no_fault half/a
expect_serial "httpd: request from $(net_re "$ip_b"):[0-9]+, 200, 89 bytes\$" "half/a: served the second fetch"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/,/sockets in use/p' | tail -n 6 | sed 's/^/    /' >&2
base=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}sockets in use: \([0-9]*\) of 64$/\2/p' | head -n 1)
after=$(net_netstat_sockets a)
[ -n "$base" ] && [ "$base" = "$after" ] || e2e_fail "half/a: sockets in use ${base:-?} before, ${after:-?} after"
left=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n = 0; l = 0 } /^  \[[0-9]+\] state=/ { n++; if ($0 ~ /state=LISTEN /) l++ } END { print n - l }')
[ "$left" = 0 ] || e2e_fail "half/a: $left TCBs besides the listener afterwards (the half-open one was not freed)"
echo "PASS: e2e/235 a connection reset in SYN_RECEIVED is freed; the server's TCBs and sockets are back at the baseline"
