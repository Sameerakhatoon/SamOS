#!/usr/bin/env python3
# JH7110 device trees for the tests (dtc is not installed;
# the blob reader and writer are dtb-harts.py's).
#
#   dtb-jh7110.py edit in.dtb out.dtb [--jh7110] [--noncoherent]
#                 [--simplefb ADDR,W,H] [--dw-uart]
#       Edits a QEMU-dumped tree: --jh7110 puts the VisionFive 2's
#       compatible strings first in the root's (the kernel's Sv39-only
#       rule), --noncoherent marks /soc dma-noncoherent (the cache
#       maintenance path), --simplefb adds a framebuffer node in
#       /chosen as U-Boot's fdt_simplefb_add_node does (a8r8g8b8),
#       --dw-uart makes virt's UART a "snps,dw-apb-uart" (byte
#       registers still: QEMU's are; board_test.c covers width 4).
#   dtb-jh7110.py board out.dtb [--usb-host] [--s7-okay]
#       Writes a VisionFive 2 v1.3B tree with the JH7110 nodes the
#       kernel reads, laid out as Linux's jh7110.dtsi and the board's
#       .dts: harts (the S7 monitor core disabled), memory, the
#       DesignWare UART, the PLIC, the composable cache, both MMC
#       controllers, the Cadence USB (dr_mode "peripheral" as on the
#       board, "host" with --usb-host) and a simple framebuffer;
#       --s7-okay leaves the S7 core enabled (as a tree without
#       OpenSBI's fixup would), so only its missing MMU rules it out. Not a
#       copy of a vendor blob: only what tests64/host/board_test.c
#       checks.
import importlib.util
import os
import struct
import sys

sys.dont_write_bytecode = True
spec =importlib.util.spec_from_file_location(
    "dtbharts", os.path.join(os.path.dirname(os.path.abspath(__file__)), "dtb-harts.py"))
dtb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dtb)
be = dtb.be


def s(*v):
    return b"".join(x.encode() + b"\0" for x in v)


def q(*v):
    return b"".join(struct.pack(">Q", x) for x in v)


def read(path):
    blob = open(path, "rb").read()
    magic, total, off_st, off_strs, off_rsv, version, last, cpu, size_strs, size_st = \
        struct.unpack(">10I", blob[:40])
    assert magic == 0xd00dfeed and version == 17
    root = dtb.parse(blob[off_st:off_st + size_st], blob[off_strs:off_strs + size_strs])
    rsv = blob[off_rsv:off_st] if off_rsv < off_st else struct.pack(">QQ", 0, 0)
    return root, rsv, cpu


def write(path, root, rsv=struct.pack(">QQ", 0, 0), cpu=0):
    st, strs = dtb.serialize(root)
    off_st = 40 + len(rsv)
    off_strs = off_st + len(st)
    hdr = be(0xd00dfeed, off_strs + len(strs), off_st, off_strs, 40, 17, 16, cpu, len(strs), len(st))
    open(path, "wb").write(hdr + rsv + st + strs)


def child(n, name):
    return next(k for k in n[2] if k[0] == name)


def get(n, name):
    return next((v for p, v in n[1] if p == name), None)


def edit(args):
    root, rsv, cpu = read(args[0])
    out = args[1]
    opts = args[2:]
    if "--jh7110" in opts:
        dtb.set_prop(root, "compatible",
                     s("starfive,visionfive-2-v1.3b", "starfive,jh7110") + get(root, "compatible"))
    if "--noncoherent" in opts:
        child(root, "soc")[1].append(("dma-noncoherent", b""))
    if "--dw-uart" in opts:
        uart = next(k for k in child(root, "soc")[2] if get(k, "compatible") == s("ns16550a"))
        dtb.set_prop(uart, "compatible", s("snps,dw-apb-uart"))
        dtb.set_prop(uart, "reg-io-width", be(1))
    if "--simplefb" in opts:
        addr, w, h = (int(x, 0) for x in opts[opts.index("--simplefb") + 1].split(","))
        chosen = child(root, "chosen")
        chosen[1] += [("#address-cells", be(2)), ("#size-cells", be(2)), ("ranges", b"")]
        chosen[2].append(["framebuffer@%x" % addr, [
            ("compatible", s("simple-framebuffer")),
            ("reg", q(addr, w * h * 4)),
            ("width", be(w)), ("height", be(h)), ("stride", be(w * 4)),
            ("format", s("a8r8g8b8")), ("status", s("okay"))], []])
    write(out, root, rsv, cpu)


def board(args):
    out = args[0]
    usb_host = "--usb-host" in args[1:]
    s7_okay = "--s7-okay" in args[1:]
    harts = []
    for i in range(5):
        props = [("device_type", s("cpu")), ("reg", be(i))]
        if i == 0:
            props += [("compatible", s("sifive,s7", "riscv")), ("riscv,isa", s("rv64imac_zba_zbb")),
                      ("status", s("okay" if s7_okay else "disabled"))]
        else:
            props += [("compatible", s("sifive,u74-mc", "riscv")), ("mmu-type", s("riscv,sv39")),
                      ("riscv,isa", s("rv64imafdc_zba_zbb")),
                      ("riscv,isa-extensions", s("i", "m", "a", "f", "d", "c", "zicntr", "zicsr",
                                                 "zifencei", "zihpm", "zba", "zbb")),
                      ("status", s("okay"))]
        intc = ["interrupt-controller", [("compatible", s("riscv,cpu-intc")), ("interrupt-controller", b""),
                                         ("#interrupt-cells", be(1)), ("phandle", be(i + 1))], []]
        harts.append(["cpu@%x" % i, props, [intc]])
    plic_ext = be(1, 11) + b"".join(be(h, 11, h, 9) for h in range(2, 6))

    def mmc(addr, irq, width, extra):
        return ["mmc@%x" % addr, [
            ("compatible", s("starfive,jh7110-mmc")), ("reg", q(addr, 0x10000)),
            ("interrupts", be(irq)), ("fifo-depth", be(32)), ("bus-width", be(width)),
            ("max-frequency", be(100000000)), ("status", s("okay"))] + extra, []]

    soc = ["soc", [("compatible", s("simple-bus")), ("#address-cells", be(2)), ("#size-cells", be(2)),
                   ("ranges", b"")], [
        ["cache-controller@2010000", [
            ("compatible", s("starfive,jh7110-ccache", "sifive,ccache0", "cache")),
            ("reg", q(0x2010000, 0x4000)), ("cache-block-size", be(64)), ("cache-level", be(2))], []],
        ["interrupt-controller@c000000", [
            ("compatible", s("starfive,jh7110-plic", "sifive,plic-1.0.0")),
            ("reg", q(0xc000000, 0x4000000)), ("interrupts-extended", plic_ext),
            ("interrupt-controller", b""), ("#interrupt-cells", be(1)), ("riscv,ndev", be(136)),
            ("phandle", be(10))], []],
        ["serial@10000000", [
            ("compatible", s("snps,dw-apb-uart")), ("reg", q(0x10000000, 0x10000)),
            ("reg-io-width", be(4)), ("reg-shift", be(2)), ("interrupts", be(32)),
            ("status", s("okay"))], []],
        ["serial@10010000", [
            ("compatible", s("snps,dw-apb-uart")), ("reg", q(0x10010000, 0x10000)),
            ("reg-io-width", be(4)), ("reg-shift", be(2)), ("interrupts", be(33)),
            ("status", s("disabled"))], []],
        ["usb@10100000", [
            ("compatible", s("starfive,jh7110-usb")), ("ranges", be(0, 0, 0x10100000, 0x100000)),
            ("#address-cells", be(1)), ("#size-cells", be(1)), ("status", s("okay"))], [
            ["usb@0", [
                ("compatible", s("cdns,usb3")),
                ("reg", be(0, 0x10000, 0x10000, 0x10000, 0x20000, 0x10000)),
                ("reg-names", s("otg", "xhci", "dev")),
                ("interrupts", be(100, 108, 110)),
                ("interrupt-names", s("host", "peripheral", "otg")),
                ("dr_mode", s("host" if usb_host else "peripheral"))], []]]],
        mmc(0x16010000, 74, 8, [("non-removable", b""), ("cap-mmc-highspeed", b"")]),
        mmc(0x16020000, 75, 4, [("cap-sd-highspeed", b""), ("no-sdio", b""), ("no-mmc", b""),
                                ("broken-cd", b"")]),
    ]]
    root = ["", [
        ("#address-cells", be(2)), ("#size-cells", be(2)),
        ("compatible", s("starfive,visionfive-2-v1.3b", "starfive,jh7110")),
        ("model", s("StarFive VisionFive 2 v1.3B"))], [
        ["aliases", [("serial0", s("/soc/serial@10000000"))], []],
        ["chosen", [("stdout-path", s("serial0:115200n8")), ("bootargs", s("samos.test=1")),
                    ("#address-cells", be(2)), ("#size-cells", be(2)), ("ranges", b"")], [
            ["framebuffer@fe000000", [
                ("compatible", s("simple-framebuffer")), ("reg", q(0xfe000000, 1920 * 1080 * 4)),
                ("width", be(1920)), ("height", be(1080)), ("stride", be(1920 * 4)),
                ("format", s("a8r8g8b8"))], []]]],
        ["cpus", [("#address-cells", be(1)), ("#size-cells", be(0)),
                  ("timebase-frequency", be(4000000))], harts],
        ["memory@40000000", [("device_type", s("memory")), ("reg", q(0x40000000, 0x100000000))], []],
        soc,
    ]]
    write(out, root)


if __name__ == "__main__":
    {"edit": edit, "board": board}[sys.argv[1]](sys.argv[2:])
