#ifndef IO_H
#define IO_H

// Lecture 33 - 64-bit IO primitives. The 32-bit insb/insw/outb/
// outw still take the same C signatures; the implementations
// now follow the AMD64 SysV ABI. NEW dword variants for 32-bit
// port IO.

unsigned char  insb(unsigned short port);
unsigned short insw(unsigned short port);
unsigned int   insdw(unsigned short port);

void           outb(unsigned short port, unsigned char val);
void           outw(unsigned short port, unsigned short val);
void           outdw(unsigned short port, unsigned int val);

#endif
