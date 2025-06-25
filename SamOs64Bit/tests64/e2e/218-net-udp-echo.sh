#!/usr/bin/env bash
# e2e/218-net-udp-echo.sh - UDP echo both ways. UDPECHO.ELF -s
# serves port 7 behind hostfwd and echoes the host's datagram back;
# UDPECHO.ELF as a client sends one to a host echo server (10.0.2.2,
# QEMU's alias for the host) and checks what comes back.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

hup=$(net_host udpport) || e2e_skip "python3 for net_host.py"
up=$(net_host udpport)
net_host udpecho "$up" 400 > "$E2E_TMP/hostecho.log" 2>&1 &
echo_pid=$!
net_programs "UDPECHO.ELF -s 7 1;UDPECHO.ELF 10.0.2.2 $up samos-udp"
E2E_NET=pci
E2E_NET_OPTS=",hostfwd=udp:127.0.0.1:$hup-:7"
_udp_mon() {
    net_wait '^udpecho: listening on port 7$' 200 || { printf 'quit\n'; return 0; }
    net_host udpsend "$hup" host-udp > "$E2E_TMP/udpsend.log" 2>&1
    net_wait '^udpecho: (reply|no matching)' 30
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 500 _udp_mon
kill "$echo_pid" 2> /dev/null
wait "$echo_pid" 2> /dev/null
net_nic_check pci udp
net_expect_no_fault udp
grep -x 'udpsend: echo ok' "$E2E_TMP/udpsend.log" > /dev/null || e2e_fail "host -> SamOS: $(cat "$E2E_TMP/udpsend.log")"
e2e_log "host -> SamOS UDP echo: ok"
expect_serial '^udpecho: 8 bytes from 10\.0\.2\.2:[0-9]+, echoed$' "SamOS echoed the host's datagram"
expect_serial "^udpecho: reply 'samos-udp' from 10\.0\.2\.2 ok\$" "SamOS -> host UDP echo"
echo "PASS: e2e/218 UDP echo host -> SamOS and SamOS -> host"
