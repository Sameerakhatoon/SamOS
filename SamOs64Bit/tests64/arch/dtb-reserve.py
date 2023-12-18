#!/usr/bin/env python3
# Add memory reservations to a QEMU-dumped device tree (dtc is
# not installed, and QEMU's own trees reserve nothing).
#
#   dtb-reserve.py in.dtb out.dtb
#
# Adds one /memreserve/ entry and a /reserved-memory node with a
# no-map region, a mapped one, an unaligned no-map one, a dynamic
# (size only) one and a disabled one. aarch64-mmu.sh boots the result
# with -dtb; the values are repeated there.
import struct
import sys

MEMRESERVE = [(0x4a000000, 0x10000)]
CHILDREN = [
    ("fw@4c000000", [("reg", (0, 0x4c000000, 0, 0x200000)), ("no-map", None)]),
    ("shm@4c400000", [("reg", (0, 0x4c400000, 0, 0x101000))]),
    ("scratch@4d001000", [("reg", (0, 0x4d001000, 0, 0x3000)), ("no-map", None)]),
    ("cma", [("size", (0, 0x1000000)), ("compatible", "shared-dma-pool")]),
    ("off@4e000000", [("reg", (0, 0x4e000000, 0, 0x1000)), ("status", "disabled")]),
]

BEGIN, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


def be(*v):
    return struct.pack(">%dI" % len(v), *v)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    blob = open(src, "rb").read()
    magic, total, off_st, off_strs, off_rsv, version, last, cpu, size_strs, size_st = \
        struct.unpack(">10I", blob[:40])
    assert magic == 0xd00dfeed and version == 17
    st = bytearray(blob[off_st:off_st + size_st])
    strs = bytearray(blob[off_strs:off_strs + size_strs])
    rsv = []
    o = off_rsv
    while True:
        base, size = struct.unpack(">QQ", blob[o:o + 16])
        if not base and not size:
            break
        rsv.append((base, size))
        o += 16
    rsv += MEMRESERVE

    def name_off(name):
        key = name.encode() + b"\0"
        i = strs.find(key)
        while i > 0 and strs[i - 1] != 0:
            i = strs.find(key, i + 1)
        if i < 0:
            i = len(strs)
            strs.extend(key)
        return i

    def prop(name, value):
        if value is None:
            data = b""
        elif isinstance(value, str):
            data = value.encode() + b"\0"
        else:
            data = be(*value)
        out = be(PROP, len(data), name_off(name)) + data
        return out + b"\0" * (-len(out) % 4)

    def begin(name):
        n = name.encode() + b"\0"
        return be(BEGIN) + n + b"\0" * (-len(n) % 4)

    node = begin("reserved-memory")
    node += prop("#address-cells", (2,)) + prop("#size-cells", (2,)) + prop("ranges", None)
    for name, props in CHILDREN:
        node += begin(name) + b"".join(prop(k, v) for k, v in props) + be(END_NODE)
    node += be(END_NODE)

    # The root's END_NODE is the last one, right before END.
    assert struct.unpack(">I", st[-4:])[0] == END and struct.unpack(">I", st[-8:-4])[0] == END_NODE
    st = st[:-8] + node + be(END_NODE, END)

    rsvmap = b"".join(struct.pack(">QQ", b, s) for b, s in rsv) + struct.pack(">QQ", 0, 0)
    off_rsv = 40
    off_st = off_rsv + len(rsvmap)
    off_strs = off_st + len(st)
    total = off_strs + len(strs)
    hdr = be(0xd00dfeed, total, off_st, off_strs, off_rsv, 17, 16, cpu, len(strs), len(st))
    open(dst, "wb").write(hdr + rsvmap + bytes(st) + bytes(strs))


if __name__ == "__main__":
    main()
