# Ch 9 - Booting Our Bootloader On Real Hardware

**Book pages:** 25-26 (Part 4)
**Code in this chapter:** none, procedure only
**Status: not executed, deliberately**

## What the book asks

Burn `boot.bin` onto a USB stick with `dd`, reboot a real machine into BIOS, change boot priority to USB, watch "Hello, World!" appear on bare metal.

```bash
sudo dd if=boot.bin of=/dev/sdx   # /dev/sdx is the USB device
```

The book stresses checking `lsblk` first because picking the wrong device destroys the contents (`dd` writes raw bytes with no confirmation).

## Why SamOs is not doing this step

QEMU emulates a full PC with BIOS and is reliable enough that running on real hardware would not surface anything we can't already see in the emulator. The remaining chapters all develop and test under QEMU. The dd-to-USB step can be done at any later point without affecting the rest of the project, so it stays optional.

## How to do it later if the urge strikes

1. Plug in a USB stick you don't care about.
2. `lsblk` and identify the device (look for the right size, e.g. `/dev/sdb`).
3. `sudo dd if=bin/boot.bin of=/dev/sdb bs=512 count=1 conv=fsync`.
4. Reboot, enter BIOS, set USB as first boot device, save and exit.
5. If the BIOS is UEFI-only and the boot mode is set to UEFI, this will not boot because our boot sector is legacy MBR. Switch to "Legacy" / "CSM" boot mode in firmware.
