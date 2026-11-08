#!/bin/bash
# OpenRC (BSD-2-Clause, external) built dynamically against notux's musl (tools/ladder/mccdyn).
# libcap is replaced by tools/ladder/capstub (notux has no capabilities).
# Result: build-ladder/openrc-root/ (a DESTDIR tree); tools/mkext2root.sh copies it onto the root disk.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/build-ladder/openrc-src"
OUT="$ROOT/build-ladder/openrc-root"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/OpenRC/openrc "$SRC"
command -v meson >/dev/null || pip install meson >/dev/null 2>&1
cd "$SRC"
rm -rf capstub; cp -r "$ROOT/tools/ladder/capstub" capstub
sed -i "s|cap_dep = dependency('libcap', version: '>=2.33')|cap_dep = declare_dependency(include_directories: include_directories('capstub'), sources: files('capstub/capstub.c'))|" meson.build
B=$(mktemp -d)
cat > "$B/native.ini" <<INI
[binaries]
c = '$ROOT/tools/ladder/mccdyn'
ar = 'ar'
strip = 'strip'
pkg-config = 'false'
INI
meson setup "$B/b" --native-file "$B/native.ini" --prefix=/usr --sysconfdir=/etc --libexecdir=/lib --libdir=lib --buildtype=release \
  -Dpkgconfig=false -Dnewnet=false -Dsysvinit=false -Dpam=false -Daudit=disabled -Dselinux=disabled \
  -Dshell=/bin/sh -Dbranding="\"notux\"" >"$B/setup.log" 2>&1 || { tail -20 "$B/setup.log"; exit 1; }
ninja -C "$B/b" >"$B/ninja.log" 2>&1 || { grep -E "error|FAILED" -B2 -A6 "$B/ninja.log" | head -40; exit 1; }
rm -rf "$OUT"; DESTDIR="$OUT" meson install -C "$B/b" >/dev/null
rm -rf "$B"
echo "ok: $(du -sh "$OUT" | cut -f1) in $OUT"; ls "$OUT/sbin" "$OUT/usr/sbin" 2>/dev/null | head -30
