#!/usr/bin/env bash
# e2e/238-net-tcp-fast-retransmit.sh - loss repaired without
# whole retransmission timeouts. net_host.py's hub drops every 11th
# TCP frame each way between a 10.0.2.15 (HTTPD.ELF) and b 10.0.2.16,
# whose WGET.ELF fetches a's 256 KiB /big256. The body must be whole;
# the sender a must show fast retransmits (three duplicate ACKs, RFC
# 5681) and fewer retransmission timeouts than fast retransmits, the
# receiver b out-of-order segments kept (the duplicate ACKs come from
# those). Before the network follow-ups every loss cost a timeout and a go-back-N resend.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
kib=256
net_hub 2 11
net_host big "$E2E_TMP/big.expect" $((kib * 1024))
big=$(net_host fnv "$E2E_TMP/big.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80 1"
net_guest_image b "samos.ip=$ip_b/24"

_mon_a() {
    shell_boot || { net_flag a.up; net_flag a.done; printf 'quit\n'; return 0; }
    net_flag a.up
    net_flags_wait $((limit * 5)) b.got
    shell_cmd 'netstat'
    net_flag a.done
    printf 'quit\n'
}

_mon_b() {
    shell_boot || { net_flag b.got; printf 'quit\n'; return 0; }
    net_flags_wait $((limit * 3)) a.up
    shell_cmd "WGET.ELF $ip_a 80 /big$kib" $((limit * 5))
    shell_cmd 'netstat'
    net_flag b.got
    net_flags_wait $((limit * 2)) a.done
    printf 'quit\n'
}

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 10)) _mon_a &
pa=$!
net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 10)) _mon_b
net_flag b.got
wait "$pa"
net_hub_stop
e2e_log "$(grep '^hub: frames' "$NET_HUB_LOG")"
grep -E '^hub: dropped TCP frame [0-9]+ 0>1 ' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub dropped nothing a sent"

# The last value netstat printed for "<what>: N" in the current log.
_counter() {
    tr -d '\r' < "$E2E_SERIAL" | sed -n "s/^\\(> \\)\\{0,1\\}$1: \\([0-9]*\\)\$/\\2/p" | tail -n 1
}

net_on b
net_nic_check pci fast/b "$ip_b"
net_expect_no_fault fast/b
expect_serial "wget: body $((kib * 1024)) bytes, fnv1a $big\$" "fast/b: /big$kib arrived whole"
ooo=$(_counter 'TCP out-of-order segments kept')
[ "${ooo:-0}" -gt 0 ] || e2e_fail "fast/b: no out-of-order segment kept (${ooo:-none})"
e2e_log "fast/b: out-of-order segments kept $ooo"

net_on a
net_nic_check pci fast/a "$ip_a"
net_expect_no_fault fast/a
expect_serial "httpd: GET /big$kib from $(net_re "$ip_b"):[0-9]+, 200, $((kib * 1024)) bytes\$" "fast/a: served /big$kib"
rto=$(_counter 'TCP retransmission timeouts')
fast=$(_counter 'TCP fast retransmits')
e2e_log "fast/a: retransmission timeouts ${rto:-none}, fast retransmits ${fast:-none}"
[ "${fast:-0}" -gt 0 ] || e2e_fail "fast/a: no fast retransmit"
[ "${rto:-0}" -lt "${fast:-0}" ] || e2e_fail "fast/a: $rto timeouts, not fewer than the $fast fast retransmits"
echo "PASS: e2e/238 $kib KiB through a link dropping every 11th TCP frame: $fast fast retransmits, $rto timeouts"
