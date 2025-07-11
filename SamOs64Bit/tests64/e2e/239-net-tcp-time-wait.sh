#!/usr/bin/env bash
# e2e/239-net-tcp-time-wait.sh - the side that closes first
# waits in TIME_WAIT, then lets go. a 10.0.2.15 boots with
# samos.tcp_time_wait_ms=6000 and runs HTTPD.ELF, which closes each
# connection after its response; b 10.0.2.16 fetches / three times in a
# row with WGET.ELF (new ports, so TIME_WAIT on a never blocks them).
# a's netstat right after: its side of those connections in TIME_WAIT,
# no socket held for them; 6 s on: no TCB but the listener. b, which
# closed second (LAST_ACK, then CLOSED), keeps none.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
tw_ms=6000
net_hub 2
printf 'hello\n' > "$E2E_TMP/hello.expect"
hello=$(net_host fnv "$E2E_TMP/hello.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.tcp_time_wait_ms=$tw_ms samos.run=HTTPD.ELF 80"
net_guest_image b "samos.ip=$ip_b/24"

_mon_a() {
    shell_boot || { net_flag a.up; net_flag a.done; printf 'quit\n'; return 0; }
    shell_cmd 'netstat'
    net_flag a.up
    net_flags_wait $((limit * 3)) b.got
    shell_cmd 'netstat'
    sleep $((tw_ms / 1000 + 2))
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.got; printf 'quit\n'; return 0; }
    net_flags_wait $((limit * 3)) a.up
    shell_cmd "WGET.ELF $ip_a 80 / 3" $((limit * 2))
    net_flag b.got
    shell_cmd 'netstat'
    net_flags_wait $((limit * 2)) a.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 6)) _mon_a &
pa=$!
net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 6)) _mon_b
net_flag b.got
wait "$pa"
net_hub_stop

net_on b
net_nic_check pci tw/b "$ip_b"
net_expect_no_fault tw/b
expect_serial "wget: 3 of 3 fetches whole, body 6 bytes, fnv1a $hello\$" "tw/b: three fetches whole"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/{n;p}' | tail -n 1 | grep -E '^ *\(none\)$' > /dev/null \
    || e2e_fail "tw/b: the side that closed second kept a TCB"

net_on a
net_nic_check pci tw/a "$ip_a"
net_expect_no_fault tw/a
expect_serial "net: samos.tcp_time_wait_ms $tw_ms" "tw/a: the boot option was taken"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/,/sockets in use/p' | sed 's/^/    /' >&2
# netstat 2 (right after the fetches): TIME_WAIT, sockets at the baseline.
tw=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n++ } n == 2 && /state=TIME_WAIT / { c++ } END { print c + 0 }')
[ "$tw" -ge 1 ] || e2e_fail "tw/a: no TCB in TIME_WAIT right after the fetches"
e2e_log "tw/a: $tw TCBs in TIME_WAIT right after the fetches"
socks=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}sockets in use: \([0-9]*\) of 64$/\2/p')
base=$(printf '%s\n' "$socks" | sed -n 1p); mid=$(printf '%s\n' "$socks" | sed -n 2p)
[ -n "$base" ] && [ "$base" = "$mid" ] || e2e_fail "tw/a: sockets in use $base before, $mid with TCBs in TIME_WAIT"
# netstat 3: only the listener.
left=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n = 0; l = 0 } /^  \[[0-9]+\] state=/ { n++; if ($0 ~ /state=LISTEN /) l++ } END { print n - l }')
[ "$left" = 0 ] || e2e_fail "tw/a: $left TCBs besides the listener after the TIME_WAIT"
echo "PASS: e2e/239 the closing side waits in TIME_WAIT (samos.tcp_time_wait_ms), holding no socket, then frees the TCB"
