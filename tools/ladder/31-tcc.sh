#!/bin/bash
# Rung 31: TinyCC (LGPL, external -- not part of the MIT kernel tree), static against notux's musl.
# Result: build-ladder/tcc-root/{usr/bin/tcc, usr/lib/tcc/{include,libtcc1.a}, usr/include, usr/lib/*.o,libc.a,libc.so}
# tools/mkext2root.sh copies build-ladder/tcc-root onto the ext2 root disk.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${1:-$ROOT/build-ladder/tinycc-src}"
OUT="$ROOT/build-ladder/tcc-root"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/TinyCC/tinycc "$SRC"
cd "$SRC"
./configure --prefix=/usr --cc="$ROOT/tools/ladder/mccdyn" --extra-cflags=-O1 --config-musl \
  --sysincludepaths=/usr/include:/usr/lib/tcc/include --libpaths=/usr/lib:/usr/lib/tcc --crtprefix=/usr/lib \
  --tccdir=/usr/lib/tcc --elfinterp=/lib/ld-musl-x86_64.so.1 >/dev/null
make clean >/dev/null 2>&1 || true
make -j"$(nproc)" tcc >/dev/null
# libtcc1.a: built with the fresh tcc against musl headers (the stock rule would use the host's glibc headers)
T=$(mktemp -d)
for f in libtcc1.c stdatomic.c builtin.c dsohandle.c alloca.S atomic.S; do
  [ -f lib/$f ] && ./tcc -B. -nostdinc -Iinclude -I"$ROOT/musl/include" -I"$ROOT/build-ladder/kinc" -c lib/$f -o "$T/${f%.*}.o"
done
ar rcs libtcc1.a "$T"/*.o
# standalone objects tcc links in by name (-run, -bt)
for f in runmain.c bt-exe.c bt-log.c; do
  [ -f lib/$f ] && ./tcc -B. -nostdinc -Iinclude -I"$ROOT/musl/include" -I"$ROOT/build-ladder/kinc" -c lib/$f -o "$T/x_${f%.c}.o"
done
rm -f "$T"/libtcc1.o.keep
rm -rf "$OUT"; mkdir -p "$OUT/usr/bin" "$OUT/usr/lib/tcc/include" "$OUT/usr/include"
cp tcc "$OUT/usr/bin/tcc"; strip "$OUT/usr/bin/tcc" 2>/dev/null || true
cp include/*.h "$OUT/usr/lib/tcc/include/"; cp libtcc1.a "$OUT/usr/lib/tcc/libtcc1.a"
for f in runmain bt-exe bt-log; do [ -f "$T/x_$f.o" ] && cp "$T/x_$f.o" "$OUT/usr/lib/tcc/$f.o"; done; rm -rf "$T"
cp -a "$ROOT"/musl/include/. "$OUT/usr/include/"
for f in crt1.o crti.o crtn.o Scrt1.o rcrt1.o libc.a libm.a libc.so; do cp "$ROOT/musl/lib/$f" "$OUT/usr/lib/" 2>/dev/null || true; done
# C programs ask for <linux/*.h>/<asm/*.h> only rarely; the musl headers are self-contained
echo "ok: $(du -sh "$OUT" | cut -f1) in $OUT"
