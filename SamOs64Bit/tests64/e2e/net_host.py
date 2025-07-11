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
#   net_host.py udpports N                N distinct free UDP ports
#   net_host.py big FILE [BYTES]          as big, BYTES long (httpd's
#                                         /big<KiB> bodies)
#   net_host.py hub PAIRS SECONDS LOG [DROP [REORDER [SYNACK [FIN [FINFROM]]]]]
#                                         One Ethernet segment for
#                                         several SamOS guests. PAIRS is
#                                         hubport:guestport,... ; guest k's
#                                         -netdev socket sends to hubport k
#                                         (udp=) and listens on guestport k
#                                         (localaddr=). A learning switch:
#                                         broadcasts and unknown MACs go to
#                                         every other guest, a known MAC to
#                                         its own. With DROP, every DROP-th
#                                         TCP frame from one guest to another
#                                         is dropped; with REORDER, every
#                                         REORDER-th is held back and sent
#                                         after the next one that way (or
#                                         after 40 ms). With SYNACK, every
#                                         SYN-ACK is dropped for SYNACK
#                                         seconds from the first one (a
#                                         client that gives up on connect
#                                         and later meets the server's
#                                         retransmitted SYN-ACK). With
#                                         FIN, every TCP frame with FIN set
#                                         that guest FINFROM (default 1)
#                                         sends is dropped for FIN seconds
#                                         from the first one (the peer of a
#                                         FIN_WAIT_2 that never closes, for
#                                         the orphan timeout). Runs until SECONDS
#                                         or LOG.stop exists, then logs
#                                         every TCP flow that carried data
#                                         (frames, bytes, first and last
#                                         time) and the totals.
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


def big_body(size=HTTPD_BIG):
    return bytes((ord('\n') if k % 64 == 63 else ord('A') + k % 26) for k in range(size))


def free_ports(kind, n):
    socks = []
    for _ in range(n):
        s = socket.socket(socket.AF_INET, kind)
        s.bind(('127.0.0.1', 0))
        socks.append(s)
    ports = [s.getsockname()[1] for s in socks]
    for s in socks:
        s.close()
    return ports


def tcp_info(frame):
    # (src ip, src port, dst ip, dst port, payload bytes) of an IPv4 TCP
    # frame, else None. The IP total length, not the frame's, as QEMU
    # pads short frames to 60 bytes.
    if len(frame) < 34 or frame[12:14] != b'\x08\x00' or frame[23] != 6:
        return None
    ihl = (frame[14] & 0xf) * 4
    total = struct.unpack('!H', frame[16:18])[0]
    t = 14 + ihl
    if len(frame) < t + 20:
        return None
    sport, dport = struct.unpack('!HH', frame[t:t + 4])
    doff = (frame[t + 12] >> 4) * 4
    src = socket.inet_ntoa(frame[26:30])
    dst = socket.inet_ntoa(frame[30:34])
    return src, sport, dst, dport, max(0, total - ihl - doff), frame[t + 13]


def hub(pairs, seconds, logname, drop=0, reorder=0, synack=0, fin=0, finfrom=1):
    import select
    start = time.time()
    until = start + seconds
    log = open(logname, 'w', buffering=1)
    socks, guests = [], []
    for p in pairs.split(','):
        hp, gp = p.split(':')
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(('127.0.0.1', int(hp)))
        socks.append(s)
        guests.append(('127.0.0.1', int(gp)))
    index = {s: k for k, s in enumerate(socks)}
    macs = {}                       # MAC -> guest index
    count = {}                      # (from, to) -> TCP frames seen
    held = {}                       # (from, to) -> (frame, time held)
    flows = {}                      # 4-tuple -> [frames, bytes, first, last]
    stats = {'frames': 0, 'tcp': 0, 'dropped': 0, 'reordered': 0}
    log.write('hub: %d guests, drop every %s, reorder every %s TCP frame each way, SYN-ACKs dropped for %s s\n'
              % (len(socks), drop or 'no', reorder or 'no', synack or 'no'))
    if fin:
        log.write('hub: FINs from guest %d dropped for %s s\n' % (finfrom, fin))

    def send(k, frame):
        socks[k].sendto(frame, guests[k])

    synack_from = []
    fin_from = []

    def forward(i, j, frame, info):
        if fin and i == finfrom and info is not None and info[5] & 0x01:
            if not fin_from:
                fin_from.append(time.time())
            if time.time() - fin_from[0] < fin:
                stats['dropped'] += 1
                log.write('hub: dropped FIN %d>%d %s:%d > %s:%d\n'
                          % (i, j, info[0], info[1], info[2], info[3]))
                return
        if synack and info is not None and info[5] & 0x12 == 0x12:
            if not synack_from:
                synack_from.append(time.time())
            if time.time() - synack_from[0] < synack:
                stats['dropped'] += 1
                log.write('hub: dropped SYN-ACK %d>%d %s:%d > %s:%d\n'
                          % (i, j, info[0], info[1], info[2], info[3]))
                return
        if info is None or (not drop and not reorder):
            send(j, frame)
            return
        key = (i, j)
        count[key] = count.get(key, 0) + 1
        n = count[key]
        if drop and n % drop == 0:
            stats['dropped'] += 1
            log.write('hub: dropped TCP frame %d %d>%d %s:%d > %s:%d len %d\n'
                      % (n, i, j, info[0], info[1], info[2], info[3], info[4]))
            return
        if reorder and n % reorder == 0 and key not in held:
            held[key] = (frame, time.time())
            stats['reordered'] += 1
            log.write('hub: held TCP frame %d %d>%d %s:%d > %s:%d len %d\n'
                      % (n, i, j, info[0], info[1], info[2], info[3], info[4]))
            return
        send(j, frame)
        if key in held:
            send(j, held.pop(key)[0])

    stop = logname + '.stop'
    while time.time() < until and not os.path.exists(stop):
        r, _, _ = select.select(socks, [], [], 0.01)
        now = time.time()
        for key in [k for k, v in held.items() if now - v[1] > 0.04]:
            send(key[1], held.pop(key)[0])
        for s in r:
            try:
                frame = s.recv(65536)
            except OSError:
                continue
            i = index[s]
            stats['frames'] += 1
            macs[frame[6:12]] = i
            info = tcp_info(frame)
            if info:
                stats['tcp'] += 1
                if info[4]:
                    f = flows.setdefault(info[:4], [0, 0, now - start, 0])
                    f[0] += 1
                    f[1] += info[4]
                    f[3] = now - start
            dst = frame[0:6]
            if dst[0] & 1 or dst not in macs:
                targets = [j for j in range(len(socks)) if j != i]
            else:
                targets = [macs[dst]] if macs[dst] != i else []
            for j in targets:
                forward(i, j, frame, info)
    for key, v in held.items():
        send(key[1], v[0])
    for k, f in sorted(flows.items(), key=lambda kv: kv[1][2]):
        log.write('hub: flow %s:%d > %s:%d frames %d bytes %d first %.3f last %.3f\n'
                  % (k[0], k[1], k[2], k[3], f[0], f[1], f[2], f[3]))
    log.write('hub: frames %d, TCP %d, dropped %d, reordered %d\n'
              % (stats['frames'], stats['tcp'], stats['dropped'], stats['reordered']))
    log.write('hub: done\n')


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
    elif cmd == 'udpports':
        print(' '.join(str(p) for p in free_ports(socket.SOCK_DGRAM, int(sys.argv[2]))))
    elif cmd == 'hub':
        hub(sys.argv[2], float(sys.argv[3]), sys.argv[4],
            int(sys.argv[5]) if len(sys.argv) > 5 else 0,
            int(sys.argv[6]) if len(sys.argv) > 6 else 0,
            float(sys.argv[7]) if len(sys.argv) > 7 else 0,
            float(sys.argv[8]) if len(sys.argv) > 8 else 0,
            int(sys.argv[9]) if len(sys.argv) > 9 else 1)
    elif cmd == 'big':
        open(sys.argv[2], 'wb').write(big_body(int(sys.argv[3]) if len(sys.argv) > 3 else HTTPD_BIG))
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
