import subprocess, sys, time, os, re, select, tty, termios
# Drives a running notux over its serial port (QEMU pty), for scripted interactive tests.
# usage: tools/qemu_drive.py DISK.img [ISO] -- "wait:8" "send:ls\r" ...   (send: understands \r \x1b ...)
# Prints everything the guest wrote. The disk is used in place: pass a copy.
img = sys.argv[1]; script = sys.argv[2:]   # sequence of "send:TEXT" / "wait:SECS"
q = subprocess.Popen(["qemu-system-x86_64","-cdrom",os.environ.get("NOTUX_ISO","notux.iso"),"-m","256","-display","none","-no-reboot","-monitor","none",
    "-serial","pty","-device","ahci,id=ahci0","-drive",f"id=disk0,file={img},if=none,format=raw","-device","ide-hd,drive=disk0,bus=ahci0.0"],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
line = q.stdout.readline()
m = re.search(r"/dev/pts/\d+", line)
pty = m.group(0)
fd = os.open(pty, os.O_RDWR | os.O_NOCTTY)
tty.setraw(fd)
out = b""
def pump(t):
    global out
    end = time.time() + t
    while time.time() < end:
        r,_,_ = select.select([fd],[],[],0.1)
        if r:
            try: out += os.read(fd, 4096)
            except OSError: pass
for s in script:
    k, _, v = s.partition(":")
    if k == "wait": pump(float(v))
    elif k == "send":
        os.write(fd, v.encode().decode("unicode_escape").encode("latin1")); pump(0.05)
q.kill()
sys.stdout.write(out.decode("latin1"))
