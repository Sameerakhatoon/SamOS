#!/usr/bin/env bash
# e2e/234-net-guests-many.sh - many TCP connections and a long
# transfer between two SamOS guests (net_host.py's hub, a clean link).
# a 10.0.2.15 runs HTTPD.ELF for ever; its netstat before any client
# is the baseline. Then b 10.0.2.16 boots with four WGET.ELF processes
# at once (three fetching / 10 times each, one /big 3 times: up to four
# connections open to the one HTTPD, its accept queue holding the
# others), then from its shell 60 more / one after another and one
# 2 MiB /big2048. Every fetch must be whole. Afterwards both netstats
# show the system-wide socket table (64 entries, fewer than the 94
# connections made) back at the baseline, and no TCB but a's listener.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
kib=2048
seq_n=60
net_hub 2
net_host big "$E2E_TMP/big.expect"
net_host big "$E2E_TMP/long.expect" $((kib * 1024))
printf 'hello\n' > "$E2E_TMP/hello.expect"
big=$(net_host fnv "$E2E_TMP/big.expect")
long=$(net_host fnv "$E2E_TMP/long.expect")
hello=$(net_host fnv "$E2E_TMP/hello.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80"
net_guest_image b "samos.ip=$ip_b/24 samos.run=WGET.ELF $ip_a 80 / 10;WGET.ELF $ip_a 80 / 10;WGET.ELF $ip_a 80 / 10;WGET.ELF $ip_a 80 /big 3"

# The four boot-time clients are done when four summaries are out.
_b_burst_done() {
    [ "$(net_count b 'wget: [0-9]+ of [0-9]+ fetches whole')" -ge 4 ]
}

_mon_a() {
    shell_boot || { net_flag a.base; net_flag a.done; printf 'quit\n'; return 0; }
    shell_cmd 'netstat'
    net_flag a.base
    net_flags_wait $((limit * 10)) b.done
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.done; printf 'quit\n'; return 0; }
    local t=0
    while [ $t -lt $((limit * 6)) ] && ! _b_burst_done; do
        sleep 0.5
        t=$((t + 1))
    done
    shell_cmd "WGET.ELF $ip_a 80 / $seq_n" $((limit * 5))
    shell_cmd "WGET.ELF $ip_a 80 /big$kib" $((limit * 5))
    # Each side frees a closed TCB on its next tick.
    sleep 2
    shell_cmd 'netstat'
    net_flag b.done
    net_flags_wait $((limit * 2)) a.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 20)) _mon_a &
pa=$!
if E2E_SERIAL="$E2E_TMP/a.serial" e2e_wait_serial '^httpd: listening on port 80$' $((limit * 3)) \
    && net_flags_wait $((limit * 3)) a.base; then
    net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 16)) _mon_b
else
    e2e_log "many: a's httpd never listened, b not started"
fi
net_flag b.done
wait "$pa"
net_hub_stop
e2e_log "$(grep '^hub: frames' "$NET_HUB_LOG")"

ra=$(net_re "$ip_a"); rb=$(net_re "$ip_b")
# Program lines are matched anywhere on a line (the shell echoes keys
# meanwhile).
net_on b
net_nic_check pci many/b "$ip_b"
net_expect_no_fault many/b
[ "$(net_count b "wget: 10 of 10 fetches whole, body 6 bytes, fnv1a $hello\$")" -eq 3 ] \
    || e2e_fail "many/b: not all three bursts of 10 / were whole: $(grep -a 'wget: ' "$E2E_SERIAL" | tr -d '\r' | head -n 20)"
expect_serial "wget: 3 of 3 fetches whole, body 65536 bytes, fnv1a $big\$" "many/b: the concurrent /big fetches were whole"
expect_serial "wget: $seq_n of $seq_n fetches whole, body 6 bytes, fnv1a $hello\$" "many/b: $seq_n sequential fetches were whole"
expect_serial "wget: body $((kib * 1024)) bytes, fnv1a $long\$" "many/b: /big$kib arrived whole ($((kib * 1024)) bytes, FNV-1a $long)"
expect_no_serial 'wget: fetch [0-9]+:' "many/b: no failed fetch"
sb=$(net_netstat_sockets b)
[ "$sb" = 0 ] || e2e_fail "many/b: ${sb:-no} sockets in use afterwards, expected 0"
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/,/sockets in use/p' | tail -n 6 | sed 's/^/    /' >&2
tr -d '\r' < "$E2E_SERIAL" | sed -n '/TCP TCBs : samos :/{n;p}' | tail -n 1 | grep -E '^ *\(none\)$' > /dev/null \
    || e2e_fail "many/b: TCBs left after the clients closed"

net_on a
net_nic_check pci many/a "$ip_a"
net_expect_no_fault many/a
served=$(net_count a "httpd: (request|GET /big[0-9]*) from $rb:[0-9]+, 200, ")
total=$((3 * 10 + 3 + seq_n + 1))
[ "$served" -eq "$total" ] || e2e_fail "many/a: httpd served $served requests, expected $total"
e2e_log "many/a: httpd served $served connections from b"
expect_serial "httpd: GET /big$kib from $rb:[0-9]+, 200, $((kib * 1024)) bytes\$" "many/a: served /big$kib"
base=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}sockets in use: \([0-9]*\) of 64$/\2/p' | head -n 1)
after=$(net_netstat_sockets a)
[ -n "$base" ] && [ "$base" = "$after" ] || e2e_fail "many/a: sockets in use ${base:-?} before, ${after:-?} after"
e2e_log "many/a: sockets in use $base before and after"
left=$(tr -d '\r' < "$E2E_SERIAL" | awk '/TCP TCBs : samos :/ { n = 0; l = 0 } /^  \[[0-9]+\] state=/ { n++; if ($0 ~ /state=LISTEN /) l++ } END { print n - l }')
[ "$left" = 0 ] || e2e_fail "many/a: $left TCBs besides the listener afterwards"

ov=$(net_hub_overlap "^hub: flow ($ra:80 > $rb|$rb:[0-9]+ > $ra:80 )")
[ "${ov:-0}" -ge 2 ] || e2e_fail "many: never two connections to a's httpd at once (at most ${ov:-0})"
e2e_log "many: up to $ov connections to a's httpd carried data at once"
echo "PASS: e2e/234 $total connections to one HTTPD.ELF (up to $ov at once) and a 2 MiB transfer, sockets back to the baseline"
