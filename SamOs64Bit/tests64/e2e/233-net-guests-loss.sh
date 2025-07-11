#!/usr/bin/env bash
# e2e/233-net-guests-loss.sh - TCP between two SamOS guests over a
# bad link. net_host.py's hub joins them and, in each direction, drops
# every 7th TCP frame and holds back every 5th until the next one has
# gone by (or 40 ms), so it arrives out of order. a 10.0.2.15 and b
# 10.0.2.16 each run HTTPD.ELF; at the same time a fetches b's /big and
# b fetches a's, 64 KiB each way, both byte for byte. Each SamOS is
# sender and receiver at once, and SamOS now retransmits to SamOS:
# netstat shows retransmission timeouts on both.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
net_hub 2 7 5
net_host big "$E2E_TMP/big.expect"
big=$(net_host fnv "$E2E_TMP/big.expect")
ip_a=$(net_ip 0); ip_b=$(net_ip 1)
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80 1"
net_guest_image b "samos.ip=$ip_b/24 samos.run=HTTPD.ELF 80 1"

_loss_mon() {
    local me=$1 peer=$2
    shell_boot || { net_flag "$me.up"; net_flag "$me.done"; printf 'quit\n'; return 0; }
    net_flag "$me.up"
    net_flags_wait $((limit * 3)) a.up b.up
    shell_cmd "WGET.ELF $peer 80 /big" $((limit * 5))
    # Until the peer has its /big too: its retransmissions come from here.
    net_flag "$me.got"
    net_flags_wait $((limit * 5)) a.got b.got
    shell_cmd 'netstat'
    net_flag "$me.done"
    net_flags_wait $((limit * 2)) a.done b.done
    printf 'quit\n'
}
_mon_a() { _loss_mon a "$ip_b"; }
_mon_b() { _loss_mon b "$ip_a"; }

net_guest a "$(net_mac 0)" "${NET_HUB_NETDEV[0]}" $((limit * 14)) _mon_a &
pa=$!
net_guest b "$(net_mac 1)" "${NET_HUB_NETDEV[1]}" $((limit * 14)) _mon_b &
pb=$!
wait "$pa" "$pb"
net_hub_stop
e2e_log "$(grep '^hub: frames' "$NET_HUB_LOG")"

grep -E '^hub: dropped TCP frame [0-9]+ 0>1 ' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub dropped nothing a sent"
grep -E '^hub: dropped TCP frame [0-9]+ 1>0 ' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub dropped nothing b sent"
grep -E '^hub: held TCP frame [0-9]+ 0>1 ' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub reordered nothing a sent"
grep -E '^hub: held TCP frame [0-9]+ 1>0 ' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub reordered nothing b sent"

# Program lines are matched anywhere on a line (the shell echoes keys
# meanwhile).
for n in a b; do
    case $n in a) k=0; other=$ip_b ;; b) k=1; other=$ip_a ;; esac
    net_on "$n"
    net_nic_check pci "loss/$n" "$(net_ip "$k")"
    net_expect_no_fault "loss/$n"
    expect_serial "httpd: GET /big from $(net_re "$other"):[0-9]+, 200, 65536 bytes\$" "loss/$n: served the peer's /big"
    expect_serial "wget: body 65536 bytes, fnv1a $big\$" "loss/$n: the peer's /big arrived whole (FNV-1a $big)"
    rto=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}TCP retransmission timeouts: \([0-9]*\)$/\2/p' | tail -n 1)
    # Most losses are repaired by fast retransmit now, not the timer.
    fast=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}TCP fast retransmits: \([0-9]*\)$/\2/p' | tail -n 1)
    ooo=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^\(> \)\{0,1\}TCP out-of-order segments kept: \([0-9]*\)$/\2/p' | tail -n 1)
    [ $((${rto:-0} + ${fast:-0})) -gt 0 ] || e2e_fail "loss/$n: SamOS retransmitted nothing (timeouts: ${rto:-none}, fast: ${fast:-none})"
    e2e_log "loss/$n: retransmission timeouts $rto, fast retransmits ${fast:-none}, out-of-order segments kept ${ooo:-none}"
done
echo "PASS: e2e/233 two SamOS guests, 64 KiB each way at once through a link dropping and reordering TCP frames"
