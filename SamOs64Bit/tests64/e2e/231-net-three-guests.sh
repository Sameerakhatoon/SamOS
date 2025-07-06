#!/usr/bin/env bash
# e2e/231-net-three-guests.sh - three SamOS guests on one segment.
# net_host.py's hub joins three QEMUs (a learning switch, nothing
# dropped): a 10.0.2.15 runs HTTPD.ELF on ports 80 and 81, and every
# guest runs UDPECHO.ELF -s. Once all three shells are up, b and c fetch
# a's 4 MiB /big4096 at the same time (b from port 80, c from 81; the
# hub's flow times show the two transfers overlapping), byte for byte,
# while a sends a datagram to b and c; then b and c echo off each
# other and a. Every guest's ARP table holds the other two MACs.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

E2E_MEM=${E2E_MEM:-256M}
E2E_NET=pci
limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
kib=4096
net_hub 3
net_host big "$E2E_TMP/big.expect" $((kib * 1024))
big=$(net_host fnv "$E2E_TMP/big.expect")
names=(a b c)
ip_a=$(net_ip 0); ip_b=$(net_ip 1); ip_c=$(net_ip 2)
net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80 1;HTTPD.ELF 81 1;UDPECHO.ELF -s 7 2"
net_guest_image b "samos.ip=$ip_b/24 samos.run=UDPECHO.ELF -s 7 2"
net_guest_image c "samos.ip=$ip_c/24 samos.run=UDPECHO.ELF -s 7 2"

# b and c: type the fetch, wait until the other has typed its own, then
# both press Enter (typing goes key by key and takes seconds under
# load, which would let one transfer end before the other starts).
_together() {
    local before
    shell_type "$1"
    net_flag "$me.typed"
    net_flags_wait 60 b.typed c.typed
    before=$(shell_log_size)
    shell_type $'\n'
    shell_wait_prompt "$before" $((limit * 4)) || e2e_log "no prompt after: $1"
}

# Every guest: shell up, wait for the others, its own part, the ARP
# table, then stay up until all three are done (a late datagram is
# still answered).
_three_mon() {
    local me=$1
    shell_boot || { net_flag "$me.up"; net_flag "$me.done"; printf 'quit\n'; return 0; }
    net_flag "$me.up"
    net_flags_wait $((limit * 3)) a.up b.up c.up
    case $me in
        a)
            shell_cmd "UDPECHO.ELF $ip_b 7 a-to-b" 30
            shell_cmd "UDPECHO.ELF $ip_c 7 a-to-c" 30
            ;;
        b)
            _together "WGET.ELF $ip_a 80 /big$kib"
            shell_cmd "UDPECHO.ELF $ip_a 7 b-to-a" 30
            shell_cmd "UDPECHO.ELF $ip_c 7 b-to-c" 30
            ;;
        c)
            _together "WGET.ELF $ip_a 81 /big$kib"
            shell_cmd "UDPECHO.ELF $ip_a 7 c-to-a" 30
            shell_cmd "UDPECHO.ELF $ip_b 7 c-to-b" 30
            ;;
    esac
    shell_cmd 'arp'
    net_flag "$me.done"
    net_flags_wait $((limit * 4)) a.done b.done c.done
    printf 'quit\n'
}
_mon_a() { _three_mon a; }
_mon_b() { _three_mon b; }
_mon_c() { _three_mon c; }

pids=()
for k in 0 1 2; do
    n=${names[k]}
    net_guest "$n" "$(net_mac "$k")" "${NET_HUB_NETDEV[k]}" $((limit * 12)) "_mon_$n" &
    pids+=($!)
done
wait "${pids[@]}"
net_hub_stop
e2e_log "hub: $(grep '^hub: frames' "$NET_HUB_LOG")"

# A program prints while the shell echoes typed keys, so its lines are
# matched anywhere on a line, not from the start.
ra=$(net_re "$ip_a"); rb=$(net_re "$ip_b"); rc=$(net_re "$ip_c")
for k in 0 1 2; do
    n=${names[k]}
    net_on "$n"
    net_nic_check pci "three/$n" "$(net_ip "$k")"
    net_expect_no_fault "three/$n"
    expect_serial "^virtio-net: eth0 $(net_mac "$k")," "three/$n: MAC $(net_mac "$k")"
    for j in 0 1 2; do
        [ "$j" = "$k" ] && continue
        expect_serial "^(> )?IP : $(net_re "$(net_ip "$j")"), MAC : $(net_mac "$j"), OIF = eth0\$" \
            "three/$n: ARP has ${names[j]}"
    done
done

net_on a
expect_serial "httpd: GET /big$kib from $rb:[0-9]+, 200, $((kib * 1024)) bytes\$" "three/a: port 80 served b"
expect_serial "httpd: GET /big$kib from $rc:[0-9]+, 200, $((kib * 1024)) bytes\$" "three/a: port 81 served c"
expect_serial "udpecho: reply 'a-to-b' from $rb ok\$" "three/a: UDP echo from b"
expect_serial "udpecho: reply 'a-to-c' from $rc ok\$" "three/a: UDP echo from c"
expect_serial "udpecho: 6 bytes from $rb:[0-9]+, echoed\$" "three/a: echoed b"
expect_serial "udpecho: 6 bytes from $rc:[0-9]+, echoed\$" "three/a: echoed c"

net_on b
expect_serial "wget: HTTP/1\\.0 200 OK\$" "three/b: 200 from a"
expect_serial "wget: body $((kib * 1024)) bytes, fnv1a $big\$" "three/b: /big$kib whole (FNV-1a $big)"
expect_serial "udpecho: reply 'b-to-a' from $ra ok\$" "three/b: UDP echo from a"
expect_serial "udpecho: reply 'b-to-c' from $rc ok\$" "three/b: UDP echo from c"
expect_serial "udpecho: 6 bytes from $ra:[0-9]+, echoed\$" "three/b: echoed a"
expect_serial "udpecho: 6 bytes from $rc:[0-9]+, echoed\$" "three/b: echoed c"

net_on c
expect_serial "wget: HTTP/1\\.0 200 OK\$" "three/c: 200 from a"
expect_serial "wget: body $((kib * 1024)) bytes, fnv1a $big\$" "three/c: /big$kib whole (FNV-1a $big)"
expect_serial "udpecho: reply 'c-to-a' from $ra ok\$" "three/c: UDP echo from a"
expect_serial "udpecho: reply 'c-to-b' from $rb ok\$" "three/c: UDP echo from b"
expect_serial "udpecho: 6 bytes from $ra:[0-9]+, echoed\$" "three/c: echoed a"
expect_serial "udpecho: 6 bytes from $rb:[0-9]+, echoed\$" "three/c: echoed b"

grep -E "^hub: flow $ra:80 > $rb:" "$NET_HUB_LOG" | sed 's/^/    /' >&2
grep -E "^hub: flow $ra:81 > $rc:" "$NET_HUB_LOG" | sed 's/^/    /' >&2
ov=$(net_hub_overlap "^hub: flow $ra:8[01] > ")
[ "$ov" -ge 2 ] || e2e_fail "three: the two /big$kib transfers did not overlap (at most $ov at once)"
e2e_log "three: the two transfers from a overlapped"
echo "PASS: e2e/231 three SamOS guests: ARP all round, two concurrent 4 MiB fetches from one, UDP echo between every pair"
