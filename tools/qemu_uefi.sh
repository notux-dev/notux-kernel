#!/bin/bash
# Boot notux.iso under UEFI (OVMF) in QEMU: tools/qemu_uefi.sh [disk.img]
# Needs: ovmf, grub-efi-amd64-bin (so that grub-mkrescue makes a hybrid BIOS+UEFI ISO), mtools, xorriso.
CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
VARS=$(mktemp); cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" "$VARS"
DISK=${1:-ext2root.img}
exec qemu-system-x86_64 -machine q35 -m 256 -serial stdio \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" -drive if=pflash,format=raw,file="$VARS" \
  -cdrom notux.iso -device ahci,id=ahci0 -drive id=d0,file="$DISK",if=none,format=raw -device ide-hd,drive=d0,bus=ahci0.0
