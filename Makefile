C_SRC := $(shell find src/ -type f -name "*.c") # Change to find later
ASM_SRC := $(shell find src/ -type f -name "*.asm")
S_SRC := $(shell find src/ -type f -name "*.S")
C_OBJ := $(patsubst src/%.c, build/%.o, $(C_SRC))
S_OBJ := $(patsubst src/%.S, build/%.o, $(S_SRC))
ASM_OBJ := $(patsubst src/%.asm, build/%.o, $(ASM_SRC))
FONT_OBJ := build/fonts/default.o
CFLAGS := -ffreestanding \
		-I include/ \
        -O2 \
        -Wall \
        -Wextra \
        -mno-red-zone \
        -mno-sse \
        -mcmodel=large \
		-Wall \
		-Wextra

QEMU_FLAGS := -smp 2 \
			-m 2G \
			-machine q35 \
			-device ahci,id=ahci \
			-drive id=disk,file=disk.img,if=none,format=raw \
			-device ide-hd,drive=disk,bus=ahci.0

ASMFLAGS := -felf64
LDFLAGS := -n

all: build/os.iso
.PHONY = run debug-run clean

run: LDFLAGS += -s
run: build/os.iso
	qemu-system-x86_64 $(QEMU_FLAGS) -cdrom $^

debug-run: debug
	qemu-system-x86_64 $(QEMU_FLAGS) -cdrom build/os.iso -s -S

debug: CFLAGS += -g
debug: ASMFLAGS += -g -F dwarf
debug: build/os.iso

build/os.iso: build/kernel.bin grub.cfg
	mkdir -p build/isofiles/boot/grub
	cp grub.cfg build/isofiles/boot/grub
	cp build/kernel.bin build/isofiles/boot
	grub-mkrescue -o build/os.iso build/isofiles

build/kernel.bin: $(ASM_OBJ) $(C_OBJ) $(S_OBJ) $(FONT_OBJ)
	ld $(LDFLAGS) -o $@ -T linker.ld $^

build/%.o: src/%.asm
	mkdir -p $(@D)
	nasm $(ASMFLAGS) $< -o $@

build/%.o: src/%.c
	mkdir -p $(@D)
	x86_64-elf-gcc $(CFLAGS) -c $< -o $@

build/%.o: %.psf
	mkdir -p $(@D)
	objcopy -O elf64-x86-64 -B i386 -I binary $< $@

clean:
	rm -rf build/
