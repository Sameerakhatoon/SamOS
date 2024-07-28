# The C compiler; the top Makefile sets it for USERCC=samcc.
UCC ?= x86_64-elf-gcc
# ktest: user-mode probes for the trap and task-switch paths.
# The e2e tests start it through CMDLINE.TXT (samos.run=KTEST.ELF ...).
FILES=./build/ktest.o ./build/regs.asm.o
INCLUDES=-I../stdlib/src

all: ./ktest.elf

./ktest.elf: $(FILES) ../stdlib/stdlib.elf
	x86_64-elf-gcc -g -T ./linker.ld -Wl,-z,max-page-size=0x1000 -o ./ktest.elf -ffreestanding -O0 -nostdlib -fpic -g $(FILES) ../stdlib/stdlib.elf

./build/ktest.o: ./ktest.c
	mkdir -p build
	$(UCC) $(INCLUDES) -g -ffreestanding -fno-builtin -Werror -Wall -nostdlib -nostartfiles -nodefaultlibs -O0 -std=gnu99 -c ./ktest.c -o ./build/ktest.o

./build/regs.asm.o: ./regs.asm
	mkdir -p build
	nasm -f elf64 -g ./regs.asm -o ./build/regs.asm.o

clean:
	rm -rf $(FILES) ./ktest.elf
