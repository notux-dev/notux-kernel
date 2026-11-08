#!/usr/bin/env python3
"""xvsXfs host tool tests: round-trip fidelity, fsck on clean images, and
fsck's ability to notice deliberate corruption of every kind of metadata.

    python3 tests/xvsx_test.py            (builds the tools into a temp dir)
"""
import os, random, re, shutil, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools", "xvsx")
FMT = os.path.join(ROOT, "kernel", "xvsxfs")
work = tempfile.mkdtemp(prefix="xvsx_test_")
passed = failed = 0


def build():
    for name, src in (("mkfs.xvsx", "mkfs.xvsx.c"), ("fsck.xvsx", "fsck.xvsx.c"),
                      ("xvsx-dump", "xvsx-dump.c")):
        subprocess.check_call(["gcc", "-Wall", "-Wextra", "-O2", "-I" + TOOLS,
                               "-I" + FMT, "-o", os.path.join(work, name),
                               os.path.join(TOOLS, src), os.path.join(TOOLS, "xvsx_lib.c")])


def run(*args, **kw):
    return subprocess.run(args, capture_output=True, **kw)


def tool(name):
    return os.path.join(work, name)


def check(cond, what):
    global passed, failed
    if cond:
        passed += 1
        print("  ok    " + what)
    else:
        failed += 1
        print("  FAIL  " + what)


# ---- crc32c in Python, to forge "valid" metadata for logic-level tests
def crc32c(data, seed=0):
    tab = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
        tab.append(c)
    c = ~seed & 0xFFFFFFFF
    for b in data:
        c = tab[(c ^ b) & 0xFF] ^ (c >> 8)
    return ~c & 0xFFFFFFFF


def super_info(img):
    out = run(tool("xvsx-dump"), img, "super").stdout.decode()
    g = lambda pat: int(re.search(pat, out).group(1))
    return dict(itable=g(r"inode table\s+block (\d+)"), ibm=g(r"inode bitmap block (\d+)"),
                bbm=g(r"block bitmap block (\d+)"), data=g(r"data starts\s+block (\d+)"))


def stat_info(img, path):
    out = run(tool("xvsx-dump"), img, "stat", path).stdout.decode()
    ino = int(re.search(r"inode (\d+)", out).group(1))
    exts = [tuple(map(int, m)) for m in re.findall(r"logical (\d+)\s+len (\d+)\s+physical (\d+)", out)]
    return ino, exts


def inode_offset(img, ino):
    s = super_info(img)
    return (s["itable"] + (ino - 1) // 8) * 4096 + ((ino - 1) % 8) * 512


def patch(img, off, fn):
    with open(img, "r+b") as f:
        f.seek(off)
        b = bytearray(f.read(fn.size))
        fn(b)
        f.seek(off)
        f.write(b)


def flip(img, off, mask=0x01):
    def f(b):
        b[0] ^= mask
    f.size = 1
    patch(img, off, f)


def fsck(img):
    r = run(tool("fsck.xvsx"), img)
    return r.returncode, (r.stdout + r.stderr).decode()


def main():
    build()
    random.seed(12345)

    # ------------------------------------------------ round trip
    print("round trip")
    src = os.path.join(work, "src")
    os.makedirs(src)
    files = {}
    sizes = [0, 1, 100, 255, 256, 257, 4095, 4096, 4097, 8192, 100000, 3000000]
    for sz in sizes:
        files["/d/f_%d" % sz] = os.urandom(sz)
    files["/d/" + "n" * 255] = b"long name ok"
    files["/d/café-日本"] = b"utf-8 name"
    for i in range(400):                      # forces a multi-block directory
        files["/many/file_%04d" % i] = ("content %d\n" % i).encode()
    img = os.path.join(work, "t.img")
    args = [tool("mkfs.xvsx"), img, "32M", "--default-layout", "--mkdir", "/d", "--mkdir", "/many"]
    for p, data in files.items():
        hp = os.path.join(src, str(abs(hash(p))))
        with open(hp, "wb") as f:
            f.write(data)
        args += ["--add", p + "=" + hp]
    args += ["--symlink", "/d/link=/d/f_100"]
    r = run(*args)
    check(r.returncode == 0, "mkfs.xvsx builds an image with %d files" % len(files))
    rc, out = fsck(img)
    check(rc == 0, "fsck reports the fresh image clean")
    if rc:
        print(out)
    bad = 0
    for p, data in files.items():
        got = run(tool("xvsx-dump"), img, "cat", p).stdout
        if got != data:
            bad += 1
            print("   mismatch:", p[:40])
    check(bad == 0, "every file reads back byte-for-byte (%d files)" % len(files))
    check(run(tool("xvsx-dump"), img, "cat", "/d/link").stdout == b"/d/f_100", "inline symlink target")
    ls = run(tool("xvsx-dump"), img, "ls", "/many").stdout.decode().strip().splitlines()
    check(len(ls) == 402, "directory with 400 entries lists 402 (with . and ..)")
    ino, exts = stat_info(img, "/many")
    check(len(exts) >= 1 and sum(e[1] for e in exts) >= 2, "directory spans more than one block")
    check(b"inline" in run(tool("xvsx-dump"), img, "stat", "/d/f_100").stdout, "100-byte file is stored inline")
    check(b"extents" in run(tool("xvsx-dump"), img, "stat", "/d/f_257").stdout, "257-byte file uses an extent")

    # ------------------------------------------------ fragmentation: extent tree depth 1
    print("fragmented files (extent tree with a separate node)")
    big = os.urandom(3000000)
    bp = os.path.join(work, "big.bin")
    open(bp, "wb").write(big)
    fimg = os.path.join(work, "frag.img")
    run(tool("mkfs.xvsx"), fimg, "16M", "--default-layout", "--max-run", "1", "--add", "/big=" + bp)
    ino, exts = stat_info(fimg, "/big")
    rc, out = fsck(fimg)
    check(rc == 0, "fsck clean with a many-extent file")
    if rc:
        print(out)
    check(run(tool("xvsx-dump"), fimg, "cat", "/big").stdout == big, "fragmented file reads back")
    node = None
    # the extent node block is the one referenced from the inode root; find it
    with open(fimg, "rb") as f:
        f.seek(inode_offset(fimg, ino) + 128)
        root = f.read(256)
    magic, entries, mx, depth = struct.unpack_from("<HHHH", root, 0)
    check(depth == 1 and entries >= 2, "extent tree has depth 1 with %d nodes" % entries)
    node = struct.unpack_from("<III", root, 16)[1]

    # ------------------------------------------------ corruption detection
    print("corruption is detected")

    def corrupt(name, mutate, expect, code=None):
        c = os.path.join(work, name + ".img")
        shutil.copy(img if not name.startswith("frag") else fimg, c)
        mutate(c)
        rc, out = fsck(c)
        ok = rc != 0 and (re.search(expect, out) is not None) and (code is None or rc == code)
        check(ok, name + " -> fsck says: " + expect)
        if not ok:
            print("      rc=%d  %s" % (rc, out.strip().splitlines()[:3]))

    s = super_info(img)
    f_ino, f_exts = stat_info(img, "/d/f_100000")
    r_ino, r_exts = stat_info(img, "/many")
    reg_ino = stat_info(img, "/d/f_4096")[0]

    corrupt("inode_bit_flip", lambda c: flip(c, inode_offset(c, f_ino) + 20), r"inode %d: checksum" % f_ino)
    corrupt("super_bit_flip", lambda c: flip(c, 4096 + 24), r"superblock checksum", code=2)
    corrupt("bad_magic", lambda c: flip(c, 4096 + 2), r"bad magic|not an xvsXfs", code=2)
    corrupt("dir_block_bit_flip", lambda c: flip(c, r_exts[0][2] * 4096 + 9), r"checksum mismatch")
    corrupt("bbm_clear_used", lambda c: flip(c, s["bbm"] * 4096 + (f_exts[0][2] // 8), 1 << (f_exts[0][2] % 8)),
            r"in use but marked free")
    last = 4000                       # a block far beyond anything allocated
    corrupt("bbm_set_unused", lambda c: flip(c, s["bbm"] * 4096 + last // 8, 1 << (last % 8)),
            r"marked used but nothing")
    corrupt("ibm_set_unused", lambda c: flip(c, s["ibm"] * 4096 + 500 // 8, 1 << (500 % 8)),
            r"orphan|free inodes")
    corrupt("ibm_clear_used", lambda c: flip(c, s["ibm"] * 4096 + (f_ino - 1) // 8, 1 << ((f_ino - 1) % 8)),
            r"marked free in the inode bitmap")
    corrupt("truncated_image", lambda c: os.truncate(c, 2 * 1024 * 1024), r"smaller than the filesystem")
    corrupt("frag_node_bit_flip", lambda c: flip(c, node * 4096 + 40), r"extent node checksum")
    corrupt("journal_magic", lambda c: flip(c, 2 * 4096 + 1), r"journal superblock has bad magic")

    def forge_nlink(c):                # valid checksum, wrong link count
        off = inode_offset(c, reg_ino)

        def fn(b):
            struct.pack_into("<I", b, 12, 7)
            struct.pack_into("<I", b, 88, 0)
            struct.pack_into("<I", b, 88, crc32c(bytes(b)))
        fn.size = 512
        patch(c, off, fn)
    corrupt("forged_nlink", forge_nlink, r"link count 7")

    def forge_size(c):                 # valid checksum, size beyond the mapping
        off = inode_offset(c, reg_ino)

        def fn(b):
            struct.pack_into("<Q", b, 16, 10 ** 9)
            struct.pack_into("<I", b, 88, 0)
            struct.pack_into("<I", b, 88, crc32c(bytes(b)))
        fn.size = 512
        patch(c, off, fn)
    corrupt("forged_size", forge_size, r"exceeds mapped space")

    def forge_double_claim(c):         # two files pointing at the same data
        a = inode_offset(c, reg_ino)
        b_ino = stat_info(img, "/d/f_8192")[0]
        with open(c, "r+b") as f:
            f.seek(a + 128)
            ext_a = f.read(256)
            f.seek(inode_offset(c, b_ino))
            ib = bytearray(f.read(512))
            ib[128:384] = ext_a
            struct.pack_into("<I", ib, 88, 0)
            struct.pack_into("<I", ib, 88, crc32c(bytes(ib)))
            f.seek(inode_offset(c, b_ino))
            f.write(ib)
    corrupt("double_claim", forge_double_claim, r"claimed more than once")

    print("\n%d passed, %d failed" % (passed, failed))
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
