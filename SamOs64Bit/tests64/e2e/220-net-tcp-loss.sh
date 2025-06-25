#!/usr/bin/env bash
# e2e/220-net-tcp-loss.sh - TCP survives packet loss. QEMU's
# filter-redirector takes every frame between the NIC and the user
# network out to net_host.py's relay and back, both ways; the relay
# drops every 7th TCP frame in each direction. Through it curl fetches
# HTTPD.ELF's /big (SamOS sending, so SamOS retransmits) and WGET.ELF
# fetches 64 KiB from a host server (SamOS receiving: it re-ACKs, the
# host retransmits), byte for byte; netstat shows SamOS's
# retransmission timeouts.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

command -v curl > /dev/null || e2e_skip "curl"
hp=$(net_host tcpport) || e2e_skip "python3 for net_host.py"
wp=$(net_host tcpport)
p0=$(net_host tcpport); p1=$(net_host tcpport); p2=$(net_host tcpport); p3=$(net_host tcpport)
net_host big "$E2E_TMP/big.expect"
mkdir -p "$E2E_TMP/www"
cp "$E2E_TMP/big.expect" "$E2E_TMP/www/big.txt"
python3 -I -m http.server --bind 127.0.0.1 --directory "$E2E_TMP/www" "$wp" > "$E2E_TMP/httpserver.log" 2>&1 &
srv=$!
net_programs "HTTPD.ELF 80 1;WGET.ELF 10.0.2.2 $wp /big.txt"
E2E_NET=pci
E2E_NET_OPTS=",hostfwd=tcp:127.0.0.1:$hp-:80"
# The order of the filters matters. QEMU walks a netdev's filters
# forward for the tx queue (net0 sending to the NIC) and backward for
# the rx queue (the NIC's frames to net0), and a frame read from an
# indev goes on to the next filter in that walk. So each queue's
# injecting filter must come after its stealing one in the walk: fr1
# before fr0 in the list. With fr0 first, every frame fr1 injected was
# taken by fr0 again and went round the relay for ever.
E2E_EXTRA_QEMU_ARGS="-chardev socket,id=rd0,host=127.0.0.1,port=$p0,server=on,wait=off
 -chardev socket,id=rd1,host=127.0.0.1,port=$p1,server=on,wait=off
 -chardev socket,id=rd2,host=127.0.0.1,port=$p2,server=on,wait=off
 -chardev socket,id=rd3,host=127.0.0.1,port=$p3,server=on,wait=off
 -object filter-redirector,id=fr1,netdev=net0,queue=rx,indev=rd1
 -object filter-redirector,id=fr0,netdev=net0,queue=rx,outdev=rd0
 -object filter-redirector,id=fr2,netdev=net0,queue=tx,outdev=rd2
 -object filter-redirector,id=fr3,netdev=net0,queue=tx,indev=rd3"
_loss_mon() {
    net_host relay "$p0,$p1,$p2,$p3" 7 550 "$E2E_TMP/relay.log" > "$E2E_TMP/relay.err" 2>&1 &
    shell_boot || { printf 'quit\n'; return 0; }
    net_wait '^httpd: listening on port 80$' 60
    curl -s --max-time 240 -o "$E2E_TMP/big.out" "http://127.0.0.1:$hp/big" 2> "$E2E_TMP/curl.err"
    net_wait '^wget: body ' 240
    shell_cmd 'netstat'
    printf 'quit\n'
}
e2e_run_qemu 600 _loss_mon
kill "$srv" 2> /dev/null
wait "$srv" 2> /dev/null
sleep 1
e2e_log "relay: $(grep -c dropped "$E2E_TMP/relay.log" 2> /dev/null) frames dropped; $(cat "$E2E_TMP/relay.err" 2> /dev/null)"
net_nic_check pci loss
net_expect_no_fault loss
grep -E '^relay: dropped TCP frame [0-9]+ out' "$E2E_TMP/relay.log" > /dev/null || e2e_fail "the relay dropped nothing SamOS sent"
grep -E '^relay: dropped TCP frame [0-9]+ in' "$E2E_TMP/relay.log" > /dev/null || e2e_fail "the relay dropped nothing sent to SamOS"
cmp -s "$E2E_TMP/big.out" "$E2E_TMP/big.expect" \
    || e2e_fail "curl /big through loss: $(stat -c %s "$E2E_TMP/big.out" 2> /dev/null || echo no) bytes, not the 65536 expected"
e2e_log "curl /big through loss: 65536 bytes, identical"
big=$(net_host fnv "$E2E_TMP/big.expect")
expect_serial "^wget: body 65536 bytes, fnv1a $big\$" "wget through loss: whole"
rto=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^TCP retransmission timeouts: \([0-9]*\)$/\1/p' | tail -n 1)
[ "${rto:-0}" -gt 0 ] || e2e_fail "SamOS retransmitted nothing (timeouts: ${rto:-none})"
e2e_log "SamOS retransmission timeouts: $rto"
echo "PASS: e2e/220 TCP both ways through a link dropping every 7th TCP frame"
