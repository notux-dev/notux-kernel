#!/bin/bash
# Build a bootable ext2 ROOT disk for notux (one disk instead of fs.img + ext2test.img).
# usage: tools/mkext2root.sh out.img child.elf [name=file ...] [etc:name=file ...]
# The arguments after out.img are the same as for tools/mkfs: child.elf goes to /bin/child,
# name=file to /bin/name, etc:name=file to /etc/name. Layout: FHS directories, /dev nodes,
# /bin (+ sbase if built), /lib (musl loader), and the ext2 test fixtures under /mnt
# (so tests/musl_ext2*.c and sbase_test.sh see the same paths as with the two-disk setup).
# Needs mke2fs and debugfs (e2fsprogs), host only.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"; CHILD="$2"; shift 2
R=$(mktemp -d); F=$(mktemp -d)
mkdir -p "$R"/{bin,etc,root,dev,proc,sys,tmp,run,lib,usr/bin,usr/lib,usr/include,home,mnt,var/tmp,var/log,var/lib,var/cache}
chmod 1777 "$R/tmp" "$R/var/tmp"
cat > "$R/README" <<'TXT'
Welcome to notux.

This is an ext2 root filesystem (tools/mkext2root.sh).
Layout:
  /bin   executables
  /etc   configuration
  /lib   dynamic loader and libraries
  /root  root's home directory
  /mnt   ext2 test fixtures
TXT
cp "$CHILD" "$R/bin/child"
for a in "$@"; do
  nm="${a%%=*}"; f="${a#*=}"
  case "$nm" in
    etc:*) cp "$f" "$R/etc/${nm#etc:}" ;;
    *)     cp "$f" "$R/bin/$nm" ;;
  esac
done
[ -f "$R/bin/mksh" ] && ln -sf mksh "$R/bin/sh"
chmod 755 "$R"/bin/* 2>/dev/null || true
# tcc (tools/ladder/31-tcc.sh) and its headers/libs under /usr
TR="$HERE/../build-ladder/tcc-root"
[ -d "$TR" ] && cp -a "$TR"/. "$R/"
[ -f "$HERE/files/tcc_test.sh" ] && cp "$HERE/files/tcc_test.sh" "$R/root/tcc_test.sh"
# busybox (tools/ladder/55-busybox.sh, GPL, external): init, mount, hostname, ...; applets that sbase/others already provide are not shadowed
BB="$HERE/../build-ladder/busybox-src/busybox"
if [ -x "$BB" ]; then
  cp "$BB" "$R/bin/busybox"
  for a in init mount umount hostname mknod mkfifo setsid halt poweroff reboot run-parts logger flock dmesg swapon swapoff ps killall; do
    [ -e "$R/bin/$a" ] || ln -s busybox "$R/bin/$a"
  done
  mkdir -p "$R/sbin"; for a in init halt poweroff reboot swapon swapoff; do ln -sf ../bin/busybox "$R/sbin/$a"; done
fi
# OpenRC (tools/ladder/60-openrc.sh, BSD-2-Clause, external)
OR="$HERE/../build-ladder/openrc-root"
[ -d "$OR" ] && cp -a "$OR"/. "$R/"
[ -d "$R/etc/init.d" ] && install -m 755 "$HERE/files/hello.initd" "$R/etc/init.d/hello"
# notux boot configuration: no hardware-probing services (no sysfs/devfs/cgroups here);
# BusyBox init runs OpenRC only when INIT=1 (an /etc/inittab marks the root as bootable by init).
if [ -d "$R/etc/runlevels" ]; then
  rm -rf "$R/etc/runlevels"; mkdir -p "$R/etc/runlevels"/{sysinit,boot,default,shutdown}
  ln -s /etc/init.d/hello "$R/etc/runlevels/default/hello"
  mkdir -p "$R/run/openrc"
fi
if [ -n "$INIT" ]; then
  rm -f "$R/etc/interactive"
  cat > "$R/etc/inittab" <<'TAB'
::sysinit:/sbin/openrc sysinit
::sysinit:/sbin/openrc boot
::wait:/sbin/openrc default
::respawn:-/bin/mksh
::shutdown:/sbin/openrc shutdown
TAB
fi
[ -e "$R/etc/passwd" ] || printf 'root:x:0:0:root:/root:/bin/mksh\n' > "$R/etc/passwd"
[ -e "$R/etc/group" ] || printf 'root:x:0:\nuucp:x:10:\ntty:x:5:\nwheel:x:10:\n' > "$R/etc/group"
# vi: toybox built with only vi (tools/ladder/50-vi.sh, 0BSD)
[ -x "$HERE/../build-ladder/toybox-src/toybox" ] && cp "$HERE/../build-ladder/toybox-src/toybox" "$R/bin/vi"
# fixtures + sbase + loader
STAGE_ONLY="$F" bash "$HERE/mkext2test.sh" >/dev/null
[ -d "$F/bin" ] && { cp --remove-destination "$F"/bin/* "$R/bin/"; rm -rf "$F/bin"; }
[ -d "$F/lib" ] && { cp -a "$F"/lib/. "$R/lib/"; rm -rf "$F/lib"; }
cp -a "$F"/. "$R/mnt/"
SIZE=${SIZE:-32M}
rm -f "$OUT"
mke2fs -q -t ext2 -b 1024 -O ^has_journal -d "$R" "$OUT" $SIZE
# character devices: major/minor are Linux's
DC=$(mktemp)
cat > "$DC" <<DBG
cd dev
mknod null c 1 3
mknod zero c 1 5
mknod random c 1 8
mknod urandom c 1 9
mknod tty c 5 0
mknod console c 5 1
mknod tty0 c 4 0
mknod ttyS0 c 4 64
sif null mode 020666
sif zero mode 020666
sif random mode 020666
sif urandom mode 020666
sif tty mode 020666
sif console mode 020620
sif tty0 mode 020620
sif ttyS0 mode 020660
DBG
debugfs -w -f "$DC" "$OUT" >/dev/null
rm -f "$DC"
rm -rf "$R" "$F"
e2fsck -fn "$OUT" >/dev/null && echo "$OUT ok ($SIZE ext2 root)"
