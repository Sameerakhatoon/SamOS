#!/usr/bin/env bash
# The router layout of e2e 232 for 248-251. Sourced after
# _lib.sh, _shell_lib.sh and _net_lib.sh. Not a test itself.
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# b has two NICs, no default route and samos.ip_forward; a's default
# route is b; c has no default route (the test gives it samos.route or
# not). Each link is a pair of -netdev socket UDP endpoints, as in 232.
#
#   net_router3 <transport> <tag> <b words> <c words> <a words>
#                    b's, c's and a's command lines get these words after
#                    their addresses. b boots first; c once b prints
#                    "net: IP forwarding on"; a once c's monitor touches
#                    $E2E_TMP/c.up (or c's QEMU is gone). The test defines
#                    the three monitor scripts _r3_b_mon, _r3_c_mon and
#                    _r3_a_mon; they wait for each other through files in
#                    $E2E_TMP (_net_wait_file). When a's QEMU is gone,
#                    a.gone is touched, then c.gone when c's is.
#   r3_values <name> <sed expression>
#                    the values a sed -n 's/.../\1/p' takes from guest
#                    <name>'s log, one per line, in order
R3_IP_A=10.1.0.2 R3_IP_B0=10.1.0.1 R3_IP_B1=10.2.0.1 R3_IP_C=10.2.0.2
R3_MAC_A=52:54:00:12:34:5a R3_MAC_B0=52:54:00:12:34:5b R3_MAC_B1=52:54:00:12:34:5c R3_MAC_C=52:54:00:12:34:5d

net_router3() {
    local transport=$1 tag=$2 bw=$3 cw=$4 aw=$5 pa pb0 pb1 pc limit=${E2E_SHELL_BOOT_TIMEOUT:-120}
    pa=$(net_host udpport) || e2e_skip "python3 for net_host.py"
    pb0=$(net_host udpport)
    while [ "$pb0" = "$pa" ]; do pb0=$(net_host udpport); done
    pb1=$(net_host udpport)
    while [ "$pb1" = "$pa" ] || [ "$pb1" = "$pb0" ]; do pb1=$(net_host udpport); done
    pc=$(net_host udpport)
    while [ "$pc" = "$pa" ] || [ "$pc" = "$pb0" ] || [ "$pc" = "$pb1" ]; do pc=$(net_host udpport); done
    # samos.run, if any, comes last on the line (kernel.c reads it to the end).
    net_guest_image b "samos.ip=$R3_IP_B0/24 samos.ip.eth1=$R3_IP_B1/24 samos.gw=none samos.ip_forward $bw"
    net_guest_image c "samos.ip=$R3_IP_C/24 samos.gw=none $cw"
    net_guest_image a "samos.ip=$R3_IP_A/24 samos.gw=$R3_IP_B0 $aw"
    E2E_NET=$transport
    e2e_log "$tag: a $R3_IP_A :$pa <-> b eth0 $R3_IP_B0 :$pb0; b eth1 $R3_IP_B1 :$pb1 <-> c $R3_IP_C :$pc"
    E2E_NETDEV2="socket,id=net1,udp=127.0.0.1:$pc,localaddr=127.0.0.1:$pb1" E2E_NET_MAC2=$R3_MAC_B1 \
        net_guest b "$R3_MAC_B0" "socket,id=net0,udp=127.0.0.1:$pa,localaddr=127.0.0.1:$pb0" $((limit * 14)) _r3_b_mon &
    local bpid=$! cpid=
    if E2E_SERIAL="$E2E_TMP/b.serial" e2e_wait_serial '^net: IP forwarding on$' $((limit * 3)); then
        net_guest c "$R3_MAC_C" "socket,id=net0,udp=127.0.0.1:$pb1,localaddr=127.0.0.1:$pc" $((limit * 12)) _r3_c_mon &
        cpid=$!
        local t=0
        while [ $t -lt $((limit * 30)) ] && [ ! -e "$E2E_TMP/c.up" ] && kill -0 "$cpid" 2> /dev/null; do
            sleep 0.2
            t=$((t + 1))
        done
        if [ -e "$E2E_TMP/c.up" ]; then
            net_guest a "$R3_MAC_A" "socket,id=net0,udp=127.0.0.1:$pb0,localaddr=127.0.0.1:$pa" $((limit * 10)) _r3_a_mon
        else
            e2e_log "$tag: c never came up, a not started"
        fi
    else
        e2e_log "$tag: b never turned forwarding on, a and c not started"
    fi
    touch "$E2E_TMP/a.gone"
    [ -n "$cpid" ] && wait "$cpid"
    touch "$E2E_TMP/c.gone"
    wait "$bpid"
}

r3_values() {
    tr -d '\r' 2> /dev/null < "$E2E_TMP/$1.serial" | sed -n "$2"
}

# r3_guests_ok <tag>: no fault on any guest, b's two NICs and addresses.
r3_guests_ok() {
    local tag=$1 g
    for g in a b c; do
        net_on $g
        net_expect_no_fault "$tag/$g"
    done
    net_on b
    expect_serial "^net: eth0 $(net_re $R3_IP_B0)/24\$" "$tag/b: eth0 $R3_IP_B0/24"
    expect_serial "^net: eth1 $(net_re $R3_IP_B1)/24\$" "$tag/b: eth1 $R3_IP_B1/24"
    expect_serial '^net: IP forwarding on$' "$tag/b: forwarding on"
    net_on a
    expect_serial "^net: eth0 $(net_re $R3_IP_A)/24, gateway $(net_re $R3_IP_B0)\$" "$tag/a: default route via b"
}
