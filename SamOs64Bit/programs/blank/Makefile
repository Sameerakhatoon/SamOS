# The C compiler; the top Makefile sets it for USERCC=samcc.
UCC ?= x86_64-elf-gcc
FILES=./build/blank.o
INCLUDES=-I../stdlib/src

all: $(FILES) ./blank.bin ./blank.elf

./blank.elf: $(FILES) ../stdlib/stdlib.elf
	x86_64-elf-gcc -g -T ./linker-elf.ld -Wl,-z,max-page-size=0x1000 -o ./blank.elf -ffreestanding -O0 -nostdlib -fpic -g $(FILES) ../stdlib/stdlib.elf

./blank.bin: $(FILES) ../stdlib/stdlib.elf
	x86_64-elf-gcc -g -T ./linker.ld -o ./blank.bin -ffreestanding -O0 -nostdlib -fpic -g $(FILES) ../stdlib/stdlib.elf

./build/blank.o: ./blank.c
	mkdir -p build
	$(UCC) $(INCLUDES) -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu99 -c ./blank.c -o ./build/blank.o

clean:
	rm -rf $(FILES) ./blank.elf ./blank.bin
