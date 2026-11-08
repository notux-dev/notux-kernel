#!/bin/sh
# Build ext2test.img: a small ext2 volume (1 KiB blocks, no journal) used by tests/musl_ext2.c.
# Needs mke2fs from e2fsprogs (host only). usage: tools/mkext2test.sh [out.img]
set -e
OUT=${1:-ext2test.img}
T=${STAGE_ONLY:-$(mktemp -d)}   # STAGE_ONLY=dir: only populate dir (used by mkext2root.sh)
mkdir -p "$T/dir1/dir2" "$T/empty"
printf 'hello from ext2\n' > "$T/hello.txt"
printf 'long name works\n' > "$T/a_rather_long_file_name_over_fourteen.txt"
printf 'deep\n' > "$T/dir1/dir2/deep.txt"
python3 - "$T/big.bin" <<'PY'
import sys
n = 3 * 1024 * 1024 + 123          # reaches the double-indirect range with 1 KiB blocks
open(sys.argv[1], 'wb').write(bytes((i ^ (i >> 8) ^ (i >> 16)) & 0xff for i in range(n)))
PY
dd if=/dev/zero of="$T/sparse.bin" bs=1 count=1 seek=2097152 2>/dev/null   # 2 MiB hole + 1 byte
ln -s hello.txt "$T/link_rel"
ln -s /mnt/dir1/dir2/deep.txt "$T/link_abs"
ln -s dir1 "$T/link_dir"
ln -s nowhere "$T/dangling"
ln -s loop_b "$T/loop_a"; ln -s loop_a "$T/loop_b"
for i in $(seq 1 60); do echo "f$i" > "$T/empty/file_number_$i.txt"; done   # a directory with many entries
SIZE=8M
SB="$(dirname "$0")/../build-ladder/sbase-bin"          # sbase utilities (tools/ladder/04-sbase.sh), if built
if [ -d "$SB" ]; then mkdir -p "$T/bin"; cp "$SB"/* "$T/bin/"; SIZE=16M; cp "$(dirname "$0")/files/sbase_test.sh" "$T/sbase_test.sh"; fi
# dynamic linking: musl's libc.so is also the loader (ld-musl). /lib on this disk is shown at /lib.
LIBS="$(dirname "$0")/../musl/lib"
if [ -f "$LIBS/libc.so" ]; then
  mkdir -p "$T/lib"; cp "$LIBS/libc.so" "$T/lib/ld-musl-x86_64.so.1"; chmod 755 "$T/lib/ld-musl-x86_64.so.1"
  ln -s ld-musl-x86_64.so.1 "$T/lib/libc.musl-x86_64.so.1"
  [ -f "$(dirname "$0")/../libfoo.so" ] && cp "$(dirname "$0")/../libfoo.so" "$T/lib/libfoo.so"
  SIZE=16M
fi
[ -n "$STAGE_ONLY" ] && exit 0
rm -f "$OUT"
mke2fs -q -t ext2 -b 1024 -O ^has_journal -d "$T" "$OUT" $SIZE
rm -rf "$T"
e2fsck -fn "$OUT" >/dev/null && echo "$OUT ok"
