CC = gcc
LD = ld

CFLAGS = -m64 -g -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
         -nostdlib -nostdinc \
         -Ikernel \
         -Ikernel/ahci \
         -Ikernel/lapic \
         -Ikernel/xv6fs \
         -Ikernel/Xjail/xv6 \
         -Ikernel/IGPU \
         -mno-red-zone

ASFLAGS = -f elf64

LDFLAGS = -m elf_x86_64 -T linker.ld

CSRC = $(filter-out kernel/scheduler.c,$(wildcard kernel/*.c))

AHCI_CSRC  = $(wildcard kernel/ahci/*.c)
LAPIC_CSRC = $(wildcard kernel/lapic/*.c)
FS_CSRC    = $(wildcard kernel/xv6fs/*.c)
XV6_CSRC   = $(wildcard kernel/Xjail/xv6/*.c)
IGPU_CSRC  = $(wildcard kernel/IGPU/*.c)

COBJ      = $(CSRC:kernel/%.c=%.o)
AHCI_OBJ  = $(AHCI_CSRC:kernel/ahci/%.c=%.o)
LAPIC_OBJ = $(LAPIC_CSRC:kernel/lapic/%.c=%.o)
FS_OBJ    = $(FS_CSRC:kernel/xv6fs/%.c=%.o)
XV6_OBJ   = $(XV6_CSRC:kernel/Xjail/xv6/%.c=%.o)
IGPU_OBJ  = $(IGPU_CSRC:kernel/IGPU/%.c=%.o)

ASM_BOOT_OBJ = boot.o

ASM_KERNEL_SRC = $(wildcard kernel/*.asm)
ASM_KERNEL_OBJ = $(ASM_KERNEL_SRC:kernel/%.asm=%.o)

OBJ = $(ASM_BOOT_OBJ) $(COBJ) $(AHCI_OBJ) $(LAPIC_OBJ) $(FS_OBJ) $(XV6_OBJ) $(IGPU_OBJ) $(ASM_KERNEL_OBJ)

all: notux.iso

fs.o: kernel/xv6fs/fs.c
	$(CC) $(CFLAGS) -Diinit=xv6fs_iinit -c $< -o $@

%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: kernel/ahci/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: kernel/lapic/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: kernel/xv6fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: kernel/Xjail/xv6/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: kernel/IGPU/%.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: boot/boot.asm
	nasm $(ASFLAGS) boot/boot.asm -o boot.o

%.o: kernel/%.asm
	nasm $(ASFLAGS) $< -o $@

notux.bin: $(OBJ)
	$(LD) $(LDFLAGS) -o notux.bin $(OBJ)

notux.iso: notux.bin user.elf fs.img
	mkdir -p iso/boot/grub
	cp notux.bin iso/boot/kernel.bin
	cp user.elf iso/boot/user.elf
	cp fs.img iso/boot/fs.img

	@echo 'set timeout=0' > iso/boot/grub/grub.cfg
	@echo 'set default=0' >> iso/boot/grub/grub.cfg
	@echo 'menuentry "notux" {' >> iso/boot/grub/grub.cfg
	@echo '    multiboot /boot/kernel.bin' >> iso/boot/grub/grub.cfg
	@echo '    module /boot/user.elf' >> iso/boot/grub/grub.cfg
	@echo '    module /boot/fs.img' >> iso/boot/grub/grub.cfg
	@echo '    boot' >> iso/boot/grub/grub.cfg
	@echo '}' >> iso/boot/grub/grub.cfg

	grub-mkrescue -o notux.iso iso

fs.img:
	@test -f fs.img || (echo "ERROR: fs.img not found. Build it with xv6's mkfs first." && exit 1)

run: notux.iso
	qemu-system-x86_64 notux.iso -serial stdio -no-reboot

clean:
	rm -f *.o *.elf notux.bin notux.iso notsh_bin.h
	rm -rf iso

notsh.elf: notsh.c
	gcc -m64 -ffreestanding -nostdlib -fno-builtin \
	    -fno-stack-protector -mno-red-zone -fcf-protection=none \
	    -c notsh.c -o notsh.o
	ld -m elf_x86_64 -nostdlib -e _start \
	   -Ttext 0x400000 notsh.o -o notsh.elf

notsh_bin.h: notsh.elf tool/bin2h.sh
	./tool/bin2h.sh notsh.elf notsh_elf > notsh_bin.h

user.elf: user.c notsh_bin.h
	gcc -m64 -ffreestanding -nostdlib -fno-builtin \
	    -fno-stack-protector -mno-red-zone -fcf-protection=none \
	    -c user.c -o user.o
	ld -m elf_x86_64 -nostdlib -e _start \
	   -Ttext 0x300000 user.o -o user.elf

.PHONY: all run clean