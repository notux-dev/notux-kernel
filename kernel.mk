ZIG  := zig
FASM := /home/patison/Downloads/fasm/fasm
LD   := ld

SRC_BOOT_DIR   := src/boot
SRC_KERNEL_DIR := src/kernel
BUILD_DIR      := build

KERNEL_ELF := $(BUILD_DIR)/kernel.elf

ASM_SOURCES := $(wildcard $(SRC_BOOT_DIR)/*.asm)
ASM_OBJECTS := $(patsubst $(SRC_BOOT_DIR)/%.asm,$(BUILD_DIR)/boot/%.o,$(ASM_SOURCES))

ZIG_SOURCES := $(wildcard $(SRC_KERNEL_DIR)/*.zig)
ZIG_OBJECT  := $(BUILD_DIR)/kernel_zig.o

.PHONY: all clean

all: $(KERNEL_ELF)

$(BUILD_DIR)/boot/%.o: $(SRC_BOOT_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(FASM) $< $@

$(ZIG_OBJECT): $(ZIG_SOURCES)
	@mkdir -p $(BUILD_DIR)
	$(ZIG) build-obj $(SRC_KERNEL_DIR)/main.zig \
		-target x86_64-freestanding-none \
		-mcpu=x86_64-sse-sse2-mmx \
		-O ReleaseSmall \
		-femit-bin=$@

$(KERNEL_ELF): $(ASM_OBJECTS) $(ZIG_OBJECT) linker.ld
	$(LD) -n -T linker.ld -o $(KERNEL_ELF) $(ASM_OBJECTS) $(ZIG_OBJECT)

clean:
	rm -rf $(BUILD_DIR)