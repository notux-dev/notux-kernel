
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
FS_CSRC = $(wildcard kernel/xv6fs/*.c) $(wildcard kernel/ext2/*.c)
XV6_CSRC = $(wildcard kernel/Xjail/xv6/*.c)
IGPU_CSRC = $(wildcard kernel/IGPU/*.c)
SYSCALLS_CSRC = $(wildcard kernel/syscalls/*.c)
NET_CSRC = $(wildcard kernel/net/*.c)

ASM_SRC = boot/boot.asm $(wildcard kernel/*.asm) $(wildcard kernel/syscalls/*.asm)

SRC = $(CSRC) $(AHCI_CSRC) $(LAPIC_CSRC) \
      $(FS_CSRC) $(XV6_CSRC) $(IGPU_CSRC) $(SYSCALLS_CSRC) $(NET_CSRC)

COBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC))
ASMOBJ = $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRC))
OBJ = $(COBJ) $(ASMOBJ)

.PHONY: all clean iso run run-disk xvsx-tools xvsx-test

all: notux.bin

notux.bin: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

# -MMD/-MP: track header dependencies. Without this, changing a struct in a
# header (process_t, say) left stale objects built with the OLD layout.
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
# musl: prebuilt libc.a + crt*.o + headers live in musl/ (committed, nothing to
# install or build). Programs are compiled with the host gcc against them:
#   gcc -static -nostdlib crt1.o crti.o prog.c libc.a libgcc.a crtn.o
MUSL_DIR = musl
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
GCCINC := $(shell $(CC) -print-file-name=include)
MUSL_CFLAGS = -O1 -static -no-pie -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
              -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib
MUSL_LINK = $(MUSL_DIR)/lib/crt1.o $(MUSL_DIR)/lib/crti.o
MUSL_LIBS = $(MUSL_DIR)/lib/libc.a $(LIBGCC) $(MUSL_DIR)/lib/crtn.o

DYN_PROGS = spie_hello dyn_hello dyn_lnk dyn_dl
MUSL_PROGS = musl_hello musl_fs musl_fd musl_time musl_io musl_sig musl_sig2 musl_proc musl_tty musl_ext2 musl_ext2w musl_mm musl_bigmem musl_thread musl_unix musl_shm musl_inet musl_epoll musl_perm musl_curl musl_apk musl_evdev musl_bigdisk musl_mknod musl_cow musl_lazy musl_gcc musl_py musl_x11 musl_fb musl_vt musl_pty musl_ptyg musl_xorg musl_chroot init
FS_EXTRA = $(foreach p,$(MUSL_PROGS) $(DYN_PROGS),$(p)=$(p).elf)
FS_DEPS = $(foreach p,$(MUSL_PROGS) $(DYN_PROGS),$(p).elf)
FS_EXTRA += etc:os-release=tools/files/os-release etc:passwd=tools/files/passwd
FS_DEPS += tools/files/os-release tools/files/passwd
# make fs-shell.img: same disk plus /etc/interactive -> boots straight into mksh on the terminal
fs-shell.img: fs.img tools/files/interactive
	./tools/mkfs $@ child.elf argtest=argtest.elf $(FS_EXTRA) etc:interactive=tools/files/interactive
# optional: fastfetch.elf (see docs/COMPAT_LADDER.md rung 25) goes to /bin/fastfetch
ifneq ($(wildcard fastfetch.elf),)
FS_EXTRA += fastfetch=fastfetch.elf
FS_DEPS += fastfetch.elf
endif
# optional: doom.elf (tools/ladder/26-doom.sh) goes to /bin/doom
ifneq ($(wildcard doom.elf),)
FS_EXTRA += doom=doom.elf
FS_DEPS += doom.elf
endif
# optional: drop a static musl build of mksh/oksh next to this makefile as mksh.elf
ifneq ($(wildcard mksh.elf),)
FS_EXTRA += mksh=mksh.elf
FS_DEPS += mksh.elf
endif

# static PIE (ET_DYN without PT_INTERP) and dynamically linked programs
spie_%.elf: tests/spie_%.c
	$(CC) -O1 -fPIE -static-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $(MUSL_DIR)/include -isystem $(GCCINC) -nostdlib -o $@ $(MUSL_DIR)/lib/rcrt1.o $(MUSL_DIR)/lib/crti.o $< $(MUSL_DIR)/lib/libc.a $(LIBGCC) $(MUSL_DIR)/lib/crtn.o
# libfoo.so is NOT put on fs.img: it goes into /lib of the ext2 disk (tools/mkext2test.sh)
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

# your own programs: add NAME to MUSL_PROGS and write NAME.c in the project root
init.elf: init.c
	$(CC) $(MUSL_CFLAGS) -o $@ $(MUSL_LINK) $< $(MUSL_LIBS)

fs.img: tools/mkfs child.elf argtest.elf $(FS_DEPS)
	./tools/mkfs $@ child.elf argtest=argtest.elf $(FS_EXTRA)

# one-disk setup: ext2 root with everything (needs mke2fs/debugfs on the host)
# a 20 MiB test image (data streamed at exec, huge .bss): too big for the small xv6fs disk, ext2 only
EXT2_ONLY = spie_bigelf=spie_bigelf.elf
ext2root.img: spie_bigelf.elf child.elf argtest.elf $(FS_DEPS) tools/files/interactive tools/mkext2root.sh tools/mkext2test.sh
	SIZE=$${SIZE:-4G} ./tools/mkext2root.sh $@ child.elf argtest=argtest.elf $(FS_EXTRA) $(EXT2_ONLY) etc:interactive=tools/files/interactive
ext2root-tests.img: spie_bigelf.elf child.elf argtest.elf $(FS_DEPS) tools/mkext2root.sh tools/mkext2test.sh
	./tools/mkext2root.sh $@ child.elf argtest=argtest.elf $(FS_EXTRA) $(EXT2_ONLY)

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
	  '    if [ "$$grub_platform" = efi ]; then' \
	  '        multiboot2 /boot/kernel.bin' \
	  '        module2 /boot/user.elf' \
	  '    else' \
	  '        multiboot /boot/kernel.bin' \
	  '        module /boot/user.elf' \
	  '    fi' \
	  '    boot' \
	  '}' > iso/boot/grub/grub.cfg
	grub-mkrescue -o notux.iso iso

# Quick smoke-test boot: no disk attached, so xv6fs will log "No SATA
# disks found" and the kernel carries on without a filesystem. Good
# for checking the kernel/process/scheduler side without needing
# fs.img built first.
run: notux.iso
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio -display none -no-reboot

# Full boot with the filesystem actually mounted: attaches fs.img as a
# real AHCI drive, matching what kernel/onion.c's AHCI driver expects
# (a raw LBA-addressed disk, no partition table). Rebuild fs.img first
# (`make fs.img`) after changing tools/mkfs.c or wanting a clean image.
run-disk: notux.iso fs.img
	$(QEMU) -cdrom notux.iso -m 256 -serial stdio -display none -no-reboot \
	  -device ahci,id=ahci0 \
	  -drive id=disk0,file=fs.img,if=none,format=raw \
	  -device ide-hd,drive=disk0,bus=ahci0.0

# Bootable hard-disk image (MBR + GRUB + ext2 partition), no ISO needed: tools/mkbootdisk.sh
bootdisk.img: notux.bin user.elf ext2root.img tools/mkbootdisk.sh
	tools/mkbootdisk.sh ext2root.img $@

# xvsXfs host tools (see docs/XVSXFS.md)
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
