
CC = gcc
CXX = g++
LD = ld
NASM = nasm
ZIG = zig
HOSTCC = gcc
QEMU = qemu-system-x86_64

BUILD = build

ifdef TRACE
TRACE_FLAG = -DSYSCALL_TRACE
endif
CFLAGS = $(TRACE_FLAG) -m64 -g -O2 -ffreestanding -fno-pie \
         -fno-stack-protector -fno-builtin -mgeneral-regs-only \
         -nostdlib -nostdinc -mno-red-zone \
         -Ikernel -Ikernel/ahci -Ikernel/lapic \
         -Ikernel/IGPU -Ikernel/ext2 -Ikernel/Xjail/xv6 -Ikernel/syscalls

USER_CFLAGS = -m64 -g -O0 -ffreestanding -fno-pie \
              -fno-stack-protector -fno-builtin -nostdlib -static \
              -fcf-protection=none
USER_LDFLAGS = -Wl,--entry=_start -Wl,-Ttext=0x400000 -Wl,--build-id=none

ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker.ld

CSRC = $(filter-out kernel/scheduler.c,$(wildcard kernel/*.c))
AHCI_CSRC = $(wildcard kernel/ahci/*.c)
LAPIC_CSRC = $(wildcard kernel/lapic/*.c)
FS_CSRC = $(wildcard kernel/xv6fs/*.c) $(wildcard kernel/ext2/*.c)
XV6_CSRC = $(wildcard kernel/Xjail/xv6/*.c)
IGPU_CSRC = $(wildcard kernel/IGPU/*.c)
SYSCALLS_CSRC = $(wildcard kernel/syscalls/*.c)

ASM_SRC = boot/boot.asm $(wildcard kernel/*.asm) $(wildcard kernel/syscalls/*.asm)

SRC = $(CSRC) $(AHCI_CSRC) $(LAPIC_CSRC) \
      $(FS_CSRC) $(XV6_CSRC) $(IGPU_CSRC) $(SYSCALLS_CSRC)

COBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC))
ASMOBJ = $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRC))
OBJ = $(COBJ) $(ASMOBJ)

.PHONY: all clean iso run run-disk xvsx-tools xvsx-test

all: notux.bin

notux.bin: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(COBJ:.o=.d)

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

user.elf: user.c
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

child.elf: child.c
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

argtest.elf: argtest.c
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

tools/mkfs: tools/mkfs.c
	$(HOSTCC) -Wall -O2 -o $@ $<

MUSL_DIR = musl
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
GCCINC := $(shell $(CC) -print-file-name=include)
MUSL_CFLAGS = -O1 -static -no-pie -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
              -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib
MUSL_LINK = $(MUSL_DIR)/lib/crt1.o $(MUSL_DIR)/lib/crti.o
MUSL_LIBS = $(MUSL_DIR)/lib/libc.a $(LIBGCC) $(MUSL_DIR)/lib/crtn.o

DYN_PROGS = spie_hello dyn_hello dyn_lnk dyn_dl
MUSL_PROGS = musl_hello musl_fs musl_fd musl_time musl_io musl_sig musl_sig2 musl_proc musl_tty musl_ext2 musl_ext2w musl_mm init
FS_EXTRA = $(foreach p,$(MUSL_PROGS) $(DYN_PROGS),$(p)=$(p).elf)
FS_DEPS = $(foreach p,$(MUSL_PROGS) $(DYN_PROGS),$(p).elf)
FS_EXTRA += etc:os-release=tools/files/os-release etc:passwd=tools/files/passwd
FS_DEPS += tools/files/os-release tools/files/passwd
fs-shell.img: fs.img tools/files/interactive
	./tools/mkfs $@ child.elf argtest=argtest.elf $(FS_EXTRA) etc:interactive=tools/files/interactive
ifneq ($(wildcard fastfetch.elf),)
FS_EXTRA += fastfetch=fastfetch.elf
FS_DEPS += fastfetch.elf
endif
ifneq ($(wildcard mksh.elf),)
FS_EXTRA += mksh=mksh.elf
FS_DEPS += mksh.elf
endif

spie_%.elf: tests/spie_%.c
	$(CC) -O1 -fPIE -static-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -o $@ $(MUSL_DIR)/lib/rcrt1.o $(MUSL_DIR)/lib/crti.o $< $(MUSL_DIR)/lib/libc.a $(LIBGCC) $(MUSL_DIR)/lib/crtn.o
libfoo.so: tests/libfoo.c
	$(CC) -O1 -fPIC -shared -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -Wl,-soname,libfoo.so -o $@ $< -L$(MUSL_DIR)/lib -lc $(LIBGCC)
dyn_lnk.elf: tests/dyn_lnk.c libfoo.so
	$(CC) -O1 -fPIE -pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-rpath,/lib -Wl,--export-dynamic -o $@ $(MUSL_DIR)/lib/Scrt1.o $(MUSL_DIR)/lib/crti.o $< ./libfoo.so -L$(MUSL_DIR)/lib -lc $(LIBGCC) $(MUSL_DIR)/lib/crtn.o
dyn_dl.elf: tests/dyn_dl.c
	$(CC) -O1 -fPIE -pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-rpath,/lib -Wl,--export-dynamic -o $@ $(MUSL_DIR)/lib/Scrt1.o $(MUSL_DIR)/lib/crti.o $< -L$(MUSL_DIR)/lib -lc $(LIBGCC) $(MUSL_DIR)/lib/crtn.o
dyn_%.elf: tests/dyn_%.c
	$(CC) -O1 -fPIE -pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-rpath,/lib -o $@ $(MUSL_DIR)/lib/Scrt1.o $(MUSL_DIR)/lib/crti.o $< -L$(MUSL_DIR)/lib -lc $(LIBGCC) $(MUSL_DIR)/lib/crtn.o

musl_%.elf: tests/musl_%.c
	$(CC) $(MUSL_CFLAGS) -o $@ $(MUSL_LINK) $< $(MUSL_LIBS)

init.elf: init.c
	$(CC) $(MUSL_CFLAGS) -o $@ $(MUSL_LINK) $< $(MUSL_LIBS)

fs.img: tools/mkfs child.elf argtest.elf $(FS_DEPS)
	./tools/mkfs $@ child.elf argtest=argtest.elf $(FS_EXTRA)

ext2root.img: child.elf argtest.elf $(FS_DEPS) tools/files/interactive tools/mkext2root.sh tools/mkext2test.sh
	./tools/mkext2root.sh $@ child.elf argtest=argtest.elf $(FS_EXTRA) etc:interactive=tools/files/interactive
ext2root-tests.img: child.elf argtest.elf $(FS_DEPS) tools/mkext2root.sh tools/mkext2test.sh
	./tools/mkext2root.sh $@ child.elf argtest=argtest.elf $(FS_EXTRA)

iso: notux.iso

notux.iso: notux.bin user.elf
	mkdir -p iso/boot/grub
	cp notux.bin iso/boot/kernel.bin
	cp user.elf iso/boot/user.elf
	printf '%s\n' \
	  'insmod all_video' \
	  'set timeout=0' \
	  'set default=0' \
	  'menuentry "notux" {' \
	  '    multiboot /boot/kernel.bin' \
	  '    module /boot/user.elf' \
	  '    boot' \
	  '}' > iso/boot/grub/grub.cfg
	grub-mkrescue -o notux.iso iso

run: notux.iso
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio -display none -no-reboot

run-disk: notux.iso fs.img
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio -display none -no-reboot \
	  -device ahci,id=ahci0 \
	  -drive id=disk0,file=fs.img,if=none,format=raw \
	  -device ide-hd,drive=disk0,bus=ahci0.0

XVSX_SRC = tools/xvsx/xvsx_lib.c
XVSX_CFLAGS = -Wall -Wextra -O2 -Itools/xvsx -Ikernel/xvsxfs

tools/xvsx/mkfs.xvsx: tools/xvsx/mkfs.xvsx.c $(XVSX_SRC) tools/xvsx/xvsx_lib.h kernel/xvsxfs/xvsx_format.h
	$(HOSTCC) $(XVSX_CFLAGS) -o $@ $< $(XVSX_SRC)
tools/xvsx/fsck.xvsx: tools/xvsx/fsck.xvsx.c $(XVSX_SRC) tools/xvsx/xvsx_lib.h kernel/xvsxfs/xvsx_format.h
	$(HOSTCC) $(XVSX_CFLAGS) -o $@ $< $(XVSX_SRC)
tools/xvsx/xvsx-dump: tools/xvsx/xvsx-dump.c $(XVSX_SRC) tools/xvsx/xvsx_lib.h kernel/xvsxfs/xvsx_format.h
	$(HOSTCC) $(XVSX_CFLAGS) -o $@ $< $(XVSX_SRC)

xvsx-tools: tools/xvsx/mkfs.xvsx tools/xvsx/fsck.xvsx tools/xvsx/xvsx-dump

xvsx-test:
	python3 tests/xvsx_test.py

clean:
	rm -f tools/xvsx/mkfs.xvsx tools/xvsx/fsck.xvsx tools/xvsx/xvsx-dump
	rm -rf $(BUILD) notux.bin notux.iso iso tools/mkfs fs.img user.elf child.elf argtest.elf musl_hello.elf musl_fs.elf musl_fd.elf musl_time.elf init.elf
