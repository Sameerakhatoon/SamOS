# Lecture 181 - PCI source part 4

Upstream: PeachOS64BitCourse b0acca5.

A tiny commit (titled "IMplementing thePCI source files part four"
in upstream; preserved as-is). Adds three lines to `kernel.c`
right before the "Loading program..." print:

    print("Total PCI devices:");
    print(itoa((int)pci_device_count()));
    print("\n");

That's enough to confirm the L180 bus scan actually ran. No
header or Makefile changes.

## BIOS test status

Source + link. Build links.
