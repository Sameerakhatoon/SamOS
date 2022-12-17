all: blank.bin blank.elf

blank.bin: build/blank.o
	i686-elf-gcc -g -T ./linker.ld -o ./blank.bin -ffreestanding -O0 -nostdlib -fpic -g ./build/blank.o

blank.elf: build/blank.o
	i686-elf-gcc -g -T ./linker-elf.ld -o ./blank.elf -ffreestanding -O0 -nostdlib -fpic -g ./build/blank.o

build/blank.o: blank.asm
	mkdir -p build
	nasm -f elf ./blank.asm -o ./build/blank.o

clean:
	rm -rf ./build/blank.o ./blank.bin ./blank.elf
