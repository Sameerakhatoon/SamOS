#!/usr/bin/env python3
# A device tree for QEMU's raspi3b (dtc is not installed and
# QEMU makes none for the raspi machines). The nodes SamOS uses, in the
# shape of Linux's bcm2837-rpi-3-b.dts and the bcm2835/6/7 bindings,
# so the kernel reads it as it reads the Pi firmware's tree:
#
#   /soc ranges 0x7e000000 -> 0x3f000000 (bus to CPU addresses), and
#   dma-ranges 0xc0000000 -> 0 (the mailbox's bus addresses)
#   the BCM2836 per-core controller (timers, mailboxes) and the BCM2835
#   ARM controller under it (GPU interrupts), the PL011, the mailbox,
#   the Arasan SDHCI, the PM watchdog, the generic timer
#   /cpus with enable-method "spin-table", cpu-release-addr 0xd8..0xf0
#   (QEMU's spin loop for the secondaries reads them, as the Pi
#   firmware's armstub does), /memreserve/ for the page they are in
#
#   dtb-raspi3b.py out.dtb "bootargs"
import struct
import sys

BEGIN, END_NODE, PROP, END = 1, 2, 3, 9


def be(*v):
    return struct.pack(">%dI" % len(v), *v)


def s(*strings):
    return b"".join(x.encode() + b"\0" for x in strings)


PH_INTC, PH_LOCAL, PH_MBOX = 1, 2, 3


def tree(bootargs):
    cpus = [("cpu@%d" % n, [
        ("device_type", s("cpu")), ("compatible", s("arm,cortex-a53")), ("reg", be(n)),
        ("enable-method", s("spin-table")), ("cpu-release-addr", be(0, 0xd8 + 8 * n)),
    ], []) for n in range(4)]
    soc = [
        ("interrupt-controller@7e00b200", [
            ("compatible", s("brcm,bcm2836-armctrl-ic")), ("reg", be(0x7e00b200, 0x200)),
            ("interrupt-controller", b""), ("#interrupt-cells", be(2)),
            ("interrupt-parent", be(PH_LOCAL)), ("interrupts", be(8, 4)), ("phandle", be(PH_INTC)),
        ], []),
        ("local_intc@40000000", [
            ("compatible", s("brcm,bcm2836-l1-intc")), ("reg", be(0x40000000, 0x100)),
            ("interrupt-controller", b""), ("#interrupt-cells", be(2)),
            ("interrupt-parent", be(PH_LOCAL)), ("phandle", be(PH_LOCAL)),
        ], []),
        ("serial@7e201000", [
            ("compatible", s("arm,pl011", "arm,primecell")), ("reg", be(0x7e201000, 0x200)),
            ("interrupts", be(2, 25)), ("status", s("okay")),
        ], []),
        ("mailbox@7e00b880", [
            ("compatible", s("brcm,bcm2835-mbox")), ("reg", be(0x7e00b880, 0x40)),
            ("interrupts", be(0, 1)), ("#mbox-cells", be(0)), ("phandle", be(PH_MBOX)),
        ], []),
        ("mmc@7e300000", [
            ("compatible", s("brcm,bcm2835-sdhci")), ("reg", be(0x7e300000, 0x100)),
            ("interrupts", be(2, 30)), ("bus-width", be(4)), ("status", s("okay")),
        ], []),
        ("watchdog@7e100000", [
            ("compatible", s("brcm,bcm2835-pm", "brcm,bcm2835-pm-wdt")), ("reg", be(0x7e100000, 0x114)),
        ], []),
    ]
    return ("", [
        ("compatible", s("raspberrypi,3-model-b", "brcm,bcm2837")), ("model", s("Raspberry Pi 3 Model B")),
        ("#address-cells", be(1)), ("#size-cells", be(1)), ("interrupt-parent", be(PH_INTC)),
    ], [
        ("aliases", [("serial0", s("/soc/serial@7e201000"))], []),
        ("chosen", [("bootargs", s(bootargs)), ("stdout-path", s("serial0:115200n8"))], []),
        ("memory@0", [("device_type", s("memory")), ("reg", be(0, 0x3c000000))], []),
        ("cpus", [("#address-cells", be(1)), ("#size-cells", be(0)),
                  ("enable-method", s("brcm,bcm2836-smp"))], cpus),
        ("timer", [("compatible", s("arm,armv7-timer")), ("interrupt-parent", be(PH_LOCAL)),
                   ("interrupts", be(0, 4, 1, 4, 3, 4, 2, 4)), ("always-on", b"")], []),
        ("firmware", [("compatible", s("raspberrypi,bcm2835-firmware", "simple-mfd")),
                      ("mboxes", be(PH_MBOX))], []),
        ("soc", [("compatible", s("simple-bus")), ("#address-cells", be(1)), ("#size-cells", be(1)),
                 ("ranges", be(0x7e000000, 0x3f000000, 0x1000000, 0x40000000, 0x40000000, 0x1000)),
                 ("dma-ranges", be(0xc0000000, 0, 0x3f000000))], soc),
    ])


def main():
    out, bootargs = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
    st, strs = bytearray(), bytearray()

    def name_off(name):
        key = name.encode() + b"\0"
        i = strs.find(key)
        if i < 0 or (i > 0 and strs[i - 1] != 0):
            i = len(strs)
            strs.extend(key)
        return i

    def emit(node):
        name, props, children = node
        n = name.encode() + b"\0"
        st.extend(be(BEGIN) + n + b"\0" * (-len(n) % 4))
        for pname, data in props:
            st.extend(be(PROP, len(data), name_off(pname)) + data + b"\0" * (-len(data) % 4))
        for c in children:
            emit(c)
        st.extend(be(END_NODE))

    emit(tree(bootargs))
    st.extend(be(END))
    rsvmap = struct.pack(">QQQQ", 0, 0x1000, 0, 0)
    off_rsv = 40
    off_st = off_rsv + len(rsvmap)
    off_strs = off_st + len(st)
    total = off_strs + len(strs)
    hdr = be(0xd00dfeed, total, off_st, off_strs, off_rsv, 17, 16, 0, len(strs), len(st))
    open(out, "wb").write(hdr + rsvmap + bytes(st) + bytes(strs))


if __name__ == "__main__":
    main()
