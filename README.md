*MY TRY MAKE NO GNU LINUX-like kernel*

Notux is a kernel I created primarily for my own use; it is a kernel that attempts to emulate the Linux ABI. Regarding 
AI, I will state upfront that I did use it for bug fixes and for examples involving MSR, syscalls, and sysret.
I also incorporated code from xv6-with credit due to Frans Kaashoek, Robert Morris, and Russ Cox (I don’t recall the exact version,
but it is included here). Additionally, there are snippets of code from FreeBSD and OpenBSD. I would also like to acknowledge the University of California,
Berkeley, as well as Jordan Hubbard, Nate Williams, Rod Grimes, Bill Jolitz, Lynne Jolitz, Poul-Henning Kamp, 
David Greenman, Bill Paul, Peter Wemm,
Theo de Raadt, Bob Beck, Todd Miller, Mark Espie, and Otto Moerbeek

Build commands: "make" for the kernel
"make iso" for an .iso file
"make run-disk" to boot with a disk
"make run" to boot without one
The kernel is raw and almost entirely unsuitable for use.
