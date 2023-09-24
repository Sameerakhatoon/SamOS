#!/usr/bin/env python3
# Write malformed copies of a good ELF64 for e2e/158.
#
#   make_bad_elf.py GOOD.ELF OUTDIR
#
# Each output differs from GOOD.ELF in one field the kernel loader
# must check. Their names are printed, one per line.
import struct
import sys

good = bytearray(open(sys.argv[1], 'rb').read())
out = sys.argv[2]

E_TYPE, E_MACHINE, E_ENTRY, E_PHOFF, E_PHENTSIZE, E_PHNUM = 16, 18, 24, 32, 54, 56
phoff = struct.unpack_from('<Q', good, E_PHOFF)[0]
P_FLAGS, P_OFFSET, P_VADDR, P_FILESZ, P_MEMSZ = 4, 8, 16, 32, 40


def ph(i, field):
    return phoff + 56 * i + field


def variant(name, edit):
    b = bytearray(good)
    edit(b)
    open(f'{out}/{name}', 'wb').write(b)
    print(name)


variant('BADTYPE.ELF', lambda b: struct.pack_into('<H', b, E_TYPE, 3))          # ET_DYN
variant('BADMACH.ELF', lambda b: struct.pack_into('<H', b, E_MACHINE, 3))       # EM_386
variant('BADENTRY.ELF', lambda b: struct.pack_into('<Q', b, E_ENTRY, 0xFFFFFFFF80100000))
variant('BADPHOFF.ELF', lambda b: struct.pack_into('<Q', b, E_PHOFF, 0x7FFFFFFF00))
variant('BADPHNUM.ELF', lambda b: struct.pack_into('<H', b, E_PHNUM, 0xFFFF))
variant('BADPHSZ.ELF', lambda b: struct.pack_into('<H', b, E_PHENTSIZE, 8))
variant('BADOFF.ELF', lambda b: struct.pack_into('<Q', b, ph(0, P_OFFSET), 0xFFFFFFFFFFFFF000))
variant('BADFSZ.ELF', lambda b: struct.pack_into('<Q', b, ph(0, P_FILESZ), 0x10000000))
variant('BADMSZ.ELF', lambda b: struct.pack_into('<Q', b, ph(1, P_MEMSZ), 0xFFFFFFFFFFFFF000))
variant('BADVADDR.ELF', lambda b: struct.pack_into('<Q', b, ph(1, P_VADDR), 0xFFFF800000100000))
variant('BADLOW.ELF', lambda b: struct.pack_into('<Q', b, ph(1, P_VADDR), 0x1000))
variant('BADWX.ELF', lambda b: struct.pack_into('<I', b, ph(0, P_FLAGS), 7))    # R W X
variant('BADOVLP.ELF', lambda b: struct.pack_into('<Q', b, ph(1, P_VADDR), 0x400000))
variant('BADTRUNC.ELF', lambda b: b.__delitem__(slice(40, None)))
