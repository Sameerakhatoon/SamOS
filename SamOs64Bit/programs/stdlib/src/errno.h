#ifndef SAMOS_ERRNO_H
#define SAMOS_ERRNO_H

// Userland errno. One global: SamOS processes are single
// threaded. Values follow the usual POSIX numbering.
extern int errno;

#define ENOENT   2
#define EIO      5
#define EBADF    9
#define ENOMEM  12
#define EINVAL  22
#define ERANGE  34

#endif
