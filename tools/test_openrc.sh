#!/bin/bash
# Boots notux with BusyBox init -> OpenRC from an ext2 root and checks the service ran.
# Needs build-ladder/{busybox-src,openrc-root} (tools/ladder/55-busybox.sh, 60-openrc.sh).
set -e
cd "$(dirname "$0")/.."
INIT=1 make -B ext2root.img >/dev/null
cp ext2root.img /tmp/notux-openrc.img
OUT=$(python3 tools/qemu_drive.py /tmp/notux-openrc.img -- wait:50 'send:cat /tmp/hello.state\r' wait:3 'send:kill -TERM 1\r' wait:8 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
echo "$OUT" | grep -q '^started' && echo "openrc: service started ok" || { echo "openrc: FAIL (service)"; exit 1; }
echo "$OUT" | grep -q 'Requesting system reboot' && echo "openrc: clean shutdown ok" || { echo "openrc: FAIL (shutdown)"; exit 1; }
rm -f /tmp/notux-openrc.img; make -B ext2root.img >/dev/null   # restore the non-init image
