#!/usr/bin/env bash
# e2e/240-net-httpd-poll.sh - one HTTPD.ELF process serves several
# downloads at once. Two guests on net_host.py's hub: a 10.0.2.15 runs
# a single "HTTPD.ELF 80 4" (poll over non-blocking sockets); b
# 10.0.2.16 boots with four WGET.ELF processes, each fetching a's 4 MiB
# /big4096. All four arrive byte for byte, a's one httpd logs the four,
# and the hub's flow times show the transfers overlapping (the socket work
# httpd served one connection at a time: at most one would carry data).
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
clients=4
kib=4096
net_hub 2
net_host big "$E2E_TMP/big.expect" $((kib * 1024))
big=$(net_host fnv "$E2E_TMP/big.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
run=
for ((k = 0; k < clients; k++)); do
    run="$run;WGET.ELF $ip_a 80 /big$kib"
done
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80 $clients"
net_guest_image b "samos.ip=$ip_b/24 samos.run=${run#;}"

_mon_a() {
    shell_boot || { net_flag a.done; printf 'quit\n'; return 0; }
    net_flag a.up
    net_flags_wait $((limit * 8)) b.done
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.done; printf 'quit\n'; return 0; }
    local t=0
    while [ $t -lt $((limit * 8)) ] && [ "$(net_count b 'wget: body ')" -lt "$clients" ]; do
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
if E2E_SERIAL="$E2E_TMP/a.serial" e2e_wait_serial '^httpd: listening on port 80$' $((limit * 3)); then
    net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 12)) _mon_b
else
    net_flag b.done
fi
wait "$pa"
net_hub_stop
e2e_log "hub: $(grep '^hub: frames' "$NET_HUB_LOG")"

ra=$(net_re "$ip_a"); rb=$(net_re "$ip_b")
for k in 0 1; do
    n=$([ $k = 0 ] && echo a || echo b)
    net_on "$n"
    net_nic_check pci "httpd-poll/$n" "$(net_ip "$k")"
    net_expect_no_fault "httpd-poll/$n"
done

net_on a
[ "$(net_count a '^httpd: listening on port 80$')" -eq 1 ] || e2e_fail "httpd-poll/a: not one httpd process"
got=$(net_count a "httpd: GET /big$kib from $rb:[0-9]+, 200, $((kib * 1024)) bytes\$")
[ "$got" -eq "$clients" ] || e2e_fail "httpd-poll/a: httpd logged $got of $clients /big$kib responses"
e2e_log "httpd-poll/a: one httpd served $clients /big$kib"
net_on b
got=$(net_count b "wget: body $((kib * 1024)) bytes, fnv1a $big\$")
[ "$got" -eq "$clients" ] || e2e_fail "httpd-poll/b: $got of $clients /big$kib arrived whole"
e2e_log "httpd-poll/b: $clients x /big$kib whole (FNV-1a $big)"

grep -E "^hub: flow $ra:80 > $rb:" "$NET_HUB_LOG" | sed 's/^/    /' >&2
ov=$(net_hub_overlap "^hub: flow $ra:80 > ")
[ "$ov" -ge 3 ] || e2e_fail "httpd-poll: the $clients transfers did not overlap (at most $ov at once)"
e2e_log "httpd-poll: up to $ov of the $clients transfers at once"
echo "PASS: e2e/240 one HTTPD.ELF process served $clients concurrent 4 MiB downloads (up to $ov at once)"
