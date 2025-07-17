#!/usr/bin/env bash
# e2e/241-net-httpd-silent.sh - a silent client does not hold up
# HTTPD.ELF. Through hostfwd, a host client connects to "HTTPD.ELF 80 4"
# and sends nothing; while it holds its connection, three curls fetch
# /big1024 (1 MiB) at once and get it byte for byte. Then the silent
# client sends its request and gets "hello". The socket httpd, reading
# one connection at a time, waited on the silent one and every curl
# timed out.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

hp=$(net_host tcpport) || e2e_skip "python3 for net_host.py"
command -v curl > /dev/null || e2e_skip "curl"
kib=1024
net_host big "$E2E_TMP/big.expect" $((kib * 1024))
net_programs "HTTPD.ELF 80 4"
E2E_NET=pci
E2E_NET_OPTS=",hostfwd=tcp:127.0.0.1:$hp-:80"

# The silent client: connect, wait for the go file, then ask for /.
cat > "$E2E_TMP/silent.py" << 'EOF'
import os, socket, sys, time
port, go, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
s = socket.create_connection(("127.0.0.1", port), timeout=30)
open(out + ".connected", "w").close()
t = time.time()
while not os.path.exists(go) and time.time() - t < 300:
    time.sleep(0.2)
s.settimeout(60)
s.sendall(b"GET / HTTP/1.0\r\n\r\n")
data = b""
while True:
    try:
        b = s.recv(4096)
    except OSError:
        break
    if not b:
        break
    data += b
open(out, "wb").write(data.split(b"\r\n\r\n", 1)[-1])
EOF

_silent_mon() {
    net_wait '^httpd: listening on port 80$' 200 || { printf 'quit\n'; return 0; }
    python3 -I "$E2E_TMP/silent.py" "$hp" "$E2E_TMP/go" "$E2E_TMP/silent.out" 2> "$E2E_TMP/silent.err" &
    local spid=$! t=0 k
    while [ $t -lt 60 ] && [ ! -e "$E2E_TMP/silent.out.connected" ]; do
        sleep 0.5
        t=$((t + 1))
    done
    sleep 2
    local pids=()
    for k in 1 2 3; do
        curl -s --max-time 120 -o "$E2E_TMP/big$k.out" \
            -w '%{time_starttransfer} %{time_total}\n' "http://127.0.0.1:$hp/big$kib" \
            > "$E2E_TMP/curl$k.time" 2> "$E2E_TMP/curl$k.err" &
        pids+=($!)
    done
    wait "${pids[@]}"
    # Only now may the silent client speak.
    touch "$E2E_TMP/go"
    wait "$spid"
    net_wait '^httpd: request from' 30
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 600 _silent_mon
net_nic_check pci httpd-silent
net_expect_no_fault httpd-silent
[ -e "$E2E_TMP/silent.out.connected" ] || e2e_fail "the silent client did not connect: $(cat "$E2E_TMP/silent.err")"
for k in 1 2 3; do
    cmp -s "$E2E_TMP/big$k.out" "$E2E_TMP/big.expect" \
        || e2e_fail "curl $k /big$kib: $(stat -c %s "$E2E_TMP/big$k.out" 2> /dev/null || echo no) bytes, not $((kib * 1024))"
    e2e_log "curl $k /big$kib whole while the silent client waited (first byte, total s: $(cat "$E2E_TMP/curl$k.time"))"
done
[ "$(cat "$E2E_TMP/silent.out" 2> /dev/null)" = hello ] \
    || e2e_fail "the silent client got '$(head -c 200 "$E2E_TMP/silent.out" 2> /dev/null)'"
e2e_log "the silent client, last, got hello"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c "^httpd: GET /big$kib from 10\\.0\\.2\\.2:[0-9]*, 200, $((kib * 1024)) bytes\$")" -eq 3 ] \
    || e2e_fail "httpd did not log three /big$kib"
# The silent client's connection was accepted first and answered last.
last=$(tr -d '\r' < "$E2E_SERIAL" | grep -E '^httpd: (GET|request)' | tail -n 1)
case $last in
    'httpd: request from 10.0.2.2:'*', 200, 89 bytes') ;;
    *) e2e_fail "httpd's last line: '$last'" ;;
esac
echo "PASS: e2e/241 HTTPD.ELF served three concurrent curls while a silent client held a connection"
