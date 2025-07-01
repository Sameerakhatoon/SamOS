/*
 * The stdlib's syscall wrappers for the GNU assembler ports
 * (programs/stdlib/arch/aarch64 and riscv64): one SYSCALL line per
 * wrapper, name and number. The .S file defines the SYSCALL macro for
 * its instruction. x86_64's samos.asm (NASM) keeps its own list with
 * the same names and numbers; tests64/host/arch_check.sh compares the
 * two. Numbers: enum SystemCommands in src/isr80h/isr80h.h.
 */
SYSCALL print,                       1
SYSCALL samos_getkey,                2
SYSCALL samos_putchar,               3
SYSCALL samos_malloc,                4
SYSCALL samos_free,                  5
SYSCALL samos_process_load_start,    6
SYSCALL samos_system,                7
SYSCALL samos_process_get_arguments, 8
SYSCALL samos_exit,                  9
SYSCALL samos_fopen,                 10
SYSCALL samos_fclose,                11
SYSCALL samos_fread,                 12
SYSCALL samos_fseek,                 13
SYSCALL samos_fstat,                 14
SYSCALL samos_realloc,               15
SYSCALL samos_window_create,         16
SYSCALL samos_sysout_to_window,      17
SYSCALL samos_get_window_event,      18
SYSCALL samos_udelay,                25
SYSCALL samos_e2e_mark,              26
SYSCALL samos_getpid,                27
SYSCALL samos_waitpid,               28
SYSCALL samos_kill,                  29
SYSCALL samos_sleep_ms,              30
SYSCALL samos_uptime,                31
SYSCALL samos_getkey_block,          32
SYSCALL samos_fwrite,                40
SYSCALL samos_ftruncate,             41
SYSCALL samos_unlink,                42
SYSCALL samos_mkdir,                 43
SYSCALL samos_rmdir,                 44
SYSCALL samos_opendir,               45
SYSCALL samos_readdir,               46
SYSCALL samos_time,                  48
SYSCALL samos_shutdown,              49
SYSCALL samos_reboot,                50
SYSCALL samos_net_info,              60
SYSCALL samos_net_ping,              61
SYSCALL samos_socket,               62
SYSCALL samos_bind,                 63
SYSCALL samos_connect,              64
SYSCALL samos_listen,               65
SYSCALL samos_accept,               66
SYSCALL samos_sendto,               67
SYSCALL samos_recvfrom,             68
SYSCALL samos_sclose,               69
SYSCALL samos_net_ping_ex,          70
