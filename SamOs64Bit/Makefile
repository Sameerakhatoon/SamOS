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
# objects. aarch64 and riscv64 compile all of it too, into
# build/<arch>/libsamos.a, which proves it builds for them through
# arch/arch.h alone; their kernels are still the aarch64 MMU work / the riscv64 MMU work test
# mains (PORT_OBJS) and take from the archive only what those
# reference (the aarch64 user-mode work / the riscv64 user-mode work switch them to kernel_main). Their user-mode
# syscall stubs (programs/stdlib/arch/<arch>/) are assembled as well.
#
# Pattern rules: an object's include path is src/, its own directory
# and the one above it (what the per-file rules used to spell out).

ARCH ?= x86_64

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
    virtio/virtio.o virtio/virtio_pci.o virtio/virtio_mmio.o disk/drivers/virtio_blk.o \
    keyboard/virtio_input.o

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
    arch/x86_64/pit.o arch/x86_64/apic.o arch/x86_64/idt.o arch/x86_64/idt.asm.o \
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
user_programs:
	$(MAKE) -C ./programs/stdlib all
	$(MAKE) -C ./programs/blank  all
	$(MAKE) -C ./programs/shell  all
	$(MAKE) -C ./programs/hello  all
	$(MAKE) -C ./programs/simple all
	$(MAKE) -C ./programs/ktest  all

# Lecture 200 - mirror clean targets across user programs.
user_programs_clean:
	$(MAKE) -C ./programs/stdlib clean
	$(MAKE) -C ./programs/blank  clean
	$(MAKE) -C ./programs/shell  clean
	$(MAKE) -C ./programs/hello  clean
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
ARCH_CFLAGS = -mgeneral-regs-only -mstrict-align -mcmodel=small -fno-tree-loop-distribute-patterns
PORT_OBJS = arch/aarch64/boot.S.o arch/aarch64/vectors.S.o arch/aarch64/main.o \
    arch/aarch64/pl011.o firmware/fdt.o arch/aarch64/console.o arch/aarch64/trap.o \
    arch/aarch64/gicv3.o arch/aarch64/timer.o arch/aarch64/sched.o arch/aarch64/fault_test.S.o \
    arch/aarch64/mmu.o arch/aarch64/pfa.o arch/aarch64/memmap.o arch/aarch64/mmu_test.o
else ifeq ($(ARCH),riscv64)
CROSS ?= riscv64-linux-gnu-
# rv64imac: no F/D, so the kernel has no FP state to save yet.
# medany: the kernel sits at 0x80200000, past medlow's +-2 GiB.
# -fno-tree-loop-distribute-patterns: the BSS-style loops must
#   not turn into memset calls, there is no libc.
# -mno-relax: boot.S and mmu_boot run at the load address before
#   the MMU maps the link address, so every symbol reference must
#   stay PC-relative; relaxation may rewrite some.
ARCH_CFLAGS = -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -mno-relax -fno-tree-loop-distribute-patterns
PORT_OBJS = arch/riscv64/boot.S.o arch/riscv64/main.o arch/riscv64/sbi.o \
    arch/riscv64/console.o arch/riscv64/trap.o arch/riscv64/uart16550.o \
    arch/riscv64/timer.o arch/riscv64/plic.o arch/riscv64/kthread.o \
    arch/riscv64/mmu.o arch/riscv64/pfa.o arch/riscv64/uaccess.o \
    arch/riscv64/uaccess.S.o arch/riscv64/mmu_test.o firmware/fdt.o string/string.o
else
$(error ARCH must be x86_64, aarch64 or riscv64)
endif

BUILD   = ./build/$(ARCH)
CC      = $(CROSS)gcc
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

all: ./bin/$(ARCH)/kernel.img $(USER_STUBS)

shared_check: $(BUILD)/libsamos.a

$(BUILD)/libsamos.a: $(SHARED_FILES)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/kernel.elf: $(PORT_FILES) $(BUILD)/libsamos.a ./src/arch/$(ARCH)/linker.ld
	$(CC) $(PORT_CFLAGS) -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
	    -Wl,-Map,$(BUILD)/kernel.map -T ./src/arch/$(ARCH)/linker.ld -o $@ \
	    $(PORT_FILES) $(BUILD)/libsamos.a -lgcc

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

clean:
	rm -rf $(BUILD) ./bin/$(ARCH)
endif

# Every C file of the kernel, shared or the arch's own.
$(BUILD)/%.o: ./src/%.c
	@mkdir -p $(@D)
	$(CC) -I./src -I$(<D) -I$(<D)/.. $(FLAGS) -std=gnu99 -c $< -o $@
