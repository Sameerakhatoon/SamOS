#ifndef SAMOS_H
#define SAMOS_H

#include <stddef.h>
#include <stdbool.h>

void* samos_malloc(size_t size);
void  samos_free(void* ptr);
void  samos_putchar(char c);
int   samos_getkey();
int   samos_getkeyblock();
// Blocks in the kernel until a key is there for this process.
int   samos_getkey_block();
void  samos_terminal_readline(char* out, int max, bool output_while_typing);
// Returns the child's pid (< 0 on error) like samos_system.
int   samos_process_load_start(const char* filename);

// Processes and time.
int           samos_getpid(void);
// Waits for child `pid` (any child if pid <= 0); returns its pid and
// stores its exit status (main's return value, 128 + vector for a
// fault, 128 + 9 when killed). Negative when there is no such child.
int           samos_waitpid(int pid, int* status);
int           samos_wait(int* status);
int           samos_kill(int pid);
void          samos_sleep_ms(unsigned long ms);
unsigned long samos_uptime(void);   // milliseconds since boot
long          samos_time(void);     // Seconds since 1970 UTC, -1 on error
void          samos_shutdown(void); // Power off, does not return
void          samos_reboot(void);   // Reset, does not return

// The network. net_info: 0 ifconfig, 1 arp, 2 route, 3 TCP
// connections and retransmission timeouts (netstat), as text
// into buf; its length or a negative status. net_ping: one ICMP echo
// to `ip` (host order, a.b.c.d is a << 24 | ...), blocking: the round
// trip in microseconds, or a negative status (-12: no reply in time).
long samos_net_info(int what, char* buf, long size);
long samos_net_ping(unsigned int ip, unsigned int timeout_ms);
// net_ping with the request's IP TTL (0: the default, 64) and
// what answered in *out (may be NULL): the round trip for an echo
// reply (type 0) or for a router's Time Exceeded (type 11, `from` is
// the router), or a negative status. `ttl` is the answer's as it
// arrived: 64 less one per router on the way back.
// A router's Destination Unreachable (type 3) answers too, with
// its code (0 net unreachable; 4 Fragmentation Needed, `mtu` the
// next-hop MTU); for an echo reply `bytes` is its data's length and
// `data_ok` whether the data came back as sent. `ttl` may also carry
// SAMOS_PING_DF (DF set on the request) and SAMOS_PING_SIZE(n) (n data
// bytes after the echo header, at most SAMOS_PING_SIZE_MAX; 0: 16):
// samos_net_ping_ex(ip, ms, ttl | SAMOS_PING_DF | SAMOS_PING_SIZE(1400), &r).
#define SAMOS_PING_ECHO_REPLY     0
#define SAMOS_PING_DEST_UNREACH   3
#define SAMOS_PING_TIME_EXCEEDED  11
#define SAMOS_PING_DF             0x100
#define SAMOS_PING_SIZE(n)        ((unsigned int)(n) << 16)
#define SAMOS_PING_SIZE_MAX       65507
struct samos_ping_result {
    unsigned int   from;          // host order
    unsigned int   rtt_us;
    unsigned char  ttl;
    unsigned char  type;
    unsigned char  code;
    unsigned char  data_ok;
    unsigned short mtu;
    unsigned short pad;
    unsigned int   bytes;
};
long samos_net_ping_ex(unsigned int ip, unsigned int timeout_ms, unsigned int ttl,
                       struct samos_ping_result* out);
// Routes at run time: dest/bits (host order) via gw, which must
// be on a connected subnet; adding replaces a route to the same prefix.
// 0 or a negative status: -2 bad prefix or gateway not on a subnet, -16
// over a connected subnet's route, -14 no such route to delete.
long samos_net_route_add(unsigned int dest, unsigned int bits, unsigned int gw);
long samos_net_route_del(unsigned int dest, unsigned int bits);
// "a.b.c.d" to a host-order address: 0 or -1; and back (16 bytes).
int  samos_inet_pton(const char* str, unsigned int* ip);
void samos_inet_ntop(unsigned int ip, char* out);

// Sockets (the kernel's net/socket.h). A socket is an index
// into one system-wide table of 64; it belongs to the process that
// made it, and another process's number gets -EBADF. Addresses are a
// host-order IPv4 address and a port. Every call returns a negative
// kernel status on failure (-12 timed out, -25 refused, -26 reset,
// -27 port in use, -28 not connected). Timeouts are milliseconds, 0
// waits for ever.
#define SAMOS_SOCK_STREAM  1      // TCP
#define SAMOS_SOCK_DGRAM   2      // UDP
struct samos_sockaddr {
    unsigned int ip;
    unsigned int port;
};
long samos_socket(int type);
long samos_bind(long s, unsigned int port);
long samos_connect(long s, unsigned int ip, unsigned int port);
long samos_listen(long s, int backlog);
long samos_accept(long s, struct samos_sockaddr* peer, unsigned int timeout_ms);
long samos_sendto(long s, const void* buf, long len, unsigned int ip, unsigned int port);
long samos_recvfrom(long s, void* buf, long len, struct samos_sockaddr* from, unsigned int timeout_ms);
long samos_sclose(long s);
// send and recv on a connected socket; samos_send_all loops until
// all of `len` is queued.
long samos_send(long s, const void* buf, long len);
long samos_send_all(long s, const void* buf, long len);
long samos_recv(long s, void* buf, long len);

// Poll, non-blocking sockets, the socket's addresses (the
// kernel's net/socket.h). samos_poll: for each entry the events asked
// for that hold now, plus ERR, HUP and NVAL always, in revents (a
// negative s is skipped); returns how many entries have revents, 0
// when timeout_ms ran out (0: at once, -1: for ever). Any of the
// sockets wakes it. At most 64 entries.
//   POLLIN   bytes to recv, the peer's FIN or an error; a connection
//            to accept on a listener; a datagram queued
//   POLLOUT  connected with room in the send buffer (datagram: always)
//   POLLERR  the connection failed or was reset
//   POLLHUP  the connection is gone (reset, refused, closed both
//            ways); the peer's FIN alone is POLLIN (recv returns 0)
//   POLLNVAL not a socket of this process
#define SAMOS_POLLIN     0x01
#define SAMOS_POLLOUT    0x04
#define SAMOS_POLLERR    0x08
#define SAMOS_POLLHUP    0x10
#define SAMOS_POLLNVAL   0x20
struct samos_pollfd {
    int            s;
    unsigned short events;
    unsigned short revents;
};
long samos_poll(struct samos_pollfd* fds, long n, long timeout_ms);
// mode 1: non-blocking (accept, recv, send return -30 instead of
// waiting, connect -31 and goes on; poll for POLLOUT or POLLERR, then
// connect again says how it went); 0: blocking; -1: only ask. Returns
// the mode now. An accepted socket starts blocking.
#define SAMOS_EAGAIN       30
#define SAMOS_EINPROGRESS  31
long samos_nonblock(long s, long mode);
// The local address (ip 0 while not fixed: unbound, or listening) and
// the peer's (-28 when not connected).
long samos_getsockname(long s, struct samos_sockaddr* out);
long samos_getpeername(long s, struct samos_sockaddr* out);

struct command_argument {
    char argument[512];
    struct command_argument* next;
};

struct process_arguments {
    int    argc;
    char** argv;
};

// Splits on spaces/tabs with shell-style quoting ("a b",
// 'a b', a\ b). Returns NULL for an empty line or an unterminated
// quote. Free the list with samos_free_command.
struct command_argument* samos_parse_command(const char* command, int max);
void samos_free_command(struct command_argument* command);
void samos_process_get_arguments(struct process_arguments* arguments);
// Both return the child's pid, or a negative status.
int  samos_system(struct command_argument* arguments);
int  samos_system_run(const char* command);
void samos_exit(int status);
void print(const char* msg);

// Lecture 105 - userland fopen syscall trampoline.
int  samos_fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose trampoline. It now
// really makes the syscall; 0 or a negative status.
long samos_fclose(size_t fd);
// Lecture 107 - userland fread trampoline.
long samos_fread(void* buffer, size_t size, size_t count, long fd);
// Lecture 111 - userland fseek trampoline.
long samos_fseek(long fd, long offset, long whence);
// Lecture 112 - userland fstat trampoline.
struct file_stat;
long samos_fstat(long fd, struct file_stat* file_stat_out);
// Lecture 115 - userland realloc trampoline.
void* samos_realloc(void* old_ptr, size_t new_size);

// Syscalls 40-46. Each returns a negative kernel
// status (src/status.h) on failure; file.h has the friendlier names.
struct file_dirent;
long samos_fwrite(const void* buffer, size_t size, size_t count, long fd);
int  samos_ftruncate(long fd, unsigned long size);
int  samos_unlink(const char* path);
int  samos_mkdir(const char* path);
int  samos_rmdir(const char* path);
int  samos_opendir(const char* path);
int  samos_readdir(long fd, struct file_dirent* out);

// SamOs e2e: write `value` into the kernel's bootmarker slot
// `slot`. Slot must be >= 40 (BM_USER_FEATURE_BASE) and below
// BM_STAGE_MAX, otherwise the syscall returns -1.
int samos_e2e_mark(unsigned int slot, unsigned int value);

// Lecture 198 - userspace udelay. Marks the current task asleep
// for the given microsecond count and yields. Returns when the
// scheduler resumes us.
void samos_udelay(unsigned long microseconds);

// Lecture 154 - userspace window create. Returns an opaque user
// handle (pointer that lives in the user mapping) or NULL.
void* samos_window_create(const char* title, int width, int height,
                          int flags, int id);

// Lecture 158 - divert the process's stdout into the given user
// window handle. No-op if the handle is NULL or stale.
void samos_sysout_to_window(void* user_window);

// Lecture 163 - drain a single event off the per-process window
// event ring into the caller-supplied buffer. Returns 0 if an
// event was delivered, non-zero if the ring was empty.
long samos_get_window_event(void* event_out);

#endif
