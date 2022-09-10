# Ch 57 - PCI IDE controller (prose)

Book Ch 53: background reading before we write the C disk driver in the
next chapter. No source-tree changes.

## The model

A PCI IDE controller sits between the CPU and one or two ATA hard
drives wired in series on a single ribbon. The CPU talks to the
controller via a fixed set of port-mapped registers and the controller
arbitrates which drive on the channel responds.

## Primary vs secondary channel

- Primary channel: base I/O 0x1F0, status/cmd at 0x1F7
- Secondary channel: base I/O 0x170, status/cmd at 0x177

Each channel can host two devices, historically called master and
slave (newer specs prefer "drive 0" and "drive 1"). The distinction is
purely about who answers on the shared cable, not about capability.
For SamOs we'll only ever drive the primary master because the build
script writes the kernel image to that disk.

## The port map we care about

| Port  | Direction | Meaning |
| ----- | --------- | ------- |
| 0x1F2 | write     | sector count (how many 512-byte sectors to transfer) |
| 0x1F3 | write     | LBA bits 0..7 |
| 0x1F4 | write     | LBA bits 8..15 |
| 0x1F5 | write     | LBA bits 16..23 |
| 0x1F6 | write     | bits 0..3 = LBA bits 24..27, bit 4 = drive select (0=master), bits 5..7 = mode bits, OR with 0xE0 for LBA-28 master |
| 0x1F7 | write     | command (0x20 = read sectors) |
| 0x1F7 | read      | status (BUSY=bit7, DRDY=bit6, DF=bit5, ERR=bit0) |
| 0x1F0 | read 16   | data port - one `insw` returns 2 bytes |

So one sector read is: poke ports 0x1F2..0x1F6 with count + LBA + drive,
poke 0x1F7 with 0x20, spin on 0x1F7 status until BUSY clears and DRDY
sets, then `insw` 256 times from 0x1F0 into the caller's buffer.

## LBA recap

LBA-28 lets us address up to 128 GiB (2^28 * 512). The 28 bits get
split across 0x1F3 (low byte), 0x1F4 (mid byte), 0x1F5 (high byte),
and the low nibble of 0x1F6. The top nibble of 0x1F6 carries the
master/slave flag + LBA-mode flag, hence the `0xE0` constant in the
book's reference code.

## Why this matters for us

In Ch 32 we read the kernel off the disk from real-mode assembly using
BIOS int 0x13. Now that we're firmly in protected mode the BIOS calls
are gone, so we have to drive the controller ourselves. Ch 54 turns the
table above into a `disk_read_sector(lba, total, buf)` function we can
call from C.
