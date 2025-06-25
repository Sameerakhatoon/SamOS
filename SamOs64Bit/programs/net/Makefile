# The network programs: ping, udpecho, httpd, wget (x86_64;
# the ports build them from the top-level Makefile).
PROGS=ping udpecho httpd wget
# The C compiler; the top Makefile sets it for USERCC=samcc.
UCC ?= x86_64-elf-gcc
INCLUDES=-I../stdlib/src
FLAGS=-g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0

all: $(addsuffix .elf,$(PROGS))

%.elf: ./build/%.o
	x86_64-elf-gcc -g -T ../hello/linker.ld -Wl,-z,max-page-size=0x1000 -o $@ -ffreestanding -O0 -nostdlib -fpic -g $< ../stdlib/stdlib.elf

./build/%.o: ./%.c
	mkdir -p build
	$(UCC) $(INCLUDES) -I./ $(FLAGS) -std=gnu99 -c $< -o $@

clean:
	rm -rf ./build $(addsuffix .elf,$(PROGS))
