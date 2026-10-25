
CC = gcc
CXX = g++
LD = ld
NASM = nasm
ZIG = zig
HOSTCC = gcc
QEMU = qemu-system-x86_64

BUILD = build

CFLAGS = -m64 -g -O2 -ffreestanding -fno-pie \
         -fno-stack-protector -fno-builtin \
         -nostdlib -nostdinc -mno-red-zone \
         -Ikernel -Ikernel/ahci -Ikernel/lapic \
         -Ikernel/IGPU -Ikernel/Xjail/xv6

# Flags for building a userspace ELF (user.c -> user.elf): still
# freestanding/no libc (there's no libc for notux yet), but linked as a
# normal static ET_EXEC at the fixed address onion.c's ELF loader
# expects user programs to run at.
#
# -fcf-protection=none: newer gcc/binutils auto-emit a
# .note.gnu.property section (Intel CET markers) that can overlap
# .text once it's linked at a fixed low address like -Ttext=0x400000,
# making ld fail with "section .note.gnu.property LMA overlaps section
# .text LMA". There's no CET support in this kernel to protect anyway.
USER_CFLAGS = -m64 -g -O0 -ffreestanding -fno-pie \
              -fno-stack-protector -fno-builtin -nostdlib -static \
              -fcf-protection=none
USER_LDFLAGS = -Wl,--entry=_start -Wl,-Ttext=0x400000 -Wl,--build-id=none

ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker.ld

CSRC = $(filter-out kernel/scheduler.c,$(wildcard kernel/*.c))
AHCI_CSRC = $(wildcard kernel/ahci/*.c)
LAPIC_CSRC = $(wildcard kernel/lapic/*.c)
FS_CSRC = $(wildcard kernel/xv6fs/*.c)
XV6_CSRC = $(wildcard kernel/Xjail/xv6/*.c)
IGPU_CSRC = $(wildcard kernel/IGPU/*.c)

ASM_SRC = boot/boot.asm $(wildcard kernel/*.asm)

SRC = $(CSRC) $(AHCI_CSRC) $(LAPIC_CSRC) \
      $(FS_CSRC) $(XV6_CSRC) $(IGPU_CSRC)

COBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC))
ASMOBJ = $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRC))
OBJ = $(COBJ) $(ASMOBJ)

.PHONY: all clean iso run run-disk

all: notux.bin

notux.bin: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

user.elf: user.c
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

child.elf: child.c
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

# Host-native tool, NOT cross-compiled for the kernel target -- it runs
# on your machine at build time, not inside notux.
tools/mkfs: tools/mkfs.c
	$(HOSTCC) -Wall -O2 -o $@ $<

# fs.img is a RAW DISK IMAGE formatted with notux's on-disk filesystem
# (see tools/mkfs.c for why). It is meant to be attached to QEMU as a
# real AHCI drive (see the `run-disk` target below) -- it is NOT a
# multiboot module, and no code path reads it that way: kernel/onion.c
# only ever mounts xv6fs from an actual AHCI device found by
# ahci_init()/ahci_get_device(0).
fs.img: tools/mkfs child.elf
	./tools/mkfs $@ child.elf

iso: notux.iso

notux.iso: notux.bin user.elf
	mkdir -p iso/boot/grub
	cp notux.bin iso/boot/kernel.bin
	cp user.elf iso/boot/user.elf
	printf '%s\n' \
	  'set timeout=0' \
	  'set default=0' \
	  'menuentry "notux" {' \
	  '    multiboot /boot/kernel.bin' \
	  '    module /boot/user.elf' \
	  '    boot' \
	  '}' > iso/boot/grub/grub.cfg
	grub-mkrescue -o notux.iso iso

# Quick smoke-test boot: no disk attached, so xv6fs will log "No SATA
# disks found" and the kernel carries on without a filesystem. Good
# for checking the kernel/process/scheduler side without needing
# fs.img built first.
run: notux.iso
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio` -no-reboot

# Full boot with the filesystem actually mounted: attaches fs.img as a
# real AHCI drive, matching what kernel/onion.c's AHCI driver expects
# (a raw LBA-addressed disk, no partition table). Rebuild fs.img first
# (`make fs.img`) after changing tools/mkfs.c or wanting a clean image.
run-disk: notux.iso fs.img
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio -no-reboot \
	  -device ahci,id=ahci0 \
	  -drive id=disk0,file=fs.img,if=none,format=raw \
	  -device ide-hd,drive=disk0,bus=ahci0.0

clean:
	rm -rf $(BUILD) notux.bin notux.iso iso tools/mkfs fs.img user.elf child.elf
