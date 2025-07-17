// polltest: the socket calls the network follow-ups added (poll, non-blocking
// sockets, getsockname, getpeername), for e2e 242 and 243.
//
//   POLLTEST.ELF local [port]
//       on the loopback, by itself: poll's timeouts (0 at once, 300 ms
//       at least 300 ms, -1 until a socket has news), a non-blocking
//       listener, connect and recv (-EAGAIN, -EINPROGRESS), the names
//       of every end, POLLOUT, POLLIN with the peer's FIN, POLLHUP,
//       POLLNVAL, a refused non-blocking connect (POLLERR), datagram
//       sockets. One line per check ("polltest: ok <what>" or
//       "polltest: FAIL <what>: ..."), then
//       "polltest: local done, N checks, F failures".
//   POLLTEST.ELF udp p1 p2 p3 [rounds]
//       binds three datagram sockets and polls them for ever, rounds
//       times (default 3): which one woke it, and the datagram, echoed
//       back ("polltest: udp port P readable alone, 'text' from ..").
//   POLLTEST.ELF conn a.b.c.d port
//       a non-blocking connect, poll for POLLOUT, both names, then an
//       HTTP/1.0 GET / read with poll and non-blocking recv
//       ("polltest: conn ..." lines); a refused one ends in
//       "polltest: conn refused (POLLERR, connect -25)".
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "samos.h"

static int checks, failures;

static void check(int ok, const char* what, long got){
    checks++;
    if(ok){
        printf("polltest: ok %s\n", what);
    } else {
        failures++;
        printf("polltest: FAIL %s: got %ld\n", what, got);
    }
}

static long poll1(long s, int events, long timeout, int* revents){
    struct samos_pollfd p = { (int)s, (unsigned short)events, 0 };
    long n = samos_poll(&p, 1, timeout);
    *revents = p.revents;
    return n;
}

static const char* ipstr(unsigned int ip, char* buf){
    samos_inet_ntop(ip, buf);
    return buf;
}

#define LOOP_IP  0x7f000001u

static int local_test(int port){
    char a[16], b[16], what[96];
    int rev = 0;
    struct samos_sockaddr sa, sb;

    // Non-blocking, set and read back.
    long l = samos_socket(SAMOS_SOCK_STREAM);
    check(l >= 0 && samos_nonblock(l, -1) == 0, "a new socket blocks", samos_nonblock(l, -1));
    check(samos_nonblock(l, 1) == 1 && samos_nonblock(l, -1) == 1, "nonblock reads back 1", samos_nonblock(l, -1));
    check(samos_nonblock(l, 2) < 0, "nonblock refuses mode 2", samos_nonblock(l, 2));
    check(samos_nonblock(60, 1) == -29 || samos_nonblock(60, 1) < 0, "nonblock on a socket not ours", samos_nonblock(60, 1));

    // An unconnected stream socket: no peer, POLLHUP.
    long r = samos_getpeername(l, &sa);
    check(r == -28, "getpeername unconnected is -28", r);
    long n = poll1(l, SAMOS_POLLIN | SAMOS_POLLOUT, 0, &rev);
    check(n == 1 && rev == SAMOS_POLLHUP, "poll unconnected: POLLHUP only", rev);

    // The listener.
    r = samos_bind(l, (unsigned)port);
    r = r < 0 ? r : samos_listen(l, 4);
    check(r == 0, "bind and listen", r);
    r = samos_getsockname(l, &sa);
    snprintf(what, sizeof(what), "getsockname listener 0.0.0.0:%d", port);
    check(r == 0 && sa.ip == 0 && sa.port == (unsigned)port, what, r < 0 ? r : (long)sa.port);
    r = samos_accept(l, &sa, 0);
    check(r == -SAMOS_EAGAIN, "accept non-blocking with none queued is -30", r);

    // Timeouts.
    unsigned long t0 = samos_uptime();
    n = poll1(l, SAMOS_POLLIN, 0, &rev);
    unsigned long dt = samos_uptime() - t0;
    snprintf(what, sizeof(what), "poll timeout 0 returns 0 at once (%lu ms)", dt);
    check(n == 0 && rev == 0 && dt < 1000, what, n);
    t0 = samos_uptime();
    n = poll1(l, SAMOS_POLLIN, 300, &rev);
    dt = samos_uptime() - t0;
    snprintf(what, sizeof(what), "poll timeout 300 returns 0 after %lu ms", dt);
    check(n == 0 && dt >= 300 && dt < 5000, what, n);

    // A non-blocking connect to it.
    long c = samos_socket(SAMOS_SOCK_STREAM);
    samos_nonblock(c, 1);
    r = samos_connect(c, LOOP_IP, (unsigned)port);
    check(r == -SAMOS_EINPROGRESS || r == 0, "connect non-blocking is -31 (or done)", r);
    n = poll1(l, SAMOS_POLLIN, 5000, &rev);
    check(n == 1 && rev == SAMOS_POLLIN, "poll listener: POLLIN for the connection", rev);
    n = poll1(c, SAMOS_POLLOUT, 5000, &rev);
    check(n == 1 && rev == SAMOS_POLLOUT, "poll connecting socket: POLLOUT once connected", rev);
    r = samos_connect(c, LOOP_IP, (unsigned)port);
    check(r == 0, "connect again says 0", r);
    struct samos_sockaddr peer;
    long k = samos_accept(l, &peer, 0);
    check(k >= 0, "accept non-blocking takes it", k);
    check(samos_nonblock(k, -1) == 0, "the accepted socket blocks", samos_nonblock(k, -1));

    // Names: both ends agree.
    long r1 = samos_getsockname(c, &sa), r2 = samos_getpeername(k, &sb);
    snprintf(what, sizeof(what), "client name %s:%u is the server's peer %s:%u",
             ipstr(sa.ip, a), sa.port, ipstr(sb.ip, b), sb.port);
    check(!r1 && !r2 && sa.ip == LOOP_IP && sa.port && sa.port != (unsigned)port && sa.ip == sb.ip && sa.port == sb.port
          && peer.ip == sb.ip && peer.port == sb.port, what, r1 ? r1 : r2);
    r1 = samos_getsockname(k, &sa);
    r2 = samos_getpeername(c, &sb);
    snprintf(what, sizeof(what), "server name %s:%u is the client's peer %s:%u",
             ipstr(sa.ip, a), sa.port, ipstr(sb.ip, b), sb.port);
    check(!r1 && !r2 && sa.ip == LOOP_IP && sa.port == (unsigned)port && sa.ip == sb.ip && sa.port == sb.port,
          what, r1 ? r1 : r2);

    // recv with nothing there.
    char buf[64];
    r = samos_recv(c, buf, sizeof(buf));
    check(r == -SAMOS_EAGAIN, "recv non-blocking with nothing there is -30", r);
    n = poll1(c, SAMOS_POLLIN, 200, &rev);
    check(n == 0, "poll POLLIN on a quiet connection times out", n);

    // Several sockets: only the one with news is reported.
    long u = samos_socket(SAMOS_SOCK_DGRAM);
    samos_bind(u, (unsigned)port + 1);
    r = samos_send_all(k, "ping", 4);
    struct samos_pollfd fds[4] = {
        { (int)l, SAMOS_POLLIN, 0 }, { (int)u, SAMOS_POLLIN, 0 }, { -1, SAMOS_POLLIN, 0 }, { (int)c, SAMOS_POLLIN, 0 },
    };
    n = samos_poll(fds, 4, 5000);
    snprintf(what, sizeof(what), "poll of 4 (one skipped): the connection alone (%x %x %x %x)",
             fds[0].revents, fds[1].revents, fds[2].revents, fds[3].revents);
    check(n == 1 && !fds[0].revents && !fds[1].revents && !fds[2].revents && fds[3].revents == SAMOS_POLLIN,
          what, n);
    r = samos_recv(c, buf, sizeof(buf));
    check(r == 4 && !memcmp(buf, "ping", 4), "recv the 4 bytes", r);

    // POLLOUT on a datagram socket, POLLIN when one is queued.
    long u2 = samos_socket(SAMOS_SOCK_DGRAM);
    n = poll1(u2, SAMOS_POLLOUT, 0, &rev);
    check(n == 1 && rev == SAMOS_POLLOUT, "datagram socket: POLLOUT", rev);
    samos_connect(u2, LOOP_IP, (unsigned)port + 1);
    samos_sendto(u2, "dgram", 5, 0, 0);
    n = poll1(u, SAMOS_POLLIN, 5000, &rev);
    check(n == 1 && rev == SAMOS_POLLIN, "datagram socket: POLLIN", rev);
    r1 = samos_getsockname(u2, &sa);
    r2 = samos_getpeername(u2, &sb);
    snprintf(what, sizeof(what), "datagram names %s:%u to %s:%u", ipstr(sa.ip, a), sa.port, ipstr(sb.ip, b), sb.port);
    check(!r1 && !r2 && sa.ip == LOOP_IP && sa.port >= 49152 && sb.ip == LOOP_IP && sb.port == (unsigned)port + 1,
          what, r1 ? r1 : r2);
    samos_nonblock(u, 1);
    struct samos_sockaddr from;
    r = samos_recvfrom(u, buf, sizeof(buf), &from, 0);
    check(r == 5 && from.port == sa.port, "recvfrom the datagram", r);
    r = samos_recvfrom(u, buf, sizeof(buf), &from, 0);
    check(r == -SAMOS_EAGAIN, "recvfrom non-blocking with none queued is -30", r);

    // The server closes: POLLIN, recv 0; POLLNVAL for a closed socket.
    samos_sclose(k);
    n = poll1(c, SAMOS_POLLIN, 5000, &rev);
    check(n == 1 && (rev & SAMOS_POLLIN), "the peer's FIN: POLLIN", rev);
    r = samos_recv(c, buf, sizeof(buf));
    check(r == 0, "then recv says 0", r);
    n = poll1(k, SAMOS_POLLIN, 0, &rev);
    check(n == 1 && rev == SAMOS_POLLNVAL, "a closed socket: POLLNVAL", rev);
    samos_sclose(c);

    // A refused non-blocking connect: POLLERR and POLLHUP whether asked
    // for or not (events 0).
    c = samos_socket(SAMOS_SOCK_STREAM);
    samos_nonblock(c, 1);
    r = samos_connect(c, LOOP_IP, (unsigned)port + 2);
    n = poll1(c, 0, 5000, &rev);
    r2 = samos_connect(c, LOOP_IP, (unsigned)port + 2);
    snprintf(what, sizeof(what), "a refused connect: POLLERR and POLLHUP, connect again -25 (%ld, %x)", r, rev);
    check(n == 1 && rev == (SAMOS_POLLERR | SAMOS_POLLHUP) && r2 == -25, what, r2);
    samos_sclose(c);

    // poll's arguments.
    r = samos_poll(fds, 65, 0);
    check(r < 0, "poll of 65 is refused", r);
    r = samos_poll(0, 1, 0);
    check(r == -20, "poll of a bad pointer is -20", r);
    n = samos_poll(fds, 0, 100);
    check(n == 0, "poll of none sleeps and says 0", n);

    samos_sclose(u);
    samos_sclose(u2);
    samos_sclose(l);
    printf("polltest: local done, %d checks, %d failures\n", checks, failures);
    return failures != 0;
}

static int udp_test(int argc, char** argv){
    struct samos_pollfd fds[3];
    int ports[3];
    for(int i = 0; i < 3; i++){
        ports[i] = atoi(argv[2 + i]);
        long s = samos_socket(SAMOS_SOCK_DGRAM);
        long r = s < 0 ? s : samos_bind(s, (unsigned)ports[i]);
        if(r < 0){
            printf("polltest: udp cannot bind %d (%ld)\n", ports[i], r);
            return 1;
        }
        fds[i].s      = (int)s;
        fds[i].events = SAMOS_POLLIN;
    }
    int rounds = argc > 5 ? atoi(argv[5]) : 3;
    printf("polltest: udp waiting on ports %d %d %d\n", ports[0], ports[1], ports[2]);
    for(int round = 0; round < rounds; round++){
        long n = samos_poll(fds, 3, -1);
        int which = -1, ready = 0;
        for(int i = 0; i < 3; i++){
            if(fds[i].revents){
                which = which < 0 ? i : which;
                ready++;
            }
        }
        if(n != 1 || ready != 1 || fds[which].revents != SAMOS_POLLIN){
            printf("polltest: udp FAIL poll %ld, %d ready\n", n, ready);
            return 1;
        }
        char buf[128], ip[16];
        struct samos_sockaddr from;
        long len = samos_recvfrom(fds[which].s, buf, sizeof(buf) - 1, &from, 0);
        buf[len > 0 ? len : 0] = 0;
        samos_sendto(fds[which].s, buf, len > 0 ? len : 0, from.ip, from.port);
        printf("polltest: udp port %d readable alone, '%s' from %s, echoed\n", ports[which], buf,
               ipstr(from.ip, ip));
    }
    for(int i = 0; i < 3; i++){
        samos_sclose(fds[i].s);
    }
    printf("polltest: udp done\n");
    return 0;
}

static int conn_test(char** argv){
    unsigned int ip;
    if(samos_inet_pton(argv[2], &ip) < 0){
        printf("polltest: conn bad address\n");
        return 1;
    }
    unsigned int port = (unsigned)atoi(argv[3]);
    char a[16], b[16];
    long c = samos_socket(SAMOS_SOCK_STREAM);
    samos_nonblock(c, 1);
    unsigned long t0 = samos_uptime();
    long r = samos_connect(c, ip, port);
    printf("polltest: conn connect %ld after %lu ms\n", r, samos_uptime() - t0);
    struct samos_pollfd p = { (int)c, SAMOS_POLLOUT, 0 };
    long n = samos_poll(&p, 1, 20000);
    long r2 = samos_connect(c, ip, port);
    if(n != 1 || !(p.revents & SAMOS_POLLOUT)){
        printf("polltest: conn %s (%s, connect %ld)\n", r2 == -25 ? "refused" : "failed",
               p.revents & SAMOS_POLLERR ? "POLLERR" : "no POLLERR", r2);
        samos_sclose(c);
        return 0;
    }
    struct samos_sockaddr sa, sb;
    samos_getsockname(c, &sa);
    samos_getpeername(c, &sb);
    printf("polltest: conn connected %s:%u to %s:%u (connect %ld)\n", ipstr(sa.ip, a), sa.port,
           ipstr(sb.ip, b), sb.port, r2);
    static const char req[] = "GET / HTTP/1.0\r\n\r\n";
    long sent = samos_send_all(c, req, sizeof(req) - 1);
    long total = 0, eagain = 0;
    char buf[1024], status[64];
    int status_len = 0, status_done = 0;
    for(;;){
        long k = samos_recv(c, buf, sizeof(buf));
        if(k == -SAMOS_EAGAIN){
            eagain++;
            p.events = SAMOS_POLLIN;
            if(samos_poll(&p, 1, 20000) != 1){
                break;
            }
            continue;
        }
        if(k <= 0){
            break;
        }
        for(long i = 0; i < k && !status_done; i++){
            if(buf[i] == '\r' || buf[i] == '\n' || status_len == (int)sizeof(status) - 1){
                status_done = 1;
            } else {
                status[status_len++] = buf[i];
            }
        }
        total += k;
    }
    status[status_len] = 0;
    printf("polltest: conn status %s\n", status);
    printf("polltest: conn sent %ld, received %ld bytes (%ld waits)\n", sent, total, eagain);
    samos_sclose(c);
    return 0;
}

int main(int argc, char** argv){
    if(argc > 1 && !strcmp(argv[1], "local")){
        return local_test(argc > 2 ? atoi(argv[2]) : 7000);
    }
    if(argc > 4 && !strcmp(argv[1], "udp")){
        return udp_test(argc, argv);
    }
    if(argc > 3 && !strcmp(argv[1], "conn")){
        return conn_test(argv);
    }
    printf("usage: polltest local [port] | udp p1 p2 p3 [rounds] | conn a.b.c.d port\n");
    return 1;
}
