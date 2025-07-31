#!/usr/bin/env bash
# e2e/242-net-poll-local.sh - the socket calls the network follow-ups added, on
# SamOS's loopback by POLLTEST.ELF local (programs/net/polltest.c):
# poll with timeout 0 (at once), 300 ms (no sooner) and -1, several
# sockets where only one has news, a skipped entry, POLLNVAL, POLLERR
# and POLLHUP of a refused connect even with no events asked; a
# non-blocking listener, connect (-31, then POLLOUT, then 0), accept
# and recv (-30); nonblock read back; getsockname and getpeername of
# both ends of a connection, of a listener (0.0.0.0) and of a datagram
# socket. Every check passes and nothing faults.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

net_programs "POLLTEST.ELF local 7000"
E2E_NET=pci
_poll_mon() {
    net_wait 'polltest: local done' 300
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _poll_mon
net_nic_check pci poll-local
net_expect_no_fault poll-local
tr -d '\r' < "$E2E_SERIAL" | grep -aE 'polltest: (ok|FAIL) ' | sed 's/^/    /' >&2
expect_no_serial 'polltest: FAIL' "poll-local: no check failed"
for what in 'poll timeout 0 returns 0 at once' 'poll timeout 300 returns 0 after [0-9]+ ms' \
    'connect non-blocking is -31' 'poll connecting socket: POLLOUT once connected' \
    'accept non-blocking with none queued is -30' 'recv non-blocking with nothing there is -30' \
    'poll of 4 \(one skipped\): the connection alone' 'client name 127\.0\.0\.1:[0-9]+ is the server.s peer' \
    'server name 127\.0\.0\.1:7000 is the client.s peer' 'getsockname listener 0\.0\.0\.0:7000' \
    'a refused connect: POLLERR and POLLHUP' 'a closed socket: POLLNVAL' 'nonblock reads back 1'; do
    expect_serial "polltest: ok $what" "poll-local: $what"
done
n=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^.*polltest: local done, \([0-9]*\) checks, 0 failures$/\1/p' | tail -n 1)
[ "${n:-0}" -ge 30 ] || e2e_fail "poll-local: $(tr -d '\r' < "$E2E_SERIAL" | grep -a 'polltest: local done' || echo 'no end line')"
e2e_log "poll-local: $n checks, 0 failures"
echo "PASS: e2e/242 poll, non-blocking sockets and socket names on the loopback"
