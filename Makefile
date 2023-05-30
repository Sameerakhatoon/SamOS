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

FILES = ./build/kernel.asm.o ./build/kernel.o ./build/string/string.o ./build/memory/memory.o ./build/memory/heap/heap.o ./build/memory/heap/kheap.o ./build/memory/heap/multiheap.o ./build/memory/paging/paging.o ./build/memory/paging/paging.asm.o ./build/io/io.asm.o ./build/idt/idt.o ./build/idt/idt.asm.o ./build/task/task.o ./build/task/task.asm.o ./build/task/process.o ./build/task/tss.asm.o ./build/fs/file.o ./build/fs/pparser.o ./build/fs/fat/fat16.o ./build/disk/disk.o ./build/disk/streamer.o ./build/gdt/gdt.o ./build/keyboard/keyboard.o ./build/keyboard/classic.o
INCLUDES = -I./src
FLAGS = -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc

all: ./bin/boot.bin ./bin/kernel.bin
	rm -rf ./bin/os.bin
	dd if=./bin/boot.bin >> ./bin/os.bin
	dd if=./bin/kernel.bin >> ./bin/os.bin
	dd if=/dev/zero bs=1048576 count=16 >> ./bin/os.bin

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
