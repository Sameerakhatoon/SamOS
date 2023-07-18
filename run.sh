#!/bin/bash
# run.sh - boot bin/os.img (built by ./build.sh) in QEMU under OVMF.
#
# The kernel's COM1 console is on this terminal (-serial stdio);
# the framebuffer opens in a QEMU window. Extra arguments are passed
# to QEMU, e.g. `./run.sh -machine q35` or `./run.sh -display none`.
#
# Requires OVMF at /usr/share/ovmf/OVMF.fd (apt install ovmf).

cd "$(dirname "$0")"
exec qemu-system-x86_64 \
  -machine pc \
  -drive file=./bin/os.img,format=raw,if=ide \
  -m 512M \
  -cpu Skylake-Server,tsc-frequency=2800000000 \
  -bios /usr/share/ovmf/OVMF.fd \
  -serial stdio \
  "$@"
