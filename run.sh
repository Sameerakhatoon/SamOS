#!/bin/bash
# Lecture 71 - top-level run.sh for the UEFI boot path.
#
# Requires:
#   - OVMF firmware at /usr/share/ovmf/OVMF.fd (apt install ovmf)
#   - bin/os.img built by ./build.sh
#
# For the BIOS path: cd SamOs64Bit && qemu-system-x86_64 -hda bin/os.bin

qemu-system-x86_64 \
  -machine pc \
  -drive file=./bin/os.img,format=raw,if=ide \
  -m 512M \
  -cpu Skylake-Server,tsc-frequency=2800000000 \
  -bios /usr/share/ovmf/OVMF.fd
