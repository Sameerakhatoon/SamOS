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
        [ "$(net_count b 'wget: body ')" -ge 2 ] && [ "$(net_count b 'udpecho: (reply|no matching)')" -ge 1 ] \
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
    expect_serial "httpd: request from $rb:[0-9]+, 200, 89 bytes\$" "$tag/a: httpd answered b's /"
    expect_serial "httpd: GET /big from $rb:[0-9]+, 200, 65536 bytes\$" "$tag/a: httpd answered b's /big"
    expect_serial "udpecho: ${#text} bytes from $rb:[0-9]+, echoed\$" "$tag/a: echoed b's datagram"
    net_shell_ping_check "$tag/a" "$ip_b" 2
    expect_serial "^IP : $rb, MAC : $mac_b, OIF = eth0\$" "$tag/a: ARP learned b"

    net_on b
    net_nic_check "$transport" "$tag/b" "$ip_b"
    net_expect_no_fault "$tag/b"
    expect_serial "^virtio-net: eth0 $mac_b," "$tag/b: MAC $mac_b"
    [ "$(net_count b 'wget: HTTP/1\.0 200 OK$')" -eq 2 ] || e2e_fail "$tag/b: not two 200 responses from a"
    expect_serial "wget: body 6 bytes, fnv1a $hello\$" "$tag/b: / arrived whole (6 bytes, FNV-1a $hello)"
    expect_serial '^(> )?hello$' "$tag/b: / printed"
    expect_serial "wget: body 65536 bytes, fnv1a $big\$" "$tag/b: /big arrived whole (65536 bytes, FNV-1a $big)"
    expect_serial "udpecho: reply '$text' from $ra ok\$" "$tag/b: UDP echo from a"
    expect_serial "ping: reply from $ra seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms" "$tag/b: PING.ELF, an echo reply from a"
    expect_serial "ping: $ra 3 sent, [2-3] received" "$tag/b: PING.ELF summary"
}

# ---- (231, 233-236) several SamOS guests on one segment ----------------
# A pair of socket endpoints is point to point, so three or more guests
# meet at net_host.py's hub instead: guest k's -netdev socket sends to
# the hub's port k and listens on its own; the hub is a learning switch
# and, for the loss tests, drops every DROP-th and holds back every
# REORDER-th TCP frame from one guest to another (net_host.py, hub).
# Guest k (0, 1, 2 ...) is 10.0.2.(15 + k) with MAC 52:54:00:12:34:(56 + k).
#
#   net_hub <guests> [drop] [reorder] [synack] [fin] [finfrom]
#                                start the hub in the background; guest
#                                k's netdev is NET_HUB_NETDEV[k], the log
#                                $E2E_TMP/hub.log; synack: drop every
#                                SYN-ACK for that many seconds from the
#                                first; fin: drop every FIN guest
#                                finfrom (default 1) sends for that many
#                                seconds from the first
#   net_hub_stop                 stop it: it logs every TCP flow that
#                                carried data, then "hub: done"
#   net_ip <k>, net_mac <k>      guest k's address and MAC
#   net_flag <flag>              (monitor script) raise a flag file
#   net_flags_wait <s> <flag>... (monitor script) wait until all are up
#   net_netstat_sockets <name>   the last "sockets in use" count netstat
#                                printed in guest <name>'s log
#   net_hub_overlap <ERE>        how many connections were carrying data
#                                at the same time, at most: the hub's
#                                flows matching ERE, both directions of
#                                a connection as one span from its first
#                                data frame to its last
#   net_transport                the network follow-ups: E2E_NET from the environment (pci
#                                by default, or mmio: virtio-net-device),
#                                skipped where the machine has no such
#                                NIC; the hub is the same for both
net_ip() { printf '10.0.2.%d' $((15 + $1)); }

net_transport() {
    E2E_NET=${E2E_NET:-pci}
    case $E2E_NET in
        pci)  e2e_arch_has virtio-pci || e2e_skip "E2E_NET=pci: no virtio-net-pci on $E2E_ARCH" ;;
        mmio) e2e_arch_has virtio-mmio || e2e_skip "E2E_NET=mmio: no virtio-mmio on $E2E_ARCH" ;;
        *)    e2e_fail "E2E_NET=$E2E_NET: pci or mmio" ;;
    esac
}
net_mac() { printf '52:54:00:12:34:%02x' $((0x56 + $1)); }

net_hub() {
    local n=$1 drop=${2:-0} reorder=${3:-0} synack=${4:-0} fin=${5:-0} finfrom=${6:-1} ports k pairs=
    ports=$(net_host udpports $((2 * n))) || e2e_skip "python3 for net_host.py"
    # shellcheck disable=SC2206
    ports=($ports)
    NET_HUB_NETDEV=()
    for ((k = 0; k < n; k++)); do
        pairs="$pairs,${ports[2 * k]}:${ports[2 * k + 1]}"
        NET_HUB_NETDEV[k]="socket,id=net0,udp=127.0.0.1:${ports[2 * k]},localaddr=127.0.0.1:${ports[2 * k + 1]}"
    done
    NET_HUB_LOG="$E2E_TMP/hub.log"
    python3 -I "$NET_HOST" hub "${pairs#,}" 3600 "$NET_HUB_LOG" "$drop" "$reorder" "$synack" "$fin" "$finfrom" > "$E2E_TMP/hub.err" 2>&1 &
    NET_HUB_PID=$!
    E2E_SERIAL="$NET_HUB_LOG" e2e_wait_serial '^hub: ' 20 || e2e_fail "the hub did not start: $(cat "$E2E_TMP/hub.err")"
    e2e_log "hub: $n guests on ${pairs#,}, drop ${drop}, reorder ${reorder}, SYN-ACK blackout ${synack}"
}

net_hub_stop() {
    touch "$NET_HUB_LOG.stop"
    wait "$NET_HUB_PID" 2> /dev/null
    grep '^hub: done$' "$NET_HUB_LOG" > /dev/null || e2e_fail "the hub did not finish: $(cat "$E2E_TMP/hub.err")"
}

net_flag() {
    touch "$E2E_TMP/flag.$1"
}

net_flags_wait() {
    local limit=$1 t=0 f ok
    shift
    while [ $t -lt $((limit * 2)) ]; do
        ok=1
        for f in "$@"; do
            [ -e "$E2E_TMP/flag.$f" ] || ok=0
        done
        [ $ok = 1 ] && return 0
        sleep 0.5
        t=$((t + 1))
    done
    e2e_log "flags $* not all up after ${limit} s"
    return 1
}

net_netstat_sockets() {
    tr -d '\r' < "$E2E_TMP/$1.serial" | sed -n 's/^\(> \)\{0,1\}sockets in use: \([0-9]*\) of 64$/\2/p' | tail -n 1
}

net_hub_overlap() {
    grep -E '^hub: flow ' "$NET_HUB_LOG" | grep -E -- "$1" \
        | awk '{ for (i = 1; i < NF; i++) { if ($i == "first") s = $(i + 1); if ($i == "last") e = $(i + 1) }
                 k = $3 < $5 ? $3 " " $5 : $5 " " $3
                 if (!(k in b) || s < b[k]) b[k] = s
                 if (!(k in f) || e > f[k]) f[k] = e }
               END { for (k in b) { print b[k], 1; print f[k], -1 } }' \
        | sort -k1,1g -k2,2n | awk '{ c += $2; if (c > m) m = c } END { print m + 0 }'
}

# ---- (232) a SamOS guest as a router ------------------------------------
# Three guests on two links, each link a pair of -netdev socket UDP
# endpoints pointing at each other (as 229's):
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# b has two NICs (E2E_NETDEV2, E2E_NET_MAC2: _lib.sh, e2e_net_args), an
# address on each (samos.ip, samos.ip.eth1), no default route and
# samos.ip_forward. a's default route is b (samos.gw); c has only a
# static route to a's subnet (samos.gw=none samos.route=...).
#
#   net_router_test <transport> [tag]
#                         b boots first, then c (HTTPD.ELF, UDPECHO.ELF -s),
#                         then a once c listens (WGET.ELF / and /big,
#                         UDPECHO.ELF to c, PING.ELF c, PING.ELF -t 1 c,
#                         UDPECHO.ELF -s); then c's shell pings a and sends
#                         a datagram to it, and b's shell shows its tables
#   net_ifconfig_count <name> <if> <rx|tx>
#                         packets on <if> in guest <name>'s last ifconfig
net_ifconfig_count() {
    tr -d '\r' 2> /dev/null < "$E2E_TMP/$1.serial" | awk -v intf="$2" -v dir="$3" '
        /Interface Name : / { cur = ($0 ~ ("Interface Name : " intf ",")) }
        cur && /^[[:space:]]*rx [0-9]+ packets/ {
            for(i = 1; i <= NF; i++){ if($i == dir){ n = $(i + 1) } }
            cur = 0
        }
        END { print n + 0 }'
}

# _net_wait_file <file> <file>: until either exists in $E2E_TMP, at
# most limit*5 s (a monitor script's wait for another guest).
_net_wait_file() {
    local t=0
    while [ $t -lt $((${E2E_SHELL_BOOT_TIMEOUT:-120} * 10)) ] && [ ! -e "$E2E_TMP/$1" ] && [ ! -e "$E2E_TMP/$2" ]; do
        sleep 0.5
        t=$((t + 1))
    done
}

net_router_test() {
    local transport=$1 tag=${2:-$1} pa pb0 pb1 pc
    pa=$(net_host udpport) || e2e_skip "python3 for net_host.py"
    pb0=$(net_host udpport)
    while [ "$pb0" = "$pa" ]; do pb0=$(net_host udpport); done
    pb1=$(net_host udpport)
    while [ "$pb1" = "$pa" ] || [ "$pb1" = "$pb0" ]; do pb1=$(net_host udpport); done
    pc=$(net_host udpport)
    while [ "$pc" = "$pa" ] || [ "$pc" = "$pb0" ] || [ "$pc" = "$pb1" ]; do pc=$(net_host udpport); done
    local ip_a=10.1.0.2 ip_b0=10.1.0.1 ip_b1=10.2.0.1 ip_c=10.2.0.2
    local mac_a=52:54:00:12:34:5a mac_b0=52:54:00:12:34:5b mac_b1=52:54:00:12:34:5c mac_c=52:54:00:12:34:5d
    local text_ac=a-to-c text_ca=c-to-a limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
    net_host big "$E2E_TMP/big.expect"
    printf 'hello\n' > "$E2E_TMP/hello.expect"
    # samos.run comes last on the line (kernel.c reads it to the end).
    net_guest_image b "samos.ip=$ip_b0/24 samos.ip.eth1=$ip_b1/24 samos.gw=none samos.ip_forward"
    net_guest_image c "samos.ip=$ip_c/24 samos.gw=none samos.route=10.1.0.0/24@$ip_b1 samos.run=HTTPD.ELF 80 2;UDPECHO.ELF -s 7 1"
    net_guest_image a "samos.ip=$ip_a/24 samos.gw=$ip_b0 samos.run=UDPECHO.ELF -s 7 1;WGET.ELF $ip_c 80 /;WGET.ELF $ip_c 80 /big;UDPECHO.ELF $ip_c 7 $text_ac;PING.ELF $ip_c 3;PING.ELF -t 1 $ip_c 1"
    E2E_NET=$transport
    local rc
    rc=$(net_re "$ip_c")
    # a's programs are done when each client has printed its last line.
    _net_a_done() {
        [ "$(net_count a 'wget: body ')" -ge 2 ] \
            && [ "$(net_count a 'udpecho: (reply|no matching)')" -ge 1 ] \
            && [ "$(net_count a "ping: $rc [0-9]+ sent")" -ge 2 ]
    }
    # b: up to the shell; once c is done, its tables.
    _net_b_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        _net_wait_file c.done c.gone
        shell_cmd 'route'
        shell_cmd 'ifconfig'
        shell_cmd 'arp'
        printf 'quit\n'
    }
    # c: up to the shell; once a's programs are done, ping a, a datagram
    # to a's UDPECHO.ELF -s, its tables.
    _net_c_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        _net_wait_file a.progs a.gone
        shell_cmd "ping $ip_a 3" 60
        shell_cmd "UDPECHO.ELF $ip_a 7 $text_ca" 60
        shell_cmd 'route'
        shell_cmd 'arp'
        touch "$E2E_TMP/c.done"
        printf 'quit\n'
    }
    # a: its programs, its tables, then up until c is done with it.
    _net_a_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        local t=0
        while [ $t -lt $((limit * 10)) ] && ! _net_a_done; do
            sleep 0.5
            t=$((t + 1))
        done
        shell_cmd 'route'
        shell_cmd 'arp'
        touch "$E2E_TMP/a.progs"
        _net_wait_file c.done c.gone
        printf 'quit\n'
    }
    e2e_log "$tag: a $ip_a $mac_a :$pa <-> b eth0 $ip_b0 $mac_b0 :$pb0; b eth1 $ip_b1 $mac_b1 :$pb1 <-> c $ip_c $mac_c :$pc"
    E2E_NETDEV2="socket,id=net1,udp=127.0.0.1:$pc,localaddr=127.0.0.1:$pb1" E2E_NET_MAC2=$mac_b1 \
        net_guest b "$mac_b0" "socket,id=net0,udp=127.0.0.1:$pa,localaddr=127.0.0.1:$pb0" $((limit * 14)) _net_b_mon &
    local bpid=$! cpid=
    if E2E_SERIAL="$E2E_TMP/b.serial" e2e_wait_serial '^net: IP forwarding on$' $((limit * 3)); then
        net_guest c "$mac_c" "socket,id=net0,udp=127.0.0.1:$pb1,localaddr=127.0.0.1:$pc" $((limit * 12)) _net_c_mon &
        cpid=$!
        # a boots once c's servers listen: WGET.ELF and UDPECHO.ELF try
        # once (a refused connect is not retried).
        if E2E_SERIAL="$E2E_TMP/c.serial" e2e_wait_serial '^httpd: listening on port 80$' $((limit * 3)) \
            && E2E_SERIAL="$E2E_TMP/c.serial" e2e_wait_serial '^udpecho: listening on port 7$' 60; then
            net_guest a "$mac_a" "socket,id=net0,udp=127.0.0.1:$pb0,localaddr=127.0.0.1:$pa" $((limit * 10)) _net_a_mon
        else
            e2e_log "$tag: c's servers never listened, a not started"
        fi
    else
        e2e_log "$tag: b never turned forwarding on, a and c not started"
    fi
    touch "$E2E_TMP/a.gone"
    [ -n "$cpid" ] && wait "$cpid"
    touch "$E2E_TMP/c.gone"
    wait "$bpid"

    local ra rb0 rb1 hello big n intf dir
    ra=$(net_re "$ip_a")
    rb0=$(net_re "$ip_b0")
    rb1=$(net_re "$ip_b1")
    hello=$(net_host fnv "$E2E_TMP/hello.expect")
    big=$(net_host fnv "$E2E_TMP/big.expect")

    net_on b
    net_expect_no_fault "$tag/b"
    expect_serial "^virtio-net: eth0 $mac_b0, $transport, " "$tag/b: eth0 is the first NIC ($mac_b0)"
    expect_serial "^virtio-net: eth1 $mac_b1, $transport, " "$tag/b: eth1 is the second NIC ($mac_b1)"
    expect_serial "^net: eth0 $rb0/24\$" "$tag/b: eth0 $ip_b0/24, no gateway"
    expect_serial "^net: eth1 $rb1/24\$" "$tag/b: eth1 $ip_b1/24"
    expect_serial "$SHELL_BANNER" "$tag/b: shell up"
    expect_serial '^IP forwarding on: [0-9]+ forwarded' "$tag/b: route shows forwarding on"
    n=$(tr -d '\r' < "$E2E_TMP/b.serial" | sed -n 's/^IP forwarding on: \([0-9]*\) forwarded.*/\1/p' | tail -n 1)
    [ "${n:-0}" -ge 50 ] || e2e_fail "$tag/b: ${n:-no} datagrams forwarded, expected at least 50"
    e2e_log "$tag/b: $n datagrams forwarded"
    expect_serial '^IP forwarding on: [0-9]+ forwarded, [1-9][0-9]* TTL expired' "$tag/b: a TTL 1 datagram expired at b"
    for intf in eth0 eth1; do
        for dir in rx tx; do
            n=$(net_ifconfig_count b $intf $dir)
            [ "$n" -ge 50 ] || e2e_fail "$tag/b: $intf $dir $n packets, expected at least 50"
        done
        e2e_log "$tag/b: $intf rx $(net_ifconfig_count b $intf rx) tx $(net_ifconfig_count b $intf tx) packets"
    done
    expect_serial "^IP : $ra, MAC : $mac_a, OIF = eth0\$" "$tag/b: ARP learned a on eth0"
    expect_serial "^IP : $rc, MAC : $mac_c, OIF = eth1\$" "$tag/b: ARP learned c on eth1"

    net_on c
    net_expect_no_fault "$tag/c"
    expect_serial "^virtio-net: eth0 $mac_c," "$tag/c: MAC $mac_c"
    expect_serial "^net: eth0 $rc/24\$" "$tag/c: eth0 $ip_c/24, no gateway"
    expect_serial "^net: route 10\\.1\\.0\\.0/24 via $rb1 on eth0\$" "$tag/c: static route to a's subnet"
    expect_serial "httpd: request from $ra:[0-9]+, 200, 89 bytes\$" "$tag/c: httpd answered a's /"
    expect_serial "httpd: GET /big from $ra:[0-9]+, 200, 65536 bytes\$" "$tag/c: httpd answered a's /big"
    expect_serial "udpecho: ${#text_ac} bytes from $ra:[0-9]+, echoed\$" "$tag/c: echoed a's datagram"
    net_shell_ping_check "$tag/c" "$ip_a" 2
    expect_serial "udpecho: reply '$text_ca' from $ra ok\$" "$tag/c: UDP echo from a"
    expect_serial "10\\.1\\.0\\.0 +24 +$rb1 +eth0" "$tag/c: route shows 10.1.0.0/24 via b"
    expect_serial '^IP forwarding off: 0 forwarded' "$tag/c: c is a host"
    expect_serial "^IP : $rb1, MAC : $mac_b1, OIF = eth0\$" "$tag/c: ARP learned b's eth1"
    [ "$(net_count c "^IP : $ra,")" -eq 0 ] || e2e_fail "$tag/c: a in c's ARP table, but a is not on c's link"

    net_on a
    net_expect_no_fault "$tag/a"
    expect_serial "^virtio-net: eth0 $mac_a," "$tag/a: MAC $mac_a"
    expect_serial "^net: eth0 $ra/24, gateway $rb0\$" "$tag/a: eth0 $ip_a/24, gateway b"
    expect_serial "0\\.0\\.0\\.0 +0 +$rb0 +eth0" "$tag/a: default route via b"
    [ "$(net_count a 'wget: HTTP/1\.0 200 OK$')" -eq 2 ] || e2e_fail "$tag/a: not two 200 responses from c"
    expect_serial "wget: body 6 bytes, fnv1a $hello\$" "$tag/a: / arrived whole (6 bytes, FNV-1a $hello)"
    expect_serial "wget: body 65536 bytes, fnv1a $big\$" "$tag/a: /big arrived whole (65536 bytes, FNV-1a $big)"
    expect_serial "udpecho: reply '$text_ac' from $rc ok\$" "$tag/a: UDP echo from c"
    expect_serial "udpecho: ${#text_ca} bytes from $rc:[0-9]+, echoed\$" "$tag/a: echoed c's datagram"
    expect_serial "ping: reply from $rc seq=[0-9]+ time=[0-9]+\\.[0-9]{3} ms ttl=63\$" \
        "$tag/a: PING.ELF, an echo reply from c with TTL 63 (64 less b's hop)"
    expect_serial "ping: $rc 3 sent, [2-3] received" "$tag/a: PING.ELF summary"
    expect_serial "ping: time exceeded from $rb0 seq=1 " "$tag/a: PING.ELF -t 1: Time Exceeded from b"
    expect_serial "ping: $rc 1 sent, 0 received" "$tag/a: PING.ELF -t 1 got no echo reply"
    expect_serial "^IP : $rb0, MAC : $mac_b0, OIF = eth0\$" "$tag/a: ARP learned b"
    [ "$(net_count a "^IP : $rc,")" -eq 0 ] || e2e_fail "$tag/a: c in a's ARP table, but c is not on a's link"
}
