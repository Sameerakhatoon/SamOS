#!/usr/bin/env bash
# Helpers for the network e2e tests (212-215). Sourced after
# _lib.sh and _shell_lib.sh. Not a test itself.
#
#   net_cmdline <words>       boot with this kernel command line
#                             (CMDLINE.TXT on a copy of the image)
#   net_nic_check <transport> [tag] [ip]
#                             the NIC and the static configuration:
#                             virtio-net eth0 on `transport` (pci,
#                             mmio), by MSI-X or a line (or "polled"
#                             with samos.net_poll), net: eth0 <ip>/24
#                             (default 10.0.2.15)
#   net_gateway_test <transport> [tag]
#                             QEMU's -netdev user: ifconfig, route, ping
#                             10.0.2.2 (slirp answers it), arp
#   net_peer_test <transport> [tag]
#                             -netdev socket to net_peer.py as 10.0.2.2:
#                             the peer pings SamOS (ARP, echo replies, a
#                             bad checksum ignored), and SamOS pings it

NET_PEER="$(dirname "${BASH_SOURCE[0]}")/net_peer.py"

# net_re <a.b.c.d>: the address as an ERE (dots escaped).
net_re() {
    printf '%s' "$1" | sed 's/\./\\./g'
}

net_cmdline() {
    cp --sparse=always "$E2E_IMAGE" "$E2E_TMP/os.img"
    E2E_IMAGE="$E2E_TMP/os.img"
    printf '%s\n' "$*" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

net_nic_check() {
    local transport=$1 tag=$2 ip=${3:-10.0.2.15} irq='(MSI-X vector [0-9]+|line, GSI [0-9]+)'
    [ "${NET_IRQ:-}" = polled ] && irq=polled
    expect_serial "^virtio-net: eth0 52:54:00:12:34:5[0-9a-f], $transport, $irq, link up, queues [0-9]+/[0-9]+\$" \
        "$tag: virtio-net eth0 on $transport"
    expect_serial "^net: eth0 $(net_re "$ip")/24, gateway 10\\.0\\.2\\.2\$" "$tag: static configuration"
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
    net_ping_check "$tag" 4
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
    net_ping_check "$tag" 3
}

# ---- (217-220) ---------------------------------------------------------
#   net_host <args>          tests64/e2e/net_host.py
#   net_programs <run>       boot with samos.run=<run> (programs ahead of
#                            BLANK.ELF, net_cmdline)
#   net_wait <ERE> [s]       (monitor script) wait for a serial line
NET_HOST="$(dirname "${BASH_SOURCE[0]}")/net_host.py"

net_host() {
    python3 -I "$NET_HOST" "$@"
}

net_programs() {
    net_cmdline "samos.run=$1"
}

net_wait() {
    e2e_wait_serial "$1" "${2:-200}"
}

net_expect_no_fault() {
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|samtcpip: assert' "$1: no fault"
}

# ---- (229) two SamOS guests on one link ---------------------------------
# Two QEMUs, each a SamOS guest from its own copy of the image (QEMU
# attaches it snapshot=on as usual), joined by a pair of -netdev socket
# UDP endpoints that point at each other: whatever one NIC sends, the
# other receives, and nothing else is on the link. Each guest has its
# own MAC (E2E_NET_MAC) and its own address (samos.ip on its command
# line, src/net/net.c); both keep the default gateway 10.0.2.2, which
# is not there and is not needed on one subnet.
#
#   net_guest_image <name> <words>   $E2E_TMP/<name>.img, a copy of the
#                                    image with this command line
#   net_guest <name> <mac> <netdev> <timeout> <fn>
#                                    boot guest <name> from its image:
#                                    e2e_run_qemu with the serial log in
#                                    $E2E_TMP/<name>.serial (the
#                                    E2E_SERIAL that <fn>, its monitor
#                                    script, and the shell helpers see)
#   net_on <name>                    point E2E_SERIAL (expect_serial,
#                                    e2e_fail's tail) at that guest's log
#   net_two_guests_test <transport> [tag]
#                                    the test: guest a (10.0.2.15) serves
#                                    HTTPD.ELF and UDPECHO.ELF -s, guest b
#                                    (10.0.2.16) fetches / and /big with
#                                    WGET.ELF, sends a datagram with
#                                    UDPECHO.ELF and pings a with
#                                    PING.ELF; then a's shell pings b
net_guest_image() {
    local name=$1
    shift
    cp --sparse=always "$E2E_IMAGE" "$E2E_TMP/$name.img"
    printf '%s\n' "$*" > "$E2E_TMP/$name.CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_TMP/$name.img@@$((2048 * 512))" "$E2E_TMP/$name.CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT for guest $name"
}

net_guest() {
    local name=$1 mac=$2 netdev=$3 limit=$4 fn=$5
    (
        E2E_IMAGE="$E2E_TMP/$name.img"
        E2E_SERIAL="$E2E_TMP/$name.serial"
        E2E_QEMU_LOG="$E2E_TMP/$name.qemu.log"
        E2E_NETDEV=$netdev
        E2E_NET_MAC=$mac
        e2e_run_qemu "$limit" "$fn"
    )
}

net_on() {
    E2E_SERIAL="$E2E_TMP/$1.serial"
}

# net_count <name> <ERE>: how many lines of guest <name>'s log match.
net_count() {
    tr -d '\r' 2> /dev/null < "$E2E_TMP/$1.serial" | grep -cE -- "$2"
}

# The shell's ping lines (net_ping_check's, for any address).
net_shell_ping_check() {
    local tag=$1 ip=$2 min=$3 re got
    re=$(net_re "$ip")
    expect_serial "^PING $re\$" "$tag: ping $ip started"
    expect_serial "^reply from $re: seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms\$" "$tag: an echo reply from $ip"
    got=$(tr -d '\r' < "$E2E_SERIAL" | sed -n "s/^$re: [0-9]* sent, \\([0-9]*\\) received\$/\\1/p" | tail -n 1)
    [ "${got:-0}" -ge "$min" ] || e2e_fail "$tag: ${got:-no} echo replies from $ip, expected at least $min"
    e2e_log "$tag: $got echo replies from $ip"
}

net_two_guests_test() {
    local transport=$1 tag=${2:-$1} pa pb
    pa=$(net_host udpport) || e2e_skip "python3 for net_host.py"
    pb=$(net_host udpport)
    while [ "$pb" = "$pa" ]; do pb=$(net_host udpport); done
    local ip_a=10.0.2.15 ip_b=10.0.2.16 mac_a=52:54:00:12:34:56 mac_b=52:54:00:12:34:57
    local text=two-guests limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
    net_host big "$E2E_TMP/big.expect"
    printf 'hello\n' > "$E2E_TMP/hello.expect"
    # samos.run comes last on the line (kernel.c reads it to the end).
    net_guest_image a "samos.ip=$ip_a/24 samos.run=HTTPD.ELF 80 2;UDPECHO.ELF -s 7 1"
    net_guest_image b "samos.ip=$ip_b/24 samos.run=WGET.ELF $ip_a 80 /;WGET.ELF $ip_a 80 /big;UDPECHO.ELF $ip_a 7 $text;PING.ELF $ip_a 3"
    E2E_NET=$transport
    # b is done when its four programs have printed their last lines.
    _net_b_done() {
        [ "$(net_count b '^(> )?wget: body ')" -ge 2 ] && [ "$(net_count b '^(> )?udpecho: (reply|no matching)')" -ge 1 ] \
            && [ "$(net_count b "ping: $(net_re "$ip_a") [0-9]+ sent")" -ge 1 ]
    }
    # a: up to the shell; once b's programs are done (or b is gone),
    # ping b and show the ARP table.
    _net_a_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        local t=0
        while [ $t -lt $((limit * 10)) ] && ! _net_b_done && [ ! -e "$E2E_TMP/b.gone" ]; do
            sleep 0.5
            t=$((t + 1))
        done
        shell_cmd "ping $ip_b 3" 60
        shell_cmd 'arp'
        touch "$E2E_TMP/a.done"
        printf 'quit\n'
    }
    # b: its programs, then up until a's pings are done.
    _net_b_mon() {
        local t=0
        while [ $t -lt $((limit * 10)) ] && ! _net_b_done; do
            sleep 0.5
            t=$((t + 1))
        done
        t=0
        while [ $t -lt 240 ] && [ ! -e "$E2E_TMP/a.done" ]; do
            sleep 0.5
            t=$((t + 1))
        done
        printf 'quit\n'
    }
    e2e_log "$tag: guest a $ip_a $mac_a on 127.0.0.1:$pa, guest b $ip_b $mac_b on 127.0.0.1:$pb"
    net_guest a "$mac_a" "socket,id=net0,udp=127.0.0.1:$pb,localaddr=127.0.0.1:$pa" $((limit * 12)) _net_a_mon &
    local apid=$!
    # b boots once a's servers listen: WGET.ELF and UDPECHO.ELF try once
    # (a refused connect is not retried).
    if E2E_SERIAL="$E2E_TMP/a.serial" e2e_wait_serial '^httpd: listening on port 80$' $((limit * 3)) \
        && E2E_SERIAL="$E2E_TMP/a.serial" e2e_wait_serial '^udpecho: listening on port 7$' 60; then
        net_guest b "$mac_b" "socket,id=net0,udp=127.0.0.1:$pa,localaddr=127.0.0.1:$pb" $((limit * 8)) _net_b_mon
    else
        e2e_log "$tag: guest a's servers never listened, guest b not started"
    fi
    touch "$E2E_TMP/b.gone"
    wait "$apid"

    local ra rb hello big
    ra=$(net_re "$ip_a")
    rb=$(net_re "$ip_b")
    hello=$(net_host fnv "$E2E_TMP/hello.expect")
    big=$(net_host fnv "$E2E_TMP/big.expect")

    # A program's line may follow the shell's prompt ("> ") when the
    # program printed while the shell waited for a key.
    net_on a
    net_nic_check "$transport" "$tag/a" "$ip_a"
    net_expect_no_fault "$tag/a"
    expect_serial "^virtio-net: eth0 $mac_a," "$tag/a: MAC $mac_a"
    expect_serial "^(> )?httpd: request from $rb:[0-9]+, 200, 89 bytes\$" "$tag/a: httpd answered b's /"
    expect_serial "^(> )?httpd: GET /big from $rb:[0-9]+, 200, 65536 bytes\$" "$tag/a: httpd answered b's /big"
    expect_serial "^(> )?udpecho: ${#text} bytes from $rb:[0-9]+, echoed\$" "$tag/a: echoed b's datagram"
    net_shell_ping_check "$tag/a" "$ip_b" 2
    expect_serial "^IP : $rb, MAC : $mac_b, OIF = eth0\$" "$tag/a: ARP learned b"

    net_on b
    net_nic_check "$transport" "$tag/b" "$ip_b"
    net_expect_no_fault "$tag/b"
    expect_serial "^virtio-net: eth0 $mac_b," "$tag/b: MAC $mac_b"
    [ "$(net_count b '^(> )?wget: HTTP/1\.0 200 OK$')" -eq 2 ] || e2e_fail "$tag/b: not two 200 responses from a"
    expect_serial "^(> )?wget: body 6 bytes, fnv1a $hello\$" "$tag/b: / arrived whole (6 bytes, FNV-1a $hello)"
    expect_serial '^(> )?hello$' "$tag/b: / printed"
    expect_serial "^(> )?wget: body 65536 bytes, fnv1a $big\$" "$tag/b: /big arrived whole (65536 bytes, FNV-1a $big)"
    expect_serial "^(> )?udpecho: reply '$text' from $ra ok\$" "$tag/b: UDP echo from a"
    expect_serial "ping: reply from $ra seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms" "$tag/b: PING.ELF, an echo reply from a"
    expect_serial "ping: $ra 3 sent, [2-3] received" "$tag/b: PING.ELF summary"
}
