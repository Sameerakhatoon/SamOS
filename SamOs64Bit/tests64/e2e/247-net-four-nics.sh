#!/usr/bin/env bash
# e2e/247-net-four-nics.sh - one guest with four NICs (eth0 to
# eth3, VNET_MAX_DEVICES), each on its own QEMU user network:
#
#   eth0 10.0.2.0/24  static (the default 10.0.2.15, gateway 10.0.2.2)
#   eth1 10.0.3.0/24  samos.ip.eth1=dhcp       -> 10.0.3.15
#   eth2 10.0.4.0/24  samos.ip.eth2=10.0.4.15/24
#   eth3 10.0.5.0/24  samos.ip.eth3=dhcp       -> 10.0.5.15
#
# Every NIC is bound and named in command line order (by MAC), the
# first two take MSI-X where the machine has it and the others a line,
# and the shell pings each network's gateway (slirp's x.x.x.2) out of
# its own NIC; the ARP table has each gateway on its NIC. x86: pc on
# virtio-net-pci. aarch64 and riscv64 virt: virtio-net-pci, then
# virtio-net-device.
# e2e-requires: net virtio-pci (virtio-net-pci NICs)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

net_cmdline "samos.ip.eth1=dhcp samos.ip.eth2=10.0.4.15/24 samos.ip.eth3=dhcp"

# _nics_boot <transport>
_nics_boot() {
    local tr=$1 k n p mac
    E2E_NET=$tr
    E2E_NET_MAC=52:54:00:12:34:60
    E2E_NETDEV2="user,id=net1,net=10.0.3.0/24" E2E_NET_MAC2=52:54:00:12:34:61
    E2E_NETDEV3="user,id=net2,net=10.0.4.0/24" E2E_NET_MAC3=52:54:00:12:34:62
    E2E_NETDEV4="user,id=net3,net=10.0.5.0/24" E2E_NET_MAC4=52:54:00:12:34:63
    _nics_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        local n
        for n in 2 3 4 5; do
            shell_cmd "ping 10.0.$n.2 3" 60
        done
        shell_cmd 'ifconfig'
        shell_cmd 'arp'
        printf 'quit\n'
    }
    e2e_run_qemu 500 _nics_mon
    unset E2E_NETDEV2 E2E_NETDEV3 E2E_NETDEV4 E2E_NET_MAC2 E2E_NET_MAC3 E2E_NET_MAC4
    E2E_NET_MAC=
    cp "$E2E_SERIAL" "$E2E_TMP/serial-$tr.log"
    net_expect_no_fault "$tr"
    for k in 0 1 2 3; do
        mac=52:54:00:12:34:6$k
        expect_serial "^virtio-net: eth$k $mac, $tr, " "$tr: eth$k is the NIC with $mac"
    done
    for k in 2 3; do
        expect_serial "^virtio-net: eth$k [0-9a-f:]+, $tr, line, GSI [0-9]+, " "$tr: eth$k on its line (MSI-X is for two NICs)"
    done
    expect_serial '^net: eth0 10\.0\.2\.15/24, gateway 10\.0\.2\.2$' "$tr: eth0 static, the default route"
    expect_serial '^net: eth1 DHCP 10\.0\.3\.15/24 from 10\.0\.3\.2, router 10\.0\.3\.2, DNS 10\.0\.3\.3, lease 86400 s$' \
        "$tr: eth1 leased 10.0.3.15"
    expect_serial '^net: eth1 10\.0\.3\.15/24$' "$tr: eth1 configured, no gateway (eth0 has the default route)"
    expect_serial '^net: eth2 10\.0\.4\.15/24$' "$tr: eth2 static"
    expect_serial '^net: eth3 DHCP 10\.0\.5\.15/24 from 10\.0\.5\.2, ' "$tr: eth3 leased 10.0.5.15"
    expect_serial '^net: eth3 10\.0\.5\.15/24$' "$tr: eth3 configured"
    expect_serial "$SHELL_BANNER" "$tr: shell up"
    [ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c 'address from DHCP server')" -ge 2 ] \
        || e2e_fail "$tr: ifconfig does not show eth1's and eth3's leases"
    for n in 2 3 4 5; do
        net_shell_ping_check "$tr" "10.0.$n.2" 2
        p=$(net_re "10.0.$n.2")
        expect_serial "^IP : $p, MAC : 52:55:[0-9a-f:]+, OIF = eth$((n - 2))\$" "$tr: ARP has 10.0.$n.2 on eth$((n - 2))"
    done
}

_nics_boot pci
e2e_arch_has virtio-mmio && _nics_boot mmio
echo "PASS: e2e/247 four NICs on one guest: bound in order, two by DHCP, each network's gateway pinged out of its own NIC"
