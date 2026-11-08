bits 64
global iflush
section .text
iflush:
    lidt [rdi]
    ret
