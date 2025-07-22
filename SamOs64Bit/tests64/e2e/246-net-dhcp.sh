#!/usr/bin/env bash
# e2e/246-net-dhcp.sh - samos.ip=dhcp. SamOS asks QEMU's user
# network (slirp's DHCP server) for eth0's address at boot: DISCOVER,
# OFFER, REQUEST, ACK (net/dhcp.c). The lease gives the address, the
# mask and the default route; the boot log, ifconfig (the server and
# the lease time) and route show it, and ping reaches the gateway.
# Each machine boots twice: once on slirp's default 10.0.2.0/24 (the
# lease is 10.0.2.15/24 via 10.0.2.2, what the static default would
# be) and once on net=10.9.8.0/24, where only DHCP can know the
# address (10.9.8.15/24 via 10.9.8.2, DNS 10.9.8.3). x86: pc and q35
# on virtio-net-pci. aarch64 and riscv64 virt: virtio-net-pci and
# virtio-net-device.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

net_cmdline "samos.ip=dhcp"

# _dhcp_boot <tag> <transport> <a.b.c>: boot on slirp's a.b.c.0/24 and check.
_dhcp_boot() {
    local tag=$1 transport=$2 net=$3 p
    p=$(net_re "$net")
    E2E_NET=$transport
    E2E_NET_OPTS=
    [ "$net" = 10.0.2 ] || E2E_NET_OPTS=",net=$net.0/24"
    _dhcp_mon() {
        shell_boot || { printf 'quit\n'; return 0; }
        shell_cmd 'ifconfig'
        shell_cmd 'route'
        shell_cmd "ping $net.2 3" 60
        shell_cmd 'arp'
        printf 'quit\n'
    }
    e2e_run_qemu 400 _dhcp_mon
    cp "$E2E_SERIAL" "$E2E_TMP/serial-$tag.log"
    net_expect_no_fault "$tag"
    expect_serial "^virtio-net: eth0 52:54:00:12:34:56, $transport, " "$tag: virtio-net eth0 on $transport"
    expect_serial "^net: eth0 DHCP $p\\.15/24 from $p\\.2, router $p\\.2, DNS $p\\.3, lease 86400 s\$" \
        "$tag: a DHCP lease for $net.15/24 from $net.2"
    expect_no_serial '^net: eth0 DHCP: ' "$tag: no lost offer or answer"
    expect_serial "^net: eth0 $p\\.15/24, gateway $p\\.2\$" "$tag: eth0 configured from the lease"
    expect_serial "$SHELL_BANNER" "$tag: shell up"
    expect_serial "IP Addr = $p\\.15/24" "$tag: ifconfig shows the leased address"
    expect_serial "address from DHCP server $p\\.2, lease 86400 s, not renewed\$" "$tag: ifconfig says it came from DHCP"
    expect_serial "0\\.0\\.0\\.0 +0 +$p\\.2 +eth0" "$tag: default route via the lease's router"
    net_shell_ping_check "$tag" "$net.2" 2
    expect_serial "^IP : $p\\.2, MAC : 52:55:[0-9a-f:]+, OIF = eth0\$" "$tag: ARP learned the gateway"
    e2e_log "$tag: $(tr -d '\r' < "$E2E_SERIAL" | grep -m1 '^net: eth0 DHCP ')"
}

if [ "$E2E_ARCH" = x86_64 ]; then
    E2E_MACHINE=pc _dhcp_boot pc/pci pci 10.0.2
    E2E_MACHINE=q35 _dhcp_boot q35/pci pci 10.9.8
else
    _dhcp_boot pci pci 10.0.2
    e2e_arch_has virtio-mmio && _dhcp_boot mmio mmio 10.9.8
fi
echo "PASS: e2e/246 samos.ip=dhcp: address, mask and default route from slirp's DHCP server, shown by ifconfig and route, ping the gateway"
