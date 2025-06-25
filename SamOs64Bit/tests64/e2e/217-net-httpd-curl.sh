#!/usr/bin/env bash
# e2e/217-net-httpd-curl.sh - curl on the host fetches pages from
# SamOS's HTTPD.ELF (SamTcpIp's HTTP responder on a stream socket)
# through QEMU's hostfwd: "/" is upstream's "hello", "/big" 64 KiB in
# many segments (the send buffer, cwnd and the peer window at work).
# Both come back byte for byte; the program saw both requests.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

hp=$(net_host tcpport) || e2e_skip "python3 for net_host.py"
command -v curl > /dev/null || e2e_skip "curl"
net_host big "$E2E_TMP/big.expect"
net_programs "HTTPD.ELF 80 2"
E2E_NET=pci
E2E_NET_OPTS=",hostfwd=tcp:127.0.0.1:$hp-:80"
_httpd_mon() {
    net_wait '^httpd: listening on port 80$' 200 || { printf 'quit\n'; return 0; }
    curl -s --max-time 60 -o "$E2E_TMP/hello.out" "http://127.0.0.1:$hp/" 2> "$E2E_TMP/curl1.err"
    curl -s --max-time 120 -o "$E2E_TMP/big.out" "http://127.0.0.1:$hp/big" 2> "$E2E_TMP/curl2.err"
    net_wait '^httpd: GET /big from' 30
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 500 _httpd_mon
net_nic_check pci httpd
net_expect_no_fault httpd
expect_serial '^httpd: listening on port 80$' "httpd started"
[ "$(cat "$E2E_TMP/hello.out" 2> /dev/null)" = hello ] \
    || e2e_fail "curl / got '$(head -c 200 "$E2E_TMP/hello.out" 2> /dev/null)'"
e2e_log "curl /: hello"
cmp -s "$E2E_TMP/big.out" "$E2E_TMP/big.expect" \
    || e2e_fail "curl /big: $(stat -c %s "$E2E_TMP/big.out" 2> /dev/null || echo no) bytes, not the 65536 expected"
e2e_log "curl /big: 65536 bytes, identical"
# 89 bytes: upstream's response (Layer5/http.c), 83 bytes of status line
# and headers (17 + 26 + 19 + 19 + 2) and the 6-byte body "hello\n".
expect_serial '^httpd: request from 10\.0\.2\.2:[0-9]+, 200, 89 bytes$' "httpd answered /"
expect_serial '^httpd: GET /big from 10\.0\.2\.2:[0-9]+, 200, 65536 bytes$' "httpd answered /big"
echo "PASS: e2e/217 curl fetched / and /big from SamOS's httpd through hostfwd"
