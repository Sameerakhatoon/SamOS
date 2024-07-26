#!/usr/bin/env bash
# Helpers for the network e2e tests (212-215). Sourced after
# _lib.sh and _shell_lib.sh. Not a test itself.
#
#   net_cmdline <words>       boot with this kernel command line
#                             (CMDLINE.TXT on a copy of the image)
#   net_nic_check <transport> the NIC and the static configuration:
#                             virtio-net eth0 on `transport` (pci,
#                             mmio), by MSI-X or a line (or "polled"
#                             with samos.net_poll), net: eth0 10.0.2.15/24
#   net_gateway_test <transport> [tag]
#                             QEMU's -netdev user: ifconfig, route, ping
#                             10.0.2.2 (slirp answers it), arp
#   net_peer_test <transport> [tag]
#                             -netdev socket to net_peer.py as 10.0.2.2:
#                             the peer pings SamOS (ARP, echo replies, a
#                             bad checksum ignored), and SamOS pings it

NET_PEER="$(dirname "${BASH_SOURCE[0]}")/net_peer.py"

net_cmdline() {
    cp --sparse=always "$E2E_IMAGE" "$E2E_TMP/os.img"
    E2E_IMAGE="$E2E_TMP/os.img"
    printf '%s\n' "$*" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

net_nic_check() {
    local transport=$1 tag=$2 irq='(MSI-X vector [0-9]+|line, GSI [0-9]+)'
    [ "${NET_IRQ:-}" = polled ] && irq=polled
    expect_serial "^virtio-net: eth0 52:54:00:12:34:5[0-9a-f], $transport, $irq, link up, queues [0-9]+/[0-9]+\$" \
        "$tag: virtio-net eth0 on $transport"
    expect_serial '^net: eth0 10\.0\.2\.15/24, gateway 10\.0\.2\.2$' "$tag: static configuration"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "$tag: no fault"
}

# net_ping_check <tag> <min received>
net_ping_check() {
    local tag=$1 min=$2 got
    expect_serial '^PING 10\.0\.2\.2$' "$tag: ping started"
    expect_serial '^reply from 10\.0\.2\.2: seq=[0-9]+ time=[0-9]+\.[0-9]{3} ms$' "$tag: an echo reply"
    got=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^10\.0\.2\.2: [0-9]* sent, \([0-9]*\) received$/\1/p' | tail -n 1)
    [ "${got:-0}" -ge "$min" ] || e2e_fail "$tag: ${got:-no} echo replies, expected at least $min"
    e2e_log "$tag: $got echo replies from 10.0.2.2"
}

net_gateway_test() {
    local transport=$1 tag=${2:-$1}
    E2E_NET=$transport
    _net_gw_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        shell_cmd 'ifconfig'
        shell_cmd 'route'
        shell_cmd 'ping 10.0.2.2 4' 60
        shell_cmd 'arp'
        printf 'quit\n'
    }
    e2e_run_qemu 400 _net_gw_mon
    net_nic_check "$transport" "$tag"
    expect_serial "$SHELL_BANNER" "$tag: shell up"
    expect_serial 'IP Addr = 10\.0\.2\.15/24' "$tag: ifconfig shows eth0's address"
    expect_serial 'IP Addr = 127\.0\.0\.1/8' "$tag: ifconfig shows lo"
    expect_serial '^L3 Routing Table:$' "$tag: route ran"
    expect_serial '0\.0\.0\.0 +0 +10\.0\.2\.2 +eth0' "$tag: default route via 10.0.2.2"
    net_ping_check "$tag" 3
    expect_serial '^IP : 10\.0\.2\.2, MAC : 52:55:0a:00:02:02, OIF = eth0$' "$tag: ARP learned the gateway"
}

net_peer_test() {
    local transport=$1 tag=${2:-$1} ports peer qemu
    ports=$(python3 -I "$NET_PEER" ports) || e2e_skip "python3 for net_peer.py"
    peer=${ports% *}
    qemu=${ports#* }
    local plog="$E2E_TMP/peer.log"
    python3 -I "$NET_PEER" run "$peer" "$qemu" "$plog" 380 4 > /dev/null 2>&1 &
    local ppid=$!
    E2E_NET=$transport
    E2E_NETDEV="socket,id=net0,udp=127.0.0.1:$peer,localaddr=127.0.0.1:$qemu"
    _net_peer_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        local t=0
        while [ $t -lt 240 ] && ! grep -E '^peer: (PASS|FAIL)' "$plog" > /dev/null 2>&1; do
            sleep 0.5
            t=$((t + 1))
        done
        shell_cmd 'ping 10.0.2.2 3' 60
        touch "$plog.stop"
        printf 'quit\n'
    }
    e2e_run_qemu 400 _net_peer_mon
    touch "$plog.stop"
    wait "$ppid"
    e2e_log "$tag: peer log:"
    sed 's/^/    /' "$plog" >&2
    net_nic_check "$transport" "$tag"
    grep -E '^peer: gratuitous ARP 10\.0\.2\.15 is-at 52:54:00' "$plog" > /dev/null \
        || e2e_fail "$tag: no gratuitous ARP from SamOS"
    grep -E '^peer: ARP reply 10\.0\.2\.15 is-at' "$plog" > /dev/null || e2e_fail "$tag: SamOS did not answer ARP"
    grep -E '^peer: PASS pings 4/4, bad checksum ignored$' "$plog" > /dev/null \
        || e2e_fail "$tag: the peer's pings: $(grep '^peer: \(PASS\|FAIL\)' "$plog")"
    grep -E '^peer: echo request seq=[0-9]+ from 10\.0\.2\.15, answered$' "$plog" > /dev/null \
        || e2e_fail "$tag: no echo request from SamOS reached the peer"
    net_ping_check "$tag" 2
}
