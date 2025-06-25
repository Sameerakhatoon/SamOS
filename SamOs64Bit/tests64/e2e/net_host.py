#!/usr/bin/env python3
# The host's side of the TCP and UDP e2e tests (217-220). Not a
# test itself.
#
#   net_host.py tcpport                   a free TCP port on 127.0.0.1
#   net_host.py udpport                   a free UDP port on 127.0.0.1
#   net_host.py big FILE                  write httpd's /big body (HTTPD_BIG
#                                         bytes, programs/net/httpd.c)
#   net_host.py fnv FILE                  FNV-1a of FILE, as wget prints it
#   net_host.py udpecho PORT SECONDS      echo datagrams on 127.0.0.1:PORT
#   net_host.py udpsend PORT TEXT         send TEXT to 127.0.0.1:PORT, print
#                                         "udpsend: echo ok" when it returns
#   net_host.py relay PORTS DROP SECONDS LOG
#                                         the lossy link for e2e/220: QEMU's
#                                         filter-redirector chardevs are TCP
#                                         servers on the four PORTS (a,b,c,d:
#                                         guest->net out, in, net->guest out,
#                                         in); every frame read from a is
#                                         written to b, from c to d, except
#                                         every DROP-th TCP frame each way
import os
import socket
import struct
import sys
import time

HTTPD_BIG = 65536


def free_port(kind):
    s = socket.socket(socket.AF_INET, kind)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def big_body():
    return bytes((ord('\n') if k % 64 == 63 else ord('A') + k % 26) for k in range(HTTPD_BIG))


def fnv(data):
    h = 2166136261
    for c in data:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return '%x' % h


def connect_retry(port, until):
    while time.time() < until:
        try:
            return socket.create_connection(('127.0.0.1', port), timeout=1)
        except OSError:
            time.sleep(0.2)
    raise SystemExit('relay: no chardev on port %d' % port)


def relay(ports, drop, seconds, logname):
    # QEMU's filter-mirror wire format: a 4-byte big-endian length, then
    # (vnet_hdr_support off) the frame.
    until = time.time() + seconds
    socks = [connect_retry(int(p), until) for p in ports.split(',')]
    log = open(logname, 'w', buffering=1)
    pairs = {socks[0]: (socks[1], 'out'), socks[2]: (socks[3], 'in')}
    bufs = {socks[0]: b'', socks[2]: b''}
    count = {'out': 0, 'in': 0}
    dropped = {'out': 0, 'in': 0}
    import select
    while time.time() < until:
        r, _, _ = select.select(list(pairs), [], [], 0.5)
        for s in r:
            data = s.recv(65536)
            if not data:
                log.write('relay: closed\n')
                return
            bufs[s] += data
            while len(bufs[s]) >= 4:
                n = struct.unpack('!I', bufs[s][:4])[0]
                if len(bufs[s]) < 4 + n:
                    break
                frame, bufs[s] = bufs[s][4:4 + n], bufs[s][4 + n:]
                dst, way = pairs[s]
                tcp = len(frame) > 34 and frame[12:14] == b'\x08\x00' and frame[23] == 6
                if tcp:
                    count[way] += 1
                    if count[way] % drop == 0:
                        dropped[way] += 1
                        log.write('relay: dropped TCP frame %d %s (%d bytes)\n' % (count[way], way, len(frame)))
                        continue
                dst.sendall(struct.pack('!I', len(frame)) + frame)
    log.write('relay: TCP frames out %d (dropped %d), in %d (dropped %d)\n' % (
        count['out'], dropped['out'], count['in'], dropped['in']))


def main():
    cmd = sys.argv[1]
    if cmd == 'tcpport':
        print(free_port(socket.SOCK_STREAM))
    elif cmd == 'udpport':
        print(free_port(socket.SOCK_DGRAM))
    elif cmd == 'big':
        open(sys.argv[2], 'wb').write(big_body())
    elif cmd == 'fnv':
        print(fnv(open(sys.argv[2], 'rb').read()))
    elif cmd == 'udpecho':
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(('127.0.0.1', int(sys.argv[2])))
        s.settimeout(0.5)
        until = time.time() + float(sys.argv[3])
        while time.time() < until:
            try:
                data, peer = s.recvfrom(2048)
            except socket.timeout:
                continue
            s.sendto(data, peer)
            print('udpecho-host: echoed %d bytes' % len(data), flush=True)
    elif cmd == 'udpsend':
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(2)
        text = sys.argv[3].encode()
        for _ in range(5):
            s.sendto(text, ('127.0.0.1', int(sys.argv[2])))
            try:
                data, _ = s.recvfrom(2048)
            except socket.timeout:
                continue
            print('udpsend: echo ok' if data == text else 'udpsend: echo mismatch %r' % data)
            return 0 if data == text else 1
        print('udpsend: no echo')
        return 1
    elif cmd == 'relay':
        relay(sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), sys.argv[5])
    return 0


if __name__ == '__main__':
    sys.exit(main())
