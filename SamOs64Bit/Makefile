# Lecture 7 - 64-bit long-mode bootloader.
#
# Toolchain: x86_64-elf-gcc + x86_64-elf-ld (at ~/opt/cross/bin)
# Assembler: nasm with -f elf64
#
# Disk image build still uses mformat + mcopy so we keep the FAT16
# image we already had; the userland binaries and hello.txt are
# parked under /mnt/d-style mount commands in PeachOS64's Makefile -
# we substitute the mformat/mcopy pipeline used by SamOs since it
# does not need sudo or a real mount point.
#
# One Makefile for every architecture :
#
#   make                  x86_64 (the default): bin/kernel.bin and the
#                         user programs, objects in build/
#   make ARCH=aarch64     bin/aarch64/kernel.img, objects in build/aarch64/
#   make ARCH=riscv64     bin/riscv64/kernel.img, objects in build/riscv64/
#
# The shared kernel code is everything under src/ outside src/arch/
# (SHARED_OBJS). x86_64 links all of it with its src/arch/x86_64/
# objects; so do aarch64 and riscv64 with their
# src/arch/<arch>/ objects (PORT_OBJS, KERNEL_LIBS), and both boot into
# kernel_main. build/<arch>/libsamos.a (shared_check) still proves the
# shared code builds through arch/arch.h alone. The user programs are
# built for each port and put in a FAT16 RAM disk image for QEMU's
# -initrd: bin/aarch64/ramdisk.img, bin/riscv64/initrd.img.
#
# Pattern rules: an object's include path is src/, its own directory
# and the one above it (what the per-file rules used to spell out).

ARCH ?= x86_64

# USERCC=samcc compiles the C of every user program (stdlib,
# blank, shell, hello, ktest, rvtest) with SamCompiler
# (tools/samcc-cc.sh, SAMCC names it) instead of gcc. The assembly
# files, crt0 sources and the link stay gcc's. gcc is the default.
USERCC ?= gcc
ifeq ($(USERCC),samcc)
SAMCC_CC = bash $(abspath ./tools/samcc-cc.sh)
else ifneq ($(USERCC),gcc)
$(error USERCC must be gcc or samcc)
endif

.PHONY: all clean user_programs user_programs_clean shared_check

# ------------------------------------------------------------------
# Shared kernel code (every arch).
# ------------------------------------------------------------------
SHARED_OBJS = kernel.o kernel_selftest.o string/string.o \
    memory/memory.o memory/heap/heap.o memory/heap/kheap.o memory/heap/multiheap.o \
    memory/pfa.o memory/heap/slab.o memory/heap/memtest.o \
    io/serial.o io/pci.o io/rtc.o acpi/acpi.o \
    task/task.o task/process.o task/userlandptr.o task/uaccess.o \
    fs/file.o fs/pparser.o fs/fat/fat.o fs/storage_selftest.o \
    disk/disk.o disk/driver.o disk/drivers/pata.o disk/drivers/nvme.o disk/drivers/ahci.o \
    disk/mbr.o disk/streamer.o disk/gpt.o \
    graphics/graphics.o graphics/font.o graphics/font_builtin.o graphics/terminal.o \
    graphics/window.o graphics/image/image.o graphics/image/bmp.o \
    mouse/mouse.o mouse/ps2mouse.o keyboard/keyboard.o keyboard/classic.o \
    isr80h/isr80h.o isr80h/io.o isr80h/heap.o isr80h/misc.o isr80h/window.o \
    isr80h/graphics.o isr80h/time.o isr80h/platform.o isr80h/file.o isr80h/process.o \
    loader/formats/elf.o loader/formats/elfloader.o \
    lib/vector/vector.o lib/spinlock/spinlock.o cpu/smp.o cpu/smp_selftest.o \
    virtio/virtio.o virtio/virtio_pci.o virtio/virtio_mmio.o virtio/virtio_bus.o disk/drivers/virtio_blk.o \
    keyboard/virtio_input.o keyboard/serial_input.o disk/drivers/ramdisk.o \
    usb/xhci.o usb/usb_hid.o usb/usb_hub.o disk/drivers/usb_msc.o disk/drivers/sdhci.o disk/drivers/dw_mmc.o \
    net/netdev.o net/virtio_net.o net/net.o net/socket.o isr80h/net.o \
    net/samtcpip/port.o net/samtcpip/log.o net/samtcpip/gluethread/glthread.o net/samtcpip/graph.o \
    net/samtcpip/net.o net/samtcpip/utils.o net/samtcpip/comm.o net/samtcpip/Layer2/layer2.o \
    net/samtcpip/Layer2/l2switch.o net/samtcpip/Layer3/layer3.o net/samtcpip/Layer3/icmp.o \
    net/samtcpip/Layer3/pmtu.o net/samtcpip/Layer4/udp.o net/samtcpip/Layer4/tcp.o \
    net/samtcpip/Layer5/ping.o

# Freestanding, warnings are errors, no unused-parameter noise.
COMMON_FLAGS = -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -nodefaultlibs \
    -Werror -Wall -Wno-unused-function -Wno-unused-label -Wno-cpp -Wno-unused-parameter
# Extra defines from the command line, e.g.
#   make EXTRA_CFLAGS=-DSAMOS_STORAGE_WRITE_REAL_HW=1
COMMON_FLAGS += $(EXTRA_CFLAGS)

ifeq ($(ARCH),x86_64)
# ------------------------------------------------------------------
# x86_64
# ------------------------------------------------------------------
BUILD = ./build
CC    = x86_64-elf-gcc
LD    = x86_64-elf-ld
# No red zone (an interrupt pushes its frame right
# below the interrupted rsp), no SSE/x87 code in the kernel (user FPU
# state is saved only at the trap boundary), and the kernel code model
# for the higher-half move in the isolation work.
FLAGS = $(COMMON_FLAGS) -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -falign-jumps \
    -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer \
    -finline-functions -O0 -Iinc

# kernel.asm.o first: the entry point starts the image.
ARCH_OBJS = arch/x86_64/kernel.asm.o arch/x86_64/early_fault.o arch/x86_64/cpu.o \
    arch/x86_64/arch.o arch/x86_64/paging.o arch/x86_64/paging.asm.o \
    arch/x86_64/io.asm.o arch/x86_64/cpuid.o arch/x86_64/tsc.o arch/x86_64/tsc.asm.o \
    arch/x86_64/pit.o arch/x86_64/refclock.o arch/x86_64/apic.o arch/x86_64/idt.o arch/x86_64/idt.asm.o \
    arch/x86_64/irq.o arch/x86_64/task.asm.o arch/x86_64/tss.asm.o arch/x86_64/gdt.o \
    arch/x86_64/smp.o arch/x86_64/ap_trampoline.asm.o

FILES = $(addprefix $(BUILD)/,$(ARCH_OBJS) $(SHARED_OBJS))

# The BIOS boot path (boot.asm + a flat FAT16 os.bin) is gone.
# ../build.sh puts kernel.bin and the programs into bin/os.img.
all: ./bin/kernel.bin user_programs

./bin/kernel.bin: $(FILES)
	$(LD) -g -relocatable $(FILES) -o $(BUILD)/kernelfull.o
	$(CC) $(FLAGS) -T ./src/arch/x86_64/linker.ld -o ./bin/kernel.bin -ffreestanding -O0 -nostdlib $(BUILD)/kernelfull.o

$(BUILD)/%.asm.o: ./src/%.asm
	@mkdir -p $(@D)
	nasm -f elf64 -g $< -o $@

# Lecture 200 - the recursive-make calls use `$(MAKE) -C` so
# the parent's make-flags + jobserver propagate cleanly. Order
# matters: stdlib first, then everything that links against it.
ifdef SAMCC_CC
USER_MAKE_ARGS = UCC="$(SAMCC_CC) x86_64-elf-gcc x86_64"
endif
# `clean` keeps the programs' objects, so they are cleaned here
# when the compiler changes: always for samcc (SamCompiler itself may
# have changed), and on the first gcc build after a samcc one.
USERCC_STAMP = ./build/usercc.stamp
user_programs:
	@if [ "$(USERCC)" = samcc ] || [ "$$(cat $(USERCC_STAMP) 2>/dev/null || echo gcc)" != "$(USERCC)" ]; then \
	    $(MAKE) user_programs_clean > /dev/null; fi
	@mkdir -p ./build && echo $(USERCC) > $(USERCC_STAMP)
	$(MAKE) -C ./programs/stdlib all $(USER_MAKE_ARGS)
	$(MAKE) -C ./programs/blank  all $(USER_MAKE_ARGS)
	$(MAKE) -C ./programs/shell  all $(USER_MAKE_ARGS)
	$(MAKE) -C ./programs/hello  all $(USER_MAKE_ARGS)
	$(MAKE) -C ./programs/net    all $(USER_MAKE_ARGS)
	$(MAKE) -C ./programs/simple all
	$(MAKE) -C ./programs/ktest  all $(USER_MAKE_ARGS)

# The user stdlib as a sysroot for another compiler
# (SamCompiler's -target=samos-x86_64): include/ holds the stdlib
# headers, lib/ crt0.o (_start, c_start), libsamos.a (the syscall
# stubs and the libc) and user.ld (the programs' linker script).
SYSROOT = $(BUILD)/x86_64/sysroot
all: sysroot
.PHONY: sysroot
sysroot: user_programs
	rm -rf $(SYSROOT)
	mkdir -p $(SYSROOT)/include $(SYSROOT)/lib
	cp ./programs/stdlib/src/*.h $(SYSROOT)/include/
	$(LD) -r ./programs/stdlib/build/start.asm.o ./programs/stdlib/build/start.o -o $(SYSROOT)/lib/crt0.o
	x86_64-elf-ar rcs $(SYSROOT)/lib/libsamos.a $(addprefix ./programs/stdlib/build/,samos.asm.o samos.o \
	    stdlib.o stdio.o string.o memory.o file.o)
	cp ./programs/shell/linker.ld $(SYSROOT)/lib/user.ld

# Lecture 200 - mirror clean targets across user programs.
user_programs_clean:
	$(MAKE) -C ./programs/stdlib clean
	$(MAKE) -C ./programs/blank  clean
	$(MAKE) -C ./programs/shell  clean
	$(MAKE) -C ./programs/hello  clean
	$(MAKE) -C ./programs/net    clean
	$(MAKE) -C ./programs/simple clean
	$(MAKE) -C ./programs/ktest  clean

clean:
	rm -rf ./bin/kernel.bin
	rm -rf ${FILES}
	rm -rf $(BUILD)/kernelfull.o
	# Lecture 14 safety net: wipe every .o under build/ (not the other
	# arches' build/<arch>/). A module dropped from FILES still leaves
	# its .o behind; the next link would happily pull stale code in if
	# anything else dragged it in. Force them gone every clean.
	find build -path build/aarch64 -prune -o -path build/riscv64 -prune -o -type f -name "*.o" -print -delete 2>/dev/null || true

else
# ------------------------------------------------------------------
# aarch64 and riscv64: the Ubuntu cross compilers <arch>-linux-gnu-gcc,
# used freestanding (no libc, no crt, own linker script). The plan
# named the <arch>-elf-gcc ones; the Linux triplets produce the same
# code with these flags (bootstrap.sh).
# ------------------------------------------------------------------
ifeq ($(ARCH),aarch64)
CROSS ?= aarch64-linux-gnu-
# -mgeneral-regs-only: no FP/SIMD in the kernel (no FP context to
#   save yet, and gcc would otherwise use q registers for copies).
# -mstrict-align: until mmu_early turns the MMU on, all memory is
#   Device memory and an unaligned access faults.
#   Kept for the whole kernel: fdt, the PFA, the console and the
#   page-table code run both before and after.
# -mcmodel=small: adrp/add, PC-relative, so the code linked high
#   also runs at the load address before the MMU is on.
# -fno-tree-loop-distribute-patterns: no memset calls for the
#   table-zeroing loops, there is no libc.
# -mno-outline-atomics: the atomics inline (LDXR/STXR), not libgcc's
#   helpers, which need the C library's __getauxval.
ARCH_CFLAGS = -mgeneral-regs-only -mstrict-align -mcmodel=small -fno-tree-loop-distribute-patterns \
    -mno-outline-atomics
PORT_OBJS = arch/aarch64/boot.S.o arch/aarch64/vectors.S.o arch/aarch64/main.o \
    arch/aarch64/pl011.o firmware/fdt.o arch/aarch64/console.o arch/aarch64/trap.o \
    arch/aarch64/gicv3.o arch/aarch64/timer.o arch/aarch64/sched.o arch/aarch64/fault_test.S.o \
    arch/aarch64/mmu.o arch/aarch64/pfa.o arch/aarch64/memmap.o arch/aarch64/mmu_test.o \
    arch/aarch64/arch.o arch/aarch64/entry.S.o arch/aarch64/paging.o arch/aarch64/uaccess.o \
    arch/aarch64/uaccess.S.o arch/aarch64/acpi_dt.o virtio/virtio_mmio_fdt.o firmware/ramfb.o \
    io/pci_fdt.o arch/aarch64/its.o \
    arch/aarch64/gicv2.o arch/aarch64/bcm2836_intc.o arch/aarch64/rpi.o
# The whole shared kernel, as on x86, except task/uaccess.o:
# arch/aarch64/uaccess.c implements task/uaccess.h with LDTR/STTR and
# the __ex_table fixups instead of a table walk.
KERNEL_LIBS = $(filter-out $(BUILD)/task/uaccess.o,$(SHARED_FILES))
else ifeq ($(ARCH),riscv64)
CROSS ?= riscv64-linux-gnu-
# rv64imac: no F/D, so the kernel has no FP state to save yet.
# medany: the kernel sits at 0x80200000, past medlow's +-2 GiB.
# -fno-tree-loop-distribute-patterns: the BSS-style loops must
#   not turn into memset calls, there is no libc.
# -mno-relax: boot.S and mmu_boot run at the load address before
#   the MMU maps the link address, so every symbol reference must
#   stay PC-relative; relaxation may rewrite some.
# -include layout.h: the address-space layout is the paging
#   mode's, probed at boot, so config.h's constants become variables.
ARCH_CFLAGS = -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -mno-relax -fno-tree-loop-distribute-patterns \
    -include ./src/arch/riscv64/layout.h
PORT_OBJS = arch/riscv64/boot.S.o arch/riscv64/main.o arch/riscv64/sbi.o \
    arch/riscv64/console.o arch/riscv64/trap.o arch/riscv64/uart16550.o \
    arch/riscv64/timer.o arch/riscv64/plic.o arch/riscv64/kthread.o \
    arch/riscv64/mmu.o arch/riscv64/paging.o arch/riscv64/arch.o arch/riscv64/fpu.S.o \
    arch/riscv64/uaccess.o arch/riscv64/uaccess.S.o arch/riscv64/mmu_test.o firmware/fdt.o \
    virtio/virtio_mmio_fdt.o firmware/ramfb.o arch/riscv64/smp.o io/pci_fdt.o arch/riscv64/aia.o \
    firmware/fdt_dev.o arch/riscv64/dma.o
# The kernel is the shared one (kernel_main): every shared
# object is linked, except task/uaccess.o, which uaccess.c replaces
# (SUM and the fixup table).
KERNEL_LIBS = $(filter-out $(BUILD)/task/uaccess.o,$(SHARED_FILES))
# User programs: rv64gc, so they have F and D (the kernel saves f0-f31).
USER_ARCH_CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany
else
$(error ARCH must be x86_64, aarch64 or riscv64)
endif

BUILD   = ./build/$(ARCH)
CC      = $(CROSS)gcc
# The user programs' compiler, SamCompiler for USERCC=samcc.
USER_CC = $(if $(SAMCC_CC),$(SAMCC_CC) $(CC) $(ARCH),$(CC))
AR      = $(CROSS)ar
OBJCOPY = $(CROSS)objcopy
NOPIC   = -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables
# The port's own files keep the early aarch64 / early riscv64 flags.
PORT_CFLAGS = -ffreestanding -fno-builtin -nostdlib $(NOPIC) -O2 -g -std=gnu99 \
    -Wall -Wextra -Werror -Wno-unused-parameter -I./src $(ARCH_CFLAGS)
# The shared code builds as on x86: -O0, the x86 warning set.
FLAGS = $(COMMON_FLAGS) $(NOPIC) -O0 $(ARCH_CFLAGS)

PORT_FILES   = $(addprefix $(BUILD)/,$(PORT_OBJS))
PORT_C_FILES = $(filter-out %.S.o,$(PORT_FILES))
SHARED_FILES = $(addprefix $(BUILD)/,$(filter-out $(PORT_OBJS),$(SHARED_OBJS)))
USER_STUBS   = $(patsubst ./programs/stdlib/arch/$(ARCH)/%.S,$(BUILD)/user/%.o,$(wildcard ./programs/stdlib/arch/$(ARCH)/*.S))

# The port's user programs: the shared stdlib (programs/stdlib/
# src) over the arch's stubs and start, BLANK.ELF and HELLO.ELF from
# programs/, the shell, RVTEST.ELF (programs/rvtest, the user-mode
# probes) and KTEST.ELF (the e2e probes). WRONGM.ELF is RVTEST.ELF with e_machine set to EM_X86_64,
# for the loader's machine check. They reach the kernel as a FAT16 image
# (label SAMOS) for QEMU's -initrd, with HELLO.TXT and the system font
# (the shell's cat, the terminal on ramfb), as on aarch64.
ifdef USER_ARCH_CFLAGS
USER_DIR    = $(BUILD)/user
USER_CFLAGS = -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -nodefaultlibs $(NOPIC) \
    -O0 -std=gnu99 -Wall -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-label -Wno-cpp \
    -I./programs/stdlib/src $(USER_ARCH_CFLAGS)
USER_LIBC   = $(addprefix $(USER_DIR)/libc/,start.S.o samos.S.o start.o samos.o stdlib.o stdio.o \
    string.o memory.o file.o)
USER_ELFS   = $(addprefix $(USER_DIR)/,blank.elf hello.elf shell.elf rvtest.elf ktest.elf \
    ping.elf udpecho.elf httpd.elf wget.elf)
USER_LD     = ./programs/stdlib/arch/$(ARCH)/linker.ld
USER_ALL    = ./bin/$(ARCH)/initrd.img ./bin/$(ARCH)/os.img
SYSROOT_CRT0 = $(USER_DIR)/libc/start.S.o $(USER_DIR)/libc/start.o
SYSROOT_LIBS = $(filter-out $(SYSROOT_CRT0),$(USER_LIBC))
endif

all: ./bin/$(ARCH)/kernel.img $(USER_STUBS) $(USER_ALL)

ifeq ($(ARCH),aarch64)
# ------------------------------------------------------------------
# aarch64 user programs and the RAM disk. The stdlib's C files
# and the programs as on x86 (-O0, the same warnings; FP/SIMD allowed,
# the kernel saves it), the svc stubs from programs/stdlib/arch/aarch64/,
# linked static at 0x400000 by user.ld. The image is FAT16 with the
# label SAMOS, so "@:/" finds it as on the x86 disk, with the system
# font and HELLO.TXT as there; X86BLANK.ELF is the x86 build's
# blank.elf, an ELF for the wrong machine.
# ------------------------------------------------------------------
USER_BUILD  = $(BUILD)/user
USER_CFLAGS = -ffreestanding -fno-builtin -nostdlib -nostartfiles -nodefaultlibs $(NOPIC) -O0 -g \
    -std=gnu99 -Wall -Werror -Wno-unused-function -Wno-unused-label -Wno-cpp -Wno-unused-parameter \
    -mno-outline-atomics -I./programs/stdlib/src
USER_LD     = ./programs/stdlib/arch/aarch64/user.ld
STDLIB_OBJS = $(USER_STUBS) $(addprefix $(USER_BUILD)/stdlib/src/,start.o samos.o stdlib.o stdio.o \
    string.o memory.o file.o)
USER_ELFS   = $(addprefix $(USER_BUILD)/,blank.elf hello.elf ktest.elf shell.elf \
    ping.elf udpecho.elf httpd.elf wget.elf)
RAMDISK     = ./bin/aarch64/ramdisk.img
SYSROOT_CRT0 = $(BUILD)/user/start.o $(USER_BUILD)/stdlib/src/start.o
SYSROOT_LIBS = $(filter-out $(SYSROOT_CRT0),$(STDLIB_OBJS))

all: $(RAMDISK)

$(USER_BUILD)/%.o: ./programs/%.c
	@mkdir -p $(@D)
	$(USER_CC) $(USER_CFLAGS) -I$(<D) -I$(<D)/.. -c $< -o $@

$(USER_BUILD)/%.S.o: ./programs/%.S
	@mkdir -p $(@D)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD)/blank.elf: $(USER_BUILD)/blank/blank.o
$(USER_BUILD)/hello.elf: $(USER_BUILD)/hello/src/hello.o
$(USER_BUILD)/ktest.elf: $(USER_BUILD)/ktest/ktest.o $(USER_BUILD)/ktest/arch/aarch64/regs.S.o
$(USER_BUILD)/shell.elf: $(USER_BUILD)/shell/src/shell.o
$(USER_BUILD)/ping.elf: $(USER_BUILD)/net/ping.o
$(USER_BUILD)/udpecho.elf: $(USER_BUILD)/net/udpecho.o
$(USER_BUILD)/httpd.elf: $(USER_BUILD)/net/httpd.o
$(USER_BUILD)/wget.elf: $(USER_BUILD)/net/wget.o
$(USER_ELFS): $(STDLIB_OBJS) $(USER_LD)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -T $(USER_LD) \
	    -o $@ $(filter %.o,$^) -lgcc

$(RAMDISK): $(USER_ELFS) ./hello.txt ./programs/blank/blank.elf ./assets/sysfont.bmp
	@mkdir -p $(@D)
	rm -f $@
	mkfs.fat -F 16 -s 2 -n SAMOS -C $@ 8192 > /dev/null || { rm -f $@; exit 1; }
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ $(USER_BUILD)/blank.elf ::/BLANK.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ $(USER_BUILD)/hello.elf ::/HELLO.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ $(USER_BUILD)/ktest.elf ::/KTEST.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ $(USER_BUILD)/shell.elf ::/SHELL.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ ./hello.txt ::/HELLO.TXT
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ ./assets/sysfont.bmp ::/SYSFONT.BMP
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ ./programs/blank/blank.elf ::/X86BLANK.ELF
	@echo "make: $@ ($$(stat -c%s $@) bytes)"

# The boot disk for virtio-blk, laid out as x86's bin/os.img (the
# e2e suite's image on aarch64).
OSIMG       = ./bin/aarch64/os.img

all: $(OSIMG)

$(OSIMG): ./bin/aarch64/kernel.img $(USER_ELFS) ./hello.txt ./assets/sysfont.bmp ./tools/arch-osimg.sh
	bash ./tools/arch-osimg.sh $@ ./bin/aarch64/kernel.img $(USER_BUILD)
	@echo "make: $@ ($$(du -h $@ | cut -f1) on disk)"

# The Raspberry Pi 4 / 5 boot files, not part of `all`: the same
# kernel.img as kernel8.img, tools/pi/config.txt, and an SD card image
# with both and the programs on one FAT32 partition (tools/pi-sdimg.sh).
PI_DIR      = ./bin/aarch64/pi

pi: $(PI_DIR)/sdcard.img

$(PI_DIR)/sdcard.img: ./bin/aarch64/kernel.img $(USER_ELFS) $(RAMDISK) ./hello.txt ./assets/sysfont.bmp \
    ./tools/pi-sdimg.sh ./tools/pi/config.txt
	bash ./tools/pi-sdimg.sh $(PI_DIR) ./bin/aarch64/kernel.img $(USER_BUILD)
	@echo "make: $(PI_DIR)/kernel8.img, config.txt, sdcard.img"

.PHONY: pi
endif

# The port's user stdlib as a sysroot for SamCompiler's
# -target=samos-<arch>, laid out as on x86 (the x86 sysroot rule).
SYSROOT = $(BUILD)/sysroot
all: $(SYSROOT)/lib/libsamos.a

$(SYSROOT)/lib/libsamos.a: $(SYSROOT_CRT0) $(SYSROOT_LIBS) $(USER_LD)
	rm -rf $(SYSROOT)
	mkdir -p $(SYSROOT)/include $(SYSROOT)/lib
	cp ./programs/stdlib/src/*.h $(SYSROOT)/include/
	$(CROSS)ld -r $(SYSROOT_CRT0) -o $(SYSROOT)/lib/crt0.o
	$(AR) rcs $@ $(SYSROOT_LIBS)
	cp $(USER_LD) $(SYSROOT)/lib/user.ld

shared_check: $(BUILD)/libsamos.a

$(BUILD)/libsamos.a: $(SHARED_FILES)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/kernel.elf: $(PORT_FILES) $(KERNEL_LIBS) ./src/arch/$(ARCH)/linker.ld
	$(CC) $(PORT_CFLAGS) -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
	    -Wl,-Map,$(BUILD)/kernel.map -T ./src/arch/$(ARCH)/linker.ld -o $@ \
	    $(PORT_FILES) $(KERNEL_LIBS) -lgcc

./bin/$(ARCH)/kernel.img: $(BUILD)/kernel.elf
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@
	@echo "make: $@ ($$(stat -c%s $@) bytes)"

$(PORT_C_FILES): $(BUILD)/%.o: ./src/%.c
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) -c $< -o $@

$(BUILD)/%.S.o: ./src/%.S
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) -c $< -o $@

$(BUILD)/user/%.o: ./programs/stdlib/arch/$(ARCH)/%.S
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) -I./programs/stdlib/src -c $< -o $@

ifdef USER_ARCH_CFLAGS
$(USER_DIR)/libc/%.o: ./programs/stdlib/src/%.c
	@mkdir -p $(@D)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DIR)/libc/%.S.o: ./programs/stdlib/arch/$(ARCH)/%.S
	@mkdir -p $(@D)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DIR)/blank.elf: ./programs/blank/blank.c
$(USER_DIR)/hello.elf: ./programs/hello/src/hello.c
$(USER_DIR)/shell.elf: ./programs/shell/src/shell.c
$(USER_DIR)/ping.elf: ./programs/net/ping.c
$(USER_DIR)/udpecho.elf: ./programs/net/udpecho.c
$(USER_DIR)/httpd.elf: ./programs/net/httpd.c
$(USER_DIR)/wget.elf: ./programs/net/wget.c
$(USER_DIR)/rvtest.elf: ./programs/rvtest/rvtest.c ./programs/rvtest/regs.S
$(USER_DIR)/ktest.elf: ./programs/ktest/ktest.c ./programs/ktest/arch/riscv64/regs.S
$(USER_ELFS): $(USER_LIBC) $(USER_LD)
	$(USER_CC) $(USER_CFLAGS) -I./programs/shell/src -T $(USER_LD) -Wl,-z,max-page-size=0x1000 \
	    -Wl,--build-id=none -static -o $@ $(filter %.c %.S,$^) $(USER_LIBC) -lgcc

# mtools on the image file, as build.sh does for x86 (8.3 names).
./bin/$(ARCH)/initrd.img: $(USER_ELFS) ./hello.txt ./assets/sysfont.bmp
	@mkdir -p $(@D)
	cp $(USER_DIR)/rvtest.elf $(USER_DIR)/wrongm.elf
	printf '\076\000' | dd of=$(USER_DIR)/wrongm.elf bs=1 seek=18 conv=notrunc status=none
	rm -f $@
	mkfs.fat -C -F 16 -s 1 -n SAMOS $@ 8192 > /dev/null || { rm -f $@; exit 1; }
	for f in $(USER_ELFS) $(USER_DIR)/wrongm.elf; do \
	    MTOOLS_SKIP_CHECK=1 MTOOLS_NO_VFAT=1 mcopy -i $@ $$f ::/$$(basename $$f | tr a-z A-Z) \
	        || { rm -f $@; exit 1; }; \
	done
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ ./hello.txt ::/HELLO.TXT
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ ./assets/sysfont.bmp ::/SYSFONT.BMP
	@echo "make: $@ ($$(stat -c%s $@) bytes)"

# The boot disk for virtio-blk, laid out as x86's bin/os.img (the
# e2e suite's image on riscv64, as on aarch64).
./bin/$(ARCH)/os.img: ./bin/$(ARCH)/kernel.img $(USER_ELFS) ./hello.txt ./assets/sysfont.bmp ./tools/arch-osimg.sh
	bash ./tools/arch-osimg.sh $@ ./bin/$(ARCH)/kernel.img $(USER_DIR)
	@echo "make: $@ ($$(du -h $@ | cut -f1) on disk)"
endif

clean:
	rm -rf $(BUILD) ./bin/$(ARCH)
endif

# Every C file of the kernel, shared or the arch's own.
$(BUILD)/%.o: ./src/%.c
	@mkdir -p $(@D)
	$(CC) -I./src -I$(<D) -I$(<D)/.. $(FLAGS) -std=gnu99 -c $< -o $@
