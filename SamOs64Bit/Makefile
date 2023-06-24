# Lecture 7 - 64-bit long-mode bootloader.
#
# Only kernel.asm is in the build right now. Every C and other-asm
# file from the 32-bit kernel is sitting on disk under src/ but
# disabled; the FILES list will grow back as the 32-bit subsystems
# get ported lecture by lecture (Lecture 8 onward).
#
# Toolchain: x86_64-elf-gcc + x86_64-elf-ld (at ~/opt/cross/bin)
# Assembler: nasm with -f elf64
#
# Disk image build still uses mformat + mcopy so we keep the FAT16
# image we already had; the userland binaries and hello.txt are
# parked under /mnt/d-style mount commands in PeachOS64's Makefile -
# we substitute the mformat/mcopy pipeline used by SamOs since it
# does not need sudo or a real mount point.

FILES = ./build/kernel.asm.o ./build/kernel.o ./build/string/string.o ./build/memory/memory.o ./build/memory/heap/heap.o ./build/memory/heap/kheap.o ./build/memory/heap/multiheap.o ./build/memory/paging/paging.o ./build/memory/paging/paging.asm.o ./build/io/io.asm.o ./build/idt/idt.o ./build/idt/idt.asm.o ./build/idt/irq.o ./build/task/task.o ./build/task/task.asm.o ./build/task/process.o ./build/task/tss.asm.o ./build/fs/file.o ./build/fs/pparser.o ./build/fs/fat/fat16.o ./build/disk/disk.o ./build/disk/streamer.o ./build/gdt/gdt.o ./build/keyboard/keyboard.o ./build/keyboard/classic.o ./build/isr80h/isr80h.o ./build/isr80h/io.o ./build/isr80h/heap.o ./build/isr80h/misc.o ./build/isr80h/process.o ./build/loader/formats/elf.o ./build/loader/formats/elfloader.o
INCLUDES = -I./src
FLAGS = -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc

all: ./bin/boot.bin ./bin/kernel.bin user_programs
	rm -rf ./bin/os.bin
	# Build the on-disk image as: boot + kernel + 16 MiB zero pad.
	dd if=./bin/boot.bin >> ./bin/os.bin
	dd if=./bin/kernel.bin >> ./bin/os.bin
	dd if=/dev/zero bs=1048576 count=16 >> ./bin/os.bin
	# Lecture 58 - lay a FAT16 filesystem ONTO THE WHOLE IMAGE
	# but preserve SamOs's boot sector (-k = keep boot sector +
	# preserve our BPB). Then mcopy user programs in. SamOs uses
	# mformat / mcopy (no sudo) instead of upstream's mount / cp.
	#
	# Our boot.asm BPB says ReservedSectors=200 (= 100 KiB), so
	# the FAT starts at sector 200. kernel.bin is well under
	# 100 KiB so it doesn't overlap the FAT region.
	# -R 200    200 reserved sectors. Our boot.asm BPB says 200,
	#           and kernel.bin loads to sectors 1..N (N < 200).
	#           Without -R, mformat picks 1 and FAT overwrites
	#           kernel.bin.
	# -c 4      4 sectors per cluster. With 16 MiB disk and 1
	#           sector = 512 bytes, this gives ~8192 clusters -
	#           in the FAT16 range (4085..65525) so mformat
	#           picks FAT16 and doesn't go FAT32 on us.
	# (no -k)   we let mformat write a fresh boot sector. The
	#           BPB it produces is the one disk_search_and_init
	#           will parse, and the boot CODE in it is mformat's
	#           generic "not bootable" stub.
	# IMPORTANT: this means BIOS won't boot from os.bin directly.
	# We re-prepend our SamOs boot.bin AFTER mformat by dd-ing
	# its first 62 bytes (jmp + BPB-header up to the OEMIdentifier
	# start) followed by the boot-asm CODE proper at the right
	# offset. Cleaner: just dd boot.bin over the whole boot sector.
	# We keep mformat's BPB in there too, because boot.bin's BPB
	# is the one the BIOS reads but the kernel's disk driver
	# trusts whatever's there at runtime.
	mformat -R 200 -c 4 -i ./bin/os.bin :: || true
	# Overlay our SamOs boot.bin's boot CODE (offsets 62..511)
	# while keeping mformat's BPB (offsets 0..61). This way:
	#   - BIOS at offset 510 sees 0xAA55 (mformat preserves it)
	#   - BIOS sees a valid FAT16 BPB to read filesystem geometry
	#   - The bootstrap CODE at offset 62 is OUR boot.asm, which
	#     loads kernel.bin at sectors 1..N and jumps to 0x100000.
	# kernel.bin is below sector 200 (ReservedSectors), so the
	# FATs are safely above it.
	# Overlay our SamOs boot.bin's boot CODE (offsets 62..511)
	# WITH the boot signature (510..511). 450 bytes covers code
	# + signature. mformat preserves the BPB (offsets 0..61) it
	# just wrote.
	dd if=./bin/boot.bin of=./bin/os.bin bs=1 skip=62 seek=62 count=450 conv=notrunc 2>/dev/null
	mcopy -n -i ./bin/os.bin ./programs/simple/build/simple.bin ::SIMPLE.BIN || true
	# L63 - drop the ELF programs onto the disk too.
	mcopy -n -i ./bin/os.bin ./programs/blank/blank.elf  ::BLANK.ELF  || true
	mcopy -n -i ./bin/os.bin ./programs/shell/shell.elf  ::SHELL.ELF  || true

user_programs:
	cd ./programs/simple && $(MAKE) all
	cd ./programs/stdlib && $(MAKE) all
	cd ./programs/blank  && $(MAKE) all
	cd ./programs/shell  && $(MAKE) all

./bin/kernel.bin: $(FILES)
	x86_64-elf-ld -g -relocatable $(FILES) -o ./build/kernelfull.o
	x86_64-elf-gcc $(FLAGS) -T ./src/linker.ld -o ./bin/kernel.bin -ffreestanding -O0 -nostdlib ./build/kernelfull.o

./bin/boot.bin: ./src/boot/boot.asm
	nasm -f bin ./src/boot/boot.asm -o ./bin/boot.bin

./build/kernel.asm.o: ./src/kernel.asm
	nasm -f elf64 -g ./src/kernel.asm -o ./build/kernel.asm.o

./build/kernel.o: ./src/kernel.c
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -std=gnu99 -c ./src/kernel.c -o ./build/kernel.o

./build/string/string.o: ./src/string/string.c
	x86_64-elf-gcc $(INCLUDES) -I./src/string $(FLAGS) -std=gnu99 -c ./src/string/string.c -o ./build/string/string.o

./build/memory/memory.o: ./src/memory/memory.c
	x86_64-elf-gcc $(INCLUDES) -I./src/memory $(FLAGS) -std=gnu99 -c ./src/memory/memory.c -o ./build/memory/memory.o

./build/memory/heap/heap.o: ./src/memory/heap/heap.c
	x86_64-elf-gcc $(INCLUDES) -I./src/memory/heap $(FLAGS) -std=gnu99 -c ./src/memory/heap/heap.c -o ./build/memory/heap/heap.o

./build/memory/heap/kheap.o: ./src/memory/heap/kheap.c
	x86_64-elf-gcc $(INCLUDES) -I./src/memory/heap $(FLAGS) -std=gnu99 -c ./src/memory/heap/kheap.c -o ./build/memory/heap/kheap.o

./build/memory/heap/multiheap.o: ./src/memory/heap/multiheap.c
	x86_64-elf-gcc $(INCLUDES) -I./src/memory/heap $(FLAGS) -std=gnu99 -c ./src/memory/heap/multiheap.c -o ./build/memory/heap/multiheap.o

./build/io/io.asm.o: ./src/io/io.asm
	nasm -f elf64 -g ./src/io/io.asm -o ./build/io/io.asm.o

./build/idt/idt.o: ./src/idt/idt.c
	x86_64-elf-gcc $(INCLUDES) -I./src/idt $(FLAGS) -std=gnu99 -c ./src/idt/idt.c -o ./build/idt/idt.o

./build/idt/irq.o: ./src/idt/irq.c
	x86_64-elf-gcc $(INCLUDES) -I./src/idt $(FLAGS) -std=gnu99 -c ./src/idt/irq.c -o ./build/idt/irq.o

./build/idt/idt.asm.o: ./src/idt/idt.asm
	nasm -f elf64 -g ./src/idt/idt.asm -o ./build/idt/idt.asm.o

./build/task/task.o: ./src/task/task.c
	x86_64-elf-gcc $(INCLUDES) -I./src/task $(FLAGS) -std=gnu99 -c ./src/task/task.c -o ./build/task/task.o

./build/task/task.asm.o: ./src/task/task.asm
	nasm -f elf64 -g ./src/task/task.asm -o ./build/task/task.asm.o

./build/task/process.o: ./src/task/process.c
	x86_64-elf-gcc $(INCLUDES) -I./src/task $(FLAGS) -std=gnu99 -c ./src/task/process.c -o ./build/task/process.o

./build/task/tss.asm.o: ./src/task/tss.asm
	nasm -f elf64 -g ./src/task/tss.asm -o ./build/task/tss.asm.o

./build/fs/file.o: ./src/fs/file.c
	x86_64-elf-gcc $(INCLUDES) -I./src/fs $(FLAGS) -std=gnu99 -c ./src/fs/file.c -o ./build/fs/file.o

./build/fs/pparser.o: ./src/fs/pparser.c
	x86_64-elf-gcc $(INCLUDES) -I./src/fs $(FLAGS) -std=gnu99 -c ./src/fs/pparser.c -o ./build/fs/pparser.o

./build/fs/fat/fat16.o: ./src/fs/fat/fat16.c
	x86_64-elf-gcc $(INCLUDES) -I./src/fs -I./src/fs/fat $(FLAGS) -std=gnu99 -c ./src/fs/fat/fat16.c -o ./build/fs/fat/fat16.o

./build/disk/disk.o: ./src/disk/disk.c
	x86_64-elf-gcc $(INCLUDES) -I./src/disk $(FLAGS) -std=gnu99 -c ./src/disk/disk.c -o ./build/disk/disk.o

./build/disk/streamer.o: ./src/disk/streamer.c
	x86_64-elf-gcc $(INCLUDES) -I./src/disk $(FLAGS) -std=gnu99 -c ./src/disk/streamer.c -o ./build/disk/streamer.o

./build/gdt/gdt.o: ./src/gdt/gdt.c
	x86_64-elf-gcc $(INCLUDES) -I./src/gdt $(FLAGS) -std=gnu99 -c ./src/gdt/gdt.c -o ./build/gdt/gdt.o

./build/keyboard/keyboard.o: ./src/keyboard/keyboard.c
	x86_64-elf-gcc $(INCLUDES) -I./src/keyboard $(FLAGS) -std=gnu99 -c ./src/keyboard/keyboard.c -o ./build/keyboard/keyboard.o

./build/keyboard/classic.o: ./src/keyboard/classic.c
	x86_64-elf-gcc $(INCLUDES) -I./src/keyboard $(FLAGS) -std=gnu99 -c ./src/keyboard/classic.c -o ./build/keyboard/classic.o

./build/isr80h/isr80h.o: ./src/isr80h/isr80h.c
	x86_64-elf-gcc $(INCLUDES) -I./src/isr80h $(FLAGS) -std=gnu99 -c ./src/isr80h/isr80h.c -o ./build/isr80h/isr80h.o

./build/isr80h/io.o: ./src/isr80h/io.c
	x86_64-elf-gcc $(INCLUDES) -I./src/isr80h $(FLAGS) -std=gnu99 -c ./src/isr80h/io.c -o ./build/isr80h/io.o

./build/isr80h/heap.o: ./src/isr80h/heap.c
	x86_64-elf-gcc $(INCLUDES) -I./src/isr80h $(FLAGS) -std=gnu99 -c ./src/isr80h/heap.c -o ./build/isr80h/heap.o

./build/isr80h/misc.o: ./src/isr80h/misc.c
	x86_64-elf-gcc $(INCLUDES) -I./src/isr80h $(FLAGS) -std=gnu99 -c ./src/isr80h/misc.c -o ./build/isr80h/misc.o

./build/isr80h/process.o: ./src/isr80h/process.c
	x86_64-elf-gcc $(INCLUDES) -I./src/isr80h $(FLAGS) -std=gnu99 -c ./src/isr80h/process.c -o ./build/isr80h/process.o

./build/loader/formats/elf.o: ./src/loader/formats/elf.c
	mkdir -p ./build/loader/formats
	x86_64-elf-gcc $(INCLUDES) -I./src/loader/formats $(FLAGS) -std=gnu99 -c ./src/loader/formats/elf.c -o ./build/loader/formats/elf.o

./build/loader/formats/elfloader.o: ./src/loader/formats/elfloader.c
	mkdir -p ./build/loader/formats
	x86_64-elf-gcc $(INCLUDES) -I./src/loader/formats $(FLAGS) -std=gnu99 -c ./src/loader/formats/elfloader.c -o ./build/loader/formats/elfloader.o

./build/memory/paging/paging.o: ./src/memory/paging/paging.c
	x86_64-elf-gcc $(INCLUDES) -I./src/memory/paging $(FLAGS) -std=gnu99 -c ./src/memory/paging/paging.c -o ./build/memory/paging/paging.o

./build/memory/paging/paging.asm.o: ./src/memory/paging/paging.asm
	nasm -f elf64 -g ./src/memory/paging/paging.asm -o ./build/memory/paging/paging.asm.o

clean:
	rm -rf ./bin/boot.bin
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/os.bin
	rm -rf ${FILES}
	rm -rf ./build/kernelfull.o
	# Lecture 14 safety net: wipe every .o under build/.
	# A module dropped from FILES still leaves its .o behind; the next
	# link would happily pull stale code in if anything else dragged
	# it in. Force them gone every clean.
	find build -type f -name "*.o" -print -delete 2>/dev/null || true
