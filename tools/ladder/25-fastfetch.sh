#!/bin/bash
# Rung 25: fastfetch, static against notux's musl. Result: ./fastfetch.elf
# (the makefile then puts it on the disk as /bin/fastfetch).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${1:-$ROOT/build-ladder/fastfetch-src}"
B="$ROOT/build-ladder/fastfetch-build"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/fastfetch-cli/fastfetch "$SRC"
mkdir -p "$B" && cd "$B"
cmake "$SRC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$ROOT/tools/ladder/mcc" -DCMAKE_C_FLAGS=-O1 \
  -DBUILD_FLASHFETCH=OFF -DENABLE_LTO=OFF -DIS_MUSL=ON -DENABLE_THREADS=OFF -DENABLE_IMAGE_LOGO=OFF \
  -DENABLE_ZLIB=OFF -DENABLE_LUA=OFF -DENABLE_LIBZFS=OFF -DENABLE_WORDEXP=OFF -DINSTALL_LICENSE=OFF \
  -DENABLE_VULKAN=OFF -DENABLE_WAYLAND=OFF -DENABLE_XCB_RANDR=OFF -DENABLE_XRANDR=OFF -DENABLE_DRM=OFF \
  -DENABLE_VADRM=OFF -DENABLE_VAX11=OFF -DENABLE_VDPAU=OFF -DENABLE_GIO=OFF -DENABLE_DCONF=OFF \
  -DENABLE_EET=OFF -DENABLE_DBUS=OFF -DENABLE_SQLITE3=OFF -DENABLE_RPM=OFF -DENABLE_EGL=OFF \
  -DENABLE_GLX=OFF -DENABLE_OPENCL=OFF -DENABLE_ELF=OFF
make -j"$(nproc)" fastfetch
cp fastfetch "$ROOT/fastfetch.elf"
echo "ok: $ROOT/fastfetch.elf"
