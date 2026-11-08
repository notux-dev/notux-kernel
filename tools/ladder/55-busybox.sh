#!/bin/bash
# busybox (GPL-2.0, external -- not part of the MIT tree), static against notux's musl, with only the applets
# OpenRC's boot scripts and an init need. Result: build-ladder/busybox-src/busybox.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/build-ladder/busybox-src"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/mirror/busybox "$SRC"
cd "$SRC"
make allnoconfig >/dev/null 2>&1
for c in STATIC INIT MOUNT UMOUNT HOSTNAME MKNOD MKFIFO SETSID HALT POWEROFF REBOOT RUN_PARTS SYNC LOGGER FLOCK DMESG \
         SWAPON SWAPOFF FEATURE_INIT_SYSLOG FEATURE_INIT_SCTTY FEATURE_USE_INITTAB FEATURE_MOUNT_FLAGS FEATURE_MOUNT_FSTAB \
         PS TOUCH SLEEP ID TEST ECHO TRUE FALSE KILL KILLALL STAT SEQ MKTEMP; do
  sed -i "s/^# CONFIG_$c is not set/CONFIG_$c=y/" .config
done
yes "" | make oldconfig >/dev/null 2>&1
make clean >/dev/null 2>&1 || true
make CC="$ROOT/tools/ladder/mcc" HOSTCC=gcc >/dev/null 2>&1
ls -la busybox
