#!/usr/bin/env bash
# e2e/236-net-guests-backlog.sh - more clients at once than the
# server's accept queue holds. a 10.0.2.15 runs HTTPD.ELF (listen
# backlog 4); b 10.0.2.16 boots with eight WGET.ELF processes, each
# fetching / five times, all started together. Every one of the 40
# fetches must be whole, and both netstats end at the baseline.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
clients=8
each=5
net_hub 2
printf 'hello\n' > "$E2E_TMP/hello.expect"
hello=$(net_host fnv "$E2E_TMP/hello.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
run=
for ((k = 0; k < clients; k++)); do
    run="$run;WGET.ELF $ip_a 80 / $each"
done
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80"
net_guest_image b "samos.ip=$ip_b/24 samos.run=${run#;}"

_b_done() {
    [ "$(net_count b 'wget: [0-9]+ of [0-9]+ fetches whole')" -ge "$clients" ]
}

_mon_a() {
    shell_boot || { net_flag a.base; net_flag a.done; printf 'quit\n'; return 0; }
    shell_cmd 'netstat'
    net_flag a.base
    net_flags_wait $((limit * 8)) b.done
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.done; printf 'quit\n'; return 0; }
    local t=0
    while [ $t -lt $((limit * 8)) ] && ! _b_done; do
        sleep 0.5
        t=$((t + 1))
    done
    sleep 2
    shell_cmd 'netstat'
    net_flag b.done
    net_flags_wait $((limit * 2)) a.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 14)) _mon_a &
pa=$!
if E2E_SERIAL="$E2E_TMP/a.serial" e2e_wait_serial '^httpd: listening on port 80$' $((limit * 3)) \
    && net_flags_wait $((limit * 3)) a.base; then
    net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 12)) _mon_b
else
    e2e_log "backlog: a's httpd never listened, b not started"
fi
net_flag b.done
wait "$pa"
net_hub_stop

net_on b
net_nic_check pci backlog/b "$ip_b"
net_expect_no_fault backlog/b
tr -d '\r' < "$E2E_SERIAL" | grep -aE 'wget: (fetch|[0-9]+ of)' | sed 's/^/    /' >&2
whole=$(net_count b "wget: $each of $each fetches whole, body 6 bytes, fnv1a $hello\$")
[ "$whole" -eq "$clients" ] || e2e_fail "backlog/b: $whole of $clients clients had all $each fetches whole"
[ "$(net_netstat_sockets b)" = 0 ] || e2e_fail "backlog/b: sockets left in use"

net_on a
net_nic_check pci backlog/a "$ip_a"
net_expect_no_fault backlog/a
served=$(net_count a "httpd: request from $(net_re "$ip_b"):[0-9]+, 200, 89 bytes")
[ "$served" -eq $((clients * each)) ] || e2e_fail "backlog/a: httpd served $served, expected $((clients * each))"
base=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}sockets in use: \([0-9]*\) of 64$/\2/p' | head -n 1)
after=$(net_netstat_sockets a)
[ -n "$base" ] && [ "$base" = "$after" ] || e2e_fail "backlog/a: sockets in use ${base:-?} before, ${after:-?} after"
left=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n = 0; l = 0 } /^  \[[0-9]+\] state=/ { n++; if ($0 ~ /state=LISTEN /) l++ } END { print n - l }')
[ "$left" = 0 ] || e2e_fail "backlog/a: $left TCBs besides the listener afterwards"
echo "PASS: e2e/236 $clients clients at once against a backlog of 4: all $((clients * each)) fetches whole, sockets back to the baseline"
