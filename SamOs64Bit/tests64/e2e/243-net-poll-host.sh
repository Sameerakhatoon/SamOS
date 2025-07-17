#!/usr/bin/env bash
# e2e/243-net-poll-host.sh - poll and non-blocking sockets
# against the host (slirp). POLLTEST.ELF udp binds three datagram
# sockets (hostfwd'ed) and polls them for ever: the host sends to the
# third, the first and the second, a few seconds apart, and each time
# the one with the datagram alone wakes the poll and echoes. Meanwhile
# POLLTEST.ELF conn connects without blocking to a host HTTP server
# (-31, POLLOUT, the names 10.0.2.15:port to 10.0.2.2:port, a GET read
# with poll and non-blocking recv) and to a closed host port (POLLERR,
# connect -25).
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

hp=$(net_host tcpport) || e2e_skip "python3 for net_host.py"
closed=$(net_host tcpport)
while [ "$closed" = "$hp" ]; do closed=$(net_host tcpport); done
# shellcheck disable=SC2207
hu=($(net_host udpports 3))
mkdir -p "$E2E_TMP/www"
printf 'served by the host to a polling SamOS\n' > "$E2E_TMP/www/index.html"
python3 -I -m http.server --bind 127.0.0.1 --directory "$E2E_TMP/www" "$hp" > "$E2E_TMP/httpserver.log" 2>&1 &
srv=$!
net_programs "POLLTEST.ELF udp 7001 7002 7003 3;POLLTEST.ELF conn 10.0.2.2 $hp;POLLTEST.ELF conn 10.0.2.2 $closed"
E2E_NET=pci
E2E_NET_OPTS=",hostfwd=udp:127.0.0.1:${hu[0]}-:7001,hostfwd=udp:127.0.0.1:${hu[1]}-:7002,hostfwd=udp:127.0.0.1:${hu[2]}-:7003"
_poll_host_mon() {
    net_wait 'polltest: udp waiting on ports 7001 7002 7003' 300 || { printf 'quit\n'; return 0; }
    # Each datagram finds the poll asleep: it has waited since boot,
    # and the next goes two seconds after the last was echoed.
    local k
    for k in 2 0 1; do
        sleep 2
        net_host udpsend "${hu[k]}" "to-700$((k + 1))" >> "$E2E_TMP/udpsend.log" 2>&1
    done
    net_wait 'polltest: udp done' 60
    local t=0
    while [ $t -lt 120 ] && [ "$(tr -d '\r' < "$E2E_SERIAL" | grep -cE 'polltest: conn (sent|refused|failed)')" -lt 2 ]; do
        sleep 0.5
        t=$((t + 1))
    done
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 500 _poll_host_mon
kill "$srv" 2> /dev/null
wait "$srv" 2> /dev/null
net_nic_check pci poll-host
net_expect_no_fault poll-host
tr -d '\r' < "$E2E_SERIAL" | grep -E 'polltest: ' | sed 's/^/    /' >&2
[ "$(grep -cx 'udpsend: echo ok' "$E2E_TMP/udpsend.log")" -eq 3 ] \
    || e2e_fail "poll-host: the host's datagrams: $(cat "$E2E_TMP/udpsend.log")"
order=$(tr -d '\r' < "$E2E_SERIAL" | sed -n "s/^.*polltest: udp port \(700[123]\) readable alone, 'to-\(700[123]\)' from 10\.0\.2\.2, echoed\$/\1=\2/p" | tr '\n' ' ')
[ "$order" = "7003=7003 7001=7001 7002=7002 " ] || e2e_fail "poll-host: the woken sockets, in order: '$order'"
e2e_log "poll-host: the three datagram sockets woke the poll alone, in the order sent (7003, 7001, 7002)"
expect_serial 'polltest: conn connect -31 after [0-9]+ ms$' "poll-host: connect non-blocking is -31"
expect_serial "polltest: conn connected 10\\.0\\.2\\.15:[0-9]+ to 10\\.0\\.2\\.2:$hp \\(connect 0\\)\$" \
    "poll-host: POLLOUT, connect 0, both names"
expect_serial 'polltest: conn status HTTP/1\.0 200 OK$' "poll-host: the host's 200"
expect_serial 'polltest: conn sent 18, received [1-9][0-9]+ bytes \([0-9]+ waits\)$' "poll-host: the response read"
expect_serial 'polltest: conn refused \(POLLERR, connect -25\)$' "poll-host: the closed port: POLLERR, -25"
echo "PASS: e2e/243 poll woken by any of three datagram sockets; non-blocking connect to the host, refused and not"
