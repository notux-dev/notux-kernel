#!/bin/bash
# vi: toybox (0BSD licence, toys/pending/vi.c) built with ONLY vi enabled, static against notux's musl.
# Result: build-ladder/toybox-src/toybox, which tools/mkext2root.sh installs as /bin/vi (toybox picks its
# applet from argv[0]). Needs: git, gcc, make.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/build-ladder/toybox-src"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/landley/toybox "$SRC"
cd "$SRC"
make allnoconfig >/dev/null 2>&1
sed -i 's/^# CONFIG_VI is not set/CONFIG_VI=y/' .config
rm -f toybox
CROSS_COMPILE= CC="$ROOT/tools/ladder/mcc" HOSTCC=gcc make >/dev/null 2>&1
ls -la toybox
