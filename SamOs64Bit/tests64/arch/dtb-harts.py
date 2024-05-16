#!/usr/bin/env python3
# Edit the harts of a QEMU-dumped riscv64 device tree (dtc is
# not installed), for riscv64-smp.sh.
#
#   dtb-harts.py in.dtb out.dtb DISABLE FAKE
#
# Marks /cpus/cpu@DISABLE status = "disabled", and adds cpu@FAKE: a
# copy of the last cpu node with reg = <FAKE> and no phandles, a hart
# the machine does not have, so hart_start on it fails or never
# answers. Together the enabled hart ids are not contiguous.
import struct
import sys

BEGIN, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


def be(*v):
    return struct.pack(">%dI" % len(v), *v)


def parse(st, strs):
    # A node is [name, [(prop, bytes)], [children]].
    pos = 0

    def word():
        nonlocal pos
        v = struct.unpack(">I", st[pos:pos + 4])[0]
        pos += 4
        return v

    def node():
        nonlocal pos
        end = st.index(b"\0", pos)
        name = st[pos:end].decode()
        pos = (end + 4) & ~3
        props, kids = [], []
        while True:
            tok = word()
            if tok == PROP:
                size, off = word(), word()
                pname = strs[off:strs.index(b"\0", off)].decode()
                props.append((pname, bytes(st[pos:pos + size])))
                pos = (pos + size + 3) & ~3
            elif tok == BEGIN:
                kids.append(node())
            elif tok == END_NODE:
                return [name, props, kids]
            elif tok != NOP:
                raise ValueError("bad token %d" % tok)

    assert word() == BEGIN
    return node()


def serialize(root):
    strs = bytearray()

    def name_off(name):
        key = name.encode() + b"\0"
        i = strs.find(key)
        while i > 0 and strs[i - 1] != 0:
            i = strs.find(key, i + 1)
        if i < 0:
            i = len(strs)
            strs.extend(key)
        return i

    def emit(n):
        name, props, kids = n
        nm = name.encode() + b"\0"
        out = be(BEGIN) + nm + b"\0" * (-len(nm) % 4)
        for p, v in props:
            out += be(PROP, len(v), name_off(p)) + v + b"\0" * (-len(v) % 4)
        for k in kids:
            out += emit(k)
        return out + be(END_NODE)

    return emit(root) + be(END), bytes(strs)


def set_prop(n, name, value):
    n[1] = [(p, v) for p, v in n[1] if p != name] + [(name, value)]


def strip_phandles(n):
    n[1] = [(p, v) for p, v in n[1] if p not in ("phandle", "linux,phandle")]
    for k in n[2]:
        strip_phandles(k)


def copy(n):
    return [n[0], list(n[1]), [copy(k) for k in n[2]]]


def main():
    src, dst, disable, fake = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    blob = open(src, "rb").read()
    magic, total, off_st, off_strs, off_rsv, version, last, cpu, size_strs, size_st = \
        struct.unpack(">10I", blob[:40])
    assert magic == 0xd00dfeed and version == 17
    root = parse(blob[off_st:off_st + size_st], blob[off_strs:off_strs + size_strs])
    cpus = next(k for k in root[2] if k[0] == "cpus")
    harts = [k for k in cpus[2] if k[0].startswith("cpu@")]
    set_prop(next(k for k in harts if k[0] == "cpu@%x" % disable), "status", b"disabled\0")
    extra = copy(harts[-1])
    extra[0] = "cpu@%x" % fake
    set_prop(extra, "reg", be(fake))
    strip_phandles(extra)
    cpus[2].insert(cpus[2].index(harts[-1]) + 1, extra)

    st, strs = serialize(root)
    rsvmap = blob[off_rsv:off_st] if off_rsv < off_st else struct.pack(">QQ", 0, 0)
    off_rsv = 40
    off_st = off_rsv + len(rsvmap)
    off_strs = off_st + len(st)
    total = off_strs + len(strs)
    hdr = be(0xd00dfeed, total, off_st, off_strs, off_rsv, 17, 16, cpu, len(strs), len(st))
    open(dst, "wb").write(hdr + rsvmap + st + strs)


if __name__ == "__main__":
    main()
