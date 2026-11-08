#!/bin/bash
# Interactive vi check on a copy of ext2root.img (needs QEMU, debugfs). Types text, saves, and compares the file.
set -e
cd "$(dirname "$0")/.."
cp ext2root.img /tmp/vi_test.img
python3 tools/qemu_drive.py /tmp/vi_test.img wait:8 'send:vi /tmp/v.txt\r' wait:3 \
  'send:ifirst\rsecond\rthird\x1b' wait:1 'send:dd' wait:1 'send::wq\r' wait:3 >/dev/null
got=$(debugfs -R "cat /tmp/v.txt" /tmp/vi_test.img 2>/dev/null)
want=$'first\nsecond'
if [ "$got" = "$want" ]; then echo "vi: ok"; else echo "vi: FAIL (got: $got)"; exit 1; fi
