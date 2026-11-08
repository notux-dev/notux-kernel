#!/bin/bash
# Rung 4: sbase (suckless core utilities), static against notux's musl.
# Result: build-ladder/sbase-bin/<tool> for every utility; tools/mkext2test.sh puts them
# on the ext2 disk as /bin/* (mounted at /mnt/bin).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${1:-$ROOT/build-ladder/sbase-src}"
OUT="$ROOT/build-ladder/sbase-bin"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/michaelforney/sbase "$SRC"
cd "$SRC"
make -j"$(nproc)" CC="$ROOT/tools/ladder/mcc" AR=ar RANLIB=ranlib CFLAGS="-O1" LDFLAGS="-static" >/dev/null 2>&1 || \
  make CC="$ROOT/tools/ladder/mcc" AR=ar RANLIB=ranlib CFLAGS="-O1" LDFLAGS="-static" 2>&1 | tail -20
mkdir -p "$OUT"; rm -f "$OUT"/*
for f in *.c; do t="${f%.c}"; [ -x "$t" ] && cp "$t" "$OUT/$t"; done
strip "$OUT"/* 2>/dev/null || true
echo "ok: $(ls "$OUT" | wc -l) tools in $OUT ($(du -sh "$OUT" | cut -f1))"
