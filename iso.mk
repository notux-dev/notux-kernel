GRUB_MKRESCUE := grub-mkrescue

BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/iso
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO_IMAGE  := $(BUILD_DIR)/mykernel.iso

.PHONY: all iso run clean kernel

all: iso

kernel:
	$(MAKE) -f kernel.mk

$(KERNEL_ELF): kernel

USER_ELF := build/user/hello.elf

$(USER_ELF): userspace

userspace:
	$(MAKE) -f user.mk

iso: $(ISO_IMAGE)

$(ISO_IMAGE): $(KERNEL_ELF) $(USER_ELF) grub.cfg
	@mkdir -p $(ISO_DIR)/boot/grub
	@mkdir -p $(ISO_DIR)/boot/user
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp $(USER_ELF) $(ISO_DIR)/boot/user/hello.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $(ISO_IMAGE) $(ISO_DIR)

run: $(ISO_IMAGE)
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -serial stdio

clean:
	rm -rf $(BUILD_DIR)