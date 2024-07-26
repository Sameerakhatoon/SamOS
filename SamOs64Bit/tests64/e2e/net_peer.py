#!/usr/bin/env python3
# A host on the far end of QEMU's -netdev socket (udp mode: one
# Ethernet frame per datagram), for the e2e tests that need the guest
# to answer: e2e/214 and 215. Not a test itself.
#
#   net_peer.py ports
#       print two free UDP ports on 127.0.0.1: the peer's and QEMU's
#   net_peer.py run PEER_PORT QEMU_PORT LOG SECONDS [PINGS]
#       be 10.0.2.2 (MAC 52:55:0a:00:02:02) on the guest's wire:
#       - answer ARP requests for 10.0.2.2 and ICMP echo requests to it,
#         so the guest's own ping works;
#       - once the guest has sent anything (its gratuitous ARP), resolve
#         10.0.2.15 by ARP, then send PINGS echo requests (56 data bytes)
#         and check each reply (IP and ICMP checksums, id, sequence,
#         data), plus one request with a bad ICMP checksum, which must
#         get no reply;
#       - log what happened to LOG, ending in "peer: PASS ..." or
#         "peer: FAIL ...", and keep answering until SECONDS have
#         passed or LOG.stop exists.
import os
import socket
import struct
import sys
import time

PEER_MAC = bytes.fromhex('52550a000202')
PEER_IP = bytes([10, 0, 2, 2])
GUEST_IP = bytes([10, 0, 2, 15])
BCAST = b'\xff' * 6


def csum(data):
    if len(data) % 2:
        data += b'\0'
    s = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff


def ip_str(b):
    return '.'.join(str(x) for x in b)


def mac_str(b):
    return ':'.join('%02x' % x for x in b)


def arp(op, smac, sip, tmac, tip, dst):
    body = struct.pack('!HHBBH6s4s6s4s', 1, 0x0800, 6, 4, op, smac, sip, tmac, tip)
    return dst + PEER_MAC + b'\x08\x06' + body


def ipv4(proto, src, dst, payload, ident=0x5350):
    hdr = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(payload), ident, 0, 64, proto, 0, src, dst)
    hdr = hdr[:10] + struct.pack('!H', csum(hdr)) + hdr[12:]
    return hdr + payload


def icmp_echo(typ, ident, seq, data, bad=False):
    msg = struct.pack('!BBHHH', typ, 0, 0, ident, seq) + data
    c = csum(msg) ^ (1 if bad else 0)
    return msg[:2] + struct.pack('!H', c) + msg[4:]


def main():
    if sys.argv[1] == 'ports':
        socks = []
        for _ in range(2):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind(('127.0.0.1', 0))
            socks.append(s)
        print(socks[0].getsockname()[1], socks[1].getsockname()[1])
        return 0
    peer_port, qemu_port, logname, seconds = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], float(sys.argv[5])
    pings = int(sys.argv[6]) if len(sys.argv) > 6 else 4
    log = open(logname, 'w', buffering=1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', peer_port))
    sock.settimeout(0.2)
    qemu = ('127.0.0.1', qemu_port)

    def send(frame):
        if len(frame) < 60:
            frame += b'\0' * (60 - len(frame))
        sock.sendto(frame, qemu)

    end = time.time() + seconds
    guest_mac = None
    resolved = False
    next_arp = 0
    sent = {}
    got = set()
    next_ping = 0
    seq = 0
    bad_sent = 0
    verdict = None
    while time.time() < end and not os.path.exists(logname + '.stop'):
        now = time.time()
        if guest_mac and not resolved and now >= next_arp:
            send(arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP, BCAST))
            next_arp = now + 1
        if resolved and seq < pings and now >= next_ping:
            seq += 1
            data = bytes((seq + i) & 0xff for i in range(56))
            sent[seq] = data
            send(guest_mac + PEER_MAC + b'\x08\x00' + ipv4(1, PEER_IP, GUEST_IP, icmp_echo(8, 0x5350, seq, data)))
            next_ping = now + 0.5
        if resolved and seq == pings and not bad_sent and len(got) == pings:
            send(guest_mac + PEER_MAC + b'\x08\x00' + ipv4(1, PEER_IP, GUEST_IP, icmp_echo(8, 0x5350, 99, b'bad', True)))
            bad_sent = now
        if bad_sent and verdict is None and now - bad_sent > 2:
            verdict = 'PASS pings %d/%d, bad checksum ignored' % (len(got), pings)
            log.write('peer: %s\n' % verdict)
        try:
            frame = sock.recv(4096)
        except socket.timeout:
            continue
        if len(frame) < 14:
            continue
        dst, src, etype = frame[0:6], frame[6:12], struct.unpack('!H', frame[12:14])[0]
        if guest_mac is None and src != PEER_MAC:
            guest_mac = src
            log.write('peer: guest MAC %s\n' % mac_str(src))
        if etype == 0x0806 and len(frame) >= 42:
            _, _, _, _, op, smac, sip, tmac, tip = struct.unpack('!HHBBH6s4s6s4s', frame[14:42])
            if sip == tip == GUEST_IP:
                log.write('peer: gratuitous ARP %s is-at %s\n' % (ip_str(sip), mac_str(smac)))
            elif op == 1 and tip == PEER_IP:
                log.write('peer: ARP request who-has %s tell %s, answered\n' % (ip_str(tip), ip_str(sip)))
                send(arp(2, PEER_MAC, PEER_IP, smac, sip, smac))
            elif op == 2 and sip == GUEST_IP and tip == PEER_IP and tmac == PEER_MAC:
                if not resolved:
                    log.write('peer: ARP reply %s is-at %s\n' % (ip_str(sip), mac_str(smac)))
                resolved = True
                guest_mac = smac
        elif etype == 0x0800 and len(frame) >= 34:
            ip = frame[14:]
            ihl = (ip[0] & 15) * 4
            total = struct.unpack('!H', ip[2:4])[0]
            if ip[0] >> 4 != 4 or csum(ip[:ihl]) != 0 or total > len(ip):
                log.write('peer: bad IPv4 header from the guest: %s\n' % ip[:20].hex())
                verdict = verdict or 'FAIL bad IPv4 header'
                continue
            proto, src_ip, dst_ip, body = ip[9], ip[12:16], ip[16:20], ip[ihl:total]
            if proto != 1 or len(body) < 8:
                continue
            if csum(body) != 0:
                log.write('peer: bad ICMP checksum from the guest\n')
                verdict = verdict or 'FAIL bad ICMP checksum'
                continue
            typ, _, _, ident, s = struct.unpack('!BBHHH', body[:8])
            if typ == 8 and dst_ip == PEER_IP:
                log.write('peer: echo request seq=%d from %s, answered\n' % (s, ip_str(src_ip)))
                reply = icmp_echo(0, ident, s, body[8:])
                send(src + PEER_MAC + b'\x08\x00' + ipv4(1, PEER_IP, src_ip, reply))
            elif typ == 0 and src_ip == GUEST_IP:
                if s == 99:
                    log.write('peer: the bad-checksum request was answered\n')
                    verdict = 'FAIL answered a bad checksum'
                elif ident == 0x5350 and sent.get(s) == body[8:]:
                    got.add(s)
                    log.write('peer: echo reply seq=%d from %s ok\n' % (s, ip_str(src_ip)))
                else:
                    log.write('peer: echo reply seq=%d does not match\n' % s)
                    verdict = verdict or 'FAIL echo reply mismatch'
    if verdict is None:
        verdict = 'FAIL timed out (guest MAC %s, ARP %s, replies %d/%d)' % (
            mac_str(guest_mac) if guest_mac else 'none', 'resolved' if resolved else 'unresolved', len(got), pings)
        log.write('peer: %s\n' % verdict)
    return 0 if verdict.startswith('PASS') else 1


if __name__ == '__main__':
    sys.exit(main())
