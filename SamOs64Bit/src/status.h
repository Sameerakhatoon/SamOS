#ifndef STATUS_H
#define STATUS_H

#define SAMOS_ALL_OK    0
#define EIO             1
#define EINVARG         2
#define ENOMEM          3
#define EBADPATH        4
#define EFSNOTUS        5
#define ERDONLY         6
#define EUNIMP          7
#define EISTKN          8
#define EINFORMAT       9
#define EOUTOFRANGE    10  // L64 - FAT cluster chain reached end
#define ENOTFOUND      11  // L115 - process allocation lookup miss
#define ETIMEOUT       12  // L137 - PS/2 mouse wait timeout
#define EINVAL         13  // L178 - PCI ECAM/MCFG invalid args
#define ENOENT         14  // L178 - PCI ECAM no ranges installed
#define ENOSPC         15  // Filesystem or directory full
#define EEXIST         16  // Name already exists
#define ENOTDIR        17  // Path component is not a directory
#define EISDIR         18  // File operation on a directory
#define ENOTEMPTY      19  // Directory not empty
#define EFAULT         20  // Bad user pointer (unmapped, kernel, no access)
#define ENAMETOOLONG   21  // User string longer than the kernel buffer
#define EBADEXEC       22  // Malformed ELF (not EINFORMAT: no flat-binary fallback)
#define ECHILD         23  // Waitpid: no such child
#define ESRCH          24  // Kill: no such process

#endif
