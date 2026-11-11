# The arcade disk's boot loader (raw binary for sectors 1..8). Built by
# a8dev/arcade.py through bin/a8-make.
build/boot.bin: boot.s boot.cfg
	mkdir -p build
	ca65 -l build/boot.lst -o build/boot.o boot.s
	ld65 -C boot.cfg -o $@ build/boot.o
clean:
	rm -rf build
