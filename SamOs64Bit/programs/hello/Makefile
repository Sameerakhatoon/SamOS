# The C compiler; the top Makefile sets it for USERCC=samcc.
UCC ?= x86_64-elf-gcc
FILES=./build/hello.o
INCLUDES=-I../stdlib/src
FLAGS=-g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0

all: $(FILES)
	x86_64-elf-gcc -g -T ./linker.ld -Wl,-z,max-page-size=0x1000 -o ./hello.elf -ffreestanding -O0 -nostdlib -fpic -g $(FILES) ../stdlib/stdlib.elf

./build/hello.o: ./src/hello.c
	mkdir -p build
	$(UCC) $(INCLUDES) -I./ $(FLAGS) -std=gnu99 -c ./src/hello.c -o ./build/hello.o

clean:
	rm -rf $(FILES) ./hello.elf
