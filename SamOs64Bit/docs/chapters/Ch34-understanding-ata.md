# Ch 30 - Understanding ATA Drives

**Book pages:** 73-74 (Part 5)
**Code in this chapter:** none, prose

## What ATA is

ATA (Advanced Technology Attachment), also called IDE (Integrated Drive Electronics), is the family of standards for connecting storage to a PC motherboard. SATA is the modern serial variant. The book targets the older parallel ATA used by QEMU's default disk emulation. The wire-level protocol is the same; only the connector and timings change.

## How we talk to ATA from software

A pile of I/O ports. The primary ATA channel exposes ports `0x1F0` through `0x1F7` and `0x3F6`. Each port is one byte wide. We write commands and arguments to specific ports and read responses from others. There is no shared memory, just `in` and `out` instructions.

| Port | Direction | Purpose |
|------|-----------|---------|
| 0x1F0 | both | Data register (read or write 16 bits per access) |
| 0x1F1 | read | Error info; write: features |
| 0x1F2 | both | Sector count to transfer |
| 0x1F3 | both | LBA byte 0 |
| 0x1F4 | both | LBA byte 1 |
| 0x1F5 | both | LBA byte 2 |
| 0x1F6 | both | Drive select + LBA byte 3 (top 4 bits) |
| 0x1F7 | both | Command (write) or status (read) |
| 0x3F6 | both | Alternate status / device control |

## The read flow at a high level

1. Tell the disk we want to read a specific LBA (write LBA bytes to ports 0x1F3 through 0x1F6, set drive bits in 0x1F6).
2. Tell the disk how many sectors (write count to 0x1F2).
3. Issue the READ command (`0x20` to 0x1F7).
4. Poll port 0x1F7's status. Bit 3 (DRQ, data request) goes high when a sector's worth of data is ready to be transferred.
5. Read 256 16-bit words from port 0x1F0 (= one 512-byte sector).
6. Loop for the next sector if there is more to read.

We will implement this exact flow in assembly next chapter. The book provides the listing.

## Compared to the BIOS INT 0x13 path

In Real Mode we just called `int 0x13` and the BIOS did all this for us. Now that we are in Protected Mode the BIOS is unavailable (its routines live in 16-bit Real Mode code that we can no longer return to). We have to talk to the hardware directly.

## Commands the book mentions

| Command | Code |
|---------|------|
| READ SECTORS (PIO) | 0x20 |
| WRITE SECTORS (PIO) | 0x30 |
| IDENTIFY DEVICE | 0xEC |

For SamOs the only command we need for now is 0x20.
