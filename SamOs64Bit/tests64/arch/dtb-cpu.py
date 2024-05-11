#!/usr/bin/env python3
# Add a CPU node that no CPU answers to a QEMU-dumped device
# tree (dtc is not installed): /cpus/cpu@<mpidr> with reg = MPIDR and
# enable-method = "psci", first under /cpus, so PSCI CPU_ON fails on
# it before the real ones start. aarch64-smp.sh boots the result with
# -dtb.
#
#   dtb-cpu.py in.dtb out.dtb mpidr
import struct
import sys

BEGIN, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


def be(*v):
    return struct.pack(">%dI" % len(v), *v)


def main():
    src, dst, mpidr = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    blob = open(src, "rb").read()
    magic, total, off_st, off_strs, off_rsv, version, last, cpu, size_strs, size_st = \
        struct.unpack(">10I", blob[:40])
    assert magic == 0xd00dfeed and version == 17
    st = bytearray(blob[off_st:off_st + size_st])
    strs = bytearray(blob[off_strs:off_strs + size_strs])
    rsvmap = blob[off_rsv:off_st]

    def name_off(name):
        key = name.encode() + b"\0"
        i = strs.find(key)
        while i > 0 and strs[i - 1] != 0:
            i = strs.find(key, i + 1)
        if i < 0:
            i = len(strs)
            strs.extend(key)
        return i

    def prop(name, data):
        out = be(PROP, len(data), name_off(name)) + data
        return out + b"\0" * (-len(out) % 4)

    def begin(name):
        n = name.encode() + b"\0"
        return be(BEGIN) + n + b"\0" * (-len(n) % 4)

    # Find the end of /cpus's own properties: walk the tokens to the
    # first BEGIN_NODE inside the depth-1 node named "cpus".
    o, depth, in_cpus, at, cells = 0, 0, False, None, 2
    while at is None:
        tok = struct.unpack(">I", st[o:o + 4])[0]
        if tok == BEGIN:
            end = st.index(b"\0", o + 4)
            name = st[o + 4:end].decode()
            if in_cpus:
                at = o
                break
            depth += 1
            in_cpus = depth == 2 and name == "cpus"
            o = (end + 1 + 3) & ~3
        elif tok == PROP:
            ln, noff = struct.unpack(">II", st[o + 4:o + 12])
            pname = strs[noff:strs.index(b"\0", noff)]
            if in_cpus and pname == b"#address-cells":
                cells = struct.unpack(">I", st[o + 12:o + 16])[0]
            o = (o + 12 + ln + 3) & ~3
        elif tok == END_NODE:
            assert not in_cpus, "/cpus has no child"
            depth -= 1
            o += 4
        elif tok == NOP:
            o += 4
        else:
            raise SystemExit("no /cpus node")

    node = begin("cpu@%x" % mpidr)
    node += prop("device_type", b"cpu\0") + prop("compatible", b"arm,cortex-a72\0")
    reg = be(mpidr) if cells == 1 else be(mpidr >> 32, mpidr & 0xffffffff)
    node += prop("reg", reg) + prop("enable-method", b"psci\0") + be(END_NODE)
    st = st[:at] + node + st[at:]

    off_rsv = 40
    off_st = off_rsv + len(rsvmap)
    off_strs = off_st + len(st)
    total = off_strs + len(strs)
    hdr = be(0xd00dfeed, total, off_st, off_strs, off_rsv, 17, 16, cpu, len(strs), len(st))
    open(dst, "wb").write(hdr + rsvmap + bytes(st) + bytes(strs))


if __name__ == "__main__":
    main()
