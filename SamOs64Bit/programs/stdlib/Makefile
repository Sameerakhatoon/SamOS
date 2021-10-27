FILES=./build/start.asm.o ./build/start.o ./build/samos.asm.o ./build/samos.o ./build/stdlib.o ./build/stdio.o ./build/string.o ./build/memory.o
INCLUDES=-I./src
FLAGS=-g -ffreestanding

all: $(FILES)
	x86_64-elf-ld -m elf_x86_64 -relocatable $(FILES) -o ./stdlib.elf

./build/start.asm.o: ./src/start.asm
	mkdir -p build
	nasm -f elf64 ./src/start.asm -o ./build/start.asm.o

./build/start.o: ./src/start.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/start.c -o ./build/start.o

./build/samos.asm.o: ./src/samos.asm
	mkdir -p build
	nasm -f elf64 ./src/samos.asm -o ./build/samos.asm.o

./build/samos.o: ./src/samos.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/samos.c -o ./build/samos.o

./build/stdlib.o: ./src/stdlib.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/stdlib.c -o ./build/stdlib.o

./build/stdio.o: ./src/stdio.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/stdio.c -o ./build/stdio.o

./build/string.o: ./src/string.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/string.c -o ./build/string.o

./build/memory.o: ./src/memory.c
	mkdir -p build
	x86_64-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/memory.c -o ./build/memory.o

clean:
	rm -rf $(FILES) ./stdlib.elf
