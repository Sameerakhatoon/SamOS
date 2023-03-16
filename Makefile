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

FILES = ./build/kernel.asm.o ./build/kernel.o
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

clean:
	rm -rf ./bin/boot.bin
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/os.bin
	rm -rf ${FILES}
	rm -rf ./build/kernelfull.o
