FILES=./build/start.o ./build/samos.asm.o ./build/samos.o ./build/stdlib.o ./build/stdio.o ./build/string.o ./build/memory.o
INCLUDES=-I./src
FLAGS=-g -ffreestanding

all: $(FILES)
	i686-elf-ld -m elf_i386 -relocatable $(FILES) -o ./stdlib.elf

./build/start.o: ./src/start.asm
	mkdir -p build
	nasm -f elf ./src/start.asm -o ./build/start.o

./build/samos.asm.o: ./src/samos.asm
	mkdir -p build
	nasm -f elf ./src/samos.asm -o ./build/samos.asm.o

./build/samos.o: ./src/samos.c
	mkdir -p build
	i686-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/samos.c -o ./build/samos.o

./build/stdlib.o: ./src/stdlib.c
	mkdir -p build
	i686-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/stdlib.c -o ./build/stdlib.o

./build/stdio.o: ./src/stdio.c
	mkdir -p build
	i686-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/stdio.c -o ./build/stdio.o

./build/string.o: ./src/string.c
	mkdir -p build
	i686-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/string.c -o ./build/string.o

./build/memory.o: ./src/memory.c
	mkdir -p build
	i686-elf-gcc $(INCLUDES) $(FLAGS) -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -Wall -O0 -std=gnu99 -c ./src/memory.c -o ./build/memory.o

clean:
	rm -rf $(FILES) ./stdlib.elf
