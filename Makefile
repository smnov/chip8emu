build:
	gcc -v ./src/*.c -l SDL2 -o chip8emu
run:
	./chip8emu
clean:
	rm chip8emu
