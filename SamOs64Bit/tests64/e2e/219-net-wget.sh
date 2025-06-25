#!/usr/bin/env bash
# e2e/219-net-wget.sh - SamOS fetches from a host HTTP server
# (python3 -m http.server on 127.0.0.1, 10.0.2.2 from the guest):
# WGET.ELF gets a short file and a 64 KiB one over active-open stream
# sockets; status line, body length and FNV-1a match the files.
# PING.ELF, the user program, pings the host's side (slirp) meanwhile
# (its lines unanchored: the shell's prompt may come first on one).
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

hp=$(net_host tcpport) || e2e_skip "python3 for net_host.py"
mkdir -p "$E2E_TMP/www"
printf 'served by the host to SamOS\n' > "$E2E_TMP/www/small.txt"
net_host big "$E2E_TMP/www/big.txt"
python3 -I -m http.server --bind 127.0.0.1 --directory "$E2E_TMP/www" "$hp" > "$E2E_TMP/httpserver.log" 2>&1 &
srv=$!
net_programs "WGET.ELF 10.0.2.2 $hp /small.txt;WGET.ELF 10.0.2.2 $hp /big.txt;PING.ELF 10.0.2.2 3"
E2E_NET=pci
_wget_mon() {
    shell_boot || { printf 'quit\n'; return 0; }
    local t=0
    while [ $t -lt 240 ] && { [ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c '^wget: body ')" -lt 2 ] \
        || ! grep -q 'ping: 10\.0\.2\.2 3 sent' "$E2E_SERIAL"; }; do
        sleep 0.5
        t=$((t + 1))
    done
    shell_cmd 'netstat'
    printf 'quit\n'
}
e2e_run_qemu 500 _wget_mon
kill "$srv" 2> /dev/null
wait "$srv" 2> /dev/null
net_nic_check pci wget
net_expect_no_fault wget
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c '^wget: HTTP/1\.0 200 OK$')" -eq 2 ] || e2e_fail "not two 200 responses"
small=$(net_host fnv "$E2E_TMP/www/small.txt")
big=$(net_host fnv "$E2E_TMP/www/big.txt")
expect_serial "^wget: body 28 bytes, fnv1a $small\$" "small.txt arrived whole"
expect_serial '^served by the host to SamOS$' "small.txt printed"
expect_serial "^wget: body 65536 bytes, fnv1a $big\$" "big.txt arrived whole"
expect_serial 'ping: reply from 10\.0\.2\.2 seq=[0-9]+ time=[0-9]+\.[0-9]{3} ms' "PING.ELF: an echo reply"
expect_serial 'ping: 10\.0\.2\.2 3 sent, [1-3] received' "PING.ELF: summary"
echo "PASS: e2e/219 SamOS fetched two files from a host HTTP server, PING.ELF pinged it"
